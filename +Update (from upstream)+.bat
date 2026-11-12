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
REM    6. If anything changed:
REM       a. PUSH origin main immediately (pristine mirror, always safe).
REM       b. Build-verify reprise-main (run the release build) BEFORE pushing it.
REM          Only on build SUCCESS offer to PUSH origin reprise-main; on build
REM          FAILURE or decline, SKIP the push (a git-clean but uncompilable
REM          merge must not reach origin/reprise-main) and print a manual-push note.
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

REM --- main is the pristine upstream mirror: always safe to publish now. ---
set /p "DOPUSH=Push updated main to origin now? [Y/n]: "
if "%DOPUSH%"=="" set "DOPUSH=Y"
if /i not "%DOPUSH%"=="Y" goto :skipmainpush
echo.
echo === Pushing origin main ===
git push origin main
:skipmainpush

REM --- reprise-main is NOT pushed until a release build verifies the merge. ---
REM     A git-clean merge can still fail to compile; building here BEFORE the
REM     push keeps a broken reprise-main from ever reaching origin.
echo.
set /p "DOBUILD=Build-verify reprise-main before pushing it (runs the release build)? [Y/n]: "
if "%DOBUILD%"=="" set "DOBUILD=Y"
if /i not "%DOBUILD%"=="Y" goto :nobuild
echo.
echo Build type for the verify-build:
echo   1. Release        [default - just press ENTER]
echo   2. Debug
echo   3. RelWithDebInfo
set /p "BT=Choose [1/2/3]: "
if "%BT%"=="" set "BT=1"
if "%BT%"=="1" set "BTTOKEN=release"
if "%BT%"=="2" set "BTTOKEN=debug"
if "%BT%"=="3" set "BTTOKEN=relwithdebinfo"
if not defined BTTOKEN (
  echo Invalid choice "%BT%" - defaulting to Release.
  set "BTTOKEN=release"
)
echo.
call "C:\Dev\+MuseScore+\+Build (release)+.bat" -t %BTTOKEN%
if errorlevel 1 goto :buildfailed
echo.
echo === Build OK - reprise-main is build-verified. ===
set /p "DOPUSHRM=Push build-verified reprise-main to origin now? [Y/n]: "
if "%DOPUSHRM%"=="" set "DOPUSHRM=Y"
if /i not "%DOPUSHRM%"=="Y" goto :skiprmpush
echo.
echo === Pushing origin reprise-main ===
git push origin reprise-main
:skiprmpush
echo.
pause
goto :eof

:buildfailed
echo.
echo *** Build FAILED - reprise-main was NOT pushed: it is not build-verified. ***
echo *** After fixing and building, push manually with: ***
echo ***   git push origin reprise-main ***
echo.
pause
goto :eof

:nobuild
echo.
echo reprise-main was NOT pushed: it is not build-verified.
echo After fixing and building, push manually with:
echo   git push origin reprise-main
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
