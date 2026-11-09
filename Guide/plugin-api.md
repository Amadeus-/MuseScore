# MuseScore — QML Plugin API

> **What this file is.** The QML **plugin API v1** exposed from `src\engraving\api\v1` — the root `MuseScore { }` component (`PluginAPI`), the `Cursor`, the wrapped engraving elements, the enums — plus how legacy `.qml` plugins are discovered, enabled and launched (`muse\framework\extensions\internal\legacy`). Authored from a full read of the API headers/cpp and the loader/runner on the 5.0 `main` tree. **Live source wins** — re-Read the cited files before relying on a line number. Start at `00-core.md`; related: `engraving-dom.md` (the classes being wrapped), `extensions-system.md` (the framework that hosts and packages plugins), `notation-layer.md` (the undo stack `startCmd/endCmd` drives).

## 0. 60-second orientation

- ⭐ A plugin is **one QML file whose root object is `MuseScore { }`**. `MuseScore` is the QML name of C++ class `mu::engraving::apiv1::PluginAPI` (a `QQuickItem`), registered as `qmlRegisterType<PluginAPI>("MuseScore", 3, 0, "MuseScore")` (`src\engraving\api\v1\qmlpluginapi.cpp:167`). The import line is therefore always `import MuseScore 3.0` — **even on 5.0** (there is no `4.0`/`5.0` import; version growth is tracked only by `\since` doc tags).
- The root object *is* the global scope: `curScore`, `newElement()`, `cmd()`, `fraction()`, `quit()` and every enum (`Element`, `Segment`, `Placement`, …) are members of `PluginAPI` and resolve unqualified inside the plugin (`qmlpluginapi.h:95-451,531-556`).
- Edit model: you get a **`Cursor`** (`curScore.newCursor()`) or walk `Segment`/`Measure` objects, read properties off thin **wrapper objects** (`Note`, `Chord`, …), and mutate either through wrapper properties/methods or `Cursor.add*`. All mutation must be inside `curScore.startCmd(...)` / `curScore.endCmd()` to be one undoable step.
- Wrappers are **not** the engraving classes — they are `QObject`s holding a pointer to the real `mu::engraving::EngravingObject` plus an *ownership flag* (`PLUGIN` = plugin owns it and deletes it; `SCORE` = score owns it) (`scoreelement.h:42-45,89-96`, dtor `scoreelement.cpp:41`).
- Legacy plugins are run through the **extensions** framework as `apiversion = 1` "macros/forms" (`muse\framework\extensions\internal\legacy\extpluginsloader.cpp:165`), on a dedicated QML engine (`ExtensionsUiEngine::engineV1`, `extensionsuiengine.cpp:91-124`). They are **disabled until the user enables them** (§6).
- Per-user plugins directory: **`<Documents>\<AppName>\Plugins`** — on Windows for a normal build `Documents\MuseScoreStudio5\Plugins` (`MuseScoreStudio5Development` in a DEV-mode build). Settings key `application/paths/myPlugins`. Resolved in §6.1.
- Bundled plugins: `share\plugins\` — `note_names`, `new_retrograde`, `intervals`, `lilyrics`, `tuning`, `dev_test_api` (+ `dev_test_websocket.qml_disabled`).

## 1. Source map (`src\engraving\api\v1\`)

| File | Contents |
|---|---|
| `qmlpluginapi.h/.cpp` | `PluginAPI` (the `MuseScore` root): properties, `Q_INVOKABLE` globals, enum properties, `registerQmlTypes()` |
| `engravingapiv1.h/.cpp` | `EngravingApiV1` — the *same* API surfaced to JS macros/extension code as `api.engraving.*`; forwards every call to a `PluginAPI` (`engravingapiv1.h:42-355`) |
| `scoreelement.h/.cpp` | base `ScoreElement` (`type`, `name`, `eid`, `spatium`, `userName()`, `is()`), generic `get/set/reset(Pid)`, `wrap()` templates |
| `elements.h/.cpp` | `EngravingItem` and all subclasses (`Note`, `Chord`, `ChordRest`, `Tuplet`, `Beam`, `Segment`, `Measure`, `MeasureBase`, `System`, `Page`, `Staff`, `Spanner`, `Tie`, `Lyrics`, `Harmony`, `FretDiagram`, `Ornament`, `SpannerSegment`, `DurationElement`) — 2837 lines in the header |
| `score.h/.cpp` | `Score` wrapper (`curScore`) |
| `cursor.h/.cpp` | `Cursor` |
| `selection.h/.cpp` | `Selection` (`curScore.selection`) |
| `part.h/.cpp`, `instrument.h/.cpp`, `excerpt.h/.cpp`, `style.h/.cpp`, `playevent.h/.cpp` | `Part`, `Instrument`/`Channel`/`StringData`/`Drumset`, `Excerpt`, `MStyle`, `PlayEvent` |
| `apistructs.h/.cpp` | `Fraction`, `IntervalWrapper`, `OrnamentIntervalWrapper` |
| `apitypes.h` | 4855 lines: **107** `enum class`es in namespace `mu::engraving::apiv1::enums` that mirror engraving enums 1:1 and are `Q_ENUM_NS`-registered (`apitypes.h:46-…`) |
| `enums.h/.cpp` | `Enum : QQmlPropertyMap` — the QML-visible enum object (`enums.h:35`) |
| `util.h/.cpp` | `FileIO` (read/write/paths) and `MsProcess` (`QProcess`) |
| `api_v1.cmake` | build glue; whole API compiled out when `ENGRAVING_NO_INTERNAL` (`src\engraving\CMakeLists.txt:406-409`) |

Hosting side (not in `api\v1`): `muse\framework\extensions\api\v1\ipluginapiv1.h` (interface `PluginAPI` implements: `setup`, `runPlugin`, `closeRequest`), `extapiv1.cpp` (registers `MessageDialog`, `FileDialog`, `Settings`, `StandardButton` under `MuseScore 3.0`, `extapiv1.cpp:35-42`).

## 2. The plugin object model

### 2.1 Anatomy of a plugin file (real property set)

```qml
import QtQuick
import MuseScore 3.0

