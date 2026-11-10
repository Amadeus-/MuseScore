# MuseScore — App Shell & Startup

> **What this file is.** Scope: the process entry point (`src\app`: `main.cpp`, `AppFactory`, `MuseScoreGuiApp`/`MuseScoreConsoleApp`, `CommandLineParser`) and the `src\appshell` module (startup scenario, sessions, app-level commands controller, app menu, main-window QML, first-launch setup, splash, config). The IoC boot **phases** (`BaseApplication::doSetup`, per-context setup) are covered in `architecture-and-ioc.md` — this file points there and only shows where the app entry plugs in. **Live source wins** — re-verify before editing. Start at `00-core.md`.

## 0. 60-second orientation

- **Entry:** `src\app\main.cpp:76` `main()` → sets Qt env/app names → (optionally) `CommandLineParser` → creates `QApplication` (or `QCoreApplication` for audio-plugin registration) → posts a **queued chain** that calls `AppFactory::newApp` → `showSplash` → `setup()` → `setupNewContext()` → then `qapp->exec()` (`main.cpp:192-217`).
- **Two app flavors**, chosen by `MuseScoreCmdOptions::runMode` (`GuiApp` / `ConsoleApp` / `AudioPluginRegistration`): `MuseScoreGuiApp` (`src\app\internal\guiapp.*`, derives `muse::ui::GuiApplication`) and `MuseScoreConsoleApp` (`consoleapp.*`, derives `muse::ConsoleApplication`). **`AppFactory`** (`appfactory.cpp`) builds the module list for each — GUI list is a superset; console list is a trimmed one.
- **`src\appshell`** = the "shell": `AppShellModule` (global + per-window `AppShellContext`), `StartupScenario` (what opens on launch), `SessionsManager` (crash-restore), `AppshellCommandsController` (quit/restart/fullscreen/about/preferences/dock toggles + drag-drop + macOS FileOpen), `AppMenuModel` (QML menu bar), `AppShellConfiguration` (startup settings, URLs, session file), plus QML pages (Home/Notation/Publish/DevTools), `FirstLaunchSetup`, `WelcomeDialog`, and Widgets-based `SplashScreen`.
- ⭐ `muse::ui::GuiApplication` (framework, `muse\framework\ui\internal\guiapplication.cpp`) owns the generic "load main QML window then call `doStartupScenario`" logic; `MuseScoreGuiApp` only supplies the QML path, splash, CLI-option wiring and the actual startup scenario call.
- Design basis: ADR `muse\docs\adr\00003-CoreArch-AppShell`.

## 1. The entry point — `main.cpp`

### 1.1 Environment set before any Qt object (`main.cpp:84-138`)

| Setting | Where | Meaning |
|---|---|---|
| crash `signal()` handlers (SEGV/ILL/FPE) | `:78-82` | only when **not** `MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT` (crashpad otherwise owns crashes); `crashCallback` logs + `exit(EXIT_FAILURE)` |
| `app_init_qrc()` | `:67,88` | `Q_INIT_RESOURCE(app)` (+ `app_win` on Windows) |
| `QT_STYLE_OVERRIDE=Fusion`, `QML_DISABLE_DISK_CACHE=true` | `:90-91` | forced always |
| `QV4_GC_TIMELIMIT=0` | `:94-96` | "HACK … crash #28840" disables incremental GC unless `MU_QV4_GC_TIMELIMIT` set |
| `QT_QUICK_FLICKABLE_WHEEL_DECELERATION=5000` | `:98-100` | only if unset |
| Linux: `QT_QPA_PLATFORMTHEME=gtk3`, `QT_QPA_PLATFORM=xcb` | `:102-111` | forced X11 ("with Wayland there are a number of problems"); override via `MU_QT_QPA_PLATFORM` |
| Windows: `QT_OPENGL_BUGLIST=:/resources/win_opengl_buglist.json` | `:113-119` | issue #8270 |
| `setMousePressAndHoldInterval(250)` | `:121` | |
| `applicationName` = `MUSE_APP_NAME_MACHINE_READABLE` (+`"Development"` if `MUSE_APP_UNSTABLE`) = `Reprise` in the Reprise fork (version-major suffix dropped so the product name is bare "Reprise"); org `MuseScore`, domain `musescore.org` (org/domain left as upstream — identifies QSettings path only, no network use) | `:123-130` | the app name keys QSettings/user-data paths, so dev builds get a separate profile |
| Freedesktop desktop-file name `org.musescore.MuseScore` + `MUSE_APP_INSTALL_SUFFIX` | `:132-138` | non-Win/mac/wasm only |
| `fixSslBackend` lambda → `QSslSocket::setActiveBackend("schannel")` | `:143-150` | Windows only; issue #33401 (OpenSSL ABI mismatch crash) |

### 1.2 Command line + QApplication (`main.cpp:155-181`)

```cpp
// main.cpp:155 — #ifdef MUE_ENABLE_CONSOLEAPP
CommandLineParser commandLineParser;  commandLineParser.init();  commandLineParser.parse(argc, argv);
IApplication::RunMode runMode = commandLineParser.runMode();
if (runMode == IApplication::RunMode::AudioPluginRegistration) qapp = new QCoreApplication(argc, argv);
else                                                          qapp = new QApplication(argc, argv);
commandLineParser.processBuiltinArgs(*qapp);      // handles --help / --version (needs a QCoreApplication)
```

- ⭐ **Parse happens BEFORE the QApplication exists**; `QCommandLineParser::process()` (help/version) is deferred to `processBuiltinArgs` (`commandlineparser.cpp:559`) because it needs an app instance.
- Console-mode runs still construct a full **`QApplication`** (Widgets) — only audio-plugin registration uses `QCoreApplication`.
- If `MUE_ENABLE_CONSOLEAPP` is OFF (web build, `SetupConfigure.cmake:170`), `main` skips the parser entirely and hardcodes `runMode = GuiApp` (`main.cpp:174-180`); `commandlineparser.cpp`/`consoleapp.cpp` aren't even compiled (`src\app\CMakeLists.txt:221-228`).

### 1.3 The queued bootstrap chain (`main.cpp:187-217`)

```cpp
//! NOTE: We immediately launch the event loop to be able to show a splash screen
QMetaObject::invokeMethod(qapp, [..]() {                 // queued #1
    AppFactory f;  app = f.newApp(opt);  app->showSplash();
    QMetaObject::invokeMethod(qapp, [..]() {             // queued #2
        app->setup();                                    // BaseApplication::doSetup — global-module phases
        QMetaObject::invokeMethod(qapp, [..]() {         // queued #3
            app->setupNewContext();                      // first window context
            LOGI() << "SSL Info: ...";
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}, Qt::QueuedConnection);
int code = qapp->exec();
if (app) app->finish();    delete qapp;    return code;
```

