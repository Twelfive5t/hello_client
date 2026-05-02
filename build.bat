@echo off
setlocal

set "VS_VERSION=2022"

for %%A in (%*) do (
    if /i "%%A"=="2019"   set "VS_VERSION=2019"
    if /i "%%A"=="vs2019" set "VS_VERSION=2019"
    if /i "%%A"=="2022"   set "VS_VERSION=2022"
    if /i "%%A"=="vs2022" set "VS_VERSION=2022"
)

if "%VS_VERSION%"=="2019" (
    set "MSVC_COMPILER_VERSION=192"
    set "VS_ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2019"
) else (
    set "MSVC_COMPILER_VERSION=193"
    set "VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
)

for /d %%D in ("%VS_ROOT%\*") do (
    if exist "%%D\VC\Auxiliary\Build\vcvars64.bat" (
        call "%%D\VC\Auxiliary\Build\vcvars64.bat"
        goto :build
    )
)

echo VS %VS_VERSION% not found.
exit /b 1

:build
rd /s /q build 2>nul
rd /s /q products 2>nul

conan install . --profile=./conanfile/win_x86_64_release -s=compiler.version=%MSVC_COMPILER_VERSION% --build=missing
cmake --preset client_release_x64
cmake --build --preset client_release_x64
cmake --install build