MuseScore {
    title: "Note Names"                     // PluginAPI::title  (also the menu/list label)
    description: "…"                         // PluginAPI::description
    version: "4.7"                           // PluginAPI::version
    categoryCode: "composing-arranging-tools" // one of the known categories (extensionsregister.cpp:100)
    thumbnailName: "note_names.png"          // path relative to the .qml
    pluginType: "dialog"                     // optional: "dialog" | "dock" | (unset = no UI / macro)
    requiresScore: true                      // default true (qmlpluginapi.cpp:246)
    onRun: { … quit() }                      // the entry point
}
```
(from `share\plugins\note_names\notenames.qml:22-27,91`, `lilyrics.qml:10-21`)

⭐ **`menuPath` and `dockArea` are dead.** The properties exist so old plugins still parse, but `setMenuPath()` / `setDockArea()` just call `DEPRECATED;` and the getters return `QString()` (`qmlpluginapi.cpp:523-531,563-571`). The bundled 5.0 plugins carry no `menuPath`. Plugins appear via the extensions UI/commands, not a `Plugins.xxx` menu path.

### 2.2 Ownership + wrapping

- `wrap<Wrapper>(ptr, Ownership)` builds a wrapper and sets `QQmlEngine::JavaScriptOwnership` — the JS GC owns the wrapper object (`scoreelement.h:136-142`). Wrappers **do not keep the underlying item alive**; stale wrappers after a structural edit are a real hazard (see §8).
- Polymorphic `wrap(EngravingItem*)` picks the most specific wrapper by `is<Type>()` in a fixed order: `Tie, Ornament, Note, Chord, ChordRest, Tuplet, DurationElement, Beam, Lyrics, Harmony, FretDiagram, Segment, Measure, MeasureBase, System, Page, SpannerSegment, Spanner`, else plain `EngravingItem` (`elements.cpp:862-891`). `Score`/`Part`/`Staff` are wrapped via the `EngravingObject` overload (`scoreelement.cpp:254-275`).
- `Ownership::PLUGIN` is what `newElement()` / `clone()` return; `Cursor::add`, `Note::add`, `Chord::add` flip it to `SCORE` and refuse an element that is already `SCORE`-owned ("already part of the score") (`cursor.cpp:285`, `elements.cpp:281,410`).

### 2.3 The generic property mechanism (`API_PROPERTY*` macros)

Most element properties are **not hand-written**; they are generated by macros that bridge to the engraving `Pid` property system (`elements.h:86-132`):

| Macro | QML type | Behaviour |
|---|---|---|
| `API_PROPERTY(name, PID)` | `QVariant` | read/write/`RESET`; value may be `undefined` if the property does not apply to this element |
| `API_PROPERTY_T(type, name, PID)` | concrete `type` | typed; only for properties valid on that class |
| `API_PROPERTY_ENUM(Enum, name, PID)` | `QJSValue` | **API v1 → int**, API v2 → enum key *string* (`elements.h:104-124`) |
| `API_PROPERTY_READ_ONLY(_T)` | `QVariant`/typed | no setter |

374 such declarations exist in `elements.h`. Conversion rules in `ScoreElement::get` / `set` (`scoreelement.cpp:105-232`):

- `FRACTION` → `Fraction` wrapper (set requires a `Fraction*`); setting `timesigActual` on a score-owned `Measure` calls `adjustToLen` instead (`:185-193`).
- `POINT` → `QPointF` **divided by spatium** on read, multiplied on write; `ABSOLUTE` likewise → **all positions/offsets in QML are in spatium units**. `SPATIUM` → real; `PAIR_REAL` → `[a, b]`; `ALIGN` → plugin `Align` bitmask (`:136-161,212-216`).
- ⭐ **Undo semantics of a set**: if the wrapper is `Ownership::SCORE` the write is `e->undoChangeProperty(pid, val, flags)` (undoable, needs an open command); if `PLUGIN`-owned (not yet added) it is a plain `setProperty` (`:226-231`). A written property is flagged `UNSTYLED` (detached from style) unless the property is `NOSTYLE` (`:223-224`). `reset` → `undoResetProperty` / `resetProperty` (`:234-245`).

## 3. INVENTORY — `PluginAPI` (the `MuseScore` root)

### 3.1 Properties (`qmlpluginapi.h:99-135`)

| Property | Type | RW | Meaning |
|---|---|---|---|
| `menuPath` | string | W-noop | **DEPRECATED**, ignored (§2.1) |
| `title` | string | RW | display title |
| `version` | string | RW | plugin version |
| `description` | string | RW | text in Plugin Manager |
| `pluginType` | string | RW | `"dialog"` / `"dock"` / unset |
| `dockArea` | string | W-noop | **DEPRECATED** |
| `requiresScore` | bool | RW | default `true`; `false` → context `Any` (loader, §6.2) |
| `thumbnailName` | string | RW | preview image |
| `categoryCode` | string | RW | category id |
| `division` | int | R | ticks per quarter (`engraving::Constants::DIVISION`) |
| `mscoreVersion` | int | R | `MMmmuu` = major*10000+minor*100+patch (`qmlpluginapi.cpp:608-611`) |
| `mscoreMajorVersion` / `mscoreMinorVersion` / `mscoreUpdateVersion` | int | R | from `IApplication::version()` |
| `mscoreDPI` | real | R | `engraving::DPI` |
| `curScore` | `Score` | R | score of the **current notation**, or `null` (`qmlpluginapi.cpp:255-262,504-511`) |
| `scores` | list<Score> | R | ⛔ `NOT_IMPLEMENTED` — returns an empty static vector (`qmlpluginapi.cpp:264-271`) |

### 3.2 Signals

| Signal | When / notes |
|---|---|
| `run()` | plugin launched — implement `onRun` (`qmlpluginapi.h:456`; emitted by `runPlugin()`) |
| `closeRequested()` | emitted by `quit()` (`qmlpluginapi.cpp:498-502`) |
| `scoreStateChanged(state)` | `QMap`: `selectionChanged`, `excerptsChanged`, `instrumentsChanged`, `startLayoutTick`, `endLayoutTick`, `undoRedo`. Doc marks **experimental**; a handler that edits must wrap in `startCmd/endCmd` and guard recursion (`qmlpluginapi.h:453-512`). Fired from `PluginAPI::endCmd(stateInfo)` (inline, `:525`) |

### 3.3 `Q_INVOKABLE` methods (`qmlpluginapi.h:531-556`)

| Method | Status / behaviour |
|---|---|
| `newElement(int type)` | creates a free element via `Factory::createItem` owned by the plugin; rejects `<= INVALID` or `>= ROOT_ITEM`; needs a current score (`qmlpluginapi.cpp:362-378`) |
| `removeElement(EngravingItem)` | `score->deleteItem(element)` — undoable (`:387-391`) |
| `cmd(string)` | dispatches a command through `ICommandDispatcher`; a **compat map** translates the legacy action names `escape, cut, copy, paste, paste-half, paste-double, select-all, delete, next-chord, prev-chord, prev-measure`; any other string is passed through as a raw `Command` id (`:421-438`). The retrograde plugin relies on `"full-measure-rest"`, `"tie"`, `"acciaccatura"` etc. passing through |
| `newScore(name, part, measures)` | creates a **MasterScore** with default style + one part from instrument template `part`, then calls `startCmd`; ⛔ `name` is ignored and the score is **not opened/named** (`NOT_IMPLEMENTED`, `:397-419`) |
| `writeScore(score, name, ext)` | only the **current** score is supported; delegates to `IEngravingPluginAPIHelper::writeScore` (`:285-298`) |
| `readScore(name, noninteractive=false)` | opens in a new tab via helper; ⛔ `noninteractive=true` logs "not yet implemented" and returns `null` (`:310-328`) |
| `closeScore([score])` | only the current score (`:334-352`) |
| `fraction(n, d)` / `fractionFromTicks(ticks)` | build `Fraction` wrappers (`:482-496`; `fractionFromTicks` is 4.6+) |
| `interval(diatonic, chromatic)` / `intervalFromOrnamentInterval(o)` | build `IntervalWrapper`. ⚠ the header names the params `(chromatic, diatonic)` but the cpp is `(diatonic, chromatic)` and passes straight to `Interval(diatonic, chromatic)` (`qmlpluginapi.h:553` vs `.cpp:653`) — trust the cpp |
| `ornamentInterval(step, type)` | build `OrnamentIntervalWrapper` (`:643`) |
| `log/logn/log2(...)` | `LOGD()` output only |
| `openLog/closeLog` | `DEPRECATED` no-ops |
| `newQProcess()` | `NOT_IMPLEMENTED`, returns `null` (`:470-474`) |
| `quit()` | requests plugin close (`:498`). `Qt.quit()` (used by the old docs) is not the supported path here — every bundled plugin calls plain `quit()` |

### 3.4 Registered QML types (`registerQmlTypes`, `qmlpluginapi.cpp:160-214`)

Creatable: `MuseScore` (PluginAPI), `Cursor`, `PlayEvent`, `QProcess` (MsProcess), `FileIO 3.0` (`qmlRegisterType<FileIO,1>("FileIO",3,0,"FileIO")`), `MuseScoreEnum` (uncreatable). Anonymous (obtainable, not instantiable): `ScoreElement, Score, EngravingItem, Chord, Note, Tuplet, DurationElement, Segment, Measure, Part, Staff, Instrument, Channel, StringData, Excerpt, Selection, Tie, Harmony, FretDiagram, Drumset, MeasureBase, System, Spanner, SpannerSegment, Ornament, Fraction, IntervalWrapper, OrnamentIntervalWrapper`. Note: `Beam`, `Page`, `Lyrics`, `MStyle` are **not** in this list yet are reachable as properties (`ChordRest.beam`, `Score.pages`, …) — the wrapper still works because the property type is declared in `Q_PROPERTY`.

### 3.5 Enum properties

Enums are `Q_PROPERTY(Enum* <QmlName> CONSTANT)` generated by `DECLARE_API_ENUM(qmlName, cppName, enumType)` (`qmlpluginapi.h:60-70`): the first access wraps the `Q_ENUM_NS` type into an `Enum`/`QQmlPropertyMap`, freezes it, and caches it in a static (`.cpp:53-154`). Use as `QmlName.VALUE`. The set exposed (107 `enum class` in `apitypes.h`; `PluginAPI` binds the QML names below):

| QML name | Mirrors | Note |
|---|---|---|
| **`Element`** | `ElementType` (`apitypes.h:323`) | `Element.NOTE`, `.CHORD`, `.REST`, `.STAFF_TEXT`, `.TUPLET`, … (older docs call this `EngravingItem.NOTE`; the QML property is `Element`) |
| **`Segment`** | `SegmentType` (`:859`) | bitmask: `ChordRest`, `TimeTick`, `KeySig`, `Clef`, `BarLine`, `EndBarLine`, `All`, `…Type` aliases |
| `Accidental`, `AccidentalBracket`, `AccidentalRole`, `AccidentalVal` | accidental enums | |
| `Align` | `Align` bitmask (`HMASK`/`VMASK`) | decoded in `scoreelement.cpp:136-161` |
| `Placement`, `PlacementH`, `Direction`, `DirectionH`, `Orientation`, `AutoOnOff`, `AutoCustomHide`, `VerticalAlignment`, `TextPlace` | layout/placement | 2.x names `MScore.UP` etc. are now `Direction.UP` |
| `NoteType`, `NoteHeadType`, `NoteHeadGroup`, `NoteHeadScheme`, `NoteValueType` (=`VeloType`), `NoteSpellingType`, `PreferSharpFlat` | notes | |
| `DurationType`, `HDuration`, `BeamMode`→`Beam`, `TupletNumberType`, `TupletBracketType`, `TripletFeelType`, `LayoutBreak`, `SpacerType`, `FrameType` | rhythm/layout | |
| `Tid` | `Tid`/text styles | was `TextStyleType` in 2.x; `Score.addText` also takes `TextStyleType` keys (`score.cpp:60-75`) |
| `Lyrics` (=`Syllabic`), `LyricsDashSystemStart` | lyrics | |
| `Spanner` (=`Anchor`), `SpannerSegmentType`, `OttavaType`, `HairpinType`, `TrillType`, `VibratoType`, `GlissandoType→Glissando`, `GlissandoStyle`, `GuitarBendType`, `GuitarBendShowHoldLine`, `TremoloBarType`, `PartialSpannerDirection`, `LineType`, `HookType`, `NoteLineEndPlacement` | spanners/lines | |
| `Key`, `KeyMode`, `ClefType`, `ClefToBarlinePosition`, `TimeSigType/Placement/Style/VSMargin`, `BarLineType`, `BracketType`, `StaffGroup`, `MMRestRangeBracketType`, `MeasureNumberMode/Placement`, `JumpType`, `MarkerType`, `RepeatPlayCountPreset` | score structure | |
| `DynamicType`, `DynamicSpeed`, `ChangeMethod`, `ChangeDirection`, `GradualTempoChangeType`, `PlayingTechniqueType`, `PlayEventType` | dynamics/playback | |
| `HarmonyType`, `HarmonyVoicing`, `ChordStylePreset`, `ArpeggioType`, `OrnamentStyle`, `OrnamentShowAccidental`, `IntervalStep`, `IntervalType`, `FermataType`, `ChordLineType`, `SlurStyleType`, `TiePlacement`, `TieDotsPlacement`, `TremoloType`, `TremoloChordType`, `ArticulationTextType`, `TappingHand`, `LHTappingSymbol`, `RHTappingSymbol`, `LHTappingShowItems`, `ParenthesesMode`, `InstrumentLabelVisibility`, `VoiceAssignment` | expressive/notation detail | |
| `LayoutMode`, `LayoutFlag`, `UpdateMode` | engine modes | |
| `SymId` | SMuFL symbol ids | `\since 3.5` |
| **`Cursor`** | `Cursor::RewindMode` + `Cursor::InputStateMode` merged via `DECLARE_API_ENUM2` (`qmlpluginapi.h:451`) | `SCORE_START`, `SELECTION_START`, `SELECTION_END`, `INPUT_STATE_INDEPENDENT`, `INPUT_STATE_SYNC_WITH_SCORE` |

(`EngravingApiV1` re-exports the same enums, plus `Element` and `TextStyleType` as JS enum objects whose representation is **int for apiversion 1, string for ≥2**, `engravingapiv1.h:33-36,343-351`.)

## 4. INVENTORY — `Score` (`curScore`) — `score.h`

⭐ API-doc blocks in this header are `/** APIDOC … */` (jsdoc source for the new generated docs; `tools\jsdoc`), not the old Doxygen `///`.