Each stage is its own event-loop turn so the splash (a Widget) can paint (`SplashScreen::repaint` also calls `processEvents`, `splashscreen.cpp:60-64`). After `exec()` returns, `app->finish()` runs (`BaseApplication::doFinish`: destroy contexts → module `onDeinit` → `onDestroy` → delete modules → `resetAll()`; restart relaunches via `QProcess::startDetached` when `FinishMode::Restart`, `baseapplication.cpp:431-484`). Details: `architecture-and-ioc.md`.

## 2. `AppFactory` — assembling the module list (`src\app\appfactory.cpp`)

`newApp` (`:292-303`) dispatches on `options->runMode`: `GuiApp` → `newGuiApp`, otherwise `newConsoleApp`. Modules are created with `new` and handed to `app->addModule(...)` (ownership: `BaseApplication`, deleted in `doFinish`).

### 2.1 Real vs stub selection

The top of the file (`:11-287`) `#include`s either the real module header or a **stub** from `framework\stubs\<mod>\` / `src\stubs\<mod>\` depending on config macros: `MUSE_MODULE_*` for framework modules, `MUE_BUILD_*_MODULE` for app modules. ⭐ The stub's class name matches the real one (e.g. both `muse::audio::AudioModule`), so `newGuiApp`'s body is **identical** regardless; the preprocessor swaps *what that name means*. Consequences:

- Modules whose `addModule` line is **unconditional** (e.g. `AccessibilityModule`, `AudioModule`, `CloudModule`, `ExtensionsModule`, `NotationModule`, `ProjectModule`…) have a stub fallback.
- Modules whose `addModule` line is wrapped in `#ifdef` have **no** stub (e.g. `DiagnosticsModule`, `AudioPluginsModule`, `MuseSamplerModule`, `ToastModule`, `DockModule`, `InteractiveModule`, `TestflowModule`, `AppShellModule`, `PreferencesModule`, `PrintModule`, converter, most importexport).
- A few lines are unconditional **without** a visible stub in the include block (e.g. `braille` has `braillestubmodule.h` when `MUE_BUILD_BRAILLE_MODULE` is off; `propertiespanel` is included unconditionally at `:233`).

### 2.2 GUI module list order (`newGuiApp`, `:305-449`)

`MuseScoreGuiApp` is created first (`:307`), then in this order: **`diagnostics` first** (`:309-313`, comment: "installs the crash handler. For other modules, the order is (and should be) unimportant"); framework (`accessibility, actions, rcommand, rcontrol, audio, [audioplugins], automation, draw, [interactive], midi, midiremote, mpe, [musesampler], network, shortcuts, ui, uicomponents, [dock], [toast], tours, vst, media`); then `[appshell]` (`MUE_BUILD_APPSHELL_MODULE`, `:365`), `[testflow]`, `braille`, `cloud`, `[converter]`, `engraving`, the import/export modules, `propertiespanel`, `instrumentsscene`, `extensions`, `languages`, `learn`, `multiwindows`, `musesounds`, `notation`, `notationscene`, `palette`, `playback`, `[preferences]`, `[print]`, `project`, `update`, `workspace`, `[appjs]` (web only).

- ⭐ **MuseSampler is not added in debug builds** unless `MUSE_MODULE_MUSESAMPLER_LOAD_IN_DEBUG` (`:333-345`, `runtime::isDebug()`).
- Because order is "unimportant", dependencies are resolved via IoC at `resolveImports`/`onInit` time, never by list position (see `architecture-and-ioc.md`).
- ⭐ **Reprise fork module** `reprise` (`src\reprise`, added unconditionally right after `project` at `:440`) — a fork-local module kept isolated from upstream modules for clean `Update (from upstream)` merges. It houses Reprise's single OMR path: the **Import PDF** feature (local Audiveris). See §2.5.

### 2.5 Reprise fork module (`src\reprise`) — "Import PDF" (local Audiveris)

New fork-local module holding the Reprise "Import PDF" orchestration. It owns the command `command://reprise/import-pdf` (`REPRISE_IMPORT_PDF_COMMAND`, `src\reprise\reprisecommands.h`).

