@echo off
REM ============================================================
REM  MuseScore fork - one-click update from upstream
REM  (musescore/MuseScore) + the TWO pinned submodules.
REM
REM  Steps:
REM    1. git fetch upstream
REM    2. git merge upstream/main     (onto the current branch)
REM    3. git submodule update --init --recursive
REM       ^-- CRITICAL: the superproject pins muse (muse_framework)
REM           and muse_deps; a pull/merge does NOT move the submodule
REM           working trees, so this re-syncs BOTH to the pinned
REM           commits. Skipping it = building against stale framework/deps.
REM
REM  Does NOT build or push. After it finishes clean:
REM    - rebuild:            "+Build (release)+.bat"
REM    - push fork (opt.):   git push origin main
REM ============================================================
setlocal
cd /d "C:\Dev\+MuseScore+"

echo === 1/3 Fetching upstream (musescore/MuseScore) ===
git fetch upstream
if errorlevel 1 goto :error

echo === 2/3 Merging upstream/main into the current branch ===
git merge upstream/main
if errorlevel 1 goto :error

echo === 3/3 Updating submodules (muse + muse_deps) to pinned commits ===
git submodule update --init --recursive
if errorlevel 1 goto :error

echo.
echo === Update complete. Both submodules synced to pinned commits. ===
echo Rebuild with:     "+Build (release)+.bat"
echo Push fork (opt.): git push origin main
goto :eof

:error
echo.
echo *** UPDATE FAILED - resolve the error above before building. ***
echo *** A merge conflict? run: git status, resolve files, git commit ***
exit /b 1
