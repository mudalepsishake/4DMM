@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem ============================================================================
rem 4DMM Build Script
rem
rem Builds the x86 RelWithDebInfo configuration with the Blaze Engine.
rem Place this file in the root of the 4DMM source tree and run it from there.
rem
rem Required:
rem   - Visual Studio with the C++ x86/x64 build tools
rem   - CMake
rem   - Ninja
rem ============================================================================

set "SOURCE_ROOT=%~dp0"
set "PRESET=x86-msvc-modern-relwithdebinfo"
set "BUILD_DIR=%SOURCE_ROOT%build\%PRESET%"
set "DIST=%SOURCE_ROOT%dist\%PRESET%"
set "LOG_DIR=%SOURCE_ROOT%logs"
set "FORCE_REBUILD=%SOURCE_ROOT%FORCE_REBUILD.txt"

if not exist "%SOURCE_ROOT%CMakePresets.json" (
    echo ERROR: CMakePresets.json was not found.
    echo.
    echo Build 4DMM.cmd must be located in the root of the 4DMM source tree.
    echo Current location:
    echo   %SOURCE_ROOT%
    echo.
    pause
    exit /b 1
)

if not exist "%SOURCE_ROOT%brender14\CMakeLists.txt" (
    echo ERROR: The Blaze Engine source is missing.
    echo Expected:
    echo   %SOURCE_ROOT%brender14
    echo.
    pause
    exit /b 1
)

tasklist /FI "IMAGENAME eq 3dmovie.exe" 2>nul | find /I "3dmovie.exe" >nul
if not errorlevel 1 (
    echo ERROR: 3dmovie.exe is currently running.
    echo Close 4DMM before building, then run this file again.
    echo.
    pause
    exit /b 1
)

rem Show the incremental-build note only after this tree has already produced
rem a 4DMM executable at least once.
if exist "%BUILD_DIR%\src\3dmovie.exe" call :ShowIncrementalBuildNotice
if exist "%DIST%\3dmovie.exe" call :ShowIncrementalBuildNotice

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%"


set "TIMESTAMP="
for /f "usebackq delims=" %%T in (`powershell -NoProfile -Command "Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'"`) do set "TIMESTAMP=%%T"
if not defined TIMESTAMP (
    echo ERROR: Could not create a build timestamp.
    pause
    exit /b 1
)

