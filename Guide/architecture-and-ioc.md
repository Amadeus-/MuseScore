# MuseScore — Architecture & IoC (module system + DI)

> **What this file is.** Scope: the modular architecture, the IoC/DI container (`muse\framework\global\modularity` + the vendored `kors_modularity`), the `IModuleSetup`/`IContextSetup` lifecycle and **who drives it** (`BaseApplication`), the per-window **context** model, the app's module list (`appfactory.cpp`), the CMake module machinery (`muse_create_module`, `MUSE_MODULE_*` / `MUE_BUILD_*`, stubs), and the ADR map. Source-verified deep pass on the MuseScore 5.0 `main` tree. **Live source wins** over this Guide — re-Read/Grep before editing. Start at `00-core.md`. Cites are relative to `C:\Dev\+MuseScore+`; `kors\` = `muse\framework\global\thirdparty\kors_modularity\modularity\`.

## 0. 60-second orientation

- ⭐ **Everything is a module.** A module = a dir with `*module.h/.cpp` (a class deriving `muse::modularity::IModuleSetup`), public interfaces `I*.h` at the dir root, private impls in `internal\`, QML + view-models in `qml\<Area>\<Name>\`. (ADR 00001 layout, `muse\docs\adr\00001-CoreArch-Modularity.md:37-49`.)
- ⭐ **There are no `Inject`/`INJECT` macros any more.** DI is C++ templates: `muse::GlobalInject<I>` / `muse::ContextInject<I>` member fields (+ `...ThreadSafeInject` variants). The container is `ModulesIoCBase`-derived `ModulesGlobalIoC` / `ModulesContextIoC`. (`muse\framework\global\modularity\ioc.h:70-81`, `kors\ioc.h`, `kors\modulesioc.h`.) The only remaining macros are `INTERFACE_ID(...)`, `MODULE_GLOBAL_INTERFACE`, `MODULE_CONTEXT_INTERFACE`.
- ⭐ **Two IoC scopes.** *Global* interfaces (`MODULE_GLOBAL_INTERFACE`) live in the single `globalIoc()`; *context* interfaces (`MODULE_CONTEXT_INTERFACE`) live in one `ioc(ctx)` **per window/context** (id ≥ 1). A `static_assert` on `I::modularity_isGlobalInterface()` enforces you register/resolve in the right container.
- ⭐ **Lifecycle is phase-by-phase across ALL modules** (not module-by-module): `registerResources`+`registerExports` for every module → `registerUiTypes`+`resolveImports` → `onPreInit` → `onInit` (+`registerApi`) → `onAllInited` → `onStartApp`. Hence module order in `appfactory.cpp` is "unimportant" except `diagnostics` first (`src\app\appfactory.cpp:309-313`). Per-context setups (`IContextSetup`) then run the same register/resolve/init phases later, once per window (`muse\framework\global\internal\baseapplication.cpp:328-353`).
- **Driver:** `main.cpp` → `AppFactory::newApp` → `BaseApplication::setup()` (module phases) → `setupNewContext()` (first window) (`src\app\main.cpp:191-212`).
- **Stubs:** when a module is disabled at build time, a *stub* module with the **same class name, same target name/alias** provides do-nothing impls of its interfaces; `appfactory.cpp` `#include`s either the real or the stub header (`muse\framework\stubs\`, `src\stubs\`).
- **Key files:** `muse\framework\global\modularity\{ioc.h,imodulesetup.h,imoduleinterface.h,ioccontext.cpp}`, `kors\{ioc.h,ioc.cpp,modulesioc.h,context.h,imoduleinterface.h,funcinfo.h}`, `muse\framework\global\internal\baseapplication.{h,cpp}`, `muse\framework\ui\internal\guiapplication.{h,cpp}`, `src\app\appfactory.cpp`, CMake: `muse\framework\cmake\{MuseCreateModule,MuseDeclareOptions,MuseModules,MuseSetupConfiguration}.cmake`, `SetupConfigure.cmake`.

## 1. The module model

### 1.1 Anatomy of a module (as built in the tree)

```
src\appshell\                      (real example; ADR 00001 gives the canonical layout)
  CMakeLists.txt                   muse_create_module(appshell) + target_sources + target_link_libraries
  appshellmodule.h/.cpp            AppShellModule : IModuleSetup   +   AppShellContext : IContextSetup
  iappshellconfiguration.h         PUBLIC interface (global)   iappshellstate.h (context)
  appshelltypes.h / appshellcommands.h
  internal\                        private impls (appshellconfiguration, appshellstate, startupscenario, sessionsmanager…)
                                   + a few "internal" interfaces (istartupscenario.h, isessionsmanager.h …)
  qml\MuseScore\AppShell\          NEW-STYLE QML module: CMakeLists (qt_add_qml_module URI MuseScore.AppShell) + *.qml + view-models (*.cpp/h with QML_ELEMENT)
  widgets\ resources\              non-QML bits (splash screen widgets, qrc)
```
Cites: `src\appshell\CMakeLists.txt:21-101`, `src\appshell\qml\MuseScore\AppShell\CMakeLists.txt:21-99`.

### 1.2 `IModuleSetup` — the real virtuals (`muse\framework\global\modularity\imodulesetup.h:57-85`)

```cpp
class IModuleSetup {                         // imodulesetup.h:57
    virtual std::string moduleName() const = 0;                // the ONLY pure virtual
    ModulesGlobalIoC* globalIoc() const;                       // convenience → modularity::globalIoc()
    virtual void registerExports() {}   virtual void resolveImports() {}
    virtual void registerResources() {} virtual void registerUiTypes() {}  virtual void registerApi() {}
    virtual void onPreInit(const IApplication::RunMode&) {}  onInit(...)  onAllInited(...)
    virtual void onDelayedInit() {}  onDeinit() {}  onDestroy() {}
    virtual void onStartApp() {}
    virtual IContextSetup* newContext(const ContextPtr& ctx) const { return nullptr; }
};
```

