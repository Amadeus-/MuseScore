# MuseScore Fork - Consolidated Bug / Oddity Catalog

*Compiled **2026-11-08** from the MuseScore 5.0 `main` dev-guide authoring passes (`Guide\*.md`, 18 topic files + `00-core.md`).*
*Findings are labeled **TRACED** (confirmed by reading the source control flow) vs **HYPOTHESIS** (plausible; runtime impact not reproduced). Each entry cites `file:line` plus the Guide file/section where it is detailed.*
*Nothing here has been fixed in source. If an entry contradicts the live source, the source wins and the entry should be corrected.*

**Private dev notes - fork-only, not for upstream PRs.**

Severity vocabulary: `data-loss`, `crash-risk`, `silent-failure`, `wrong-behavior`, `cosmetic`, `dead-code`, `doc-drift`, `hazard` (works today, easy to break), `security`.

Line numbers are as recorded by the Guide passes; re-verify before acting on one.

---

## 1. Engraving / DOM

- **E1 `toX()` casts are unchecked in release** - `hazard`, TRACED. `engravingobject.h` (`toChord(e)` etc.): asserts the type in debug, plain cast in release; wrong type is UB. See Guide/engraving-dom.md §5.
- **E2 `ldata()` read before layout** - `hazard`, TRACED. `LD_ACCESS` warning and possibly uninitialised geometry when `ldata()->pos()` is read before layout runs (`infrastructure\ld_access.h`). See Guide/engraving-dom.md §5.
- *(Cross-ref: `Score::doLayoutRange` spanner-range bug is filed under Layout, L1.)*

## 2. Layout / Rendering

- **L1 `Score::doLayoutRange` spanner range is last-wins, not union** - `wrong-behavior` (stale layout) / possibly full-layout flag flip, TRACED code, HYPOTHESIS impact. `src\engraving\dom\score.cpp:5021-5029`: in the spanner loop `start`/`end` are recomputed from the original `st`/`et` (`start = min(st, spanner->tick()); end = max(et, spanner->tick2());`) instead of accumulating, so only the last overlapping spanner counts. For a full layout `(0,-1)` it could also turn `et=-1` into a non-negative `end` and make `isLayoutAll` false in `ScoreLayout::layoutRange` (depends on whether `findOverlapping(0,-1)` returns anything - UNCLEAR). See Guide/layout-and-rendering.md (gotchas) and Guide/engraving-dom.md §3A.
- **L2 Potential forgotten shape updates** - `hazard`, TRACED (author comment). `rendering\score\systemlayout.cpp:418-419` admits "potential forgotten shape updates" - anything changing segment item widths after `Segment::createShapes()` (courtesy sigs, parentheses) must re-create shapes. See Guide/layout-and-rendering.md gotchas.

## 3. Style system

- **S1 `legacy-style-defaults-v450.mss` missing from `engraving.qrc`** - `silent-failure` (wrong defaults for old scores), TRACED qrc gap, HYPOTHESIS runtime effect. `src\engraving\engraving.qrc`; `defaultstyle.cpp:133`; `resolveStyleDefaults(450)` may find no resource. Also unclear whether `styleDefaultByMscVersion` intentionally has no 450 bucket. See Guide/style-system.md §4.2, §6, §7.
- **S2 `styleDef` / `Sid` table is positional with no order check** - `hazard`, TRACED. `styledef.h` enum vs `styledef.cpp:41-2262`: inserting a row at the wrong position shifts every later default silently. See Guide/style-system.md §1.2, §6.
- **S3 Six `TextStyle` tables have only 15 initializers** - `hazard` / possible `wrong-behavior`, TRACED aggregate-init, HYPOTHESIS effect. `textstyle.cpp:406-425` (`articulationTextStyle`, `tabFretNumber`, `hammerOnPullOff`, `stringTunings`, `fretDiagramFretNumber`, ...): the 16th element is value-initialized `{Undefined, Sid(0)=pageWidth, Pid(0)=SUBTYPE}`, not `NOSTYLE`, and loops over `*textStyle(tid)` visit it. See Guide/style-system.md §6, §7.
- **S4 `styleAbsolute` precomputed doubles possibly indeterminate** - `crash-risk`/`wrong-behavior`, HYPOTHESIS. `m_precomputedValues` valid only after `set`/`precomputeValues` (`defaultstyle.cpp:62,74,88`, `score.cpp:234`); a fresh `MStyle` falling back to defaults may hold indeterminate values. See Guide/style-system.md §6.
- **S5 `MStyle::set` has no change notification** - `hazard`, TRACED. Only undo commands call `Score::styleChanged()`; raw `style().set(...)` leaves items stale (`twrite.cpp:655-659` does this transiently). See Guide/style-system.md §6.
- **S6 `TextBase` flag index spans two tables** - `hazard`, TRACED. `textbase.cpp:2813-2829`: a subclass overriding only one of `getPropertyFlagsIdx`/`styleChanged`/`initElementStyle` mis-indexes flags. See Guide/style-system.md §6.
- **S7 `dynamicsTextStyle` aliases `expression*` sids** - `wrong-behavior` (surprising), TRACED. Editing "Dynamics" font edits Expression sids; `DYNAMICS` excluded from `editableTextStyles()`. See Guide/style-system.md §6.

