# MuseScore — CI & Release

> **What this file is.** Scope: `.github\workflows\*` (22 workflows), `buildscripts\` (CI scripts, packaging, version stamping), `version.cmake` / `SetupConfigure.cmake` (version + build mode), Transifex translations (`.tx`, `tools\translations`), and crash reporting (Crashpad → Sentry; `muse\framework\diagnostics`, `muse_deps`). Source-verified deep pass (2026-11-08) from the workflow YAML and scripts on disk. **Live source wins** — workflow YAML and scripts churn fast (Qt version, runner images, action majors). Start at `00-core.md`.
>
> Paths are relative to `C:\Dev\+MuseScore+`. **Two script trees exist:** repo-local `buildscripts\ci\...` (app-specific wrappers; most delegate or duplicate) and the framework's `muse\buildscripts\ci\...` (shared; the workflows call into it for checksum, ccache, sentry, release tooling, S3, codestyle, coverage). Always check both when tracing a step.

## 0. 60-second orientation

- **One driver, many leaves.** `build_all.yml` is the orchestrator (`workflow_dispatch` + `workflow_call`). It mints **one build number** and fans out to `build_backend`, `build_linux`, `build_macos`, `build_windows`, and optionally `check_unit_tests` / `check_visual_tests`. `build_nightly.yml` and `deploy.yml` are the two callers that matter. PRs trigger the per-platform workflows directly (Linux, macOS, Windows-x64, without-qt, unit tests, vtests, codestyle, submodule check) — **not** `build_all`.
- **Every build is parameterised by `build_mode`**: `devel | nightly | testing | stable`. The mode flows through `make_build_mode_env.sh` → `build.artifacts\env\build_mode.env` → each platform script, which maps it to CMake's `MUSE_APP_BUILD_MODE` (`dev | testing | release`) and to a packaging/naming variant. See §3.
- **Artifacts** land in `build.artifacts\` with a small `env\*.env` side-channel (`build_mode`, `build_number`, `build_version`, `build_revision`, `build_branch`, `release_channel`, `artifact_name`) written by `buildscripts\ci\tools\make_*_env.sh`. Later steps (package, checksum, publish, release collation) read these instead of re-deriving.
- **Version** = `MUSE_APP_VERSION` from `version.cmake` (`5.0.0`) + `.` + CI build number (`yymmddHHM`, 9 digits). No version is stored anywhere else.
- **Fork reality:** most jobs gate on secrets (Transifex, Sentry, OSUOSL, S3, Apple, Windows signing). The workflows degrade gracefully (`::warning::` and a `DO_*=false` flag) rather than fail, and scheduled jobs are gated `github.repository == 'musescore/MuseScore'` — so on `Amadeus-/MuseScore` schedules do not run and signing/publishing is off.

## 1. CI workflow inventory

`.github\workflows\` — all 22 files. Runner names are as written (`ubuntu-slim`, `xcode-27`, `windows-2025` are the repo's labels; availability on a fork is **UNCLEAR**).

| Workflow (name) | Trigger | What it does |
|---|---|---|
| `build_all.yml` ("Build: All") | `workflow_dispatch`, `workflow_call` | Orchestrator. Inputs: `platforms` (default `backend linux_arm64 linux_x64 macos windows_x64 windows_portable`), `build_mode` (default `devel`), `publish`, `deploy_backend`, `sentry_project`, `dockwidgets_v2` (default `ON`). Job `generate_build_number` runs `make_build_number.sh` once → output `build_number` → passed to `backend`/`linux`/`macos`/`windows`. Extra platform tokens: `utest`, `vtest`, `dummy` (matrix of ubuntu-22.04 / xcode-27 / windows-2025 producing a fake artifact to test the plumbing). |
| `build_linux.yml` | `pull_request`, `workflow_dispatch`, `workflow_call` | Matrix `linux_x64` (ubuntu-24.04, Qt `linux_gcc_64`) + `linux_arm64` (ubuntu-24.04-arm, `linux_gcc_arm64`); builds an **AppImage** (`ninja_build.sh -t appimage`), optional Sentry upload, optional OSUOSL publish, ccache restore/save. |
| `build_macos.yml` | `pull_request`, `workflow_dispatch`, `workflow_call` | One job `macos_universal` on `xcode-27`: universal `arm64;x86_64` build, optional dSYM + Sentry, optional keychain/sign/notarize, `.dmg`. |
| `build_windows.yml` | `pull_request`, `workflow_dispatch`, `workflow_call` | Two jobs: `windows_x64` (7z for devel/nightly, **MSI** for testing/stable) and `windows_portable` (PortableApps `.paf.exe`; **skipped on `pull_request`**, line 313). Both `windows-2025`, Qt `win64_msvc2022_64`. |
| `build_backend.yml` | `schedule` Thursday 03:00 (main repo only), `workflow_dispatch`, `workflow_call` | Headless "converter" backend: Linux AppImage → extracted → `7z` with a `convertor` launcher; optional S3 upload + Docker image publish + S3 config deploy. |
| `build_wasm.yml` | `workflow_dispatch`, `workflow_call` (**PR trigger commented out**, line 4) | Qt-for-wasm (`wasm_singlethread`, host `all_os`) build of configuration `app-web`, plus a second build of `src/web/audioengine`. Not called by `build_all`. |
| `build_without_qt.yml` | `pull_request`, `workflow_dispatch` | Compiles `tools/check_build_without_qt` (CMake+Ninja Debug) to prove the Qt-free core still builds. |
| `build_nightly.yml` | `schedule` 03:00 (main) and 04:00 (release branch), `workflow_dispatch` | A `github-script` step calls `createWorkflowDispatch` on `build_all.yml` with `platforms: 'linux_arm64 linux_x64 macos windows_x64 windows_portable utest vtest'`, `build_mode: nightly`, `publish: on`. Schedule gated on `musescore/MuseScore`. |
| `check_unit_tests.yml` | `pull_request`, `workflow_dispatch`, `workflow_call`, `schedule` Thu 03:00 | `utest` configuration build on ubuntu-24.04, `ctest -V`; optional code coverage (forced on for `schedule`) → artifact, and published to `musescore/mss_code_coverage` (main repo only). See `testing-and-vtest.md`. |
| `check_visual_tests.yml` | `pull_request`, `workflow_dispatch`, `workflow_call` | Builds the *current* and a *reference* commit in parallel, generates PNGs from each, compares, uploads a diff artifact, **fails the PR if anything differs**. See `testing-and-vtest.md`. |
| `check_codestyle.yml` | `pull_request`, `workflow_dispatch` | `cmake -P muse/buildscripts/ci/checkcodestyle/checkcodestyle.cmake` over `./src/` ("MSS") and `./muse/framework/` ("MF"). |
| `check_submodules.yml` | `pull_request`, `workflow_dispatch` | Fails if `.gitmodules` `submodule.muse_framework.url` ≠ `https://github.com/musescore/muse_framework.git`, or if the pinned `muse` SHA is not an ancestor of upstream `main`. (A third "must equal upstream HEAD" step is disabled with `if: false`.) |
| `deploy.yml` ("Deploy") | `workflow_dispatch` only | The release pipeline: `plan` (compute tag/release name) → `build` (calls `build_all`) → `update_learn_playlists` → `create_release` → `notify_users`. §5. |
| `translate_lupdate.yml` | `schedule` Sunday 12:00 (main repo), `workflow_dispatch` | Runs `tools/translations/run_lupdate.sh`, opens a PR (`ci_lupdate*`, label `strings`) updating `share/locale/*`. Schedule or `cleanup_obsolete=true` adds `-no-obsolete`. |
| `translate_tx_pull_to_s3.yml` | `schedule` every 15 min (Sunday 12:00 is the special slot), `workflow_dispatch` | `tx pull -f` from Transifex; on the Sunday-12:00 slot opens a PR; on every run (if enabled) lrelease-packs changed languages and pushes them to S3 for the in-app language downloader. §6. |
| `translate_tx_push.yml` | `workflow_dispatch` (requires retyping the branch name) | `tx push -s` — pushes source strings to Transifex. |
| `cleanup_ccache.yml` | hourly at :30, `workflow_dispatch` | Runs `buildscripts/ci/tools/cleanup_ccache.py` with `GH_TOKEN` (actions: write) to prune GitHub Actions ccache entries. |
| `update_release_info.yml` | `workflow_dispatch`, `workflow_call` | Builds `release_info.json` from the GitHub release (`make_release_info_file.sh` + `correct_release_info.py`), pushes it to the updates feed on S3 (stable vs `.test` feed), and appends to the "all releases" notes file. |
| `update_release_info_clear.yml` | `workflow_dispatch` | Pushes an *empty* release-info file to the feed (retract the "latest release" notice). |
| `update_learn_playlists.yml` | `workflow_dispatch`, `workflow_call` | Fetches the YouTube "started" playlist via the YouTube API and pushes `started_playlist[.test].json` to `s3://extensions.musescore.org/4.0/learn/`. |
| `triage_issues.yml` | `issues` opened/labeled/unlabeled | Auto-label via `issue_add_label_config.yml`; add labeled issues to org projects (engraving 23, cloud 22, MuseSounds 21, VST 20, design 26); assign by label. Main repo only. |
| `triage_prs.yml` | `pull_request_target` opened | If author is not in `.github/team_members.txt`, add the PR to org project 117 (community-PR triage). Main repo only. |

