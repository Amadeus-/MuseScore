# MuseScore — Testing & vtest

> **What this file is.** How MuseScore 5.0 `main` is tested: the **gtest/gmock unit-test framework** (`muse\framework\testing` + per-module `tests\` dirs, declared by `SetupGTest`/`gtest.cmake`, run via **ctest**), the **vtest** visual-regression pipeline (`vtest\`), and the legacy `test\` data dir. Source-verified 2026-11-08. **Live source wins** — if this contradicts the tree, fix the Guide. Start at `00-core.md`.

## 0. 60-second orientation

- **Unit tests = gtest + gmock.** Every testable module has a `tests\CMakeLists.txt` that sets `MODULE_TEST*` variables then `include(SetupGTest)` → `muse\framework\testing\gtest.cmake`, which builds **one executable per module** (e.g. `engraving_tests`, `muse_global_tests`, `playback_test`) linking the shared `gmain.cpp` + `environment.cpp` and registers it with `add_test(...)` (`muse\framework\testing\gtest.cmake:87`). Run all with `ctest`.
- **The harness is an IoC mini-app.** `muse::testing::Environment` boots the real `GlobalModule` + the module(s) a suite declares as dependencies, so tests can resolve real services, and replace chosen ones with gmock mocks (`muse\framework\testing\environment.cpp:58`).
- **Engraving tests are score-in/score-out.** `src\engraving\tests\*_tests.cpp` each own a `*_data\` dir of `.mscx` files; typical idiom = `ScoreRW::readScore` → mutate → `ScoreComp::saveCompareScore` (text-diff of written `.mscx` vs a stored reference).
- **vtest = draw-data regression, not pixels.** Scores in `vtest\scores` are laid out and painted into a *buffered* `DrawData` JSON; the current JSON is compared with a reference JSON (`vtest\reference\{default,small}`). A separate PNG pipeline (`vtest-generate-pngs.sh` + ImageMagick `compare`) is what **CI actually runs** (`check_visual_tests.yml`), comparing two *builds* (PR base vs PR head), not a checked-in baseline.
- **`test\`** (repo root) = MuseScore-3-era data, **not referenced by any build script / CMake / workflow** (grep-verified). Historical.
- CI: `.github\workflows\check_unit_tests.yml`, `check_visual_tests.yml`. PR template asks "I created a unit test or vtest" (`.github\pull_request_template.md:16`).

## 1. The unit-test framework (`muse\framework\testing\`)

Files (the whole dir): `README.md` (2 lines: "Unit testing infrastructure. Uses Google Test"), `gmain.cpp`, `environment.{h,cpp}`, `gtest.cmake`, plus an **unused-looking QtTest path** (`qmain.cpp`, `qtest.cmake`, `qtestsuite.h`).

### 1.1 `gmain.cpp` — the shared `main()`

`GTEST_API_ int main(...)` (`gmain.cpp:38`) is compiled into every gtest executable:
1. Sets org name/domain, constructs a **`QGuiApplication`** (so Qt Gui/fonts/QImage work), `qputenv("QML_DISABLE_DISK_CACHE","true")`.
2. Strips quotes from `--gtest_filter="Suite.Test"` (a QtCreator-debugger quirk, `gmain.cpp:47-76`).
3. `muse::runtime::setThreadName("main")`, then **`muse::testing::Environment::setup()`** (`gmain.cpp:81`).
4. `InitGoogleMock`, `death_test_style = "threadsafe"`, `PROFILER_CLEAR`, `RUN_ALL_TESTS()`, `Environment::deinit()`, `PROFILER_PRINT`.

### 1.2 `Environment` / `SuiteEnvironment` — booting modules for a suite

`environment.h:35-102`. `Environment` is all-static: `setDependency(Modules)`, `setPreInit`, `setPostInit`, `setDeInit`, `setup()`, `deinit()`. `SuiteEnvironment` is the **static-initializer helper**: constructing a file-scope `SuiteEnvironment` object stores those four settings before `main()` runs.

`Environment::setup()` (`environment.cpp:58-109`) mirrors the app boot phases (see `architecture-and-ioc.md`), with **RunMode = `GuiApp`** and a stubbed application:
```cpp
// Environment::setup
muse::modularity::globalIoc()->registerExport<IApplication>("testing", new muse::ApplicationStub());
m_globalModule = new GlobalModule();   // registerResources/Exports/UiTypes
// each dependency module: registerResources, registerExports  -> then resolveImports + registerUiTypes
// onPreInit  -> m_preInit()  -> onInit -> onAllInited -> onStartApp -> m_postInit()
```
Order: globals first, then deps, per phase. `deinit()` (`:111`) runs `m_deInit`, `onDeinit`, `onDestroy`, `qDeleteAll(modules)`, `modularity::resetAll()`.

⭐ A module is **only** live in a test binary if the suite's `SuiteEnvironment` lists it. Everything else is whatever the test registers itself (mocks) or a missing import (resolves to null).

### 1.3 `gtest.cmake` — what a test target gets

Inputs (documented `gtest.cmake:21-30`): `MODULE_TEST` (target name), `MODULE_TEST_SRC`, `MODULE_TEST_INCLUDE`, `MODULE_TEST_DEF`, `MODULE_TEST_LINK`, `MODULE_TEST_DATA_ROOT`. Effects:
- `add_executable(${MODULE_TEST} gmain.cpp environment.cpp environment.h ${MODULE_TEST_SRC})` (`:37`).
- Include dirs: `src`, the muse framework paths, and the fetched googletest headers via `GLOBAL PROPERTY googletest_SOURCE_DIR` (`:44-63`).
- ⭐ **`target_compile_definitions(... ${MODULE_TEST}_DATA_ROOT="${MODULE_TEST_DATA_ROOT}")`** (`:65-68`) → tests reference their data dir via a macro named after the target, e.g. `engraving_tests_DATA_ROOT`, `project_test_DATA_ROOT`, `iex_bww_tests_DATA_ROOT`.
- Links `Qt6::Core Qt6::Gui gmock muse_global ${MODULE_TEST_LINK}`; with `MUSE_ENABLE_UNIT_TESTS_CODE_COVERAGE` adds `-fprofile-arcs -ftest-coverage --coverage` (`:69-73`).
- **`add_test(NAME ${MODULE_TEST} COMMAND ${MODULE_TEST})`** (`:87`) — one ctest entry per module binary.

`muse\buildscripts\cmake\SetupGTest.cmake:21` is simply `include(${MUSE_FRAMEWORK_SRC_PATH}/testing/gtest.cmake)`, hence module files say `include(SetupGTest)`.

### 1.4 The QtTest alternative (`qmain.cpp`, `qtest.cmake`, `qtestsuite.h`)

A parallel harness for `QObject`-slot tests: `qtest.cmake` links `Qt6::Test` + `Widgets`, `QTestSuite` keeps a static list of registered `QObject` tests and `QTEST_MAIN(className)` is redefined to `DECLARE_TEST` (`qtestsuite.h:119-124`) so many QTest classes can share one binary. **Not used by any live CMake** (only `src\importexport\bww\tests\tst_bww_io.cpp` and three `DISABLED_*.cpp` still contain `QTEST_MAIN`; the bww one is commented out of its CMake: `src\importexport\bww\tests\CMakeLists.txt:29` "outdate"). Treat gtest as the only active framework.

## 2. How tests are declared, enabled, built, run

### 2.1 Option layers (top-down)

| Option | Where | Meaning |
|---|---|---|
| `MUSE_ENABLE_UNIT_TESTS` (default ON) | `muse\framework\cmake\MuseDeclareOptions.cmake:20` | Master switch. When ON, `muse\framework\CMakeLists.txt:161-168` calls `googletest_add_to_build()` and defines the `OUTPUT_XML` target property. When OFF, `SetupConfigure.cmake:381-391` force-clears every `MUE_BUILD_*_TESTS`. |
| `MUSE_MODULE_<NAME>_TESTS` | `MuseDeclareOptions.cmake:33` (macro `declare_muse_module_opt`) | Per framework module, default = `MUSE_ENABLE_UNIT_TESTS`. |
| `MUE_BUILD_<MODULE>_TESTS` | `CMakeLists.txt:96-155` | Per app module: `BRAILLE`, `CONVERTER`, `ENGRAVING` (default ON), `IMPORTEXPORT` (ON), `MUSESOUNDS`, `NOTATION`, `NOTATIONSCENE`, `PLAYBACK`, `PROJECT`. Most default to "is the module built". |
| `MUSE_ENABLE_UNIT_TESTS_CODE_COVERAGE` | `MuseDeclareOptions.cmake:19` | gcov flags on every module (`MuseCreateModule.cmake:123-127`) and on test exes. |
| `MUSE_BUILD_UNIT_TESTS` / `MUSESCORE_BUILD_UNIT_TESTS` | env vars in `muse\ninja_build.sh:35`, `ninja_build.sh:108` | Script-level knobs that become `-DMUSE_ENABLE_UNIT_TESTS=...`. `muse\ninja_build_utests.sh` sets it ON. |

A module wires its tests in its own `CMakeLists.txt` behind its flag — e.g. `src\engraving\CMakeLists.txt:511-514`:
```cmake
if (MUE_BUILD_ENGRAVING_TESTS)
    add_subdirectory(tests)
    add_subdirectory(api/tests)
endif()
```
⚠ `MuseCreateModule.cmake:103-107` adds the googletest include dir to **every** module target when unit tests are ON (so production headers can include gtest-based test helpers/friends).

### 2.2 The `UTEST` build configuration

`SetupConfigure.cmake:276-299`: `BUILD_CONFIGURATION == UTEST` forces `MUSE_ENABLE_UNIT_TESTS ON`, `MUSE_MODULE_AUDIO ON`, **ASan ON** (unless coverage), `MUSE_MODULE_TESTFLOW OFF`, and — when *not* doing coverage — turns OFF modules that have no tests (`AUTOMATION, LEARN, MPE_QML, MUSESAMPLER, SHORTCUTS, TOURS …`). The comment (`:293`) warns: *if you add tests to a module that had none, make sure the module stays enabled here.* With coverage every module is built so untested ones count as uncovered. ⭐ This is the likeliest reason "my new tests are not built in CI".

### 2.3 Running locally

- **Build (all tests):** `MUSESCORE_BUILD_UNIT_TESTS=ON … ninja_build.sh` (the CI recipe is `buildscripts\ci\linux\build_utest.sh:58-67`: `MUSESCORE_BUILD_CONFIGURATION=utest MUSE_APP_BUILD_MODE=dev … MUSESCORE_COMPILE_USE_UNITY=ON bash ./ninja_build.sh -t installdebug`). A plain CMake configure already defaults `MUSE_ENABLE_UNIT_TESTS=ON`.
- **Run:** from the build dir, `ctest -V` (CI: `buildscripts\ci\linux\runutests.sh:25-37` — `cd build.debug`, `GTEST_OUTPUT=xml:test-results`, `GTEST_COLOR=1`, **`QT_QPA_PLATFORM=minimal:enable_fonts`**, `ASAN_OPTIONS=detect_leaks=0:new_delete_type_mismatch=0`, then `ctest -V`).
- **One module / one test:** run the binary directly (`engraving_tests --gtest_filter=Engraving_BeamTests.*`). Because `main` is shared, standard gtest flags work.
- Test exes are named per module; see the inventory (§6).

### 2.4 `check_unit_tests.yml`

Triggers: `pull_request`, `workflow_dispatch`, `workflow_call`, and a **weekly cron** (`0 3 * * 4`, "Thursday night") — the cron/`code_coverage` input turn on coverage (`:97-105`). Steps: checkout (+submodules) → ccache → Qt **6.11.2** (`jurplel/install-qt-action`) → `buildscripts/ci/linux/setup.sh` → `build_utest.sh` → `runutests.sh` (`ASAN_OPTIONS: detect_leaks=0`) → optional coverage report via `muse\buildscripts\ci\codecoverage\make_code_coverage.cmake`, upload, and publish to `musescore/mss_code_coverage` (main repo only). Runner `ubuntu-24.04`. Full CI context → `ci-and-release.md`.

## 3. Representative test idioms

### 3.1 Engraving suite bootstrap (`src\engraving\tests\environment.cpp:39-80`)

A file-scope `SuiteEnvironment engraving_se(...)` lists the modules (`DrawModule`, `EngravingModule`), a **postInit lambda** and a **deInit lambda**:
```cpp
// engraving tests environment.cpp (postInit)
ScoreRW::setRootPath(String::fromUtf8(engraving_tests_DATA_ROOT));
MScore::testMode = true;  MScore::noGui = true;
loadInstrumentTemplates(":/engraving/instruments/instruments.xml");
// NiceMock<EngravingConfigurationMock> with ON_CALL defaults (black colors, debuggingOptions, allowReadingImagesFromOutsideMscz)
globalIoc()->unregister<IEngravingConfiguration>("utests");
globalIoc()->registerExport<IEngravingConfiguration>("utests", configurator);
```
The deInit lambda manually `delete`s the mock ("HACK … some score objects are not deleted", `:72-78`) — a known leak workaround.

### 3.2 Utilities (`src\engraving\tests\utils\`)

| Helper | Role |
|---|---|
| `ScoreRW::setRootPath/rootPath` (`scorerw.h:41`) | Test-data root (set by the environment). |
| `ScoreRW::readScore(name, isAbsolutePath=false, importFunc=nullptr, iocCtx=nullptr)` (`scorerw.cpp:58`) | Creates a `MasterScore` with base style (`compat::ScoreAccess::createMasterScoreWithBaseStyle`), loads `.mscz/.mscx` via `compat::loadMsczOrMscx`, or an `ImportFunc` for other suffixes (else `Err::FileUnknownType`); then `doLayout()` on every score in `scoreList()`. Returns `nullptr` on failure. Uses a fixed test IoC context `Context(1)` (`:46`). |
| `ScoreRW::saveScore(score, name[, exportFunc])` (`:91`) | `File::remove(name)` then write via `RWRegister::writer()->writeScore` into a buffer. |
| `ScoreRW::writeElement` / `writeReadElement` (`:119,130`) | Round-trip one element through XML. |
| `ScoreComp::saveCompareScore(score, saveName, compareWithLocalPath)` (`scorecomp.cpp:115`) | Save, then `compareFiles(rootPath/…ref, saveName)`. |
| `ScoreComp::compareFiles` (`:131`) | Line-split (CRLF-tolerant) both files; if unequal prints a **unified-diff** (LCS dp, 3 lines context) to the log and returns false. Hand-rolled — no external diff tool. |
| `TestUtils::createPart/createEmptyPart/createParts` (`testutils.h:31`) | Excerpt/part scaffolding. |

### 3.3 Fixture + data-file round trip (`beam_tests.cpp:45-66`)

```cpp
static const String BEAM_DATA_DIR("beam_data/");
class Engraving_BeamTests : public ::testing::Test { public: void beam(const char* path); };

void Engraving_BeamTests::beam(const char* path) {
    MasterScore* score = ScoreRW::readScore(BEAM_DATA_DIR + String::fromUtf8(path));
    ASSERT_TRUE(score);
    EXPECT_TRUE(ScoreComp::saveCompareScore(score, String::fromUtf8(path), BEAM_DATA_DIR + String::fromUtf8(path)));
    delete score;
}
```
Naming convention: fixture `Engraving_<Thing>Tests`; data in `<thing>_data\`; the *input* file doubles as the *reference* (load → layout → re-save → must equal the input text, i.e. layout/RW idempotence).

### 3.4 Behavioral test via transactions + undo (`beam_tests.cpp:68-124`)

```cpp
std::unique_ptr<MasterScore> score(ScoreRW::readScore(BEAM_DATA_DIR + u"historical-tab-grid.mscx"));
score->select(cr, SelectType::SINGLE);
score->transactionManager()->transaction(TranslatableString::untranslatable("Engraving beam tests"),
    [&](Transaction& tx) { EditBeam::setBeamMode(tx, score.get(), mode); });
ASSERT_EQ(first->beamMode(), BeamMode::BEGIN);
score->undoRedo(true, &ed);   // undo
ASSERT_TRUE(ScoreRW::saveScore(score.get(), u"historicalTabGridModes.mscx"));
std::unique_ptr<MasterScore> reopened(ScoreRW::readScore(u"historicalTabGridModes.mscx", true)); // absolute path
```
Shows the 5.0 edit model (`Transaction&` + `EditBeam::…`, see `engraving-dom.md` "editing/undo") and the absolute-path reload idiom. Output files are written to the **current working dir** (ctest's cwd = build dir).

### 3.5 gmock-heavy service test (`src\playback\tests\playbackcontrollertests.cpp`)

Includes `gtest`+`gmock`, **mocks from sibling modules' `tests\mocks\`** (`project/tests/mocks/…`, `notation/tests/mocks/*mock.h`, `rcommand/tests/mocks/commanddispatchermock.h`), `NiceMock`, `Return/ReturnRef/ReturnRefOfCopy`, custom `MATCHER_P(VolumeEnvelopeIs, …)` (`:119`), and a test-specific `muse::modularity::IoCID TEST_CONTEXT_ID = 1` (`:69`). Pattern: construct the controller, register mocks into the IoC (context-scoped), drive it, assert on mock expectations. Mock headers live **in each module's `tests\mocks\`**, not a central place; framework-wide ones are in `muse\framework\global\tests\mocks\` (`applicationmock.h`, `applicationstub.h`, `filesystemmock.h`, `globalconfigurationmock.h`, `processmock.h`, `systeminfomock.h`).

### 3.6 Plain pure-logic test (`muse\framework\global\tests\ret_tests.cpp`)

No data, no fixture: `TEST(Global_RetTests, Data_KeyExistsWithMatchingType_ReturnsStoredValue)` etc. Naming style in framework tests: `Suite_Subject` + `Scenario_Expectation`.

### 3.7 Disabled / dead tests

- `DISABLED_*.cpp` in `src\engraving\tests\` (`concertpitch_benchmark`, `midimapping_tests`, `textedit_tests`) are **not in the CMake list**; the list also comments out `concertpitch_tests.cpp`, `midimapping_tests.cpp`, `textedit_tests.cpp` with "doesn't compile and needs actualization" (`src\engraving\tests\CMakeLists.txt:47,70,93`). Individual cases can also be gtest-`DISABLED_` (e.g. `Engraving_DrawDataTests.DISABLED_Rw`, `drawdata_tests.cpp:67`).
- `drawdata_tests.cpp` is compiled only with `MUE_BUILD_ENGRAVING_DEVTOOLS` (`tests\CMakeLists.txt:120`); playback tests only with `MUE_BUILD_ENGRAVING_PLAYBACK` (`:126`).

## 4. The vtest visual-regression pipeline (`vtest\`)

`vtest\README.md` is two lines: *"The main idea is to compare the current draw data with the reference data"* (+ a wiki link). Layout (verified `ls`): `scores\` (~274 `.mscx`, with a `disabled\` subdir), `scores_small\` (31 `.mscz` + `test.mscx`), `gp_small\` (`test.gp`), `reference\{default,small}\` (draw-data **JSON**, ~357 files in `small`), `small.mss` (a style), `style.css`, the scripts, `vtest.js`, `vtest_context.json`, `vtest_gtest_runner.cpp`, `CMakeLists.txt`.

> There are **two** parallel mechanisms; do not conflate them.

### 4.1 Mechanism A — DrawData JSON (script-driven, `vtest.js`)

**Capture.** The painter used for layout/paint can record into a `BufferedPaintProvider` (cross-ref `layout-and-rendering.md` §paint path / `muse\framework\draw`): every `beginObject`/draw call is stored as a `DrawData` tree (`item.chilren[].datas[]` with `paths/polygons/texts` + shared `states` = pen/brush/font/transform/…), see how it is built in `src\engraving\tests\drawdata_tests.cpp:112+` (`Painter p(prv,"test"); p.beginObject("page_1"); p.drawLine(...)`). `DrawDataGenerator` (`src\engraving\devtools\drawdata\`) loads a score, lays it out, paints with that provider and serializes via `DrawDataRW::writeData` to JSON.

**Compare.** `DrawDataComparator::compare(ref, test, outdiff)` (`drawdatacomparator.cpp:42-64`) reads both JSONs, runs `DrawDataComp::compare`, and if the `Diff` is non-empty writes `DrawDataRW::writeDiff(outdiff, diff)` and returns `make_ret(Err::DDiff)`. `DiagnosticDrawProvider::compareDrawData` (`diagnosticdrawprovider.cpp:56-82`) additionally, on a diff, copies `*.ref.json`/`*.json` (`ComOpt.isCopySrc`) and renders `*.ref.png`, `*.png`, `*.diff.png` (`ComOpt.isMakePng`; the diff PNG paints origin grey `#999999` and diff red `#ff0000`, cf. `drawdata_tests.cpp:58-65`).

**Options.** `GenOpt{ SizeF pageSize }`, `ComOpt{ isCopySrc=true, isMakePng=true }` (`drawdatatypes.h:28-35`). Two variants are run (`vtest.js:12-14`): **`default`** (`opt_default: {}`) and **`small`** (`pageSize` 60×130, `vtest_context.json:8-10`) — the small page forces many line breaks / dense layout.

**Driver.** `vtest.js` is a **testflow** script run through the app's CLI:
```
mscore --test-case ./vtest/vtest.js --test-case-context ./vtest/vtest_context.json [--test-case-func generateRefDrawData]
```
(`vtest.sh`, `gen-ref-data.sh`; flags registered at `src\app\internal\commandlineparser.cpp:176-180`). `vtest_context.json` supplies `score_dir ./vtest/scores`, `reference_data_dir ./vtest/reference`, `current_data_dir ./vtest/temp_current`, `comparison_dir ./vtest/temp_comparison`. Steps (`vtest.js:19-54`): Clear → generate (default) → PNGs → compare → generate (small) → PNGs → compare → `createReport()` (writes `vtest_compare.html`, `vtest.js:131-187`) → Finish (`return {code: 3101}` if any diff). `generateRefDrawData()` (`:71`) is the entry that (re)creates the reference JSON. The same operations exist as direct CLI flags: `--diagnostic-gen-drawdata <dir|file> --diagnostic-output <out>`, `--diagnostic-com-drawdata a.json b.json`, `--diagnostic-drawdata-to-png`, `--diagnostic-drawdiff-to-png` (examples in comments at `diagnosticdrawprovider.cpp:36-41`; dispatch `src\app\internal\consoleapp.cpp:289-298`; see `mscore-cli.md`).
Both scripts export `QT_QPA_PLATFORM=offscreen`, `MU_QT_QPA_PLATFORM=offscreen`, `XDG_RUNTIME_DIR=/tmp/runtime-root`. `vtest.sh` always `exit 0` ("At moment needs for CI", `:44-46`) — its exit code is deliberately ignored.

⚠ **Mechanism A is currently disabled in CI** — the `Generate ref drawdata` / `DrawData VTest` steps in `check_visual_tests.yml:270-277` are commented out. It is still runnable by hand.

### 4.2 Mechanism B — PNG diff between two builds (what CI runs)

1. **Build the VTEST configuration** (`SetupConfigure.cmake:200-…`: `MUE_CONFIGURATION_IS_VTEST`, `MUSE_ENABLE_UNIT_TESTS OFF`, `MUSE_MODULE_UI_QML OFF`, TESTFLOW/TOURS/SHORTCUTS/NETWORK off — a lean engraving-only app). `buildscripts\ci\vtests\build_and_pack.sh` builds it as an **AppImage** (`MUSESCORE_BUILD_CONFIGURATION="vtest" bash ninja_build.sh -t appimagedebug` → `MuseScore-Studio-vtest.AppImage`). `vtest\build.sh` is the local equivalent (`-t installdebug`).
2. **Generate PNGs** — `vtest\vtest-generate-pngs.sh`: wipes the output dir, writes a JSON **job file** (`vtestjob.json`: one `{ "in": score, "out": png }` per file in `scores\`, terminated by `{}` — `:58-66`), then runs the app's **batch-convert mode**: `mscore [-S style] -j vtestjob.json -r $DPI [--gp-linked]` (`:70-74`; default DPI **180**; log in `convert.log`). See `mscore-cli.md` for `-j`/`-r`.
3. **Compare** — `vtest\vtest-compare-pngs.sh`: for every PNG in the reference dir, if the current file exists run ImageMagick **`compare -metric AE -fuzz 0.0%`** (`:70`; **exact** pixel match, any differing pixel > 0 fails); on difference copy ref/current, make `*.diff.png` and (unless `--gen-gif 0`) an animated `convert -delay 80 -loop 0 current ref` GIF, then build `vtest_compare.html` (side-by-side Reference / Current / Diff) and append `VTEST_DIFF_FOUND=true` to `$GITHUB_ENV`. `--ci 1` makes it exit 0 even on diffs (CI turns the flag into a PR failure later); without it, it exits 1. Requires the `compare`/`convert` binaries (`sudo apt install imagemagick`, `:52-57`).
4. **Three variants** in `buildscripts\ci\vtests\generate_pngs.sh:36-39`: full `scores\` @180 dpi → `pngs`; `scores_small\` @460 dpi with style `vtest\small.mss` → `pngs_small`; `gp_small\` @460 dpi with `small.mss` and **`--gp-linked`** (Guitar-Pro import with linked tab/std staves) → `pngs_gp_small`.

⭐ A file only in *current* (new score) is silently skipped: the loop iterates the **reference** list and tests `if test -f $PNG_CUR_FILE` (`vtest-compare-pngs.sh:59-69`). A reference file missing from current is also skipped (no "missing" failure).

### 4.3 `check_visual_tests.yml` (PR gate)

Triggers `pull_request`, `workflow_dispatch`, `workflow_call` (optional `reference_ref`). Jobs:
- **setup** (`ubuntu-slim`): decides `do_run` and the **reference ref** — for a PR the *base branch* (`github.event.pull_request.base.ref`), for a push `event.before`, for manual `inputs.reference_ref || github.sha`; builds the artifact name `VTests Comparison <build#> <PR info>`.
- **build_current** / **build_reference** (parallel, `ubuntu-24.04`): each checks out (reference with `ref: reference_ref`), ccache, Qt 6.11.2, `ci/linux/setup.sh`, `ci/vtests/build_and_pack.sh`, upload artifacts `current_build` / `reference_build`.
- **generate_and_compare**: installs `libegl1 imagemagick`, sets `QT_QPA_PLATFORM=offscreen`, downloads both AppImages into `vtests_current`/`vtests_reference`, runs `generate_pngs.sh` for each, then `vtest-compare-pngs.sh --ci 1` three times (full, `small`, `gp`). Uploads `comparison` if `VTEST_DIFF_FOUND`. **On a PR with diffs it `exit 1`s** with "please carefully review…artifact" (`:286-291`); on a push it posts a commit comment. A (commented-out, `:278-285`) 'vtests' PR label used to waive the failure.

⭐ **There is no checked-in PNG baseline.** The "reference" is the *base commit built fresh* — a change that intentionally alters rendering fails the check by design and the reviewer inspects the HTML report. (The checked-in `vtest\reference\` JSON belongs to Mechanism A.)

### 4.4 `vtest_gtest_runner.cpp` + `vtest\CMakeLists.txt` — ctest integration

Optional gtest target `vtests` (`CMakeLists.txt:21`): `Engraving_VTest.1_GenerateRef` runs `vtest-generate-pngs.sh --mscore $VTEST_MSCORE_REF_BIN --output-dir ./reference_pngs`; `2_GenerateCurrentAndCompare` runs it with `$VTEST_MSCORE_BIN` (= `${INSTALL_BIN_DIR}/mscore`) into `./current_pngs`, then `vtest-compare-pngs.sh --gen-gif 0` (`vtest_gtest_runner.cpp:61-81`; 5-minute `QProcess` timeout, `:53`). It is only added when `MUE_BUILD_ENGRAVING_TESTS` is ON **and** the reference binary `MUE_VTEST_MSCORE_REF_BIN` exists on disk (`vtest\CMakeLists.txt:33-43`); that cache var defaults to `../MU_ORIGIN/MuseScore/build.debug/install/<bin>/mscore` (`CMakeLists.txt:168`) — a sibling checkout of origin built by the developer. ⇒ On a normal tree it is silently skipped. Bash-script based → **Linux/macOS only** in practice.

### 4.5 Adding / updating a vtest score

Drop a `.mscx` (or `.mscz` in `scores_small`) into the right dir; names are the PNG/JSON stem, so keep them unique. Because the CI reference is the base commit, a new score is "skipped" until it exists on the base branch. For Mechanism A, regenerate with `vtest.sh`/`gen-ref-data.sh -m <mscore>` and commit the JSON under `vtest\reference\<opt>\`. Files under `vtest\scores\disabled\` (e.g. `harmony-3.mscx`, `tremolo-1.mscx`) are excluded — `ls -p … | grep -v /` in `vtest-generate-pngs.sh:60` lists only non-directory entries.

## 5. The shared test data

### 5.1 Per-module data (the live ones)

Each module's data lives **beside its tests**, addressed through `${MODULE_TEST}_DATA_ROOT`: `src\engraving\tests\<topic>_data\` (e.g. `beam_data`, `compat114_data`, `selectionfilter_data`; `all_elements_data`, `spanners_data`, `parts_data`… ~60 dirs, plus `test.mscx`), `src\project\tests\data` (`CMakeLists.txt:54`), importexport per-format `tests\data`/dirs (musicxml, midi, guitarpro, mei, mnx, capella, ove, bww, bb, lyricsexport, tabledit), etc. `drawdata_tests.cpp:43-44` reaches the vtest scores via `DATA_ROOT + "/../../../vtest/scores"` — vtest scores are shared data too.

### 5.2 Root `test\` (legacy MuseScore-3-era)

~45 entries: `.mscx/.mscz` samples (`accidental3.mscz`, `beam1.mscx`, `harmony.mscx`, `mmrest.mscz`, `noteheads.mscz`, `repeat-*.mscz`, `slur*.`, `tab.mscx`, `test3.mscx`, `testsmall.mscx`), old `.msc`/`.xml`/`.MID`, `xmltest.mss`, subdirs `cap/ capx/ md/ midi/ midi10/ msc/ ntest/ reftest/ iotest/ testoves/ testswc/ bwwrefs/ META-INF/`. Its `README` describes scripts `iotest` (read `*.msc`, save, compare) and `rendertest` (render `*.xml` with lilypond **and** mscore → `.html`) and annotates each file (repeat1…repeat13 playback orders, MuseData/Lilypond/Band-in-a-Box samples). **Grep over CMake/shell/yml/cpp/py found no references to `test/`** — it is orphaned data; modern tests use their own `*_data\` dirs. Safe to read for ideas; do not assume anything depends on it.

## 6. Inventory — test executables (one `add_test` each)

Framework (`muse\framework\*\tests`): `muse_global_tests` (~35 `*_tests.cpp`: string, uri, val, ret, json, xml, zip, msgpack, ringqueue, rpcqueue…), `muse_actions_tests`, `muse_accessibility_tests`, `muse_audio_tests`, `muse_audio_rpcpacker_tests`, `muse_audioplugins_test`, `muse_cloud_tests`, `muse_diagnostics_tests`, `muse_dockwindow_tests`, `muse_draw_tests`, `muse_extensions_tests`, `muse_interactive_tests`, `muse_languages_test`, `muse_media_tests`, `muse_midi_test`, `muse_midiremote_test`, `muse_mpe_test`, `muse_multiwindows_tests`, `muse_network_test`, `muse_rcommand_tests`, `muse_shortcuts_tests`, `muse_ui_tests`, `muse_ui_qml_tests`, `muse_uicomponents_qml_tests`, `muse_update_test`, `muse_workspace_tests`.
App (`src\*`): `engraving_tests`, `engraving_api_tests`, `notation_tests` (mocks only — dir has only `CMakeLists.txt` + `mocks\`), `notationscene_qml_tests`, `playback_test`, `project_test`, `project_qml_tests`, `converter_tests`, `braille_tests`, `musesounds_test`, and `iex_{musicxml,midi,guitarpro,mei,mnx,capella,ove,bww,bb,lyricsexport,tabledit}_tests`. Plus `vtests` (optional, §4.4).

## Cross-subsystem dependencies

- **`architecture-and-ioc.md`** — `Environment::setup()` replays the module boot phases (`registerExports/resolveImports/onPreInit/onInit/…`); mocks work by registering into `globalIoc()` / per-context IoC.
- **`layout-and-rendering.md`** — vtest Mechanism A captures the painter stream through `BufferedPaintProvider`/`DrawData`; `ScoreRW::readScore` calls `doLayout()`.
- **`file-format-and-rw.md`** — `compat114/compat206`, `styledproperties_rw`, `readwriteundoreset`, `msczfile` tests exercise the readers/writers; `saveCompareScore` is an RW-idempotence check.
- **`engraving-dom.md`** — `Transaction`/`TransactionManager` edit idiom in tests.
- **`mscore-cli.md`** — vtest drives `-j`/`-r`/`-S`/`--gp-linked` and `--test-case*` / `--diagnostic-*` flags.
- **`ci-and-release.md`** — both workflows, Qt 6.11.2, ccache, coverage publishing.
- **`contributing-and-conventions.md`** — PR checklist requires a unit test or vtest.

## Non-obvious gotchas

- ⛔ **A test only sees the modules its `SuiteEnvironment` lists.** Forgetting `new SomeModule()` yields null IoC resolutions, not a compile error.
- ⛔ **New tests not built in CI?** `UTEST` config turns off modules without tests (`SetupConfigure.cmake:291-299`) — enable your module there.
- **`QT_QPA_PLATFORM`:** CI unit tests use `minimal:enable_fonts`; vtests use `offscreen`. Font-dependent layout differs across platform plugins/fonts → compare only with the same toolchain (this is why vtest compares two CI builds on one runner).
- **Pixel-exact compare:** `-fuzz 0.0%` — any anti-aliasing change from a Qt/FreeType/ImageMagick upgrade shows as a diff; both sides rebuilt in one run mitigates it.
- **Tests write into cwd** (`ScoreRW::saveScore(name)` with a bare name) — run from a scratch/build dir; stale outputs from earlier runs are `File::remove`d first.
- **Test-data macro naming:** `<target>_DATA_ROOT` — renaming `MODULE_TEST` silently breaks the macro used in sources.
- **Unity builds:** `MUSE_COMPILE_USE_UNITY` ON in `build_utest.sh`; anonymous-namespace/static helper name collisions across `*_tests.cpp` files can break the unity build — use per-file unique names.
- **`vtest.sh` exits 0 always** and the Mechanism-A CI steps are commented out; a green vtest script is not evidence.
- **vtest compare ignores added/removed files** (see §4.2 note).
- `ScoreComp` leaves the diff in the **log**, so read the test output (`ctest -V` / `--gtest_filter`) for the unified diff.
- Engraving death tests use `death_test_style=threadsafe` (re-exec) — slow but safe with Qt threads (`gmain.cpp:85`).
- Several engraving tests are disabled/uncompiled (§3.7); the deInit mock-delete HACK (`environment.cpp:72`) hides a real leak of score objects.
- Possible Guide-drift: the README link points at the MuseScore wiki "Visual Tests (VTests)" page (not read).

## UNCLEAR — needs verification

- Whether Windows/macOS developers can run vtest end-to-end (scripts are bash + ImageMagick; no Windows variant found). Likely Linux-first; **HYPOTHESIS**.
- Exact `DrawDataComp::compare` tolerance (e.g. real-number precision) — `muse\framework\draw\utils\drawdatacomp.cpp` not read (`SetCompareRealPrecision(3)` appears in `drawdata_tests.cpp:81`, suggesting a 3-digit precision for the Rw test).
- Whether `vtest\reference\{default,small}` JSON is still maintained now that Mechanism A is disabled in CI (counts only: `small` ~357 files).
- `muse\framework\*\tests` and importexport tests were enumerated by `CMakeLists.txt`, not each read; per-test content only sampled (beam, drawdata, playback controller, global ret).
- Source of Mechanism-A `GenOpt.pageSize` handling inside `DrawDataGenerator` (not read).
- `MUSE_MODULE_*_TESTS` flags: declared (`MuseDeclareOptions.cmake:33`) — how each framework module's `CMakeLists` consumes them was checked only for `global` (`muse\framework\global\CMakeLists.txt:279`).

*Authored (deep pass, 2026-11-08) from: `muse\framework\testing\*` (all files), `muse\buildscripts\cmake\SetupGTest.cmake`, `MuseDeclareOptions.cmake`, `MuseCreateModule.cmake`, `SetupConfigure.cmake` (UTEST/VTEST blocks), `CMakeLists.txt` option block, `src\engraving\tests\{CMakeLists,environment.cpp,beam_tests.cpp,drawdata_tests.cpp,utils\*}`, `src\playback\tests\playbackcontrollertests.cpp`, `src\engraving\devtools\drawdata\*`, all of `vtest\` (scripts, `vtest.js`, context, CMake, gtest runner), `buildscripts\ci\{linux,vtests}\*.sh`, `.github\workflows\check_{unit,visual}_tests.yml`, `test\README`. **If this contradicts the live source, the source wins.***