## 4. File format / Read-Write

- **F1 Articulation `channe` write/read typo loses channel on round-trip** - `data-loss`, TRACED. `src\engraving\rw\write\twrite.cpp:814` writes `xml.tag("channe", ...)`; `read500\tread.cpp:1967` reads `channel`. Written 5.0 `channelName` is dropped as an unknown tag on re-read; old files writing `channel` still load. See Guide/file-format-and-rw.md §8 (and gotchas).
- **F2 `FileTooOld` message contradicts the gate** - `cosmetic`, TRACED. Message says "older than 2.0.0" (`engravingerrors.h:81`) but gate is `<114` (`mscloader.cpp:62`). See Guide/file-format-and-rw.md gotchas.
- **F3 `DirWriter::open` wipes the target directory** - `data-loss`, TRACED. `mscwriter.cpp:331` `removeRecursively`; pointing `MscWriter(Dir)` at a user folder deletes its contents - safe only via `NotationProject::doSave`'s `_saving` temp dir. See Guide/file-format-and-rw.md gotchas.
- **F4 Version attribute parsing unguarded** - `crash-risk`, TRACED code, HYPOTHESIS (bounds-check of `StringList::operator[]` unverified). `mscloader.cpp:230-231` indexes `sl[0]`,`sl[1]` with no length/`ok` check. See Guide/file-format-and-rw.md gotchas.
- **F5 Only version `300` rejected as "dev 3.0"** - `hazard`, TRACED. `mscloader.cpp:66`; odd in-between versions (301+, 115..207) have no gate. See Guide/file-format-and-rw.md gotchas.
- **F6 `readStyleDefaultsVersion` re-parses the whole score** - `cosmetic` (perf), TRACED. `rw\compat\readstyle.cpp:42-55`: extra full XML scan on every pre-4.0 open; also scans for any `defaultsVersion` element. See Guide/file-format-and-rw.md gotchas, Guide/style-system.md §7.
- **F7 `.mscs` (`XmlFileWriter`) is lossy** - `data-loss`, TRACED. `mscwriter.cpp:440-445` silently skips members other than `mscx/json/mss` (no chordlist, images, thumbnail). See Guide/file-format-and-rw.md §2, gotchas.
- **F8 Clef `isCourtesy` written but never read** - `data-loss` or harmless, HYPOTHESIS. Writer emits it; `TRead::read(Clef*)` has no branch. See Guide/file-format-and-rw.md §8.2.2, UNCLEAR.
- **F9 `Read500::readScoreFile` does not call `ctx.initLinks` / write `data->links` back** - `wrong-behavior` (link resolution) or intentional (EID linking), HYPOTHESIS. Compare `read400.cpp:78-80,117-118`; `mscloader.cpp:178` still seeds `partReadInData.links`. See Guide/file-format-and-rw.md §3, UNCLEAR.
- **F10 MM-rest write/read symmetry unclear in 5.0** - `wrong-behavior`, HYPOTHESIS. Writer emits generated MM-rest measures (`staffwrite.cpp:50`), reader builds them (`staffread.cpp:64-78`), yet `removeMMRestElements` runs for `<500` (`compatutils.cpp:199`). See Guide/file-format-and-rw.md UNCLEAR.
- **F11 Excerpts assumed same version as master** - `hazard`, TRACED. `mscloader.cpp:180` `makeReader(masterScore->mscVersion(), ...)`; excerpts only read for `>=400` containers (`:157`). See Guide/file-format-and-rw.md gotchas.
- **F12 Crash mid-save leaves `foo.mscz_saving`; part export/range save skip excerpts** - `hazard`, TRACED. Temp name literally `<containerPath>_saving`; `mscsaver.cpp:91`, `notationproject.cpp:922`. See Guide/file-format-and-rw.md gotchas.
- **F13 A "pure" save mutates the score (EIDs assigned on write)** - `hazard`, TRACED. `writeItemEid`; not undoable. See Guide/file-format-and-rw.md gotchas.

## 5. Notation layer

