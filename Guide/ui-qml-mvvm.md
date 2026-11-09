# MuseScore — UI: Qt Quick/QML, MVVM, theming, navigation, docking

> **What this file is.** How the MuseScore 5.0 `main` UI is built: the Qt Quick/QML view layer, the C++ view-model (MVVM) pattern, the per-window `UiEngine` + IoC context, theming, the keyboard-**Navigation** accessibility tree, how C++ opens QML dialogs/pages (`interactive`), the `uicomponents` control library, and the KDDockWidgets docking (`dockwindow`). Authored from the live source in `C:\Dev\+MuseScore+` (cites are relative to it; `muse\…` = framework submodule). **Source is authoritative — if this contradicts the code, the code wins; fix the Guide.** Start at `00-core.md`; DI mechanics are in `architecture-and-ioc.md`; startup/main window in `app-shell-and-startup.md`.

## 0. 60-second orientation

- **QML is the UI; Widgets are legacy.** ADR 00004 (`muse\docs\adr\00004-CoreArch-MVVM.md:18-36`): View = QML under `modulename\qml\Area\ModuleName\*.qml`; ViewModel = Qt `QObject`/`QAbstractListModel` classes (usually suffix `…Model`) with `Q_PROPERTY`/`Q_INVOKABLE`, living **next to the QML** in the same dir; Model = pure-C++ domain services (`i*.h`, `internal\`). Remaining QWidget dialogs (~16 in `src\notationscene\widgets`, `src\palette\widgets`, plus `uicomponents\view\topleveldialog.h`) derive `muse::ui::WidgetDialog` (`muse\framework\ui\view\widgetdialog.h:30-41`) and are themed by a `QProxyStyle` (`WidgetStyle`, `ui\view\widgetstyle.h:36`). Counted `.qml` files under `src\` + `muse\framework\` (excl. thirdparty/tests): **724**, which includes *both* dock trees (24), the `stubs\` trees (~22) and `src\web` (6).
- **Per-window everything.** Each app window = one IoC *context* = **its own `QQmlApplicationEngine`** (`UiEngine`), its own `NavigationController`, `UiState`, `Interactive`, dock provider. (`ioccontext.cpp:55-58`: "each IoC context (window) has its own instance of the Qml Engine, and thus its own root Qml Context".) Root CMake sets `MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE ON` (`CMakeLists.txt:198`) so multiple windows share one process.
- **The five framework pieces that matter** (all under `muse\framework\`): `ui\` (engine, theme, navigation, `Muse.Ui` QML module), `uicomponents\` (`Muse.UiComponents` control library), `interactive\` (open dialogs/pages by URI — *this replaced the old `interactiveprovider` in `ui`*), `dockwindow_v2\` (`Muse.Dock`, the default), `accessibility\` (screen-reader bridge fed by `NavigationControl.accessible`).
- **App-side QML** lives in `src\<module>\qml\MuseScore\<Area>\` (e.g. `src\appshell\qml\MuseScore\AppShell\`, also `propertiespanel` 133 qml, `project` 86, `notationscene` 84, `preferences` 69, `playback` 29, `palette`/`instrumentsscene` 13 each).
- **There is no `registerUiTypes()` in new code.** View-models self-register via `QML_ELEMENT` + `qt_add_qml_module` (§3.3). `registerUiTypes` still exists in `IModuleSetup` but most modules no longer define it (`architecture-and-ioc.md` §1.2).

## 1. The UI stack at a glance

```
src\app  (MuseScoreGuiApp : muse::ui::GuiApplication)
   │ loadMainWindow(ctx): QQmlComponent(":/qt/qml/MuseScore/AppShell/platform/<win|mac|linux>/Main.qml")
   ▼
Main.qml → AppWindow.qml (ApplicationWindow) → WindowContent.qml (DockWindow + InteractiveProvider)
   │                                   ├─ pages: HomePage / NotationPage / PublishPage / DevToolsPage (DockPage)
   │                                   └─ panels/toolbars: DockPanel / DockToolBar (hosting module QML)
   ▼
Each QML view  ⇄  C++ view-model (QML_ELEMENT QObject, ContextInject<IService>)  ⇄  domain services (IoC)
Shared globals in every engine:  ui (UiEngine: ui.theme, ui.tooltip, ui.df)  ·  api (QmlApi: api.launcher)  ·  qsTrc()  ·  ioc_context
```

| Layer | Where | Role |
|---|---|---|
| `Muse.Ui` QML module | `muse\framework\ui\qml\Muse\Ui\` (URI `Muse.Ui` 1.0, `CMakeLists.txt:23-57`) | `NavigationSection/Panel/Control`, `NavigationEvent`, `AccessibleItem`+`MUAccessible`, `WindowsModel`, `ToolTipProvider.qml`, drag (`qmldrag`), `GraphicsTestObject`, and the *foreign* re-exports of the core `ui` classes (§3.3) |
| `ui` core (C++) | `muse\framework\ui\` | `UiEngine`, `UiConfiguration` (themes/fonts), `NavigationController`, `UiState`, `UiActionsRegister`, `GuiApplication`, `MainWindow`, `WindowsController`, `DragController`, `GraphicsApiProvider` |
| `Muse.UiComponents` | `muse\framework\uicomponents\qml\Muse\UiComponents\` (URI `Muse.UiComponents`) | ~80 reusable controls + C++ model/view helpers (§7) |
| `Muse.Interactive` | `muse\framework\interactive\qml\Muse\Interactive\` | `InteractiveProvider.qml`, standard dialogs, file/folder dialogs (§6) |
| `Muse.Dock` | `muse\framework\dockwindow_v2\qml\Muse\Dock\` | `DockWindow`, `DockPage`, `DockPanel`, `DockToolBar`, … (§8) |

## 2. UiEngine + the per-window QML context (IoC)

### 2.1 Who creates what (`muse\framework\ui\uimodule.cpp`)

`UiModule` (global) registers `IUiConfiguration`, `IPlatformTheme` (Win/Mac/Linux/stub chosen with `#ifdef`, `:81-99`) and the *global* `ThemeApi` singleton (`:85`). `UiModuleContext` (per window) registers the contextual services (`:164-192`):

```cpp
// UiModuleContext::registerExports  uimodule.cpp:164-192
m_uistate = std::make_shared<UiState>(iocContext());
m_uiengine = std::make_shared<UiEngine>(iocContext());          // a NEW engine per window
m_keyNavigationController = std::make_shared<NavigationController>(iocContext());
ioc()->registerExport<IUiEngine>(module_name, m_uiengine);
ioc()->registerExport<INavigationController>(module_name, m_keyNavigationController);
ioc()->registerExport<IMainWindow>(module_name, new MainWindow()); // + IUiState, IDragController, IWindowsController, IUiContextConfiguration, IUiActionsRegister
```

Phase order for the context (`uimodule.cpp:208-242`): `onInit` → `NavigationController::init()` (GUI only); `onAllInited` → `UiState::init()` (reads workspace data so workspaces must be up), `UiActionsRegister::init()` (collects actions from *all* modules), then `m_uiengine->setTheme(<MuseApi.Theme singleton>)` and **`m_uiengine->init()`**.

### 2.2 What `UiEngine::init()` puts in the engine (`ui\internal\uiengine.cpp:53-84`)

```cpp
QmlIoCContext* qmlIoc = new QmlIoCContext(this);  qmlIoc->ctx = iocContext();
rootContext->setContextProperty("ioc_context", QVariant::fromValue(qmlIoc));  // :55-60  ← how view-models find their IoC context
rootContext->setContextProperty("ui",  this);     // UiEngine  — ui.theme, ui.tooltip, ui.df, ui.rootItem, ui.blendColors(), …
rootContext->setContextProperty("api", m_api);    // QmlApi    — api.launcher.open()/openSync()/openApp()/openUrl()
m_engine->globalObject().setProperty("qsTrc", translateFn); // :65-67  → muse::qtrc(context,text,disambiguation,n)
m_engine->addImportPath(":/qml");                  // + "<appdir>/../qml" on Windows (:72-77); source paths if MUSE_LOAD_QML_FROM_SOURCE
```

`UiEngine` itself is `QML_ELEMENT`/`QML_UNCREATABLE` with `Q_PROPERTY(theme, tooltip, df, rootItem, isEffectsAllowed, isSystemDragSupported)` (`uiengine.h:45-57`). `isEffectsAllowed` is false under the Software graphics API (`uiengine.cpp:120-126`) — QML guards GraphicalEffects with it. `rootItem` is set by `AppWindow.qml` (`ui.rootItem = root.contentItem`).

### 2.3 Main window load + context attach (`ui\internal\guiapplication.cpp:155-225`)

`GuiApplication::loadMainWindow(ctxId)` resolves that context's `IUiEngine`, takes `qmlAppEngine()`, builds a `QQmlComponent` from `mainWindowQmlPath(platform)` (the app returns `:/qt/qml/MuseScore/AppShell/platform/%1/Main.qml`, `src\app\internal\guiapp.cpp:98-101`), wraps it in a fresh `QQmlContext` that also carries `ioc_context` (`:188-194`), creates it and `setVisible(true)` *before* KDDockWidgets reads sizes (`:201-204`, issue 21148). `doDestroyContext` hides+deletes the window, then `IUiEngine::quit()` (`:211-225`).

### 2.4 How a QML-created object finds its IoC context

```cpp
// muse\framework\global\modularity\ioccontext.cpp:32-67
Contextable::GetContext iocCtxForQmlObject(const QObject* o)   // returns a *lazy* lambda
{ return [o]() { …walk parents until qmlContext(p) is non-null…; return iocCtxForQmlContext(ctx); }; }
ContextPtr iocCtxForQmlContext(const QQmlContext* ctx)
{ rootContext = ctx->engine()->rootContext();                   // per-window engine ⇒ its root ctx
  return rootContext->contextProperty("ioc_context").value<QmlIoCContext*>()->ctx; }
```

`Contextable(const GetContext&)` stores the lambda and evaluates it on the first `iocContext()` call (`kors_modularity\modularity\context.h:71-84`), so `ContextInject<I> x = { this }` members resolve *after* the object has a QQmlContext. QWidgets use the `ioc_context` int *property* on the widget chain instead (`iocCtxForQWidget`, `:69-89`). → `architecture-and-ioc.md` §3.4.

## 3. The MVVM pattern — concrete walk-through

### 3.1 The view-model (C++) — `src\appshell\qml\MuseScore\AppShell\aboutmodel.h/.cpp`

```cpp
// aboutmodel.h:37-48
class AboutModel : public QObject, public muse::Contextable
{
    Q_OBJECT
    QML_ELEMENT                                                   // ← auto-registers in the QML module URI
    muse::GlobalInject<IAppShellConfiguration> configuration;     // global service
    muse::GlobalInject<muse::IApplication> application;
public:
    explicit AboutModel(QObject* parent = nullptr);
    Q_INVOKABLE QString museScoreVersion() const;                 // view calls these
    Q_INVOKABLE void toggleDevMode();
};
// aboutmodel.cpp:36-39
AboutModel::AboutModel(QObject* parent) : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}
```

A stateful model adds `Q_PROPERTY(... READ ... NOTIFY ...)` and subscribes to service notifications — `NotationPageModel` (`notationpagemodel.h:40-56`, `.cpp:61-100`): `Q_PROPERTY(bool isNavigatorVisible READ … NOTIFY isNavigatorVisibleChanged)`, `ContextInject<…> x = { this }` members, an `init()` (`Q_INVOKABLE`, called from QML `Component.onCompleted`) that wires `…Changed().onNotify(this, …)` (the `Asyncable` pattern) to `emit …Changed()`.

### 3.2 The view (QML) — `AboutDialog.qml:31-48`

```qml
import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell                     // ← the qt_add_qml_module URI that exported AboutModel
StyledDialogView {
    title: qsTrc("appshell/about", "About MuseScore Studio")
    AboutModel { id: aboutModel }              // engine instantiates the view-model; ctor runs iocCtxForQmlObject
    StyledTextLabel { text: qsTrc("appshell/about", "Version: %1").arg(aboutModel.museScoreVersion())
                      font: ui.theme.bodyBoldFont }                // global `ui` context property
    FlatButton { icon: IconCode.COPY; onClicked: aboutModel.copyRevisionToClipboard() }
}
```

**Lifecycle rule of thumb:** QML declares the model → C++ ctor (no injected-service use yet) → properties bound → `Component.onCompleted: model.init()/load()` → model subscribes to services and emits `…Changed` → bindings re-evaluate. List-shaped data = `QAbstractListModel` subclasses (`AbstractMenuModel`, `AbstractToolBarModel`, filter/sort proxies, §7). The dialog itself is opened by URI, not by QML `Qt.createComponent` (§6).

### 3.3 Registration: `qt_add_qml_module` + `QML_ELEMENT`

- App module QML target — `src\appshell\qml\MuseScore\AppShell\CMakeLists.txt:21-114`: `muse_create_qml_module(appshell_qml FOR appshell)` then `qt_add_qml_module(appshell_qml URI MuseScore.AppShell VERSION 1.0 SOURCES <viewmodels .cpp/.h> QML_FILES <*.qml> RESOURCES <…> IMPORTS TARGET muse_ui_qml TARGET muse_uicomponents_qml … )` (≈22 `IMPORTS TARGET` deps). Add a new view-model = add `.cpp/.h` to `SOURCES`; add a new view = add to `QML_FILES` (**forgetting this leaves it out of the resource tree**).
- `muse_create_qml_module` (`muse\framework\cmake\MuseCreateModule.cmake:135-153`): wraps `muse_create_module`, `target_link_libraries(<t> INTERFACE <t>plugin)` (the auto-generated plugin target, so linking the module pulls the static QML plugin), and with `FOR <module>` adds the parent's include dir + link + a QtCreator import path. `fixup_qml_module_dependencies` (`:209-216`) is a Visual-Studio-generator workaround (`<t>_qmltyperegistration` dependency).
- Resulting resource path: `:/qt/qml/<URI with / >/File.qml` (e.g. `qrc:/qt/qml/Muse/Dock/DockTitleBar.qml`, `dockwindow_v2\dockmodule.cpp:107`).
- **The core `ui` module cannot register its own QML types** (it is below `ui_qml`), so `muse\framework\ui\qml\Muse\Ui\foreign.h:47-97` re-exports them with `QML_FOREIGN` + `QML_NAMED_ELEMENT`: `UiEngine`, `ThemeApi` (as **`QmlTheme`**), `QmlToolTip`, `QmlDataFormatter` (as `DataFormatter`), `MainWindowBridge` (macOS: `MacOSMainWindowBridge`), `IconCode`, `MusicalSymbolCodes`. (A `// TODO` there admits the layering smell, `foreign.h:43`.)
- **Legacy / runtime registration** still used by the *API register*: `ApiRegister::regApiCreator` → `qmlRegisterSingletonType("MuseInternal.Navigation", …)` etc. (`global\api\internal\apiregister.cpp:52-90`); `IconCode` is also `qmlRegisterUncreatableMetaObject`'d into `MuseApi.Controls` (`uimodule.cpp:119`) for extensions.
- **The `api.*` object**: in the main window `api` is `QmlApi` (`ui\view\qmlapi.h:30-44`) — just `launcher` (`QmlLauncher`: `open/openSync/openApp/openUrl`, `qmllauncher.cpp:31-51`, forwards to `IInteractive`/`IPlatformInteractive`). Do not confuse with the **extensions/plugin `api`** (`muse\framework\extensions\internal\extensionsuiengine.cpp`, `MuseApi.*` modules, `@APIDOC` doc blocks in `FlatButton.qml:30-46`) → `extensions-system.md`. `MuseInternal.Navigation`/`MuseInternal.Keyboard` (`ui\api\navigationapi.h`, `keyboardapi.h`) expose `nextPanel/goToControl/triggerControl/dump` and `key/pressKey/text` to **testflow** scripts.

## 4. Theming

- **Data model** (`ui\uitypes.h`): four built-in theme codes `light`, `dark`, `high_contrast_white`, `high_contrast_black` (`:54-67`); `enum ThemeStyleKey` (`:81-117`: 19 colors — `BACKGROUND_PRIMARY…QUARTERNARY`, `POPUP_BACKGROUND`, `PROJECT_TAB`, `TEXT_FIELD`, `ACCENT`, `STROKE`, `STROKE_SECONDARY`, `BUTTON`, `FONT_PRIMARY/SECONDARY`, `LINK`, `FOCUS`, `WHITE/BLACK`, `PLAY`, `RECORD` — then `BORDER_WIDTH`, `NAVIGATION_CONTROL_BORDER_WIDTH`, accent/button opacity normal/hover/hit, `ITEM_OPACITY_DISABLED`); `struct ThemeInfo {codeKey,title,values,extra}`; `FontSizeType {BODY,BODY_LARGE,TAB,HEADER,TITLE}`; `IconSizeType {Regular,Toolbar}`.
- **Source of values**: per-theme `.cfg` files compiled in as `:/configs/<code>.cfg` (app copies at `src\app\configs\dark.cfg` etc.), parsed by `ConfigReader` in `UiConfiguration::makeStandardTheme` (`ui\internal\uiconfiguration.cpp:285-350`); every raw key is also copied into `ThemeInfo.extra` (exposed to QML as `ui.theme.extra`). **Hot reload**: in *unstable* builds, if `<appDataPath>/<code>.cfg` exists it is used instead and watched (`:290-297`, `appUnstable()`); stable builds always use the baked-in file. User tweaks are a JSON array in setting `ui/application/themes` overlaid by `updateThemes` (`:199-226`, keys `:55-65`).
- **Settings** (`ui\internal\uiconfiguration.cpp:55-65,89-152`): current theme (default `light`), follow-system-theme (+`IPlatformTheme` listening), font family/size (`correctUserFontIfNeeded` falls back if family missing, `:174-183`), icons font `MusescoreIcon` (loaded from `:/ui/data/MusescoreIcon.ttf`, `uimodule.cpp:135`), musical font `Leland` 24, musical text font `Leland Text`. `flickableMaxVelocity`=4000, `tooltipDelay`=500 ms (`:67-69`). `IUiConfiguration` is the interface (`iuiconfiguration.h:41-110`).
- **QML surface — `ThemeApi`** (`ui\api\themeapi.h:39-254`, QML name `QmlTheme`): `ui.theme.backgroundPrimaryColor … fontPrimaryColor/accentColor/strokeColor/buttonColor/focusColor`, opacities (`buttonOpacityNormal`, `accentOpacityHover`, `itemOpacityDisabled`…), `borderWidth`, `navCtrlBorderWidth`, `defaultButtonSize` (= max(body font height, icon font height)+8, min 30 — `themeapi.cpp:377-387`), fonts `bodyFont/bodyBoldFont/largeBodyFont/…/headerFont/titleBoldFont/tabFont`, `iconsFont/toolbarIconsFont/musicalFont/musicalTextFont`. Everything `NOTIFY themeChanged`, re-emitted from `ThemeApi::update()` on any configuration change (`:42-55,88-93`), so QML bindings (`color: ui.theme.backgroundPrimaryColor`) retheme live. "Bold" = `QFont::DemiBold` (`:332-342`).
- **One shared instance**: `ThemeApi` is created once in the global module with a null engine (`uimodule.cpp:85`), registered as singleton `MuseApi.Theme` (`:117`), and handed to each `UiEngine` via `setTheme` (`:231-239`) — so all windows share it. Extension QML sees it as `MuseApi.Theme`.
- **Widgets** follow the same palette through `WidgetStyle` (`QProxyStyle`, built in `UiModule::onAllInited` `:146-149`, owned by `QApplication`); `IUiConfiguration::applyPlatformStyle(QWindow*)` handles title-bar chrome per OS (`WindowsPlatformTheme` etc.).
- Handy helpers on `ui`: `colorWithAlphaF`, `blendColors` (`uiengine.cpp:176-191`), `allTextFonts()` (filters `nonTextFonts`, `:193-202`), `keyboardModifiers()`, `currentLanguageLayoutDirection()`.

## 5. The Navigation (keyboard accessibility) system

A **signature MuseScore subsystem**: a 3-level focus tree that is *independent of Qt's own focus*, driven by *commands* (not key events in QML), consumed by `NavigationFocusBorder` and by the screen-reader bridge.

### 5.1 Model — Section ▸ Panel ▸ Control (`ui\inavigation.h:43-173`)

| Level | C++ interface / QML type | Meaning | Extra |
|---|---|---|---|
| **Section** | `INavigationSection` / `NavigationSection` | a top-level region (toolbar, panel area, a dialog) — **registered with the controller** | `type`: `Regular`, `Exclusive` (dialog: Tab/F6 stay inside), `Ignore` (not registered; used for diagnostics) (`:156-164`) |
| **Panel** | `INavigationPanel` / `NavigationPanel` | a group of controls inside a section | `direction`: `Horizontal`, `Vertical`, `Both` (arrow-key wrap rules) (`:133-137`) |
| **Control** | `INavigationControl` / `NavigationControl` | a focusable thing (button, field…) | `trigger()`, `requestActive(enableHighlight)` |

Common to all (`INavigation`): `name`, `index{column,row}` (`order` ≡ `column`), `enabled`, `active`, `onEvent(EventPtr)`, `window()`, `visualItem()`. `Event::Type` = `Left/Right/Up/Down/Trigger/Escape` + internal `AboutActive` (`:51-61`; "sync with `view/NavigationEvent::Type`"). QML view classes: `AbstractNavigation` (`ui\qml\Muse\Ui\abstractnavigation.h:38-136`, `QQmlParserStatus`, `Contextable(iocCtxForQmlObject(this))`, `:34-37`) → `NavigationSection/Panel/Control`; they hold an `accessible` (`AccessibleItem`) and raise `navigationEvent(NavigationEvent*)` to QML via `AbstractNavigation::onEvent` (`:232-236`). `visualItem()` = nearest `QQuickItem` ancestor (`:244-257`) — **a navigation object must be parented inside an Item to know its window**.

### 5.2 Wiring in QML — real excerpts

```qml
// FlatButton.qml:149-166  (every control: one NavigationControl, placed *by the user* into a panel/section via navigation.panel / .order)
NavigationControl { id: navCtrl
    name: root.objectName !== "" ? root.objectName : "FlatButton"
    enabled: root.enabled && root.visible
    accessible.role: MUAccessible.Button   accessible.name: …   accessible.visualItem: root
    onTriggered: { if (navCtrl.enabled && root.isClickOnKeyNavTriggered) { root.doClicked(null); navCtrl.notifyAboutControlWasTriggered() } } }
NavigationFocusBorder { navigationCtrl: navCtrl }   // NavigationFocusBorder.qml:27-51 — visible: navigationCtrl.highlight, border = ui.theme.navCtrlBorderWidth

// ButtonBox.qml:45-57  (panel decides which control gets focus when the panel activates)
NavigationPanel { name: "ButtonBox"; direction: NavigationPanel.Horizontal
    onNavigationEvent: function(event) { if (event.type === NavigationEvent.AboutActive)
        event.setData("controlIndex", [ btn.navigation.row, btn.navigation.column ]) } }

// StyledDialogView.qml:74-92  (dialogs create an Exclusive section; Escape closes)
NavigationSection { name: "StyledDialogView"; type: NavigationSection.Exclusive; enabled: root.isOpened; order: 1
    onNavigationEvent: function(event) { if (event.type === NavigationEvent.Escape && root.closeOnEscape) root.close() } }
```

Registration rules (`navigationsection.cpp:53-70`): a section **must have a non-empty `name` and `order > -1`**, else `IF_ASSERT_FAILED` returns *without registering* (silent in release); `type: Ignore` skips registration; registration happens in `componentComplete()` ("Reg after set properties"). Panels/controls attach themselves via `section:` / `panel:` property setters (`navigationpanel.cpp:186-206`, `navigationcontrol.cpp:146-168`). `enabled` is **derived upward**: a panel is enabled only if ≥1 of its controls is; a section only if ≥1 panel is (`navigationpanel.cpp:77-91`, `navigationsection.cpp:92-107`).

### 5.3 The controller (`ui\internal\navigationcontroller.cpp`)

`INavigationController` (`inavigationcontroller.h:30-67`) is a **context** service: `reg/unreg(section)`, `requestActivateByName/ByIndex`, `activeSection/Panel/Control`, `setDefaultNavigationControl`, `setPrioritySection`, `isHighlight` (focus ring visible), `navigationChanged`, `dump()`.

- **Input = commands.** 14 `command://navigation/*` commands (`navigationcommands.h:27-47`: `escape, next/prev-section, next/prev-panel, next/prev-tab (≡ panel), left/right/up/down, first/last-control, nextrow/prevrow-control, trigger-control`) registered in `NavigationCommandsRegister` (`internal\navigationcommandsregister.cpp:30-…`, `Availability::All`) and handled via `ICommandDispatcher::onRequest` in `NavigationController::init()` (`:282-304`). Legacy `nav-*` **action codes** are forwarded to the same commands for compatibility (`:306-331`; `NavigationUiActions` declares them, `navigationuiactions.cpp:38-…`). Default key bindings are in `src\app\configs\data\shortcuts.json:15-28`: `Esc`; `F6`/`` ` `` next section; `Shift+F6`/`~` prev section; `Tab`/`Ctrl+Tab` next panel; `Shift+Tab` prev; arrows; `Home`/`End`; `PgDown`/`PgUp`; `Return/Enter/Space` trigger (autorepeat off). → shortcuts module (`framework-services.md`; root sets `MUSE_MODULE_SHORTCUTS_V2 ON`, `CMakeLists.txt:199`).
- **Algorithm.** Pure index geometry: `findNearestEnabled<T>(set, currentIndex, direction)` scans *all* enabled siblings in a `std::set<T*>` (pointer-ordered, so order comes from `index`, not insertion) — Right/Left match same `row` and nearest `column`; Down/Up match same `column` and nearest `row` (`:65-169`). `goToControl` (`:1230-1381`) adds wrap rules: a `Horizontal` panel wraps Right→first/Left→last and (for non-`Both`) falls through to the other axis ("tryOtherOrientation"); `Vertical` likewise.
- **Event bubbling.** `onRight()` etc. send an `Event` to the **panel first, then the active control**; only if neither sets `accepted` does the controller move focus (`:998-1021`). `onEscape` bubbles section → panel → control, then deactivates the control and returns to `m_defaultNavigationControl` (`:1098-1150`). `doTriggerControl` bubbles panel → control → `ctrl->trigger()` (`:1383-1410`).
- **Activation.** `doActivateSection` deactivates its panels, activates first (or last) enabled panel (`:507-535`); `doActivatePanel` sends `AboutActive` to the panel, which may answer with `controlName` or `controlIndex[row,col]` (+`controlOptional`) in `event.data`, else the first enabled control (`:553-602`). `onActiveRequested(sect,panel,ctrl)` (`:1471-1540`) is what `requestActive()` from QML ends up in; a **mouse-originated** request for a control is ignored unless the section is `Exclusive` (`reg` lambda `:341-347`) so clicks don't steal the keyboard focus ring.
- **Section cycling.** `goToNextSection/PrevSection` (`:808-900`): an `Exclusive` section traps (only cycles its panels); a `prioritySection` (set via `NavigationSection.requestPriority()`) jumps first; wrap-around at ends. Last active control is remembered/restored (`saveLastActiveControl/restoreLastActiveControl`, `:757-801`).
- **Highlight.** `isHighlight` flips true on any navigate command (`navigateTo`, `:472`) and false on mouse press (`eventFilter` on `qApp`, `:419-426`; `resetIfNeed` `:399-417`, may reset to the default control). `AbstractNavigation::highlight` re-emits `highlightChanged` (`abstractnavigation.cpp:61-63`); `NavigationFocusBorder` shows only while `highlight` is true.
- **Dev tooling.** `muse://devtools/keynav/controls` (`:57`, action `nav-dev-show-controls`), `NavigationController::dump()` (`:1548`), the `KeyNav` DevTools pages (`src\appshell\qml\…\DevTools\KeyNav\`), diagnostics module panels (`DiagnosticNavigationPanel.qml`).
- **Accessibility link.** `NavigationControl::setActive` sets `IAccessible::State::Focused` on its `AccessibleItem`; `NavigationPanel::setActive` sets `Active`; `setPanel` re-parents the control's accessible under the panel's (`navigationcontrol.cpp:89-95,160`). `AccessibleItem` (`ui\qml\Muse\Ui\qmlaccessible.h:77-…`) implements `accessibility::IAccessible` with role (`MUAccessible::Role`, 25 roles), name/description, value/text interfaces → `accessibility\` module.
- **Tests.** `muse\framework\ui\tests\navigationcontroller_tests.cpp` (gtest with mock sections; exercises `nav-next-section` flows).

## 6. Opening dialogs and pages from C++ — `interactive` (not `ui`)

> The old "interactiveprovider" lives in **`muse\framework\interactive\`** now. `ui` only keeps `QmlLauncher`.

- **API** — `IInteractive` (`interactive\iinteractive.h:38-310`, context interface): `question/info/warning/error(...)` → `async::Promise<Result>`; `selectOpeningFile/SavingFile/Directory/Color`; **`open(UriQuery)` → `Promise<Val>`**, `openSync` (nested `QEventLoop`, only if `MUSE_MODULE_INTERACTIVE_SYNC_SUPPORTED`, `interactive.cpp:669-723`; primary pages refuse sync), `isOpened`, `raise`, `close*`, `currentUri/stack/topWindow`. `…Sync` question variants are marked "please don't use".
- **URI registry** — `IInteractiveUriRegister` (global; `iinteractiveuriregister.h:38-72`): `registerPageUri(uri)` (a dock page), `registerQmlUri(uri, qmlModule, qmlFile)` (a QML dialog), `registerWidgetUri<T>(uri)` (a `WidgetDialog`, static_assert-checked). Modules do this in `resolveImports()`: `appshellmodule.cpp:75-85` (`musescore://home|notation|publish…` pages; `musescore://about/musescore` → `("MuseScore.AppShell","AboutDialog")`), `interactivemodule.cpp:70-83` (`muse://interactive/standard|error|progress|selectfile|selectdir`), `uicomponentsmodule.cpp:41`.
- **Call site** — C++: `interactive()->open("musescore://about/musescore")` (`appshellcommandscontroller.cpp:368`; URI query params become `QVariantMap` initial properties, e.g. `…/sample?color=#0F9D58`, `interactive.cpp:740-749`). QML: `api.launcher.open(uri)` / `openSync`.
- **Mechanism** (`interactive.cpp:751-794`, `1154-1298`): `openFunc` records `m_openingObject{query,resolve,reject}` (**single-flight** — asserts if a previous open hasn't reached `onOpen`, `:754`), looks up `ContainerMeta` and calls `openWidgetDialog` or `openQml`. `openQml` fills a `QmlLaunchData` and fires `m_openRequested` → `InteractiveProviderModel` → signal to **`InteractiveProvider.qml`** (declared in `WindowContent.qml:49-56` with `topParent: root`), whose `onOpenRequested` (`InteractiveProvider.qml:52-82`): `PrimaryPage` → `root.requestedDockPage(uri, params)` → `DockWindow.loadPage`; `QmlDialog` → `Qt.createComponent(module, file)`, `createObject(topWindow, params)`, hooks `opened`/`closed` back to `onOpen`/`onClose` and `show()`s it (`:100-131`). The model resolves the IoC ctx with `iocCtxForQmlObject` (`interactiveprovidermodel.cpp:27-30`). `onOpen` (`interactive.cpp:1246-1298`) maintains the dialog **stack** (index 0 = current primary page), emits `currentUriChanged`/`opened`, and resolves the promise (pages immediately; dialogs on close with the `ret` map). Widget dialogs get `ioc_context` set as a property (`:1172`).
- **Dialog base** — QML dialogs derive `StyledDialogView` → `DialogView` (`uicomponents\qml\…\dialogview.h:36-91`, props `title/modal/frameless/resizable/ret`, `show/hide/accept/reject`) → `WindowView` (a `QQuickView` wrapper). `StyledDialogView` auto-adds an Exclusive nav section and requests activation on `opened` (`StyledDialogView.qml:47-51,74-92`). Popups: `PopupView` (`popupview.h:47-…`, `anchorItem`, `placementPolicies`, `closePolicies`, arrow).

## 7. `uicomponents` — the control library (categories, not a catalog)

Two parts, one QML module `Muse.UiComponents` (`uicomponents\qml\Muse\UiComponents\CMakeLists.txt:21-…`; module class itself is nearly empty — it only registers the `selectmultipledirectories` URI, `uicomponentsmodule.cpp:37-43`):

- **QML controls (~80 `.qml`)** — all themed from `ui.theme` and keyboard-navigable via `NavigationControl`: *buttons* (`FlatButton`, `FlatToggleButton`, `FlatRadioButton(List)`, `MenuButton`, `PopupButton`, `ProgressButton`, `ToggleButton`, `CheckBox`, `RoundedRadioButton`), *text/labels* (`StyledTextLabel`, `StyledIconLabel`, `TextInputField`, `TextInputArea`, `SearchField`, `NumberInputField`, `RealInputField`, `TimeInputField`, `IncrementalPropertyControl`, `ValueAdjustControl`), *lists/trees/tables* (`StyledListView`, `StyledGridView`, `StyledTableView`, `StyledTreeView`, `ValueList`, `GridViewSectional`, `LegacyTreeView\`), *containers/scroll* (`StyledFlickable`, `StyledScrollBar`, `StyledGroupBox`, `ExpandableBlank`, `InfoPanel`, `StyledTabBar/Button`, `PageTabButton`, `SeparatorLine`), *dialogs/popups/menus* (`StyledDialogView`, `StyledPopupView`, `StyledPopupLoader`, `StyledMenuLoader`, `ContextMenuLoader`, `StyledDropdown`, `ButtonBox`, `PopupPanel`, `StyledToolTip`), *pickers* (`ColorPicker`, `FilePicker`, `FontDropdown`, `SelectMultipleDirectoriesDialog`), *toolbars* (`StyledToolBarView/Item`), *misc* (`StyledSlider`, `ProgressBar`, `StyledBusyIndicator`, `PageIndicator`, `StyledRectangularShadow`, `Border`, `Utils.mjs`); plus `NavigationFocusBorder`, `FocusableControl/Item`. A sibling `Muse.GraphicalEffects` module (`Effect*.qml`, shaders) wraps Qt effects (gate with `ui.isEffectsAllowed`). `MuseApi.Controls` (`uicomponents\api\MuseApi\Controls\`, `ExtensionBlank.qml`) is the whitelisted subset for extensions (see `@APIDOC` blocks).
- **C++ helpers** (same dir; all `QML_ELEMENT`): *window views* `WindowView`/`DialogView`/`PopupView`/`MenuView`/`DropdownView`; *menus/toolbars* `AbstractMenuModel` (a `QAbstractListModel` of `MenuItem` built from **commands** (`rcommand` registers/state/dispatcher) and, if `MUSE_MODULE_ACTIONS_SUPPORT`, legacy `UiAction`s; `makeMenu/makeMenuItem/findItem`, `handleMenuItem`, `abstractmenumodel.h:52-131`), `AbstractToolBarModel`, `ToolBarItem`; *list plumbing* `SortFilterProxyModel` + `Filter/Sorter(Value)`, `FuzzyFilter/FuzzyScoreSorter`, `SectionProxyModel`, `SelectableItemListModel`, `ItemMultiSelectionModel`, `FilteredFlyoutModel`, `AbstractTableViewModel` + `TableSortFilterProxyModel`; *dialog models* `ButtonBoxModel`, `ColorPickerModel`, `FilePickerModel`, `SelectMultipleDirectoriesModel`, `ShortcutOverrideModel(_v2)`; *painting* `QuickPaintedView`, `IconView`, `PolylinePlot`, `WidgetView`; `QmlListProperty<T>` helper; `validators\` (int/double input). Tests are C++ only (`UiComponents\tests`: validators, section/sort-filter proxies).

## 8. Docking — `dockwindow_v2` (default) vs `dockwindow` (legacy)

- **Which is built.** Two dirs with the **same module/QML URI (`Muse.Dock`) and same public headers**: `dockwindow\` (vendored `thirdparty\KDDockWidgets`) and `dockwindow_v2\` (KDDockWidgets **v2** from `muse_deps\recipes\kddockwidgets`, the MuseScore fork pinned at commit `662d74a6…`, QtQuick frontend only, `meta.cmake:3-17`, `spec.cmake:4`). `muse\framework\CMakeLists.txt:44-50` picks `dockwindow_v2` iff `MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2`. **Root `CMakeLists.txt:86` does `option(... ON)` *before* `include(MuseDeclareOptions)` whose own default is OFF (`MuseDeclareOptions.cmake:76`) — the first `option()` wins ⇒ the MuseScore app builds v2.** (A standalone Muse build defaults to v1.) TRACED from CMake; no build cache was inspected. Diff-verified: `dockmodule.h`, `dockcommands.h`, `idockwindow.h` are byte-identical between the dirs, which is why `appfactory.cpp:117` including `framework/dockwindow/dockmodule.h` and `notationpagemodel.cpp:38-39` mixing `dockwindow/idockwindow.h` + `dockwindow_v2/dockcommands.h` still link.
- **Architecture (v2, `dockwindow_v2\`).** `DockModule` (global) registers `DockCommandsRegister` (`dockmodule.cpp:149-155`); `DockContext` (per window) exports `IDockWindowProvider` and, in `onInit`, configures KDDW **per IoC context id**: `ContextData::context(ctx)`, `Config::self(ctx).setViewFactory(new DockWidgetFactory)`, `setCtxForEngine(engine, ctx)`, `QtQuick::Platform::instance()->setQmlEngine(engine)` (`:168-221`) — the factory maps KDDW's title bar/tab bar/separator/floating window/group onto MuseScore's own QML (`DockTitleBar.qml`, `DockFrame.qml`, … `:66-132`) and C++ (`DockTitleBar`, `DockTabBar`, `DockSeparator`, `DropController`). `onDeinit` destroys the context data (`:223-229`).
- **QML types** (`Muse.Dock`): `DockWindow` (`QQuickItem`; `pages`, `toolBars`, `loadPage/openPage`, `pageLoaded` signal; injects `IUiState/IInteractive/IWorkspaceManager/IDockWindowProvider`, saves/restores window geometry + page layout through `IUiState::windowGeometry/pageState`, `dockwindow_v2\qml\Muse\Dock\dockwindow.h:52-159`), `DockPage` (a page = central view + panels + toolbars + status bar), `DockPanel`, `DockToolBar`, `DockStatusBar`, `DockCentral`, `DockingHolder`, `DockFloatingWindow`. `IDockWindow` (`idockwindow.h:34-52`): `isDockOpen/setDockOpen/toggleDock/isDockFloating/restoreDefaultLayout/currentPage`.
- **Usage** — `WindowContent.qml:39-119` (`DockWindow { toolBars: [DockToolBar { MainToolBar {…} }]; pages: [HomePage, NotationPage, NotationReviewPage, PublishPage, DevToolsPage] }`); `NotationPage.qml` is a `DockPage` with ~10 `DockPanel`s (palettes, layout, properties, selection filter, undo history, mixer, piano keyboard, timeline, percussion) and several `DockToolBar`s, each panel registering its own `NavigationSection`/`contentNavigationPanelOrderStart`. Docks are addressed by **name constants** exposed through the page's view-model (`NotationPageModel::mixerPanelName()`…, `NOTATION_TOOLBAR_NAME` etc.) and toggled by `dockwindow` commands (`DockCommandsRegister/State`, `DockWindowActionsController`).

## 9. Cross-subsystem dependencies

- **Leans on:** `architecture-and-ioc.md` (contexts, `ContextInject/GlobalInject`, `IModuleSetup`, module/CMake wiring); `framework-services.md` (`rcommand` commands + legacy `actions`, `shortcuts(_v2)`, `workspace` for `UiState`, `languages` for `qsTrc`, `multiwindows`); `accessibility` module (screen reader bridge); `app-shell-and-startup.md` (`GuiApplication` subclass, startup scenario, which pages are registered).
- **Feeds:** every module with a `qml\` dir (`notation-layer.md` → `src\notationscene\qml`, `notationscene` view items; `playback-and-audio.md` → `src\playback\qml` mixer/toolbars; `plugin-api.md`/`extensions-system.md` → `MuseApi.Controls`/`MuseApi.Theme`/`api.interactive`); `testing-and-vtest.md` (`MuseInternal.Navigation/Keyboard` + `testflow`).

## Non-obvious gotchas

- ⛔ **Don't touch a `ContextInject` member in a view-model constructor.** `iocCtxForQmlObject` resolves lazily from the object's `QQmlContext`; before QML attaches it, it asserts and returns a null ctx (`ioccontext.cpp:42-45`). Do work in `init()/load()` called from `Component.onCompleted`.
- ⭐ **`ui`, `api`, `qsTrc`, `ioc_context` are engine/root *context properties*** (`uiengine.cpp:55-67`), invisible to `qmllint`/QtCreator's static analysis; and only exist in engines created by `UiEngine` (the plugin/extension engine has its own `api`).
- ⭐ **One engine per window; the `ThemeApi` is shared** across windows (global singleton). Per-window state must live in context services, not statics.
- ⛔ **A `NavigationSection` with empty `name` or `order < 0` silently never registers** (release builds skip the assert, `navigationsection.cpp:56-62`) — the keyboard "can't reach" that area. Also, a panel with zero enabled controls disables its section (derived `enabled`).
- ⭐ **Navigation uses mouse-vs-keyboard rule:** `requestActive` triggered by mouse on a control is dropped unless the section is `Exclusive` (`navigationcontroller.cpp:343-347`); use `requestActiveByInteraction` in controls that should only highlight on keyboard.
- ⭐ **Interactive `open()` is single-flight** (`interactive.cpp:754`): opening a second dialog before the first reached `onOpen` logs an error and clobbers `m_openingObject`. `openSync` spins a nested event loop and refuses primary pages (`:709-712`).
- ⭐ **New QML file/view-model must be added to the module's `qt_add_qml_module` list** (`QML_FILES` / `SOURCES`); a `QML_ELEMENT` class in an unlisted file is not compiled into the module.
- ⭐ **Core `ui` types reach QML only through `foreign.h`** (`QML_FOREIGN`); adding a new core `ui` QObject for QML means adding a `*Foreign` struct there (e.g. `ThemeApi` is `QmlTheme` in QML).
- ⭐ **Dock v1 vs v2 share names and headers** — grep results are doubled; edit `dockwindow_v2\` for the shipping app (see §8). Includes of `dockwindow/…` compile fine against v2 only because the headers are identical; if they ever diverge, that will break silently at ODR level.
- ⭐ **Theme hot-reload is unstable-builds only** (`uiconfiguration.cpp:293`); editing `src\app\configs\*.cfg` needs a rebuild in stable/release config.
- ⭐ **Commands vs actions coexist** in menus/nav (`MUSE_MODULE_ACTIONS_SUPPORT ON` default, `MuseDeclareOptions.cmake:41`; ADR 00101/00104). Navigation is already command-first with `nav-*` action forwarding (`navigationcontroller.cpp:306-331`).
- Observation (not a verified bug): `findNearestEnabled` `MoveDirection::First/Last` requires **both** row and column to be ≤/≥ the running best (`navigationcontroller.cpp:75-100`), so for 2-D panels "first" is not a strict lexicographic minimum; irrelevant for 1-D lists/sections where one axis is constant.

## UNCLEAR — needs verification

- **QML testing strategy.** Only C++ tests found (`ui\tests\navigationcontroller_tests.cpp`, `themeconverter_tests.cpp`, `Muse\Ui\tests\qmltooltip_tests.cpp`, `UiComponents\tests`); the `testflow` module (`muse\framework\testflow`, `MuseInternal.Navigation/Keyboard` APIs) appears to drive UI scripts but was not read end to end. Not yet traced: how `vtest`/`utest` build configs interact with `MUSE_MODULE_UI_QML`.
- **Multi-window navigation:** each context's `NavigationController` installs an event filter on the shared `qApp` (`navigationcontroller.cpp:333`); whether a mouse press in window B resets/clears highlight in window A's controller (the `setIsHighlight(false)` path runs for any watched object, `:416`) is HYPOTHESIS, not traced.
- **Text-input vs navigation shortcuts:** Tab/Space/arrows are global shortcuts mapped to navigation commands; how text fields (`TextInputField`) suppress them while editing was not traced (shortcuts module).
- `JsApiEngine` role inside `UiEngine` (`uiengine.cpp:41`) and the full extensions `api` object were not read (belongs to `extensions-system.md`).
- Per-element detail of individual `uicomponents` controls (properties/signals) and `propertiespanel` QML (133 files) is out of scope here.

*Authored from: `muse\framework\ui\` (uimodule, uiengine, guiapplication, uiconfiguration, themeapi, navigationcontroller + `Muse.Ui` navigation/accessible classes, foreign.h, qmlapi/qmllauncher), `muse\framework\interactive\` (iinteractive, interactive.cpp, InteractiveProvider.qml, module), `uicomponents\` (CMake, module, FlatButton/StyledDialogView/ButtonBox/NavigationFocusBorder, popup/dialog/window views, AbstractMenuModel), `dockwindow` + `dockwindow_v2` (CMake, module, dockwindow.h, diff of headers), `src\appshell\qml` (AboutModel/AboutDialog/NotationPageModel/WindowContent/AppWindow/CMake), `src\app\internal\guiapp.cpp`, root + framework CMake option wiring, ADR 00004 (2026-11-08). **If this contradicts the live source, the source wins.***
