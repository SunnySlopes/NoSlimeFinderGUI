@echo off
setlocal EnableExtensions EnableDelayedExpansion
set MASTER=%~dp0
call :resolve_java_home
if errorlevel 1 exit /b 1
set PATH=C:\msys64\ucrt64\bin;%PATH%
set CUB=%MASTER%cubiomes
set OPT=%MASTER%slimeoptimizer
set OUT=%MASTER%native\jni\build\native
set RF=%MASTER%..\native

if not exist "%CUB%\noise.c" (
  echo ERROR: cubiomes not found at:
  echo   %CUB%
  echo Init the submodule: git submodule update --init jni/cubiomes
  exit /b 1
)
if not exist "%OPT%\src\slime_check.cpp" (
  echo ERROR: slimeoptimizer not found at:
  echo   %OPT%
  exit /b 1
)

mkdir "%OUT%" 2>nul
mkdir "%RF%\windows" 2>nul
cd /d "%MASTER%"

set INC=%JAVA_HOME%\include
set INCW=%JAVA_HOME%\include\win32
rem -I. so #include "cubiomes/..." resolves to jni/cubiomes
set CDEF=-DNDEBUG -D_WIN32 -DNOSLIME_FINDER_JNI_LIB -I. -I"%INC%" -I"%INCW%"
rem pre_cubiomes.h avoids C++20 std::lerp clash without copying cubiomes sources
set CXXDEF=%CDEF% -include cxx_compat/pre_cubiomes.h -I"%OPT%\include"

del /q *.o bridge.o 2>nul
rem Core biome/noise only (no finders/quadbase/loot — unused by this search)
gcc -std=c17 -O3 %CDEF% -c "%CUB%\generator.c" "%CUB%\layers.c" "%CUB%\biomenoise.c" "%CUB%\biomes.c" "%CUB%\noise.c" "%CUB%\util.c"
if errorlevel 1 exit /b 1

rem SlimeOptimizer submodule: do not modify these sources.
g++ -std=c++20 -O3 %CXXDEF% -c "%OPT%\src\geometry.cpp"
if errorlevel 1 exit /b 1
g++ -std=c++20 -O3 %CXXDEF% -c "%OPT%\src\slime_check.cpp"
if errorlevel 1 exit /b 1
g++ -std=c++20 -O3 %CXXDEF% -c "%OPT%\src\java_random.cpp"
if errorlevel 1 exit /b 1

g++ -std=c++20 -O3 %CXXDEF% -c NoSlimeSearch.cpp
if errorlevel 1 exit /b 1
g++ -std=c++20 -O3 %CXXDEF% -c native/jni/com_sunnyslopes_noslimefinder_NoSlimeFinderBridge.cpp -o bridge.o
if errorlevel 1 exit /b 1

g++ -shared -O3 -o "%OUT%\libNoSlimeFinderLibJ.dll" generator.o layers.o biomenoise.o biomes.o noise.o util.o geometry.o slime_check.o java_random.o NoSlimeSearch.o bridge.o -static -static-libgcc -static-libstdc++ -pthread
if errorlevel 1 exit /b 1

copy /y "%OUT%\libNoSlimeFinderLibJ.dll" "%RF%\windows\libNoSlimeFinderLibJ.dll"
echo Built: %OUT%\libNoSlimeFinderLibJ.dll
echo Copied to: %RF%\windows\libNoSlimeFinderLibJ.dll
echo Cubiomes: %CUB%
echo SlimeOptimizer: %OPT%
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