**Properties** (R unless noted): `scoreName` (RW), `title`, `composer`, `lyricist` (from metaTags `workTitle`/`composer`/`lyricist`), `duration` (seconds), `mscoreVersion`, `mscoreRevision`, `style` (`MStyle`: `value(key)`, `setValue(key, v)`, `resetValue(key)`, `style.h:75-93`), `keysig`, `npages`, `pages`, `pageNumberOffset` (RW), `parts`, `nstaves`, `staves`, `ntracks`, `systems`, `spanners` (4.7), `hasHarmonies`, `harmonyCount`, `hasLyrics`, `lyricCount`, `lyrics` (4.7), `nmeasures`, `firstMeasure`, `firstMeasureMM`, `lastMeasure`, `lastMeasureMM`, `lastSegment`, `selection`, `excerpts`; view flags (all RW, 4.6): `layoutMode`, `showVerticalFrames`, `showInvisible`, `showUnprintable`, `showFrames`, `showPageborders`, `showSoundFlags`, `markIrregularMeasures`, `showInstrumentNames`.

**Methods** (`Q_INVOKABLE`):

| Group | Methods |
|---|---|
| Edit transaction | **`startCmd(name?)`**, **`endCmd(rollback=false)`** |
| Navigation | `newCursor()`, `firstSegment(type=All)`, `findSegmentAtTick(types, fraction)` (4.6), `tick2measure(fraction)` (4.6), `showElementInScore(el, staffIdx=-1)` (4.6) |
| Layout | `doLayout(startFraction, endFraction)` (4.6; whole score = `doLayout(fraction(0,1), fraction(-1,1))`) |
| Meta/text | `metaTag(tag)`, `setMetaTag(tag, v)`, `addText(tidName, text)` (adds to first VBox, creating one), `extractLyrics()` |
| Measures/parts | `appendMeasures(n)`, `appendPart(instrumentId)`, `appendPartByMusicXmlId(id)`, `insertPart(id, idx)`, `replacePart`, `replaceInstrument`, `removeParts`, `moveParts`, `setPartVisible`, `setPartSharpFlat`, `setInstrumentName`, `setInstrumentAbbreviature`, `replaceDrumset` (4.7) |
| Staves | `appendStaff`, `appendLinkedStaff`, `removeStaves`, `moveStaves`, `setStaffType`, `setVoiceVisible` (excerpt scores only), `setScoreOrder` (4.7) |
| System objects/locks | `addSystemObjects`, `removeSystemObjects`, `moveSystemObjects` (4.7); `addRemoveSystemLocks(interval, lock)`, `makeIntoSystem(first, last)` (4.6); `brackets(staffIdx)` (5.0) |
| Deprecated | `createPlayEvents()` — "PlayEvents don't have a playback effect in MuseScore 4" |