| Virtual | Meaning / typical use | Called by (file:line) | When |
|---|---|---|---|
| `moduleName()` | string id; passed as the `module` arg of `registerExport/resolve` (diagnostic only — see gotchas) | everywhere | — |
| `registerResources()` | `Q_INIT_RESOURCE(...)` for static qrc (only `engraving` still uses it among app modules) | `baseapplication.cpp:175,180` | phase 1 (global module first, then each module in `addModule` order) |
| `registerExports()` | create impl objects and `globalIoc()->registerExport<I>(name, impl)` | `baseapplication.cpp:176,181` | phase 1 |
| `registerUiTypes()` | legacy `qmlRegisterType`-style registration (`MScore::registerUiTypes()`); **most modules no longer define it** — view-models use `QML_ELEMENT` + `qt_add_qml_module` | `baseapplication.cpp:177,187` | phase 2 (global: right after its registerExports; others: after ALL modules registered exports) |
| `resolveImports()` | `globalIoc()->resolve<IOther>(...)` to *push registrations into other modules' registries* (e.g. register URIs/commands/APIs) | `baseapplication.cpp:184,188` | phase 2 (**global** interfaces only exist now; no context ioc yet) |
| `registerApi()` | register script/extension API creators (`IApiRegister::regApiCreator`) | `baseapplication.cpp:185` (global, before onPreInit) and `:210` (every other module, **right after its own `onInit`**) | asymmetric — see gotchas |
| `onPreInit(mode)` | earliest init; `GlobalModule` loads `settings`, builds logger/profiler here | `baseapplication.cpp:199,201` | phase 3 |
| `onInit(mode)` | main init (`m_configuration->init()`, load fonts, …) | `baseapplication.cpp:207,209` | phase 4 |
| `onAllInited(mode)` | everything is inited; cross-module wiring | `baseapplication.cpp:216,218` | phase 5 |
| `onStartApp()` | after setup, on the *next event-loop turn* | `baseapplication.cpp:224-229` (`QMetaObject::invokeMethod(qApp, …, Qt::QueuedConnection)`) | phase 6 |
| `onDelayedInit()` | slow/low-priority init; GUI only: **5000 ms** timer after `doSetup` | `guiapplication.cpp:43,57-65` | later |
| `onDeinit()` / `onDestroy()` | teardown in two sweeps over modules (same order as added, **not** reversed) | `baseapplication.cpp:451-461` | `doFinish()` |
| `newContext(ctx)` | factory returning this module's per-window `IContextSetup*` (or `nullptr`) | `baseapplication.cpp:252,258` | when a context is created |

`RunMode` passed to the `on*` hooks: `GuiApp`, `ConsoleApp`, `AudioPluginRegistration` (`muse\framework\global\iapplication.h:44-48`) — modules branch on it (`AppShellContext::onInit` returns early unless `GuiApp`, `appshellmodule.cpp:147-157`).

### 1.3 `IContextSetup` — per-window module state (`imodulesetup.h:32-55`)

Ctor takes the `ContextPtr`; exposes `iocContext()` and `ioc()` (= `modularity::ioc(iocContext())`, the **context** container). Virtuals: `registerExports, resolveImports, onPreInit(mode), onInit(mode), onAllInited(mode), onDelayedInit, onDeinit, onDestroy`. **No** `registerResources/UiTypes/Api/onStartApp` at context level. Each module that owns window-scoped services subclasses it next to its `IModuleSetup` (e.g. `AppShellContext`, `EngravingContext`, `DockContext`, `CloudContext` stub).

### 1.4 Real module walk-through — `appshell` (`src\appshell\appshellmodule.cpp`)

```cpp
static const std::string mname("appshell");                          // :57
void AppShellModule::registerExports() {                             // :64
    m_appShellConfiguration = std::make_shared<AppShellConfiguration>(globalCtx());
    globalIoc()->registerExport<IAppShellConfiguration>(moduleName(), m_appShellConfiguration);
}
void AppShellModule::resolveImports() {                              // :71  push my pages/commands into other modules' registers
    auto ir = globalIoc()->resolve<muse::interactive::IInteractiveUriRegister>(mname);
    if (ir) { ir->registerPageUri(Uri("musescore://home")); /* … */ ir->registerQmlUri(Uri("musescore://about/musescore"), "MuseScore.AppShell", "AboutDialog"); }
    auto cr = globalIoc()->resolve<muse::rcommand::ICommandsRegister>(mname);
    if (cr) { cr->reg(std::make_shared<AppshellCommandsRegister>()); }
}
void AppShellModule::onInit(const IApplication::RunMode&) { m_appShellConfiguration->init(); }   // :94
IContextSetup* AppShellModule::newContext(const ContextPtr& ctx) const { return new AppShellContext(ctx); }  // :108
void AppShellContext::registerExports() {                            // :113  one set of services PER WINDOW
    m_appshellState = std::make_shared<AppShellState>(iocContext());
    ioc()->registerExport<IAppShellState>(mname, m_appshellState);
    ioc()->registerExport<IStartupScenario>(mname, new StartupScenario(iocContext()));
    /* ISessionsManager, IAppshellCommandsController, IShortcutsResolver, IExtensionContextResolver, IAppMenuModelHook … */
}
void AppShellContext::resolveImports() { auto cs = ioc()->resolve<ICommandsState>(mname); if (cs) cs->reg(std::make_shared<AppshellCommandsState>(iocContext())); }   // :134
void AppShellContext::onPreInit(...) { m_commandsController->preInit(); }   // :142
void AppShellContext::onInit(mode)  { m_sessionsManager->init(); if (mode != GuiApp) return; m_commandsController->init(); m_extensionContextResolver->init(); }  // :147
void AppShellContext::onDeinit()    { m_sessionsManager->deinit(); }     // :159
```
⭐ Pattern to copy: **global config → `IModuleSetup`; anything that touches a window/project/notation → `IContextSetup`**. `engraving` is the richest example (`src\engraving\engravingmodule.cpp:96-311`): exports renderers + config globally, `registerApi()` registers the QML plugin API (`apiv1::PluginAPI::registerQmlTypes()` + `regApiCreator("MuseApi.Engraving", …)`, `:134-149`), `registerResources()` inits qrc fonts (`:151`), `registerUiTypes()` → `MScore::registerUiTypes()` (`:156`), `onInit` loads fonts/styles (`:161-275`), and `EngravingContext` registers `IPaletteScoreProvider`, `IEngravingElementsProvider`, `IEngravingContextConfiguration` per window (`:289-297`). `GlobalModule` (`muse\framework\global\globalmodule.cpp:79-295`) registers `IGlobalConfiguration`, `ISystemInfo`, `ITickerProvider`, `IApiRegister`, `IFileSystem`, `IProcess`, … and does logger/profiler/settings setup in `onPreInit` (`:133-247`); its `GlobalContext::registerExports()` is empty (`:292`).

### 1.5 Interface / implementation / view-model layering (ADR 00004)

