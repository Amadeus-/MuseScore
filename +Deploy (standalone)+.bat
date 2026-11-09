@echo off
REM ============================================================
REM  Make the Release build double-click runnable (standalone).
REM  Run this AFTER "+Build (release)+.bat". It does the FULL deploy:
REM    1. copies the 6 C-lib DLLs the exe imports next to the exe
REM       (opus, FLAC, FLAC++, fdk-aac, zlib1, ogg),
REM    2. runs windeployqt (Qt platform/style/imageformat plugins,
REM       core Qt DLLs, translations),
REM    3. copies the FULL Qt qml module tree next to the exe, and
REM    4. copies ALL Qt6 DLLs next to the exe.
REM  Steps 3-4 are required because windeployqt only deploys what the
REM  MAIN exe imports -- it misses QML modules that are embedded in the
REM  app's resources (e.g. Qt5Compat.GraphicalEffects) and the Qt DLLs
REM  that only the QML plugins need (e.g. Qt6ShaderTools.dll).
REM
REM  After this, build.release\MuseScoreStudio5.exe runs by double-click.
REM  (Re-run after any clean rebuild of build.release.)
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
set "BR=C:\Dev\+MuseScore+\build.release"
set "D=%BR%\_deps"

if not exist "%BR%\MuseScoreStudio5.exe" (
  echo *** Not built: %BR%\MuseScoreStudio5.exe
  echo *** Run "+Build (release)+.bat" first.
  exit /b 1
)

echo === 1/4 Copying the 6 C-lib DLLs next to the exe ===
copy /y "%D%\opus\bin\opus.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC++.dll"     "%BR%\" >nul
copy /y "%D%\fdk-aac\bin\fdk-aac.dll" "%BR%\" >nul
copy /y "%D%\zlib\bin\zlib1.dll"      "%BR%\" >nul
copy /y "%D%\ogg\bin\ogg.dll"         "%BR%\" >nul

echo === 2/4 Deploying Qt plugins (windeployqt) ===
set "PATH=%QT_KIT%\bin;%PATH%"
"%QT_KIT%\bin\windeployqt.exe" --release "%BR%\MuseScoreStudio5.exe"

echo === 3/4 Copying the full Qt qml module tree ===
xcopy /e /i /y /q "%QT_KIT%\qml" "%BR%\qml" >nul

echo === 4/4 Copying all Qt6 DLLs ===
copy /y "%QT_KIT%\bin\Qt6*.dll" "%BR%\" >nul

echo.
echo === Done. %BR%\MuseScoreStudio5.exe is now standalone (double-click it). ===
