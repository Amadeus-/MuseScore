@echo off
REM ============================================================
REM  Make the Release build double-click runnable (standalone).
REM  Run this AFTER build-release.bat. It:
REM    1. copies the 6 C-lib DLLs the exe imports next to the exe
REM       (opus, FLAC, FLAC++, fdk-aac, zlib1, ogg), and
REM    2. runs windeployqt to drop the Qt DLLs/plugins/QML next to it.
REM  After this, build.release\MuseScoreStudio5.exe runs by double-click
REM  with no "DLL not found" errors and no PATH tricks.
REM
REM  (Re-run after any clean rebuild of build.release.)
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
set "BR=C:\Dev\+MuseScore+\build.release"
set "D=%BR%\_deps"

if not exist "%BR%\MuseScoreStudio5.exe" (
  echo *** Not built: %BR%\MuseScoreStudio5.exe
  echo *** Run build-release.bat first.
  exit /b 1
)

echo === Copying the 6 C-lib DLLs next to the exe ===
copy /y "%D%\opus\bin\opus.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC++.dll"     "%BR%\" >nul
copy /y "%D%\fdk-aac\bin\fdk-aac.dll" "%BR%\" >nul
copy /y "%D%\zlib\bin\zlib1.dll"      "%BR%\" >nul
copy /y "%D%\ogg\bin\ogg.dll"         "%BR%\" >nul

echo === Deploying Qt (windeployqt) ===
set "PATH=%QT_KIT%\bin;%PATH%"
"%QT_KIT%\bin\windeployqt.exe" --release "%BR%\MuseScoreStudio5.exe"

echo.
echo === Done. %BR%\MuseScoreStudio5.exe is now standalone (double-click it). ===