- **N1 View-mode deselect loop deselects nothing (dead condition)** - `dead-code`/`wrong-behavior`, TRACED code, HYPOTHESIS intent. `src\notation\internal\notationinteraction.cpp:356`: `if (!item->findAncestor(VBOX) || !item->findAncestor(FBOX)) continue;` - an item is deselected only with BOTH a VBox and FBox ancestor; `&&` probably intended. See Guide/notation-layer.md §7.3.
- **N2 `NotationPlayback::addSoundFlags/removeSoundFlags` mutate the score with no transaction** - `data-loss` (non-undoable edit) / `silent-failure`, TRACED mechanics, HYPOTHESIS whether callers bracket. See Guide/notation-layer.md §13.4.
- **N3 `navigateToNextSyllable` returns after `startCmd` without `endCmd`** - `crash-risk` (open command), TRACED flow, HYPOTHESIS reachability. `notationinteraction.cpp:7049-7055` (`if (!toLyrics && !cr) return;` after `score()->startCmd` at `:7049`). See Guide/notation-layer.md §11.2.
- **N4 `rollbackChanges()` does not emit `stackChanged`** - `silent-failure`, TRACED, intent UNCLEAR. `notationundostack.cpp:131-157` (commit/transaction do emit). See Guide/notation-layer.md gotchas.
- **N5 Nested `prepareChanges` is a silent no-op; its `commitChanges` closes the outer transaction early** - `wrong-behavior`, TRACED. `editing\transaction.cpp:146-156` (`transaction()` assert) vs `:166-169` (`beginTransaction` logs "cmd already active"). See Guide/notation-layer.md gotchas.
- **N6 `commitChanges/prepareChanges/rollbackChanges` silently ignored while the stack is locked** - `silent-failure`, TRACED. Edits during `setupNewScore` are applied but unrecorded. See Guide/notation-layer.md gotchas.
- **N7 `INotationStyle::setStyleValue` is not self-transacting** - `wrong-behavior` (non-undo-step style change if unbracketed), TRACED. See Guide/notation-layer.md §4.6, gotchas.
- **N8 Text/lyrics editing bypasses `NotationUndoStack` (`score()->startCmd/endCmd` direct)** - `silent-failure` (`stackChanged` not fired), TRACED consequence, HYPOTHESIS that Score shims onto the same manager. `editText`, `toggleFontStyle`, `toggleVerticalAlignment`, `navigateToLyrics*`. See Guide/notation-layer.md §11.2.
- **N9 `NotationSelection::doSelect` reads global Qt keyboard modifiers** - `hazard`, TRACED. `notationselection.cpp:440` `QGuiApplication::keyboardModifiers()`; selection depends on the physical Ctrl key (headless/MCP/test automation). See Guide/notation-layer.md gotchas.

## 6. Playback / Audio

- **P1 Hybrid-mode `m_engineController` dereferenced without null check** - `crash-risk`, HYPOTHESIS. `startaudiocontroller.cpp:187` (assigned on worker thread in `th_setupEngine`; driver opened at `:217`). See Guide/playback-and-audio.md gotcha 10.
- **P2 Fluid track with missing soundfont may never resolve `AddTrack`** - `silent-failure` (`isPlayAllowed()` stays false), HYPOTHESIS. `enginerpccontroller.cpp:229-270` (absent-and-not-loading branch ~270-282 unread). See Guide/playback-and-audio.md gotcha 6.
- **P3 `SynthResolver::resolveSynth` failure silently falls back to default Fluid** - `silent-failure`, TRACED. `eventaudionode.cpp:137-143`: broken VST/MuseSampler track plays with wrong sound. See Guide/playback-and-audio.md gotcha 7.
- **P4 Thread-ownership asserts compiled out** - `hazard`, TRACED. `audiosanitizer.h:61`. See Guide/playback-and-audio.md gotcha 8.
- **P5 IoC used from engine thread is not thread-safe** - `crash-risk`, TRACED (author comment). `enginesetup.cpp:49-53`, `enginecontroller.cpp:88-93`. See Guide/playback-and-audio.md gotcha 9.
- **P6 `Mixer::addTrack` lacks `ONLY_AUDIO_ENGINE_THREAD` marker that `addAuxTrack` has** - `cosmetic`, TRACED. `mixer.cpp:58` vs `:77`. See Guide/playback-and-audio.md gotcha 11.

## 7. MIDI / VST / MuseSampler

- **M1 "Could not reload MuseSampler library" shown on success** - `wrong-behavior`, TRACED. `musesampleractioncontroller.cpp:88-90`: condition not negated for `reloadAllInstruments()`. See Guide/midi-vst-musesampler.md gotchas (suspected bug 1).
- **M2 MuseSampler vibrato start-offset cap is 0** - `wrong-behavior`, TRACED code, HYPOTHESIS impact. `musesamplersequencer.cpp:502`: `(int64_t)0.1 * 1000000` casts first so `MAX_VIBRATO_STARTOFFSET_US == 0`. See Guide/midi-vst-musesampler.md (bug 2).
- **M3 Windows MIDI open failure reports "UNKNOWN"** - `cosmetic` (wrong message), TRACED. `winmidiinport.cpp:193` uses `errorString(ret)` (old OK `Ret`) not `openRes`. See Guide/midi-vst-musesampler.md (bug 3).
- **M4 `2 ^ (srcBits - 1)` is XOR, not power** - `wrong-behavior` (low impact), TRACED. `midievent.h:1215,1221` `scaleUp` center/mask. See Guide/midi-vst-musesampler.md (bug 4).
- **M5 `ExpressionContext::operator==` self-compares `velocityOverride`** - `wrong-behavior`, TRACED. `mpe\events.h:95`; velocity differences never detected. See Guide/midi-vst-musesampler.md (bug 5).
- **M6 `std::less` specializations are not strict weak orderings** - `hazard`, TRACED, appear unused. `vsttypes.h:149-209` (`ParamChangeEvent`/`NoteOnEvent`/`NoteOffEvent`/`VstEvent`). See Guide/midi-vst-musesampler.md (bug 6).
- **M7 `printApiStatus` prints `ms_MuseSampler_add_pitch_bend` twice** - `cosmetic`, TRACED. `libhandler.h:353,355`. See Guide/midi-vst-musesampler.md (bug 7).
- **M8 `ArticulationMap` averaging pairs offset-curve points by index** - `wrong-behavior`, TRACED. `mpetypes.h:837-870`; expects 11 points, patterns with other counts blend wrongly. See Guide/midi-vst-musesampler.md gotchas.
- **M9 VST: only first audio-effect class of a `.vst3` exposed; identity is file basename** - `silent-failure`/`hazard`, TRACED. `vstpluginmetareader.cpp:57-71`, `vstplugininstance.cpp:140-147`; shell bundles expose one plugin, same-named plugins collide. See Guide/midi-vst-musesampler.md gotchas.
- **M10 Windows out-port hides software synths; unmapped VST controllers drop silently** - `silent-failure`, TRACED. `MOD_SWSYNTH` filter (no "Microsoft GS Wavetable"); plugins not mapping `kCtrlSustainOnOff`/`kPitchBend` via `IMidiMapping` get no pedal/bend. See Guide/midi-vst-musesampler.md gotchas.

