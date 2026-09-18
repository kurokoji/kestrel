#include "TreePane.h"
#include "IconCache.h"

#include <shlobj.h>
#include <windowsx.h>
#include <functional>

namespace {
bool isDotOrDotDot(const wchar_t* name) {
    return name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0));
}
}  // namespace

bool TreePane::create(HWND parent, HINSTANCE hInstance, int controlId) {
    parentWnd_ = parent;
    hwnd_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT |
            TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), hInstance, nullptr);
    if (!hwnd_) return false;

    SendMessageW(hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

    if (HIMAGELIST himl = IconCache::instance().systemImageList()) {
        TreeView_SetImageList(hwnd_, himl, TVSIL_NORMAL);
    }

    addRootItems();
    return true;
}

HTREEITEM TreePane::addNode(HTREEITEM parent, const std::wstring& text, const std::wstring& path,
                             bool likelyHasChildren) {
    auto* data = new NodeData{path, false};
    const int icon = IconCache::instance().iconForDirectory();

    TVINSERTSTRUCTW tvis{};
    tvis.hParent = parent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
    tvis.item.pszText = const_cast<LPWSTR>(text.c_str());
    tvis.item.lParam = reinterpret_cast<LPARAM>(data);
    tvis.item.cChildren = likelyHasChildren ? 1 : 0;
    tvis.item.iImage = icon;
    tvis.item.iSelectedImage = icon;
    return TreeView_InsertItem(hwnd_, &tvis);
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

    // cChildren must already claim "has children" here - comctl32 gates
    // TVE_EXPAND on that hint, not just on whether child items actually
    // exist yet, so inserting the real drive nodes below and expanding
    // wouldn't otherwise show anything.
    HTREEITEM thisPC = addNode(nullptr, L"PC", L"", true);

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

    std::wstring search = data->path;
    if (search.back() != L'\\') search += L'\\';
    search += L'*';

    bool any = false;
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileExW(search.c_str(), FindExInfoBasic, &fd,
                                     FindExSearchLimitToDirectories, nullptr, 0);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (isDotOrDotDot(fd.cFileName)) continue;
            if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;

            std::wstring childPath = data->path;
            if (childPath.back() != L'\\') childPath += L'\\';
            childPath += fd.cFileName;
            addNode(item, fd.cFileName, childPath, true);
            any = true;
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (!any) {
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
            delete reinterpret_cast<NodeData*>(nmtv->itemOld.lParam);
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
    HTREEITEM best = nullptr;

    std::function<bool(HTREEITEM)> walk = [&](HTREEITEM item) -> bool {
        while (item) {
            NodeData* data = dataOf(item);
            if (data && !data->path.empty() && _wcsicmp(data->path.c_str(), path.c_str()) == 0) {
                best = item;
                return true;
            }
            if (HTREEITEM child = TreeView_GetChild(hwnd_, item)) {
                if (walk(child)) return true;
            }
            item = TreeView_GetNextSibling(hwnd_, item);
        }
        return false;
    };

    walk(TreeView_GetRoot(hwnd_));
    if (best) {
        TreeView_SelectItem(hwnd_, best);
        TreeView_EnsureVisible(hwnd_, best);
    }
}
