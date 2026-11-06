@echo off
rem ============================================================
rem  7z_wimlib - build_gui.bat (Windows)
rem  Compiles src\*.cs into 7z_wimlib.exe (in this folder) with the C#
rem  compiler that comes with the .NET Framework 4.x of Windows 10/11
rem  (no Visual Studio, no SDK, no download).
rem  The icon (DVT.ico), the manifest (app.manifest) and the language
rem  files (lang\*.ini) are embedded; the version information comes
rem  from src\AssemblyInfo.cs. 7z_wimlib.exe also reads lang\*.ini at
rem  start, so edited copies work without a rebuild.
rem  At run time 7z_wimlib.exe uses lib\libwim-15.dll (wimlib) and
rem  libz.exe + 7z.dll (7-Zip); missing ones are downloaded by
rem  7z_wimlib.exe itself (newest version).
rem  Administrator rights are not needed.
rem
rem  Usage: build_gui.bat [nopause]
rem  Exit : 0 = OK
rem         1 = failed
rem ============================================================
setlocal EnableExtensions
cd /d "%~dp0"

set "SRC_DIR=%~dp0src"
set "OUT_EXE=%~dp07z_wimlib.exe"
set "MANIFEST=%~dp0app.manifest"
set "ICON=%~dp0DVT.ico"

rem ---- C# compiler of the .NET Framework 4.x (64-bit first) ----
set "CSC=%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%CSC%" set "CSC=%SystemRoot%\Microsoft.NET\Framework\v4.0.30319\csc.exe"
if not exist "%CSC%" (
    echo [ERROR] csc.exe of the .NET Framework 4.x was not found.
    echo         Turn on ".NET Framework 4.8 Advanced Services" in Windows Features.
    goto :failed
)

if not exist "%SRC_DIR%\Program.cs" (
    echo [ERROR] Source files not found: %SRC_DIR%
    goto :failed
)

set "OPT_ICON="
if exist "%ICON%" set "OPT_ICON=/win32icon:"%ICON%""

rem ---- language files embedded as Dvt.lang.<code>.ini ----
set "OPT_RES="
for %%F in ("%~dp0lang\*.ini") do call :add_resource "%%~fF" "%%~nF"
if not defined OPT_RES (
    echo [ERROR] No language file found in %~dp0lang ^(en.ini is required^).
    goto :failed
)
if not exist "%~dp0lang\en.ini" (
    echo [ERROR] %~dp0lang\en.ini is required.
    goto :failed
)
if not exist "%~dp0lib\libwim-15.dll" (
    echo [INFO] lib\libwim-15.dll not found yet: 7z_wimlib.exe downloads it when needed.
)

echo ==============================================
echo  7z_wimlib - build 7z_wimlib.exe
echo ==============================================
echo [INFO] Compiler : %CSC%
echo [INFO] Output   : %OUT_EXE%

if exist "%OUT_EXE%" del /f /q "%OUT_EXE%" 2>nul
if exist "%OUT_EXE%" (
    echo [ERROR] %OUT_EXE% is in use. Close 7z_wimlib first.
    goto :failed
)

"%CSC%" /nologo /target:winexe /platform:anycpu /optimize+ /warn:4 ^
    /out:"%OUT_EXE%" /win32manifest:"%MANIFEST%" %OPT_ICON% %OPT_RES% ^
    /r:System.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll ^
    /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll ^
    "%SRC_DIR%\*.cs"
if errorlevel 1 (
    echo [ERROR] The compiler reported errors ^(see above^).
    goto :failed
)
if not exist "%OUT_EXE%" (
    echo [ERROR] 7z_wimlib.exe was not created.
    goto :failed
)

echo.
echo [DONE] Built %OUT_EXE%
if /i not "%~1"=="nopause" pause
exit /b 0

:failed
echo.
echo [FAILED] 7z_wimlib.exe was not built.
if /i not "%~1"=="nopause" pause
exit /b 1


rem ============================================================
rem  :add_resource "<file>" "<code>"   adds /resource:<file>,Dvt.lang.<code>.ini
rem ============================================================
:add_resource
set OPT_RES=%OPT_RES% /resource:"%~1",Dvt.lang.%~2.ini
echo [INFO] Language : %~2
exit /b 0