## 8. Plugin API

- **PA1 `Cursor::rewindToTick` can crash on a tick with no segment (and may land one segment early)** - `crash-risk`, TRACED. `src\engraving\api\v1\cursor.cpp:170-183`; prefer `rewindToFraction`. See Guide/plugin-api.md §8.
- **PA2 `PluginAPI::interval` parameter-name mismatch** - `doc-drift`, TRACED. Header names `(chromatic, diatonic)` (`qmlpluginapi.h:553`) but cpp is `(diatonic, chromatic)` (`qmlpluginapi.cpp:653`); cpp wins. See Guide/plugin-api.md §3 (table), UNCLEAR.
- **PA3 Several PluginAPI members are stubs** - `dead-code`/`silent-failure`, TRACED. `qmlpluginapi.cpp:264-474`: `scores` empty, `newScore` does not open/name the score, `readScore(noninteractive=true)` returns null, `writeScore`/`closeScore` only the current score, `newQProcess` returns null. See Guide/plugin-api.md §8.
- **PA4 `menuPath`/`dockArea` silently ignored** - `silent-failure`, TRACED. 3.x plugins load but never appear in a Plugins menu. See Guide/plugin-api.md §8.
- **PA5 `Cursor.add` silently does nothing without a current chord** - `silent-failure`, TRACED. `cursor.cpp:349-354,380-388`. See Guide/plugin-api.md §8.
- **PA6 Plugin header discovery is a text scan** - `silent-failure`, TRACED. Properties after the first inner `{` or computed expressions are lost; `title` falls back to file name. See Guide/plugin-api.md §6.2, §8.
- **PA7 `Doxyfile.plugins` INPUT points at moved docs** - `doc-drift`, TRACED. `Doxyfile.plugins:793-797` lists `doc/plugins.md` etc., now in `docs\old_docs\`. See Guide/plugin-api.md §8.
- **PA8 `FileIO` write path restriction unverified** - `security`, HYPOTHESIS (UNVERIFIED). `isPathWriteable` helper exists; `write()`/`writeBinary()` bodies not read. See Guide/plugin-api.md UNCLEAR.

## 9. Extensions system

- **X1 `composite` action type hits `UnknownError` in `perform`** - `dead-code`/`silent-failure`, TRACED. `extensionsprovider.cpp:101-119`; manifest `type` only validates, dispatch uses the action `type`. See Guide/extensions-system.md gotchas.
- **X2 `ExtensionsProvider` ignores manifest `context` at perform time** - `wrong-behavior` (gating bypass), TRACED. Only command *state* is gated; CLI/dispatch bypass `ProjectOpened`. See Guide/extensions-system.md gotchas.
- **X3 No duplicate-URI check in `loadManifestList`** - `hazard`, TRACED. Two manifests with one URI both load; `manifest(uri)` returns the first. See Guide/extensions-system.md gotchas.
- **X4 Docked extension forms appear unwired** - `dead-code`, TRACED by grep. `ExtensionsProvider::perform` emits `dockRequested`, but no consumer / `ExtensionPanel` instantiation in `src\`; `ExtensionsLoader` never sets `docked` for JSON manifests. See Guide/extensions-system.md UNCLEAR.
- **X5 v2 form import ban is a line-text heuristic, not a sandbox** - `security`, TRACED. `import` + `Muse.` text match; v1 engine has `:/qml` on import path. See Guide/extensions-system.md gotchas.
- **X6 Manifest doc drift** - `doc-drift`, TRACED. `extensionstypes.h` comment lists `type` "form, macros" and `apiversion` "String"; code also accepts `composite` and parses an int. See Guide/extensions-system.md gotchas.
- **X7 `extensions/dev/` prefix test depends on `Uri::path()` semantics** - `hazard`, HYPOTHESIS. Manifest `uri` rewritten to `extension://`. See Guide/extensions-system.md gotchas.

