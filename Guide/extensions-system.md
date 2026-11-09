# MuseScore — Extensions System

> **What this file is.** Scope: the **modern extensions framework** — `muse\framework\extensions` (manifest-driven JS/QML extensions, manager/register/provider, installer, script engine, QML UI engines, the `legacy\` bridge that wraps old v1 `.qml` plugins) plus its app-side glue in `src\appshell` and `src\converter`. The *legacy v1 QML plugin API itself* (`PluginAPI`, `Cursor`, `Score`, ...) is `plugin-api.md`. **Live source wins** over this Guide; re-verify before editing. Start at `00-core.md`. Deep pass 1 (2026-11-08).

## 0. 60-second orientation

- ⭐ An **extension** = a directory with a `manifest.json` + JS (`macros`) and/or QML (`form`) files. It is discovered at startup from two roots, listed in **Home > Plugins**, **disabled by default**, and when enabled each manifest *action* becomes a **command** (`command://extension/<uri-path>/<action>`) that shows up in the Plugins menu, optionally a toolbar, and can be shortcut-bound.
- ⭐ The same framework also loads **legacy v1 `.qml` plugins** (MuseScore 3/4 style) by synthesising a `Manifest` from the QML file header (`internal\legacy\`) — one list, one enable switch, one menu for both (§6).
- Where: `muse\framework\extensions\`. Key files: `extensionstypes.h` (Manifest/Action), `internal\extensionsloader.cpp` (manifest.json parser), `internal\extensionsregister.cpp` (global list + enable state), `internal\extensionsprovider.cpp` (perform/run/session), `internal\scriptengine.cpp` (QJSEngine + `require`), `qml\Muse\Extensions\extensionbuilder.cpp` (hosts a QML form).
- Bundled extensions: `share\extensions\` (§4). Design ADR: `muse\docs\adr\00102-Extensions-Adding-support-for-native-code-plugins.md` — ⚠ **native-code support is NOT implemented in this tree** (§5).
- ⛔ README says it plainly: *"A module implementing an extension system. Allows adding user functionality, similar to browser extensions."* (`muse\framework\extensions\README.md:3`).
- If `MUSE_MODULE_EXTENSIONS` is off the app links `muse\framework\stubs\extensions\*` instead (`src\app\appfactory.cpp:138-141`); the real module is added at `appfactory.cpp:424` (GUI) and `:550` (console).

## 1. The extension model

### 1.1 Manifest (`extensionstypes.h:135`, parser `extensionsloader.cpp:162`)

```cpp
// muse::extensions::Manifest
ExtensionUri uri; io::path_t path; ExtensionType type; String title, description, category;
io::path_t thumbnail; std::string version, context; int apiversion = DEFAULT_API_VERSION; // 2
bool legacyPlugin, isRemovable;
std::map<std::string, std::vector<ValMap>> contributes; std::vector<Action> actions;
```

`manifest.json` keys as actually parsed (`extensionsloader.cpp:181-267`):

| Key | Req? | Parse / default | Notes |
|---|---|---|---|
| `uri` | **yes** | `Uri`; scheme *forced* to `extension` (warn + `setScheme`, :188-191) | identifier. Shipped dev manifests write `musescore://...` and rely on the coercion. |
| `type` | **yes** | `form` / `macros` / `composite`, else `Undefined` (`typeFromString`, `extensionstypes.h:69`) | `Undefined` makes `Manifest::isValid()` false → silently dropped (`extensionsloader.cpp:95`). |
| `title` | **yes** | String | |
| `actions` | **yes** | array (may be empty) | each needs `path` (:247-252). |
| `description` `category` `thumbnail` `version` | no | strings | thumbnail path resolved relative to manifest dir. |
| `apiversion` | no | **`toInt()`**, default 2 (:210) | 1 = legacy QML-plugin API, 2 = modern. (Header comment says "String"; shipped manifests use a JSON number.) |
| `ui_context` | no | default `ProjectOpened` | manifest-wide default for actions. Values: `Any`, `ProjectOpened` (`extensionstypes.h:50-52`). |
| `modal` | no | default `false` | default for form actions. |
| `contributes` | no | `{name: [obj,...]}`; non-array values and non-object items are skipped (:211-230); JSON→`Val` via `jsonValue` (:63) | generic extension-point data for host apps (e.g. Audacity `audacity.effects`, see unit test `ParsesContributions`, `tests\extensionsloader_tests.cpp:73`). Nothing in MuseScore src consumes it. |

Per-action keys (`extensionsloader.cpp:245-266`): `path` (req), `code` (default = index as string), `type` (default = manifest type), `modal`, `title`, `icon` (`ui::IconCode::fromString`), `ui_context`, `show_on_appmenu` (default true), `show_on_toolbar` (default false), `func` (default `"main"`). `Action.apiversion` is copied from the manifest. `Action::isValid()` = type≠Undefined && code non-empty.

### 1.2 Types

`ExtensionType { Undefined, Form (QML UI), Macros (JS script, no UI), Composite (both) }` (`extensionstypes.h:62`). A *composite* manifest declares per-action `type` (see `share\extensions\courtesy_accidentals\manifest.json`). Dispatch is by **action** type, not manifest type (`ExtensionsProvider::perform`, §2.4).

### 1.3 Identity and commands

`ExtensionUri = Uri`. Command URI = `command://extension/<uri.path>/<action>` (`extensionscommands.h:30`); inverse helpers `extensionUriByCommand`/`actionByCommand` (:41,:61). Plus one framework command `command://extensions/open-apidump` (`extensionscommands.h:28`).

### 1.4 Lifecycle in one line

discover (`ExtensionsRegister::reload`) → listed → user enables (`setEnabled`, persisted) → commands registered → triggered (`perform`) → Form: open viewer dialog / Macros: new `ExtensionSession` evaluates script + calls `func`. Install/uninstall = unzip/delete + `reloadExtensions()`.

## 2. Manager / provider / install flow

### 2.1 Module wiring (`extensionsmodule.cpp`)

Two scopes (ADR 00001/00002, see `architecture-and-ioc.md`):

| Scope | Registered | Where |
|---|---|---|
| **Global** (`registerExports`, :58) | `IExtensionsConfiguration`, `IExtensionsRegister` | one per process |
| **Per window context** (`ExtensionsContext::registerExports`, :117) | `IExtensionsUiEngine`, `IExtensionsProvider`, `IExtensionInstaller`; `ExtensionsActionController` created | one per window |

Init order: `onInit` (global, :95) → `m_configuration->init()` (create dirs), `m_extensionsRegister->reload()` (first scan), register diagnostics paths. `resolveImports` (:67) registers viewer URIs `muse://extensions/viewer` → `ExtensionViewerDialog` and `muse://extensions/apidump`, and adds `ExtensionsCommandsRegister` to `ICommandsRegister`. `registerApi` (:83) registers the v1 QML types and adds **filesystem allow-list bases** `extensions: userPath` + `plugins (legacy): userPath` to `IFileSystemApiRestriction`. Context `onInit` (:135) → `m_actionController->init()` then `m_provider->reloadExtensions()`.

`IExtensionContextResolver` (`iextensioncontextresolver.h`) is **"must be implemented externally"**; MuseScore implements it in `src\appshell\internal\extensioncontextresolver.cpp:36` — `ProjectOpened` ⇒ `projectSession()->project() != nullptr`, everything else (incl. `Any`) ⇒ true; `contextChanged` fires on `projectChanged`.

### 2.2 Disk layout (`extensionsconfiguration.cpp:54-77`)

| Root | Path | Purpose |
|---|---|---|
| default | `appDataPath()/extensions` (= `share\extensions` in install) | bundled, not removable |
| user | `userAppDataPath()/extensions` | installed `.mext`s + `config.json`; `isRemovable=true` |
| legacy default | `appDataPath()/plugins` (= `share\plugins`) | bundled v1 plugins |
| legacy user | setting `application/paths/myPlugins`, default `userDataPath()/Plugins` | user `.qml` plugins (`plugin-api.md`) |

`config.json` = array of `{uri, enabled}` guarded by a multiwindow resource lock `EXTENSIONS` (`:84-109`). **First-run migration**: if `config.json` is absent, read the old `userAppDataPath()/plugins/plugins.json` (codeKey-keyed) and map codeKeys→URIs through `legacy::ExtPluginsLoader::loadCodekeyUriMap` (:155-205); two codeKeys are hard-mapped to the ported extensions: `colornotes` → `extension://notation/colornotes`, `addCourtesyAccidentals` → `extension://notation/courtesy_accidentals` (`extpluginsloader.cpp:61-62`).

### 2.3 Register (global; `internal\extensionsregister.cpp`)

- `reload()` (:34): `ExtensionsLoader::loadManifestList(default,user)` + `legacy::ExtPluginsLoader::loadManifestList(pluginsDefault,pluginsUser)` joined into one `m_manifests`, reload `m_configs`, fire `manifestListChanged`.
- Loader filters (`extensionsloader.cpp:84-120`): skips invalid manifests; **skips any URI whose path starts with `extensions/dev/` unless `runtime::isDebug()`**; marks everything from the user root `isRemovable`. Discovery is `Dir::scanFiles(root,{"manifest.json"})` (recursive scan; one extension per manifest) — `:144`. A parse failure is logged and skipped (:129).
- `isEnabled(uri)` = `m_configs[uri].enabled` — **default false** (`extensionstypes.h:171`). `setEnabled` writes `config.json` and sends `enabledChanged`.
- `knownCategories()` (:98): `composing-arranging-tools`, `color-notes`, `playback`, `lyrics` (translatable). Unknown manifest categories are added on the fly in the app menu (`appmenumodel.cpp:772`).

### 2.4 Provider (per context; `internal\extensionsprovider.cpp`)

A thin facade over the register plus execution:

```cpp
// ExtensionsProvider::perform (:89)
switch (a.type) {
case Form:   if (a.docked) { m_dockRequested.send(uri, actionCode); return ok; }
             openSync("muse://extensions/viewer?extension=..&action=..&modal=..")  // :107-111
case Macros: return run(a, m);
default: UnknownError }
```

`run(Action, Manifest)` (:153): `a.legacyPlugin` → `legacy::ExtPluginRunner`; otherwise `newSession(manifest, a.path)` → `session->evaluate()` (→ `Err::ExtLoadError` on failure) → `session->call(a.func)` (→ `Err::ExtBadFormat`). Errors in `extensionserrors.h` (`ExtNotFound/ExtLoadError/ExtBadFormat`, base `Ret::Code::ExtensionsFirst`). Note: `Composite` type at the *action* level is rejected (falls to `UnknownError`) — composites must declare per-action `form`/`macros`. `reloadExtensions()` first calls `extensionsUiEngine()->clearComponentCache()` then `register->reload()` (:39) — without it QML edits only take effect after restart.

`newSession(uri, relativeScriptPath)` (:137, used by host apps) resolves the script through `resolveBundleFile` (`internal\extensionbundle.cpp:25`): rejects empty/absolute paths and anything whose canonical path escapes the bundle directory (`../`) — covered by test `ResolvesFileFromBundleDirectory`.

### 2.5 Action controller / commands

- `ExtensionsActionController::registerExtensions` (:44) re-registers a dispatcher handler for **every** action of **every** manifest (enabled or not) on each `manifestListChanged`, plus `OPEN_APIDUMP_COMMAND`.
- `onExtensionTriggered` (:61): enabled → `provider->perform`; disabled → a Yes/No warning *"The plugin “%1” is currently disabled. Do you want to enable it now? … Home > Plugins."*; Yes → `setEnabled(true)` + `perform`.
- `ExtensionsCommandsRegister` (`internal\extensionscommandsregister.cpp:56`) publishes one `CommandInfo` per action (title = action title or manifest title, icon from action) into the rcommand registry (ADR 00101/00104, `framework-services.md`); `ExtensionsCommandsState::commandState` (:81) enables a command only if `contextResolver()->isContextAllowed(manifest.context)` — note it uses the **manifest-level** `context`, not `Action.context`.

### 2.6 Install / uninstall (`internal\extensioninstaller.cpp`)

- Package format: **`.mext`** = a zip with `manifest.json` at its root (`SINGLE_FILE_EXT = "mext"`, `extensionstypes.h:47`; `isFileSupported` :16).
- Entry point: drag-and-drop on the main window — `AppshellCommandsController::dragTarget` → `DragTarget::Extension` → `installExtension(path)` (`appshellcommandscontroller.cpp:173,234-238`). (No menu "Install extension…" found.)
- `installExtension` (:25): read `manifest.json` from the zip (error dialog if missing) → `ExtensionsLoader::parseManifest(ByteArray)` → if URI not installed → `doInstallExtension`; same `version` string → info "already installed"; existing not removable → error "cannot be updated"; else Cancel/OK "Update extension" question → `uninstallExtension` + `doInstallExtension`.
- `doInstallExtension` (:88): unpack to `userPath()/<zip basename>_<Uuid::gen()>/`, then `provider()->reloadExtensions()`. Install does **not** auto-enable.
- `uninstallExtension` (:113): requires `isRemovable`; legacy plugin → delete the single `.qml` file (asserts suffix `qml`); else **delete the whole directory** containing the manifest; then reload. UI trigger: `ExtensionsListModel::removeExtension` (`qml\Muse\Extensions\extensionslistmodel.cpp:199`).

## 3. What an extension gets — API + QML runtime

### 3.1 Macros (JS): `ScriptEngine` (`internal\scriptengine.cpp`)

Each `ExtensionSession` owns one `ScriptEngine` = one `QJSEngine` (console extension installed), a `JsApiEngine`, and a global `api` object (`ExtApi` for v2, `ExtApiV1` for v1). Constructor (:39-77) also installs all registered **global enums** (`apiRegister()->globalEnums()`, e.g. `Interactive` `ButtonCode`), and `require`/`exports`/`module` globals (CommonJS style).

- `evaluate()` (:166) reads the script via the filesystem service, evaluates it **only if the content changed** (`m_lastEvalScript`), `call(func)` (:196) then looks up a **global function** by name and calls it; return value mapped by `jsValueToRet` (:240): exception → `UnknownError` with file/line/err (`"Error: abort"` → `Cancel`), `false` → error, object with `code`/`text` → that Ret, else Ok.
- `require(module)` (`jsmoduleloader.cpp:55`): `MuseInternal.*` and `api.*` are **blocked** ("Not allowed module"); `MuseApi.*` → `apiRegister()->createApi(name)`; else treat as JS file — relative to the current script dir, then `defaultPath()`, then `userPath()` (`resolvePath`, :116; `.js` appended if missing); fallthrough → application module via `requireModule`. File modules are wrapped in `(function(){ ... }())` and their `exports` returned (`scriptengine.cpp:122-144`). `//! TODO Make recursion protection` — no cycle guard.

Public `MuseApi.*` modules registered in the tree (grep of `regApiCreator`/`regApi`/`regApiSingltone`): `Log` (`globalmodule.cpp:120`), `Interactive` (+ButtonCode enum, `interactivemodule.cpp:64-66`), `Theme` (`uimodule.cpp:117`), `Engraving` (`src\engraving\engravingmodule.cpp:141`), `Converter` (`src\converter\convertermodule.cpp:46`), `Websocket`/`WebsocketServer`/`Http` (`networkmodule.cpp:66-69`), `FS` (`globalmodule.cpp:122`). Hazard APIs `api.process` and `MuseApi.FS` are *not* exposed on `ExtApi` (see the "ATTENTION" comment, `extapi.h:51-56`).

`ExtApi` properties (`api\extapi.h:37-59,67-82`): `log`, `interactive`, `theme`, `engraving`, `converter`, `websocket`, `websocketserver` as `Q_PROPERTY`; `dispatcher/navigation/shortcuts/keyboard/accessibility` exist as **non-property** accessors (so not script-visible as properties) and `shortcuts/navigation/keyboard/accessibility` are commented out as "requires approval". Sample (`share\extensions\quickstart\main.js`): `const Log = require("MuseApi.Log"); function main(){ Log.info(...); Interactive.info(...) }`.

**v1 JS macros**: when `apiversion == 1` the engine builds `ExtApiV1` and calls `setup(globalObj)` which asks `MuseApi.Engraving` (cast to `IApiV1Object`) to inject the legacy globals (`curScore`, enums...) — `scriptengine.cpp:50-56`, `extapiv1.cpp:58-79`. This is how e.g. `colornotes/main.js` (`apiversion: 1`, uses `api.log...`) runs as a JS macro.

**Sandboxing**: `ApiContext{ apiversion, bundlePath = extension dir, scopeId = uri }` is passed to the JS engine (`iapiengine.h:35`), and `ExtensionSession` adds the extension directory as an allowed filesystem base for its lifetime, removed in the destructor (`extensionsession.cpp:37,42`).

### 3.2 Forms (QML): `ExtensionsUiEngine` + `ExtensionBuilder`

- `ExtensionsUiEngine` (per context) lazily owns **two separate `QQmlEngine`s** (`extensionsuiengine.cpp`): v2 (`apiversion` property 2, `ioc_context`, `ui` context property, global `api = ExtApi`, global enums) and v1 (`apiversion` 1, `api = ExtApiV1`, import path `:/qml` so old plugins can `import MuseScore.UiComponents`; comment admits this "need[s] to think about how to limit"). `clearComponentCache()` drops compiled components on both.
- `ExtensionBuilder::load` (`qml\Muse\Extensions\extensionbuilder.cpp:62`) picks the engine by `a.apiversion`; for **v2 forms it rejects QML whose lines start with `import` and contain `Muse.`** — *"Importing 'Muse.' modules is prohibited."* (`validateImports`, :42-60) — then shows `ExtensionErrorMessage.qml` instead. The component is created with initial property `parent`; implicit size defaults 600×480 (360 wide if docked). For v1 it casts the root to `IPluginApiV1`, calls `setup(engine)`, wires `closeRequest()`, and **queues `runPlugin()` on the Qt event loop** (comment :162-165: `openSync` nested loop would starve `async::Async`).
- `ExtensionViewer.qml` hosts one builder; `ExtensionViewerDialog.qml` is the `muse://extensions/viewer` dialog.

Other QML in the module (`qml\Muse\Extensions\`): `ExtensionsListPanel`/`internal\ExtensionsListView`/`ExtensionItem`/`EnablePanel` (Plugins page; model roles uri/name/description/thumbnailUrl/enabled/category/version/shortcuts/isRemovable — `extensionslistmodel.cpp:46`), `ExtensionsToolBar` + `extensionstoolbarmodel.cpp` (one `ToolBarItem` per enabled action with `show_on_toolbar`), `ExtensionsApiDump`/`devtools\apidumpmodel` + `devextensionslistmodel` (developer tools; "Show API dump" command).

### 3.3 UI integration points in the app

- **Plugins menu**: `AppMenuModel::makeExtensionsItems` (`appmenumodel.cpp:734`) — *enabled* manifests only; actions with `show_on_appmenu`; 1 visible action → plain item, >1 → submenu titled by manifest; grouped by category (translated titles), sorted; uncategorised appended last.
- **Toolbar**: `NotationPageModel::doUpdateExtensionsToolBarVisibility` (`notationpagemodel.cpp:328`) opens the extensions toolbar dock iff any enabled manifest has a `show_on_toolbar` action.
- **Home > Plugins**: `PluginsPage.qml` → `ExtensionsListPanel`; "Reload plugins" button → `ExtensionsListModel::reloadPlugins` → `provider()->reloadExtensions()`.
- **Shortcuts**: only action 0's command is editable from the list (`editShortcut`, `//! TODO add actions support`, :175).
- **CLI**: `--extension <uri>` (`commandlineparser.cpp:92,477`) → `ConverterController::convertByExtension` → `extensionsProvider()->perform(uri, action)` with action from query param `action` (default `"main"`) (`convertercontroller.cpp:489-495`). See `mscore-cli.md`.

## 4. Bundled extensions (`share\extensions\`)

| Dir | Manifest `uri` | Type / api | Notes |
|---|---|---|---|
| `colornotes` | `extension://notation/colornotes` | macros, apiversion 1, category `color-notes` | ported v1 plugin; `main.js` |
| `courtesy_accidentals` | `extension://notation/courtesy_accidentals` | composite, apiversion 1 | actions `configure`(form, `configure.qml`) / `add`(macros) / `remove`(macros) |
| `example1` | `musescore://extensions/dev/example_1` | form, modal, `icon: CLOCK`, `ui_context: Any` | `Main.qml` |
| `example2` | (dev) | macros | `main.js` |
| `example3` | (dev) | composite | `configure.qml` + `add.js` + `remove.js` |
| `quickstart` | `musescore://extensions/dev/quickstart` | macros | `main.js` (Log + Interactive) |
| `quickstart_v` | `musescore://extensions/dev/quickstart_v` | form | `Main.qml` |
| `batch_example` | `musescore://extensions/dev/batch_example` | composite, `ui_context: Any` | `configure.qml` + `processing.js` (`show_on_appmenu:false`) — for CLI batch |
| `dev\apitests`, `dev\fetch1` | `musescore://extensions/dev/apitests` ... | composite / form | `show_on_toolbar:true` actions with `icon: PLUS/MINUS` |

(example2/3/fetch1 manifests not individually read; types per file listing + loader rules.) The dev-prefixed URIs become `extension://extensions/dev/...`; the loader hides URIs starting `extensions/dev/` in **non-debug** builds (`extensionsloader.cpp:99`). Full example (`colornotes\manifest.json`):

```json
{ "uri": "extension://notation/colornotes", "type": "macros", "title": "Color Notes",
  "category": "color-notes", "thumbnail": "color_notes.png", "version": "3.5", "apiversion": 1,
  "actions": [ { "path": "main.js" } ] }
```

## 5. Native-code plugins (ADR 00102) — status

⭐ ADR 00102 (*Accepted*, dated 2026-07-15, maintainers D. Makarenko / I. Korsukov): extend the **one** extension system with loadable dynamic libraries callable from JS — motivated by Audacity (OpenVINO, DSP libs). Proposed shape:

```js
const native = require("MuseApi.Native");  const lib = native.open("gain");  const gain = lib.bind("process");
```
```c
EXT_EXPORT int32_t extension_dispatch_v0(const char* call, const ext_value* args, uint32_t n, ext_value* result)
```
(`ext_value` types incl. `EXT_VALUE_BUFFER`, `EXT_VALUE_NUMBER`, `EXT_VALUE_NONE`; status codes `EXT_STATUS_OK/UNKNOWN_CALL/INVALID_ARGUMENT`; header `extension.h`.) Con noted by the ADR: a JS glue layer is required even for UI-less plugins; a separate native-plugin system was rejected as duplicative.

**TRACED: not present in this tree.** Greps for `MuseApi.Native`, `extension_dispatch`, `EXT_EXPORT` across the whole repo (excluding `*.md`) return nothing; `muse\framework\global\dlib.h` is only the generic dlopen helper, `MuseApi.Native` is not registered by any module, `JsModuleLoader::require` has no native branch, and `Manifest` has no library field. What *is* present and ADR-adjacent groundwork: the generic `contributes` manifest map, `resolveBundleFile` bundle-path confinement, `IExtensionsProvider::newSession` + `IExtensionSession` (host apps drive an extension script directly, with `exports()`/`wrapQObject()`), `ApiContext.bundlePath`/`scopeId`. Treat native loading as **design only** until a `MuseApi.Native` module appears.

## 6. Legacy v1 plugins vs. modern extensions — coexistence

| Aspect | Legacy v1 `.qml` plugin | Modern extension |
|---|---|---|
| Unit | one `.qml` file (root `MuseScore {…}`), `apiversion=1` | dir + `manifest.json`, `apiversion` default 2 |
| Discovery | `legacy::ExtPluginsLoader` scans `*.qml` in `share\plugins` + user Plugins dir (`extpluginsloader.cpp:116`) | `ExtensionsLoader` scans `manifest.json` in `share\extensions` + `userAppDataPath()/extensions` |
| Manifest | **synthesised** by text-scanning the QML header: needs the word `MuseScore` and a `{`; reads `title`, `description`, `pluginType`, `categoryCode`, `thumbnailName`, `requiresScore`, `version` (stops after 7 or at 2nd `{`); `//4.4 ` prefix lines honoured; `qsTr("…")`/quotes stripped (:127-291) | parsed JSON |
| URI | `extension://plugin/v1/<path rel. to root, lowercased, '.'→'-'>` (`makeUri`, :37) | manifest `uri` |
| Type | `pluginType` `dialog`/`dock` → `form`, else `macros`; `requiresScore:false` → context `Any`, else `ProjectOpened`; `dock` forces `ProjectOpened` and sets `Action.docked` | per manifest |
| Single action | code `"main"`, `legacyPlugin=true` | any number |
| Run | macros → `ExtPluginRunner::run` (`QQmlComponent` on `qmlEngineApiV1()`, cast to `IPluginApiV1`, `runPlugin()`; `Err::ExtBadFormat` if not a MuseScore plugin) ; forms → viewer dialog → `ExtensionBuilder` v1 path | JS: `ScriptEngine`; QML: v2 engine with import restrictions |
| API | v1 QML API (`src\engraving\api\v1`, see `plugin-api.md`), `import MuseScore 3.0` types registered by `ExtApiV1::registerQmlTypes` (`MessageDialog`, `Settings`, `FileDialog`, `StandardButton`; `extapiv1.cpp:35-42`) | `api.*` + `MuseApi.*` modules |
| Enable state | same `config.json` (by URI) | same |
| Disable `dev_test_` | files containing `dev_test_` skipped unless debug (`extpluginsloader.cpp:100`) | `extensions/dev/` skipped unless debug |
| Remove | deletes the `.qml` | deletes the dir |

**Migration story**: plugins are being ported one at a time to modern manifests that keep `apiversion: 1` (so the old JS/engraving globals still work — `colornotes`, `courtesy_accidentals`) while the old codeKey enable-state is carried over by the hard-coded map (§2.2). A v1 plugin and a v2 extension are *indistinguishable* to menus, toolbars, Plugins page, shortcuts, command dispatch and the CLI.

## Cross-subsystem dependencies

- `plugin-api.md` — the v1 API objects an `apiversion: 1` extension/plugin sees; user Plugins dir; `QmlPluginApi` implements `IPluginApiV1`/`IApiV1Object`.
- `ui-qml-mvvm.md` — extensions get their own `QQmlEngine`s but use the window `ui` engine and `ioc_context` (`QmlIoCContext`); dialogs via `interactive` URIs; `uicomponents` toolbar items.
- `framework-services.md` — `rcommand` commands/state registers (ADR 00101/00104), shortcuts (`commandShortcutsRegister` in `extensionslistmodel.cpp`), `global/api` (`IApiRegister`, `JsApiEngine`, `IFileSystemApiRestriction`), settings, multiwindow resource lock, zip reader/unpack.
- `architecture-and-ioc.md` — global vs context IoC scoping used by `ExtensionsModule`/`ExtensionsContext`.
- `app-shell-and-startup.md` — `ExtensionContextResolver`, Plugins menu/toolbar, drop-install.
- `mscore-cli.md` — `--extension` job runner.
- `engraving-dom.md` / `notation-layer.md` — what `MuseApi.Engraving` wraps.

## Non-obvious gotchas

- ⭐ Extensions are **disabled by default** and a freshly installed `.mext` is not enabled; triggering a disabled command prompts to enable (§2.5).
- ⭐ `type` of the **manifest** only validates; **dispatch uses the action `type`**. A `composite` action type hits `UnknownError` in `perform` (`extensionsprovider.cpp:101-119`).
- A manifest `uri` with any scheme is rewritten to `extension://` — and the first path component becomes the *URI host/path* as `Uri` defines it; verify `Uri::path()` semantics before relying on the `extensions/dev/` prefix test (HYPOTHESIS that it works as intended; shipped dev manifests depend on it).
- `ExtensionsCommandsState` uses `manifest.context`, `ExtensionsProvider` ignores context entirely at `perform` time (CLI/dispatch bypass `ProjectOpened` gating; only the command *state* is gated).
- `legacy ExtPluginsLoader::parseManifest` reads the header by **text matching** — properties placed after the first nested `{`, or a plugin whose header lacks the substring `MuseScore`, are silently not recognised.
- `loadManifestList` ordering: bundled first then user; **no duplicate-URI check** — two manifests with the same URI both enter `m_manifests`; `manifest(uri)` returns the first match.
- `ExtensionActionController` registers commands for disabled extensions too (so the enable prompt works); the menu only lists enabled ones.
- Script re-evaluation is skipped when the file content is unchanged (`m_lastEvalScript`), but each `run` constructs a **new** `ExtensionSession` + engine, so global state does not persist across runs.
- v2 form import ban is a **line-text heuristic** (`import` + `Muse.`); it is not a real sandbox (e.g. `ui` context property is injected for UI components by design).
- The v1 QML engine has `:/qml` on its import path (broad access to app QML modules) — acknowledged in a code comment.
- Install unpacks into a fresh `<basename>_<uuid>` dir, so an update (uninstall old + install new) never collides, but a failed unpack may leave a partial dir (UNCLEAR, not traced).
- Possible doc drift: `extensionstypes.h` manifest comment lists `type` values "form, macros" and `apiversion` "String"; code also accepts `composite` and parses an int.

## UNCLEAR — needs verification

- **Docked forms**: `ExtensionsProvider::perform` emits `dockRequested(uri, action)` for `Action.docked`, `ExtensionPanel.qml` and `ExtensionBuilder` support it, but no consumer of `dockRequested()` or instantiation of `ExtensionPanel` exists in `src\` or elsewhere in this tree (grep) — the right-panel hosting appears unwired here (only legacy `pluginType: dock` sets `docked`; `ExtensionsLoader` never sets it for JSON manifests).
- `Uri` parsing details (`global\types\uri.h`) for the `extensions/dev/` prefix check and the legacy `extension://plugin/v1/...` URIs.
- `api\v1\apiv1.cmake` contents and `filedialog`/`messagedialog`/`qqmlsettings` behaviour (not read).
- `example2`, `example3`, `dev\fetch1` manifests and the `.js`/`.qml` sources other than `quickstart` were not read in full.
- How `MuseApi.Http` (`FetchApi`) is exposed to extensions — it is registered but `ExtApi` has no `http` property; presumably reached via `require("MuseApi.Http")` (HYPOTHESIS).
- Whether `ExtensionsApiDump` enumerates the registered API surface for documentation (name suggests so; not traced).

*Authored from: every file under `muse\framework\extensions\` except `api\v1\{filedialog,messagedialog,qqmlsettings}*`, most QML (read `ExtensionViewer`, `ExtensionPanel`, `extensionbuilder`, `extensionslistmodel`, `extensionstoolbarmodel`), ADR 00102, `src\appshell` glue (`extensioncontextresolver`, `appmenumodel`, `notationpagemodel`, `appshellcommandscontroller`), `src\converter\convertercontroller.cpp`, and `share\extensions\*\manifest.json`. **If this contradicts the live source, the source wins.***