set "BUILD_LOG=%LOG_DIR%\build_%TIMESTAMP%.log"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$lines=@(" ^
  "'=== 4DMM BUILD ==='," ^
  "'timestamp: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')," ^
  "'source: ' + $env:SOURCE_ROOT," ^
  "'preset: ' + $env:PRESET," ^
  "'build: ' + $env:BUILD_DIR," ^
  "'install: ' + $env:DIST," ^
  "'engine: Blaze Engine'," ^
  "'=================='" ^
  "); Set-Content -LiteralPath $env:BUILD_LOG -Value $lines -Encoding UTF8"

if errorlevel 1 (
    echo ERROR: Could not create build log:
    echo   %BUILD_LOG%
    pause
    exit /b 1
)

echo.
echo ============================================================
echo 4DMM Build
echo ============================================================
echo Source : %SOURCE_ROOT%
echo Preset : %PRESET%
echo Output : %DIST%
echo Log    : %BUILD_LOG%
echo.

rem Files copied or extracted from another machine can occasionally have
rem timestamps ahead of this computer's clock. Normalize only those future
rem timestamps so Ninja does not repeatedly rebuild or reconfigure them.
echo Checking source timestamps...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$root=[IO.Path]::GetFullPath($env:SOURCE_ROOT);" ^
  "$now=Get-Date;" ^
  "$skip=@((Join-Path $root 'build'),(Join-Path $root 'dist'),(Join-Path $root 'logs'),(Join-Path $root '.git'));" ^
  "$files=Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction Stop | Where-Object {" ^
  "  $p=$_.FullName;" ^
  "  -not ($skip | Where-Object { $p.StartsWith($_,[StringComparison]::OrdinalIgnoreCase) }) -and $_.LastWriteTime -gt $now.AddMinutes(1)" ^
  "};" ^
  "foreach($f in $files){$f.LastWriteTime=$now; Write-Host ('  corrected future timestamp: ' + $f.FullName.Substring($root.Length))};" ^
  "if($files.Count -gt 0){Add-Content -LiteralPath $env:BUILD_LOG -Value ('Corrected future timestamps: ' + $files.Count) -Encoding UTF8}"
if errorlevel 1 (
    call :Fail "Could not normalize future source timestamps."
    exit /b 1
)

rem FORCE_REBUILD.txt is a one-shot list of source-relative paths. Touching the
rem listed files guarantees that Ninja recompiles files whose contents changed
rem even if an extracted/copied file arrived with an older timestamp.
if exist "%FORCE_REBUILD%" (
    echo Applying FORCE_REBUILD.txt...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
      "$ErrorActionPreference='Stop';" ^
      "$root=[IO.Path]::GetFullPath($env:SOURCE_ROOT);" ^
      "$manifest=$env:FORCE_REBUILD;" ^
      "$now=Get-Date;" ^
      "$count=0;" ^
      "Get-Content -LiteralPath $manifest | ForEach-Object {" ^
      "  $rel=$_.Trim();" ^
      "  if($rel.Length -eq 0 -or $rel.StartsWith('#')){return};" ^
      "  $path=[IO.Path]::GetFullPath((Join-Path $root $rel));" ^
      "  if(-not $path.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)){throw ('Path escapes source root: ' + $rel)};" ^
      "  if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw ('File not found: ' + $rel)};" ^
      "  (Get-Item -LiteralPath $path).LastWriteTime=$now;" ^
      "  Write-Host ('  touched ' + $rel);" ^
      "  Add-Content -LiteralPath $env:BUILD_LOG -Value ('FORCE_REBUILD: ' + $rel) -Encoding UTF8;" ^
      "  $count++" ^
      "};" ^
      "Write-Host ('Touched modified source files: ' + $count)"
    if errorlevel 1 (
        call :Fail "Could not apply FORCE_REBUILD.txt."
        exit /b 1
    )
    del /q "%FORCE_REBUILD%"
    if exist "%FORCE_REBUILD%" (
        call :Fail "Build used FORCE_REBUILD.txt but could not remove it afterward."
        exit /b 1
    )
)

rem Load the Visual Studio x86 compiler environment.
if /I "%VSCMD_ARG_TGT_ARCH%"=="x86" goto HAVE_VS_ENV

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    call :Fail "Could not find Visual Studio Installer\vswhere.exe."
    exit /b 1
)

set "VSINSTALL="
for /f "tokens=*" %%I in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VSINSTALL=%%I"
if not defined VSINSTALL (
    call :Fail "Could not locate Visual Studio C++ x86/x64 build tools."
    exit /b 1
)

set "VCVARS=%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" (
    call :Fail "Could not find vcvarsall.bat."
    exit /b 1
)

echo Loading Visual Studio x86 compiler environment...
call "%VCVARS%" x86
if errorlevel 1 (
    call :Fail "Visual Studio x86 compiler environment setup failed."
    exit /b 1
)

:HAVE_VS_ENV
where cl >nul 2>nul
if errorlevel 1 (
    call :Fail "cl.exe is not available."
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    call :Fail "cmake.exe is not available."
    exit /b 1
)

where ninja >nul 2>nul
if errorlevel 1 (
    call :Fail "ninja.exe is not available."
    exit /b 1
)

echo Compiler:
cl 2>&1 | findstr /C:"Version"
for /f "delims=" %%V in ('cl 2^>^&1 ^| findstr /C:"Version"') do >>"%BUILD_LOG%" echo compiler: %%V

pushd "%SOURCE_ROOT%"
if errorlevel 1 (
    call :Fail "Could not enter the 4DMM source directory."
    exit /b 1
)

echo.
echo Configuring %PRESET%...
>>"%BUILD_LOG%" echo.
>>"%BUILD_LOG%" echo === CONFIGURE ===

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Continue';" ^
  "& cmake --preset $env:PRESET ('-DCMAKE_INSTALL_PREFIX=' + $env:DIST) 2>&1 | ForEach-Object { $_; Add-Content -LiteralPath $env:BUILD_LOG -Value $_ -Encoding UTF8 };" ^
  "exit $LASTEXITCODE"
