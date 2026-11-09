# MuseScore — Framework Services

> **What this file is.** The cross-cutting Muse framework services: the `global` foundation (async primitives, settings, logging, io, serialization, core value types), the **command/dispatch triad** (`actions` + `rcommand` + `shortcuts`/`shortcuts_v2`), and a table of the smaller service modules (`workspace`, `cloud`, `network`, `update`, `languages`, `rcontrol`, …). Source-verified against the MuseScore 5.0 `main` tree at `C:\Dev\+MuseScore+` (framework = `muse\framework\`). **If this file contradicts the live source, the source wins** — re-Read before asserting. Start at `00-core.md`. Module/IoC/lifecycle mechanics are in `architecture-and-ioc.md` and are **not** repeated here.

## 0. 60-second orientation

- **`muse\framework\global\`** is the base library every module links. Reactive plumbing = `async\` (`Channel`, `Notification`, `Asyncable`, `Async`, `Promise`, `ChangedNotify`) — **all thin `using` aliases onto the vendored `kors_async` library** (`global\thirdparty\kors_async`). Persistent config = `Settings` singleton (`settings.h`). Core value types = `Val`, `Ret`/`RetVal`, `String`, `Uri`/`UriQuery`, `TranslatableString`, `io::path_t`.
- **Command system, two generations coexisting:** legacy **actions** (`muse\framework\actions`: `IActionsDispatcher`, `ActionCode` = `std::string`, `ActionData`) and the new **commands** (`muse\framework\rcommand`: `ICommandDispatcher`, `Command` = `Uri` with scheme `command`, `Params`, async `Promise<Response>`). ADR `00101`/`00104`/`00107` say commands **fully replace** actions; in the tree today `ActionsDispatcher` still exists and **forwards `command://…` codes into the command dispatcher** (§4.1). New code → commands.
- **Shortcuts:** `shortcuts` (v1, context-based, action-keyed) vs **`shortcuts_v2`** (scope-based, command-keyed, JSON). Root `CMakeLists.txt:199` hard-sets `set(MUSE_MODULE_SHORTCUTS_V2 ON)` and `muse\framework\CMakeLists.txt:116-121` then builds **only** `shortcuts_v2` — so **v2 is the live one**. The `shortcuts` v1 headers survive as thin `#ifdef MUSE_MODULE_SHORTCUTS_V2 → #include "../shortcuts_v2/…"` forwarders (e.g. `shortcuts\shortcutstypes.h:26`).
- **Service modules** (each has `I*.h` at the module root + `internal\`): `workspace`, `cloud`, `network`, `update`, `languages`, `extensions`, `rcontrol` (MCP server) — §6.
- **Key files:** `global\async\{channel,notification,asyncable,async,promise}.h` · `global\thirdparty\kors_async\async\internal\channelimpl.h` · `global\settings.h/.cpp` · `global\types\{val,ret,retval,uri}.h` · `actions\internal\actionsdispatcher.cpp` · `rcommand\{commandtypes,icommanddispatcher,typedcommand}.h` · `shortcuts_v2\internal\shortcutscontroller.cpp` · `src\appshell\internal\shortcutresolver.cpp`.

---

## 1. `global\` foundation

### 1.1 Directory inventory

| Path | What |
|---|---|
| `async\` | reactive primitives (aliases onto `kors_async`) + `ChangedNotify`/`NotifyList` — §2 |
| `settings.h/.cpp` | `muse::Settings` singleton — §3 |
| `log.h` → `logger.h`, `logstream.h`, `thirdparty\kors_logger` | logging + assert macros — §1.4 |
| `io\` | `IFileSystem`, `path_t`, `File`/`Buffer`/`IODevice`, `Dir`, `FileInfo`, `FileSystemWatcher` — §5 |
| `serialization\` | `json.h`, `xmlstreamreader/writer`, `xmldom`, `zipreader/writer`, `textstream`, `msgpack` — §5.4 |
| `types\` | `Val`, `Ret`, `RetVal*`, `String`, `ByteArray`, `Uri`, `Color`, `DateTime`, `Version`, `Flags`, `Id`, `Ratio`, `Secs`, `TranslatableString`, `MnemonicString`, `number.h` — §4 |
| `modularity\` | IoC (`ioc.h`, `imodulesetup.h`) — **see `architecture-and-ioc.md`** |
| `internal\` | `BaseApplication`, `FileSystem`/`MemFileSystem`, `GlobalConfiguration`, `Process`, `SystemInfo`, … (impls) — **see `architecture-and-ioc.md`** |
| `globalmodule.*` | `GlobalModule`/`GlobalContext` — registers the global exports (§1.3) |
| `iapplication.h` | `IApplication` (name/version/`RunMode`/`setupNewContext`/`finish`/`restart`) — lifecycle in `architecture-and-ioc.md` |
| `iglobalconfiguration.h` | path/config provider (§1.2) |
| `timer.h`, `ticker.h`, `itickerprovider.h` | `Timer` (steady-clock µs, `onTimeout(Asyncable*,cb)`), `Ticker` (`start(interval,cb,Mode::Single/Repeat)`) |
| `progress.h` | `Progress` — shared-state progress/cancel/finish object (channels inside) — §2.6 |
| `concurrency\`, `allocator.*`, `runtime.*` | thread-pool / allocator / runtime helpers (not traced) |
| `uuid.*`, `defer.h`, `containers.h`, `stringutils.*`, `stringsearch.*`, `realfn.h`, `dataformatter.*`, `interpolation.h`, `ptrutils.h`, `vectorview.h`, `utils.*` | utilities (`DEFER` macro `defer.h:48`; `muse::strings::{join,split,startsWith,toUpper}`; `muse::value(map,key,default)` in `containers.h`) |
| `translation.*` | `mtrc(...)`/`qtrc(...)`/`TranslatableString` back-end |
| `configreader.*` | `ConfigReader::read(":/configs/x.cfg")` → `ValMap` (JSON `.cfg` resources) |
| `api\` | `IApiRegister`, `LogApi`/`ProcessApi`/`FileSystemApi` — the framework-side QML/extension API surface (`MuseApi.Log`, `api.process`, `MuseApi.FS`, `globalmodule.cpp:120-122`) |
| `platform\`, `thirdparty\` | per-OS code; vendored `kors_async`, `kors_logger`, `kors_profiler`, `kors_modularity`, msgpack |

### 1.2 `IGlobalConfiguration` (`global\iglobalconfiguration.h:29`)

The one place that knows platform paths. Key members (all `io::path_t` unless noted):

| Method | Meaning |
|---|---|
| `appBinPath` / `appBinDirPath` | executable / its dir |
| `appDataPath` | shipped read-only data (`…/share`) |
| `isBundledWithApp` / `toBundledPath` / `isBundledPath` / `fromBundledPath` | `${INSTALL_ROOT}` token rewriting so config files don't hard-code the install dir (`:51-63`) |
| `appConfigPath` | writable per-user config dir |
| `userAppDataPath` | writable per-user app data (shortcuts, workspaces…) |
| `userBackupPath`, `userDataPath` | backups; public documents-style dir |
| `homePath`, `downloadsPath`, `documentsPath`, `genericDataPath` | system dirs |
| `useFactorySettings()`, `enableExperimental()`, `devModeEnabled()`/`setDevModeEnabled`, `metricUnit()`/`setMetricUnit`, `museHubWebUrl()`, `highResolutionTimers()` | global switches |

### 1.3 What `GlobalModule` registers (`global\globalmodule.cpp:88-111`)

`IGlobalConfiguration`, `ISystemInfo`, `ICryptographicHash`, `IProcess`, `ITickerProvider`, `api::IApiRegister`, `api::IFileSystemApiRestriction`, `IApplicationEventController` (Qt builds), and **`IFileSystem`** = `FileSystem` (or `MemFileSystem` under `Q_OS_WASM`, `:107-111`). `GlobalModule::onPreInit` also sets up the logger + profiler and **starts the async pump** (§2.5); `onInit` does Windows `timeBeginPeriod(1)` (`:256-267`); `onDeinit` stops the ticker and calls `async::terminate()` (`:270-273`).

### 1.4 Logging & assert macros

`log.h` just includes `logger.h`/`profiler.h`; the macros live in `global\thirdparty\kors_logger\src\log_base.h`:

| Macro | Meaning |
|---|---|
| `LOGE()` / `LOGW()` / `LOGI()` / `LOGD()` | stream-style error / warn / info / debug (`log_base.h:49-52`); tag defaults to `CLASSFUNC` (`:36`) |
| `LOGDA()` | "active debug" (cyan, debug level), `LOGN()` compiles but emits nothing (`:53-54`) |
| `IF_ASSERT_FAILED(cond) { … }` / `IF_ASSERT_FAILED_X(cond, msg)` | asserts in debug **and** runs the following block in release (the dominant guard idiom — e.g. `actionsdispatcher.cpp:57`) |
| `DO_ASSERT(cond)`, `ASSERT_X(msg)` | assert without a recovery block |
| `UNUSED(x)`, `UNREACHABLE`, `NOT_IMPLEMENTED` (`LOGD "Not implemented!!"`), `NOT_IMPL_RETURN` | misc |
| `TRACEFUNC` / `TRACEFUNC_C(info)` | profiler scope timer (`kors_profiler\profiler\profiler.h:45`) |
| `DEFER { … };` | scope-exit lambda (`defer.h:48`) |

Output stream types accept custom `operator<<(muse::logger::Stream&, T)` (see `Uri`, `path_t`, `Params`, `Shortcut`).

---

## 2. Async primitives (`global\async\`) — the reactive core

⭐ **Everything cross-module is wired with these.** The `muse::async::X` names are one-line `using` aliases: `Channel` → `kors::async::Channel` (`async\channel.h:36`), `Notification` (`notification.h:27`), `Asyncable` (`asyncable.h:27`), `Async` (`async.h:27`), `Promise` + `make_promise` (`promise.h:28-43`), `processMessages`/`terminate` (`processevents.h:27-40`). The real code is in `global\thirdparty\kors_async\async\`. Only `ChangedNotify`/`ChangedNotifier`/`NotifyList` (`changednotify.h`, `notifylist.h`) are Muse-native.

### 2.1 `Channel<T...>` — typed multicast signal (`kors_async\async\channel.h:35`)

```cpp
// Channel<T...> (kors_async channel.h:77-135) — value-semantic handle; copies SHARE one Data (shared_ptr)
void send(const T&... args);                                         // fire to all receivers
template<typename Func>
void onReceive(const Asyncable* receiver, Func f, Mode mode = Mode::SetOnce);
void close();   void onClose(const Asyncable*, Func, Mode = SetOnce);
bool isConnected() const;                                            // any enabled receiver?
void disconnect(const Asyncable* a);
```

- A `Channel` is a **handle**: copy-constructing/assigning shares state (`m_data` shared_ptr, `:51,:64-75`). Returning a `Channel` by value from an interface (`IActionsDispatcher::preDispatch()`) hands out a live alias to the owner's channel — which is exactly how subscriptions work.
- **`send` is cross-thread-aware** (`ChannelImpl::sendAuto`, `channelimpl.h:480`): receivers registered on the **sender's own thread run synchronously inline**; receivers registered from **other threads** get the args copied into a per-(sender-thread→receiver-thread) queue and run **when that thread pumps** (`processMessages`). The thread a receiver lives on = the thread that called `onReceive`. `SendMode::Queue` (queue even for same-thread receivers) exists in `ChannelImpl` but `Channel::send` always uses `Auto`.
- **`send` is a no-op when nobody is subscribed** (`ChannelImpl::send` early-return `!isConnected()`, `:598`).
- **Args are copied** into a `std::tuple<std::decay_t<T>...>` for cross-thread delivery (`ReceiverCall`, `:326`) — so pointer params (`bool*` in `ICommandDispatcher::preDispatch`) only work for same-thread receivers' out-params.
- **Subscribe-safety during send:** receivers added/removed *inside* a callback go to `pendingToAdd/Remove` and are applied immediately after the loop (`receiversCall`, `:176-192`).
- `ChannelOpt` (`channelimpl.h:47`) tunes a channel: `name()`, `threads(n)` (max distinct threads), `capacity(n)` (queue size), `disableWaitPendingsOnSend()`, `disableWarnOnPendingsSendTimeout()`. `muse::async::makeOpt()` returns a default one. `Channel(const std::string& name)` is a naming shorthand.
- **Global limits** (`kors_async\async\internal\conf.cpp:28-36`): `MAX_THREADS=100`, `MAX_THREADS_PER_CHANNEL=10`, `QUEUE_CAPACITY=128`, `DO_WAIT_PENDINGS_ON_SEND=true` (a sender **blocks up to `WAIT_PENDINGS_MS=4` × `MAX_SEND_PENDINGS_ATTEMPTS=50` ≈ 200 ms** if a cross-thread queue is full, then warns), `DO_ASSERT_ON_IMPLICIT_REPLACE=true`.

### 2.2 `Asyncable` — receiver lifetime owner (`kors_async\async\asyncable.h:33`)

```cpp
class Asyncable {
    enum class Mode { SetOnce = 0, SetReplace };
    virtual ~Asyncable() { async_disconnectAll(); }   // auto-unsubscribes from EVERY channel it ever joined
    ...
};
```

- Any class that subscribes inherits `muse::async::Asyncable`. It records every `IConnectable` (channel/notification impl) it joined **plus the thread it joined from** (`async_connect`, `:73-93`); the destructor disconnects all (`:42-45,108-126`). That is why subscribe lambdas capturing `this` are safe: the `Asyncable` base dies → callbacks removed.
- ⭐ **`Mode::SetOnce` (default)**: subscribing the **same `Asyncable` to the same channel twice** is rejected, and with `DO_ASSERT_ON_IMPLICIT_REPLACE=true` it **asserts in debug** (`channelimpl.h:110-116`). One `Asyncable` ⇒ at most one callback per channel. Pass `Asyncable::Mode::SetReplace` to intentionally replace the callback.
- `receiver == nullptr` is allowed for fire-and-forget subscriptions that can never be auto-removed (used e.g. in `Promise::onResolve(nullptr, …)`, `icommanddispatcher.h:139`).
- Subscribing from thread A and unsubscribing from thread B is handled by posting a "remover" message to thread A (`disconnect`, `channelimpl.h:664-701`).

### 2.3 `Notification` (`kors_async\async\notification.h:29`)

A `Channel<>` with no payload: `notify()`, `onNotify(Asyncable*, f, mode)`, `close/onClose`, `isConnected`, `disconnect`, `key()`. Copies share one channel. Idiom: `async::Notification fooChanged() const` on an interface; owner holds a member and calls `notify()`.

### 2.4 `Async::call` and `Promise<T...>`

- **`Async::call(const Asyncable* caller, Func f, const std::thread::id& th = this_thread)`** (`kors_async\async\async.h:190`) — post `f` to run on thread `th` (default: the **current** thread, i.e. "run later on this thread's next pump"). If `caller` is non-null, the call is dropped if that `Asyncable` has been destroyed/disconnected (`d->isConnected(m.receiver)`, `:121-125`). The 2-arg overload `call(caller, f, arg1, th)` binds one argument.
- **`Promise<T...>`** (`kors_async\async\promise.h:43`): JS-style. Construct with `make_promise<T...>(body)`; `body` receives `Resolve` and `Reject`, and **must `return resolve(val...)` or `return reject(code, msg)`** — the `[[nodiscard]] Result` return type is a compile-time "resolve-or-reject exactly once" enforcement (`:49-106`). `PromiseType::AsyncByPromise` (default) runs the body via `Async::call` (deferred); `AsyncByBody` runs it inline and the body decides when to resolve (e.g. after a network call). Consumers: `.onResolve(Asyncable*, cb)`, `.onReject(Asyncable*, cb(int code, const std::string&))`, `.then<U...>(…)` chaining (`:184-215`). `onReject` **asserts** if the promise was built with the resolve-only body (`:197-199`).
- Example from the command dispatcher (`rcommand\internal\commanddispatcher.cpp:46-48`):
  ```cpp
  return async::make_promise<Response>([request](auto resolve) {
      return resolve(make_response(request, make_ret(Ret::Code::Ok)));
  });
  ```

### 2.5 The pump — nothing cross-thread happens without it

Queued deliveries (Channel cross-thread, `Async::call`, Promise bodies) only run when someone calls **`async::processMessages(thisThreadId)`**. `GlobalModule::onPreInit` starts a `Ticker`: `m_asyncTicker.start(1, [thisThId]{ async::processMessages(thisThId); }, Ticker::Mode::Repeat)` (`globalmodule.cpp:226-230`) — i.e. the **main/GUI thread is pumped every ~1 unit of the ticker provider**; worker threads (audio, etc.) must pump their own queues if they receive. `BaseApplication::finish` does a final `async::processMessages()` before tearing contexts down (`baseapplication.cpp:443`), and `GlobalModule::onDeinit` calls `async::terminate()` (sets `conf::terminated`, after which sends/disconnects across threads silently no-op, `channelimpl.h:667`).

### 2.6 Derived helpers

| Type | File | Purpose |
|---|---|---|
| `ChangedNotify<T>` / `ChangedNotifier<T>` | `async\changednotify.h` | collection-change signals: `onChanged()`, `onItemChanged/Added/Removed(const T&)`, `onItemReplaced(old,new)`. Consumers hold `shared_ptr<ChangedNotify<T>>`; the owner holds the `ChangedNotifier<T>` and calls `changed()/itemAdded(item)/…`. |
| `NotifyList<T>` | `async\notifylist.h` | `std::vector<T>` + a `ChangedNotify<T>`; forwards `onChanged/onItem*`; **`assert(m_notify)`** if no notify was set. |
| `ValCh<T>`, `ValNt<T>` | `types\retval.h:116,124` | value + channel / value + notification with `set(v)` = assign + signal. |
| `RetValCh<T>`, `RetCh<T>` | `types\retval.h:100,110` | `Ret`+value+channel bundles. |
| `Progress` | `progress.h:37` | shared-data progress object: `start()`, `progress(cur,total,msg)` (de-dups identical values), `finish(ProgressResult)`, `cancel()`, plus `started()/progressChanged()/finished()/canceled()` channels. `ProgressResult = RetVal<Val>`. Used by `INetworkManager`, `ILanguagesService::update`, `IAppUpdateService::downloadRelease`. |
| `Timer` / `Ticker` | `timer.h`, `ticker.h` | periodic callbacks; `Timer::onTimeout(Asyncable*, cb)` |

**Idiom summary:**
```cpp
class Foo : public muse::async::Asyncable {
    muse::GlobalInject<ISomething> something;                 // IoC (see architecture-and-ioc.md)
    void init() {
        something()->changed().onReceive(this, [this](const Val& v) { ... });   // channel
        something()->listChanged().onNotify(this, [this]() { ... });            // notification
        something()->fetch().onResolve(this, [this](const Ret& r) { ... });     // promise
    }
};
```

---

## 3. `Settings` (`global\settings.h` / `.cpp`)

Singleton wrapping `QSettings` (INI format on non-macOS, `settings.cpp:57-61`) with typed `Val`s, defaults, and **per-key change channels**. Accessor: `muse::settings()` (`settings.h:134`).

```cpp
// settings.h:53-110 (abridged)
struct Key  { std::string moduleName; std::string key; };      // map key; NOTE only `key` is persisted
struct Item { Key key; Val value; Val defaultValue; std::string description;
              bool canBeManuallyEdited; Val minValue, maxValue; };
const Val& value(const Key&) const;           const Val& defaultValue(const Key&) const;
void setSharedValue(const Key&, const Val&);  // persist + broadcast to other app instances (multiwindows)
void setLocalValue (const Key&, const Val&);  // persist, NOT synced
void setDefaultValue(const Key&, const Val&); // registers the key + default (ALSO fixes the Val type)
async::Channel<Val> valueChanged(const Key&) const;
void beginTransaction(bool notifyOthers = true); void commitTransaction(...); void rollbackTransaction(...);
void reset(bool keepDefaultSettings=false, bool notify=true, bool notifyOthers=true);
```

**Standard usage pattern** (a module's `*Configuration::init()`; real example `shortcuts_v2\internal\shortcutsconfiguration.cpp:34-43`):
```cpp
static const muse::Settings::Key CURRENT_SHORTCUTS_PRESET_KEY("shortcuts", "shortcuts/currentPreset");
settings()->setDefaultValue(KEY, Val(""));                                 // register + default
settings()->valueChanged(KEY).onReceive(this, [this](const Val& v) { ... }); // react to changes
std::string s = settings()->value(KEY).toString();                         // read
settings()->setSharedValue(KEY, Val("x"));                                 // write (persist + notify)
```

How it behaves (`settings.cpp`):
- **`setLocalValue`** (`:209-232`): no-op if value unchanged; writes through to `QSettings` **only if no transaction is open** (`:217-219`); updates the in-memory `Item`; **sends on the key's channel** (if anyone subscribed). `setSharedValue` = `setLocalValue` + `multiwindowsProvider()->settingsSetValue` (`:199-207`, only `#ifdef MUSE_MODULE_MULTIWINDOWS`).
- **Transactions:** `beginTransaction` copies `m_items → m_localSettings` and `items()` then returns the local copy (`:297-305, 74-77`); `commitTransaction` writes the diffs to disk (`:315-331`). Used by Preferences dialogs for Apply/Cancel.
- **Load:** `load()` = `readItems()` over `QSettings::allKeys()` (`:91-94, 172-187`) — every key read from disk gets an **empty `moduleName`**; `setDefaultValue` later attaches the default/type. `compat_QVariantToVal` (`:148-170`) maps `QByteArray`/`QDateTime`/`QStringList` → `Val`.
- **`reset`** (`:96-146`): clears `QSettings`, deletes `AppLocalDataLocation` unless `keepDefaultSettings`, then re-publishes every changed key's default through its channel (so UI refreshes).
- The file path is `Settings::filePath()` (= `QSettings::fileName()`, registered as the "settings file" diagnostics path, `globalmodule.cpp:244`).
- **Portable Windows build** (`WIN_PORTABLE`) redirects the settings dir to `…/Data/settings` (`:52-55, 245-246`).

### 3.1 `Val` — the variant (`global\types\val.h:46`)

`Val::Type { Undefined, Bool, Int, Int64, Double, String, List, Map, Color }`. Constructors are `explicit` (bool/int/int64/double/`number_t`/`std::string`/`const char*`/`io::path_t`/`ValList`/`ValMap`/enums/`QString`/`QColor`). Getters: `toBool/toInt/toInt64/toDouble/toFloat/toString/toPath/toList/toMap/toEnum<E>()` + Qt `toQString/toQColor/toQVariant/fromQVariant`. Typed helpers `toType<T>() → std::optional<T>` and `fromType<T>(v)` support `bool,int,int64,double,number_t,string,path_t,ValList,ValMap`, **`std::vector<T>`** and **`std::map<std::string,T>`** recursively (`:101-197`) — used by rcommand typed commands (§4.4). `ValList = std::vector<Val>`, `ValMap = std::map<std::string,Val>`. `setType()` forces the stored type (used by `Settings::setDefaultValue`, `settings.cpp:260`).

---

## 4. Core types (`global\types\`) — the most-used

### 4.1 `Ret` / `RetVal` (`types\ret.h`, `types\retval.h`)

`Ret` = result code (`int`) + optional `text` + optional `std::any` data map. **`operator bool` ⇒ `success()`**; `Ret(bool)` maps true→`Ok(0)`, false→`UnknownError(1)`; default-constructed `Ret` is `Undefined(-1)` (**falsy**).

```cpp
// ret.h:41-115 — Code ranges by subsystem (value = first code of the range)
Undefined=-1, Ok=0, UnknownError=1, Cancel=3, NotSupported=4, NotImplemented=5, BadData=6, BadArgs=7, Busy=8, NotAllowed=9,
GlobalFirst=20..99 (InternalError=21)   UiFirst=100  ExtensionsFirst=200  AudioFirst=300  SystemFirst=400  NetworkFirst=500
MidiFirst=600   LanguagesFirst=700   NotationFirst=1000..1299   ConverterFirst=1300   VstFirst=1400   WorkspaceFirst=1500
LearnFirst=1600 UpdateFirst=1700     CloudFirst=1800   EngravingFirst=2000..2999   ProjectFirst=3000..3999
DiagnosticsFirst=4000   MuseSoundsFirst=5000
```

Helpers: `make_ok()`, `make_ret(Code[, text])`, `make_ret(int, text)`, `check_ret(ret, Code)`. `ret.data<T>(key, default)` asserts on type mismatch (`:131-161`). Each module defines its own `*errors.h` enum inside its range (`clouderrors.h`, `networkerrors.h`, `updateerrors.h`, …) — **pick codes from your module's range**.

`RetVal<T>{Ret ret; T val;}` with `make_ok(v)` / `make_ret(code, text)`; `RetVal2<T1,T2>`; `RetValCh`, `RetCh`, `ValCh`, `ValNt` (see §2.6).

### 4.2 `Uri` / `UriQuery` (`types\uri.h:33,74`)

`Uri{scheme, path}` (`Uri("command://notation/select/target")`, `scheme()`, `path()`, `pathSegments()`, `addPath`, `toLocalFile`/`fromLocalFile`). `UriQuery` = `Uri` + `std::map<string, Val> params` (`set(key,val)` chainable, `param(key, def)`, `contains`, `toString()` ⇒ `uri?k=v&…`). **Both are the identity types of the command system**: `ActionUri = Uri`, `ActionQuery = UriQuery` (`actiontypes.h:36-37`), `rcommand::Command = Uri`, `CommandQuery = UriQuery` (`commandtypes.h:38-39`). `std::hash<Uri>` is provided.

### 4.3 Strings & translation

- `muse::String` (`types\string.h`) — UTF-16 string (Qt-free); `AsciiStringView`, `StringList`; `String::fromUtf8/fromQString`, `toStdString`, `toQString`.
- `TranslatableString{context, str, disambiguation, n}` (`types\translatablestring.h:36`) — **write `TranslatableString("ctx", "text")` literally so `lupdate` sees it** (comment `:34-35`); `.translated()` / `.qTranslated()`; `untranslatable("x")` for dynamic text; `.arg(...)` chains. `MnemonicString` (`types\mnemonicstring.h`) = title with `&` mnemonic (ADR `00106`). Back-end functions: `mtrc/qtrc` in `translation.h`.
- `io::path_t` (`io\path.h:38`) — `std::string`-backed path with `+`/`appendingComponent`; free functions `suffix/filename/basename/completeBasename/dirpath/absolutePath/isAbsolute/isAllowedFileName/escapeFileName/toNativeSeparators`. **Always use `path_t`**, not raw `QString`, in framework APIs.
- Other value types: `ByteArray` (non-Qt byte buffer), `DateTime`, `Version`, `Color`/`rgba.h`, `Flags` (`DECLARE_FLAGS`/`DECLARE_OPERATORS_FOR_FLAGS`, `flags.h`), `Id`, `Ratio`, `Secs`, `number.h` (`number_t<T>` strong numeric), `SharedMap/HashMap`.

---

## 5. I/O & serialization

### 5.1 `IFileSystem` (`global\io\ifilesystem.h:34`) — injectable, **global** interface

The abstraction everything uses instead of `QFile`/`QDir` (so tests can swap in a mock and WASM gets `MemFileSystem`, `globalmodule.cpp:107-111`). Methods: `exists/remove/clear/copy/move/makePath/makeLink`, `entryType`, `fileSize/availableSpace` (`RetVal<uint64_t>`), `scanFiles(rootDir, filters, ScanMode)` → `RetVal<paths_t>`, `setAttribute(Hidden)`, `readFile(path)`→`RetVal<ByteArray>` / `readFile(path, ByteArray&)`, `writeFile`, streaming `openStream/writeToStream/closeStream` (`StreamId`), `temporaryDirectoryPath`, `canonicalFilePath/absolutePath/absoluteFilePath/cleanPath`, `isSymLink/symLinkTarget`, `birthTime/lastModified`, `isWritable`. Enums in `ioenums.h`: `OpenMode`, `ScanMode`, `EntryType`.

### 5.2 Device classes

`IODevice` (`io\iodevice.h:31`: `open(OpenMode)/close/read/readAll/write/seek/pos/size/meta/hasError/errorString`) is the base of `io::File` (`file.h:31`) and `io::Buffer` (`buffer.h:30`, in-memory). `Dir` (`dir.h`), `FileInfo` (`fileinfo.h`) and `FileSystemWatcher` (`filesystemwatcher.h`, wraps `QFileSystemWatcher`) are static/value helpers.

### 5.3 Serialization

| File | Class | Notes |
|---|---|---|
| `serialization\json.h` | `JsonValue`/`JsonArray`/`JsonObject`/`JsonDocument` | `JsonDocument::fromJson(ByteArray, std::string* err)` / `toJson(Format)`; `JsonValue::toVal()/fromVal(Val)` bridges to `Val` |
| `serialization\xmlstreamreader.h` | `XmlStreamReader` | **the reader behind `.mscx`/`.mss`** — `readNextStartElement()`, `readNext()`, `tokenType()`, `readText/readInt/readDouble`, attributes (see `file-format-and-rw.md`) |
| `serialization\xmlstreamwriter.h` | `XmlStreamWriter` | `startDocument/startElement(name, attrs)/endElement/element(name[, attrs][, body])/comment`, `escapeString` (`:52-71`) |
| `serialization\xmldom.h` | `XmlDomDocument/Node/Element/Attribute` | small DOM API (handle-based) |
| `serialization\zipreader.h` / `zipwriter.h` | `ZipReader`, `ZipUnpack`, `ZipWriter` | **the `.mscz` container**: `ZipReader(path or IODevice*)`, `fileExists`, `fileData(name)`, `fileInfoList()`; `ZipUnpack::unpack(zip, dir)` (`zipreader.h:60-72`) |
| `serialization\textstream.h` | `TextStream` | text I/O over an `IODevice` |
| `serialization\msgpack.h` | `kors::msgpack` `Packer/UnPacker/Cursor` (aliases in `msgpack_forward.h:31-33`) | binary pack/unpack used by RPC-style code |

---

## 6. The command/dispatch triad

### 6.1 Relationship at a glance

```
 QML / menus / shortcuts / MCP / extensions
          |
          v                                     (legacy)                         (new, ADR 00101/00104/00107)
 UiActionsRegister (ui\)              IActionsDispatcher  ----- "command://..." code or scheme ----->  ICommandDispatcher
   UiAction{code, uiCtx, scCtx,        dispatch(ActionCode[,ActionData])                                   dispatch(Command[,Params])
   title,...}  (ui\uiaction.h:57)       reg(Actionable*, code, cb)                                         onRequest(Commandable*, cmd, cb)
                                                                                                             -> async::Promise<Response>
 shortcuts_v2: Shortcut{command, scope, sequences}  --activate(seq)--> ShortcutsController --> ICommandDispatcher
 ICommandsRegister (static CommandInfo per module)   ICommandsState (enabled/checked per command)   [two separate services]
```

⭐ **ADR `00101`** (`muse\docs\adr\00101-…`): commands exist because actions (1) can't return data, (2) have no input schema, (3) give no result/ack — all needed for an **MCP server** — and because action *availability/context* logic was "blurred across different services". Differences: commands **return a result**, **are asynchronous**, **carry an input schema**, have **no context baked in** (availability is in one class; shortcut scope lives in the shortcut config), **register and state are separate services**. ADR `00104` **"command-first"**: only a *command* (never a query-with-params) is the key for info/state/shortcut; dynamic items become distinct commands (`command://workspace/select/<name>` not `…?name=`); **query params carry data only, never identity**. ADR `00107`: each `CommandInfo` has an `Availabilities` bit set (see §6.3). ADR `00108`: shortcut **scope** replaces context (§6.5).

### 6.2 Legacy: `actions` module

Files: `actions\iactionsdispatcher.h`, `actionable.h`, `actiontypes.h`, impl `actions\internal\actionsdispatcher.*`.

- **`ActionCode` = `std::string`**, `ActionCodeList`, `ActionUri = Uri`, `ActionQuery = UriQuery` (`actiontypes.h:33-37`).
- **`ActionData`** (`actiontypes.h:49`): positional type-erased args. `make_arg1/2/3(...)`, `setArg<T>(i, v)`, `arg<T>(i)`. ⛔ `arg<T>` does an unchecked `reinterpret_cast<Arg<T>*>` (`:94`) — asking for the wrong `T` is **undefined behavior**, not an error; `m_args.at(i)` throws on out-of-range. `setArg` uses `insert(begin()+i)` so arg indices must be set in order.
- **`Actionable`** (`actionable.h:28`) — base for handlers: `canReceiveAction(code)` (default `true`; override to gate), and its dtor auto-`unReg`s from the dispatcher.
- **`IActionsDispatcher`** (`iactionsdispatcher.h:33`, `MODULE_CONTEXT_INTERFACE` = per-window context): `dispatch(code)`, `dispatch(code, data)`, `dispatch(ActionQuery)`; `reg(Actionable*, code|query, cb)` with many convenience overloads (void cb, `(code)`, `(data)`, `(code,data)`, member-function pointers; `:59-115`); `unReg`, `isReg`, `actionList()`; channels `preDispatch()`, `postDispatch()`, `notRegistered()`.
- **Dispatch algorithm** (`actionsdispatcher.cpp:52-152`):
  1. If the code parses as a valid `ActionQuery` (it's a URI) → `dispatch(query)` (asserts `data` empty).
  2. **If the code starts with `rcommand::COMMAND_SCHEME` (`"command"`) → `commandDispatcher()->dispatch(rcommand::Command(actionCode))`** (`:64-67`); likewise a query whose scheme is `command` (`:82-85`). *This is the action→command bridge: old call sites keep working when the code is a command URI.*
  3. Look up `m_clients[code]`; if absent → `LOGW "not a registered action"` + `notRegistered` signal.
  4. For each client where `canReceiveAction(code)` is true: send `preDispatch`, **call**, count; after the loop send `postDispatch` once. >1 handler ⇒ `LOGW "More than one client can handle the action, this is not a typical situation"`.
  - A query action is stored under `query.toString()`; dispatch tries the **full query string first, then bare uri** (`:88-101`); the handler receives the query re-serialized in `ActionData` arg 0.
- `rcommand\actiontocommand.h` — compat shim: `ActionToCommand{actionCode, command, convertor}` table + `registerActionToCommand(actionable, table, commandDispatcher, actionsDispatcher)` registers each legacy action code with a lambda that turns `ActionData` into a `CommandQuery` (via `make_conv({{"path", param<io::path_t>}})` etc.) and dispatches it (`:81-94`). This is how old action codes (`"file-open"`) route into new commands.

### 6.3 New: `rcommand` module (`muse\framework\rcommand\`)

**Data types** (`commandtypes.h`):

| Type | Line | Meaning |
|---|---|---|
| `COMMAND_SCHEME` | `:37` | `"command"` |
| `Command` = `Uri`, `CommandQuery` = `UriQuery` | `:38-39` | identity (`command://notation/select/target`) |
| `NONE_COMMAND` | `:42` | `command://none` — a valid no-op (always enabled; dispatch resolves `Ok`). Used to **explicitly unbind** a shortcut (resolver prefers non-NONE on ties, `shortcutresolver.cpp:130`) |
| `Params` | `:44` | `std::map<string,Val>` w/ `set`, `at(key, def)`, `contains` |
| `Request{callId, command, params}` / `Response{callId, ret, result: Params}` | `:271-283` | `make_request()` allocates a process-unique `callId` (`:285`); `make_response(req, ret[, result])` |
| `Arg{description, dataType, presence, allowedValues, min, max}`, `ArgsSchema`/`InputSchema`/`OutputSchema` | `:135-167` | JSON-schema-like arg description (what MCP/extension tooling reads). `DataType {Undefined,Bool,Int,Int64,Float,Double,String,Enum,List,Map}`, `Presence {Required,Optional}` |
| `Decoration{iconCode, iconColor, checkable}` | `:174` | UI hints |
| `Availability` flags | `:192-202` | `Disabled=0`, `Dispatch=1`, `Shortcut=2`, `Extension=4`, `MCP=8`; composites `UI = Dispatch|Shortcut`, `API = Extension|MCP`, `All` |
| `Annotation` flags | `:207-212` | MCP hints: `ReadOnly`, `Destructive`, `Idempotent`, `OpenWorld` |
| `CommandInfo{command, title(MnemonicString), description, inputSchema, outputSchema, decoration, availabilities, annotations}` | `:216` | static metadata for one command |
| `CommandState{enabled, checked}` | `:246` | dynamic state |

⚠ **Default drift vs ADR `00107`:** the ADR says availabilities default to **`Disabled`** ("to ensure the flag is specified explicitly"), but the code defaults `availabilities = Availability::All` with the comment *"All temporarily… needs to be replaced with Availability::Disabled"* (`commandtypes.h:224`), and `annotations` defaults to `Destructive` (`:225`). Treat the ADR as the *intent*; the tree is mid-migration.

**Services** (registered in `rcommand\rcommandmodule.cpp`):

| Interface | Scope | Role |
|---|---|---|
| `ICommandsRegister` (`icommandsregister.h`) | **global** (`MODULE_GLOBAL_INTERFACE`, `RCommandModule::registerExports` `:38-41`) | modules `reg(IModuleCommandsRegisterPtr)`; `commandInfoList()`, `commandInfo(cmd)`, `commandInfoListChanged()`, `commandModuleName(cmd)` |
| `ICommandsState` (`icommandsstate.h`) | **per-context** (`RCommandContext::registerExports` `:56-59`) | modules `reg(IModuleCommandsStatePtr)`; `commandState(cmd)`, `commandStateChanged()` channel `<Command, CommandState>`. Caches per command in `m_cache`; the cache entry is refreshed **only** when the owning module emits `commandStateChanged` (`commandsstate.cpp:43-46,100-103`) — stale state ⇒ the module forgot to emit |
| `ICommandDispatcher` (`icommanddispatcher.h:33`) | **per-context** | `dispatch(Request) → async::Promise<Response>`; handlers via `onRequest(Commandable*, cmd, cb)`; `preDispatch()` = `Channel<Command, bool* allow>` (a listener may veto → `Ret::Code::Cancel`), `postDispatch()` |
| `IModuleCommandsRegister` / `IModuleCommandsState` | per module | the module-supplied halves: `moduleName()`, `commandList()`, `commandInfoList()`, `commandListChanged()` / `commandState(cmd)`, `commandStateChanged()`, `init/deinit` |
| `Commandable` (`commandable.h`) | — | base class for handlers; dtor auto-`unreg`s (mirror of `Actionable`) |

**ADR `00101` module recipe** — each module supplies: (1) `<module>commands.h` constants (e.g. `workspace\workspacecommands.h:28`: `inline static const Command WORKSPACE_SELECT_COMMAND("command://workspace/select")`), (2) an `internal\<module>commandsregister.*` (static `CommandInfo` list) registered with `ICommandsRegister`, (3) an `internal\<module>commandsstate.*` registered with `ICommandsState`, (4) a controller deriving `Commandable` that calls `commandDispatcher()->onRequest(...)`. Real wiring examples: `src\notationscene\internal\notationactioncontroller.cpp` (~12 `onRequest` registrations around `:2910-3126`; the generic helpers there take `enabler`/`handler` lambdas), `appshellcommandscontroller.cpp`, `projectactionscontroller.cpp`.

**Dispatch algorithm** (`rcommand\internal\commanddispatcher.cpp:43-94`):
1. `NONE_COMMAND` → resolved `Ok` promise.
2. Command not in `m_clients` → `LOGW "command not registered"` and resolve `Ret::Code::UnknownError`.
3. Otherwise a `Promise<Response>` whose body: sends `preDispatch(command, &allowDispatch)` (veto ⇒ `Cancel`), then either calls the **sync** `callback(request)` and resolves its `Response`, or the **async** `asyncCallback(request, onResponse)` (resolution deferred until the handler calls `onResponse`; `postDispatch` fires then). A handler's `Response` is **immediate for sync handlers**, but the *caller* sees a `Promise` — the body runs via `PromiseType::AsyncByPromise` default ⇒ **deferred to the next pump** (so a `dispatch()` is not synchronous unless you pass `AsyncByBody`; there is also a **blocking** `CommandDispatcher::dispatch(const Command&, const Params&) → Response` member on the impl, `:96-115`, not on the interface — it only supports sync callbacks).
4. **Only one handler per command**: `reg()` rejects a duplicate (`LOGW "command already registered"`, `:134-137`) — unlike actions, which allow several clients.

**Handler registration overloads** (`icommanddispatcher.h:49-133`):
- raw: `onRequest(client, cmd, CallBack{Response(const Request&)})` / `AsyncCallBack{void(const Request&, OnResponse)}`
- convenience: `CallBackRet{Ret()}`, `CallBackParamsRet{Ret(const Params&)}`, `CallBackParamsPromiseRet{Promise<Ret>(const Params&)}`
- **typed**: `onRequest<C>(client, std::function<Ret(const C&)>)` and, for commands with `Output`, `std::function<RetVal<C::Output>(const C&)>` — the dispatcher converts `Params → C` (via `fromInputParams`, returning `BadArgs` on missing/invalid params) and the result back to `Params` (`toOutputParams`). Caller side: `dispatch(const C& command)` builds params from the struct.

⚠ **Passing a lambda to `onRequest` with the wrong shape can be ambiguous across the overloads** — the notation controller disambiguates by wrapping (`AsyncCallBack(...)`, `CallBack(...)`, `icommanddispatcher.h:88`). If you get an overload error, give the lambda an explicit signature.

### 6.4 Typed commands (`rcommand\typedcommand.h`)

A **compile-time command description**: a struct with `static` `id`, `title`, `description` (concept `TypedCommand`, `:35`), optionally `input` (a `FieldsScheme` mapping member pointers to param keys; concept `HasInput`), `Output` + `output` (`HasOutput`), `decoration`, `availabilities`, `annotations`. `makeCommandInfo<C>()` (`:397`) derives the `CommandInfo` incl. the schema; **`static_assert(!HasAvailability<C>)`** catches the typo `availability` vs `availabilities` (`:413`). Params ⇄ struct: `toInputParams/fromInputParams(params, c, ConvertParamsPolicy)` with policies `StrictCompliance` (default: unknown param = error), `WarningUnknown`, `IgnoreUnknown`; result `ConvertParamsResult{ret, unknownParams, missingParams, invalidParams}` (`:76-132`). Enums in fields convert via a user-provided `EnumContract<E>` (`rcommand\enumcontract.h`: `str_val/enum_val/str_vals`, `typedcommand.h:155-156`); min/max bounds are NaN-safe (`isWithinBounds`, `:259-272`).

Real example (`src\notationscene\notationcommands.h:46-60`):
```cpp
struct SelectTargetCommand {
    sci_auto id = Command("command://notation/select/target");
    sci_auto title = MnemonicString("action", "Select");
    sci_auto description = TranslatableString("action", "Select items by target");
    sci_auto availabilities = Availability::API;
    sci_auto annotations = Annotation::Idempotent;
    SelectionTarget target = SelectionTarget::FirstItem;
    PlayMode playMode = PlayMode::NoPlay;
    sce_auto input = FieldsScheme {
        Field{ "target", &SelectTargetCommand::target, "Selection target" },
        Field{ "play-mode", &SelectTargetCommand::playMode, "Play mode", Presence::Optional },
    };
};
```
The file's own comment (`:40`): *"A few typed commands as an example. Eventually, all commands should be made typed."* — i.e. today **most** notation commands are still plain `Command("command://…")` constants; typed commands are the exception (only `notationcommands.h` + tests use `typedcommand.h`).

⚠ ADR-`00104` naming: commands are `command://<module>/<verb>[/<detail>]`; **don't** put identity in params.

### 6.5 Shortcuts: v2 (live) and v1 (legacy)

**Build selection:** `CMakeLists.txt:199` `set(MUSE_MODULE_SHORTCUTS_V2 ON)` → `muse\framework\CMakeLists.txt:116-121` adds `shortcuts_v2` instead of `shortcuts`. Public headers in `shortcuts\` forward to `shortcuts_v2\` under `#ifdef MUSE_MODULE_SHORTCUTS_V2` (`shortcutstypes.h:26`, `ishortcutscontroller.h:26`, `shortcutcontext.h:26`). Both READMEs are one line.

**v2 data model** (`shortcuts_v2\shortcutstypes.h:39`):
```cpp
struct Shortcut {
    rcommand::Command command;                  // KEY (was `action` string in v1)
    std::string scope;                          // ADR 00108 — replaces v1 `context`
    std::vector<std::string> sequences;         // e.g. "Ctrl+S" (QKeySequence portable text)
    QKeySequence::StandardKey standardKey = UnknownKey;
    bool autoRepeat = true;
};
using ShortcutList = std::vector<Shortcut>;     // (v1 used std::list)
```
Helpers: `needIgnoreKey(Qt::Key)` (pure modifiers), `correctKeyInput` (Backtab→Shift+Tab, strip keypad modifier), `sequencesToNativeText`, `canShortcutsConflict(a,b) = (scope1==scope2)` (`:126`; v1 used `areContextPrioritiesEqual` with `"any"`).

**Services:**

| Interface (shortcuts_v2) | Scope | Role |
|---|---|---|
| `ICommandShortcutsRegister` (`icommandshortcutsregister.h:36`) | **global** | `shortcuts()`, `updateShortcuts`, `resetShortcuts`, `shortcutsChanged`, `setAdditionalShortcuts(ctx, list)`, **`shortcutsForSequence(seq)`**, `shortcut(command)`, `defaultShortcut(command)`, import/export JSON, **presets** (`availablePresets/currentPresetName/…/deletePreset`), `reload(onlyDef)` for testflow |
| `IShortcutsController` (`ishortcutscontroller.h`) | per-context | `activate(sequence)`, `isRegistered(sequence)`; `active()/setActive()/activeChanged()` |
| `IShortcutsResolver` (`ishortcutsresolver.h:30`) | per-context, **implemented by the APP** | `selectActive(list) → Shortcut` (invalid if none active) + `scopeTitle(code)` |
| `IShortcutsConfiguration` | global | paths/presets |

**Where the data lives:** `src\app\configs\shortcuts.cfg` (JSON; resource `:/configs/shortcuts.cfg`) names the shipped files — `command_shortcuts` → `:/configs/data/shortcuts.json`, `…_mac.json`, `…_azerty.json`, preset list `["shortcuts_azerty"]`. (The v1 XML entries `shortcuts*.xml` are still listed but belong to v1.) User overrides: `<userAppData>/shortcuts/<name>.json` (`shortcutsconfiguration.cpp:111-123`). `CommandShortcutsRegister::reload` (`commandshortcutsregister.cpp:73-135`) reads the default file, applies the **platform diff** (mac) and **preset diff** on top, **appends every remaining registered command without a shortcut**, then merges the user file and additional (extension/etc.) shortcuts; reload also fires when the multiwindows resource `"CommandShortcuts"` changes (`:41,61`).

**Activation flow** (`shortcutscontroller.cpp:45-78`):
1. Ignored if `!m_active`. (`init()` subscribes to `interactive()->currentUri()`: shortcuts are **disabled whenever the top window is a QWidget dialog** — `setActive(!interactive()->topWindowIsWidget())`, `:39-42`.)
2. `commandShortcutsRegister()->shortcutsForSequence(seq)` → candidates.
3. **Filter** by `commandsState()->commandState(cmd).enabled` — first-level conflict resolution.
4. If >0 left: `shortcutsResolver()->selectActive(list)` (or `front()` if no resolver registered).
5. If the chosen shortcut is valid → `commandDispatcher()->dispatch(shortcut.command)`.
   `SHORTCUTS_DEBUG 1` is **hard-coded on** (`:26`), so every key press logs `[SC] …` at INFO.

**App-level scope priority** (`src\appshell\internal\shortcutresolver.cpp:58-81`; registered in `appshellmodule.cpp:124`):

| Scope | Priority |
|---|---|
| `NAVIGATION` | 1 |
| `PLAYBACK` | 2 |
| `NOTATION_PAGE` | 3 |
| `NOTATION` | 4 if notation panel (`"ScoreView"`) focused else −1 |
| `NOTATION_TAB` | 5 if focused & tab staff |
| `NOTATION_SELECTION` | 6 if focused & has selection |
| `NOTATION_SINGLE_SELECTION` / `NOTATION_RANGE_SELECTION` | 7 |
| `NOTATION_NOTE_INPUT` | 8 |
| `NOTATION_TEXT_EDITING` | 9 |
| unknown scope (`PROJECT`, `APP`, `PALETTE`, `INSTRUMENTS`, `EXTENSIONS`, `AUDIO`, `DOCK`, `WORKSPACE`, `UPDATE`, `DIAGNOSTICS`, `TESTFLOW`) | `0` + `LOGD "Unknown scope"` (`:79`) |

`selectActive` (`:119-140`): **`stable_sort`** by scope priority descending (ties between different scopes by priority; same-scope ties put non-`NONE_COMMAND` first); picks `front()`; **if its priority is < 0 (inactive scope) returns an invalid `Shortcut`** (nothing dispatches). Note the ADR sample differs from the shipped map (ADR: `PLAYBACK` conditional on `isPlaybackAllowed()`; code: constant 2) — **code wins**.

**v1 (legacy, not built):** `shortcuts\ishortcutsregister.h` (`Shortcut{action(string), sequences, context, standardKey, autoRepeat}`, `ShortcutList = std::list`, XML `shortcuts.xml`), contexts `CTX_DISABLED/ANY/PROJECT_OPENED/PROJECT_FOCUSED/NOT_PROJECT_FOCUSED` and `IShortcutContextPriority::hasLowerPriorityThan` (`shortcutcontext.h:42-57`). `UiAction.scCtx` (`ui\uiaction.h:62`) still carries a v1 shortcut context string per UI action.

### 6.6 `UiAction` (ui module, still action-keyed)

`ui\uiaction.h:57`: `UiAction{code, children, uiCtx, scCtx, title, description, iconCode, iconColor, checkable}` + `UiActionState{enabled, checked}` — the *legacy UI-facing* descriptor registered with `UiActionsRegister` (`ui\internal\uiactionsregister.h:37`). `uiCtx` = when the action is *available* (e.g. `UiCtxProjectOpened`), `scCtx` = when its *shortcut* fires — the two contexts ADR `00101` calls confusing. The menu/toolbar models (`uicomponents\…\abstractmenumodel.h`, `abstracttoolbarmodel.h`) have **both** code paths behind `#ifdef MUSE_MODULE_ACTIONS_SUPPORT` / `MUSE_MODULE_SHORTCUTS_V2`. See `ui-qml-mvvm.md`.

⭐ **Which one for new code?** Commands (`rcommand`) + `shortcuts_v2` scopes. ADR `00101` states "the new command system will completely replace actions"; `notation-layer.md` documents `NotationActionController` as the command handler.

---

## 7. Service modules overview

| Module | Key interface(s) (scope) | What it does |
|---|---|---|
| **`workspace`** | `IWorkspaceManager` (context): `defaultWorkspace()`, `currentWorkspace()`, `workspaces()`, `cloneWorkspace`, `changeCurrentWorkspace(name)`, `createAndAppendNewWorkspace`, `openConfigureWorkspacesDialog`, `currentWorkspaceChanged()`/`…AboutToBeChanged()` · `IWorkspace`: `name`, `isBuiltin`, `isEdited`, `rawData(DataKey)`/`setRawData(key, QByteArray)`, `reset`, `reloadNotification` · `IWorkspacesDataProvider`, `IWorkspaceConfiguration` | Persisted UI layouts (palettes, toolbars, dock arrangement stored as opaque `QByteArray` blobs keyed by `DataKey`). Commands in `workspacecommands.h:28-30` (`command://workspace/select`, `/configure`, `/create`). Ret range `WorkspaceFirst=1500`. Shipped workspaces under `share\workspaces`. |
| **`cloud`** | `IAuthorizationService` (context): `signUp/signIn/signOut`, `userAuthorized()` (`ValCh<bool>`), `accountInfo()`, `cloudInfo()`, `checkCloudIsAvailable()` → `Promise<Ret>`; sub-dirs `musescorecom\` and `audiocom\` (two cloud providers); `icloudconfiguration.h`, `clouderrors.h` (`CloudFirst=1800`), `qml\` | musescore.com / audio.com account login & upload services. |
| **`network`** | `INetworkManager` (non-injected; created via `inetworkmanagercreator.h`): `get/head/post/put/patch/del(QUrl, …) → RetVal<Progress>` with `IncomingDevicePtr`/`OutgoingDeviceVar` + `RequestHeaders`; `inetworkconfiguration.h`, `inetworkinformation.h`, `networkerrors.h` (`NetworkFirst=500`), `api\` | HTTP client (Qt Network) returning a `Progress` object per request. |
| **`update`** | `IAppUpdateService` (context): `checkForUpdate()` → `Promise<RetVal<ReleaseInfo>>`, `lastCheckResult()`, `downloadRelease()` → `RetVal<Progress>`, `isReleaseReadyToInstall`, `downloadedReleasePath`, `canAutoInstall`, `installUpdate`; `IAppUpdateScenario`, `IUpdateInstaller`, `IUpdateConfiguration`, `IUpdateRequestParamsProvider`, `updatecommands.h`, `updateerrors.h` (`UpdateFirst=1700`) | App self-update (check → download → install). |
| **`languages`** | `ILanguagesService` (global): `languages()`, `currentLanguage()`/`currentLanguageChanged()`, `placeholderLanguage`, `update(code)` → `Progress`, `restartRequiredToApplyLanguage()` + channel; `languagestypes.h`, `languageserrors.h` (`LanguagesFirst=700`) | UI-language packs (download/update translations; `resources\`). |
| **`extensions`** | `IExtensionsProvider`/`IExtensionsRegister`/`IExtensionInstaller`/`IExtensionSession`/`IExtensionsUiEngine`/`IExtensionContextResolver`, `extensionscommands.h`, `extensionserrors.h` (`ExtensionsFirst=200`) | Extension/plugin framework — **see `extensions-system.md`**. Its commands follow ADR 00104: `command://extension/<uri>/<action>`. |
| **`rcontrol`** | `rcontrol\mcp\McpController` (context; only `init()`s in `RunMode::GuiApp`, `rcontrolmodule.cpp:44-51`), `McpServer`, `itransport.h`, `transport\`, `bridge\` | The **MCP server** (ADR `00101`'s motivating consumer): exposes commands whose `Availability` includes `MCP`, using `CommandInfo` schemas. Not traced beyond the module entry. |
| **`learn`**, **`tours`**, **`toast`** | — | Learning-content / guided tours / toast notifications (not traced). |
| **`diagnostics`** | `IDiagnosticsPathsRegister` (modules register named dirs/files, e.g. `globalmodule.cpp:236-244`), crash reporting | dev/diagnostic tooling; `muse://diagnostics/rcommand/list` dialog (`rcommandmodule.cpp:45-48`). |
| **`interactive`**, **`accessibility`**, **`multiwindows`**, **`testing`**, **`testflow`**, **`media`** | `IInteractive` (dialogs/URIs), … | UI interaction, a11y, multi-window sync (settings/shortcuts sync), test infrastructure. See `ui-qml-mvvm.md`, `testing-and-vtest.md`. |
| **Own Guide files** | — | `ui`, `uicomponents`, `dockwindow`(+`_v2`) → `ui-qml-mvvm.md`; `audio`, `audioplugins`, `automation` → `playback-and-audio.md`; `midi`, `midiremote`, `vst`, `musesampler`, `mpe` → `midi-vst-musesampler.md`; `draw` → `layout-and-rendering.md` (paint path); `extensions` → `extensions-system.md`; `stubs` = no-op replacements for disabled modules. |

---

## Cross-subsystem dependencies

- **Leans on:** Qt (`QSettings`, `QKeySequence`, `QUrl`, `QByteArray`) — `NO_QT_SUPPORT` builds exist (the `#ifndef NO_QT_SUPPORT` guards throughout `types\`), `kors_*` vendored libs, IoC (`modularity`) — `architecture-and-ioc.md`.
- **Feeds:** every module — `Channel/Notification/Asyncable` and `Ret/RetVal` are the lingua franca (`notation-layer.md`, `playback-and-audio.md`, `file-format-and-rw.md`); `Settings` backs every `*Configuration` class; the command triad feeds `notation-layer.md` (`NotationActionController` + `notationcommands.h`), `app-shell-and-startup.md` (`appshellcommandscontroller`, `ShortcutResolver`), `ui-qml-mvvm.md` (menus/toolbars), `extensions-system.md`, `plugin-api.md` (extension-callable commands via `Availability::Extension`).
- **XML/zip serialization** here is the substrate of the `.mscx`/`.mscz` readers/writers — `file-format-and-rw.md`.

## Non-obvious gotchas

1. ⭐ **`Asyncable` + `SetOnce`** — subscribing the same object to the same channel twice **asserts in debug** (`DO_ASSERT_ON_IMPLICIT_REPLACE`, `conf.cpp:35`; `channelimpl.h:110`). One receiver per `Asyncable` per channel; use a different `Asyncable` member or `Mode::SetReplace`.
2. **Callbacks run on the *subscribing* thread**, not the sender's. Same-thread subscribers are inline/synchronous (re-entrancy!), cross-thread ones are deferred until that thread calls `processMessages`. A thread with no pump never receives.
3. **A `send` can block** up to ~200 ms when a cross-thread queue is full (`DO_WAIT_PENDINGS_ON_SEND`, 4 ms × 50), then prints `[async::channel] [warning] not all pending messages were sent` to stdout.
4. **Thread-pool exhaustion:** each channel tracks ≤10 threads (`MAX_THREADS_PER_CHANNEL`); beyond that it prints `thread data pool exhausted!!` and asserts (`channelimpl.h:415-424`). Heavy fan-out of short-lived worker threads touching one channel will trip it.
5. **Channel args are copied cross-thread** — `bool*` out-params (`ICommandDispatcher::preDispatch`) only work for same-thread listeners.
6. **`Ret` default is falsy** (`Undefined`); `Ret(bool)` true = `Ok`. `if (ret)` ⇒ `success()` ⇒ `m_code == Ok(0)` exactly (`types\ret.cpp:49-52`); `Cancel(3)` and every other code are **falsy** too, so a user-cancel looks like failure unless you check `ret.code()`/`check_ret(ret, Ret::Code::Cancel)`. `valid()` = `code > Undefined` (`:44-47`).
7. **`ActionData::arg<T>` is an unchecked `reinterpret_cast`** (`actiontypes.h:94`) — wrong `T` = UB. Another reason to prefer typed commands.
8. **`ICommandDispatcher::dispatch` returns a `Promise`** and defaults to deferred execution; don't assume the handler already ran when it returns. Only one handler per `Command`; duplicate registration is a `LOGW` and silently ignored (`commanddispatcher.cpp:134-137`).
9. **`CommandInfo.availabilities` default is `All` in code, `Disabled` in the ADR** (`commandtypes.h:224` TODO) — a command you forget to flag is currently exposed to MCP/extensions.
10. **`ICommandsState` caches** — a module whose state changes must emit `commandStateChanged(cmd, state)`, or menus/shortcut-enablement keep the first-seen value (`commandsstate.cpp:80-84,103`).
11. **Shortcuts disabled over widget dialogs** — `ShortcutsController::init` turns the controller off while the top window is a QWidget dialog (`:39-42`). "My shortcut does nothing" inside an old-style dialog is by design.
12. **Unknown shortcut scope ⇒ priority 0, not "never"** (`shortcutresolver.cpp:79-80`) — it sorts *below* active scopes but *above* inactive (−1) ones, and a priority-0 winner is dispatched. Scope strings in `shortcuts.json` are case-handled only for the *title* lookup (`toUpper`, `:168`), not for priority (`m_scopePriorityMap` is exact-match) — keep scope codes upper-case exactly as in `shortcutresolver.cpp:34-54`.
13. **Settings**: `Key.moduleName` is *not* persisted (`writeValue` uses only `key.key`, `settings.cpp:239-240`; a `TODO` says so) — two modules using the same `key` string collide. `value()` of an unregistered key returns a null `Val`. `setDefaultValue` must run before `value()` to get the right `Val::Type`.
14. **`Val` constructors are `explicit`** — `Val(5)` ok, implicit conversions don't compile; `toEnum<E>()` goes through `toInt()`.
15. **`IFileSystem` is the abstraction** — don't `QFile` in framework code; tests inject a mock. On WASM the global one is `MemFileSystem`.
16. **`TranslatableString` must be spelled literally** at the call site for `lupdate` extraction; building one from a variable context/text isn't translatable.

## UNCLEAR — needs verification

- **`kors_async` internals beyond the above:** `RpcQueue`/`ObjectPool` sizing behavior, `Promise::then` chaining semantics and `Result` plumbing (only `Promise` lines 1-221 were read).
- **How/where the main-thread pump interval maps to real time** — `Ticker`/`ITickerProvider` timing units were not traced (`m_asyncTicker.start(1, …)`); assumed ≈1 ms.
- **Per-module command registers/state classes** beyond `workspacecommands.h` and `notationcommands.h` (e.g. appshell/project/palette registers) were not enumerated; the ~39-command count of `notationcommands.h` is a `Command("command://` grep, not a full inventory.
- **`rcontrol` MCP internals** (`McpServer`, transport, bridge), `extensions` command integration, `update`/`cloud` implementations, `learn`/`tours`/`toast`/`interactive`/`multiwindows` — orientation only.
- **`shortcuts_v2` JSON schema** (`share`/resources `shortcuts.json` structure, preset-diff semantics, `applyShortcutsDiff`/`mergeShortcuts`) — only the reload flow was read, not the file format or merge rules.
- **Whether the `actions` module is compiled out in some build configs** (`MUSE_MODULE_ACTIONS` / `_SUPPORT` flags in `SetupConfigure.cmake`); the `#ifdef MUSE_MODULE_ACTIONS_SUPPORT` paths in UI models suggest it is optional.
- **`concurrency\`, `allocator.*`, `runtime.*`, `profiler`** internals not read.

*Authored from: whole-header reads of `global\async\*`, `kors_async\async\{channel,asyncable,async,notification,promise(1-221),conf,processevents}.h` + `internal\{channelimpl.h,conf.cpp,queuepool.h}`, `global\{settings.h/.cpp,iglobalconfiguration.h,iapplication.h,progress.h,timer.h,ticker.h,globalmodule.cpp}`, `global\types\{val,ret,retval,uri,translatablestring}.h`, `global\io\{ifilesystem,path}.h`, `global\serialization\*` (signatures); `actions\*` (all headers + `actionsdispatcher.cpp`); `rcommand\*` (all headers, `commanddispatcher.cpp`, `commandsregister.cpp`, `commandsstate.cpp`, `rcommandmodule.cpp`); `shortcuts\*` and `shortcuts_v2\*` headers, `shortcutscontroller.cpp`, `shortcutsmodule.cpp`, `shortcutsconfiguration.cpp`; `src\appshell\internal\shortcutresolver.cpp`; `src\app\configs\shortcuts.cfg`; `src\notationscene\notationcommands.h`; ADRs `00101`, `00104`, `00107`, `00108`; workspace/cloud/network/update/languages interface headers; root + framework `CMakeLists.txt` (2026-11-08). **If this contradicts the live source, the source wins.***
