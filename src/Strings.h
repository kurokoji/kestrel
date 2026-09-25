#pragma once

// The app's translatable UI strings. Two constexpr tables (English,
// Japanese; see Strings.cpp) indexed by StringId - tr() just picks the
// table for currentLanguage() and indexes into it, so it's an O(1) array
// lookup, not a hash/map lookup: StringId is already a dense integer key,
// there's nothing a hash would buy here. Entries are const wchar_t*
// pointing at string-literal storage (not std::wstring) so the tables stay
// constexpr - no heap allocation, no per-call construction cost, even
// though tr() is called from hot paths like ListView owner-draw and menu
// rebuilds.
//
// Format strings (status bar counts, error messages with a path/name
// spliced in) use std::format placeholders ("{}") - callers format() them
// themselves rather than this header taking on formatting concerns.
enum class StringId {
    MenuBarFile,
    MenuBarEdit,
    MenuBarView,
    MenuBarGo,
    MenuBarTools,
    MenuBarHelp,

    MenuFileView,
    MenuFileEdit,
    MenuFileCopy,
    MenuFileMove,
    MenuFileMkdir,
    MenuFileDelete,
    MenuFileRename,
    MenuFileProperties,
    MenuFileExit,

    MenuEditUndo,
    MenuEditUndoWithTarget,  // "{}" = Undo::describe(kind)
    MenuEditCopy,
    MenuEditCut,
    MenuEditPaste,
    MenuEditSelectAll,
    MenuEditFind,

    MenuViewRefresh,
    MenuViewTree,
    MenuViewSinglePane,
    MenuViewHidden,

    MenuGoBack,
    MenuGoForward,
    MenuGoUp,
    MenuTabNew,
    MenuTabDuplicate,
    MenuTabClose,
    MenuTabNext,
    MenuTabPrev,

    MenuToolsFont,
    MenuToolsSettings,
    MenuToolsLanguage,
    MenuLanguageAuto,
    MenuLanguageEnglish,
    MenuLanguageJapanese,

    MenuSortName,
    MenuSortType,
    MenuSortSize,
    MenuSortModified,
    MenuSortDescending,
    MenuSettingsSortOrder,

    MenuHelpAbout,

    // Context-menu-only variants (different mnemonic letter than their File
    // menu counterpart, since they share a popup with shell-supplied items).
    MenuCtxOpenInNewTab,
    MenuCtxRename,
    MenuCtxRefresh,
    MenuCtxNewFolder,

    ToolbarSinglePane,
    ToolbarBack,
    ToolbarForward,
    ToolbarUp,
    ToolbarRefresh,

    AboutTitle,
    AboutBody,

    StatusFilesFoldersSelected,  // "{} {} {} {}" = fileCount, dirCount, selectedCount, selectedSize
    StatusFilesFolders,          // "{} {}" = fileCount, dirCount
    FreeSpace,                   // "{} {}" = formatted free size, formatted total size

    UndoPartialFailure,  // "{}" = Undo::describe(kind)
    ErrorLocationNotFound,  // "{}" = path
    ErrorAccessDenied,      // "{}" = path

    TipNewTab,
    TipDuplicateTab,
    ButtonEmptyRecycleBin,

    ColumnName,
    ColumnType,
    ColumnSize,
    ColumnModified,

    NewFolderBaseName,

    FolderDesktop,
    FolderUserProfile,
    FolderDocuments,
    FolderDownloads,
    FolderPictures,
    FolderMusic,
    FolderVideos,
    RecycleBin,
    TreeLoading,

    OpRename,
    OpCopy,
    OpMove,
    OpDelete,

    TypeFileFolder,
    TypeFile,
    TypeFileWithExtension,  // "{}" = uppercased extension

    PreviewFolder,
    PreviewNone,

    DialogCancel,

    Count
};

const wchar_t* tr(StringId id);