Other `.github` content: `dependabot.yml`, `issue_add_label_config.yml`, `issue_assign_by_label_config.yml`, `pull_request_template.md`, `ISSUE_TEMPLATE\{bug-report,feature_request,config}.yml`, `problem_matchers\{gcc,msvc}.json` (registered in Linux/Windows/Wasm builds so compiler errors annotate the PR).

### Cross-cutting workflow mechanics (identical in linux/macos/windows)

- **Concurrency:** `group: ${{ github.workflow }} ${{ github.event_name }} ${{ github.ref }} ${{ github.event.issue.number }} <suffix>` with `cancel-in-progress: true` — the suffix (`linux`, `macos`, `windows`, `backend`, `wasm`, `utest`, `vtest`) is needed because a `workflow_call` child inherits the *root* workflow's name (comment at `build_linux.yml:81-82`).
- **"Configure workflow" step** (the same ~80 lines pasted per platform, e.g. `build_linux.yml:131-213`) computes env flags into `$GITHUB_ENV`:

  | Flag | True when |
  |---|---|
  | `DO_UPDATE_TS` | mode is `testing` or `stable` **and** `TRANSIFEX_API_TOKEN` set → `tx pull -f` before build |
  | `DO_PLACEHOLDER_TRANSLATIONS` | mode is `nightly` or `devel` → `--generate-placeholder-translations` |
  | `DO_UPLOAD_SYMBOLS` | `SENTRY_PROJECT == mu4` (default for `stable`, needs `SENTRY_MUSE_MU4_KEY`) **or** `mu4-sandbox` (needs `SENTRY_MUSE_MU4_SANDBOX_KEY`) |
  | `SENTRY_URL` | the minidump endpoint baked into the build (`…/api/4/minidump/?sentry_key=…` for mu4, `…/api/3/…` for sandbox). **Nightly with no project** still gets the sandbox URL but `DO_UPLOAD_SYMBOLS` stays false ("files are large… costly", `:180`). |
  | `DO_PUBLISH` | `publish == on` **and** `OSUOSL_SSH_ENCRYPT_SECRET` set |
  | macOS only: `DO_SIGN`, `DO_NOTARIZE` | `MAC_SIGN_CERTIFICATE_ENCRYPT_SECRET` present; notarize additionally requires mode ≠ `devel` and `APPLE_USERNAME`/`APPLE_PASSWORD` |

- **Qt:** `jurplel/install-qt-action@v4`, **Qt 6.11.2**, modules `qt5compat qtnetworkauth qtshadertools qtwebsockets` (wasm drops `qtnetworkauth`). Pinned in every workflow separately — bumping Qt means editing all of them (`build_linux/macos/windows/backend/wasm`, `check_unit_tests`, `check_visual_tests` ×2, `translate_*`).
- **ccache:** restore/save to `.ccache` keyed `<os>-<arch>-ccache-` (utests/vtests/wasm use their own keys); saving skipped on `pull_request`. Windows additionally skips saving when symbols are being uploaded (debug-info cache is huge, `build_windows.yml:298`). `muse/buildscripts/ci/tools/setup_ccache_config.sh` configures it.
- **`- parallel:` / `background: true` step keys** appear in these workflows (e.g. `build_linux.yml:215`, `check_unit_tests.yml:67`). They are not part of the GitHub Actions schema I know — see UNCLEAR.
- **Framework override:** the `app_repo/app_ref/framework_repo/framework_ref` `workflow_call` inputs let a caller check out a different MuseScore ref and/or overlay a different `muse_framework` into `muse/` (a second `actions/checkout` with `path: muse`).
- **Problem matchers**, `ccache -sv` summary, `Upload artifacts on GitHub` (`actions/upload-artifact@v7`, name `MU4_<number>_<Lin|Mac|Win|Win_portable|Wasm>…`).

## 2. Platform build + packaging matrix

All real builds go through `ninja_build.sh` (Linux/macOS) / `ninja_build.bat` (Windows), which translates env vars `MUSESCORE_*` / `MUSE_APP_*` into CMake `-D` flags (`ninja_build.sh:95-120`). Build trees: `build.release` / `build.debug`; install prefixes `../MuseScore` (AppImage), `../applebuild`, `../build.install`.

