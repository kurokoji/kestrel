#include "TreePane.h"
#include "FileDropTarget.h"
#include "IconCache.h"
#include "Messages.h"
#include "Types.h"

#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <functional>
#include <thread>

namespace {
bool isDotOrDotDot(const wchar_t* name) {
    return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
}

std::wstring lowerCopy(std::wstring s) {
    std::ranges::transform(s, s.begin(), ::towlower);
    return s;
}
}  // namespace

bool TreePane::create(HWND parent, HINSTANCE hInstance, int controlId) {
    parentWnd_ = parent;
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT |
            TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), hInstance, nullptr);
    if (!hwnd_) return false;

    TreeView_SetExtendedStyle(hwnd_, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    if (HIMAGELIST himl = IconCache::instance().systemImageList()) {
        TreeView_SetImageList(hwnd_, himl, TVSIL_NORMAL);
    }

    addRootItems();

    FileDropTarget::registerOn(hwnd_, {
        [this](POINT pt) { return dropHitTest(pt); },
        [this](intptr_t key) { TreeView_SelectDropTarget(hwnd_, reinterpret_cast<HTREEITEM>(key)); },
    });
    return true;
}

FileDropTarget::Hit TreePane::dropHitTest(POINT pt) {
    TVHITTESTINFO hit{};
    hit.pt = pt;
    const HTREEITEM item = TreeView_HitTest(hwnd_, &hit);
    if (!item || !(hit.flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON))) {
        dropHoverItem_ = nullptr;
        return {};
    }

    // Hovering a collapsed node for a moment expands it, as in Explorer, so
    // a drag can reach folders that aren't visible yet.
    const ULONGLONG now = GetTickCount64();
    if (item != dropHoverItem_) {
        dropHoverItem_ = item;
        dropHoverSince_ = now;
    } else if (now - dropHoverSince_ >= kDropExpandDelayMs &&
               !(TreeView_GetItemState(hwnd_, item, TVIS_EXPANDED) & TVIS_EXPANDED)) {
        TreeView_Expand(hwnd_, item, TVE_EXPAND);
    }

    const NodeData* data = dataOf(item);
    // "PC" itself isn't a folder you can drop into; its drives are.
    if (!data || data->path.empty() || data->path == kThisPcPath) return {};
    return {{data->path}, reinterpret_cast<intptr_t>(item)};
}

HTREEITEM TreePane::addNode(HTREEITEM parent, const std::wstring& text, const std::wstring& path,
                             bool likelyHasChildren) {
    auto* data = new NodeData{path, false};
    const int icon = path.empty() ? IconCache::instance().iconForDirectory()
                                   : IconCache::instance().iconForPath(path);

    TVINSERTSTRUCTW tvis{};
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvis.item.pszText = const_cast<LPWSTR>(text.c_str());
    tvis.item.lParam = reinterpret_cast<LPARAM>(data);
    tvis.item.cChildren = likelyHasChildren ? 1 : 0;
    tvis.item.iImage = icon;
    tvis.item.iSelectedImage = icon;
    const HTREEITEM item = TreeView_InsertItem(hwnd_, &tvis);
    // try_emplace, not []: a path can legitimately end up in the tree
    // twice - e.g. the root "ダウンロード" shortcut and, once "ユーザー
    // プロファイル" is expanded, its real Downloads subfolder child,
    // both pointing at the same real path. The root shortcuts are always
    // inserted first (addRootItems runs before any populateChildren),
    // so keeping whichever entry got there first means trySelectPath()
    // (and therefore which node lights up after a navigation) prefers
    // the shortcut over a same-path descendant instead of whichever was
    // inserted most recently.
    if (!path.empty()) pathIndex_.try_emplace(lowerCopy(path), item);
    return item;
}

void TreePane::addRootItems() {
    PWSTR kf = nullptr;
    auto addKnownFolder = [&](REFKNOWNFOLDERID id, const wchar_t* label) {
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &kf))) {
            addNode(nullptr, label, kf, true);
            CoTaskMemFree(kf);
            kf = nullptr;
        }
    };

    addKnownFolder(FOLDERID_Desktop, L"デスクトップ");
    addKnownFolder(FOLDERID_Profile, L"ユーザープロファイル");
    addKnownFolder(FOLDERID_Documents, L"ドキュメント");
    addKnownFolder(FOLDERID_Downloads, L"ダウンロード");
    addKnownFolder(FOLDERID_Pictures, L"ピクチャ");
    addKnownFolder(FOLDERID_Music, L"ミュージック");
    addKnownFolder(FOLDERID_Videos, L"ビデオ");

    // Leaf node (no expand arrow, cChildren=false) - its contents are
    // listed in the file pane like any other navigable node, but aren't a
    // *tree* of subfolders, so there's nothing here to lazily expand.
    addNode(nullptr, L"ゴミ箱", kRecycleBinPath, false);

    // cChildren must already claim "has children" here - comctl32 gates
    // TVE_EXPAND on that hint, not just on whether child items actually
    // exist yet, so inserting the real drive nodes below and expanding
    // wouldn't otherwise show anything.
    // kThisPcPath (not empty) so clicking "PC" itself navigates the active
    // pane to a synthetic drive listing (DirectoryModel special-cases
    // this path) - matching what other file managers do for "This PC".
    HTREEITEM thisPC = addNode(nullptr, L"PC", kThisPcPath, true);
    // Its children (the drives) are added right here, synchronously, not
    // lazily via populateChildren - mark them already-loaded so a later
    // collapse+re-expand doesn't try to enumerate kThisPcPath as a tree
    // node too and duplicate them.
    dataOf(thisPC)->childrenLoaded = true;

    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        wchar_t root[4] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', 0};
        const UINT type = GetDriveTypeW(root);
        if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR) continue;
        addNode(thisPC, root, root, true);
    }

    TreeView_Expand(hwnd_, thisPC, TVE_EXPAND);
}

