@echo off
setlocal

set "VS_VERSION=2022"
set "ARCH=x64"

for %%A in (%*) do (
    if /i "%%A"=="2019"   set "VS_VERSION=2019"
    if /i "%%A"=="vs2019" set "VS_VERSION=2019"
    if /i "%%A"=="2022"   set "VS_VERSION=2022"
    if /i "%%A"=="vs2022" set "VS_VERSION=2022"
    if /i "%%A"=="x64"    set "ARCH=x64"
    if /i "%%A"=="x86"    set "ARCH=x86"
)

if "%VS_VERSION%"=="2019" (
    set "VS_ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2019"
) else (
    set "VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
)

for /d %%D in ("%VS_ROOT%\*") do (
    if /i "%ARCH%"=="x64" (
        if exist "%%D\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%D\VC\Auxiliary\Build\vcvars64.bat"
            goto :build
        )
    ) else (
        if exist "%%D\VC\Auxiliary\Build\vcvars32.bat" (
            call "%%D\VC\Auxiliary\Build\vcvars32.bat"
            goto :build
        )
    )
)

echo VS %VS_VERSION% not found.
exit /b 1

:build
@REM rd /s /q build 2>nul
@REM rd /s /q products 2>nul

rem Auto-detect MSVC compiler version from VCToolsVersion (e.g. 14.44.xxxxx -> 194)
for /f "tokens=2 delims=." %%V in ("%VCToolsVersion%") do set "_MSVC_MINOR=%%V"
set "MSVC_COMPILER_VERSION=19%_MSVC_MINOR:~0,1%"
echo Detected MSVC compiler.version: %MSVC_COMPILER_VERSION%

conan install . --profile:all=./conanfile/win_%ARCH%_release -s:a=compiler.version=%MSVC_COMPILER_VERSION% --build=missing
if errorlevel 1 exit /b 1
cmake --preset client_release_%ARCH%
if errorlevel 1 exit /b 1
cmake --build --preset client_release_%ARCH%
if errorlevel 1 exit /b 1
cmake --install build/Release