| Platform | Runner / Qt arch | CI script chain | `ninja_build` target | Package type → artifact name |
|---|---|---|---|---|
| **Linux x86_64** | ubuntu-24.04 / `linux_gcc_64` | `ci/linux/setup.sh` → `ci/linux/build.sh` → `ci/linux/package.sh` → `tools/make_appimage.sh` | `appimage` = `RelWithDebInfo` + `ninja install/strip` (`ninja_build.sh:201-224`) | **AppImage** (+`.zsync` for stable/nightly): `MuseScore-Studio-<ver>-<arch>.AppImage`; nightly `MuseScore-Studio-Nightly-<num>-<branch>-<rev>-<arch>.AppImage` |
| **Linux aarch64** | ubuntu-24.04-arm / `linux_gcc_arm64` | same, `--arch aarch64` | same | same; **update module forced OFF** (`ci/linux/build.sh:74-76`, "current broken functionality") |
| **macOS universal** | `xcode-27` / `clang_64` | `ci/macos/build.sh` → (generate_dsym, sentry) → `setup_keychain.sh` → `ci/macos/package.sh` → `buildscripts/packaging/macOS/package.sh` | `install` (Release), `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` | **DMG**: `MuseScore-Studio-<ver>.dmg`; app/volume names vary by mode (`MuseScore 5`, `… Development`, `… Nightly`, `… Testing`) |
| **Windows x64** | windows-2025 / `win64_msvc2022_64` | `ci/windows/setup.bat` → `ci/windows/build.bat` → (sentry) → `ci/windows/package.bat` | `installrelwithdebinfo` (`MUSESCORE_DEBUG_INFO` ON only if uploading symbols) | devel/nightly → **7z**; testing/stable → **MSI** (WiX v3.11 via CPack); names `MuseScore-Studio-<ver>-x86_64.{7z,msi}` |
| **Windows portable** | same | `setup.bat --portable ON`, `build.bat --portable ON`, `package.bat --portable ON` | `installrelwithdebinfo`, configuration `app-portable` | **PortableApps** `…-x86_64.paf.exe` (launcher + installer generators downloaded from S3 `utils.musescore.org`, `ci/windows/setup.bat:38-44`). No crash URL passed (`build_windows.yml:439`) |
| **Backend** | ubuntu-24.04 / `linux_gcc_64` | `ci/backend/{setup,build,package}.sh` | `appimage` with `MUSESCORE_BUILD_IMPEXP_VIDEOEXPORT_MODULE=ON` | **7z** `MuseScore-<ver>.7z` containing the extracted AppImage + `convertor` launcher (`ci/backend/package.sh:81`, `convertor.in`) |
| **Wasm** | ubuntu-24.04 / `wasm_singlethread` | `ci/linux/setup.sh --arch wasm` → `ci/wasm/build.sh` → `ci/wasm/package.sh` | `release`, `MUSESCORE_BUILD_CONFIGURATION=app-web`; then `cmake ../src/web/audioengine` | (see `ci/wasm/package.sh` — not read in detail) |
| **Without Qt** | ubuntu-24.04 | `ci/withoutqt/{setup,build}.sh` | n/a — `tools/check_build_without_qt` | none (compile check) |
| **utest** | ubuntu-24.04 | `ci/linux/build_utest.sh` → `ci/linux/runutests.sh` | `installdebug`, configuration `utest` | none; JUnit-style xml via `GTEST_OUTPUT=xml:test-results` |
| **vtest** | ubuntu-24.04 | `ci/vtests/build_and_pack.sh`, `generate_pngs.sh` | `appimagedebug`, configuration `vtest` | `MuseScore-Studio-vtest.AppImage` (internal) |

**Linux AppImage specifics** (`ci/linux/tools/make_appimage.sh`): downloads `appimagetool`, `linuxdeploy`, `linuxdeploy-plugin-qt` (`continuous` releases) into `$HOME/build_tools`; bundles Qt + libs; **removes** system-glib libs (`libglib/gio/gmodule/gobject`) and `libwayland-client` so the host's are used; adds Wayland platform plugins (use `MU_QT_QPA_PLATFORM=wayland`); **fallback libraries** (loaded only if host lacks them): `libjack.so.0`, `libOpenGL.so.0`, `libsystemd.so.0`, optionally `libpipewire-0.3.so.0`, and a `libnss3` set (Chromebook). Bundles `libssl/libcrypto 1.1` on x86_64 only (for "Save Online"). Embeds `appimageupdatetool` only when `UPDATE_INFORMATION` is set — `stable` → `gh-releases-zsync|musescore|MuseScore|latest|…`, `nightly` → `zsync|https://ftp.osuosl.org/pub/musescore-nightlies/linux/<N>x/nightly/MuseScore-Studio-Nightly-latest-<branch>-<arch>.AppImage.zsync`, all other modes **no updates** (`ci/linux/package.sh:89-93`).

