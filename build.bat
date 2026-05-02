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
    set "MSVC_COMPILER_VERSION=192"
    set "VS_ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2019"
) else (
    set "MSVC_COMPILER_VERSION=193"
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
rd /s /q build 2>nul
rd /s /q products 2>nul

conan install . --profile=./conanfile/win_%ARCH%_release -s=compiler.version=%MSVC_COMPILER_VERSION% --build=missing
cmake --preset client_release_%ARCH%
cmake --build --preset client_release_%ARCH%
cmake --install build/Release