## 10. Import / Export

- **I1 Video-export leading/trailing silence truncated to whole seconds** - `wrong-behavior`, TRACED mechanics, HYPOTHESIS impact. `abstractaudiowriter.cpp:126-132` casts the seconds option to `msecs_t` (`number_t<int64_t>`) and assigns to a `secs_t` (`number_t<double>`, `audio\common\audiotypes.h:138-139`) consumed as seconds (`soundtrackwriter.cpp:72-79`); 0.5 s becomes 0. Only video passes these options (`videowriter.cpp:271-272`). See Guide/importexport.md §7.1.
- **I2 BB import may lose simultaneous notes (single voice / dropped notes)** - `data-loss`, TRACED mechanics, impact inferred. `bb\internal\bb.cpp:1095-1163` (`BBTrack::findChords`, `:1127-1160`): chord pushed by value before simultaneous notes are appended, later notes marked `ME_INVALID`; `Event::_notes` is by-value (`engraving\compat\midi\event.h:124`); `convertTrack` (`:800`) only ever loops one voice. See Guide/importexport.md §8.12.
- **I3 Failed GP / Capella / BWW imports can report success (`NoError`)** - `silent-failure`, TRACED. `importgtp.cpp:2939-2944`, `capella.cpp:2877-2886`, `capxml.cpp:1441-1444`, BWW always `NoError`; score left empty/partial. See Guide/importexport.md gotchas.
- **I4 GP split-chord bend path is dead code** - `dead-code`, TRACED. `-DSPLIT_CHORD_DURATIONS` defined nowhere (`guitarbendimport\benddatacollector.cpp:48,67`); `BendDataProcessorSplitChord` never runs. See Guide/importexport.md §8.10.
- **I5 No piano overlay in video export** - `dead-code`/missing feature, TRACED. `videowriter.cpp` `prepareScore` (`:307`): the overlay does not exist. See Guide/importexport.md §7.1 and §8.7.
- **I6 MNX export skips TAB parts** - `data-loss`, TRACED. MNX exporter `createParts` (Guide cites `parts:933`; `mnxexporter.cpp` area) skips any part with a TAB staff ("Skipping tab part"). See Guide/importexport.md §8.11.
- **I7 MusicXML schema validation compiled out** - `dead-code`, TRACED. `iex_musicxml` `MUSICXML_NO_VALIDATION # TODO` (`musicxml\CMakeLists.txt:72`); `MusicXmlValidation::validate` is a `NoError` stub (`musicxmlvalidation.cpp:25-33`), `:35-209` and XSD resources dead; `ForceMode` therefore a no-op for MusicXML. See Guide/importexport.md §7 / gotchas.
- **I8 Reader/writer registration is first-wins** - `silent-failure`, TRACED. `notationreadersregister.cpp:29`, `notationwritersregister.cpp:30` (`std::map::insert`); duplicate suffixes silently ignored. See Guide/importexport.md gotchas.
- **I9 `tools\bww2mxml` CMake points at a nonexistent path** - `dead-code`, TRACED. `tools\bww2mxml\CMakeLists.txt:27` uses `mu4/importexport/internal/bww` (now `src\importexport\bww\internal\bww`); unbuildable as written. See Guide/importexport.md gotchas.
- **I10 Stale MIDI-import comment** - `doc-drift`, TRACED. `importmidi_operations.h:46-50` names nonexistent `importmidi_opmodel.cpp`/`trmodel.cpp`; `importmidi_model.{h,cpp}` (`TracksModel`) exists, used only by tests. See Guide/importexport.md §8.9.
- **I11 `GuitarPro2::read` compares a MIDI channel to a `StaffTypes` value** - `wrong-behavior`, HYPOTHESIS. `importgtp.cpp:1587` `midiChannel == int(StaffTypes::PERC_DEFAULT)`; probably meant `GP_DEFAULT_PERCUSSION_CHANNEL`. See Guide/importexport.md §8.10.
- **I12 BWW `symbolToString` guard uses `sizeof` of the pointer array** - `cosmetic` (benign), TRACED. `bww\internal\bww\symbols.cpp:32-59`. See Guide/importexport.md §8.12.
- **I13 Export loop deletes the destination first** - `data-loss` (on cancel/failure), TRACED. `exportprojectscenario.cpp:457-467`; may leave nothing. See Guide/importexport.md gotchas.
- **I14 Audio/video export busy-waits the GUI thread with `processEvents()`** - `hazard` (re-entrancy), TRACED. `abstractaudiowriter.cpp:106-142`, `videowriter.cpp:150-153`. See Guide/importexport.md gotchas.
- **I15 MusicXML export on a never-laid-out score sees no pages** - `wrong-behavior`, HYPOTHESIS. Scenario forces layout/PAGE view (`exportprojectscenario.cpp:149-158`); headless `saveXml` without `doLayout()` untested. See Guide/importexport.md gotchas.
- **I16 GP beam breaks applied one beat late; brush direction inverted on purpose** - `wrong-behavior` or intentional compensation, TRACED. `gpconverter.cpp:3008-3009` (beam), `:2532-2559`/`importgtp-gp5.cpp:154-163` (brush). See Guide/importexport.md gotchas, §8.10.

