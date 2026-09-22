@echo off
setlocal
set REPO_DIR=%~dp0
set REPO_DIR=%REPO_DIR:~0,-1%
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
set PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%
where cl
where ninja
set CC=cl
set CXX=cl
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S "%REPO_DIR%" -B "%REPO_DIR%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "%REPO_DIR%\build"
