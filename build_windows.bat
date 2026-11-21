@echo off
REM Builds MediCare Pro and its installer on Windows 10/11 64-bit.
REM Needs: Visual Studio 2022 (Desktop C++), Qt 6.5 or newer for MSVC 2019/2022 64-bit
REM        with the Qt Charts module and the OpenSSL 3 tool (both in the Qt online installer),
REM        CMake, Ninja (installed with Visual Studio), Inno Setup 6.
REM Edit the three paths below to match your PC, then double click this file.

set QT_DIR=C:\Qt\6.5.3\msvc2019_64
set OPENSSL_DIR=C:\Qt\Tools\OpenSSLv3\Win_x64
set INNO="C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" || goto :error

cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QT_DIR% -DOPENSSL_ROOT_DIR=%OPENSSL_DIR% || goto :error
cmake --build build --config Release || goto :error

REM Run all automated tests, including the acceptance flow.
set PATH=%QT_DIR%\bin;%OPENSSL_DIR%\bin;%PATH%
set QT_QPA_PLATFORM=offscreen
ctest --test-dir build --output-on-failure || goto :error

REM Prepare the deploy folder.
rmdir /s /q deploy 2>nul
mkdir deploy\refdata
copy build\MediCarePro.exe deploy\ || goto :error
%QT_DIR%\bin\windeployqt.exe --release --no-compiler-runtime --no-opengl-sw deploy\MediCarePro.exe || goto :error
copy %OPENSSL_DIR%\bin\libcrypto-3-x64.dll deploy\ || goto :error
copy %OPENSSL_DIR%\bin\libssl-3-x64.dll deploy\ || goto :error

REM Preloaded reference catalog. Uses the official file in data\official if present,
REM otherwise the small sample file (shown in the app as sample data).
if exist data\official\catalog.csv (
  build\refpack.exe import --db deploy\refdata\reference_seed.db --csv data\official\catalog.csv --source-code official --source-name "Official dataset supplied to SoftzenAgency" --verified --coverage "See data\official\README.md" || goto :error
) else (
  build\refpack.exe import --db deploy\refdata\reference_seed.db --csv data\sample\sample_catalog.csv --source-code sample --source-name "MediCare Pro demonstration sample" --sample --coverage "Sample data for first run only." || goto :error
)
del deploy\refdata\reference_seed.db-wal 2>nul
del deploy\refdata\reference_seed.db-shm 2>nul

REM Visual C++ runtime (needed on fresh Windows installs).
copy "%VCToolsRedistDir%x64\Microsoft.VC143.CRT\*.dll" deploy\ || goto :error

%INNO% installer\MediCarePro.iss || goto :error
echo.
echo Done. Installer: dist\MediCare_Pro_Setup.exe
pause
exit /b 0

:error
echo Build failed.
pause
exit /b 1