## 11. Framework / Commands / Services

- **FW1 Command availability default `All` in code vs `Disabled` in ADR 00107** - `security`/`doc-drift`, TRACED. `muse\framework\rcommand\commandtypes.h:224` (TODO): an unflagged command is exposed to MCP/extensions. See Guide/framework-services.md gotcha 9.
- **FW2 `ActionData::arg<T>` is an unchecked `reinterpret_cast`** - `hazard`, TRACED. `actiontypes.h:94`; wrong `T` is UB. See Guide/framework-services.md gotcha 7.
- **FW3 Duplicate command-handler registration only `LOGW`s and is ignored** - `silent-failure`, TRACED. `commanddispatcher.cpp:134-137`. See Guide/framework-services.md gotcha 8.
- **FW4 `Settings::Key.moduleName` not persisted -> cross-module key collisions** - `wrong-behavior`, TRACED. `settings.cpp:239-240` (`TODO`). See Guide/framework-services.md gotcha 13.
- **FW5 Unknown shortcut scope gets priority 0 and can win** - `wrong-behavior`, TRACED. `shortcutresolver.cpp:79-80`; scope priority map is exact-match (`:168` upper-cases only title lookup). See Guide/framework-services.md gotcha 12.
- **FW6 Channel `send` can block ~200 ms; thread pool exhaustion asserts** - `hazard`, TRACED. `DO_WAIT_PENDINGS_ON_SEND` (4 ms x 50); `channelimpl.h:415-424` (`MAX_THREADS_PER_CHANNEL` 10). See Guide/framework-services.md gotchas 3-4.
- **FW7 `Ret` falsy for `Cancel` and every non-`Ok` code** - `hazard`, TRACED. `types\ret.cpp:49-52`; a user cancel looks like failure unless `ret.code()` is checked. See Guide/framework-services.md gotcha 6.

## 12. Architecture / IoC / CMake wiring

- **A1 `INTERFACE_ID` uniqueness never verified** - `hazard`, HYPOTHESIS. Collision = double-register assert (debug) / silently ignored (release) / `static_pointer_cast` UB; not scanned across ~200 headers. See Guide/architecture-and-ioc.md gotchas, UNCLEAR.
- **A2 `ioc(nullptr)` returns null -> null-deref on `ContextInject` before context exists** - `crash-risk`, TRACED. `ioc.cpp:46-49`, `kors\ioc.h:75-77`; QML view-models: `ioccontext.cpp:42-45`. See Guide/architecture-and-ioc.md gotchas.
- **A3 `ioc` `s_contexts` map unsynchronized** - `crash-risk`, TRACED. `ioc.cpp:36,51-56`. See Guide/architecture-and-ioc.md gotchas.
- **A4 `MUSE_FRAMEWORK_MODULES` omits `AUTOMATION`; `RCOMMAND`/`RCONTROL` lack `#cmakedefine`** - `wrong-behavior` (sub-options not disabled), TRACED. `MuseModules.cmake:2-33`, `MuseDeclareOptions.cmake:65`, `MuseSetupConfiguration.cmake:13-48`, `muse_framework_config.h.in`, `appfactory.cpp:17-19`. See Guide/architecture-and-ioc.md gotchas.
- **A5 `MUSE_MODULE_GLOBAL_MULTI_IOC` is vestigial** - `dead-code`, TRACED by grep. Declared `MuseDeclareOptions.cmake:90`, `global\CMakeLists.txt:275-277`; never consumed. See Guide/architecture-and-ioc.md gotchas.
- **A6 Dock v1/v2 `dockmodule.h` ODR hazard** - `hazard`, TRACED. `appfactory.cpp:98` (v2 shortcuts unconditional) vs `:117` (v1 dock header); headers identical today. Root `CMakeLists.txt:199,86`. See Guide/architecture-and-ioc.md gotchas, Guide/ui-qml-mvvm.md.
- **A7 Unnamespaced `::globalCtx` in `ioc.cpp:30` appears unused** - `dead-code`, HYPOTHESIS. See Guide/architecture-and-ioc.md UNCLEAR.
- **A8 No build-level layering barrier between modules** - `hazard`, TRACED. `MuseCreateModule.cmake:85-101` gives every module identical include roots; `tools\checkheaders` has only `iwyu.sh`. See Guide/architecture-and-ioc.md gotchas.

## 13. CI / Release / Build