`startCmd`/`endCmd` mechanics (`score.cpp:600-631`):
```cpp
// Score::startCmd — name defaults to "Plugin edit"
undoStack()->prepareChanges(actionName);
undoStack()->lock();           // so cmd() and plugin edits collapse into ONE undo step
// Score::endCmd
undoStack()->unlock();
if (rollback) undoStack()->rollbackChanges(); else undoStack()->commitChanges();
notation()->notationChanged().send(muse::RectF());
```

`Part`: `startTrack`, `endTrack`, `instrumentId`, `musicXmlId`, `longName`, `shortName`, `partName`, `show` (RW), `midiChannel`, `midiProgram`, `hasDrumStaff/PitchedStaff/TabStaff/ChordSymbol`, `harmonyCount`, `lyricCount`, `instruments`, `staves`, `masterPart`; methods `instrumentAtTick(int|Fraction)`, `longNameAtTick`, `shortNameAtTick`, `instrumentNameAtTick`, `instrumentIdAtTick`, harp-diagram lookups (`part.h:58-197`).
`Instrument`: `instrumentId`, `musicXmlId`, `longName`, `shortName`, `transpose`, `stringData`, `drumset`, `channels`, `cloneDrumset()`; `Channel`: `name`, `volume`/`pan`/`chorus`/`reverb`/`mute`/`midiProgram`/`midiBank` (RW) (`instrument.h`). `Drumset` has per-pitch getters + setters (`setName`, `setNoteHead`, `setLine`, `setVoice`, `setStemDirection`, `setShortcut`). `Excerpt`: `partScore`, `title`. `PlayEvent` (creatable): `pitch`, `ontime`, `len` (RW), `offtime`.

