@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "CXX=g++"
set "WINDRES=windres"
if exist "C:\msys64\mingw64\bin\g++.exe" (
  set "CXX=C:\msys64\mingw64\bin\g++.exe"
  set "WINDRES=C:\msys64\mingw64\bin\windres.exe"
)

if not exist build mkdir build

echo [1/2] Embedding resources...
"%WINDRES%" resources\app.rc -O coff -o build\app.res --include-dir="%CD%" --include-dir="%CD%\resources"
if errorlevel 1 (
  echo windres failed
  exit /b 1
)

echo [2/2] Compiling CodeTranslationMouse.exe...
"%CXX%" -std=c++20 -O2 -municode -mwindows ^
  -static -static-libgcc -static-libstdc++ ^
  -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN ^
  -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
  -Isrc ^
  src\main.cpp src\App.cpp src\TextCapture.cpp src\Translate.cpp src\Ui.cpp ^
  build\app.res ^
  -lole32 -loleaut32 -loleacc -luuid -luser32 -lgdi32 -lshell32 -lcomctl32 -ladvapi32 ^
  -o build\CodeTranslationMouse.exe
if errorlevel 1 (
  echo compile failed
  exit /b 1
)

copy /Y dictionary.txt build\dictionary.txt >nul
echo.
echo Built: build\CodeTranslationMouse.exe
echo Run:   build\CodeTranslationMouse.exe
echo Debug: build\CodeTranslationMouse.exe --debug
exit /b 0