TreePane::NodeData* TreePane::dataOf(HTREEITEM item) const {
    if (!item) return nullptr;
    TVITEMW tvi{};
    tvi.mask = TVIF_PARAM | TVIF_HANDLE;
    tvi.hItem = item;
    if (!TreeView_GetItem(hwnd_, &tvi)) return nullptr;
    return reinterpret_cast<NodeData*>(tvi.lParam);
}

void TreePane::populateChildren(HTREEITEM item) {
    NodeData* data = dataOf(item);
    if (!data || data->childrenLoaded) return;
    data->childrenLoaded = true;
    if (data->path.empty()) return;

    // Placeholder so the expand shows something immediately instead of
    // looking like it did nothing while enumeration runs in the
    // background; handleChildrenResult() removes it once real results
    // (or "no subfolders") arrive.
    addNode(item, L"読み込み中...", L"", false);

    std::thread(&TreePane::enumerateChildrenWorker, data->path, item, parentWnd_).detach();
}

void TreePane::enumerateChildrenWorker(std::wstring path, HTREEITEM item, HWND notifyWnd) {
    auto result = std::make_unique<TreeChildrenResult>();
    result->item = item;

    std::wstring search = path;
    if (search.back() != L'\\') search += L'\\';
    search += L'*';

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileExW(search.c_str(), FindExInfoBasic, &fd,
                                     FindExSearchLimitToDirectories, nullptr, 0);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (isDotOrDotDot(fd.cFileName)) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;

            std::wstring childPath = path;
            if (childPath.back() != L'\\') childPath += L'\\';
            childPath += fd.cFileName;
            result->children.emplace_back(fd.cFileName, std::move(childPath));
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    PostMessageW(notifyWnd, WM_APP_TREE_CHILDREN, 0, reinterpret_cast<LPARAM>(result.release()));
}

void TreePane::handleChildrenResult(std::unique_ptr<TreeChildrenResult> result) {
    const HTREEITEM item = result->item;

    // Remove the "読み込み中..." placeholder populateChildren() inserted -
    // it's always the (only) first child at this point.
    if (HTREEITEM placeholder = TreeView_GetChild(hwnd_, item)) {
        TreeView_DeleteItem(hwnd_, placeholder);
    }

    for (const auto& [name, childPath] : result->children) {
        addNode(item, name, childPath, true);
    }

    if (result->children.empty()) {
        TVITEMW tvi{};
        tvi.mask = TVIF_HANDLE | TVIF_CHILDREN;
        tvi.hItem = item;
        tvi.cChildren = 0;
        TreeView_SetItem(hwnd_, &tvi);
    }
}

void TreePane::navigateFromItem(HTREEITEM item) {
    NodeData* data = dataOf(item);
    if (data && !data->path.empty() && onNavigate) {
        onNavigate(data->path);
    }
}

LRESULT TreePane::handleNotify(NMHDR* nmhdr) {
    if (!nmhdr || nmhdr->hwndFrom != hwnd_) return 0;

    switch (nmhdr->code) {
        case TVN_ITEMEXPANDINGW: {
            auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(nmhdr);
            if (nmtv->action == TVE_EXPAND) {
                populateChildren(nmtv->itemNew.hItem);
            }
            return 0;
        }
        case TVN_DELETEITEMW: {
            auto* nmtv = reinterpret_cast<NMTREEVIEWW*>(nmhdr);
            auto* data = reinterpret_cast<NodeData*>(nmtv->itemOld.lParam);
            if (data && !data->path.empty()) pathIndex_.erase(lowerCopy(data->path));
            delete data;
            return 0;
        }
        case NM_CLICK:
        case NM_DBLCLK: {
            // Don't trust TreeView_GetSelection() here - depending on
            // internal click/drag-threshold handling, the notification can
            // arrive before the control has actually committed the new
            // selection, which would make us silently navigate to the
            // *previous* item instead. Hit-test the real click point
            // (NM_CLICK carries no point of its own, so pull it from
            // GetMessagePos()) so we always act on the item actually
            // clicked.
            DWORD msgPos = GetMessagePos();
            POINT pt{GET_X_LPARAM(msgPos), GET_Y_LPARAM(msgPos)};
            ScreenToClient(hwnd_, &pt);
            TVHITTESTINFO hit{};
            hit.pt = pt;
            if (HTREEITEM item = TreeView_HitTest(hwnd_, &hit)) {
                if (hit.flags & (TVHT_ONITEMLABEL | TVHT_ONITEMICON | TVHT_ONITEM)) {
                    TreeView_SelectItem(hwnd_, item);
                    navigateFromItem(item);
                }
            }
            return 0;
        }
        case TVN_KEYDOWN: {
            auto* kd = reinterpret_cast<NMTVKEYDOWN*>(nmhdr);
            if (kd->wVKey == VK_RETURN) {
                if (HTREEITEM sel = TreeView_GetSelection(hwnd_)) {
                    navigateFromItem(sel);
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

void TreePane::trySelectPath(const std::wstring& path) {
    // pathIndex_ only has nodes that have actually been inserted (i.e.
    // already-expanded/loaded ancestors) - a path under a never-expanded
    // node simply won't be found, matching the existing "best-effort,
    // never force enumeration" contract.
    const auto it = pathIndex_.find(lowerCopy(path));
    if (it == pathIndex_.end()) return;

    TreeView_SelectItem(hwnd_, it->second);
    TreeView_EnsureVisible(hwnd_, it->second);
}
