@echo off
REM ============================================================
REM  Reprise fork - one-click update from upstream
REM  (musescore/MuseScore), two-branch model, + the TWO pinned
REM  submodules, then optionally push the fork and rebuild.
REM
REM  BRANCH MODEL:
REM    main         = PRISTINE mirror of upstream/main (NO dev commits ever).
REM    reprise-main = our dev branch (all Reprise work lives here).
REM  Upstream flows:  upstream/main  ->  main  ->  reprise-main.
REM
REM  Canonical flow (runs the SAME regardless of the branch you start on;
REM  it always ends on reprise-main):
REM    1. git fetch upstream
REM    2. checkout reprise-main (so we are not on main, and we end here)
REM    3. fast-forward local main to upstream/main  (git fetch upstream main:main)
REM       ^-- FF-ONLY. If main can't fast-forward, something committed to main
REM           (it must stay pristine) -> STOP and warn, change nothing.
REM    4. merge --no-edit main  into reprise-main  (bring upstream fixes in).
REM       STOP on conflicts (resolve, commit, push/build manually).
REM    5. git submodule update --init --recursive
REM       ^-- CRITICAL: the superproject pins muse (muse_framework) and
REM           muse_deps; a merge does NOT move the submodule working trees,
REM           so this re-syncs BOTH. Skipping it = stale framework/deps.
REM    6. If anything changed: offer to PUSH (origin main + origin reprise-main),
REM       then offer to BUILD.
REM ============================================================
setlocal
cd /d "C:\Dev\+MuseScore+"

echo === 1/5 Fetching upstream (musescore/MuseScore) ===
git fetch upstream
if errorlevel 1 goto :error

echo === 2/5 Switching to reprise-main (dev branch) ===
git checkout reprise-main
if errorlevel 1 goto :error

for /f "delims=" %%h in ('git rev-parse main') do set "MAIN_BEFORE=%%h"
for /f "delims=" %%h in ('git rev-parse reprise-main') do set "RM_BEFORE=%%h"

echo === 3/5 Fast-forwarding pristine main to upstream/main ===
git fetch upstream main:main
if errorlevel 1 goto :mainnotff

for /f "delims=" %%h in ('git rev-parse main') do set "MAIN_AFTER=%%h"

echo === 4/5 Merging upstream fixes (via main) into reprise-main ===
git merge --no-edit main
if errorlevel 1 goto :conflict

echo === 5/5 Updating submodules (muse + muse_deps) to pinned commits ===
git submodule update --init --recursive
if errorlevel 1 goto :error

for /f "delims=" %%h in ('git rev-parse reprise-main') do set "RM_AFTER=%%h"

echo.
if "%MAIN_BEFORE%"=="%MAIN_AFTER%" if "%RM_BEFORE%"=="%RM_AFTER%" goto :nochange

echo === Updated. ===
echo     main:         %MAIN_BEFORE:~0,9%  to  %MAIN_AFTER:~0,9%
echo     reprise-main: %RM_BEFORE:~0,9%  to  %RM_AFTER:~0,9%
echo.

set /p "DOPUSH=Push updated main and reprise-main to origin now? [Y/N]: "
if /i not "%DOPUSH%"=="Y" goto :skippush
echo.
echo === Pushing origin main ===
git push origin main
echo === Pushing origin reprise-main ===
git push origin reprise-main
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

:nochange
echo === Already up to date - no new upstream changes. ===
echo (On reprise-main.)
echo.
pause
goto :eof

:mainnotff
echo.
echo *** main could NOT be fast-forwarded to upstream/main. ***
echo *** main must stay a PRISTINE mirror of upstream - it looks like a commit ***
echo *** landed on main. Restore main to upstream/main (it should contain ONLY ***
echo *** upstream commits) before updating. Nothing was pushed. ***
echo.
pause
exit /b 1

:conflict
echo.
echo *** Merge conflict bringing upstream into reprise-main. ***
echo *** Resolve the conflicts, then run: git status, edit files, git add, git commit. ***
echo *** Submodules NOT yet updated and nothing was pushed - finish the merge, ***
echo *** then update submodules, push, and build manually. ***
echo.
pause
exit /b 1

:error
echo.
echo *** UPDATE FAILED - resolve the error above. Nothing was pushed. ***
echo.
pause
exit /b 1
