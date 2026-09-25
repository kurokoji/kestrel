#include "Strings.h"
#include "Language.h"

#include <array>
#include <cstddef>

namespace {

constexpr size_t kCount = static_cast<size_t>(StringId::Count);

// clang-format off
constexpr std::array<const wchar_t*, kCount> kEn = {
    L"&File",                        // MenuBarFile
    L"&Edit",                        // MenuBarEdit
    L"&View",                        // MenuBarView
    L"&Go",                          // MenuBarGo
    L"&Tools",                       // MenuBarTools
    L"&Help",                        // MenuBarHelp

    L"&View\tF3",                    // MenuFileView
    L"&Edit\tF4",                    // MenuFileEdit
    L"&Copy\tF5",                    // MenuFileCopy
    L"&Move\tF6",                    // MenuFileMove
    L"&New Folder\tF7",              // MenuFileMkdir
    L"&Delete\tF8",                  // MenuFileDelete
    L"&Rename\tF2",                  // MenuFileRename
    L"&Properties\tAlt+Enter",       // MenuFileProperties
    L"E&xit",                        // MenuFileExit

    L"&Undo\tCtrl+Z",                // MenuEditUndo
    L"Undo: {}\tCtrl+Z",             // MenuEditUndoWithTarget
    L"&Copy\tCtrl+C",                // MenuEditCopy
    L"Cu&t\tCtrl+X",                 // MenuEditCut
    L"&Paste\tCtrl+V",               // MenuEditPaste
    L"Select &All\tCtrl+A",          // MenuEditSelectAll
    L"&Find\tCtrl+F",                // MenuEditFind

    L"&Refresh",                     // MenuViewRefresh
    L"&Tree\tCtrl+Shift+T",          // MenuViewTree
    L"&Single Pane\tCtrl+U",         // MenuViewSinglePane
    L"&Hidden Files\tCtrl+H",        // MenuViewHidden

    L"&Back\tAlt+Left",              // MenuGoBack
    L"&Forward\tAlt+Right",          // MenuGoForward
    L"&Up\tAlt+Up",                  // MenuGoUp
    L"&New Tab\tCtrl+T",             // MenuTabNew
    L"&Duplicate Tab",               // MenuTabDuplicate
    L"&Close Tab\tCtrl+W",           // MenuTabClose
    L"Ne&xt Tab\tCtrl+Tab",          // MenuTabNext
    L"&Previous Tab\tCtrl+Shift+Tab",// MenuTabPrev

    L"&Font...",                     // MenuToolsFont
    L"&Settings",                    // MenuToolsSettings
    L"&Language",                    // MenuToolsLanguage
    L"&Auto (System)",               // MenuLanguageAuto
    L"&English",                     // MenuLanguageEnglish
    L"日本語(&J)",                    // MenuLanguageJapanese

    L"&Name",                        // MenuSortName
    L"&Type",                        // MenuSortType
    L"&Size",                        // MenuSortSize
    L"&Modified",                    // MenuSortModified
    L"&Descending by Default",       // MenuSortDescending
    L"S&ort Order (Default)",        // MenuSettingsSortOrder

    L"&About Kestrel...",            // MenuHelpAbout

    L"Open in New &Tab",             // MenuCtxOpenInNewTab
    L"Rena&me\tF2",                  // MenuCtxRename
    L"&Refresh",                     // MenuCtxRefresh
    L"&New Folder\tF7",              // MenuCtxNewFolder

    L"Single Pane",                  // ToolbarSinglePane
    L"Back",                         // ToolbarBack
    L"Forward",                      // ToolbarForward
    L"Up",                           // ToolbarUp
    L"Refresh",                      // ToolbarRefresh

    L"About Kestrel",                                   // AboutTitle
    L"Kestrel {}\nA lightweight Win32 file manager.",     // AboutBody

    L"{} files | {} folders | {} selected | {}",  // StatusFilesFoldersSelected
    L"{} files | {} folders",                      // StatusFilesFolders
    L"{} free / {}",                                // FreeSpace

    L"Some items from \"{}\" could not be undone.\nThey may have been moved, changed, or deleted since.",  // UndoPartialFailure
    L"Location not found:\n{}",      // ErrorLocationNotFound
    L"Access denied:\n{}",           // ErrorAccessDenied

    L"New Tab (Ctrl+T)",             // TipNewTab
    L"Duplicate Tab",                // TipDuplicateTab
    L"&Empty Recycle Bin",           // ButtonEmptyRecycleBin

    L"Name",                         // ColumnName
    L"Type",                         // ColumnType
    L"Size",                         // ColumnSize
    L"Modified",                     // ColumnModified

    L"New Folder",                   // NewFolderBaseName

    L"Desktop",                      // FolderDesktop
    L"User Profile",                 // FolderUserProfile
    L"Documents",                    // FolderDocuments
    L"Downloads",                    // FolderDownloads
    L"Pictures",                     // FolderPictures
    L"Music",                        // FolderMusic
    L"Videos",                       // FolderVideos
    L"Recycle Bin",                  // RecycleBin
    L"Loading...",                   // TreeLoading

    L"Rename",                       // OpRename
    L"Copy",                         // OpCopy
    L"Move",                         // OpMove
    L"Delete",                       // OpDelete

    L"File Folder",                  // TypeFileFolder
    L"File",                         // TypeFile
    L"{} File",                      // TypeFileWithExtension

    L"Folder",                       // PreviewFolder
    L"No Preview",                   // PreviewNone

    L"Cancel",                       // DialogCancel
};

constexpr std::array<const wchar_t*, kCount> kJa = {
    L"ファイル(&F)",                  // MenuBarFile
    L"編集(&E)",                      // MenuBarEdit
    L"表示(&V)",                      // MenuBarView
    L"移動(&G)",                      // MenuBarGo
    L"ツール(&T)",                    // MenuBarTools
    L"ヘルプ(&H)",                    // MenuBarHelp

    L"表示(&V)\tF3",                  // MenuFileView
    L"編集(&E)\tF4",                  // MenuFileEdit
    L"コピー(&C)\tF5",                // MenuFileCopy
    L"移動(&M)\tF6",                  // MenuFileMove
    L"新しいフォルダー(&F)\tF7",       // MenuFileMkdir
    L"削除(&D)\tF8",                  // MenuFileDelete
    L"名前の変更(&R)\tF2",             // MenuFileRename
    L"プロパティ(&P)\tAlt+Enter",      // MenuFileProperties
    L"終了(&X)",                      // MenuFileExit

    L"元に戻す(&U)\tCtrl+Z",           // MenuEditUndo
    L"元に戻す: {}(&U)\tCtrl+Z",       // MenuEditUndoWithTarget
    L"コピー(&C)\tCtrl+C",            // MenuEditCopy
    L"切り取り(&T)\tCtrl+X",          // MenuEditCut
    L"貼り付け(&P)\tCtrl+V",          // MenuEditPaste
    L"すべて選択(&A)\tCtrl+A",         // MenuEditSelectAll
    L"検索(&F)\tCtrl+F",              // MenuEditFind

    L"更新(&R)",                      // MenuViewRefresh
    L"ツリー(&T)\tCtrl+Shift+T",       // MenuViewTree
    L"シングルペイン表示(&S)\tCtrl+U", // MenuViewSinglePane
    L"隠しファイル(&H)\tCtrl+H",       // MenuViewHidden

    L"戻る(&B)\tAlt+Left",            // MenuGoBack
    L"進む(&F)\tAlt+Right",           // MenuGoForward
    L"上へ(&U)\tAlt+Up",              // MenuGoUp
    L"新しいタブ(&N)\tCtrl+T",         // MenuTabNew
    L"タブを複製(&D)",                // MenuTabDuplicate
    L"タブを閉じる(&C)\tCtrl+W",       // MenuTabClose
    L"次のタブ(&X)\tCtrl+Tab",         // MenuTabNext
    L"前のタブ(&P)\tCtrl+Shift+Tab",   // MenuTabPrev

    L"フォント(&F)...",               // MenuToolsFont
    L"設定(&S)",                      // MenuToolsSettings
    L"言語(&L)",                      // MenuToolsLanguage
    L"自動（OSに合わせる）(&A)",       // MenuLanguageAuto
    L"English(&E)",                  // MenuLanguageEnglish
    L"日本語(&J)",                    // MenuLanguageJapanese

    L"名前(&N)",                      // MenuSortName
    L"種類(&T)",                      // MenuSortType
    L"サイズ(&S)",                    // MenuSortSize
    L"更新日時(&M)",                  // MenuSortModified
    L"降順を既定にする(&D)",           // MenuSortDescending
    L"並び順(既定)(&O)",              // MenuSettingsSortOrder

    L"Kestrelについて(&A)...",        // MenuHelpAbout

    L"新しいタブで開く(&T)",           // MenuCtxOpenInNewTab
    L"名前の変更(&M)\tF2",             // MenuCtxRename
    L"最新の情報に更新(&E)",           // MenuCtxRefresh
    L"新しいフォルダー(&N)\tF7",       // MenuCtxNewFolder

    L"1ペイン",                       // ToolbarSinglePane
    L"戻る",                          // ToolbarBack
    L"進む",                          // ToolbarForward
    L"上へ",                          // ToolbarUp
    L"更新",                          // ToolbarRefresh

    L"Kestrelについて",                                    // AboutTitle
    L"Kestrel {}\n軽量な Win32 ファイラーです。",           // AboutBody

    L"{} 個のファイル | {} 個のフォルダー | {} 個選択 | {}",  // StatusFilesFoldersSelected
    L"{} 個のファイル | {} 個のフォルダー",                   // StatusFilesFolders
    L"{} 空き / {}",                                        // FreeSpace

    L"「{}」を元に戻せなかった項目があります。\nその後に移動・変更・削除された可能性があります。",  // UndoPartialFailure
    L"場所が見つかりません:\n{}",      // ErrorLocationNotFound
    L"アクセスできません:\n{}",        // ErrorAccessDenied

    L"新しいタブ (Ctrl+T)",           // TipNewTab
    L"タブを複製",                    // TipDuplicateTab
    L"ゴミ箱を空にする(&E)",           // ButtonEmptyRecycleBin

    L"名前",                          // ColumnName
    L"種類",                          // ColumnType
    L"サイズ",                        // ColumnSize
    L"更新日時",                      // ColumnModified

    L"新しいフォルダー",               // NewFolderBaseName

    L"デスクトップ",                   // FolderDesktop
    L"ユーザープロファイル",           // FolderUserProfile
    L"ドキュメント",                   // FolderDocuments
    L"ダウンロード",                   // FolderDownloads
    L"ピクチャ",                      // FolderPictures
    L"ミュージック",                   // FolderMusic
    L"ビデオ",                        // FolderVideos
    L"ゴミ箱",                        // RecycleBin
    L"読み込み中...",                  // TreeLoading

    L"名前の変更",                    // OpRename
    L"コピー",                        // OpCopy
    L"移動",                          // OpMove
    L"削除",                          // OpDelete

    L"ファイル フォルダー",            // TypeFileFolder
    L"ファイル",                      // TypeFile
    L"{} ファイル",                   // TypeFileWithExtension

    L"フォルダー",                    // PreviewFolder
    L"プレビューなし",                 // PreviewNone

    L"キャンセル",                    // DialogCancel
};
// clang-format on

static_assert(kEn.size() == kCount);
static_assert(kJa.size() == kCount);

}  // namespace

const wchar_t* tr(StringId id) {
    const auto& table = currentLanguage() == Language::Ja ? kJa : kEn;
    return table[static_cast<size_t>(id)];
}