set "CONFIG_RC=%ERRORLEVEL%"
if not "%CONFIG_RC%"=="0" (
    popd
    call :Fail "Configure failed with exit code %CONFIG_RC%."
    echo.
    echo See:
    echo   %BUILD_LOG%
    pause
    exit /b %CONFIG_RC%
)

echo.
echo Building and installing 4DMM...
>>"%BUILD_LOG%" echo.
>>"%BUILD_LOG%" echo === BUILD AND INSTALL ===

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Continue';" ^
  "& cmake --build $env:BUILD_DIR --target install --parallel 2>&1 | ForEach-Object { $_; Add-Content -LiteralPath $env:BUILD_LOG -Value $_ -Encoding UTF8 };" ^
  "exit $LASTEXITCODE"
set "BUILD_RC=%ERRORLEVEL%"
popd

if not "%BUILD_RC%"=="0" (
    call :Fail "Build failed with exit code %BUILD_RC%."
    echo.
    echo See:
    echo   %BUILD_LOG%
    pause
    exit /b %BUILD_RC%
)

if not exist "%DIST%\3dmovie.exe" (
    call :Fail "Build reported success, but 3dmovie.exe was not found in the install folder."
    echo Expected:
    echo   %DIST%\3dmovie.exe
    pause
    exit /b 1
)

rem Force the Windows Compatibility-tab High DPI scaling override to
rem "Application" for the freshly installed 4DMM executable. This is stored
rem per-user and does not require Administrator privileges.
echo Applying 4DMM DPI compatibility setting...
reg add "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers" /v "%DIST%\3dmovie.exe" /t REG_SZ /d "~ HIGHDPIAWARE" /f >nul
if errorlevel 1 (
    echo WARNING: Could not set the High DPI scaling override for 3dmovie.exe.
    >>"%BUILD_LOG%" echo WARNING: Could not set High DPI scaling override: Application
) else (
    >>"%BUILD_LOG%" echo DPI compatibility: High DPI scaling override = Application
)

rem Mark this exact installed executable as already acknowledged by Windows
rem Program Compatibility Assistant. This suppresses the first-run
rem "Did this program work correctly?" prompt without disabling PCA globally.
echo Applying 4DMM Program Compatibility Assistant setting...
reg add "HKCU\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Compatibility Assistant\Persisted" /v "%DIST%\3dmovie.exe" /t REG_DWORD /d 1 /f >nul
if errorlevel 1 (
    echo WARNING: Could not suppress the Program Compatibility Assistant prompt for 3dmovie.exe.
    >>"%BUILD_LOG%" echo WARNING: Could not set Program Compatibility Assistant persisted state
) else (
    >>"%BUILD_LOG%" echo Program Compatibility Assistant: persisted as working correctly
)

>>"%BUILD_LOG%" echo.
>>"%BUILD_LOG%" echo BUILD SUCCESSFUL.

echo.
echo ============================================================
echo BUILD SUCCESSFUL
echo ============================================================
echo 4DMM:
echo   %DIST%\3dmovie.exe
echo.
echo Build log:
echo   %BUILD_LOG%
echo.
pause
exit /b 0


:ShowIncrementalBuildNotice
if defined INCREMENTAL_NOTICE_SHOWN exit /b 0
set "INCREMENTAL_NOTICE_SHOWN=1"
echo.
echo ============================================================
echo Incremental build note
echo ============================================================
echo This source tree has been built before.
echo.
echo If modified source files were copied or extracted over this tree and
echo their modification timestamps may be OLDER than files already compiled,
echo list those modified files in:
echo.
echo   FORCE_REBUILD.txt
echo.
echo Put one path per line, relative to the 4DMM source root. Example:
echo.
echo   brender14\core\v1db\render.c
echo   brender14\core\v1db\enables.c
echo   inc\studio.h
echo.
echo The builder will update the timestamps of exactly those files before
echo building, then remove FORCE_REBUILD.txt.
echo.
echo Files accidentally dated in the FUTURE are corrected automatically and
echo do not need to be listed in FORCE_REBUILD.txt.
echo ============================================================
echo.
pause
exit /b 0


:Fail
set "ERROR_TEXT=%~1"
echo.
echo ERROR: %ERROR_TEXT%
if defined BUILD_LOG >>"%BUILD_LOG%" echo ERROR: %ERROR_TEXT%
exit /b 0
