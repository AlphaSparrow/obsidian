@echo off
REM ============================================================
REM  Obsidian Terminal — Build Windows .exe
REM ============================================================
REM  Produces: dist\ObsidianTerminal.exe (single file, ~20-30 MB)
REM ============================================================

echo [BUILD] Packaging Obsidian Terminal...

pyinstaller ^
    --onefile ^
    --windowed ^
    --name ObsidianTerminal ^
    --add-data "app;app" ^
    --icon "app\icon.ico" ^
    main.py

if %ERRORLEVEL% EQU 0 (
    echo [BUILD] Success: dist\ObsidianTerminal.exe
) else (
    echo [BUILD] Failed with error code %ERRORLEVEL%
)

pause