| Layer | Where | Rules |
|---|---|---|
| **Model (domain)** | `i*.h` public interfaces, `*types.h`, `internal\*` impls | pure C++/STL where possible, no view knowledge |
| **ViewModel** | `qml\<Area>\<Name>\*.cpp/h` | `QObject`/`QAbstractListModel`, `Q_PROPERTY`/`Q_INVOKABLE`, usually `…Model` suffix; **gets services via `ContextInject`** with `Contextable(iocCtxForQmlObject(this))` |
| **View** | `qml\…\*.qml` | geometry/animation only; QWidget views are legacy |
| **Interaction** | commands (ADR 00005/00101): UI → ViewModel `dispatch(command)` → `ICommandDispatcher` → module `…CommandsController` → service; state back via `…CommandsState` | each module owns `modulecommands.h`, `internal\*commandsregister`, `internal\*commandsstate` (ADR 00101 §Implementation) |
| **Async** | `muse::async::{Channel,Notification,Promise}` + `Asyncable` (ADR 00006) | subscribers auto-unsubscribe with `Asyncable`; handler runs on the subscriber's thread |

## 2. Interfaces (`imoduleinterface.h`)

```cpp
class IAppShellConfiguration : MODULE_GLOBAL_INTERFACE        // src\appshell\iappshellconfiguration.h:33
{
    INTERFACE_ID(IAppshellConfiguration)                      // :35
public:
    virtual ~IAppShellConfiguration() = default;  /* pure virtuals … */
};
```
- `MODULE_GLOBAL_INTERFACE` → `public IModuleGlobalInterface` (`modularity_isGlobalInterface() == true`); `MODULE_CONTEXT_INTERFACE` → `public IModuleContextInterface` (`false`) (`kors\imoduleinterface.h:40-54,84-85`). Legacy aliases `MODULE_GLOBAL_EXPORT_INTERFACE` (= global) and `MODULE_EXPORT_INTERFACE` (= **context**) remain (`:88-89`; 1 use total: `src\importexport\mnx\imnxconfiguration.h:27`). Usage counts: ~115 global vs ~86 context interface headers (Grep counts, `*.h`).
- `INTERFACE_ID(id)` stamps `static constexpr InterfaceInfo modularity_interfaceInfo()` = `{ #id , MODULENAME }` (`:65-71`). ⭐ The **registry key is the stringified token `#id`** (`std::map<std::string_view, Service>` keyed by `info.id`, `modulesioc.h:290`) — it is **not** the C++ type, so it must be globally unique (e.g. class `IAppShellConfiguration` uses id `IAppshellConfiguration`, differing in case from the class name).
- `MODULENAME` = `kors::funcinfo::moduleNameBySig(__FUNCSIG__ / __PRETTY_FUNCTION__)` — the first namespace component after the return type (`kors\funcinfo.h:48,165-192`; e.g. `mu::appshell::…` → `appshell`); informational only (stored in `InterfaceInfo.module`; the real "source module" printed on double-register is the string passed to `registerExport`).

## 3. The IoC container

