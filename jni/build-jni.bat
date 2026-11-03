@echo off
rem SPDX-License-Identifier: MIT
rem Copyright (c) 2026 SunnySlopes
rem ---------------------------------------------------------------------------
rem Builds libNoSlimeFinderLibJ.dll (thin JNI shim) and packages the
rem NoSlimeFinderBase CLI next to it.
rem
rem The NoSlimeFinderBase git submodule provides:
rem   - build\lib\libnoslimefinderbase.a   search core + cubiomes + SlimeOptimizer
rem   - build\bin\NoSlimeFinderBase-cli.exe standalone CLI
rem
rem Layout required:
rem   NoSlimeFinderGUI\
rem     jni\build-jni.bat            (this file)
rem     jni\NoSlimeFinderBridge.cpp  (thin JNI shim, no search logic)
rem     NoSlimeFinderBase\           (git submodule)
rem
rem Outputs:
rem   native\windows\libNoSlimeFinderLibJ.dll
rem   native\windows\NoSlimeFinderBase-cli.exe
rem ---------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion
set MASTER=%~dp0
call :resolve_java_home
if errorlevel 1 exit /b 1

set BASE=%MASTER%..\NoSlimeFinderBase
set OUT=%MASTER%..\native\windows
set BJ=%MASTER%build

if not exist "%BASE%\include\noslimefinder\base.h" (
  echo ERROR: NoSlimeFinderBase submodule not found at:
  echo   %BASE%
  echo Initialize it first:
  echo   git submodule update --init --recursive
  exit /b 1
)
if not exist "%BASE%\cubiomes\generator.c" (
  echo ERROR: base submodule cubiomes is empty:
  echo   %BASE%\cubiomes
  echo   git -C "%BASE%" submodule update --init --recursive
  exit /b 1
)
if not exist "%BASE%\slimeoptimizer\src\slime_check.cpp" (
  echo ERROR: base submodule slimeoptimizer is empty:
  echo   %BASE%\slimeoptimizer
  echo   git -C "%BASE%" submodule update --init --recursive
  exit /b 1
)

echo [jni] building NoSlimeFinderBase via scripts\build_msys2.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%BASE%\scripts\build_msys2.ps1"
if errorlevel 1 exit /b 1

mkdir "%OUT%" 2>nul
mkdir "%BJ%" 2>nul

rem Same MSYS2 discovery as NoSlimeFinderBase/scripts/build_msys2.ps1.
set MSYS=%MSYS2_ROOT%
if "%MSYS%"=="" set MSYS=C:\msys64
set MSYS_MINGW=%MSYS2_MINGW%
if "%MSYS_MINGW%"=="" (
  if exist "%MSYS%\ucrt64\bin\g++.exe" (
    set MSYS_MINGW=ucrt64
  ) else (
    set MSYS_MINGW=mingw64
  )
)
if not exist "%MSYS%\%MSYS_MINGW%\bin\g++.exe" (
  echo ERROR: g++ not found under %MSYS%\%MSYS_MINGW%\bin
  echo Set MSYS2_ROOT / MSYS2_MINGW or install the MSYS2 UCRT64 toolchain.
  exit /b 1
)
set PATH=%MSYS%\%MSYS_MINGW%\bin;%PATH%

set INC=%JAVA_HOME%\include
set INCW=%JAVA_HOME%\include\win32

echo [jni] compiling thin bridge
g++ -std=c++20 -O3 -DNDEBUG -I"%BASE%\include" -I"%INC%" -I"%INCW%" -c NoSlimeFinderBridge.cpp -o "%BJ%\bridge.o"
if errorlevel 1 exit /b 1

echo [jni] linking libNoSlimeFinderLibJ.dll
g++ -shared -O3 -o "%OUT%\libNoSlimeFinderLibJ.dll" "%BJ%\bridge.o" "%BASE%\build\lib\libnoslimefinderbase.a" -static -static-libgcc -static-libstdc++ -pthread
if errorlevel 1 exit /b 1

copy /y "%BASE%\build\bin\NoSlimeFinderBase-cli.exe" "%OUT%\NoSlimeFinderBase-cli.exe" >nul
if errorlevel 1 exit /b 1

echo Built: %OUT%\libNoSlimeFinderLibJ.dll
echo Built: %OUT%\NoSlimeFinderBase-cli.exe
echo Base : %BASE%
echo JAVA_HOME: %JAVA_HOME%
exit /b 0

:resolve_java_home
if defined JAVA_HOME (
  if /I "%JAVA_HOME:~-1%"=="\" set "JAVA_HOME=%JAVA_HOME:~0,-1%"
  if /I "%JAVA_HOME:~-4%"=="\bin" set "JAVA_HOME=%JAVA_HOME:~0,-4%"
)
if exist "%JAVA_HOME%\include\jni.h" exit /b 0
for %%D in ("C:\Program Files\Java\jdk-17" "C:\Program Files\Java\jdk1.8" "C:\Program Files\Java\latest") do (
  if exist "%%~D\include\jni.h" (
    set "JAVA_HOME=%%~D"
    exit /b 0
  )
)
for /f "delims=" %%i in ('where java 2^>nul') do (
  if exist "%%~dpi..\include\jni.h" (
    set "JAVA_HOME=%%~dpi.."
    exit /b 0
  )
)
echo ERROR: Cannot find a JDK with include\jni.h.
echo JAVA_HOME=%JAVA_HOME%
echo Point JAVA_HOME at a JDK root, for example C:\Program Files\Java\jdk-17
exit /b 1
