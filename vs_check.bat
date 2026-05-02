set VS_VERSION=2022
set ARCH=x64

for /f %%i in (version.txt) do set VER=%%i

if "%VS_VERSION%"=="2019" (
    if "%ARCH%"=="x64" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars32.bat"
    )
) else if "%VS_VERSION%"=="2022" (
    if "%ARCH%"=="x64" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
    )
) else (
    echo Unsupported Visual Studio version: %VS_VERSION%
    exit /b 1
)

echo MSVC environment has been set for %VS_VERSION% %ARCH%.

dumpbin /headers ./products/bin%VER%/hello_client.dll

dumpbin /exports ./products/bin%VER%/hello_client.dll