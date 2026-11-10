@echo off
REM ============================================================
REM  MuseScore fork - one-click update from upstream
REM  (musescore/MuseScore) + the TWO pinned submodules, then
REM  optionally push your fork and rebuild.
REM
REM  Steps:
REM    1. git fetch upstream
REM    2. git merge --no-edit upstream/main   (onto the current branch;
REM       --no-edit accepts git's default merge-commit message, no editor)
REM    3. git submodule update --init --recursive
REM       ^-- CRITICAL: the superproject pins muse (muse_framework)
REM           and muse_deps; a pull/merge does NOT move the submodule
REM           working trees, so this re-syncs BOTH to the pinned
REM           commits. Skipping it = building against stale framework/deps.
REM    4. If anything changed: offer to PUSH the fork, then offer to BUILD.
REM
REM  If the merge hits conflicts it stops at step 2 (resolve, commit,
REM  then build manually) - the push/build prompts are skipped.
REM ============================================================
setlocal
cd /d "C:\Dev\+MuseScore+"

for /f "delims=" %%h in ('git rev-parse HEAD') do set "HEAD_BEFORE=%%h"

echo === 1/3 Fetching upstream (musescore/MuseScore) ===
git fetch upstream
if errorlevel 1 goto :error

echo === 2/3 Merging upstream/main into the current branch ===
git merge --no-edit upstream/main
if errorlevel 1 goto :error

echo === 3/3 Updating submodules (muse + muse_deps) to pinned commits ===
git submodule update --init --recursive
if errorlevel 1 goto :error

for /f "delims=" %%h in ('git rev-parse HEAD') do set "HEAD_AFTER=%%h"

echo.
if "%HEAD_BEFORE%"=="%HEAD_AFTER%" (
  echo === Already up to date - no new upstream changes. ===
  echo.
  pause
  goto :eof
)

echo === Updated:  %HEAD_BEFORE:~0,9%  to  %HEAD_AFTER:~0,9% ===
echo.

set /p "DOPUSH=Push the updated fork to origin/main now? [Y/N]: "
if /i not "%DOPUSH%"=="Y" goto :skippush
echo.
echo === Pushing to origin/main ===
git push origin main
:skippush

echo.
set /p "DOBUILD=Rebuild now (runs the release build)? [Y/N]: "
if /i not "%DOBUILD%"=="Y" goto :skipbuild
echo.
call "C:\Dev\+MuseScore+\+Build (release)+.bat"
goto :eof
:skipbuild

echo.
echo To build later:  "+Build (release)+.bat"
echo.
pause
goto :eof

:error
echo.
echo *** UPDATE FAILED - resolve the error above before building. ***
echo *** A merge conflict? run: git status, resolve files, git commit ***
echo.
pause
exit /b 1
