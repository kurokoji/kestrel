cmake_minimum_required(VERSION 3.28)
project(kestrel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(kestrel WIN32
    src/main.cpp
    src/App.cpp
    src/WindowLayout.cpp
    src/MainWindow.cpp
    src/TreePane.cpp
    src/FilePane.cpp
    src/FilePaneTabs.cpp
    src/FileEntrySort.cpp
    src/DirectoryModel.cpp
    src/DirectoryWatcher.cpp
    src/FileOperations.cpp
    src/ShellSelection.cpp
    src/ShellContextMenu.cpp
    src/RecycleBinOps.cpp
    src/SplitterController.cpp
    src/ClipboardFiles.cpp
    src/IconCache.cpp
    src/Dialogs.cpp
    src/FileClassify.cpp
    src/PreviewPane.cpp
    src/Session.cpp
    src/SessionFormat.cpp
    src/TabCycle.cpp
    src/DriveBadge.cpp
    src/kestrel.rc
)

target_compile_definitions(kestrel PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

# Use static CRT so the exe stays close to dependency-free and the redistributable footprint is minimal.
set_property(TARGET kestrel PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(kestrel PRIVATE
    comctl32
    comdlg32
    shell32
    shlwapi
    ole32
    oleaut32
    uuid
    gdi32
    gdiplus
    user32
    advapi32
)

if(MSVC)
    target_compile_options(kestrel PRIVATE /W4 /permissive- /utf-8 /EHsc)
    # Trim unused code paths and keep the binary small.
    target_link_options(kestrel PRIVATE /OPT:REF /OPT:ICF /MANIFEST:NO)
endif()

option(KESTREL_BUILD_TESTS "Build the doctest-based unit test suite" ON)

if(KESTREL_BUILD_TESTS)
    enable_testing()

    add_executable(kestrel_tests
        src/WindowLayout.cpp
        tests/LayoutTests.cpp
        tests/test_main.cpp
        tests/FormattingTests.cpp
        tests/FileEntrySortTests.cpp
        src/FileEntrySort.cpp
        tests/FileClassifyTests.cpp
        src/FileClassify.cpp
        tests/SessionFormatTests.cpp
        src/SessionFormat.cpp
        tests/TabCycleTests.cpp
        src/TabCycle.cpp
        tests/EditKeysTests.cpp
        tests/DriveBadgeTests.cpp
        src/DriveBadge.cpp
    )

    target_include_directories(kestrel_tests PRIVATE
        src
        third_party/doctest
    )

    target_compile_definitions(kestrel_tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
    )

    if(MSVC)
        target_compile_options(kestrel_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()

    add_test(NAME kestrel_tests COMMAND kestrel_tests)
endif()