**macOS packaging** (`buildscripts/packaging/macOS/package.sh`): `macdeployqt` (with `-qmldir=.` and the QuickLook `.appex` as extra executable) using `-sign-for-notarization=` when signing, else ad-hoc `-codesign=-`; deletes `*.dSYM`; **renames `Resources/qml` → `Resources/qml_mu`** (and patches `qt.conf`) so third-party VST3 plugins with their own QML don't pick up MuseScore's (issues #21372/#24331); re-signs appex, bundled dylibs, then the app with `entitlements.plist`; notarizes + staples the **.app** (zip via `ditto`), builds an APFS DMG (800 MB blank, Finder-scripted layout), compresses to ULFO, signs, **notarizes + staples the DMG as a second submission**, runs `syspolicy_check distribution`. `notarize_file` retries up to 3× (30 s apart) but not on `Invalid/Rejected`. `APPLE_TEAM_ID` is hard-coded in `build_macos.yml:79` (public by design).

**Windows packaging** (`ci/windows/package.bat`): `PACKAGE_TYPE` — `portable` if `--portable ON`; else `7z` (devel/nightly) or `msi` (testing/stable). **Signing only for `msi` and `portable`**, and only if both `--signkey`/`--signsecret` exist (`:54-70`). Signing is a **remote service over S3**: `sign_service_aws.sh` uploads to `s3://muse-sign/prod-unsigned/<file>`, polls `prod-signed/` every 10 s up to 60× (10 min), reads `prod-failed/<file>.error` on failure, then swaps the signed file in. For MSI/portable the `*.exe/*.dll` in the install dir are zipped, signed as one blob, unzipped back **before** packing (`:SIGN_INSTALL_DIR`), then the final installer is signed again. MSI via `cpack -G WIX` (`SetupWindowsPackaging.cmake`: `CPACK_PACKAGE_VERSION` = `5.0.0.<build>`, `CPACK_WIX_PRODUCT_GUID` = fresh `uuidgen` per build, `CPACK_WIX_UPGRADE_GUID` = secret `WIN_MSI_STABLE_MU4_GUID` for stable else `WIN_MSI_TESTING_MU4_GUID`, `MUE_PACK_INSTALL_DIR=ON` packs the already-signed install dir instead of re-running install rules). The WiX template conditions ProgId/shortcut names on `MUSE_APP_RELEASE_CHANNEL` and `MUSE_APP_IS_PRERELEASE` (`WIX.template.in:6-12,193+`).

**Publishing to OSUOSL** (`buildscripts/ci/tools/osuosl/publish.sh`): decrypts `osuosl_nighlies_rsa.enc` (sic) with `OSUOSL_SSH_ENCRYPT_SECRET` via 7z, `scp` to `musescore-nightlies@ftp-osl.osuosl.org:~/ftp/<os>/<major>x/<nightly|testing|stable>/`; for nightly: `ln -sf` a `…Nightly-latest-<branch>-<arch>.<ext>` symlink, rename the `.zsync` to the stable `latest` name, **prune to `(40 days + 2 today[+1 zsync]) × 2 branches × variants`** files, upload `index.html`, run `~/trigger-musescore-nightlies`. Valid modes there: nightly/testing/stable only (devel exits). Public URL `https://ftp.osuosl.org/pub/musescore-nightlies/<path>`.

**Checksums:** every platform runs `muse/buildscripts/ci/tools/checksum.sh` → `openssl sha256 <artifact> > build.artifacts/checksum.txt`; `deploy.yml`'s `collate_release_binaries.sh` re-hashes with `sha256sum` and **fails on mismatch** (guards against corruption between build and release).

## 3. Versioning, build modes & release channels

### 3.1 Where each piece comes from

```cmake
# version.cmake:24-34
set(MUSE_APP_VERSION_MAJOR "0")
set(MUSE_APP_VERSION_MINOR "1")
set(MUSE_APP_VERSION_PATCH "0")
set(MUSE_APP_VERSION "${MUSE_APP_VERSION_MAJ_MIN}.${MUSE_APP_VERSION_PATCH}")
set(MUSE_APP_VERSION_LABEL "")
if (NOT CMAKE_BUILD_NUMBER)  set(CMAKE_BUILD_NUMBER "0")  endif()
```

- ⭐ **Reprise owns its version: `0.1.0`** (changed from the inherited MuseScore `5.0.0`). `version.cmake` is the single source of truth; everything downstream (config header `MUSE_APP_VERSION`, splash, About, WiX `CPACK_PACKAGE_VERSION`, the exe VERSIONINFO) derives from these three numbers — no parallel version was introduced. **`1.0.0` is RESERVED; everything `<1.0` is pre-release.** Only the MAJOR/MINOR/PATCH numbers were changed; the build-number/revision/label plumbing is untouched.
- **Exe VERSIONINFO (Reprise addition):** upstream `Reprise.exe` carried **no** version resource (`share/icons/windows_icons.rc` is icons only; `Get-Item … .VersionInfo.FileVersion` was empty). Reprise adds `share/icons/windows_version.rc.in` → `configure_file`d to `build.release/src/app/windows_version.rc` and appended to `MSCORE_APPEND_SRC` (`src/app/CMakeLists.txt`, in the `OS_IS_WIN` block). It is fed from `MUSE_APP_VERSION*` (same source of truth), so FileVersion/ProductVersion string = `0.1.0` (binary FILEVERSION = `0,1,0,0` — the 10-digit build number overflows the 16-bit field, so the 4th field is `0`; the full build number lives in WiX/CPack, not the exe fixed-info).

- `version.cmake` is **also runnable as a script** (`cmake -P version.cmake`): it ends with `message(STATUS "MUSE_APP_RELEASE_CHANNEL …")` / `"MUSE_APP_VERSION …"` (`:54-55`) *specifically for CI*, which parses them with `sed` (`make_version_env.sh:29`, `make_release_channel_env.sh:36`, `make_tag_name.sh:65`).
- **Build number** (`make_build_number.sh:25-26`): `date -u +%y%m%d%H%M`, then `${NUMBER:0:9}` — i.e. **10-minute resolution, 9 digits** ("less than 2147483647 to fit in Int32 for WiX"). E.g. `5.0.0.261108031`. Can be overridden by arg (used when `build_all` passes one number to all platforms, so all artifacts in a run share it). Local builds use `12345678` (`ninja_build.sh:36`) or `0` from `version.cmake`.
- **Full version** = `$MUSE_APP_VERSION.$BUILD_NUMBER` (`make_version_env.sh:31`) → `build_version.env`. **Revision** = `git rev-parse --short=7 HEAD` → `build_revision.env`. **Branch** = `git rev-parse --abbrev-ref HEAD` with `/`→`_` → `build_branch.env`.
- The runtime version/build/revision reach the app via `SetupConfigure.cmake:314-316` (`MUSE_APP_REVISION`, `MUSE_APP_BUILD_NUMBER`, `MUSE_APP_INSTALL_PREFIX`) → framework config header, then `configure_file(src/app/app_config.h.in → app_config.h)` (`:405`). Crashpad annotates every report with `sentry[release] = fullVersion + "." + build` (`crashhandler.cpp:71`).

### 3.2 Build mode → CMake mode → everything else

| CI `build_mode` | Intended use (`make_build_mode_env.sh:52-57`) | `MUSE_APP_BUILD_MODE` | Install `SUFFIX` (Linux) | Windows pkg | `MUSE_APP_UNSTABLE` | `IS_PRERELEASE` | `ALLOW_UPDATE_ON_PRERELEASE` |
|---|---|---|---|---|---|---|---|
| `devel` | PR builds (default for `pull_request` event) | `dev` | `dev` | 7z | ON | ON | OFF |
| `nightly` | scheduled (default for `schedule` event) | `dev` | `nightly` | 7z | ON | ON | OFF |
| `testing` | alpha/beta/rc, manual | `testing` | `testing` | MSI | OFF | ON | **ON** |
| `stable` | release, manual | `release` | `""` | MSI | OFF | OFF | OFF |

(`workflow_dispatch` with no `build_mode` is an **error** — `make_build_mode_env.sh:40`.) Linux/backend/wasm map via a `case`; macOS via `if`s; Windows via nested `IF … ELSE` (`ci/windows/build.bat:34-40`) — **keep all four in sync when adding a mode**.

`SetupConfigure.cmake:47-69`:

```cmake
if(BUILD_MODE STREQUAL "DEV")     # UNSTABLE ON, channel "dev",     IS_PRERELEASE ON,  ALLOW_UPDATE_ON_PRERELEASE OFF
if(BUILD_MODE STREQUAL "TESTING") # UNSTABLE OFF, channel "Testing", IS_PRERELEASE ON,  ALLOW_UPDATE_ON_PRERELEASE ON
if(BUILD_MODE STREQUAL "RELEASE") # UNSTABLE OFF,                   IS_PRERELEASE OFF, ALLOW_UPDATE_ON_PRERELEASE OFF
if (MUSE_APP_UNSTABLE)
    set(MUSE_APP_GUI_IDENTIFIER "${MUSE_APP_GUI_IDENTIFIER}Development")   # :67-69
```

- ⭐ **The "Development" suffix** is not about the word "devel": it's `MUSE_APP_UNSTABLE`, which is ON only for `dev` (devel **and** nightly). `MUSE_APP_GUI_IDENTIFIER` becomes `org.musescore.MuseScoreStudio5Development` (base is `org.musescore.${MACHINE_READABLE}${MAJOR}`, `version.cmake:36`). A local `cmake` run with no mode defaults to `dev` (`SetupConfigure.cmake:29-31`), so a local build is a "Development" build. Testing and stable use the plain identifier (so testing and stable share settings/identity — **UNCLEAR whether intentional**; coexistence is handled by the Windows ProgId/shortcut name and the install suffix instead).
- ⭐ **Where the "Unstable prerelease" label actually renders (TRACED):** the **splash** (`loadingscreenview.cpp:91-93`) draws **only** `"Version %1"` from `BaseApplication::appFullVersion()` — it has **no** unstable/prerelease banner at all, so a local `dev`-mode build already shows a clean `Version 0.1.0` on the splash. The unstable wording appears in exactly two places, both gated on `appUnstable()` = `#ifdef MUSE_APP_UNSTABLE`: **Help▸About** (`aboutmodel.cpp:44-45` → "Unstable prerelease for 0.1.0") and the **CLI** `--long-version` (`commandlineparser.cpp:577-579`). `MUSE_APP_UNSTABLE` also drives a **behavioural** side effect — the app name gets a `"Development"` suffix (`main.cpp:123-124`), which changes the QSettings/user-data profile path — so flipping it is not purely cosmetic. For a local build `MUSE_APP_BUILD_MODE` defaults to `dev` (`ninja_build.sh:35`), so `SetupConfigure.cmake` forces `MUSE_APP_UNSTABLE ON` regardless of `version.cmake`'s hard-coded `set(MUSE_APP_UNSTABLE ON)`. To present as a stable release (clean About/CLI, bare app name) build with `MUSE_APP_BUILD_MODE=release` — the same mechanism CI uses for `stable` — rather than editing the flag; and note that for a sub-1.0 pre-release the About wording is arguably *correct*. The splash goal (clean `0.1.0`, no banner) needed no flag change.
- **Release channel strings are inconsistent** (TRACED): `version.cmake` emits `devel/testing/stable` (substring-matching `MUSE_APP_BUILD_MODE MATCHES "dev"|"testing"|"release"`, `:41-51`); `SetupConfigure.cmake` then **overrides** DEV→`"dev"` and TESTING→`"Testing"` and leaves RELEASE as `version.cmake`'s `"stable"`. CI's `release_channel.env` (from running `version.cmake` alone) therefore says `devel`/`testing`/`stable`, while the compiled-in/WiX value is `dev`/`Testing`/`stable`. Consumers: WiX (`WIX.template.in:6,11-12`), `release_channel.env`. See UNCLEAR for whether the WiX `!= ""` branch is ever empty.
- **Update behaviour:** `MUSESCORE_ALLOW_UPDATE_ON_PRERELEASE` (`app_config.h.in:59`) → `UpdateConfiguration::init` default `allowUpdateOnPreRelease` (`muse\framework\update\internal\updateconfiguration.cpp:49-55`) — only **testing** builds default to accepting pre-release updates. `WIN_PORTABLE` forces `MUSE_MODULE_UPDATE OFF` (`SetupConfigure.cmake:104-106`); Linux aarch64 CI also disables it.
- **Build configurations** (`MUSESCORE_BUILD_CONFIGURATION`, `SetupConfigure.cmake`): `app` (desktop), `app-portable` (Windows portable; `MUSE_MODULE_UPDATE` off), `app-web` (wasm: dozens of modules OFF — no network/VST/cloud/midi/extensions/updates, `:115-195`), `vtest` (headless: UI without QML, ASAN, converter+notation+project only, crashpad off, `:200-271`), `utest` (`MUSE_ENABLE_UNIT_TESTS ON`, ASAN unless coverage, many QML-ish modules off unless coverage builds everything, `:276-309`). `MUSE_MODULE_AUDIO_WORKMODE` is `3` (driver callback + worker RPC) for app/portable, `2` for web.
- **Debug info / strip** (ADR 00105): release is built `RelWithDebInfo` and stripped at install (`install/strip` on Linux AppImage); Windows passes `MUSESCORE_DEBUG_INFO` (= `DO_UPLOAD_SYMBOLS`) so ccache stays small when not uploading (`build_windows.yml:245-246`, `ninja_build.sh:86-93`). macOS stripping is done by `macdeployqt`.

### 3.3 Channels and where artifacts live

| Channel | Produced by | Location | Notes |
|---|---|---|---|
| devel | PR workflows | GitHub Actions artifacts only (`MU4_<num>_<Lin/Mac/Win>_<pr#>_<title>`) | never published (publish needs `inputs.publish`, absent on PRs; OSUOSL rejects devel) |
| nightly | `build_nightly` → `build_all` (publish on) | OSUOSL `…/<os>/5x/nightly/` + `Nightly-latest-<branch>-<arch>` symlink; GitHub artifact | retained 40 days; two branches (main + `CURRENT_RELEASE_BRANCH`) |
| testing | `deploy.yml` with `release_type` ≠ stable | OSUOSL `…/testing/` if `create_tag`; draft **prerelease** on GitHub | updates feed `*.test.xml`, learn playlists `*.test.json` |
| stable | `deploy.yml` with `release_type=stable` | OSUOSL `…/stable/`, draft release on GitHub, updates feed `latest.xml`/`all.xml` | symbols to Sentry project `mu4` |

## 4. Local-vs-CI note (hand-off to the build playbook)

CI scripts are the *authoritative recipe* for flag combinations (they show which `MUSESCORE_*` env vars a "real" build sets: `MUSESCORE_BUILD_VST_MODULE=ON`, `MUSESCORE_BUILD_WEBSOCKET=ON`, `MUSESCORE_BUILD_PIPEWIRE_AUDIO_DRIVER`, `MUSESCORE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2`, `MUSESCORE_COMPILE_USE_PCH=OFF` on Windows CI, `MUSESCORE_RUN_WINDEPLOYQT=ON`). For the *local* build procedure see the agent playbook `build-from-source.md` (STUB at time of writing); `ninja_build.sh -h` lists targets (`release, debug, relwithdebinfo, install, installrelwithdebinfo, installdebug, clean, compile_commands, revision, appimage[debug]`). `ninja_build.sh -t revision` writes `local_build_revision.env` for `TryUseLocalRevision.cmake`. Unit/visual test workflows are the same builds a developer runs locally — see `testing-and-vtest.md`.

### 4.1 Reprise slim build + standalone deploy (fork-specific)

Two fork-local size optimisations cut the Windows deploy footprint from ~507 MB to ~206 MB (measured on `build.release`: exe + root DLLs + `qml\` + `translations\` + `share\locale`):

- **Module overrides in the top-level `CMakeLists.txt`** (a `set(<OPT> OFF CACHE BOOL "Reprise slim" FORCE)` block *before* `include(MuseDeclareOptions.cmake)`, same mechanism as the existing `MUSE_MODULE_UPDATE OFF` line; FORCE beats both the stale cache and `ninja_build.sh`'s `-D` defaults). **Dropped** (OFF): `MUSE_MODULE_MUSESAMPLER`, `MUSE_MODULE_AUDIOPLUGINS`, `MUSE_MODULE_TESTFLOW`, and import/export `MUE_BUILD_IMPEXP_{GUITARPRO,MNX,MEI,OVE,CAPELLA,BWW,TABLEDIT,VIDEOEXPORT}_MODULE` — only ~6 MB off the exe (the modules link statically into `Reprise.exe`; most savings are Part A below). **Must stay ON** (each breaks the app if dropped): `MUSE_MODULE_DIAGNOSTICS` (no stub + `appshell_qml` imports `muse_diagnostics_qml` unconditionally -> configure error; its heavy crashpad_handler is separately gated OFF); `MUSE_MODULE_{CLOUD,LEARN,TOURS,EXTENSIONS}` and `MUE_BUILD_MUSESOUNDS_MODULE` (stub when off, but the desktop Home page SIGSEGVs against the stubs at startup — upstream only ships these stubs in the web build); `MUE_BUILD_BRAILLE_MODULE` (`notationscene/.../NotationView.qml` imports `MuseScore.Braille`, which the braille stub's QML module does not register -> main-window QML fails to load). After flipping a module ON/OFF you must update the stale `build.release/CMakeCache.txt` entry (or wipe the cache) — the build script passes no `-D` for these, so `option()` keeps the cached value.
- **`+Deploy (standalone)+.bat` prunes the dead weight** windeployqt + the blanket `Qt6*.dll` / full-`qml\` copy drag in: step 5 removes every DEBUG DLL twin (~250 MB) using the **twin rule** (delete `*d.dll` only when the release `*.dll` exists beside it — protects release DLLs whose name ends in `d`, e.g. `tls\qschannelbackend.dll`, and `Qt6*3D.dll`), and deletes all non-English `.qm` from `share\locale` + `translations` (~44 MB; keeps `qt_en` / `*_en` / `*_en_GB` / `*_en_US`). **Note:** `lrelease` regenerates all ~140 language `.qm` into `build.release\share\locale` on every build, so the locale trim is a *deploy-time* step (re-run the deploy, or the trim, after each rebuild). Pruning is done via embedded `powershell` calls from the `.bat`.
- **C4702 "unreachable code" flood from Qt headers is suppressed external-scoped**, kept in a merge-safe fork file `RepriseBuildFlags.cmake` (repo root) that the top-level `CMakeLists.txt` pulls in with one `include(${CMAKE_CURRENT_LIST_DIR}/RepriseBuildFlags.cmake)` line right after `include(GetPlatformInfo)`. It does `if (MSVC) add_compile_options(/external:anglebrackets /external:W0)` — angle-bracket includes (Qt, STL, third-party) compile at `/W0` while our own code keeps `/W4` **and still gets C4702** (preferred over a blanket `/wd4702`, preserves the zero-warnings discipline). The logic lives in the fork-only `.cmake` so it never conflicts on an `+Update` merge; only the one `include()` line sits in the upstream file. `.cmake` (not `.local.cmake`) is committed — `.gitignore:69` ignores only `*.local.cmake`, which is the upstream per-dev override hook at `CMakeLists.txt:240`, gitignored and unusable for a committed fork change. Verified: a configure-only `cmake build.release` reconfigure exits 0 and the flags land in all ~820 compile rules in `build.ninja` (after `/W4 /wd4127`). Note the muse submodule's `SetupCompileWarnings.cmake:113` disables C4702 only *per-target* on opt-in, so it does not cover the Qt-header flood.
- **Helper-bat interactive prompts default to Enter.** The `set /p` `[Y/n]` prompts in `+Build (release)+.bat` (DODEPLOY) and `+Update (from upstream)+.bat` (DOPUSH, DOBUILD) treat empty input as **Y** (an `if "%VAR%"=="" set "VAR=Y"` line after each `set /p`), so hitting Enter proceeds. The one exception is the final "Launch Reprise now?" prompt (DORUN in `+Build (release)+.bat`), which shows `[y/N]` and defaults to **N** (no auto-launch on Enter — it just omits the empty-default line, relying on the existing `if /i not …=="Y"`). User-facing prompt/echo text in these bats says "Reprise", not "MuseScore".

> WARNING — **msys64 toolchain poisoning:** `C:\msys64\clang64\bin` is on the user's PATH, and plain `vcvars64.bat` (which `ninja_build.bat` calls) does **not** add VS's CMake/Ninja. A build run from such a shell grabs msys64's `cmake`/`ccache` and finds `Vulkan`/`fmt` under `C:/msys64/clang64`, baking `-IC:\msys64\clang64\include` into every MSVC compile -> clang CRT headers poison `vcruntime.h` (C2146/C2061/C2059 cascades). Build with VS CMake + VS Ninja first on PATH (`…\Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`) and msys64 stripped out; if a cache was already generated by msys cmake, delete `build.release\CMakeCache.txt` + `CMakeFiles\` and reconfigure.

## 5. Release flow (`deploy.yml`)

```
workflow_dispatch(platforms, release_type, create_tag)
 └ plan            muse/buildscripts/ci/release/make_tag_name.sh  → TAG_NAME, RELEASE_NAME
 └ build           build_all.yml  (build_mode = stable if release_type==stable else testing;
                                  publish = on iff create_tag; deploy_backend = on;
                                  sentry_project = (create_tag && stable) ? 'mu4' : 'sandbox')
 └ update_learn_playlists   (main repo only; environment: production = manual approval)
 └ create_release  (if create_tag; environment: production)
 │     collate_release_binaries.sh → ./release/ + checksums.sha256.txt (verifies vs build checksums)
 │     git tag <TAG> && git push origin <TAG>   (regex-guarded)
 │     softprops/action-gh-release  draft:true, prerelease: release_type!=stable
 └ notify_users    update_release_info.yml (mode stable|testing, tag) — main repo, production approval
```

- **`release_type`:** `alpha|beta|rc|stable` (required when `create_tag`) or any label matching `^[A-Za-z][A-Za-z0-9._]*[A-Za-z0-9]$` for a throwaway test run (`make_tag_name.sh:47-63`).
- **Tag logic** (`make_tag_name.sh`): reads `MUSE_APP_VERSION` via `cmake -P version.cmake`; refuses if a stable tag for that version exists ("bump version.cmake"), refuses alpha/beta after rc, alpha after beta, and refuses to skip releases (requires a tag for `VERSION-1` patch/minor/major). Pre-release labels auto-number: `v5.0.0-rc`, then `-rc.2`, `-rc.3`…; release name `MuseScore Studio 5.0.0 Release Candidate 2`. **Tag creation is deliberately the last step** so a failed release doesn't leave a stray tag (`deploy.yml:103`). Final tag regex: `^v<int>.<int>(.<int>)?(-(alpha|beta|rc)(.<int>)?)?$` (`:107-115`).
- **Release notes / update feed:** `make_release_info_file.sh` GETs the GitHub release JSON (latest, or a tag), then `correct_release_info.py` post-processes it (Markdown → HTML via `pip install markdown`); `push_file_to_s3.sh` uploads `release_info.json` to `s3://updates.musescore.org/feed/latest.xml` (stable) or `latest.test.xml` (testing); `make_previous_releases_notes.sh` merges into `all.xml`/`all.test.xml`. The in-app update checker reads these. (File names say `.xml`, payload is JSON — see UNCLEAR.)
- **Backend deploy** (`deploy_backend=on` in a deploy run): `check_update_config_on_s3.sh --stage <devel|stable> --mu_version … --mu_version_major_minor …` in `build_backend.yml:145-159`; the `docker/` tree (`mu2`, `mu3`, current) builds and publishes the converter image (`publish_to_registry.sh`, token `PACKAGES_PAT`).

## 6. Translations (Transifex)

**Resources** — `.tx\config` (Transifex host `app.transifex.com`, org/project `musescore`):

```ini
[o:musescore:p:musescore:r:musescore]   file_filter = share/locale/musescore_<lang>.ts   source_file = share/locale/musescore_en.ts   type = QT
[o:musescore:p:musescore:r:instruments] file_filter = share/locale/instruments_<lang>.ts source_file = share/locale/instruments_en.ts type = QT
```

Only the two **`_en.ts` source files** are generated locally; all other-language `.ts` files come from Transifex (`run_lupdate.sh:56-57` comment). `share\locale\` holds the `.ts`/`.qm`.

**Pipeline**

1. **Extract** — `tools/translations/run_lupdate.sh` (env `LUPDATE_ARGS`, `POSTPROCESS_ARGS`, `POSTPROCESS_LAUNCHER`): `lupdate -recursive -locations absolute -extensions cpp,h,mm,ui,qml,js,mjs src → share/locale/musescore_en.ts`, with **function aliases** so the custom wrappers are recognised: `trc`, `mtrc`, `qtrc`, `TranslatableString`, `MnemonicString` (as `translate`), `qsTrc` (as `qsTranslate`), `QT_TRANSLATE_NOOP_U16`. The **instruments** resource is extracted from a fake header `share/instruments/instrumentsxml.h` (instruments + templates + score orders). Run with no `-no-obsolete` by default (obsolete strings preserved); weekly CI and the manual toggle pass `-no-obsolete`.
2. **Post-process/validate** — `tools/translations/process_source_ts_files.py`: rewrites `<location>` filenames repo-relative and **replaces line numbers with per-file occurrence counts** (smaller diffs); lints every source string (illegal bytes, empty, whitespace-only, leading/trailing/double spaces, `...` → use `…`, straight `'`/`"` → curly quotes; ignores `editstyle.ui` and `symnames.cpp`); fills the `en` translation with the source text (so `lupdate` doesn't silently drop "obsolete" entries) and flips `obsolete`→`unfinished`; exits non-zero on errors unless `--warn-only` (macOS/Windows CI use `--warn-only`, Linux does not). `--generate-placeholder-translations` also emits `*_en@placeholder.ts` (every string wrapped `ᵗʳ«…»`, `%N` marked `⌜%N⌝`, plural forms `ᵗʳ¹…`) — selectable in Preferences → General as «Placeholder translations».
3. **Push** — `translate_tx_push.yml` (`tx push -s`, source only, manual, branch-retype guard).
4. **Pull** — `tx pull -f` is used (a) before testing/stable builds (`DO_UPDATE_TS`) so shipped installers carry fresh translations, (b) by `translate_tx_pull_to_s3.yml` every 15 min.
5. **Compile** — `lrelease` runs at build time (`-DMUE_RUN_LRELEASE=ON` default, `ninja_build.sh:38,104`; disabled for `vtest`/web/no-linguist-tools builds in `SetupConfigure.cmake`); `tools/translations/run_lrelease.sh` is the manual loop over `musescore_*.ts`/`instruments_*.ts`.
6. **Live language packs** (`translate_tx_pull_to_s3.yml` → `buildscripts/ci/translation/s3_packandsend.{sh,py}`): reads `src/app/configs/languages.cfg` for `server_url` (currently `http://extensions.musescore.org/4.7/languages/`, converted to an `s3://` URL), iterates the language list in `muse/framework/languages/resources/languages.json`; for any `.ts` modified within the last `period = 300` s (or with no `.qm` yet) runs `lrelease`, records `file_name/hash(sha1)/file_size` in `share/locale/details.json`, zips `musescore_<l>.qm + instruments_<l>.qm` as `locale_<l>.zip`, `s3cmd put --acl-public`; finally pushes `details.json`. `period` must be ≤ the cron interval (comment). This is what the in-app **Languages** module (`muse\framework\languages`) downloads.

**Automation PRs:** weekly `translate_lupdate` (Sunday 12:00) and Sunday-12:00 `tx pull` each open a PR via `peter-evans/create-pull-request` (branches `ci_lupdate*` / `ci_tx_pull*`, `add-paths: share/locale/*`, label `strings`, reviewer `shoogle`).

## 7. Crash reporting (Crashpad → Sentry)

**Runtime (client):**

- Module: `muse::diagnostics` (`muse\framework\diagnostics`). Crashpad client sources compiled only when `MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT` is ON (**default OFF, "enable on CI"** — `MuseDeclareOptions.cmake:71`; also OFF if the module is off, `MuseSetupConfiguration.cmake:34-36`, on FreeBSD, MinGW, wasm, vtest). `diagnostics/CMakeLists.txt:61-88`.
- `DiagnosticsModule::onInit` (`diagnosticsmodule.cpp:97-104`) starts `CrashHandler::start(handlerPath)` (`internal\crashhandler\crashhandler.cpp:45-112`): launches the **out-of-process `crashpad_handler`** (`crashpad_handler[.exe]` next to the app) with `--no-rate-limit --no-upload-gzip`, restartable; database/dump dir = `<userAppData>/logs/dumps` (`diagnosticsconfiguration.cpp:69`; `…/dumps/completed` holds finished `.dmp`s); annotations `sentry[release]=<fullVersion>.<build>` plus `sentry[tags][*]`; uploads enabled in the DB settings, but the **server URL is passed only if `isDumpUploadAllowed()`** (user consent) — otherwise `""` (dumps stay local).
- The upload URL is compiled in: `MUSE_MODULE_DIAGNOSTICS_CRASHREPORT_URL` ← `-DMUSE_MODULE_DIAGNOSTICS_CRASHREPORT_URL="$MUSESCORE_CRASHREPORT_URL"` ← the `--crash_log_url` arg of the CI build script (`SENTRY_URL`). `--crash_log_url` also **turns the crashpad client ON** (`ci/linux/build.sh:41`, `ci/macos/build.sh:34`, `ci/windows/build.bat:17`) — so any build given a (possibly empty) `--crash_log_url` includes the client.
- `MUSE_MODULE_AUDIOPLUGINS_CRASHREPORT_URL` is a *separate* URL for VST plugin-validation child-process crashes (`MuseDeclareOptions.cmake:63`).

**Dependencies (`muse_deps`, fetched by `muse\buildscripts\cmake\ExtDepsManifest.cmake:61-70`):**

| Recipe | Role |
|---|---|
| `muse_deps\recipes\crashpad_client` | source dep; builds the `gcrashpad` lib (+ mini_chromium) linked into `muse_diagnostics` |
| `muse_deps\recipes\crashpad_handler` | tool dep; builds standalone `crashpad_handler` from Chromium crashpad `ae5b334…` with `gn`+clang (clang 23-init / 18.1.8 for linux-arm64); path forced into `MUSE_MODULE_DIAGNOSTICS_CRASHPAD_HANDLER_PATH` and `install(PROGRAMS …)` next to the app |
| `muse_deps\recipes\sentry-cli` (3.5.0, mirrored in `musescore/muse_deps` releases) | CI tool for symbol upload |
| `muse_deps\recipes\dump_syms` | **legacy** Breakpad tool; superseded (ADR 00105) |

**Symbols (ADR `muse\docs\adr\00105-CI-Uploading-native-debug-files-to-Sentry.md`, Accepted 2026-09-25):** Breakpad `.sym` conversion was dropped; CI uploads **native debug files** with the shipped binary: macOS = dSYM DWARF (`mscore.dSYM/Contents/Resources/DWARF/mscore`) + `mscore`; Windows = `MuseScore*.pdb` + matching `.exe`; Linux = the unstripped `mscore4portable*` in `build.release/src/app`. The CI step runs `cmake -DFILES="…" -P muse/buildscripts/ci/tools/sentry/upload_debug_files.cmake` (env `SENTRY_URL=https://sentry.musescore.com`, `SENTRY_ORG=sentry`, `SENTRY_AUTH_TOKEN` from `SENTRY_MUSE_AUTH_TOKEN`, `SENTRY_PROJECT` from `$GITHUB_ENV`). That script **requires https**, runs `sentry-cli debug-files check` on each file, **fails unless the set collectively exposes both `debug` and `unwind` features**, then `debug-files upload --wait`. macOS first needs `muse/buildscripts/ci/macos/generate_dsym.cmake` (`dsymutil`) **before** packaging (macdeployqt strips; package.sh deletes dSYMs). Symbols are matched by debug-id (LC_UUID / PDB GUID / ELF build-id), which survives stripping and signing.
- Reading dumps: drop `logs/dumps/completed/*.dmp` on `https://musescore.github.io/dumps/` (uploads as a *new* Sentry event), or LLDB (macOS) / WinDbg (Windows) with symbols from Sentry → Project Settings → Debug Files; Linux = read in Sentry. Source: `muse\buildscripts\ci\tools\sentry\README.md`.

## Cross-subsystem dependencies

- `testing-and-vtest.md` — what `utest`/`vtest` configurations build and how PNG comparison works; the CI wrappers are `ci/linux/build_utest.sh`, `runutests.sh`, `ci/vtests/*`, `vtest/vtest-*.sh`.
- Agent playbook `build-from-source.md` (STUB) — the local build; this file documents the *CI* recipe it should mirror.
- `architecture-and-ioc.md` — `MUSE_MODULE_*` / `MUE_BUILD_*` module switches set by the configurations in §3.2.
- `framework-services.md` (update, languages modules) — consumers of `MUSESCORE_ALLOW_UPDATE_ON_PRERELEASE`, `languages.cfg`, and the update feed.
- `app-shell-and-startup.md` — Diagnostics/crash handler is initialised during module init.
- `contributing-and-conventions.md` — `check_codestyle`, PR template, `AI_POLICY.md`.

## Non-obvious gotchas

- ⛔ **`uses: $/.github/workflows/…` in `build_all.yml` (lines 92,103,117,129,142,148) and `deploy.yml` (62,75,136).** As written on disk the reusable-workflow reference starts with a literal `$/`; GitHub requires `./.github/workflows/<file>` for same-repo callers. Either the tree was transformed or the orchestrator would not parse. Treat any attempt to *run* `build_all`/`deploy` on the fork as suspect until this is resolved (see UNCLEAR).
- ⚠ **`deploy.yml:69` passes `sentry_project: 'sandbox'`** for non-stable/no-tag runs, but the platform "Configure workflow" logic recognises only `mu4` and **`mu4-sandbox`** (`build_linux.yml:173,184`). Tracing it: `SENTRY_PROJECT='sandbox'` → not `mu4`, not empty (so no nightly default), not `mu4-sandbox` → `SENTRY_URL=""` and `DO_UPLOAD_SYMBOLS=false`. Net effect: testing/test-label builds from `deploy` compile the crashpad client with an **empty** crash URL and upload no symbols. A suspected naming bug — **TRACED logic, not run**. Also a `release_type=stable` run *without* `create_tag` gets `'sandbox'`, never the `mu4` default.
- ⚠ **`check_visual_tests.yml` handles `github.event_name == 'push'`** (reference = `event.before`, commit comment) but the workflow has **no `push:` trigger** — those branches are dead. Only `pull_request`, `workflow_dispatch`, `workflow_call` fire.
- **`build_nightly.yml:14` `CURRENT_RELEASE_BRANCH: 4.7`** and `languages.cfg` `server_url …/4.7/languages/` — release-branch / language-server constants still say 4.7 while `version.cmake` is 5.0.0. Either deliberate (5.0 not yet branched / shared S3 path) or stale. The 04:00 nightly would try to dispatch on ref `4.7`.
- ⚠ **`ci/tools/make_build_number.sh:31`** assigns `OUT_DIR` but never uses it (writes always to `build.artifacts/env`); harmless dead code.
- **Build number granularity is 10 minutes** (last minute digit is chopped). Two manual builds in the same 10-minute window get the same number → same artifact/MSI version.
- **Windows MSI needs a per-mode upgrade GUID secret:** `WIN_MSI_GUID` is `WIN_MSI_STABLE_MU4_GUID` only for `stable`, otherwise the **testing** GUID — including devel/nightly (those are 7z, so unused) — if the secrets are empty `package.bat` falls back to `11111111-…` (`:14`) — a wrong/shared UpgradeCode on a fork's MSI.
- **`publish.sh` echoes `SECRET: $OSUOSL_SSH_ENCRYPT_SECRET`** (`osuosl/publish.sh:47`); GitHub masks registered secrets, but anyone running it locally prints the secret. Also the encrypted key file is spelled `osuosl_nighlies_rsa.enc`.
- **Linux/aarch64 and Portable disable auto-update**; Linux AppImages only self-update for `stable`/`nightly` (`UPDATE_INFORMATION`).
- **`--crash_log_url ""` still enables the crashpad client** — the flag, not the value, flips `BUILD_CRASHPAD_CLIENT=ON`. Then `diagnostics/CMakeLists.txt:79-84` hard-fails the configure if the handler tool path is empty/missing, so a fork without access to `muse_deps` mirrors fails at configure, not at runtime.
- **`check_submodules` expects `muse` pinned to a commit that exists on `musescore/muse_framework` `main`** — a local framework commit (user fork work in `muse\`) fails this check until upstreamed; the check also fails if `.gitmodules` points at a fork URL.
- **`s3_packandsend.py:33`** builds the filename from the global loop variable `lang_code` instead of its `langCode` parameter — works only because it is called from the loop; copy-paste hazard.
- **Qt pin is per-workflow** (6.11.2 ×~9 files) and macOS signing uses a hard-coded `APPLE_TEAM_ID`; Windows `package.bat` hard-codes `uuidgen` at `Windows Kits\10\bin\10.0.26100.0` and WiX at `Program Files\WiX Toolset v3.11` — runner image bumps can break these.
- **`update_release_info_clear.yml`** targets bucket `musescore-updates` (`s3://musescore-updates/feed/…`, plus `--s3_bucket`) while `update_release_info.yml` targets `s3://updates.musescore.org/feed/…`; the two call different helper scripts (`muse/…/s3_push_file.sh --file_name` vs `buildscripts/ci/release/push_file_to_s3.sh`). Check they really address the same feed before relying on "clear".

## UNCLEAR — needs verification

- **`$/` in reusable-workflow `uses:`** (see gotchas) — literal on disk, or an artefact of how this tree was exported? A real repo would read `./.github/workflows/build_backend.yml`.
- **`- parallel:` and `background: true` step syntax** — not part of the stock GitHub Actions schema as far as I know; could be a newer Actions feature, a runner-side extension, or something the repo preprocesses. Not confirmed here.
- **Runner labels `ubuntu-slim` and `xcode-27`** — assumed to exist for the upstream org (GitHub-hosted/larger runners); a fork may have neither.
- **Release-channel string mismatch** (`devel` vs `dev`, `testing` vs `Testing`, stable `"stable"` vs WiX's `!= ""` guard at `WIX.template.in:11`) — which value is authoritative at runtime and whether the WiX empty-channel branch is reachable for stable was not traced.
- **Whether testing/stable intentionally share the non-"Development" GUI identifier** (`SetupConfigure.cmake:67-69`).
- **`ci/wasm/package.sh`** body and what `build_all` omits it for; WASM status (PR trigger disabled).
- **`buildscripts/ci/linux/local/workflow.sh`** (in `muse\buildscripts\ci`) and `muse\buildscripts\ci\windows\Dockerfile` — local/containerised CI helpers, not read.
- **`muse\buildscripts\ci\crashdumps\*`** (`generate_syms.cmake`, `ci_generate_*`, `view_dump.sh`, python scripts): ADR 00105 says these are **legacy and "still to be dropped"**; no workflow in this repo calls them. Their README still describes `dump_syms`/`minidump_stackwalk`.
- **`tools\release_notes`** and `buildscripts\ci\tools\osuosl\index.html` purpose beyond the nightly index page.
- **Fork behaviour** (`Amadeus-/MuseScore`): which of the above jobs actually run there, given scheduled jobs and triage jobs are hard-gated to `musescore/MuseScore`, and secrets are absent.

*Authored from: all 22 files in `.github\workflows\`; `buildscripts\ci\{linux,macos,windows,backend,wasm,withoutqt,vtests,release,translation,tools}\*` (build/package/sign/publish/env scripts), `buildscripts\packaging\{macOS,Windows}`; `version.cmake`, `SetupConfigure.cmake`, `ninja_build.sh`; `.tx\config`, `tools\translations\*`; `muse\buildscripts\ci\{release,tools\sentry,crashdumps}`, `muse\docs\adr\00105`; `muse\framework\diagnostics` (CMake, `crashhandler.cpp`, config), `muse\buildscripts\cmake\ExtDepsManifest.cmake`, `muse_deps\recipes\{crashpad_handler,sentry-cli}`; `muse\framework\update\internal\updateconfiguration.cpp`. Deep pass 2026-11-08. **If this contradicts the live source, the source wins.***
