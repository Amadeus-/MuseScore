@echo off
REM ============================================================
REM  Make the Release build double-click runnable (standalone).
REM  Run this AFTER "+Build (release)+.bat". It does the FULL deploy:
REM    1. copies the 6 C-lib DLLs the exe imports next to the exe
REM       (opus, FLAC, FLAC++, fdk-aac, zlib1, ogg),
REM    2. runs windeployqt (Qt platform/style/imageformat plugins,
REM       core Qt DLLs, translations),
REM    3. copies the FULL Qt qml module tree next to the exe, and
REM    4. copies ALL Qt6 DLLs next to the exe,
REM    5. PRUNES the dead weight: removes every DEBUG DLL twin and
REM       all non-English locale/translation .qm files.
REM  Steps 3-4 are required because windeployqt only deploys what the
REM  MAIN exe imports -- it misses QML modules that are embedded in the
REM  app's resources (e.g. Qt5Compat.GraphicalEffects) and the Qt DLLs
REM  that only the QML plugins need (e.g. Qt6ShaderTools.dll). The
REM  blanket copy of steps 3-4 drags in the matching *d.dll DEBUG twins
REM  (~250 MB) and lrelease bakes ~140 language packs -- step 5 strips
REM  both back out so the standalone set is release-only + English.
REM
REM  Step 5 uses the TWIN RULE (delete "<name>d.dll" only when the
REM  release "<name>.dll" exists beside it) instead of a blind
REM  "del *d.dll" -- that protects RELEASE dlls whose name legitimately
REM  ends in 'd' (e.g. Qt tls\qschannelbackend.dll) and any Qt6*3D.dll.
REM
REM  After this, build.release\Reprise.exe runs by double-click.
REM  (Re-run after any clean rebuild of build.release.)
REM ============================================================
setlocal
set "QT_KIT=C:\Dev\Qt\6.11.2\msvc2022_64"
set "BR=C:\Dev\+MuseScore+\build.release"
set "D=%BR%\_deps"

if not exist "%BR%\Reprise.exe" (
  echo *** Not built: %BR%\Reprise.exe
  echo *** Run "+Build (release)+.bat" first.
  echo.
  pause
  exit /b 1
)

echo === 1/5 Copying the 6 C-lib DLLs next to the exe ===
copy /y "%D%\opus\bin\opus.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC.dll"       "%BR%\" >nul
copy /y "%D%\flac\bin\FLAC++.dll"     "%BR%\" >nul
copy /y "%D%\fdk-aac\bin\fdk-aac.dll" "%BR%\" >nul
copy /y "%D%\zlib\bin\zlib1.dll"      "%BR%\" >nul
copy /y "%D%\ogg\bin\ogg.dll"         "%BR%\" >nul

echo === 2/5 Deploying Qt plugins (windeployqt) ===
set "PATH=%QT_KIT%\bin;%PATH%"
"%QT_KIT%\bin\windeployqt.exe" --release "%BR%\Reprise.exe"

echo === 3/5 Copying the full Qt qml module tree ===
xcopy /e /i /y /q "%QT_KIT%\qml" "%BR%\qml" >nul

echo === 4/5 Copying all Qt6 DLLs ===
copy /y "%QT_KIT%\bin\Qt6*.dll" "%BR%\" >nul

echo === 5/5 Pruning DEBUG DLL twins and non-English locales ===
REM Remove every "<name>d.dll" whose release twin "<name>.dll" exists (anywhere under BR).
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-ChildItem -LiteralPath '%BR%' -Recurse -File -Filter *d.dll -ErrorAction SilentlyContinue | Where-Object { Test-Path -LiteralPath (Join-Path $_.DirectoryName ($_.BaseName.Substring(0,$_.BaseName.Length-1)+'.dll')) } | Remove-Item -Force -ErrorAction SilentlyContinue"
REM Keep only English .qm (qt_en, *_en, *_en_GB, *_en_US); drop the other ~140 language packs.
powershell -NoProfile -ExecutionPolicy Bypass -Command "@('%BR%\share\locale','%BR%\translations') | ForEach-Object { Get-ChildItem -LiteralPath $_ -File -Filter *.qm -ErrorAction SilentlyContinue | Where-Object { $_.Name -notmatch '_en(_[A-Za-z]{2})?\.qm$' } | Remove-Item -Force -ErrorAction SilentlyContinue }"

echo.
echo === Done. %BR%\Reprise.exe is now standalone (double-click it). ===
echo.
pause
