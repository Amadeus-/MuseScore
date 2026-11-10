# MuseScore — Command Line (`mscore` / MuseScore CLI)

> **What this file is.** Scope: every command-line option of the MuseScore 5.0 `main` binary, the converter module (`src\converter`) that implements single-file and batch (`-j`) conversion, the console-mode dispatch, and headless considerations. Everything is traced from source at `C:\Dev\+MuseScore+`. **If this contradicts the live source, the source wins.** Start at `00-core.md`; the GUI-relevant subset of the CLI and the `ConsoleApp` plumbing are also in `app-shell-and-startup.md` (§3.3, §4) — this file does not repeat them in depth.

## 0. 60-second orientation

- **One binary, three run modes** (`muse::IApplication::RunMode`): `GuiApp` (default), `ConsoleApp` (any converter/diagnostic/testflow option), `AudioPluginRegistration` (`--register-audio-plugin`). The mode is decided purely by which options are present, in `CommandLineParser::parse()` (`src\app\internal\commandlineparser.cpp:192-557`) **before** the `QApplication` exists (`main.cpp:155-172`).
- **Pipeline:** `commandlineparser.cpp` (Qt `QCommandLineParser`) fills `MuseScoreCmdOptions` (`src\app\cmdoptions.h`) → `AppFactory::newApp` builds the trimmed console module list (`appfactory.cpp:451-552`, includes `ConverterModule`) → `MuseScoreConsoleApp::doStartupScenario` routes (`consoleapp.cpp:149-192`) → `processConverter` (`:194-262`) calls `IConverterController` → `ConverterController` loads the score via `INotationProject::load(in, OpenParams)` and writes through an `INotationWriter` looked up **by output-file suffix** from `INotationWritersRegister`.
- **Exit code** = `Ret::code()` of the operation (`consoleapp.cpp:261`, then `qApp->exit(code)` at `:178`). `0` = ok; converter errors are 1300-1331 (`convertercodes.h`, table in §6). Note POSIX truncates exit status to 8 bits (OS behavior, not MuseScore code).
- **Binary name:** `src\app\CMakeLists.txt:25` `EXECUTABLE_NAME mscore` (default, also Linux; Linux appends `MUSE_APP_INSTALL_SUFFIX` at `:55-57`); on **Windows** `${MUSE_APP_NAME_MACHINE_READABLE}` (`:31-32`); WASM `MuseScoreStudio` (`:79`). The CMake target is `MuseScoreStudio`, `OUTPUT_NAME ${EXECUTABLE_NAME}` (`:245`). In the Reprise fork `MUSE_APP_NAME_MACHINE_READABLE` = `"Reprise"` (`version.cmake:22`), so the Windows binary is **`Reprise.exe`** (the version suffix was dropped from the exe name; `applicationName` / the user-data dir is likewise bare `Reprise` — see `main.cpp:124-126`).
- Positional: `[scorefile...]` (`commandlineparser.cpp:69`). In **converter modes only the first** is used (`firstScoreFile`, `:235-243`: logs error if none, warns "processing only the first one" if several). In GUI mode only `scorefiles[0]` is opened (`:548-551`).
- All paths from the CLI are passed through `io::Dir::fromNativeSeparators` (`:226-228`), so backslashes are accepted on Windows.

## 1. Invocation and the two (three) app modes

| Mode | Triggered by | App object / Qt app | Module set | Ends how |
|---|---|---|---|---|
| `GuiApp` | none of the console options | `MuseScoreGuiApp`, `QApplication` | full | user quits |
| `ConsoleApp` | `-o`, `-j`, `--score-media`, `--score-meta`, `--score-parts`, `--score-parts-pdf`, `--score-transpose`, `--score-elements`, `--source-update`, `--score-video`, `--diagnostic-gen/com/drawdata/-to-png/drawdiff`, `--test-case` | `MuseScoreConsoleApp`, **`QApplication`** (Widgets, not `QCoreApplication`) — `main.cpp:166` | `addConsoleModules` (`appfactory.cpp:451`) | `qApp->exit(code)` after the single task |
| `AudioPluginRegistration` | `--register-audio-plugin` | `MuseScoreConsoleApp`, `QCoreApplication` (`main.cpp:163-164`) | audio + audioplugins + vst only (`appfactory.cpp:554-565`) | `qApp->exit(code)` |

`QCommandLineParser::parse()` is called early (`commandlineparser.cpp:197`); `process()` (which handles `-h`/`-v` and rejects unknown options) is deferred to `processBuiltinArgs(*qapp)` because it needs a Qt app (`:559-563`, `main.cpp:171`). `--long-version` is handled inside `parse()` and exits immediately (`:245-248`).

### Console dispatch (summary — detail in `app-shell-and-startup.md` §3.3)

`MuseScoreConsoleApp::doStartupScenario` (`consoleapp.cpp:157-181`), strict priority for `ConsoleApp`:

1. **Testflow** — `testflow.testCaseNameOrFile` non-empty (`--test-case`) → `processTestflow` (`:339-369`); stays alive until the testflow status channel reports `Finished` (then `exit(0)`) or any step fails (`exit(ret.code())`).
2. **Diagnostic** — `diagnostic.type != Undefined` → `processDiagnostic` (`:264-317`) → `qApp->exit(code)`.
3. **Converter** — otherwise `processConverter` (`:194-262`) → `qApp->exit(code)`. (If `ConsoleApp` was set by e.g. only `--diagnostic-*`, step 2 wins; if nothing at all set a converter type the default `ConvertType::File` with empty paths runs and fails.)

`applyCommandLineOptions` (`consoleapp.cpp:75-147`) pushes the global/UI/export overrides into module configurations **before** the task runs: `-D` → `uiConfiguration()->setCustomPhysicalDotsPerInch`; `-t`/`--template-mode` → `notationConfiguration()`; `-T`/`-r` → `imagesExportConfiguration`; video → `videoExportConfiguration`; `-M` → `midiImportExportConfiguration`; MusicXML flags → `musicXmlConfiguration`; `-b` → `audioExportConfiguration`; GP flags → `guitarProConfiguration`; `-F/-R` → `settings()->reset(...)`. It also configures project migration: `migration.isAskAgain=false` ("Don't ask about migration in convert mode", `:101-102`) and applies `--migration full` to all migration types, session-only (`:112-115`).