- **Module setup** `RepriseModule`/`RepriseContext` (`reprisemodule.{h,cpp}`): the module's `resolveImports()` registers command metadata via `globalIoc()->resolve<ICommandsRegister>()->reg(RepriseCommandsRegister)`; the context's `onInit()` (GuiApp only) calls `ImportPdfController::init()`, which registers the handler via `commandDispatcher()->onRequest(...)`. Mirrors the `project` module's register-metadata-at-module / register-handler-at-context split. Registered in `appfactory.cpp` (`new mu::reprise::RepriseModule()`), wired into `src\CMakeLists.txt` (`add_subdirectory(reprise)`) and linked in `src\app\CMakeLists.txt` (`add_to_link_if_exists(reprise)`).
- **Command enable-STATE** `RepriseCommandsState` (`internal/reprisecommandsstate.{h,cpp}`): registering command *metadata* (the `ICommandsRegister` above) is NOT enough to make a menu item clickable — the enabled/grayed state comes from a SEPARATE registrar, the context-scoped `ICommandsState`. `muse::rcommand::CommandsState::commandState()` looks up the owning module's `IModuleCommandsState`; a command whose module registered NO state hits `IF_ASSERT_FAILED(mit != m_modules.end())` and falls back to the default `CommandState()` whose `enabled == false` → **grayed out** (the bug fixed in `081ef2865a`: Import PDF was disabled on a cold launch with no score open). `RepriseCommandsState::commandState()` returns `CommandState(true, false)` for every reprise command (always enabled — Import PDF creates a score, so it must be clickable with nothing open), the equivalent of the `project` module's `PCondition::Any`. It is wired through `RepriseContext::resolveImports()` via `ioc()->resolve<ICommandsState>(mname)->reg(...)`, exactly mirroring `ProjectContext::resolveImports()`. **Takeaway: any new reprise command needs BOTH a metadata entry in `RepriseCommandsRegister` AND an enable rule here, or it ships disabled.** (Module `resolveImports()` run before context `resolveImports()`, so the commands-register exists when the state's `init()` seeds initial states — `baseapplication.cpp:188` vs `:339`.)
- **`AudiverisRunner`** (`internal/audiverisrunner.{h,cpp}`): detects `Audiveris.exe` (`C:\Program Files\Audiveris\Audiveris.exe`, else PATH, else common dirs), runs the CLI **blocking** (`muse::IProcess::execute`, injected) as `"<exe>" -batch -save -export -output "<tempdir>" -- "<pdf>"`, best-effort sets `TESSDATA_PREFIX` if a tessdata dir is detected, then locates the produced `.mxl` (fallback `.xml`) by recursive search of the temp dir. Outputs (incl. the `.omr`) are left in a non-auto-removed temp dir (durable copy is a later step).
- **`ImportPdfController`** (`internal/importpdfcontroller.{h,cpp}`): the handler flow — abort with an Audiveris-not-found warning if the exe is missing; else `interactive()->selectOpeningFileSync(... *.pdf)` → `AudiverisRunner::run()` → on success **dispatch `project::PROJECT_OPEN_COMMAND`** with `url = QUrl::fromLocalFile(mxl)` (the `.mxl` routes through MuseScore's native import / `MusicXmlReader` — no new import plumbing). Cross-module reuse is via the command system only; `reprise` does **not** link `project`.
- **File-menu change** (`appmenumodel.cpp`, `makeFileMenu()`): the cloud **"Convert file…"** item (`PROJECT_CONVERT_TO_SCORE_COMMAND`) was **replaced** by **"Import &PDF…"** (`REPRISE_IMPORT_PDF_COMMAND`). The underlying `ConvertFileToScore*` code stays dormant (handler still registered in `project`), it is just unreachable from the UI — Reprise has exactly one OMR path.
- **Not yet implemented** (later steps): page-range dialog, async `QProcess` + progress UI, `reprise:*` metadata persistence, durable `.omr` copy, a Preferences override for the Audiveris path.

### 2.3 Console module list (`addConsoleModules`, `:451-552`)

Trimmed: `diagnostics`, actions, rcommand, rcontrol, audio, [audioplugins], draw, midi, mpe, [musesampler], ui (module only — no uicomponents/dock/shortcuts), [vst], media, [testflow], [converter], engraving, import/export modules, multiwindows, notation, playback, project, extensions, **notationscene** (kept, last). Not present: appshell, palette, languages, network, cloud, update, workspace, learn, tours, propertiespanel, instrumentsscene, preferences, print, musesounds. A `//! TODO Some modules can be removed` comment (`:459`) notes it is not minimal.

### 2.4 Audio-plugin-registration list (`:554-565`)

Only `AudioModule`, `[AudioPluginsModule]`, `[VSTModule]`. Used for `--register-audio-plugin` (the separate validation child process the app spawns to probe a VST).

`newConsoleApp` (`:567-587`) returns `nullptr` when `MUE_ENABLE_CONSOLEAPP` is off.

## 3. The app classes

### 3.1 `MuseScoreGuiApp` (`src\app\internal\guiapp.cpp`)

Injects `IMultiWindowsProvider`, `IAppShellConfiguration`, `IGuitarProConfiguration` (`guiapp.h:21-23`). Overrides of `muse::ui::GuiApplication` hooks:

| Method | Line | Behavior |
|---|---|---|
| `showSplash()` | `:55` | `MUE_ENABLE_SPLASHSCREEN`: builds `splashConfig(m_appOptions)`; only if type is `Default` shows `SplashScreen::Default` (the generic loading splash). |
| `showContextSplash(ctx)` | `:66` | per-window; only if config type is `ForNewInstance` shows the "opening X / new score" splash. |
| `splashConfig` | `:23` | `startup.type=="start-with-new"` → ForNewInstance/forNewScore; else if `startup.scoreUrl` → ForNewInstance with the file's display name; else `Default`. |
| `makeContextOptions(args)` | `:77` | for a **secondary window** (`args` non-empty) re-runs `CommandLineParser` over a synthesized argv (`"dummy/path/to/app.exe"` + args); empty args → reuse app-level `m_appOptions`. |
| `mainWindowQmlPath(platform)` | `:98` | `:/qt/qml/MuseScore/AppShell/platform/<win|mac|linux>/Main.qml` |
| `doStartupScenario(ctx)` | `:103` | see §4. |
| `applyCommandLineOptions` | `:140` | base + `--revert-settings/-F` → `appshellConfiguration()->revertToFactorySettings(...)`; GP `--gp-experimental`/`--gp-linked` → `IGuitarProConfiguration`. |

### 3.2 Framework `GuiApplication` (what runs around it)

- `doSetup` (`guiapplication.cpp:50`): `BaseApplication::doSetup` + a **5 s single-shot timer** (`DELAYED_INIT_INTERVAL_MS`, `:43`) that fires every module's `onDelayedInit` + `QQuickWindow::setDefaultAlphaBuffer(true)` (transparent popups) + `setupGraphicsApi()` (GraphicsApiProvider: picks required API, detects problems, **restarts the app** on a failed API check, `:77-112`).
- `startupScenario(ctx)` (`:121`): queued → `loadMainWindow(ctx)` → on success queued → `doStartupScenario(ctx)` + a per-context `contextDelayedInit` after another 5 s.
- `loadMainWindow` (`:155-209`): resolves `IUiEngine` for the context, `QQmlComponent(engine, mainWindowQmlPath)`, creates a `QQmlContext` with the context property **`ioc_context`** (`QmlIoCContext{ctx}`) — the way QML view-models find their IoC context (`iocCtxForQmlObject`); then `window->setVisible(true)` ("must be shown at this point so KDDockWidgets can read its size", issue #21148); stores window in `m_windows[ctxId->id]`.
- `doDestroyContext` (`:211`): hide + `delete` the window, `IUiEngine::quit()`, then base.

### 3.3 `MuseScoreConsoleApp` (`src\app\internal\consoleapp.cpp`)

- `showSplash()` prints a banner to stdout (`:68-73`).
- `applyCommandLineOptions` (`:75-147`): pushes parsed options into configuration services — DPI (`uiConfiguration`), template/test mode (`notationConfiguration`), **migration options** (`projectConfiguration` — for `ConsoleApp` mode: `isAskAgain=false`, per-type apply flags from `--migration full`, set *without persisting*), image-export trim/DPI, video export (resolution/fps/lead/trail), MIDI import ops file, MusicXML flags, mp3 bitrate, GP flags, and `-F/-R` → `settings()->reset(...)`. Skipped for `AudioPluginRegistration`.
- `doStartupScenario(ctx)` (`:149-192`), `ConsoleApp` branch priority: **Testflow** (`--test-case` → `processTestflow`, keeps running until its status channel finishes) → **Diagnostic** (`diagnostic.type != Undefined` → `processDiagnostic`, then `qApp->exit(code)`) → **Converter** (`processConverter`, then `qApp->exit(code)`). `AudioPluginRegistration` → `validatePlugin(path, outputFile)` if `--register-audio-plugin-out` given, else `registerPlugin(path)`.
- `processConverter` (`:194-262`) maps `ConvertType` → `IConverterController` calls (`batchConvert`, `fileConvert`, `convertScoreParts`, `exportScoreMedia/Meta/Parts/PartsPdfs/Transpose/Elements/Video`, `updateSource`). Exit code = `Ret::code()`. Full converter semantics → `mscore-cli.md`.
- In console mode the `ConsoleApplication::startupScenario` just calls `doStartupScenario` directly (no window) — `consoleapplication.cpp:34-38`.

## 4. Command line — `CommandLineParser` (`src\app\internal\commandlineparser.cpp`)

`init()` registers options (`:63-190`); `parse()` fills `MuseScoreCmdOptions` (`cmdoptions.h:34`); `printLongVersion` for `--long-version` exits immediately (`:245-248`). ⭐ **Run-mode switching:** options that set `runMode = ConsoleApp` are `-o`, `-j`, `--score-media`, `--score-meta`, `--score-parts`, `--score-parts-pdf`, `--score-transpose`, `--score-elements`, `--source-update`, `--score-video`, all `--diagnostic-*` generators, and `--test-case`; `--register-audio-plugin` sets `AudioPluginRegistration`. No such option → stays `GuiApp` (default in the struct from `muse::CmdOptions`).

App-shell-relevant (GUI) options:

| Option | Effect | Cite |
|---|---|---|
| positional `scorefiles` | GUI mode: **only `scorefiles[0]`** → `startup.scoreUrl` (`QUrl::fromUserInput(... AssumeLocalFile)`); the rest are ignored | `:547-551` |
| `--session-type <type>` | `startup.type` = raw string; interpreted by `StartupScenario` (`start-empty`, `continue-last`, `start-with-new`, `start-with-file`) | `:293-295`, `startupscenario.cpp:43-62` |
| `--score-display-name-override <name>` | hidden option; display name for splash/recent entry (used when a secondary instance is spawned for a file) | `:187-189,553` |
| `-F/--factory-settings`, `-R/--revert-settings` | **both** set `app.revertToFactorySettings = true` (the help text differs but the code does not distinguish) | `:453-455` |
| `-d/--debug` | logger level Debug | `:250-252` |
| `-D/--monitor-resolution` | `ui.physicalDotsPerInch` | `:254-261` |
| `-t/--test-mode`, `--template-mode` | `notation.testModeEnabled/templateModeEnabled` | `:285-291` |
| `--gp-linked`, `--gp-experimental` | GP import flags | `:481-487` |
| `--long-version`, `-h`, `-v` | print & exit | `:245`, `:66-67` |

Converter/diagnostic/testflow options are catalogued in `mscore-cli.md`. Notes: `--migration` is honored only in `ConsoleApp` mode and only value `full` is meaningful (`:165,489-494`). `-P/--export-score-parts` error text says "Option: -R" (copy-paste bug, `:335`). Video options are compiled in only with `MUE_BUILD_IMPEXP_VIDEOEXPORT_MODULE`, and `--score-video` reads its output path from `-o` (`:417`) — but `-o` has already forced `ConvertType::File`; `--score-video` runs later in `parse()` and overrides the type (order-dependent).

`prepareArguments` strips `-qmljsdebugger*` in debug builds (`:43-47`).

## 5. The startup scenario (GUI launch flow)

### 5.1 End-to-end sequence (one window)

1. `main` queued chain → `app->setup()` → global module + all `IModuleSetup`s: `registerResources/Exports/UiTypes` → `resolveImports` → **`applyCommandLineOptions`** → `onPreInit` → `onInit` → `onAllInited` → queued `onStartApp` (`baseapplication.cpp:158-230`). The `AppShellModule` hooks here: `registerExports` (config), `resolveImports` (URIs + commands), `onInit` (`m_appShellConfiguration->init()`), `onAllInited` (macOS `processEvents()` to flush a postponed `QEvent::FileOpen`, `appshellmodule.cpp:99-105`).
2. `app->setupNewContext()` → allocates context id → queued: `showContextSplash` → queued: `setupContext` (per-context `registerExports → resolveImports → onPreInit → onInit → onAllInited` for every module's `IContextSetup`, incl. `AppShellContext`) → queued: `startupScenario(ctx)` (`baseapplication.cpp:291-326`). The `static bool once` assert means **`setupNewContext` may be called only once per process** unless `MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE` (`:293-299`); additional windows come from `IMultiWindowsProvider::openNewWindow(args)` → `setupNewContext(args)` (`singleprocessprovider.cpp:151-158`).
3. `GuiApplication::startupScenario` → `loadMainWindow` (QML `Main.qml` → `AppWindow` + `WindowContent` DockWindow) → `MuseScoreGuiApp::doStartupScenario` (below).

### 5.2 `MuseScoreGuiApp::doStartupScenario` (`guiapp.cpp:103-138`)

```cpp
auto startupScenario = ioc(ctxId)->resolve<IStartupScenario>("app");
startupScenario->setStartupType(options->startup.type);              // --session-type
if (options->startup.scoreUrl) startupScenario->setStartupScoreFile(ProjectFile{url, displayNameOverride});
startupScenario->runOnSplashScreen();                                 // while splash visible
QMetaObject::invokeMethod(qApp, [..]() {                              // next turn
    if (m_splashScreen) { close(); delete; m_splashScreen = nullptr; }
    startupScenario->runAfterSplashScreen();
}, Qt::QueuedConnection);
```

### 5.3 `StartupScenario` (`src\appshell\internal\startupscenario.cpp`)

`IStartupScenario` is a **context** interface (`MODULE_CONTEXT_INTERFACE`, `istartupscenario.h:30`) registered per window in `AppShellContext::registerExports` (`appshellmodule.cpp:121`). Injects `multiwindowsProvider`, `configuration`, `interactive`, `commandDispatcher`, `sessionsManager`, `projectAutoSaver`, plus *optional* update/musesounds/musesampler/audio-plugin scenarios (`startupscenario.h:45-54`).

**`runOnSplashScreen`** (`:107-125`): non-first window → only `registerAudioPlugins()`. First window → kick off app-update check (`appUpdateScenario->needCheckForUpdate()` → `checkForUpdate(false)`), MuseSounds update check, then `registerAudioPlugins()`. `registerAudioPlugins` disables `qApp->setQuitLockEnabled` around `updatePluginsRegistry()` because it may show a dialog and — with no windows — the app would auto-quit (`:127-140`).

**`runAfterSplashScreen`** (`:142-164`), guarded once by `m_startupCompleted`:

```cpp
if (multiwindowsProvider()->isFirstWindow() && sessionsManager()->hasProjectsForRestore())
    modeType = StartupModeType::Recovery;
else
    modeType = resolveStartupModeType();
interactive()->open(startupPageUri(modeType)).onResolve(.., [..]{ onStartupPageOpened(modeType); });
```

**`resolveStartupModeType`** (`:171-182`) precedence: (1) a valid startup score file (CLI arg / macOS FileOpen) → `StartWithScore`; (2) `--session-type` string via `modeTypeTromString` (unknown strings → `StartEmpty`); (3) persisted setting `AppShellConfiguration::startupModeType()`.

| `StartupModeType` (`appshelltypes.h:61`) | Page opened first (`startupPageUri`, `:64-77`) | Action in `onStartupPageOpened` (`:184-220`) |
|---|---|---|
| `StartEmpty` | `musescore://home` | nothing |
| `StartWithNewScore` | home | dispatch `PROJECT_NEW_COMMAND` |
| `ContinueLastSession` | `musescore://notation` | dispatch `PROJECT_CONTINUE_LAST_SESSION_COMMAND` (handled in `projectactionscontroller.cpp:80`) |
| `Recovery` | home | `restoreLastSession()` — question "The previous session quit unexpectedly… restore?"; Yes → `sessionsManager->restore()`; No → `removeProjectsUnsavedChanges` + `sessionsManager->reset()` |
| `StartWithScore` | notation | `openScore(file)` → dispatch `PROJECT_OPEN_COMMAND {url, display_name}`; file = CLI/FileOpen file else `configuration()->startupScorePath()` (setting default = `projectConfiguration()->myFirstProjectPath()`) |

⭐ `Recovery` is **not a user setting** (only the first four are meaningful as preferences; `Recovery` is computed at runtime).

**Startup dialogs** (`onStartupPageOpened` tail → `showStartupDialogsIfNeed`, `:208-263`): count active MuseSounds update checks (`m_activeUpdateCheckCount`); once none are pending:
1. `!hasCompletedFirstLaunchSetup()` → open `musescore://firstLaunchSetup?floating=true` and stop.
2. If `welcomeDialogLastShownVersion < museScoreVersion` → force `welcomeDialogShowOnStartup=true` and reset last-shown index to −1 (a version bump re-shows the dialog regardless of preference).
3. `shouldShowWelcomeDialog` (`:265-281`): preference on **and** first window **and** no pending MuseSounds update **and** `interactive()->currentUri() == startup page` → open `musescore://welcomedialog`, then record version.
4. For `StartEmpty`/`StartWithNewScore`, afterwards `checkAndShowMuseSamplerStatusIfNeed()` (once, `alreadyChecked()` guard).

### 5.4 Opening a file from outside the process

- **macOS `QEvent::FileOpen`** — `AppshellCommandsController::eventFilter` (`appshellcommandscontroller.cpp:109-123`): if `openProjectScenario->isUrlSupported(url)`: after startup completed → dispatch `PROJECT_OPEN_COMMAND`; **before** completion → `startupScenario->setStartupScoreFile(...)` so it becomes the `StartWithScore` target. The filter is installed in `AppshellCommandsController::preInit()` (`:67-70`, `qApp->installEventFilter`).
- **Drag-and-drop onto the main window** — same filter, `dragTarget(url)` classifies (`:165-184`): project file → `openProjectScenario->openProject`; SoundFont (`audio::synth::isSoundFont`) → `soundFontInstallScenario`; extension package (`extensionInstaller->isFileSupported`) → `installExtension`; convertible file (only when not already on the notation page with a project open) → `convertFileToScoreScenario->convertFiles`. Calls are deferred with `async::Async::call`.

## 6. Module registration & what `appshell` exposes (`appshellmodule.cpp`)

Module name `"appshell"` (`:57`). Two layers:

**Global (`AppShellModule`)**
- `registerExports` → `IAppShellConfiguration` (global; `AppShellConfiguration(globalCtx())`).
- `resolveImports`: registers **page URIs** in `IInteractiveUriRegister` — `musescore://home`, `://notation`, `://notation/review`, `://sequencer`, `://publish`, `://devtools`; **QML dialog URIs** — `://about/musescore` (`AboutDialog`), `://about/musicxml`, `://welcomedialog` (`WelcomeDialog`), `://firstLaunchSetup` (`FirstLaunchSetupDialog`) in QML module `MuseScore.AppShell`; and `AppshellCommandsRegister` into `ICommandsRegister` (`:73-91`).
- `onInit` → `AppShellConfiguration::init()`.

**Per-window (`AppShellContext`, `:107-162`)** — each window/context gets its own: `IAppShellState`, `IStartupScenario`, `ISessionsManager`, `IAppshellCommandsController`, `IShortcutsResolver` (`ShortcutResolver`), `IExtensionContextResolver`, `IAppMenuModelHook` (macOS hook `MacOSAppMenuModelHook`, else `AppMenuModelHookStub`). `resolveImports` registers `AppshellCommandsState` in `ICommandsState`. Lifecycle: `onPreInit` → `commandsController->preInit()`; `onInit` → `sessionsManager->init()` and, **GUI only**, `commandsController->init()` + `extensionContextResolver->init()`; `onDeinit` → `sessionsManager->deinit()`.

⭐ In the **web** (APPWEB) configuration the appshell module is replaced wholesale by `src\web\appshell` (`src\CMakeLists.txt:90-96`), which has its own `startupscenario`/`istartupscenario` (a separate interface copy). Not covered here.

## 7. The app-level commands controller (`AppshellCommandsController`)

Despite the stale header guard `MU_APPSHELL_APPLICATIONCONTROLLER_H`, the class is `AppshellCommandsController` (rcommand/ADR 00101 style — the old "ApplicationActionController" no longer exists). It is a `QObject` (event filter) + `Commandable`.

**Command registry** — IDs in `src\appshell\appshellcommands.h`; metadata (title, input schema, checkable, `Availability`) in `appshellcommandsregister.cpp`; handlers bound in `init()` (`appshellcommandscontroller.cpp:72-96`):

| Command URI | Handler | Notes |
|---|---|---|
| `command://app/quit` | `quit(params)` | schema: `all_instances` (bool, default true), `installer_path` (string); `Availability::All` |
| `app/restart` | `restart()` | closes project; 1 window → `application()->restart()`; else `quitAllAndRestartLast()` + `QCoreApplication::exit()` |
| `app/fullscreen` | `toggleFullScreen()` | checkable; state from `IMainWindow::isFullScreen` |
| `app/about-musescore`, `about-qt`, `about-musicxml` | open dialogs | `Availability::Dispatch` |
| `app/online-handbook`, `ask-help`, `accessibility-statement` | `platformInteractive->openUrl(configuration URL)` | |
| `app/preferences` | stops playback first if playing, then `muse://preferences`; if another window already has Preferences open, activates that window (`multiwindowsProvider`) | `Availability::All` |
| `app/revert-to-factory` | confirm dialog → `configuration->revertToFactorySettings(false,false,false)` → offer restart | |
| `app/extensions` | `interactive->open("musescore://home?section=plugins")` | |
| `app/dock/toggle-*` (14) | `m_dockToggleRequested.send(dockName)` | map `s_dockToggleCommands` (`:50-65`) command↔dock name (palettes, layout, properties, selection filters, undo history, navigator, braille, timeline, mixer, piano keyboard, percussion, playback toolbar, note-input bar, status bar). `NotationPageModel` consumes the channel (`notationpagemodel.cpp:69-71`) and either handles navigator/braille locally or dispatches `dock::DOCK_TOGGLE_COMMAND`. |

**Quit flow** (`:268-340`): `m_quiting` re-entry guard (returns `Busy`) → `closeProjectScenario->closeOpenedProject(false)` (may prompt to save; user cancel → resolve with the failed Ret and clear flag) → `resolve(ok)` *before* `doQuit` ("doQuit destroys this window") → `doQuit`: if first window and an `installer_path` was given, try `appUpdateService->installUpdate()` (when `canAutoInstall`), falling back to opening/revealing the installer; non-first windows notify `notifyAboutWindowWasQuited`; then `quitForAll(ctx)` or `quitWindow(ctx)` — "the following destroys the IoC context and `this` with it".

⭐ **Window-close and app-quit are intercepted** in `eventFilter` (`:98-107`): a `QEvent::Close` on the main window or any `QEvent::Quit` is **rejected** (`setAccepted(false)`) and rerouted to `quit(false)` (this window only), so all closes go through the save-prompt path.

**Command state** — `AppshellCommandsState` (`appshellcommandsstate.cpp`) computes checked/enabled: fullscreen follows `IMainWindow`; navigator via `IAppShellState`; braille via config; dock toggles follow `IDockWindow::isDockOpen`; all dock-toggle commands (`PROJECT_PAGE_COMMANDS`) are **disabled unless `musescore://notation` is opened** (`isProjectPage`, `:165-172`).

`IAppshellCommandsController` (public iface) exposes only `dockToggleCommand(name)`, `commandDockName(cmd)`, `dockToggleRequested()`.

## 8. Main window, toolbar, pages

- **QML window stack**: platform `Main.qml` (`platform\win|mac|linux`) → `AppWindow` (a `QtQuick.Controls.ApplicationWindow`: 1150×800 default, min 1050×500, starts `visible:false`, sets `ui.rootItem`, hosts `MainWindowTitleProvider`, `MainWindowBridge`, `ToolTipProvider`, `ToursProvider`, a `Shortcuts{}` object "need only create", `ToastProvider` on top; `AppWindow.qml:33-92`) → `WindowContent` (`DockWindow`, `WindowContent.qml:39`) which declares the main toolbar and the **pages**: `HomePage`, `NotationPage`, `NotationReviewPage`, `PublishPage`, `DevToolsPage` (`:100-118`). `InteractiveProvider` there maps `IInteractive` page opens to `DockWindow::loadPage(uri, params)` (`:49-56`). The Windows `Main.qml` adds a custom `AppTitleBar` (32 px) above `WindowContent`.
- **`MainToolBarModel`** (`maintoolbarmodel.cpp`): items **Home**, **Score** (`musescore://notation`, URI re-resolved via `openProjectScenario->resolveNotationPageUri()` — may become `…/review`; bold when a project is open), **Publish**, plus **DevTools** only if `globalConfiguration()->devModeEnabled()` (`:100-120`). Notation/review count as the same checked tab.
- **`MainWindowTitleProvider`**: no project → `"Reprise"` (Reprise fork; upstream `"MuseScore Studio"`); else `notation->projectNameAndPartName()`; `filePath` empty for new/cloud projects; `fileModified` = `isNeedSave()` (`mainwindowtitleprovider.cpp:103-120`).
- **`NotationPageModel`**: wires dock toggles; **auto-shows the Percussion panel** by selection/note-input on drum staves per `percussionPanelAutoShowMode` and auto-shows the **Extensions toolbar** when any enabled extension action has `showOnToolbar` (`:212-357`).
- **`AppShellState`** (`appshellstate.cpp`): one UI-state flag, `showNavigator`, via `IUiState`.
- **DevTools page** (`DevToolsPage.qml`, only reachable with dev mode): Settings, UI Gallery, Interactive, Toast, Crash handler, Corrupt score, MPE, MuseSounds, Extensions, KeyNav (`:74-83`).

## 9. The app menu (`AppMenuModel`, `qml\MuseScore\AppShell\appmenumodel.cpp`)

Built entirely in C++ from **command IDs** (`makeMenuItem(COMMAND)`), titles/shortcuts/checked/enabled come from the command registry + state, not the model. `load()` (`:82-116`) assembles top-level menus **File, Edit, View, Add, Format, Tools, Extensions**, then **Help** (`makeHelpMenu(addDiagnosticsSubMenu)`): in `devModeEnabled` the Help menu has no diagnostics sub-menu and a separate top-level **Diagnostics** menu is appended; otherwise Diagnostics is a Help submenu. Then `setupConnections()` and `appMenuModelHook()->onAppMenuInited()` (macOS strips "Start Dictation"/"Special Characters").

Dynamic parts (`setupConnections`, `:123-159`):
- *Open recent* (`menu-file-open`) rebuilt on `recentFilesListChanged`; each entry is a `PROJECT_OPEN_COMMAND` query with `url` + `display_name`; cloud files get the CLOUD icon; disabled if empty; a "clear recent" item follows a separator.
- *Workspaces* submenu (`menu-workspaces`) when `MUSE_MODULE_WORKSPACE`.
- *Extensions* submenu (`menu-extensions`) rebuilt on `manifestListChanged` / `enabledChanged`: fixed "Manage extensions" item + enabled extensions grouped by category (extensions with exactly one `showOnAppmenu` action are inlined, else a titled submenu; categories and uncategorized sorted by translated title, `:734-800`).
- *Undo/Redo titles* ("Undo ‘%1’") updated on the current notation's undo-stack changes (`:257-272`).

Platform roles: Quit/Preferences/About/AboutQt use `MenuItemRole` (`QuitRole`, `PreferencesRole`, `AboutRole`, `AboutQtRole`) for the macOS app menu; `APP_FULLSCREEN_COMMAND` is omitted on mac (`:277-279`). Version-specific Help items: update check only if `updateConfiguration()->isAppUpdatable()`; MuseSampler cache clear only Win/mac with MuseSampler. `NavigableAppMenuModel` / `AppMenuBar.qml` / `PlatformMenuBar.qml` are the QML-side hosts (keyboard-navigable bar, native macOS menu).

## 10. Sessions & first-launch

### 10.1 `SessionsManager` (crash-restore bookkeeping)

State = a JSON array of project paths in `<userAppDataPath>/session/session.json` (`appshellconfiguration.cpp:322-342`), guarded by a cross-process `Read/WriteResourceLockGuard("SESSION")` (so multiple app instances don't corrupt it).

- `init()` (`sessionsmanager.cpp:31`): `update()` and re-`update()` on every `projectSession()->projectChanged()` and the project's `pathChanged()`.
- `update()` (`:80`): current project path (**new unsaved projects use `projectConfiguration()->newProjectTemporaryPath()`**) — remove the previously tracked path, add the new one; no-op if unchanged.
- `deinit()` (`:46`): only if **last window** (`windowCount()==1`) and `startupModeType() != ContinueLastSession` → `reset()` (clear the list). So a **clean** exit leaves an empty list; a **crash** leaves paths behind → next launch's first window sees `hasProjectsForRestore()` → `Recovery` (§5.3). With "continue last session" configured the list is deliberately kept and consumed by `PROJECT_CONTINUE_LAST_SESSION_COMMAND` instead.
- `restore()` (`:63`): dispatches `PROJECT_OPEN_COMMAND` per path; `reset()` writes `[]`.
- Autosave unsaved-changes files are deleted via `projectAutoSaver->removeProjectUnsavedChanges` when the user declines restore (`startupscenario.cpp:313-318`). Autosave internals → `file-format-and-rw.md`/project layer.

### 10.2 First-launch setup & Welcome dialog

- Flag `application/hasCompletedFirstLaunchSetup` (default false; **always true on WASM**, `appshellconfiguration.cpp:96-103`); set shared via `setSharedValue` (cross-instance).
- `FirstLaunchSetupModel` (`FirstLaunchSetup\firstlaunchsetupmodel.cpp`): 3 pages — `ThemesPage` (background `musescore://notation`), `PlaybackPage` (notation), `TutorialsPage` (`musescore://home?section=learn`); each page change opens its *background* URI behind the floating dialog (`:34-39,96-98`). `askAboutClosingEarly()` shows a "Visit MuseHub / Keep going / Cancel" warning (`:101-148`; returns true when the user cancels or visits MuseHub). `finish()` sets the completed flag (`:150`).
- `WelcomeDialogModel` (`welcomedialogmodel.cpp`): carousel of 5 hard-coded promo/tutorial items (MuseSounds Pro, What's new, cloud storage, free MuseSounds, tutorials), cycling and persisting the last-shown index; only shown per §5.3.

## 11. Configuration (`AppShellConfiguration`, module name `appshell`)

Settings keys (`appshellconfiguration.cpp:40-66`):

| Key (under `appshell/`) | Default (set in `init()`, `:71-94`) | Use |
|---|---|---|
| `application/hasCompletedFirstLaunchSetup` | false | first-launch gate |
| `application/welcomeDialogShowOnStartup` | true | user preference |
| `application/welcomeDialogLastShownVersion` | `"0.0.0"` | re-show on version bump |
| `application/welcomeDialogLastShownIndex` | −1 | carousel position |
| `application/startup/modeStart` | `StartupModeType::StartEmpty` | which startup mode |
| `application/startup/startScore` | `projectConfiguration()->myFirstProjectPath()` | file for `StartWithScore` |
| `ui/application/startup/showSplashScreen` | **no default set** | `needShowSplashScreen()` — see UNCLEAR |

Other: URL builders (`handbookUrl` with `utm_*` + locale, ask-for-help, accessibility statement, forum, contribute, MuseHub free MuseSounds, MusicXML license URLs — most derive from `updateConfiguration()->appWebSiteUrl()`), `museScoreVersion()` = `version + "." + build`, a transactional `startEditSettings/applySettings/rollbackSettings` (settings transaction used by Preferences), `revertToFactorySettings(keepDefault, notify, notifyOthers)` → `settings()->reset(...)`, and the session-file accessors. `init()` also `makePath(sessionDataPath())`.

## 12. Splash screen (`src\appshell\widgets\splashscreen`)

A `QWidget` (`Qt::SplashScreen | FramelessWindowHint`, translucent background; macOS flag tweak for rounded corners, `splashscreen.cpp:34-39`) painting one of two views: `LoadingScreenView` (`Default`, shown before setup) or `NewInstanceLoadingScreenView(forNewScore, openingFileName)` (`ForNewInstance`, shown per context for secondary instances opening a file/new score). Gated by `MUE_ENABLE_SPLASHSCREEN`. Closed and deleted in `MuseScoreGuiApp::doStartupScenario` right before `runAfterSplashScreen`. Note `m_splashScreen` is a **single member**: `showContextSplash` assigns it without freeing a previous one.

## 13. `ShortcutResolver` and `ExtensionContextResolver` (small app-owned policies)

- **`ShortcutResolver`** (`IShortcutsResolver`, `shortcutresolver.cpp`): decides which shortcut wins when several bind the same keys, by **scope priority**: NAVIGATION 1, PLAYBACK 2, NOTATION_PAGE 3, NOTATION 4 (only if the `ScoreView` navigation panel is active), NOTATION_TAB 5, NOTATION_SELECTION 6, SINGLE/RANGE_SELECTION 7, NOTATION_NOTE_INPUT 8, NOTATION_TEXT_EDITING 9; inactive notation scopes return −1; unknown scopes → 0 (+debug log). `selectActive` stable-sorts by priority desc, tie → entries with a real command beat `NONE_COMMAND`; if the winner's priority is negative returns an empty `Shortcut`. Also `scopeTitle()` for the shortcuts UI. Framework side → `framework-services.md`.
- **`ExtensionContextResolver`**: `isContextAllowed("project opened")` ⇔ `projectSession()->project() != nullptr`; fires `contextChanged` on project change.

## Cross-subsystem dependencies

- `architecture-and-ioc.md` — boot phases (`BaseApplication::doSetup`, `setupContext`, `doFinish`), `GlobalInject`/`ContextInject`, `IModuleSetup`/`IContextSetup`; here only the *app-specific* hooks are shown.
- `ui-qml-mvvm.md` — `IInteractive`/`IInteractiveUriRegister` page & dialog URIs, `DockWindow`, `iocCtxForQmlObject`/`ioc_context`, `AbstractMenuModel`, `GuiApplication` window management.
- `framework-services.md` — `rcommand` (commands register/state/dispatcher), shortcuts, multiwindows provider (`isFirstWindow`, `openNewWindow`, `quitForAll`), update/workspace/extensions/languages.
- `mscore-cli.md` — the converter/diagnostic/testflow options and `ConverterController`.
- project layer (`src\project`) / `file-format-and-rw.md` — `PROJECT_OPEN/NEW/CONTINUE_LAST_SESSION` commands, `IProjectSession`, `IOpenProjectScenario`/`ICloseProjectScenario`, autosave, recent files.
- `notation-layer.md` — `NotationPageModel`/menu build use `INotation`, undo stack, note-input state.

## Non-obvious gotchas

- ⛔ **`setupNewContext()` can only be called once per process** (assert, `baseapplication.cpp:293-299`) in non-single-process multiwindow mode; further windows are separate processes (or single-proc mode contexts). Don't call it ad hoc to open a window — use `IMultiWindowsProvider::openNewWindow`.
- ⭐ **Window close ≠ close.** `QEvent::Close`/`Quit` on the main window are swallowed and converted to `quit(false)` (`appshellcommandscontroller.cpp:98-107`); don't expect `closeEvent` side effects elsewhere.
- ⭐ Filter is installed on `qApp` in `preInit` but `mainWindow()` (via `qWindow()`) is `nullptr` until the window exists — Close/Drag handling for the window only starts working after `loadMainWindow`.
- `FileOpen` before `startupCompleted()` is **queued as the startup score**, not opened immediately (hence the macOS `processEvents()` in `AppShellModule::onAllInited`).
- CLI GUI mode opens **only the first** positional file (`commandlineparser.cpp:549`); `-F` and `-R` are identical in code.
- `--session-type` valid strings (`start-empty|continue-last|start-with-new|start-with-file`) live in `startupscenario.cpp:43-62`; the parser comment names a non-existent `sessionTypeTromString` (the function is `modeTypeTromString`, typo `Tro`). `start-with-file` without a file falls back to `startScore` setting.
- `isStartWithNewFileAsSecondaryInstance()` (`startupscenario.cpp:84`) has **no callers** in `src\` (only definitions in desktop + web appshell) — likely dead/legacy.
- `AppshellCommandsController` declares `setupConnections()` and `isFullScreen()` (`.h:93,114`) with **no definitions** in the `.cpp` — dead declarations (harmless unless something references them).
- Splash: only ONE `SplashScreen*` (`guiapp.h:47`); `showSplash` (app-level, Default) and `showContextSplash` (ForNewInstance) are mutually exclusive by config type, otherwise one would leak.
- Console `-F/-R` resets via `settings()->reset(value)` where `value` is `true` ("keep defaults?" parameter naming differs from `revertToFactorySettings`'s `keepDefaultSettings`); GUI path calls `appshellConfiguration()->revertToFactorySettings(value)` — same underlying `reset`, verify argument semantics in `muse\framework\global\settings` before relying on it.
- Module order in `AppFactory` is irrelevant except **`diagnostics` first** (crash handler install).
- Stub vs real: grepping `AppFactory` shows the *same* `addModule(new X())` for real and stub builds — check CMake options (`SetupConfigure.cmake`) to know which is linked.
- Debug builds skip MuseSampler (`appfactory.cpp:333-345`) — playback with MuseSounds is unavailable in debug unless `MUSE_MODULE_MUSESAMPLER_LOAD_IN_DEBUG`.

## UNCLEAR — needs verification

- `AppShellConfiguration::needShowSplashScreen/setNeedShowSplashScreen` have no consumers in `src\` and no default registered — apparently vestigial; confirm there is no QML/preferences usage (`grep` found none under `src\`; `muse\` not searched).
- `webapi_export.cpp` + `exported_functions.cmake` are the **WASM/Emscripten** exports for the web app (`load(source,len)`, `addSoundFont(uri)`, `startAudioProcessing()` → `mu::appjs::WebApi`); compiled only when `CC_IS_EMCC` (`src\app\CMakeLists.txt:230-232`). The web app's `src\web\appshell` and `src\web\appjs` were not read.
- Exact semantics of `--score-video` vs `-o` interplay (§4) not traced end-to-end in `ConverterController`.
- macOS `MacOSAppMenuModelHook` implementation (`internal\platform\macos`) — only the header was seen.
- `AboutModel`, `NotationStatusBarModel`, `PublishToolBarModel`, `ThemesPageModel`, `TutorialsPageModel`, `SettingsListModel`, `NavigableAppMenuModel` bodies and the individual QML pages' layout were not read in depth.
- Per-platform `Main.qml` for mac/linux not read (only win); assumed analogous.

*Authored from: full read of `src\app\{main.cpp, appfactory.*, cmdoptions.h, internal\{guiapp,consoleapp,commandlineparser}.*}`, `src\appshell\{appshellmodule.*, appshellcommands.h, appshelltypes.h, iappshellconfiguration.h, internal\{startupscenario,sessionsmanager,appshellcommandscontroller,appshellcommandsregister,appshellcommandsstate,appshellconfiguration,appshellstate,shortcutresolver,extensioncontextresolver}.*, qml\…\{appmenumodel,maintoolbarmodel,mainwindowtitleprovider,notationpagemodel,welcomedialogmodel,FirstLaunchSetup\firstlaunchsetupmodel}.cpp, AppWindow.qml, WindowContent.qml, platform\win\Main.qml, widgets\splashscreen\splashscreen.cpp}`, plus `muse\framework\global\internal\baseapplication.cpp`, `muse\framework\ui\internal\guiapplication.cpp`, `SetupConfigure.cmake` (2026-11-08). **If this contradicts the live source, the source wins.***