- **C1 `uses: $/.github/workflows/...` (literal `$/`) in reusable-workflow calls** - `wrong-behavior` (workflow may not parse), TRACED text, HYPOTHESIS cause (export artefact?). `build_all.yml:92,103,117,129,142,148`; `deploy.yml:62,75,136`. See Guide/ci-and-release.md gotchas.
- **C2 `deploy.yml` passes `sentry_project: 'sandbox'`** - `silent-failure`, TRACED logic, not run. `deploy.yml:69`; recognised values are `mu4`/`mu4-sandbox` (`build_linux.yml:173,184`) -> empty crash URL and no symbol upload; stable without `create_tag` also gets `'sandbox'`. See Guide/ci-and-release.md gotchas.
- **C3 Stale 4.7 release constants vs 5.0** - `doc-drift`/`wrong-behavior`, TRACED. `build_nightly.yml:14` `CURRENT_RELEASE_BRANCH: 4.7`; `languages.cfg` `server_url .../4.7/languages/`. See Guide/ci-and-release.md gotchas.
- **C4 `check_visual_tests.yml` push-event branches are dead** - `dead-code`, TRACED. Handles `github.event_name == 'push'` but has no `push:` trigger. See Guide/ci-and-release.md gotchas.
- **C5 `make_build_number.sh:31` assigns unused `OUT_DIR`** - `dead-code`, TRACED. See Guide/ci-and-release.md gotchas.
- **C6 Build number granularity is 10 minutes** - `hazard`, TRACED. Two builds in one window share artifact/MSI version. See Guide/ci-and-release.md gotchas.
- **C7 Windows MSI GUID falls back to `11111111-...`** - `wrong-behavior`, TRACED. `package.bat:14`; testing GUID used for devel/nightly; shared UpgradeCode on a fork MSI. See Guide/ci-and-release.md gotchas.
- **C8 `publish.sh` echoes `SECRET: $OSUOSL_SSH_ENCRYPT_SECRET`; key file spelled `osuosl_nighlies_rsa.enc`** - `security`/`cosmetic`, TRACED. `osuosl/publish.sh:47`. See Guide/ci-and-release.md gotchas.
- **C9 `s3_packandsend.py:33` uses global `lang_code` instead of param `langCode`** - `hazard`, TRACED. See Guide/ci-and-release.md gotchas.
- **C10 `update_release_info_clear.yml` vs `update_release_info.yml` target different buckets/scripts** - `wrong-behavior`, TRACED, same-feed unverified. `musescore-updates` vs `updates.musescore.org`. See Guide/ci-and-release.md gotchas.
- **C11 `--crash_log_url ""` still enables crashpad; configure hard-fails without handler path** - `hazard`, TRACED. `diagnostics\CMakeLists.txt:79-84`. See Guide/ci-and-release.md gotchas.
- **C12 `check_submodules` fails on local `muse\` commits or fork URL in `.gitmodules`** - `hazard`, TRACED. See Guide/ci-and-release.md gotchas.
- **C13 Hard-coded Windows Kits `uuidgen` / WiX paths, `APPLE_TEAM_ID`, per-workflow Qt pin** - `hazard`, TRACED. `package.bat`; Qt 6.11.2 x ~9 files. See Guide/ci-and-release.md gotchas.
- **C14 Release-channel string mismatch (`devel`/`dev`, `testing`/`Testing`, WiX `!= ""` guard)** - `wrong-behavior`, HYPOTHESIS. `WIX.template.in:11`. See Guide/ci-and-release.md UNCLEAR.
- **C15 Legacy crashdump scripts still present and uncalled** - `dead-code`, TRACED. `muse\buildscripts\ci\crashdumps\*` (ADR 00105 "still to be dropped"). See Guide/ci-and-release.md UNCLEAR.

## 14. Testing / vtest

- **T1 `vtest.sh` always exits 0** - `silent-failure`, TRACED. Mechanism-A CI steps are commented out; a green script is not evidence. See Guide/testing-and-vtest.md gotchas.
- **T2 deInit mock-delete HACK hides a real score-object leak** - `hazard`, TRACED. `testing\environment.cpp:72`; several engraving tests disabled/uncompiled (§3.7). See Guide/testing-and-vtest.md gotchas.
- **T3 vtest compare ignores added/removed files** - `silent-failure`, TRACED. See Guide/testing-and-vtest.md §4.2, gotchas.
- **T4 Test README links the wiki VTests page (unread)** - `doc-drift`, HYPOTHESIS. See Guide/testing-and-vtest.md gotchas.

## 15. CLI (mscore-cli / converter)

- **CLI1 `-F` and `-R` identical; "factory settings" does not wipe the data dir** - `wrong-behavior`, TRACED. `commandlineparser.cpp:453-455` both call `settings()->reset(true)`; `settings.cpp:115-118` data-path removal branch not taken. See Guide/mscore-cli.md gotcha 1, Guide/app-shell-and-startup.md.
- **CLI2 `-P` error text says "-R"** - `cosmetic`, TRACED. `commandlineparser.cpp:335`. See Guide/mscore-cli.md gotcha 2.
- **CLI3 `-P` without `*` overwrites every part to the same path** - `data-loss`, TRACED. See Guide/mscore-cli.md gotcha 3, §3.5.
- **CLI4 Batch `out` array item neither string nor 2-array still pushes an empty-`out` job** - `wrong-behavior`, TRACED. `parseBatchJob` `:469-481`; fails later with `ConvertTypeUnknown`. See Guide/mscore-cli.md gotcha 6.
- **CLI5 Blank error messages for malformed `visibleParts`/`copyright`** - `cosmetic`, TRACED. `:432,442,453` use the document-level `QJsonParseError` (NoError). See Guide/mscore-cli.md gotcha 7.
- **CLI6 `copyright.showOnAllPages` parsed but unused** - `dead-code`, TRACED. `convertFile` `:260-272`. See Guide/mscore-cli.md gotcha 8.
- **CLI7 `--page 0` underflows (`toULongLong - 1`, `size_t`)** - `crash-risk`, TRACED. `consoleapp.cpp:53`; not validated. See Guide/mscore-cli.md gotcha 9.
- **CLI8 `-S` during import falls back to saved Preferences style** - `hazard` (non-hermetic conversion), TRACED. See Guide/mscore-cli.md gotcha 11, §5.1.
- **CLI9 `BackendApi::openOutputFile` failure ignored by callers** - `silent-failure`, TRACED. `backendapi.cpp:221-232`. See Guide/mscore-cli.md gotcha 5.
- **CLI10 `-t` help says it includes `--template-mode` but code sets two independent flags; unknown `--sound-profile` only logs** - `doc-drift`, TRACED / HYPOTHESIS for engraving implication. `:82`; `consoleapp.cpp:203-206`. See Guide/mscore-cli.md gotchas 13, 15.

## 16. App shell / Startup

- **AS1 `isStartWithNewFileAsSecondaryInstance()` has no callers** - `dead-code`, TRACED. `startupscenario.cpp:84` (definitions only in desktop + web appshell). See Guide/app-shell-and-startup.md gotchas.
- **AS2 `AppshellCommandsController::setupConnections()` and `isFullScreen()` declared, never defined** - `dead-code`, TRACED. `appshellcommandscontroller.h:93,114`. See Guide/app-shell-and-startup.md gotchas.
- **AS3 Typos `sessionTypeTromString` / `modeTypeTromString`** - `cosmetic`/`doc-drift`, TRACED. Parser comment names a nonexistent `sessionTypeTromString`; the function is `modeTypeTromString` (`startupscenario.cpp:43-62`). See Guide/app-shell-and-startup.md gotchas.
- **AS4 `needShowSplashScreen/setNeedShowSplashScreen` vestigial** - `dead-code`, TRACED (src\ only; `muse\` not searched). No consumers, no default registered. See Guide/app-shell-and-startup.md UNCLEAR.
- **AS5 CLI GUI mode opens only the first positional file** - `wrong-behavior`, TRACED. `commandlineparser.cpp:549`. See Guide/app-shell-and-startup.md gotchas.

## 17. UI / QML

- **U1 `NavigationSection` with empty `name` or `order < 0` silently never registers (release)** - `silent-failure`, TRACED. `navigationsection.cpp:56-62`. See Guide/ui-qml-mvvm.md gotchas.
- **U2 `findNearestEnabled` First/Last is not lexicographic for 2-D panels** - `wrong-behavior`, TRACED observation, not verified bug. `navigationcontroller.cpp:75-100`. See Guide/ui-qml-mvvm.md gotchas.
- **U3 Interactive `open()` single-flight clobbers `m_openingObject`** - `hazard`, TRACED. `interactive.cpp:754`. See Guide/ui-qml-mvvm.md gotchas.
- **U4 Multi-window navigation highlight reset across windows** - `wrong-behavior`, HYPOTHESIS. `navigationcontroller.cpp:333,416`. See Guide/ui-qml-mvvm.md UNCLEAR.

## 18. Contributing / Conventions

- **K1 Helper scripts and CI error text reference nonexistent `tools/codestyle/uncrustify_run_file.sh`** - `doc-drift`, TRACED. `cherry_pick.sh:67,84`, `checkcodestyle.cmake:32-35`; use `tidy_file.sh`. See Guide/contributing-and-conventions.md gotchas.
- **K2 Git hook covers `src\` only; CI covers `src\` + `muse\framework\`** - `hazard`, TRACED. See Guide/contributing-and-conventions.md §2.1, gotchas.
- **K3 Fork `Guide\` directory untracked** - `hazard`, TRACED (`git status` `?? Guide/` at pass time; may be stale). See Guide/contributing-and-conventions.md gotchas.

---

## Count summary

| Group | Entries |
|---|---|
| 1 Engraving / DOM | 2 |
| 2 Layout / Rendering | 2 |
| 3 Style | 7 |
| 4 File format / RW | 13 |
| 5 Notation | 9 |
| 6 Playback / Audio | 6 |
| 7 MIDI / VST / MuseSampler | 10 |
| 8 Plugin API | 8 |
| 9 Extensions | 7 |
| 10 Import / Export | 16 |
| 11 Framework / Commands | 7 |
| 12 Architecture / IoC | 8 |
| 13 CI / Release / Build | 15 |
| 14 Testing / vtest | 4 |
| 15 CLI | 10 |
| 16 App shell | 5 |
| 17 UI / QML | 4 |
| 18 Contributing | 3 |
| **Total** | **136** |