## 2. Full flag reference

Notation: **Arg** `—` = boolean switch. **Mode** = what the flag sets. Source cites are `commandlineparser.cpp` unless noted. `OpenParams` = `project::OpenParams` (`src\project\types\projecttypes.h:47-51`: `stylePath`, `forceMode`, `unrollRepeats`, …).

### 2.1 General / Qt-built-in

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `-h`, `-?`, `--help` | — | Qt help text, exit (via `process()`) | `:66` |
| `-v`, `--version` | — | Qt version output (app version set by `setApplicationVersion`), exit | `:67`, `main.cpp:130` |
| `--long-version` | — | prints `MuseScore: Music Score Editor; Version X; Build Y` (or the "Unstable Prerelease" form) and `exit(EXIT_SUCCESS)` inside `parse()` | `:71,245-248,575-584` |
| `-d`, `--debug` | — | `global.loggerLevel = Debug` → `BaseApplication::applyCommandLineOptions` → `setLoggerLevel` | `:72,250-252`; `baseapplication.cpp:234-236` |
| `-D`, `--monitor-resolution` | `DPI` (double) | `ui.physicalDotsPerInch`; bad value → `LOGE`, ignored | `:74,254-261`; `consoleapp.cpp:92` |
| `-t`, `--test-mode` | — | `notation.testModeEnabled` → `MScore::testMode` | `:82,289-291`; `notationconfiguration.cpp:1083-1086` |
| `--template-mode` | — | `notation.templateModeEnabled` → `MScore::saveTemplateMode` ("no page size, no platform/creationDate tags" per the source comment `:81`) | `:81,285-287`; `notationconfiguration.cpp:1078-1081` |
| `--session-type` | `type` | `startup.type` (raw string). **GUI only**; interpreted by `StartupScenario` (see `app-shell-and-startup.md`) | `:84,293-295` |
| `--score-display-name-override` | `name` | **hidden** (`HiddenFromHelp`); GUI only; splash/recent display name | `:188-189,553-555` |
| `-F`, `--factory-settings` | — | `app.revertToFactorySettings = true` (identical to `-R`) | `:94,453-455` |
| `-R`, `--revert-settings` | — | same; see gotcha 1 | `:95,453-455` |

### 2.2 Conversion / export (the `-o` family)

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `-o`, `--export-to` | `file` | `runMode=ConsoleApp`, `type=ConvertType::File`, `inputFile = first positional`, `outputFile = file`. **Format is chosen by `file`'s suffix** (writer lookup, §3). Also consumes `--transpose`, `--page`, `--region` | `:90,313-331` |
| `-j`, `--job` | `file` | `runMode=ConsoleApp`, `type=Batch`, `inputFile = the job JSON path`. Overrides `-o`'s type if both given (later in `parse()`) | `:91,341-345` |
| `--extension` | `uri` | stored as `ParamKey::ExtensionUri`; converted to `UriQuery`; when valid the conversion runs the extension action **before** writing (§3.4) | `:92,477-479`; `consoleapp.cpp:201` |
| `-S`, `--style` | `style` file (`.mss`) | `ParamKey::StylePath` → `OpenParams.stylePath`. Loaded via `EditStyle::loadStyle` after load/migration (native `.mscz/.mscx`) or **before** reading (imports) — §5 | `:115,465-467`; `consoleapp.cpp:209`; `notationproject.cpp:210-215,311-317` |
| `-f`, `--force` | — | `ParamKey::ForceMode` → `OpenParams.forceMode`: "ignore warnings reg. score being corrupted or from wrong version" — passed to `loadMscz`/`setupMasterScore` (error from setup is swallowed, `engravingproject.cpp:127`) and to importers (MusicXML, MEI use it to skip validation/warning prompts) | `:98,457-459`; `notationproject.cpp:179,195,299,329` |
| `--unroll-repeats` | — | `OpenParams.unrollRepeats`: after load, if `repeatList().size() > 1` the master score is replaced by `MasterScore::unrollRepeats()` (works for both native and imported) | `:86,461-463`; `notationproject.cpp:221-225,334-339` |
| `--transpose` | `options` (JSON string) | **only meaningful with `-o`** (read inside the `-o` block). Applied after load, before writing. Schema §5 | `:126,319-322` |
| `--page` | `n` (**1-based**) | with `-o`: export only that page. Supported: SVG, PNG, PDF, MSCZ (help text). Ignored in the batch path (use the per-job `page`) | `:130,324-326`; `consoleapp.cpp:40-61` |
| `--region` | JSON string | with `-o` and an `.mscz/.mscx/.mscs` output: save only the region (measure/staff/voice range) as a new mscz. Schema §5.3. **Takes precedence over `--page`** (`parseTarget` checks region first) | `:135,328-330`; `consoleapp.cpp:42-45` |
| `-r`, `--image-resolution` | `DPI` (float) | `exportImage.pngDpiResolution` → `imagesExportConfiguration->setExportPngDpiResolutionOverride` (PNG only, by name) | `:89,304-311`; `consoleapp.cpp:120` |
| `-T`, `--trim-image` | `margin` (int, px) | `exportImage.trimMarginPixelSize`; for PNG and SVG | `:75,263-270`; `consoleapp.cpp:119` |
| `-b`, `--bitrate` | `kbps` (int) | `exportAudio.mp3Bitrate` → `audioExportConfiguration->setExportMp3BitrateOverride` | `:79,276-283`; `consoleapp.cpp:138` |
| `-M`, `--midi-operations` | `file` | `importMidi.operationsFile` → MIDI **import** operations file (for importing `.mid`) | `:96,272-274`; `consoleapp.cpp:131` |
| `-P`, `--export-score-parts` | — | if `outputFile` already set: `type = ConvertScoreParts` → `convertScoreParts(in,out)` (PDF / PNG / MP3 per-part files; see §3.5). Error text wrongly says `-R` | `:97,333-339` |
| `--sound-profile` | `"MuseScore Basic"` \| `"MuseSounds"` | with `-o x.mp3` or `-j`: override the score's active sound profile (clears track input params first). Unknown names are logged and **dropped** | `:117,469-471`; `consoleapp.cpp:200-206`; `convertercontroller.cpp:243-246` |
| `--tracks-diff` | `file` | with `-o x.mp3`: write JSON `{ "oldTracks":[…], "newTracks":[…] }` (track list before / after sound-profile change) | `:121,473-475`; `convertercontroller.cpp:238-241,660-673` |
| `--migration` | `mode` | console mode only; only value `full` has an effect (anything else → `fullMigration=false`) → applies Edwin/Leland/percussion-remap/migration | `:166,489-494`; `consoleapp.cpp:104-110` |