## 5. INVENTORY — `Cursor` — `cursor.h` / `cursor.cpp`

A `Cursor` carries its own `InputState` (position+track+duration) unless `inputStateMode = INPUT_STATE_SYNC_WITH_SCORE`, in which case it shares the live note-input state (`cursor.cpp:91-109`). **A fresh cursor points nowhere** — `segment` is null until you `rewind*`.

| Member | Kind | Behaviour (cite `cursor.cpp`) |
|---|---|---|
| `track` / `staffIdx` / `voice` / `staff` | RW | `track` clamped to `[0, ntracks-1]` (`:702-709`); `staffIdx = track/4`, `voice = track%4` (`VOICES`=4); `setStaff` moves to that `Staff` keeping voice (`:769`) |
| `filter` | RW int | `SegmentType` bitmask; default `ChordRest`; governs `next()`/`nextMeasure()`/`rewind` (`cursor.h:84`, ctor `:50`) |
| `score` | RW | associated score |
| `segment`, `measure`, `element` | R | `element` = item **at current track** in the segment (`currentElement`, `:649`) — may be a chord, a rest, or null if the track is empty there |
| `tick` | R int | deprecated for position since 4.6 (use `fraction`) |
| `utick` | R int | tick accounting for repeats (4.6) |
| `fraction` | R `Fraction` | exact position (4.6) |
| `tempo` | R | at current position |
| `keySignature` | R int | staff key at pos (`:837`) |
| `stringNumber` | RW | physical string for tab input |
| `inputStateMode` | RW | independent / sync (`Cursor.INPUT_STATE_*`) |
| `rewind(mode)` | method | `SCORE_START` (first measure's first `filter` segment, then `nextInTrack`), `SELECTION_START`, `SELECTION_END` (no-op unless a **range** selection) (`:123-158`) |
| `rewindToTick(int)` | method | uses `tick2leftSegment(tick+1)`; may land on the segment *before* the tick; **crashes if no segment is found** (`seg->segmentType()` on null, `:170-183`) — ⚠ potential bug, not fixed here |
| `rewindToFraction(f)` | method | exact `tick2segment`; ignores invalid/negative; creates a `TimeTickAnchor` if `filter` includes `TimeTick` and nothing is there (`:185-211`) |
| `time(includeRepeats=false)` | method | ms from tempo timeline (`:631`) |
| `next()` / `prev()` / `nextMeasure()` | method | return `false` at end; **skip segments with nothing on the current track** (`nextInTrack`/`prevInTrack`, `:803-829`) |
| `add(el)` | method | the central insertion routine — dispatches on element type (below) |
| `addNote(pitch, addToChord=false)` | method | validates pitch; default duration 1/4 if unset; `NoteInput::addPitch(...)` on the current transaction (`:450-465`) |
| `addRest()` | method | `score->enterRest(duration)` (`:475`) |
| `addTuplet(ratio, duration)` | method | validates (positive, within measure, valid base length, `canAddTuplet`) then `score->addTuplet(...)` with style-default number/bracket types (`:521-579`); only logs on failure |
| `setDuration(z, n)` | method | `TDuration(z/n)`; invalid → quarter (`:590`) |

`Cursor::add` dispatch (`cursor.cpp:276-437`): sets `track`, ownership parent, score. Chords/rests → `undoAddCR`. `KEYSIG` → `KeySig` segment at tick. `TIMESIG` → `EditTimeSig::addTimeSig` (then re-seats cursor). `LAYOUT_BREAK` → `MeasureBase::addInternal`. `MEASURE_NUMBER/SPACER/JUMP/MARKER/HBOX/STAFFTYPE_CHANGE` → parent = **measure**. `NOTE/ARPEGGIO/CHORD_BRACKET/TREMOLO_*/CHORDLINE/ORNAMENT/TAPPING/ARTICULATION` → onto the **current chord** (`Chord::addInternal`). `LYRICS` → on current ChordRest. `SYMBOL` (also onto a Rest), `FINGERING/BEND/NOTEHEAD` → onto the chord's **first note**. `CLEF` → header/clef segment (defaults `INVALID`→`G`). `AMBITUS` → searches backwards for a clef. Everything else (`default:`) → `undoAddElement` at the current segment.

## 6. Packaging, discovery, enabling, launching

### 6.1 Where plugins live (⭐ resolves the stub's UNCLEAR)

Two roots are scanned (`extensionsconfiguration.cpp:64-72`):

| Root | Path | Source |
|---|---|---|
| **Bundled** | `<appDataPath>/plugins` — on Windows `<exe dir>/../plugins` (`globalconfiguration.cpp:77-79`); in the repo, installed from `share\plugins` | `pluginsDefaultPath()` |
| **Per-user** | setting `application/paths/myPlugins` (`USER_PLUGINS_PATH`, `extensionsconfiguration.cpp:36,69-72`); **default** `userDataPath() + "/Plugins"` (`:42`) | `pluginsUserPath()` |

`userDataPath()` = `QStandardPaths::DocumentsLocation + "/" + QCoreApplication::applicationName()` (`globalconfiguration.cpp:161-165`). `applicationName` = `MUSE_APP_NAME_MACHINE_READABLE MUSE_APP_VERSION_MAJOR` (`src\app\main.cpp:124-126`) = `"MuseScoreStudio"` + `"5"` (`version.cmake:22,24`), with a `"Development"` suffix when `MUSE_APP_UNSTABLE` (set for build mode `DEV`, `SetupConfigure.cmake:47-48`). So:

- Release/Testing build, Windows: **`%USERPROFILE%\Documents\MuseScoreStudio5\Plugins`**
- DEV-mode build: **`…\Documents\MuseScoreStudio5Development\Plugins`**

The directory is created at startup (`io::Dir::mkpath`, `:47-49`), and the user can repoint it in Preferences → Folders → Plugins (the FileIO helper comments say so, `util.cpp:57`). Plugin code can read the effective value with `FileIO.pluginsUserPath()` (`util.cpp:58`) and its own folder with `FileIO.pluginDirectoryPath()` (`:99`). The user dir is on the file-system write allow-list (`extensionsmodule.cpp:91`; `FileIO::isPathWriteable`, `util.cpp:165-239`).

Not to be confused with `extensions` (new-style, `manifest.json`): `defaultPath() = appDataPath/extensions`, `userPath() = userAppDataPath/extensions` (`:54-62`) — see `extensions-system.md`.

### 6.2 Discovery — `legacy::ExtPluginsLoader`

`ExtensionsRegister::reload()` loads new-style manifests, then `loadManifestList(pluginsDefaultPath, pluginsUserPath)` and joins them (`extensionsregister.cpp:34-49`). For each root:

1. `Dir::scanFiles(root, {"*.qml"})` — **recursive**; every `.qml` under the root is a candidate (`extpluginsloader.cpp:116-125`).
2. `dev_test_*` plugins are skipped unless `runtime::isDebug()` (`:31,100`).
3. `parseManifest` is a **line-oriented text scan, not a QML parse** (`:127-291`): the file must contain `{` and the substring `MuseScore`; then it walks lines until the first line ending in `{` *after* the root's `{` and picks up `title:`, `description:`, `pluginType:`, `categoryCode:`, `thumbnailName:`, `requiresScore:`, `version:` (7 expected, stops early when all found). Surrounding `qsTr("…")`/quotes are stripped; a `//4.4 ` prefix is honored for 3.6-compat. Result: `type = Macros`, `apiversion = 1`, `legacyPlugin = true`; `pluginType: "dialog"|"dock"` → `type = Form` (`docked` for `"dock"`); context `ProjectOpened` (default), `Any` if `requiresScore: false`, forced `ProjectOpened` if docked; missing `title` → file basename.
4. URI = `extension://plugin/v1/<path relative to root, lowercased, '.'→'-'>` (`makeUri`, `:37-42`). User-dir plugins get `isRemovable = true` (`:84-90`).
5. Two old plugin codeKeys are remapped to ported extensions: `colornotes → extension://notation/colornotes`, `addCourtesyAccidentals → extension://notation/courtesy_accidentals` (`:60-62`).

⚠ Consequences of the text scan: **properties must be written one per line, before the first nested `Item {`/function brace, at the top of the root object**; a property placed after the first `{` is invisible to the registry (the in-source comment says so, `:199-213`). `menuPath` is never parsed.

### 6.3 Enabling

- Enabled-ness is stored per URI in `<userAppDataPath>/extensions/config.json` as `[{ "uri": …, "enabled": bool }]` (`extensionsconfiguration.cpp:84-109,111-154`). **Default is disabled** (`ExtensionsRegister::isEnabled` → `muse::value(m_configs, uri).enabled`, a default-constructed `ExtensionConfig{false}`, `extensionsregister.cpp:122-125`, `extensionstypes.h:170-172`).
- One-time migration: if `config.json` is absent it reads the old 3.x/4.x list `<userAppDataPath>/plugins/plugins.json` and maps `codeKey` (= file basename) → URI (`:114,155-205`).
- Triggering a disabled plugin pops a dialog "The plugin “%1” is currently disabled. Do you want to enable it now?" (Yes → `setEnabled` + `perform`); the text points to **Home > Plugins** for the manual toggle (`extensionsactioncontroller.cpp:61-86`).
- Every manifest action is registered as a command via `makeCommand(uri, actionCode)` (`:44-59`), which is how menus/toolbar/shortcuts reach a plugin.
- Edits to a plugin on disk are picked up by `ExtensionsProvider::reloadExtensions()`, which first calls `clearComponentCache()` on the engines (`extensionsprovider.cpp:39-47`, `extensionsuiengine.cpp:131-…`); `ExtensionsListModel::reloadPlugins()` is the UI hook (`extensionslistmodel.h:70`).

### 6.4 Launching

`ExtensionsProvider::perform(uri, action)` (`extensionsprovider.cpp:89-120`):

- **`Form`** (`pluginType` dialog/dock): `docked` → `dockRequested` signal (right panel of the notation page); otherwise open `muse://extensions/viewer` (`ExtensionViewerDialog`). `ExtensionBuilder::load` builds the QML with `qmlEngineApiV1()`, gives it default implicit size (docked 360 wide / dialog 600 wide, height 480) when none is declared, calls `plugin->setup(engine)`, hooks `closeRequest()`, then queues `plugin->runPlugin()` on the Qt event loop (`extensionbuilder.cpp:62-180`).
- **`Macros`** (no `pluginType`): `run()` → `legacy::ExtPluginRunner::run`: `QQmlComponent` on `engine()->qmlEngineApiV1()`, `create()`, `dynamic_cast<IPluginApiV1*>`, then `runPlugin()` which just `emit run()` (`extpluginrunner.cpp:36-64`, `qmlpluginapi.h:522`).
- `PluginAPI::setup(QQmlEngine*)` wires the instance to the engine's `api.engraving` object (`EngravingApiV1::setApi(this)`) so `api.engraving.*` and the root object stay in sync (`qmlpluginapi.cpp:216-241`).
- The plugin runs against the **current** project/notation: `currentScore()` = `projectSession()->currentNotation()->elements()->msScore()` (`:504-511`). Hence `curScore` is `null` for `requiresScore:false` plugins when nothing is open.

## 7. Worked example — minimal plugin (cursor walk + read + add)

Save as `<Documents>\MuseScoreStudio5\Plugins\hello_notes\hello_notes.qml`, then enable it in Home > Plugins:

```qml
import QtQuick
import MuseScore 3.0

MuseScore {
    title: "Hello Notes"
    description: "Prints every note's pitch/tpc and adds a staff text above the first chord"
    version: "1.0"
    categoryCode: "composing-arranging-tools"   // keep these 3-7 header props on single lines, above any nested { }

    onRun: {
        curScore.startCmd("Hello Notes")        // one undo step; REQUIRED before any edit
        var c = curScore.newCursor()
        c.rewind(Cursor.SCORE_START)            // a new cursor points nowhere until rewound
        c.staffIdx = 0
        c.voice = 0
        var first = true
        while (c.segment) {
            var el = c.element                  // chord OR rest OR null on this track
            if (el && el.type == Element.CHORD) {
                for (var i = 0; i < el.notes.length; ++i) {
                    var n = el.notes[i]
                    console.log("measure", c.measure.no, "pitch", n.pitch, "tpc", n.tpc)
                }
                if (first) {
                    var t = newElement(Element.STAFF_TEXT)   // PLUGIN-owned, not in the score yet
                    t.text = "Hi"
                    t.placement = Placement.ABOVE
                    c.add(t)                                 // default branch: undoAddElement at this segment
                    first = false
                }
            }
            c.next()                            // false (segment -> null) at end of score
        }
        curScore.endCmd()                       // commit; endCmd(true) would roll back
        quit()
    }
}
```
Idioms confirmed in the bundled plugins: `curScore.startCmd` / `cmd("select-all")` / `curScore.endCmd()` / `quit()` (`notenames.qml:91-158`); note reading `note.tpc`, `note.line`, `note.string`, `chord.notes`, `note.elements`, `note.add(text)` / `note.remove(el)`; Fraction arithmetic `a.plus(b)`, `a.greaterThan(b)`, cursor `rewindToFraction`, `removeElement`, `Selection.select`, `cmd("tie")` and rollback via `curScore.endCmd(true)` inside `try/catch` (`NewRetrograde.qml:15-447`).

## 8. Non-obvious gotchas

- ⭐ **No `startCmd` = no undo / possible corruption.** Property writes on score-owned items call `undoChangeProperty`, and `Cursor.add*`/`removeElement` use the current transaction (`currentOrDummyTransaction()`, `cursor.cpp:312,464`). Without an open command the edit lands on a *dummy* transaction. Always bracket edits; on failure use `endCmd(true)`.
- ⭐ **API version changes enum representation.** Legacy plugins run with `apiversion = 1`: `el.type` is an **int** and enum-valued properties (`API_PROPERTY_ENUM`) are ints; v2 (extensions) yields **strings** (`scoreelement.cpp:69-77`, `elements.h:104-124`). `el.type == Element.NOTE` works in both only because `Element.NOTE` follows the same representation on the same engine.
- **`scores` is a stub** (empty list); **`newScore` doesn't open or name the score**; **`readScore(noninteractive=true)` returns null**; `writeScore`/`closeScore` only accept the **current** score; `newQProcess` returns null (`qmlpluginapi.cpp:264-474`). Don't build plugins on these.
- **`menuPath`/`dockArea` silently ignored.** A 3.x plugin keeps loading but never appears under a `Plugins` menu; it is listed in Home > Plugins and triggered via commands.
- **Header discovery is a text scan** (§6.2): properties after the first inner `{`, or computed with expressions the scanner can't see (e.g. `title: someFunction()`), are lost; `title` falls back to the file name.
- **Stale wrappers.** Wrappers hold raw pointers; after `removeElement`, `Cursor.add`, `cmd(...)` or layout, a wrapper may point to a deleted item. Re-fetch via the cursor/segment, as the retrograde plugin does (`NewRetrograde.qml:56-89`). `Chord.remove` refuses the last note (`elements.cpp:450`); `Note/Chord.remove` require the element to be a **direct child**, else use `removeElement()` (`:322,448`).
- **Positions are in spatium**, not pixels/points: `offsetX/Y`, `pos`, `pagePos`, `canvasPos`, `bbox`, POINT/ABSOLUTE properties are all divided by `spatium()` on read (`elements.h:1287-1304`, `scoreelement.cpp:120-129`).
- **`parent` is the *layout* parent** — a Measure reports its System, a SpannerSegment its System (`elements.h:1306-1308`).
- **`Cursor.rewindToTick` can crash on tick with no segment** and may land one segment early (`cursor.cpp:170-183`); prefer `rewindToFraction`. `Cursor.tick`/`Segment.tick` are integer ticks; 4.6+ recommends `fraction` for exactness (`cursor.h:86-99`).
- **Cursor skips empty tracks.** `next()` only stops where the current *track* has an element (`nextInTrack`); to see rests/other voices set `track` or `voice` first.
- **`Cursor.add` only supports listed element types** and places by type, not by "where the cursor is" for chord-level items — it needs a current **chord** (`curElement->isChord()`), else silently does nothing (`cursor.cpp:349-354,380-388`). `Note.add` accepts only the set in `Note::isChildAllowed` (`elements.cpp:247-269`).
- **`cmd()` takes engraving command ids**, not the 3.x action names (except the 11 in the compat map, `qmlpluginapi.cpp:423-435`). A wrong string is dispatched and does nothing visible.
- **Wrappers don't expose everything.** Commented-out `Note` properties (`ghost`, `headGroup`, `small`, `mirror`, `ppitch`, `subchannel` — `elements.h:1391-1399`) are covered instead by the generic `API_PROPERTY` set (`ghost`, `headGroup`, `small`, `mirrorHead`, `string`, …) — look there before assuming a property is missing.
- **Docs drift.** `Doxyfile.plugins` `INPUT` lists `doc/plugins.md`, `doc/plugins2to3.md`, … (`Doxyfile.plugins:793-797`) but the files now live in `docs\old_docs\` — the old Doxygen build is stale; the new docs are the `/** APIDOC */` jsdoc blocks in the headers (`tools\jsdoc`). `docs\old_docs\plugins.md` still shows `menuPath` and `Qt.quit()` and cites `Ms::PluginAPI` — pre-4.0 content.
- **Version history (from in-repo evidence only).** 2→3: `import MuseScore 1.0` → `3.0`, `setSig()` → `timesig = fraction(n,d)`, `pos` removed (use `offset`/`offsetX/Y`), enums split out of `MScore`/`Element` into `Placement`, `Direction`, `DirectionH`, `Tid`, `NoteHeadType/Group`, `NoteValueType` (`docs\old_docs\plugins2to3.md`). 3→5: the import stays `3.0`; additions are tagged `\since 3.2/3.3/3.5/3.6` (selection API, cursor input-state, tuplets, `Fraction`), **4.6** (Fraction arithmetic, `Cursor.fraction/utick/rewindToFraction/setStaff`, `Staff` per-tick queries, pages/systems/locks, ~60 new enums), **4.7** (score editing: parts/staves/instruments/drumset/system objects/spanners/lyrics) and **5.0** (`Score.brackets`, `qmlpluginapi` now `Contextable` + IoC-injected helpers). A line-by-line 3.x/4.x diff was not done.

## Cross-subsystem dependencies

- `engraving-dom.md` — every wrapper delegates to `src\engraving\dom` classes through `Pid` get/set (`undoChangeProperty`, `undoAddElement`, `deleteItem`, `Factory::createItem`).
- `extensions-system.md` — loader/register/provider/session/UI engine, new-style `manifest.json` extensions (api v2), JS macros (`ScriptEngine`, `api.engraving`), `.mext` bundles.
- `notation-layer.md` — `startCmd/endCmd` use `INotationUndoStack` (`prepareChanges/lock/commitChanges/rollbackChanges`); `cmd()` goes through `ICommandDispatcher` and the `notation` command ids; current score from `IProjectSession::currentNotation`.
- `style-system.md` — `Score.style` (`MStyle.value/setValue/resetValue` by style key string).
- `file-format-and-rw.md` — `readScore`/`writeScore` use `IEngravingPluginAPIHelper` (`src\project\internal\engravingpluginapihelper.*`).
- `architecture-and-ioc.md` — `PluginAPI` is `muse::Contextable` with `ContextInject<IProjectSession/IEngravingPluginAPIHelper/ICommandDispatcher>` (`qmlpluginapi.h:138-141`); registration happens in `EngravingModule::registerApi()` (`src\engraving\engravingmodule.cpp:134-149`).

## UNCLEAR — needs verification

- Exact UI text/location of the **Home > Plugins** page toggle (inferred from `extensionsactioncontroller.cpp:75` and `ExtensionsListModel`; QML `ExtensionsListPanel.qml` / `ui` not read this pass).
- Whether any **menu/toolbar slot** for plugins still exists in `appshell` (the old `menuPath` path is gone; `Action.showOnAppmenu = true` default in `extensionstypes.h:104-105` suggests macros appear in the app menu — not traced).
- Behaviour of `PluginAPI::interval` argument order against `engraving::Interval` constructor (assumed `(diatonic, chromatic)`; header and cpp disagree on names).
- Full property inventories for `Staff`, `Measure`, `System`, `Spanner*`, `Lyrics`, `Harmony`, `FretDiagram`, `Beam` were taken from `Q_PROPERTY`/`Q_INVOKABLE` greps, not every doc line; `Staff.*AtTick` semantic details unread.
- Whether `FileIO` writes outside `isPathWriteable` roots are actually blocked at the `write()` level (the helper exists; the `write()`/`writeBinary()` bodies were not read).
- Reload-on-edit UX (`ExtensionsListModel::reloadPlugins` trigger) and Plugin Creator availability (documented in `docs\old_docs\plugins.md` for 3.x; not found in this pass).
- 3.x/4.x → 5.0 behavioral differences beyond `\since` tags.

*Authored from: full reads of `qmlpluginapi.h/.cpp`, `cursor.h/.cpp`, `score.h`, `selection.h/.cpp`, `part.h`, `scoreelement.h/.cpp`, `apistructs.h`, `enums.h`, `engravingapiv1.h`, key spans of `elements.h/.cpp`, `apitypes.h`, `util.cpp`, `score.cpp`; `muse\framework\extensions` (`extensionsconfiguration.cpp`, `extensionsregister.cpp`, `extensionsprovider.cpp`, `extensionsactioncontroller.cpp`, `extensionsuiengine.cpp`, `legacy\extpluginsloader.cpp`, `legacy\extpluginrunner.cpp`, `qml\Muse\Extensions\extensionbuilder.cpp`, `extensionstypes.h`), `globalconfiguration.cpp`, `src\app\main.cpp`, `version.cmake`, `SetupConfigure.cmake`; bundled plugins `notenames.qml`, `NewRetrograde.qml`, `lilyrics.qml`, `dev_test_api.qml`; `docs\old_docs\plugins.md`, `plugins2to3.md`, `Doxyfile.plugins` (2026-11-08). **If this contradicts the live source, the source wins.***