### 3.1 Where the code lives
`muse\framework\global\modularity\ioc.h` is a thin alias header over the vendored MIT-licensed **kors_modularity** library by Igor Korsukov (`muse\framework\global\thirdparty\kors_modularity\`, README: "Used in … MuseScore"). `modulesioc.h:37` **force-defines `IOC_CHECK_INTERFACE_TYPE`**, so the typed variants are always in effect: `muse::modularity::ModulesGlobalIoC = kors::…::ModulesGlobalIoC`, `ModulesContextIoC = kors::…::ModulesContextIoC` (`ioc.h:33-35`); the `#else` "untyped" branch (`ioc.cpp:59-81`, `modulesioc.h:85-138`) is dead in this tree.

### 3.2 Container API (`kors\modulesioc.h`)

| Member (on `ModulesGlobalIoC` / `ModulesContextIoC`; both `static_assert` the interface scope) | Behavior |
|---|---|
| `registerExport<I>(const std::string& module, std::shared_ptr<I> p)` | store `p` under `I::modularity_interfaceInfo().id`; notifies waiting subscribers |
| `registerExport<I>(module, I* p)` | wraps raw ptr in `shared_ptr<I>(p)` → **container takes ownership** (`new Impl()` idiom) |
| `registerExportNoDelete<I>(module, I* p)` | same but no-op deleter (non-owned) |
| `unregister<I>(module)` / `unregisterIfRegistered<I>(module, p)` | null the slot, notify subscribers with `nullptr`, then release |
| `resolve<I>(std::string_view usageModule)` | returns `shared_ptr<I>` or **`nullptr`** if not registered; `usageModule` is ignored (`doResolvePtrByInfo`'s first arg is unused, TODO stats) |
| `subscribe<I>(Subscriber<I>*)` | register an `onChanged(shared_ptr<I>)` callback; works only if the slot already exists (`doSubscribe` returns -1 otherwise) |
| `reset()` | moves the map out under the mutex, calls every subscriber with `nullptr` (does **not** delete registered objects explicitly; they die with the copied map) |
| free fns (`muse\framework\global\modularity\ioc.h:44-67`) | `globalCtx()`, `globalIoc()`, `ioc(const ContextPtr&)`, `removeIoC(ctx)`, `resetAll()` |

Behaviors worth knowing (all TRACED to `modulesioc.h`/`ioc.cpp`):
- **Double registration**: second `registerExport` of the same id → prints `"<module>: double register:<id>, first register in <srcModule>"` to `std::cerr` + `assert(false)`; in release builds the **first registration wins**, the second is silently dropped (`modulesioc.h:195-206`).
- `pointer_cast`: `dynamic_pointer_cast` in debug (no `NDEBUG`) vs `static_pointer_cast` in release (`:261-269`) → an `INTERFACE_ID` collision is a null in debug but **UB** in release.
- `registerExport(module, nullptr)` asserts and returns (`:187-190`).
- One `std::mutex` per container guards its map; callbacks are invoked *outside* the lock.
- `ioc(ctx)` (`ioc.cpp:44-57`): asserts `ctx && ctx->id > 0` and returns `nullptr` otherwise; lazily `new`s a `ModulesContextIoC` per id in a plain `static std::map<IoCID, …*> s_contexts` (**not mutex-protected**). `removeIoC(ctx)` deletes it (`:85-96`); `resetAll()` resets every context container then the global (`:98-104`).
- `globalIoc()` is a function-local static (`ioc.cpp:38-42`).

### 3.3 Contexts (`kors\context.h`)
- `struct Context { IoCID id = -1; }` with `IoCID = uint16_t`; `ContextPtr = shared_ptr<Context>`; `globalId = 0` (`context.h:32-43`). Real window contexts get ids **1, 2, 3…** (`baseapplication.cpp:301-305`).
- ⭐ The "global context" `globalCtx()` is effectively a **null `ContextPtr`**: `static const ContextPtr globalCtx;` in the header is default-constructed (`context.h:44`). (`ioc.cpp:30` defines a *different* unnamespaced `::globalCtx` holding `Context(globalId)`; nothing in `kors::modularity` uses it — TRACED oddity.) Objects owned by a global module are constructed with `globalCtx()` (e.g. `appshellmodule.cpp:66`).
- `class Contextable` (alias `Injectable`): holds a `ContextPtr`, **or** a pointer to another `Contextable`, **or** a lazy `std::function<ContextPtr()>`; `iocContext()` resolves and caches on first non-null result (`context.h:46-90`). Classes that use `ContextInject` derive from it.
- Per-window plumbing: `BaseApplication::contextData(ctx)` builds `ContextData{ctxId, initializing, options, setups}` and fills `setups` with `GlobalModule::newContext` + every `IModuleSetup::newContext` that returns non-null (`baseapplication.cpp:239-265`).
- **QML bridge** (`muse\framework\global\modularity\ioccontext.cpp`, `ioc.h:83-97`): `GuiApplication::loadMainWindow` creates a `QQmlContext` for the window, wraps the `ContextPtr` in `QmlIoCContext`, and sets it as context property `"ioc_context"` (`guiapplication.cpp:188-192`). `iocCtxForQmlObject(this)` returns a lambda that walks `qmlContext(p)` up the QObject parents and reads that property off the **engine root context** (`ioccontext.cpp:32-67`; "each IoC context (window) has its own instance of the Qml Engine", `:55-56`). `iocCtxForQWidget(w)` instead walks parents for a dynamic int property `"ioc_context"` and builds a *new* `Context(ctxId)` (`:69-89`).

### 3.4 Injection at the call site
`kors\ioc.h:48-229`:

| Type | Use | Ctor |
|---|---|---|
| `GlobalInject<I>` | global interface | default (binds to `globalCtx`); `static_assert(global)` |
| `ContextInject<I>` | context interface | `(const ContextPtr&)` **or** `(const Contextable*)` — usually `= { this }` |
| `GlobalThreadSafeInject<I>` / `ContextThreadSafeInject<I>` | same, but `get()` under a `std::shared_mutex` — for use from non-main threads (audio etc.) | same |
| `Inject` / `ThreadSafeInject` / `Injectable` | **deprecated aliases** of `ContextInject` / `ContextThreadSafeInject` / `Contextable` (`ioc.h:77-81`, `kors\ioc.h:231-235`, `context.h:92-93`) | — |

Semantics (`InjectBase`, `kors\ioc.h:48-133`): lazy — `operator()`/`get()` resolves on **first use** and caches a `shared_ptr<I>` in `m_i`. Global interface → `globalIoc()->resolve`; context interface → `ioc(iocContext())->resolve` (`:72-77`). On a successful resolve it `subscribe`s so that `unregister`/`reset` later nulls the cached pointer (`:116-127`). If the first resolve fails nothing is cached/subscribed, so the next call retries. `set(shared_ptr<I>)` injects a mock ("For testing purposes only").

Call-site example (`src\appshell\internal\appshellstate.h:31-43`, global service created per context):
```cpp
class AppShellState : public IAppShellState, public muse::Contextable
{
    muse::ContextInject<muse::ui::IUiState> uistate = { this };   // resolved lazily from THIS window's ioc
public:
    AppShellState(const muse::modularity::ContextPtr& iocCtx) : muse::Contextable(iocCtx) {}
    …
};
// use:  uistate()->…        (operator() returns std::shared_ptr<I>)
```
View-model (`src\appshell\qml\MuseScore\AppShell\notationpagemodel.h:40-56`, `.cpp:46-49`):
```cpp
class NotationPageModel : public QObject, public muse::Contextable, public muse::async::Asyncable {
    Q_OBJECT  QML_ELEMENT
    muse::GlobalInject<notation::INotationSceneConfiguration> notationSceneConfiguration;
    muse::ContextInject<IAppShellState> appShellState = { this };
    muse::ContextInject<muse::rcommand::ICommandDispatcher> commandDispatcher = { this };
    …
};
NotationPageModel::NotationPageModel(QObject* parent) : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}
```
Registration is the mirror image (`appshellmodule.cpp:64-69,113-132`). Unit-test substitution: `service.dep.set(std::make_shared<DepMock>())` (ADR 00002 §Replacement in unit tests). The test `Environment` replays the **global** module phases only — no `newContext`/contexts (`muse\framework\testing\environment.cpp:58-109`); mock context interfaces with `.set()`.

### 3.5 Adding a service — checklist (global case)
1. `ifoo.h`: `class IFoo : MODULE_GLOBAL_INTERFACE { INTERFACE_ID(IFoo) public: virtual ~IFoo()=default; … };` (use `MODULE_CONTEXT_INTERFACE` if it must be per-window).
2. `internal\foo.h/.cpp` impl; add to the module `CMakeLists.txt` `target_sources`.
3. In `<mod>module.cpp::registerExports()`: `globalIoc()->registerExport<IFoo>(moduleName(), std::make_shared<Foo>(globalCtx()));` (context case: `ioc()->registerExport<IFoo>(mname, …(iocContext()))` inside the module's `IContextSetup`).
4. Consumers: `GlobalInject<foo::IFoo> foo;` / `ContextInject<foo::IFoo> foo = { this };` and `foo()->…`. Do not use it before the context exists (see gotchas).
5. If the module has a stub, add a stub impl and register it in the stub module too.

## 4. Application lifecycle (who calls what)

### 4.1 Boot (`BaseApplication::doSetup`, `muse\framework\global\internal\baseapplication.cpp:158-230`)
`main.cpp:191-212` runs everything via nested `QMetaObject::invokeMethod(…, Qt::QueuedConnection)` so the splash can show: `newApp` + `showSplash` → `setup()` → `setupNewContext()`.

```
registerExport<IApplication>("global", shared_from_this())      :172
GlobalModule: registerResources, registerExports, registerUiTypes                 :174-177
for m in modules: registerResources, registerExports                              :179-182
GlobalModule.resolveImports, GlobalModule.registerApi                             :184-185
for m: registerUiTypes, resolveImports                                            :186-189
applyCommandLineOptions                                                           :194
GlobalModule.onPreInit ; for m: onPreInit                                         :199-202
GlobalModule.onInit    ; for m: onInit, registerApi                               :207-211
GlobalModule.onAllInited ; for m: onAllInited                                     :216-219
queued: GlobalModule.onStartApp ; for m: onStartApp                               :224-229
(GuiApplication::doSetup, after the above) 5000 ms timer → onDelayedInit (global then modules)   guiapplication.cpp:50-75
```
`GlobalModule` is held separately from `m_modules` "to initialize logger and profiler as early as possible" (`baseapplication.h:111-113`) and always goes first in every phase.

### 4.2 Context (window) lifecycle
`BaseApplication::setupNewContext(args)` (`baseapplication.cpp:291-326`): allocates `ctxId->id = ++lastId`, calls `contextData()` (which **immediately** creates all `IContextSetup`s via `newContext`), marks `initializing`, then three chained queued events: `showContextSplash` → `setupContext(ctx)` → `initializing=false; startupScenario(ctx)`. Without `MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE` a `static bool once` limits it to a single context (`:293-299`); the root `CMakeLists.txt:198` sets that option ON (so multiple windows are allowed in-process). Subsequent windows come from `SingleProcessProvider::openNewWindow` → `application()->setupNewContext(args)` (`muse\framework\multiwindows\internal\singleprocess\singleprocessprovider.cpp:151-158`).

`setupContext` (`:328-353`) — each phase loops over **all** `IContextSetup`s before the next: `registerExports` → `resolveImports` → `onPreInit` → `onInit` → `onAllInited`.

`GuiApplication::startupScenario` (`guiapplication.cpp:121-137`): queued `loadMainWindow` (creates the QML window, `ioc_context` property, `:155-209`) → queued `doStartupScenario` (the app subclass `MuseScoreGuiApp` resolves `IStartupScenario` from `ioc(ctx)`, applies startup options, `runOnSplashScreen`, then `runAfterSplashScreen`, `src\app\internal\guiapp.cpp:103-138`) → `QTimer::singleShot(5000, contextDelayedInit)` which calls each setup's `onDelayedInit()` if the context still exists (`:131-153`).

Teardown: `destroyContext(ctx)` (`:355-381`) refuses while `initializing`; `doDestroyContext` = every setup `onDeinit()` → every setup `onDestroy()` → `qDeleteAll(setups)` → `removeIoC(ctx)` (`:383-396`); `GuiApplication::doDestroyContext` first hides/deletes the window and calls `IUiEngine::quit()` (`guiapplication.cpp:211-225`). Whole-app `doFinish()` (`:431-484`): process async messages → destroy every context → `m_modules[*]->onDeinit()` → `GlobalModule::onDeinit` → `onDestroy` for modules then global → `qDeleteAll(m_modules)`, `delete m_globalModule` → `modularity::resetAll()` → optional detached restart.

### 4.3 App wiring (`src\app\appfactory.cpp`)
`AppFactory::newApp` → `runMode == GuiApp` ? `newGuiApp` : `newConsoleApp` (`:292-303`). `newGuiApp` (`:305-449`) does `app->addModule(new …Module())` for every module (order below); `newConsoleApp` builds either the console set or the audio-plugin-registration set (`:567-587`; `MUE_ENABLE_CONSOLEAPP`). The app classes: `MuseScoreGuiApp : muse::ui::GuiApplication : BaseApplication`.

## 5. Module inventory

### 5.1 GUI app — `addModule` order (`appfactory.cpp:305-449`)
Gate = C preprocessor macro used for `#include`/`addModule`; **Stub** = a stub module exists that is included when the gate is off. Namespaces: `muse::*` (framework) / `mu::*` (app).

| # | Module (class) | Dir | Gate | Stub |
|---|---|---|---|---|
| 1 | `muse::diagnostics::DiagnosticsModule` — **must be first (installs crash handler)** | `muse\framework\diagnostics` | `MUSE_MODULE_DIAGNOSTICS` | no |
| 2 | `AccessibilityModule` | `framework\accessibility` | `MUSE_MODULE_ACCESSIBILITY` | yes |
| 3 | `ActionsModule`, `RCommandModule`, `RControlModule` | `actions`, `rcommand` (ADR 00101), `rcontrol` (MCP etc.) | always | no |
| 4 | `muse::audio::AudioModule` (`audio\main\audiomodule.h`) | `framework\audio` | `MUSE_MODULE_AUDIO` | yes |
| 5 | `AudioPluginsModule` | `audioplugins` | `MUSE_MODULE_AUDIOPLUGINS` | no |
| 6 | `AutomationModule` (ADR 00103) | `automation` | `MUSE_MODULE_AUTOMATION` | yes |
| 7 | `DrawModule`, `InteractiveModule`(gated, no stub), `MidiModule`, `MidiRemoteModule`, `MpeModule` | `draw`, `interactive`, `midi`, `midiremote`, `mpe` | `MUSE_MODULE_*` | midi/midiremote/mpe yes |
| 8 | `MuseSamplerModule` — **skipped in debug builds unless `MUSE_MODULE_MUSESAMPLER_LOAD_IN_DEBUG`** (`:333-345`) | `musesampler` | `MUSE_MODULE_MUSESAMPLER` | no |
| 9 | `NetworkModule`, `shortcuts::ShortcutsModule` (header from `shortcuts_v2\`), `UiModule`, `UiComponentsModule`, `dock::DockModule`, `ToastModule`, `ToursModule`, `VSTModule`, `MediaModule` | `network`, `shortcuts_v2`, `ui`, `uicomponents`, `dockwindow`, `toast`, `tours`, `vst`, `media` | `MUSE_MODULE_*` | network/shortcuts/tours/vst/media yes; ui/dock/toast no |
| 10 | `mu::appshell::AppShellModule` | `src\appshell` | `MUE_BUILD_APPSHELL_MODULE` | (web: `src\web\appshell`) |
| 11 | `TestflowModule` | `testflow` | `MUSE_MODULE_TESTFLOW` | no |
| 12 | `mu::braille::BrailleModule`, `muse::cloud::CloudModule`, `mu::converter::ConverterModule` | `src\braille`, `framework\cloud`, `src\converter` | `MUE_BUILD_BRAILLE_MODULE`(stub `src\stubs\braille`), `MUSE_MODULE_CLOUD`(stub), `MUE_BUILD_CONVERTER_MODULE` | braille, cloud yes |
| 13 | `mu::engraving::EngravingModule` | `src\engraving` | always | no |
| 14 | importexport: `iex::bb, bww, capella, midi, mlscore, mnxio, musedata, musicxml, ove, audioexport, imagesexport, guitarpro, mei, videoexport, tabledit, lrcexport` | `src\importexport\*` | `MUE_BUILD_IMPEXP_*_MODULE` | midi, mnx, musicxml, ove, mei yes (`src\stubs\importexport\…`) |
| 15 | `PropertiesPanelModule`, `InstrumentsSceneModule`, `ExtensionsModule`, `LanguagesModule`, `LearnModule`, `mi::MultiWindowsModule`, `MuseSoundsModule`, `NotationModule`, `NotationSceneModule` (both in `mu::notation`), `PaletteModule`, `PlaybackModule`, `PreferencesModule`, `PrintModule`, `ProjectModule`, `UpdateModule`, `WorkspaceModule` | `src\propertiespanel`, `instrumentsscene`, … ; `framework\extensions`, `languages`, `learn`, `multiwindows`, `update`, `workspace` | `MUE_BUILD_*_MODULE` / `MUSE_MODULE_*` | most yes |
| 16 | `mu::appjs::AppJsModule` | `src\web\appjs` | `MUE_CONFIGURATION_IS_APPWEB` | — |

### 5.2 Console app (`addConsoleModules`, `:451-552`) and plugin registration (`:554-565`)
Console set: diagnostics, actions, rcommand, rcontrol, audio, audioplugins, draw, midi, mpe, musesampler, ui (gated), vst (gated), media, testflow, converter, engraving, all importexport modules, multiwindows, notation, playback, project, extensions, notationscene (no appshell/dock/palette/…). `AudioPluginRegistration` mode: only audio, audioplugins, vst.

### 5.3 Framework dirs not in the add-module list
`global` (always built, driven as `m_globalModule`), `dockwindow` vs `dockwindow_v2`, `shortcuts` vs `shortcuts_v2` (see build wiring), `stubs`, `testing` (gtest `Environment`), `uicomponents` (module class `UiComponentsModule` inside the UI group).

## 6. CMake module wiring

### 6.1 The macros (`muse\framework\cmake\MuseCreateModule.cmake`)
- `muse_create_module(<target> [ALIAS muse::x] [NO_COVERAGE] [NO_QT] [NO_PCH] [NO_UNITY] [STUB])` (`:45-133`): `qt_add_library` (or plain `add_library` + AUTOMOC/UIC/RCC OFF if `NO_QT` or `!MUSE_QT_SUPPORT`; type from `BUILD_SHARED_LIBS`); optional alias; **adds the same private include dirs to every module** — `${PROJECT_SOURCE_DIR}/src`, `${MUSE_FRAMEWORK_PATH}`, `…/framework`, `…/framework/global` (+ legacy `src` compat roots) (`:85-101`); reuses PCH from `muse_pch` (`MUSE_COMPILE_USE_PCH`, `:110-113`); unity build when `MUSE_COMPILE_USE_UNITY` (`:115-120`); coverage flags; **links every module `PRIVATE` to `muse_global`** (`:129-132`). `STUB` is cosmetic — it only adds `[stub]` to the status message (`:55-57`).
- `muse_create_qml_module(<name>_qml [FOR <target>] …)` (`:135-153`): forwards to `muse_create_module`, `target_link_libraries(<name>_qml INTERFACE <name>_qmlplugin)` (the static QML plugin target made by `qt_add_qml_module`), and for `FOR x` adds `x`'s source dir to includes, links `x` PRIVATE, and registers `…/qml` as a QtCreator import path. Real use: `muse_create_qml_module(appshell_qml FOR appshell)` + `qt_add_qml_module(appshell_qml URI MuseScore.AppShell VERSION 1.0 SOURCES … QML_FILES … IMPORTS TARGET muse_ui_qml …)` (`src\appshell\qml\MuseScore\AppShell\CMakeLists.txt:21-99`).
- `muse_create_thirdparty_module`, `muse_module_add_qrc`, `fixup_qml_module_dependencies` (VS-generator workaround) — `:156-216`.

Typical module `CMakeLists.txt` (`muse\framework\learn\CMakeLists.txt:21-41`): `muse_create_module(muse_learn ALIAS muse::learn)` → `target_sources(muse_learn PRIVATE learnmodule.cpp … internal\…)` → `target_link_libraries(… Qt::…)` → `if (MUSE_MODULE_LEARN_QML) add_subdirectory(qml/Muse/Learn)`. App modules use plain target names (`appshell`, `notation`, `iex_musicxml`), framework modules use `muse_<x>` + alias `muse::<x>`.

### 6.2 Option system
- **Framework modules** — `muse\framework\cmake\MuseDeclareOptions.cmake:25-34`: `declare_muse_module_opt(NAME def)` defines `MUSE_MODULE_<NAME>` (default `def`) and sub-options `_API` (ON), `_QML` (ON), `_TESTS` (`= MUSE_ENABLE_UNIT_TESTS`). Module-specific extras are plain options in the same file (e.g. `MUSE_MODULE_AUDIO_WORKMODE`, `MUSE_MODULE_SHORTCUTS_V2`, `MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2`, `MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE`, `MUSE_MODULE_GLOBAL_LOGGER_DEBUGLEVEL`). `MUSE_MODULE_VST` defaults **OFF** (`:128`).
- **App modules** — root `CMakeLists.txt:91+`: `MUE_BUILD_<X>_MODULE`, `_QML`, `_TESTS`; imports/exports `MUE_BUILD_IMPEXP_*_MODULE`.
- **Fix-ups/hard deps** — `MuseSetupConfiguration.cmake`: no AUDIO ⇒ MUSESAMPLER off; no UI ⇒ TOAST off, UI_QML off, INTERACTIVE off; disabled module ⇒ its `_API/_TESTS` off; no UI_QML ⇒ every `_QML` off (`:3-64`); loops over `MUSE_FRAMEWORK_MODULES` (`MuseModules.cmake:2-33`). Root `SetupConfigure.cmake:334-372` cascades `MUE_BUILD_*` (no NOTATION ⇒ NOTATIONSCENE/PROJECT/PALETTE off; no UI ⇒ APPSHELL off, no UI_QML ⇒ all app `_QML` off).
- **Build configurations** — `MUSESCORE_BUILD_CONFIGURATION` = `app` (default), `app-portable`, `app-web`, `vtest`, `utest` (`SetupConfigure.cmake:25-41,91-276`) each force sets of module flags (e.g. `app-web` turns off ~17 framework modules (`SetupConfigure.cmake:150-167`); `vtest` keeps UI but not UI_QML).
- **How the flags reach C++:** `configure_file(muse_framework_config.h.in → muse_framework_config.h)` emits `#cmakedefine MUSE_MODULE_<X>[ _TESTS/_API ] 1` (`muse\framework\cmake\muse_framework_config.h.in`), and `src\app\app_config.h.in` emits `MUE_BUILD_*` / `MUE_CONFIGURATION_*`. `appfactory.cpp` includes both (`:8-9`).

### 6.3 Add-subdirectory order and linking
- Framework: `muse\framework\CMakeLists.txt:21-171` — `global` first ("should be first to work pch"), then ui/uicomponents, interactive, toast, dockwindow (v2 if `MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2` else v1), … shortcuts (v2 if `MUSE_MODULE_SHORTCUTS_V2`), … update; tests; then `add_subdirectory(stubs)`. "modules need to be listed in dependency order".
- Stubs: `muse\framework\stubs\CMakeLists.txt:21-95` — `if (NOT MUSE_MODULE_X) add_subdirectory(x)` per module (shortcuts picks v1/v2 stub too). The stub target has the **same name + alias** as the real one, so downstream `target_link_libraries(... muse::cloud)` is unchanged.
- App tree: `src\CMakeLists.txt:21-106` — `braille, converter, engraving, importexport, propertiespanel, instrumentsscene, musesounds, notation, notationscene, palette, playback, preferences, print, project, appshell` (each `if (MUE_BUILD_…) add_subdirectory(x) else() add_subdirectory(stubs/x)` where a stub exists), then `app`. Root `CMakeLists.txt:243-245`: `share` → framework → `src`.
- Linking into the exe: `src\app\CMakeLists.txt:95-181` — `LINK_LIB` starts with Qt + `muse::global` + `muse::draw`, then `add_to_link_if_exists(<lib>)` per module, which also appends `<lib>_qml` when it exists; the macro tolerates 3 states: enabled / disabled-with-stub / disabled-no-target (`:113-117`). Executable target `MuseScoreStudio` (`:234-239`), output name `mscore` / `<AppName><major>` on Windows (`:25-32`).

### 6.4 Adding a new module (checklist)
1. Dir with the §1.1 layout; `CMakeLists.txt` with `muse_create_module(<name>)` (+ `qml\` with `muse_create_qml_module(<name>_qml FOR <name>)` if it has UI).
2. App-tree module: add `option(MUE_BUILD_<X>_MODULE …)` in root `CMakeLists.txt`, `#cmakedefine` in `src\app\app_config.h.in`, `add_subdirectory` in `src\CMakeLists.txt` (dependency order), `add_to_link_if_exists(<name>)` in `src\app\CMakeLists.txt`, `#include` + `app->addModule(new …Module())` in `appfactory.cpp` (GUI and, if needed, console list). Framework module: `declare_muse_module_opt(<X> ON)` + add `<X>` to `MuseModules.cmake` + `#cmakedefine`s in `muse_framework_config.h.in` + `add_subdirectory` in `muse\framework\CMakeLists.txt`.
3. Optional stub dir (`stubs\<x>`, same target/alias/class names) + `else()` branches in `appfactory.cpp` and the stub CMake lists.
4. Add the module to the `Guide` module table if public.

## 7. ADR map (`muse\docs\adr\`)

| ADR | Title | Decision (1 line) |
|---|---|---|
| 00001 | CoreArch — Modularity | Split the app into modules (independently valuable, app can run without it); fixed dir layout (`CMakeLists`, `*module.cpp/h`, `i*.h`, `*types.h`, `*errors.h`, `*commands.h`, `internal\`, `qml\`); each module implements `IModuleSetup`, is added to CMake and to the app factory. |
| 00002 | CoreArch — Dependency injection | IoC container + injection fields; modules register services in `registerExports`, consumers `GlobalInject<I>`; `.set()` replaces deps with mocks in tests. |
| 00003 | CoreArch — AppShell | The `appshell` pattern: UI structure (toolbars/panels/pages/lifecycle/navigation) is separated from content views, so views don't know how they're displayed. |
| 00004 | CoreArch — MVVM | View = QML, ViewModel = Qt `…Model` classes under `qml\`, Model = domain interfaces/services (`i*.h`, `internal\`) in pure C++/STL. |
| 00005 | CoreArch — Interact workflow | UI → command dispatcher → per-module commands controller → service; state flows back via command state/notifications (actions superseded by commands, ADR 00101). |
| 00006 | CoreArch — Asynchronous interaction | Custom `Channel` / `Notification` / `Promise` / `Async::call` (not Qt signals); `Asyncable` auto-unsubscribes; handlers run on the subscriber's thread. |
| 00101 | Commands — replace actions with commands | New `rcommand` (register/state/dispatcher) + `rcontrol` (MCP) modules: commands return results, have input schemas, are async, context removed. |
| 00102 | Extensions — native-code plugins | Extend the existing extensions system (QML+JS) to load dynamic libs and call native code from JS (`MuseApi.Native`, `extension_dispatch_v0`) rather than a second plugin system. |
| 00103 | Audio — automation in the audio engine | Pass volume/pan maps by RPC keyed on playback position; swap `ControlNode` for `AutomationControlNode` in the graph. |
| 00104 | Commands — command-first | Only *commands* identify things (info/state/shortcuts); dynamic items become dynamic commands (`command://workspace/select/<n>`); query params carry data only. |
| 00105 | CI — native debug files to Sentry | Drop Breakpad `.sym`; build `RelWithDebInfo` + strip; upload dSYM/PDB/ELF (stable builds only) via `upload_debug_files.cmake`. |
| 00106 | Commands — mnemonics in titles | `&` and `…` live in the command title itself (`MnemonicString`); strip where not needed. |
| 00107 | Commands — availability | Commands carry an `Availability` flags field (Dispatch/Shortcut/API/MCP); default Disabled. |
| 00108 | Shortcuts — context → scope | Shortcut "scope" areas with a priority map replace the action "context"; new `shortcuts_v2` module + JSON config. |
(Also in the dir: `00000-template.md`, `README.md`, `maintainers_ref.md`.)

## Cross-subsystem dependencies

Feeds **every** other file. `app-shell-and-startup.md` — `main.cpp`, `MuseScoreGuiApp`, `AppShellModule`, startup scenario, console flow. `ui-qml-mvvm.md` — `iocCtxForQmlObject`, `IUiEngine` per context, QML modules / `QML_ELEMENT`. `framework-services.md` — `GlobalModule` services, `IApiRegister`, async, settings. `extensions-system.md` / `plugin-api.md` — `registerApi()`/`IApiRegister`. `notation-layer.md`, `playback-and-audio.md` — heavy users of context interfaces. `testing-and-vtest.md` — `muse\framework\testing\Environment`, `.set()` mocking. `ci-and-release.md` — build configurations, ADR 00105. `contributing-and-conventions.md` — code-style for module code.

## Non-obvious gotchas

- ⛔ **Resolving can yield `nullptr` — and `Inject::operator()` then returns an empty `shared_ptr`.** Calling through it crashes. Core code guards with `if (ir) {…}` for optional/stub-able interfaces (`appshellmodule.cpp:73-91`).
- ⛔ **Never use a `ContextInject` before the object has a context.** `ioc(nullptr)` asserts (debug) and returns `nullptr` → null-deref on resolve (`ioc.cpp:46-49`, `kors\ioc.h:75-77`). In QML view-models the context is only discoverable once the object has a `QQmlContext`; don't touch injected members in constructors that run before attachment (`iocCtxForQmlObject` asserts + warns, `ioccontext.cpp:42-45`).
- ⛔ **Module-level code cannot resolve context interfaces.** `IModuleSetup::resolveImports()`/`onInit` run before any context exists; window-scoped registrations/resolves belong in `IContextSetup` (compare `AppShellModule::resolveImports` vs `AppShellContext::resolveImports`). The context phases run on a *later* event-loop turn than the module phases (`main.cpp:199-203`).
- ⛔ **`INTERFACE_ID` must be unique across the whole program** (key is the `#id` string). A collision = double-register assert (debug) / silently ignored (release) / `static_pointer_cast` UB. The id string need not match the class name (see `IAppshellConfiguration`). Not exhaustively scanned for existing collisions (UNCLEAR).
- ⭐ `registerExport<I>(name, new Impl())` transfers ownership; pass a `shared_ptr` when the module also needs the object (pattern: `m_x = make_shared<X>(…); registerExport<IX>(…, m_x)` then `m_x->init()` in `onInit`, `appshellmodule.cpp:66-68,94-97`).
- ⭐ **`registerApi()` timing is asymmetric**: `GlobalModule` runs it between `resolveImports` and `onPreInit`; every other module runs it *immediately after its own `onInit`* (`baseapplication.cpp:185` vs `:210`). It cannot assume other modules have already `registerApi`'d.
- ⭐ **Teardown is not the reverse of init**: `onDeinit`/`onDestroy` iterate `m_modules` in add order (`baseapplication.cpp:451-461`); contexts are destroyed *before* module `onDeinit`.
- ⭐ **Per-window services** (`IAppShellState`, `IStartupScenario`, `IUiEngine`, project/notation sessions…) exist once per context; their lifetime is the `IContextSetup`'s — `removeIoC` deletes the whole container on `destroyContext`. Never cache a context-interface `shared_ptr` outside objects owned by that context.
- ⭐ **`Inject` members store a raw `Contextable*` (`= { this }`)** — the owning object must not be copied/relocated after use (`kors\ioc.h:111-114,130`).
- ⭐ Unit tests: `Environment::setup` does not create contexts (`testing\environment.cpp:58-109`) — context interfaces must be mocked via `.set()`; global modules you depend on must be passed in via `Environment::setDependency`.
- ⛔ Thread safety: `ioc(ctx)`'s `s_contexts` map is unsynchronized (`ioc.cpp:36,51-56`); off-main-thread code should hold a `…ThreadSafeInject` created on the main thread / resolved up front rather than calling `ioc()` itself.
- ⭐ `muse_create_module` gives **every** module the same include roots (`src`, `muse`, `muse\framework`, `muse\framework\global`) (`MuseCreateModule.cmake:85-101`) — there is no build-level barrier to including another module's headers; layering (module → `i*.h` only, never `internal\`) is convention + review. `tools\checkheaders\` contains only `iwyu.sh`; no CMake-enforced rule was found.
- ⭐ **`MUSE_FRAMEWORK_MODULES` omits `AUTOMATION`** (`MuseModules.cmake:2-33`) while the option is declared (`MuseDeclareOptions.cmake:65`), so the "disable sub-options of disabled modules" loops in `MuseSetupConfiguration.cmake:13-48` do not touch `MUSE_MODULE_AUTOMATION_{API,QML,TESTS}`. Also `RCOMMAND`/`RCONTROL` have no `#cmakedefine` in `muse_framework_config.h.in` (they are unconditionally added in `appfactory.cpp:17-19`).
- ⭐ `MUSE_MODULE_GLOBAL_MULTI_IOC` is declared and propagated as a compile definition (`MuseDeclareOptions.cmake:90`, `global\CMakeLists.txt:275-277`) but **nothing in `*.cpp/*.h/*.cmake/*.txt` consumes it** (Grep) — apparently vestigial; multi-window behavior is controlled by `MUSE_MODULE_MULTIWINDOWS_SINGLEPROC_MODE`.
- ⭐ Root `CMakeLists.txt` hard-sets `MUSE_MODULE_SHORTCUTS_V2 ON` (`:199`) and defaults `MUSE_MODULE_DOCKWINDOW_KDDOCKWIDGETS_V2 ON` (`:86`), and `appfactory.cpp` includes `framework/shortcuts_v2/shortcutsmodule.h` unconditionally (`:98`) — but includes `framework/dockwindow/dockmodule.h` (the v1 path, `:117`) even though v2 builds the impl. The two `dockmodule.h` classes have identical declarations today (verified by Grep of both), so it works; a future divergence would be an ODR hazard.
- ⭐ Stubs reuse the **real module's class name and namespace** (`muse::cloud::CloudModule` in `stubs\cloud\cloudstubmodule.h`); exactly one of real/stub may be linked. Stub `moduleName()` is `"<x>_stub"` (`cloudstubmodule.cpp:32`). Stub QML modules also exist for most UI-bearing framework modules; some stubs lack QML (see `MuseSetupConfiguration.cmake:38-64`).
- `resolve<I>("app")` strings (`guiapplication.cpp:171`, `guiapp.cpp:105`) are meaningless labels — the module arg is ignored by `doResolvePtrByInfo`.

## UNCLEAR — needs verification

- Whether any two `INTERFACE_ID`s collide in the current tree (would need a full scan of ~200 interface headers).
- Console-app init flow after `setupNewContext` (`src\app\internal\consoleapp.cpp` was not read in this pass; see `app-shell-and-startup.md` / `mscore-cli.md`).
- `src\web\appshell` / `appjs` module behavior for the APPWEB configuration (only noted from `src\CMakeLists.txt:90-102`, `appfactory.cpp:285-287,444-446`).
- Per-module exported-interface lists (this file inventories modules, build gates and stubs, but not every interface each module registers — read each `*module.cpp::registerExports`).
- Why `ioc.cpp:30` defines an unnamespaced `::globalCtx` (appears unused).

*Authored from (2026-11-08, deep pass): `muse\framework\global\modularity\*`, `…\thirdparty\kors_modularity\modularity\*` (+README, example), `muse\framework\global\internal\baseapplication.{h,cpp}`, `globalmodule.cpp`, `muse\framework\ui\internal\guiapplication.{h,cpp}`, `muse\framework\testing\environment.cpp`, `src\app\{appfactory.cpp,main.cpp,CMakeLists.txt,internal\guiapp.cpp,app_config.h.in}`, `src\appshell\*` (module, CMake, view-model), `src\engraving\engravingmodule.{h,cpp}`, `muse\framework\{learn,stubs\cloud,dockwindow*}` examples, `muse\framework\cmake\*`, `muse\framework\CMakeLists.txt`, `stubs\CMakeLists.txt`, `src\CMakeLists.txt`, root `CMakeLists.txt`/`SetupConfigure.cmake`, and ADRs 00001-00006, 00101-00108. **If this contradicts the live source, the source wins.***