### 2.3 Score-meta / JSON-to-stdout exporters (`--score-*`, `--source-update`)

All of these take the **first positional** as the input score, produce JSON via `BackendApi` (`src\converter\internal\compat\backendapi.cpp`) and write to **stdout** unless an output path is given — and the only way an output path reaches them is a co-supplied `-o` (the `-o` block sets `outputFile`, and none of these blocks clear it). `BackendApi::openOutputFile` (`:221-232`): empty `out` → `file.open(stdout, WriteOnly)`. Its return value is ignored by the callers (if the file can't be opened the writes silently fail). They open the project with `openProject` (`:234-264`), which also forces **page view** and lays out all excerpts.

| Flag | Arg | Output (JSON) | Source |
|---|---|---|---|
| `--score-meta` | — | score metadata (`NotationMeta::metaJson`) under key `metadata` | `:104,357-361`; `backendapi.cpp:112-132,457-471` |
| `--score-media` | — | one JSON with base64 `pngs[]`, `svgs[]`, `sposXML`, `mposXML` (segment/measure positions), `pdf`, `midi`, `mxml` (MusicXML .mxl), `metadata`, `devinfo` (`version`) | `:101-102,347-355`; `backendapi.cpp:78-110` |
| `--highlight-config` | `file` | modifier of `--score-media`: JSON file `{"highlight":[{"beats":[idx…],"color":"#rrggbb"}]}` → per-beat colors passed as `BEATS_COLORS` to the **SVG** writer | `:103,351-354`; `backendapi.cpp:266-296,355-363` |
| `--score-parts` | — | `{ parts:[titles], partsMeta:[{…tags, open, id}], partsBin:[base64 zipped mscz per part] }`. Includes *potential* (not yet created) excerpts: `allExcerpts` calls `initExcerpts` | `:105,363-367`; `backendapi.cpp:542-587,806-825` |
| `--score-parts-pdf` | — | `{ score, scoreBin, parts[], partsBin[], scoreFullPostfix:"-Score_and_parts.pdf", scoreFullBin }` (base64 PDFs; the combined one uses `UnitType::MULTI_PART`) | `:106-107,369-373`; `backendapi.cpp:589-631` |
| `--score-transpose` | `options` (JSON string, schema §5.2) | transposes whole score then `{ mscz: base64 zip (name `<name>_transposed.<rand>.mscz`), pdf: base64 }` | `:108-110,375-380`; `backendapi.cpp:176-201,633-652` |
| `--score-elements` | — | compact JSON array, one entry per instrument `{instrumentId, partId, elements:[{type, name?, notes?, duration?, location…/start,end, …}]}` from `ScoreElementScanner::scanElements` | `:111-112,382-386`; `backendapi.cpp:203-219,654-749` |
| `--source-update` | (uses 2 **positionals**: `<score> <newSource>`) | not JSON: sets `ProjectMeta.source` and **saves the score in place** (`project->save()`); uses `-f` only (not `-S`). Missing 2nd positional → `LOGW` | `:113,388-400`; `backendapi.cpp:827-844` |

### 2.4 MusicXML / Guitar Pro import tweaks

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `--musicxml-use-default-font` | — | `importMusicXml.useDefaultFont` → `musicXmlConfiguration->setNeedUseDefaultFontOverride` ("Apply default typeface (Edwin)") | `:140-141,403-405`; `consoleapp.cpp:134` |
| `--musicxml-infer-text-type` | — | `importMusicXml.inferTextType` → `setInferTextTypeOverride` | `:142-143,407-409`; `consoleapp.cpp:135` |
| `--gp-linked` | — | `guitarPro.linkedTabStaffCreated` → create tablature **linked** staves for GP import | `:162,481-483`; `consoleapp.cpp:141` |
| `--gp-experimental` | — | `guitarPro.experimental` | `:163,485-487`; `consoleapp.cpp:142` |

(These MusicXML/GP/MIDI overrides are applied via `#ifdef MUE_BUILD_IMPEXP_*_MODULE` blocks in `applyCommandLineOptions`, i.e. only if that module is built. In GUI mode their application is not traced here — UNCLEAR.)

### 2.5 Audio / video

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `--score-video` | — | **compiled only with `MUE_BUILD_IMPEXP_VIDEOEXPORT_MODULE`** (`:146,412`). `runMode=ConsoleApp`, `type=ExportScoreVideo`, `outputFile = -o value`. Because it runs after the `-o` block, it overrides `-o`'s `ConvertType::File` (order-dependent but deterministic). Output writer is the `mp4` writer (`videoexportmodule.cpp:53`) | `:147,413-417`; `consoleapp.cpp:247-250` |
| `--resolution` | `2160p\|1440p\|1080p\|720p\|480p\|360p` (default shown `1080p`) | `exportVideo.resolution` string → `videoExportConfiguration->setResolution` | `:155,431-433` |
| `--fps` | `60\|30\|24` (shown `24`) | `exportVideo.fps` (int) | `:156,435-437` |
| `--ls` | seconds (shown `3.0`) | leading pause before playback → `leadingSec` | `:157,439-441` |
| `--ts` | seconds (shown `3.0`) | trailing pause before end → `trailingSec` | `:158,443-445` |
| `--no-audio` | — | `ParamKey::NoAudio` → `exportScoreVideo(..., withAudio=false)` → writer option `WITH_AUDIO` | `:159,447-449`; `convertercontroller.cpp:749-753` |
| `view-mode`, `piano`, `piano-position` | — | **commented out** ("not implemented", `:148-154`); the `isSet` checks at `:419-429` hit `NOT_IMPLEMENTED` but can never fire because the options aren't registered |
| (mp3 export) | | see `-b`, `--sound-profile`, `--tracks-diff` above | |

### 2.6 Audio plugin registration

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `--register-audio-plugin` | `path` | `runMode = AudioPluginRegistration`; `QCoreApplication` (no widgets) and only audio/audioplugins/vst modules. Without `-out`: `registerPlugin(path)` (check compatibility **and register**) | `:183-184,297-301`; `consoleapp.cpp:319-337` |
| `--register-audio-plugin-out` | `path` | if non-empty → `validatePlugin(path, out)` instead: check and **write the result to the file** (no registration) | `:185`; `consoleapp.cpp:326-327` |

(This is how the app spawns a separate process to probe VST plugins — see `midi-vst-musesampler.md`.)

### 2.7 Diagnostics (developer tools — not for general use)

All set `runMode=ConsoleApp` and a `DiagnosticType` (`cmdoptions.h:26-32`); executed in `processDiagnostic` via `diagnosticDrawProvider()` (returns `NotSupported` if the diagnostics module isn't built, `consoleapp.cpp:266-268`). Output dir defaults to `"./"` if `--diagnostic-output` unset (`:281-285`).

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `--diagnostic-output` | `output` | output path for any diagnostic | `:169,497-499` |
| `--diagnostic-gen-drawdata` | `scores-dir` | `GenDrawData`: generate engraving draw data (input = that single value, `input.front()`) | `:170,501-505`; `consoleapp.cpp:288-290` |
| `--diagnostic-com-drawdata` | — | `ComDrawData`: compare two draw-data inputs = the **two positional** files (asserts `size()==2`) | `:171,507-511`; `consoleapp.cpp:291-296` |
| `--diagnostic-drawdata-to-png` | `file` | `DrawDataToPng` | `:172,513-517` |
| `--diagnostic-drawdiff-to-png` | — | `DrawDiffToPng`: positionals = `<diff> [<ref>]` | `:173,519-523`; `consoleapp.cpp:300-307` |

### 2.8 Testflow (scripted UI/test automation; used by `vtest`)

`--test-case` (also sets `ConsoleApp`) is the trigger; the others only fill `ITestflow::Options` (`consoleapp.cpp:362-368`) → `testflow()->execScript(nameOrFile, opt)`.

| Flag | Arg | Effect | Source |
|---|---|---|---|
| `--test-case` | `nameOrFile` | run a test case (path normalized); highest dispatch priority | `:176,526-529` |
| `--test-case-context` | `nameOrFile` | context file/name (`opt.context`) | `:177,531-533` |
| `--test-case-context-value` | `value` | `opt.contextVal` | `:178,535-537` |
| `--test-case-func` | `name` | `opt.func` — call a specific function | `:179,539-541` |
| `--test-case-func-args` | `args` | `opt.funcArgs` | `:180,543-545` |

Real use: `vtest\vtest.sh:37-39` runs `$MSCORE_BIN --test-case vtest.js --test-case-context vtest_context.json` (see `testing-and-vtest.md`).

## 3. Single-file conversion — `-o` (converter module)

### 3.1 Module layout (`src\converter`)

| File | Role |
|---|---|
| `iconvertercontroller.h` | `IConverterController` (context-scoped interface): `fileConvert`, `batchConvert`, `convertScoreParts`, `exportScoreMedia/Meta/Parts/PartsPdfs/Transpose/Elements/Video`, `updateSource` |
| `convertertypes.h` | `ConvertRegion`, `ConvertRegionJson = std::string`, `page_num_t = size_t`, `ConvertTarget = std::variant<ConvertRegionJson, page_num_t>`, `OpenParams = project::OpenParams` |
| `convertercodes.h` | `Err` enum (§6) |
| `convertermodule.cpp` | registers `IConverterController` per **context** (`:55-58`) and the plugin-API object `MuseApi.Converter` (`:40-48`) |
| `internal\convertercontroller.*` | file/batch/part/page logic (below) |
| `internal\converterutils.*` | `parseTransposeOptions`, `parseRegion`, `applyTranspose`, `setVisibleParts` |
| `internal\compat\backendapi.*`, `backendjsonwriter.*`, `notationmeta.*`, `scoreelementsscanner.*`, `backendtypes.h` | the legacy "backend" JSON exporters used by the `--score-*` flags |
| `api\converterapi.*` | QML/extension scripting wrapper (§4.3) |
| `tests\convertercontroller_tests.cpp`, `scoreelementsscanner_tests.cpp` | gtest |

### 3.2 `ConverterController::convertFile` — the algorithm (`convertercontroller.cpp:206-332`)

1. `suffix = io::suffix(out)`; `writers()->writer(suffix)`; **no writer → `Err::ConvertTypeUnknown`** (1310) — so the output suffix must be a registered writer *even for mscz* (registered in `notationmodule.cpp:67-68`).
2. `notationCreator()->newProject(iocContext())`, then `project->load(in, openParams)`. **Input format is also by suffix** — native (`mscz/mscx/mscs`) → `doLoad`; anything else → `doImport` using the reader registry (§3.3). Failure → `Err::InFileFailedLoad` (1320).
3. If `--tracks-diff`: snapshot `oldTracks` (`NotationMeta::tracksJsonArray`).
4. If a (validated) sound profile: `audioSettings()->clearTrackInputParams(); setActiveSoundProfile(profile)`.
5. If transpose options: `ConverterUtils::applyTranspose` (select all, `interaction->transpose`) → `Err::TransposeFailed` (1304) on failure.
6. If `visibleParts` (batch only): `ConverterUtils::setVisibleParts` — set listed (0-based part indices) visible, hide all others (`converterutils.cpp:191-208`).
7. If `copyright.text` (batch only): append to **both** `oddFooterC` and `evenFooterC` style values, `doLayout()` (`:260-272`). `showOnAllPages` is parsed (`:448-451`) but **never read** afterwards — see gotchas.
8. `projectSession()->setProject(project)` (cleared by `DEFER` at return).
9. **Parts-by-name:** if the output basename contains `*` → `convertScoreParts(writer, master, out)` and return (`:281-284`).
10. **Extension:** if `extensionUri.isValid()` → `convertByExtension` (§3.4) *instead of* the standard path.
11. **Standard path:**
    - output suffix `mscz`/`mscx`/`mscs` (`engraving::MSCZ/MSCX/MSCS`): page target → `project->savePage(out, n)`; region target → `saveRegion(...)`; else `project->save(out)`. **Returns immediately** (tracks-diff not written).
    - `pageNumIsSet` **or** suffix in {`png`, `svg`} → page-by-page (`convertPage` for the one page, or `convertPageByPage`).
    - else `convertFullNotation` (single `writeNotationToFile`).
12. If ok and `--tracks-diff`: `writeTracksDiff` (JSON, compact).

`writeNotationToFile` (`:85-130`): creates the output directory tree (`Dir::mkpath`), remembers which dirs it created (`missingDirs`), opens a `FileStream` with meta `file_path`(+`dir_path`), calls `writer->write(notation, stream, options)`; on failure deletes the partial file and removes the dirs it created (only if empty). Errors → `OutFileFailedOpen` (1330) / `OutFileFailedWrite` (1331).

Page-by-page naming (`convertPageByPage`, `:513-529`): `<dir>/<basename>-<N>.<suffix>`, **N starting at 1**. With an explicit `--page n` the file is written to exactly `out` (no `-N`).

### 3.3 Supported input / output format pairs (from module registrations)

Output = writers registered with `INotationWritersRegister::reg`; input = readers registered with the readers register (`importexport.md` is still STUB — this table is the authoritative list from the `*module.cpp` registrations). A writer/reader exists only if its `MUE_BUILD_IMPEXP_*` module is built into this binary.

| Direction | Suffixes | Module (source) |
|---|---|---|
| **Out (native)** | `mscz` (zip), `mscx` (dir/plain) | `notationmodule.cpp:67-68`; `mscs` is also accepted by `convertFile` (`:298`) but has no writer registered in `notationmodule.cpp` → the step-1 writer lookup would fail with `ConvertTypeUnknown` (UNCLEAR whether `mscs` is registered elsewhere) |
| Out (images/print) | `pdf`, `svg`, `png` | `imagesexportmodule.cpp:53-55` |
| Out (audio) | `wav`, `mp3`, `ogg`, `flac`, `aac` | `audioexportmodule.cpp:58-62` |
| Out (video) | `mp4` | `videoexportmodule.cpp:53` |
| Out (score interchange) | `musicxml`, `xml` (uncompressed MusicXML), `mxl` (compressed); `mid`/`midi`/`kar`; `mei`; `mnx` | `musicxmlmodule.cpp:64-65`; `midimodule.cpp:60`; `meimodule.cpp:59`; `mnxmodule.cpp:55` |
| Out (other) | `lrc` (lyrics); `brf` (braille) | `lyricsexportmodule.cpp:51`; `braillemodule.cpp:59` |
| Out (internal) | `spos`, `mpos` (segment / measure positions XML) | `notationmodule.cpp:65-66` — used by `--score-media`, usable with `-o x.spos` |
| **In (native)** | `mscz`, `mscx`, `mscs` | `notationproject.cpp:163`, `mscio.h:31-48` |
| In | `xml`, `musicxml`, `mxl` | `musicxmlmodule.cpp:59` |
| In | `mid`, `midi`, `kar` | `midimodule.cpp:55` |
| In | `gtp`, `gp3`, `gp4`, `gp5`, `gpx`, `gp`, `ptb` | `guitarpromodule.cpp:52` |
| In | `mei`; `mnx`, `json`; `cap`, `capx`; `bww`, `bmw`; `mgu`, `sgu`; `ove`, `scw`; `md`; `tef` | `meimodule.cpp:54`; `mnxmodule.cpp:51`; `capellamodule.cpp:43`; `bwwmodule.cpp:44`; `bbmodule.cpp:44`; `ovemodule.cpp:52`; `musedatamodule.cpp:43`; `tableditmodule.cpp:43` |

Any input-reader × any output-writer pair is allowed — the controller does not whitelist pairs. Notes: `.mscz~` backups are recognized by `isMuseScoreFile` (`mscio.h:52`) but not as an output.

### 3.4 `--extension` (convert with an extension)

`convertByExtension` (`:488-501`): `extensionUri = UriQuery` parsed from the `--extension` string; `action` query param (default `"main"`); calls `extensionsProvider()->perform(uri, action)` **first** ("it can modify the notation (score)"), then `writeNotationToFile(writer, notation, out)`. Page/region targets and the mscz fast-path are bypassed when an extension is valid. See `extensions-system.md`.

### 3.5 Part conversion (`-P` and `*` in the output name)

`convertScoreParts(writer, master, out)` (`:360-373`) supports **only** `pdf`, `png`, `mp3` (else `Ret::Code::NotSupported`). For each **excerpt** (`masterNotation->excerpts()` — existing excerpts only, not potential ones) the `*` in the output basename is replaced with the part's notation name (`:561,580,602`):
- `pdf`/`mp3`: per-part file, writer option `UNIT_TYPE = PER_PART`;
- `png`: each part page-by-page → `<base-with-part>-N.png`.

Triggers: `-o "out-*.pdf"` (the `*` is detected inside `convertFile`, `:281-284`), or `-P` with `-o` (`-P` → `convertScoreParts(in, out)`, a separate entry that does *not* require a `*`; if the name has no `*` every part writes the same path and later ones overwrite earlier ones — TRACED from `baseName.replace("*", partName)` being a no-op without `*`).

## 4. Batch conversion — `-j <job.json>`

`-j` → `ConvertType::Batch`, `inputFile = jobfile` → `converter()->batchConvert(jobfile, openParams, soundProfile, extensionUri)` (`consoleapp.cpp:214-216`). **Global flags that apply to every job:** `-S`, `-f`, `--unroll-repeats` (via `OpenParams`), `--sound-profile`, `--extension`, plus the image/audio/MIDI/MusicXML overrides. **Not** applied: `--transpose`, `--page`, `--region`, `--tracks-diff` (the batch path has per-job equivalents in the JSON).

### 4.1 Job-file schema (`ConverterController::parseBatchJob`, `convertercontroller.cpp:375-486`)

The file must be a **JSON array** (`doc.isArray()` else `Err::BatchJobFileFailedParse` 1302; unreadable → `BatchJobFileFailedOpen` 1301). Each element is an object. Internal `Job` struct: `convertercontroller.h:85-93`.

| Key | JSON type | Meaning | Parse cite |
|---|---|---|---|
| `in` | string | input score path (native separators OK) | `:403` |
| `out` | string **or** array | output path; **array → one job per item**: an item may be a string (an output path) or a 2-element array `[prefix, suffix]` → `prefix + "*" + suffix` (i.e. the per-part naming of §3.5) | `:463-481` |
| `transpose` | object | same schema as `--transpose` (§5.1) but as a nested **object** (not a string); invalid → whole batch fails to parse (`InvalidTransposeOptions`) | `:405-413` |
| `page` | number (1-based) | export that single page (`toInt() - 1`); same semantic as `--page` | `:415-418` |
| `visibleParts` | array of numbers | 0-based part indices to keep visible; all other parts are hidden before export (so excerpts/instruments can be selected per job); non-array → parse error; non-number elements logged and skipped | `:420-435`; `converterutils.cpp:191-208` |
| `copyright` | object `{ "text": string, "showOnAllPages": bool }` | `text` required (missing → parse error); appended to the odd+even **footer center** style strings | `:437-456`; `convertercontroller.cpp:260-272` |
| `tracksDiff` | string | path for the per-job tracks-diff JSON (as `--tracks-diff`) | `:458-461` |

There is **no** per-job `style`, `soundProfile` or `extension` key — those are global flags.

### 4.2 Execution semantics (`batchConvert`, `:132-182`)

- Jobs run **sequentially** in file order, each as a full `convertFile` (load → apply → write). `progress` (an optional `muse::ProgressPtr`, null for the CLI) reports `(current, total, job.in)`.
- **A failing job does not stop the batch.** Each failure appends `"failed convert, err: …, in: …, out: …"` to `errors`; at the end if any: `Err::ConvertFailed` (1303) with all messages joined by newline; the process exit code is 1303. (`consoleapp.cpp:257-261`).
- Batch is the only place `visibleParts` and `copyright` can be used.

### 4.3 Worked examples

Single file (suffix picks the writer):

```
mscore -o out.pdf in.mscz
mscore -o out.png -r 150 -T 10 in.mscz           # -> out-1.png, out-2.png ... (page-by-page)
mscore -o out.mp3 -b 192 --sound-profile "MuseSounds" in.mscz
mscore -o out.musicxml in.mscz
mscore -S house.mss -o out.pdf in.mxl            # style applied BEFORE import
mscore --transpose "{\"mode\":\"by_interval\",\"direction\":\"up\",\"transposeInterval\":5}" -o out.mscz in.mscz
mscore --page 2 -o p2.svg in.mscz                # exactly p2.svg
mscore -o "parts-*.pdf" in.mscz                  # one PDF per existing excerpt
```

Batch job (`jobs.json`) and invocation:

```json
[
  { "in": "a.mscz", "out": "out/a.pdf" },
  { "in": "b.mxl",  "out": ["out/b.mid", "out/b.mp3"],
    "transpose": { "mode": "to_key", "direction": "closest", "targetKey": -2,
                   "transposeKeySignatures": true, "transposeChordNames": true } },
  { "in": "c.mscz", "out": [["out/c-", ".pdf"]], "page": 1 },
  { "in": "d.mscz", "out": "out/d.pdf", "visibleParts": [0, 2],
    "copyright": { "text": " (c) 2026 Me", "showOnAllPages": true } }
]
```
```
mscore -j jobs.json -S house.mss -f
```

Scripted batch from QML/extensions: `ConverterApi::batch(outDir, jobJsonString, uriQuery, progressCallback)` (`converterapi.cpp:66-96`) writes `<outDir>/job.json` then calls `converter()->batchConvert(jobFile, {}, String(), UriQuery(uriQuery), progress)` — note it passes **empty `OpenParams`** (no style/force) and runs `QCoreApplication::processEvents()` around progress callbacks. Other `ConverterApi` helpers: `selectDir`, `scanDir(dir, filters)`, `basename`. Exposed as `MuseApi.Converter` (`convertermodule.cpp:46`).

## 5. Style, transpose, region, meta options

### 5.1 `-S/--style` (style application)

`OpenParams.stylePath` is consumed in `NotationProject` (`src\project\internal\notationproject.cpp`):
- **Native load (`doLoad`, `:210-215`)**: *after* `setupMasterScore` and migration, `EditStyle::loadStyle(<dummy transaction>, masterScore, File(stylePath))`. So a `.mss` overrides the style stored in the score (and any migration-applied style).
- **Import (`doImport`, `:311-317`)**: `stylePath = openParams.stylePath` **or, if empty, the configured default import style** `notationConfiguration()->styleFileImportPath()` (the Preferences → Import style file); loaded *before* the reader runs, matching MuseScore 3's order (`:308-309` comment). So `-S` overrides the preference; without `-S` the saved preference *still applies* to CLI imports.
- Not applied by `--source-update` (it builds its own `OpenParams`, `backendapi.cpp:830-831`) or by `ConverterApi::batch`.

### 5.2 Transpose JSON (`--transpose`, `--score-transpose`, job `transpose`)

Parsed in `ConverterUtils::parseTransposeOptions` (`converterutils.cpp:38-111`). The CLI value is a **JSON object string**; the batch value is a nested object. Any problem → `Err::InvalidTransposeOptions` (1311).

| Key | Values | Notes |
|---|---|---|
| `mode` | `"to_key"` (alias `"by_key"` kept for back-compat), `"by_interval"`, `"diatonically"` | required; `to_key` ⇒ `TransposeMode::TO_KEY`, `by_interval` ⇒ `BY_INTERVAL`, `diatonically` ⇒ `DIATONICALLY` |
| `direction` | `"up"`, `"down"`, `"closest"` | required |
| `targetKey` | int in `Key::MIN..Key::MAX` (`C_B`..`C_S`, i.e. -7..7 flats/sharps) | required only for `to_key` |
| `transposeInterval` | int `0..25` (index into a 26-entry interval list) | required for `by_interval`/`diatonically` |
| `transposeKeySignatures` | bool | `needTransposeKeys` |
| `transposeChordNames` | bool | `needTransposeChordNames` |
| `useDoubleSharpsFlats` | bool | `needTransposeDoubleSharpsFlats` |

Applied with the whole score selected (`interaction->select(SelectionTarget::All)`; `transpose(options)`, `converterutils.cpp:177-189`).

### 5.3 Region JSON (`--region`)

`ConverterUtils::parseRegion` (`converterutils.cpp:113-165`): object `{ "start": {"measure":N,"staff":N}, "end": {…}, "voices":[1..4] }` — **all 1-based** (the parser subtracts 1). Asserts `start.staff <= end.staff` and `start.measure <= end.measure` (via `IF_ASSERT_FAILED`; failure → `Err::InvalidScoreRegion` 1312). `saveRegion` (`convertercontroller.cpp:613-658`): start/end measures resolved with `crMeasure(...)` (fallback to first/last measure); `voices` toggles the selection-filter for voices 1-4; range-selects start→end then `project->save(out, SaveMode::SaveSelection)`. Only valid with an `mscz/mscx/mscs` output (`:298-303`).

### 5.4 Meta options

`--score-meta` (JSON metadata), `--score-elements`, `--score-media` etc. → §2.3. `--source-update` rewrites the `source` meta (`ProjectMeta::source`) and saves the file in place.

## 6. Converter error codes (`convertercodes.h:27-46`)

| Code | `Err` | Raised by |
|---|---|---|
| 1300 | `UnknownError` (= `Ret::Code::ConverterFirst`) | project creation assert |
| 1301 | `BatchJobFileFailedOpen` | job file unreadable |
| 1302 | `BatchJobFileFailedParse` | not an array / bad `visibleParts`/`copyright` shape |
| 1303 | `ConvertFailed` | batch: ≥1 job failed (messages joined) |
| 1304 | `TransposeFailed` | `interaction->transpose` returned false |
| 1310 | `ConvertTypeUnknown` | no writer for output suffix |
| 1311 | `InvalidTransposeOptions` | bad transpose JSON |
| 1312 | `InvalidScoreRegion` | bad region JSON |
| 1320 | `InFileFailedLoad` | `project->load` failed |
| 1330 / 1331 | `OutFileFailedOpen` / `OutFileFailedWrite` | output dir/file creation / write failure |

The `--score-*` JSON exporters instead return `Ret::Code::InternalError` on any failure (`backendapi.cpp`), and `processConverter` ends with `return ret.code()` (`consoleapp.cpp:261`).

## 7. Headless notes

- ⭐ **Console mode is not a `QCoreApplication`.** `main.cpp:166` creates a full **`QApplication`** (Widgets) for every mode except audio-plugin registration (`:163-164`). A Qt platform plugin must therefore be loadable. On a server with no display set `QT_QPA_PLATFORM=offscreen`.
- ⭐ **Linux quirk (TRACED):** `main.cpp:102-111` — unless `MU_QT_QPA_PLATFORM` is set, main does `qputenv("QT_QPA_PLATFORM", "xcb")` (forced X11 because "with Wayland there are a number of problems"), **overwriting** any `QT_QPA_PLATFORM` you exported. To go offscreen you must export **both** `MU_QT_QPA_PLATFORM=offscreen` (to skip the xcb override and the gtk3 theme) **and** `QT_QPA_PLATFORM=offscreen` — exactly what `vtest\vtest.sh:34-35` and `.github\workflows\check_visual_tests.yml:235-236` do (and `vtest.sh:33` also sets `XDG_RUNTIME_DIR`). AppImage bundles `libqoffscreen.so` (`buildscripts\ci\linux\tools\make_appimage.sh:105`).
- Windows / macOS: no overriding of the platform in `main.cpp`; the normal `windows`/`cocoa` plugin is used (a desktop session is assumed; offscreen can be requested the Qt way — not tested here, HYPOTHESIS).
- Console mode still builds a large module set (audio, VST, MuseSampler outside debug, UI, extensions, all importexport — `appfactory.cpp:451-552`) but no main window and no QML window; the console `showSplash` just prints a banner to stdout (`consoleapp.cpp:68-73`). **Beware: that banner (`====…The MuseScore console application is starting…`) is on stdout**, ahead of any `--score-*` JSON written to stdout — consumers must skip it. (TRACED: `showSplash` is called from `main.cpp:198` for all app types; the exporters write to `stdout` via `QFile::open(stdout)`.)
- Logging (`LOGI/LOGE`) goes to the framework logger (console/file), not necessarily to stderr only; use `-d` for debug level.
- Audio export (`mp3`, `wav`…) needs the audio engine modules, which are in the console list; MuseSampler is skipped in debug builds (`appfactory.cpp:473-485`).
- Settings: console runs read/write the same user settings as the GUI except migration choices (explicitly "not written to settings, just on current session", `consoleapp.cpp:112`).

## Cross-subsystem dependencies

- `app-shell-and-startup.md` — entry point, `AppFactory`, `ConsoleApp` vs `GuiApp`, GUI use of `--session-type`/positional file, `-F/-R` semantics.
- `importexport.md` (STUB) — reader/writer implementations behind §3.3; MusicXML/MIDI/GP option semantics.
- `style-system.md` — what `.mss` loading (`EditStyle::loadStyle`) does; `oddFooterC/evenFooterC` Sids used by `copyright`.
- `file-format-and-rw.md` — `mscz/mscx` load/save, migration (`--migration`), `savePage`, `SaveSelection`.
- `notation-layer.md` — `INotation`, `interaction()->transpose`, parts/excerpts (`setPartVisible`), selection filter.
- `playback-and-audio.md` — sound profiles (`--sound-profile`), audio writers.
- `extensions-system.md` — `--extension`, `ConverterApi` as `MuseApi.Converter`.
- `testing-and-vtest.md` — `--test-case*`, `--diagnostic-*`.
- `midi-vst-musesampler.md` — `--register-audio-plugin`.

## Non-obvious gotchas

1. **`-F` and `-R` are identical in code** (`:453-455`) and both call `settings()->reset(true)` — `revertToFactorySettings` is passed as `keepDefaultSettings`, so only `m_settings->clear()` runs; the data-path `removeRecursively()` branch (`settings.cpp:115-118`) is **not** taken. So "factory settings" does not wipe the user data directory in this path. (Help text for `-R`: "Revert to factory settings, but keep default preferences"; for `-F`: "Use factory settings".)
2. **`-P` error text says "-R"** (`:335`, copy-paste) — it's the `-P/--export-score-parts` check that `-o` was set first. Order matters: the `-P` block runs after the `-o` block but is evaluated from `outputFile`, so `-P` with no `-o` just logs and stays `ConvertType::File`/default.
3. **`-P` without `*` overwrites** (see §3.5): every part's PDF/MP3 resolves to the same path.
4. **`--score-video` clobbers `-o`'s type** but reuses its output path (`:413-417`). `-o` must still be given for the file name.
5. **`--score-*` JSON goes to stdout unless `-o` is also passed** (the `-o` value is reused as output file); stdout is preceded by the console banner (§7). `BackendApi::openOutputFile`'s failure is ignored (`backendapi.cpp:221-232` result unused by callers).
6. **Batch `out` array bug (TRACED):** in `parseBatchJob` the loop over `outArray` (`:469-481`) calls `rv.val.push_back(std::move(partJob))` **outside** the `if (isString) … else if (isArray && size==2)` chain, so an item that is neither a string nor a 2-array still pushes a job with an **empty `out`**; that job then fails with `ConvertTypeUnknown` (empty suffix) and is reported in the batch errors.
7. **Error messages for malformed `visibleParts`/`copyright` are blank:** they call `make_ret(Err::BatchJobFileFailedParse, err.errorString()…)` where `err` is the *document-level* `QJsonParseError` that is `NoError` (`:432,442,453`).
8. **`copyright.showOnAllPages` is parsed but unused** — `convertFile` only reads `copyright.text` (`:260-272`); the text is appended (no separator) to both odd/even footer-center styles for the whole score.
9. **`--page` is 1-based** at the CLI and in the job file; `--page 0` would underflow (`toULongLong(&ok) - 1`, `consoleapp.cpp:53`, then `page_num_t` = `size_t`). Not validated. Region/measure/staff/voice numbers are also 1-based.
10. **Targets are ignored in some paths:** with `--extension` (page/region ignored), and region/page are honored only for a native `mscz/mscx/mscs` output (region) or any writer that takes `PAGE_NUMBER` (page).
11. **`-S` during import silently falls back to the saved preference style** if not given (§5.1) — CLI conversions of foreign files are not hermetic with respect to Preferences.
12. **`--transpose` only works with `-o`** (`:319`); with `-j` use the job `transpose` object; with `--score-transpose` the JSON is the flag's own argument.
13. `--sound-profile` with an unknown profile only logs `Unknown sound profile` and continues without override (`consoleapp.cpp:203-206`).
14. **Unknown options:** `parse()` ignores them, but `processBuiltinArgs → QCommandLineParser::process` then reports and exits (standard Qt behavior; HYPOTHESIS — not read from Qt source here).
15. `-t/--test-mode` help text says "this includes --template-mode" (`:82` comment) but the code sets two independent flags (`MScore::testMode`, `MScore::saveTemplateMode`); whether `testMode` implies template behavior inside engraving was not traced.
16. Diagnostic and testflow options set `ConsoleApp`, so they also run through `addConsoleModules`; `--diagnostic-*` returns `NotSupported` when the diagnostics draw provider isn't built.

## UNCLEAR — needs verification

- ~~Literal Windows executable name~~ RESOLVED: `MUSE_APP_NAME_MACHINE_READABLE` is set in `version.cmake:22` (top-level, not under `muse\`). In the Reprise fork it is `"Reprise"` and the version suffix was dropped from the exe name, so the Windows exe is `Reprise.exe` (upstream was `MuseScoreStudio5.exe`).
- Whether a writer for `mscs` is registered anywhere (only `mscz`/`mscx` seen in `notationmodule.cpp`).
- Exact semantics of the `Key` ints for `targetKey` (enum `Key` at `types.h` ~1440-1460; `MIN=C_B`, `MAX=C_S` assumed -7..7).
- Whether GUI mode applies the export/import override flags (`-r`, `-T`, `-b`, `--musicxml-*`, `--gp-*`) or only `ConsoleApp` does (`guiapp.cpp` not re-read for this).
- Platform-specific headless behavior on Windows/macOS (offscreen platform plugin availability).
- `--extension` URI string grammar (`UriQuery`): only `action` query param (default `main`) is consumed in `convertByExtension`.
- Behavior when `MUE_ENABLE_CONSOLEAPP` is OFF (WASM): the parser and console app are not compiled at all (see `app-shell-and-startup.md` §2).

*Authored from (2026-11-08 deep pass): `src\app\internal\commandlineparser.cpp` (whole), `src\app\cmdoptions.h`, `src\app\internal\consoleapp.cpp` (whole), `src\app\main.cpp`, `src\app\appfactory.cpp:451-587`, `src\app\CMakeLists.txt`, `src\converter\**` (`convertercontroller.cpp/.h`, `converterutils.cpp`, `convertercodes.h`, `convertertypes.h`, `iconvertercontroller.h`, `convertermodule.cpp`, `api\converterapi.*`, `internal\compat\backendapi.cpp`), `src\project\internal\notationproject.cpp:157-359`, the importexport `*module.cpp` registrations, `vtest\vtest.sh`, `muse\framework\global\settings.cpp:96-134`. Not read in full: `backendjsonwriter`, `notationmeta`, `scoreelementsscanner` internals, the individual writers' option handling. **If this contradicts the live source, the source wins.***
