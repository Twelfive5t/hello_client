@echo off
setlocal

set "VS_VERSION=2022"
set "ARCH=x64"

if not "%~1"=="" call :parse_arg "%~1"
if not "%~2"=="" call :parse_arg "%~2"

if /i "%ARCH%"=="amd64" set "ARCH=x64"

if /i not "%ARCH%"=="x64" (
    echo Unsupported architecture: %ARCH%
    echo Usage: build_ninja.bat [2019^|2022^|vs2019^|vs2022] [x64]
    exit /b 1
)

if "%VS_VERSION%"=="2019" (
    set "MSVC_COMPILER_VERSION=192"
    set "VS_ROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2019"
) else (
    if "%VS_VERSION%"=="2022" (
        set "MSVC_COMPILER_VERSION=193"
        set "VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
    ) else (
        echo Unsupported Visual Studio version: %VS_VERSION%
        echo Usage: build_ninja.bat [2019^|2022^|vs2019^|vs2022] [x64]
        exit /b 1
    )
)

set "PRESET=ninja_release_%ARCH%"
set "CONAN_PROFILE=./conanfile/%ARCH%-release_ninja"

if exist build (
    rd /s /q build
)

if exist products (
    rd /s /q products
)

if not exist "%CONAN_PROFILE%" (
    echo Conan profile not found: %CONAN_PROFILE%
    exit /b 1
)

set "VS_INSTALL_PATH="

call :find_vs_install "%VS_ROOT%"

if not defined VS_INSTALL_PATH (
    echo Unable to find a Visual Studio %VS_VERSION% installation with C++ tools.
    exit /b 1
)

set "VCVARS_BAT=%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VCVARS_BAT%" (
    echo MSVC environment script not found: %VCVARS_BAT%
    exit /b 1
)

call "%VCVARS_BAT%"
if errorlevel 1 exit /b %errorlevel%

echo MSVC environment has been set for %VS_VERSION% %ARCH%.
echo Using Conan profile %CONAN_PROFILE% with compiler.version=%MSVC_COMPILER_VERSION%.

conan profile detect --exist-ok
if errorlevel 1 exit /b %errorlevel%

conan install . --profile:host "%CONAN_PROFILE%" --profile:build default -s:h compiler.version=%MSVC_COMPILER_VERSION% --output-folder=build/conan --build=missing
if errorlevel 1 exit /b %errorlevel%

cmake --preset %PRESET%
if errorlevel 1 exit /b %errorlevel%

cmake --build --preset %PRESET%
if errorlevel 1 exit /b %errorlevel%

cmake --install build
exit /b %errorlevel%

:parse_arg
if "%~1"=="" exit /b 0

if /i "%~1"=="2019" set "VS_VERSION=2019"
if /i "%~1"=="2022" set "VS_VERSION=2022"
if /i "%~1"=="vs2019" set "VS_VERSION=2019"
if /i "%~1"=="vs2022" set "VS_VERSION=2022"
if /i "%~1"=="x64" set "ARCH=x64"
if /i "%~1"=="amd64" set "ARCH=x64"
exit /b 0

:find_vs_install
if "%~1"=="" exit /b 0
if not exist "%~1" exit /b 0

for /d %%D in ("%~1\*") do (
    if exist "%%~fD\VC\Auxiliary\Build\vcvars64.bat" (
        set "VS_INSTALL_PATH=%%~fD"
        exit /b 0
    )
)

exit /b 0
