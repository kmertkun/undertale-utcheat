@echo off
rem utcheat.dll + UTInjector.exe derler (32-bit). Gerekli: toolchain\ altinda llvm-mingw.
setlocal
cd /d "%~dp0"
set TC=%~dp0toolchain\bin
if not exist "%TC%\i686-w64-mingw32-clang++.exe" (
    echo llvm-mingw bulunamadi: %TC%
    echo https://github.com/mstorsjo/llvm-mingw/releases adresinden ucrt-x86_64 zip'ini indirip toolchain\ klasorune ac.
    exit /b 1
)
set CXX="%TC%\i686-w64-mingw32-clang++.exe"
set CC="%TC%\i686-w64-mingw32-clang.exe"
if not exist build mkdir build
if not exist bin mkdir bin

set IMGUI=third_party\imgui
set MH=third_party\minhook

echo [1/3] MinHook
for %%f in (%MH%\src\buffer.c %MH%\src\hook.c %MH%\src\trampoline.c %MH%\src\hde\hde32.c) do (
    %CC% -O2 -c %%f -I%MH%\include -o build\%%~nf.o || exit /b 1
)

echo [2/3] utcheat.dll
%CXX% -O2 -std=c++17 -shared -static ^
    -I%IMGUI% -I%MH%\include ^
    src\dllmain.cpp src\autododge.cpp src\input.cpp ^
    %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
    %IMGUI%\backends\imgui_impl_dx9.cpp %IMGUI%\backends\imgui_impl_win32.cpp ^
    build\buffer.o build\hook.o build\trampoline.o build\hde32.o ^
    -ld3d9 -ldwmapi -lgdi32 -luser32 -limm32 ^
    -o bin\utcheat.dll || exit /b 1

echo [3/3] UTInjector.exe
%CXX% -O2 -std=c++17 -static -municode src\injector.cpp -luser32 -lshell32 -o bin\UTInjector.exe || exit /b 1

echo Tamam: bin\utcheat.dll, bin\UTInjector.exe
