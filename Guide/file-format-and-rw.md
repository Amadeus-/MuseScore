# MuseScore — File Format & Read/Write

> **What this file is.** Scope: the on-disk format (`.mscz` zip container, `.mscx` folder, `.mscs` single-XML test container, and the `.mscx` score XML inside), `src\engraving\rw` (versioned readers + the one writer), `src\engraving\infrastructure\mscreader/mscwriter` (the container I/O), and `src\project` (the load → migrate → in-memory → save lifecycle). Deep pass 1 (container/ladder/lifecycle) and deep pass 2 (per-element XML tags, §8) source-verified 2026-11-08 against the 5.0 `main` tree. **Status: ✅ complete for mechanism + category coverage; residual long-tail listed in §8.3.** **Live source wins** — re-verify line numbers before editing. Start at `00-core.md`. Element *semantics* live in `engraving-dom.md`; the style file in `style-system.md`.
>
> All cites are relative to `C:\Dev\+MuseScore+`.

## 0. 60-second orientation

- ⭐ **Format version number** is `Constants::MSC_VERSION = 500`, string `"5.00"` (`src\engraving\types\constants.h:31-32`). It is written as `<museScore version="5.00">` and parsed back as `major*100 + minor` (`mscloader.cpp:231`). 1.14→114, 2.06→206, 3.02→302, 4.10→410, 4.60→460, 5.00→500.
- ⭐ **Container** (`MscIoMode`, `src\engraving\infrastructure\mscio.h:55`): `mscz` = ZIP; `mscx` = a *directory* with the same layout as the zip, unpacked (and the `.mscx` main file is inside it); `mscs` = one XML file concatenating all text members (tests/workflows); `mscz~` = backup (zip).
- ⭐ **One reader class per format era**, picked by the file's version in `RWRegister::reader(version)` (`src\engraving\rw\rwregister.cpp:39`). **One writer** (`write::Writer`) — it only ever writes the current version.
- ⭐ **Key classes:** `MscReader`/`MscWriter` (container I/O) → `MscLoader`/`MscSaver` (assemble all container members around the score XML) → `RWRegister` → `IReader` (`ReadNNN`) / `IWriter` (`write::Writer`) → `TRead`/`TWrite` (per-element tag handlers). Above them: `EngravingProject::loadMscz/writeMscz` (`src\engraving\engravingproject.cpp:135,145`) and `NotationProject::load/save` (`src\project\internal\notationproject.cpp`).
- **Public vs internal** (README): classes in the `rw\` root (`rwregister`, `mscloader`, `mscsaver`, `xmlreader`, `xmlwriter`, `ireader`, `iwriter`, `inoutdata`) are the public surface; anything in `read*\` / `write\` subfolders is internal to the rw subsystem (`src\engraving\rw\README.md:5`).

## 1. The container — `.mscz` / `.mscx` / `.mscs`

### 1.1 Layout of a `.mscz` (as written by `MscSaver::writeMscz`, `src\engraving\rw\mscsaver.cpp:54`)

Member names are hard-coded in `MscWriter` (`infrastructure\mscwriter.cpp`) and mirrored in `MscReader` (`infrastructure\mscreader.cpp`). The reader resolves members **by name/prefix** ("The current implementation resolves files by extension", `mscreader.cpp:33`).

| Zip member | Written by | Content | Condition |
|---|---|---|---|
| `META-INF/container.xml` | `MscWriter::writeContainer` (`mscwriter.cpp:217`), auto at `close()` (`writeMeta`, :206) | `<container><rootfiles><rootfile full-path="…"/>…` — a list of every member written (in write order) | always, last |
| `score_style.mss` | `writeStyleFile` (:135) | `<museScore version="5.00"><Style>…` — the **full** master style (`MStyle::write`, `style\style.cpp:742`) | always (master only) |
| `<name>.mscx` (default `score.mscx`) | `writeScoreFile` (:159); name = `mainFileName()` (:140): `params.mainFileName`, else `<completeBaseName of filePath>.mscx`, else `score.mscx` | the score XML (§4) | always |
| `Excerpts/<excerptName>/<excerptName>.mss` | `addExcerptStyleFile` (:164) | part-score style | per excerpt (part), not when range-saving |
| `Excerpts/<excerptName>/<excerptName>.mscx` | `addExcerptFile` (:170) | part-score XML | per excerpt |
| `Excerpts/<excerptName>/audiosettings.json`, `…/viewsettings.json` | `writeAudioSettingsJsonFile/ViewSettings…(pathPrefix)` (:191,196) via `NotationProject::writeProject` (`notationproject.cpp:926-935`) | per-part solo/mute + view state | per excerpt |
| `chordlist.xml` | `writeChordListFile` (:176) | custom chord list | only if `chordList->customChordList() && !empty()` (`mscsaver.cpp:156`) |
| `Pictures/<hashName>` | `addImageFile` (:186) | embedded images (`imageStore` items) | only images with `isUsed(score)` (`mscsaver.cpp:166`) |
| `Thumbnails/thumbnail.png` | `writeThumbnailFile` (:181) | 512-px-max PNG of page 1 (`MscSaver::createThumbnail`, :247) | `createThumbnail && !pages.empty()` |
| `audiosettings.json` | `ProjectAudioSettings::write` (`projectaudiosettings.cpp:339`) | mixer/playback settings | project layer, not engraving |
| `viewsettings.json` | `viewState()->write(msczWriter)` (`notationproject.cpp:920`) | per-notation view state | project layer |
| `automation.json` | `writeAutomationJsonFile` (:201); `AutomationRW::write(…, writeGenerated=false)` | audio automation | only if `score->automationData()` and result non-empty (`mscsaver.cpp:189-194`) |

`META-INF/container.xml` is *not read* by the zip reader — `ZipFileReader::fileList()` just enumerates zip entries (`mscreader.cpp:313`). It *is* the discriminator for the directory flavour: `DirReader::isContainer()` returns true only if `<root>/META-INF/container.xml` exists (`mscreader.cpp:384-389`).

⭐ **Locating the main score on read** (`MscReader::readScoreFile`, `mscreader.cpp:154`): try `mainFileName()`; if empty *and* a container, pick the first root-level (`!contains('/')`) `*.mscx` (case-insensitive). This is how a renamed `.mscz` still opens.

### 1.2 The three I/O modes

| `MscIoMode` | Suffix (`mscIoModeBySuffix`, `mscio.h:62`) | Reader / Writer impl | Notes |
|---|---|---|---|
| `Zip` | `mscz`, `mscz~` | `ZipFileReader`/`ZipFileWriter` over a `muse::io::IODevice` (`ZipReader`/`ZipWriter`) | the real format; device may be a file or in-memory `Buffer` |
| `Dir` | `mscx` | `DirReader`/`DirWriter` (a folder at `containerPath(path)` = the **containing directory** of the `.mscx`, `mscio.h:74`) | `DirWriter::open` does **`removeRecursively()` on the target dir first** (`mscwriter.cpp:331`) — hence `NotationProject::doSave` writes to a `<path>_saving` temp dir and moves files (§5.4). Reader asserts no device (`NOT_SUPPORTED` if given one, `mscreader.cpp:359`). |
| `XmlFile` | `mscs` | `XmlFileReader`/`XmlFileWriter` | `<files><file name="x"><![CDATA[…]]></file>…</files>`; **only `mscx`, `json`, `mss` members are stored, others silently skipped** (`mscwriter.cpp:440-445`); reads re-scan the stream from offset 0 for every lookup (O(n) per file, `mscreader.cpp:497-559`). |

`mscs` is handled only by the engraving I/O layer and tests: `notationmodule.cpp:67-68` registers writers for `mscz` and `mscx` only (`MscNotationWriter`, `src\notation\internal\mscnotationwriter.cpp`); `NotationProject::load` accepts `mscs` via `isMuseScoreFile` (`mscio.h:50`).

### 1.3 Other flavours / helpers
- `MscMetaReader` (`src\project\internal\mscmetareader.cpp`) opens a `MscReader` just to pull **thumbnail** (`readThumbnailFile`) and **project meta** (stream-parses the `.mscx` for `metaTag`s and title-frame `Text` boxes, `doReadMeta/doReadBox`) — used by the recent-files / open-dialog UI without loading the score.
- `tools\ziprw\main.cpp` — tiny standalone `unzip`/`zip` utility built on `muse::ZipReader/ZipWriter` (generic dir↔zip helper; no MuseScore-specific logic in its source — presumably for unpacking/repacking `.mscz` in dev workflows).
- `mscx` "folder" saves produce exactly the zip layout un-zipped, but with `mainFileName` = the chosen `.mscx` name (`mscio.h:92`, `NotationProject::doSave` passes `params.mainFileName = targetMainFileName`, `notationproject.cpp:700`).

## 2. The rw version / registry system

### 2.1 Selecting a reader — two stages

**Stage A: parse version from the file** (`MscLoader::readMasterScore`, `mscloader.cpp:224`):
```cpp
// MscLoader::readMasterScore — mscloader.cpp:228-234
if (e.name() == "museScore") {
    const String version = e.attribute("version");
    const StringList sl = version.split(u'.');
    score->setMscVersion(sl[0].toInt() * 100 + sl[1].toInt());
    RetVal<IReaderPtr> reader = makeReader(score->mscVersion(), ignoreVersionError);
```
(Note: `sl[1]` indexing assumes a `major.minor` string; a malformed attribute is not guarded — see gotchas.)

**Stage B: gate then dispatch** — `makeReader` (`mscloader.cpp:55`):
```cpp
if (!ignoreVersionError) {
    if (version > Constants::MSC_VERSION) return …Err::FileTooNew;
    if (version < 114)                     return …Err::FileTooOld;
    if (version == 300)                    return …Err::FileOld300Format;
}
return RetVal<IReaderPtr>::make_ok(RWRegister::reader(version));
```
`ignoreVersionError` is the project's `forceMode` (`OpenParams.forceMode` → `loadMscz(…, openParams.forceMode)`, `notationproject.cpp:179`).

**The register** (`RWRegister::reader`, `rwregister.cpp:39-60`) — pure threshold ladder, no table:

| File version `v` | Reader class | Dir | Notes |
|---|---|---|---|
| `v <= 114` | `read114::Read114` | `read114\` | MuseScore 1.x. `readScoreFile` asserts `score->isMaster()` (`read114.cpp:2745`). |
| `114 < v <= 207` | `read206::Read206` | `read206\` | 2.x (2.07 mapped here too). Applies bar-line-span fix-ups after reading (`read206.cpp:3649-3712`). |
| `207 < v < 400` | `read302::Read302` | `read302\` | 3.x (3.0.1–3.6). |
| `400 <= v < 410` | `read400::Read400` | `read400\` | 4.0.x. |
| `410 <= v < 460` | `read410::Read410` | `read410\` | 4.1–4.5 (copy of 400). |
| `460 <= v < 500` | `read460::Read460` | `read460\` | 4.6–4.7 (copy of 410). |
| `v >= 500` (and `v<0` ⇒ current) | `read500::Read500` | `read500\` | **current** (5.0). |

`reader(int version = -1)`: negative ⇒ `Constants::MSC_VERSION` (`rwregister.cpp:41`). The register is also used for **paste**: `IReader::pasteStaff/pasteSymbols/readTremoloCompat/readItem` (`ireader.h:97-101`) are called with the *current* reader — clipboard data is version-stamped `StaffList version="5.00"` and `Read500::pasteStaff` rejects any other version outside test mode (`read500.cpp:385-390`).

### 2.2 Code-sharing structure (README + verified)
- `src\engraving\rw\README.md:9-13`: **114–400 share code** (careful when editing); `read410` is a copy of `read400` used *only* for 4.10..4.59, and may be freely refactored; `read460` is a copy of `read410`. `read500` is in turn the evolution of `read460` (no README line yet).
- Verified: `Read114`, `Read206`, `Read302` are **single `.cpp` files** that `using namespace read400` and call `read400::TRead`, `read400::StaffRead`, `read400::ReadContext` for the element body (`read302.cpp:50,60,124,134`; `read206.cpp:120`; `read114.cpp:107`). They override only the score-level tag loop and legacy quirks. Their `pasteStaff/pasteSymbols/readTremoloCompat/doReadItem` are `UNREACHABLE` (`read302.cpp:336-355`, `read206.cpp:3717-3736`).
- `Read400/410/460/500` each own the full set: `readNNN`, `readcontext`, `tread` (≈4.3k–4.8k lines each), `staffread|staffrw`, `measureread|measurerw`, `connectorinforeader` (not in 500).

### 2.3 The `IReader` / `IWriter` interfaces (`src\engraving\rw\ireader.h`, `iwriter.h`)
- `IReader::readScoreFile(Score*, XmlReader&, ReadInOutData*)` — the one required entry (reads a *whole `.mscx`*, master or excerpt).
- `IReader` also: `pasteStaff`, `pasteSymbols`, `readTremoloCompat`, templated `readItem<T>` (debug-asserts T is one of `Accidental*, ChordRest*, Harmony*, Spanner*, StaffTypeChange*, Symbol*, Text*, Tuplet*` or `EngravingItem*`) → private `doReadItem`.
- `IWriter`: `writeScore(Score*, IODevice*, WriteInOutData*)`, `writeStaffSelection(...)` (clipboard), `writeSymbolListSelection(...)`, `writeItem<T>`.

### 2.4 In/out data carriers (`inoutdata.h`)
```cpp
struct ReadInOutData {          // inoutdata.h:45
    ReadLinks links;            // for master - out; for excerpt - in  (linked-element bookkeeping)
    std::optional<double> overriddenSpatium;
    PropertyIdSet propertiesToSkip;
    bool forcePageMode = false; // set from OpenParams.forcePageMode
    SettingsCompat settingsCompat;           // out: legacy audio mute/solo etc. (applied by NotationProject after load)
    std::optional<double> originalSpatium;   // out
};
struct WriteInOutData { write::WriteContext ctx; … };   // :58
```
`WriteContext` (`write\writecontext.h`) carries curTick/curTrack/tickDiff/trackDiff (used to write `<tick>`/`<location>` deltas only when needed), clipboard mode, `SelectionFilter`, and an optional `WriteRange` (staff + measure range) for **save-selection** and **save-page** (`NotationProject::saveSelectionOnScore`/`savePage`, `notationproject.cpp:940,576`).

⚠ **Observation (TRACED):** `Read500::readScoreFile` does *not* call `ctx.initLinks(data->links)` nor write `data->links` back (compare `Read400::readScoreFile`, `read400.cpp:78-80,117-118`). `MscLoader::loadMscz` still seeds `partReadInData.links = inOut->links` for excerpts (`mscloader.cpp:178`). Whether `read500` replaced link resolution entirely by EIDs (it reads `<eid>` everywhere) is the likely reason — see UNCLEAR.

### 2.5 What `write\` emits
Single writer, no versioning: `RWRegister::writer()` → `std::make_shared<write::Writer>()` (`rwregister.cpp:62`).
- `Writer::writeScore` (`write\writer.cpp:49`): `xml.startDocument()`; `<museScore version="5.00">`; unless `MScore::testMode`: `<programVersion>` + `<programRevision>`; then `Writer::write(score, xml, ctx, hook)`; finally stamps `score->m_mscVersion = MSC_VERSION`, `m_mscoreVersion`, `m_mscoreRevision` **unless** range-saving (`:74-79`).
- Also `writeStaffSelection` → `<StaffList version="5.00" tick len staff staves [timeStretch]>` (clipboard, `:264`), `writeSymbolListSelection` → `<SymbolList version fromtrack totrack>` (`:322`), `doWriteItem` (single element, clipboard mode, `:258`).
- Per-element bodies are in `twrite.cpp` (3.8k lines, `TWrite::write(const X*, …)` overloads + `writeProperties`), `staffwrite.cpp`, `measurewrite.cpp`, `connectorinfowriter.cpp`.
- The `xml.tag()` family (`xmlwriter.h`) skips default-valued properties (`tag(name, val, def)`, `tagProperty(Pid, val, def)`) — unchanged-from-default properties are **omitted** from the file; the style is the same (§3 + `MStyle::save(xml, optimize)`).

## 3. `XmlReader` / `XmlWriter` wrappers (`rw\xmlreader.*`, `rw\xmlwriter.*`)
- `XmlReader : muse::XmlStreamReader` (`xmlreader.h:33`) adds: `unknown()` (logs tag + byte offset via `m_docName`, then `skipCurrentElement()`, `xmlreader.cpp:141`), `readBool/readPoint/readSize/readScale/readRect/readColor/readFraction/readXml`, `setDocName` (error reporting), `setByteOffsetAdjustment` (pre-3.01 files). `readColor` expects `r,g,b[,a=255]` attributes.
- `XmlWriter : muse::XmlStreamWriter` (`xmlwriter.h:31`): `startElement(name|EngravingObject*, attrs)`, `tag(...)` overloads, `tagProperty(Pid|name, PropertyValue, def)`, `tagFraction`, `tagPoint`, `writeXml`, `comment`; optional `setRecordElements` to collect `(object, name)` pairs.
- ⭐ **Unknown tags are tolerated**: every reader loop ends with `else { e.unknown(); }` (log + skip subtree), so *adding* a new tag in 5.x is read-compatible with same-version readers that don't know it; but any data it carries is **silently dropped** on resave.

## 4. The `.mscx` XML structure (5.0)

Root: `<museScore version="5.00">`, then (non-test) `<programVersion>`, `<programRevision>` (hex), then `<Score>`. Excerpt files have the same root with a `<Score>` containing `<name>`/`<initialPartId>`/`<Tracklist>` and part-local style.

Verified sample (`src\engraving\tests\tuplet_data\tuplet1-ref.mscx`, version 5.00): `<Score><eid>…</eid><Division>480</Division><Style>…</Style><showInvisible/>… <metaTag name=…/>… <Part id="1"><eid/><Staff><eid/><StaffType/></Staff><Instrument id="voice">…</Instrument></Part> <Staff id="1"><Measure><eid/><voice><Clef/><TimeSig/><Rest/></voice></Measure>…`.

### 4.1 `<Score>` children, in write order (`Writer::write`, `writer.cpp:88-249`; read side `Read500::readScoreTag`, `read500.cpp:119`)

| Tag | Meaning / notes |
|---|---|
| `<eid>` | the score's `EID` (base64 `uint64_t` pair, `A_B`-style; written for every item except palette scores, `TWrite::writeItemEid`, `twrite.cpp:585`) |
| `<name>`, `<Tracklist sTrack dstTrack>`, `<initialPartId>` | excerpt-only: part name, track mapping (written only if mapping is not the identity) and initial part id (`writer.cpp:96-111`) |
| `<layoutMode>` | `line` / `system` (omitted for page mode); skipped on read if `forcePageMode` |
| `<Synthesizer>` | master only, not in test mode — legacy synth state (`m_synthesizerState`) |
| `<page-offset>`, `<Division>` | page-number offset; tick division (always `Constants::DIVISION` = 480) |
| `<Style>` | **only** for non-master (part) scores *or* `testMode+testWriteStyleToScore` — via `WriteScoreHook::onWriteStyle302` → `style().save(xml, optimize=true)` (diff vs builtin). The master's style goes to `score_style.mss` instead. Reader still accepts `<Style>` in the master (`read500.cpp:149`). |
| `<showInvisible> <showUnprintable> <showFrames> <showMargins> <markIrregularMeasures> <showSoundFlags> <open>` | view flags (`showSoundFlags` written only when false; `open` only when true) |
| `<metaTag name="…">value</metaTag>` | all `m_metaTags`; `platform`/`creationDate` omitted in test/save-template mode. Legacy `copyright/rights/work-title/…` tags are still *read* and mapped to metaTags. |
| `<Order>` | `ScoreOrder` (instrument ordering) |
| `<SystemObjects><Instance staffId>` | staves that carry system objects (1-based id) |
| `<Part id>` / `<SharedPart id>` | parts (+ `<eid>`, staves, instrument, `<show>`, `<soloist>`, `<hideWhenEmpty>`, `<preferSharpFlat>`; `SharedPart` adds `SHARED_PART_ENABLED` and `<StaveSharingState tick>` blocks) — **`SharedPart` is new in 5.00** (stave sharing; `write(const SharedPart*)`, `twrite.cpp:2640`; a normal `Part` links it via `<sharedPart>eid`) |
| `<Staff id>` | one per staff, each containing `<Measure>`s (+ `HBox/VBox/TBox/FBox` frames in staff 1) — see below |
| `<SpannerMap>` | **new in 5.00 form:** all non-generated spanners (slurs, hairpins, ottavas, …) written once at score level, main-linked elements first (`TWrite::writeScoreSpanners`, `twrite.cpp:514`) |
| `<PageLocks>`, `<SystemLocks>`, `<SystemDividers>` | layout locks (`RangeLock`s) and user system dividers (`<system idx>…`) — written only when present |
| nested `<Score>` | excerpts in the main file — **test mode only** (`WriteScoreHook::onWriteExcerpts302`, `writescorehook.cpp:54`); in production excerpts go to `Excerpts/…` members. Reader still supports it (`read500.cpp:217`). |

### 4.2 `<Staff id="N">` → `<Measure>` → `<voice>`
- Staff 1 (idx 0) is read with the *frames* and sets measure ticks/timesig inheritance (`StaffRead::readStaff`, `read500\staffread.cpp:36-94`); staves ≥ 2 re-walk the same measure list (`:95-126`). A `<tick>` element resets the running tick (`ctx.fileDivision(...)`).
- `<Measure [len="n/d"]>` — `len` only when irregular (`MeasureWrite::writeMeasure`, `measurewrite.cpp:45`). System-level data (`<eid>`, `<startRepeat/>`, `<endRepeat>`, `<irregular>`/`<noOffset>`/`<measureNumberMode>`/`<breakMultiMeasureRest>`/`<stretch>`) is written **only on the first staff** (`writeSystemElements`). Per-staff: `<MeasureNumber>`, `<MMRestRange>`, `<vspacerUp/Down/Fixed>`, `<visible>`, `<stemless>`, `<hideIfEmpty>`, `<measureRepeatCount>`, measure-level elements (`Marker`, `Jump`, …), then one `<voice>` per used voice.
- `<voice>` content is segment-ordered elements: `Clef`, `KeySig`, `TimeSig`, `Tempo`, `Rest`, `Chord`(→`Note`), `Tuplet`…`<endTuplet/>`, `BarLine`, annotations, etc. A `<tick>`/`<location>` element is emitted only when the running position differs (`TWrite::writeSegments` + `writeVoiceMove`, `twrite.cpp:3640-3788`). Element bodies: see `engraving-dom.md`; tag handlers `TRead::read(X*, …)` (`read500\tread.cpp`) / `TWrite::write(const X*, …)`.
- **MM rests:** the writer still emits the generated MM-rest `<Measure>` after its first underlying measure (with `<multiMeasureRest>` + `<mmRestLast>eid`) when `createMultiMeasureRests` is on (`staffwrite.cpp:50-52`, `measurewrite.cpp:55-68`); the *time signature* is deliberately moved back into the underlying measure so the reader sees it first (`MeasureWrite::segmentsToWrite`, `measurewrite.cpp:153`). The reader links the MM rest to its underlying measures by EID (`ctx.addMMRestEndMeasureEID` / `setMMRestEndMeasures`, `measureread.cpp:179`, `read500.cpp:263`).
- ⭐ **Linked staves / elements** use `<linkedTo>`/EIDs via `TWrite::writeItemLink` (skipped in clipboard mode, `twrite.cpp:603`). In the `SpannerMap` writer the *main* linked spanner is written before its linked copies "because the reader resolves linkedTo immediately" (`twrite.cpp:532-537`).

### 4.3 EIDs (new identity scheme)
`EID` (`src\engraving\infrastructure\eid.h`) is a 128-bit (2×`uint64_t`) id serialized as base64 `A_B`; `isValid()` false only when both halves are `-1`. Every non-palette item is written with `<eid>` (assigning a new one if missing — **writing can mutate the score** by assigning EIDs, `twrite.cpp:591-594`). Test mode uses deterministic EIDs (`newUniqueTestMode`) — which is why test refs show `B_B`, `C_C`, …

## 5. The versioned reader chain (table: version → reader → notes)

| Reader | Handles | Score-level specialties (TRACED) |
|---|---|---|
| `Read114` (`read114.cpp:2743`) | ≤ 1.14 | Master only. Old tags: `siglist` → time-sig map (via `Read206::readTimeSigMap`), `tempolist` → converted into invisible tempo text (`tm` map), `Spatium`, `SyntiSettings`, obsolete `Mag/MagIdx/xoff/yoff/Symbols/cursorTrack` skipped. Own `readStaffContent` + 1.x element readers (3257 lines). |
| `Read206` (`read206.cpp:3609`) | 2.0–2.07 | Reads 2.x tags; afterwards `read114::Read114::setBarLineSpanToStaves` + per-segment bar-line-span override reconstruction (`:3649-3712`). Has `readTimeSigMap`. 3736 lines. |
| `Read302` (`read302.cpp:248`) | 3.0.1–3.6 (355 lines) | Skips `Omr/Audio/showOmr/playMode/LayerTag/Layer/currentLayer/Revision`; `<Style>` handled with spatium override; recognises inline spanners `HairPin/Ottava/TextLine/Volta/Trill/Slur/Pedal`; instrument-ID repair: **3.6.x (`mscVersion==302`)** runs `fixInstrumentId` (maps `Winds`→`winds`, `harmonica-d12*`→`d10*`, `mdl-*` marching ids, …, `read302.cpp:292`), older scores `updateInstrumentId()` (`:200-214`); `CompatUtils::assignInitialPartToExcerpts` for master. Element bodies delegate to `read400`. |
| `Read400` (`read400.cpp:66`) | 4.0.x | Per `mscio.h:36-43`, 4.0 is the first format with style/chordlist/etc. in separate files. Uses `ctx.initLinks`, `clearOrphanedConnectors`; `connectorinforeader`; skips legacy tags (`Omr`, `Audio`, `Layer`, …). |
| `Read410` (`read410.cpp:67`) | 4.1–4.5 | Copy of 400; free to prune pre-4.10 compat (README). |
| `Read460` (`read460.cpp:68`) | 4.6–4.7 | Copy of 410 (410 already has `eid`, `showSoundFlags`, `SystemLocks`, `initialPartId`; grep shows `SystemDividers` only from 460). Still reads **inline spanners** (`HairPin/Ottava/TextLine/Volta/Trill/Slur/Pedal`) and `ctx.reconnectBrokenConnectors()` (`read460.cpp:197-203`). |
| `Read500` (`read500.cpp:69`) | ≥ 5.00 | **vs 460 (diffed):** + `PageLocks`; + `SpannerMap` (replaces the inline spanner tags — they now go through `TRead::readScoreSpanners`); + `SharedPart`; `ctx.reconnectBrokenConnectors` → `ctx.setMMRestEndMeasures()` + `ctx.connectNoteAnchoredSpanners()`; `readItemEID(score, e, ctx)` takes ctx; `ReadContext(score, pasteMode)` ctor; paste handles `<SpannerMap>` inside `StaffList` instead of per-staff `<Spanner>`; removed `clearOrphanedConnectors`/`initLinks`. |

After the tag loop `Read500::readScoreTag` (`read500.cpp:263-324`) does the **post-read fix-ups** common to all modern readers: `setMMRestEndMeasures`, `connectNoteAnchoredSpanners`, error check (`XmlStreamReader::CustomError` ⇒ `FileCriticallyCorrupted`, other ⇒ `FileBadFormat`, `:95-98`), dash-lyrics `nextLyrics` relinking, `m_fileDivision = DIVISION`, `updateInstrumentId` for all instruments, `updateTicksAndTimeSigMap`, `invalidateRepeatList`, `connectTies`, `undoRemoveStaleTieJumpPoints`, `updateHarmonyChannels`, `rebuildMidiMapping/updateChannel`, `updateOttava`, then `addSystemObjectStaff`.

### 5.1 Compat layers *outside* the readers
Reading a score runs **three** compatibility stages, not one:
1. **In `MscLoader::loadMscz`** (`mscloader.cpp:74`): `ReadStyleHook` — default style by version (§5.2); chord list hook; **`CompatUtils::doCompatibilityConversions(masterScore)`** after *all* parts are read (`:211`, impl `rw\compat\compatutils.cpp:164`) — version-gated structural conversions:

   | `mscVersion <` | Conversion (`CompatUtils::`) |
   |---|---|
   | 400 | `replaceStaffTextWithPlayTechniqueAnnotation` |
   | 410 | `reconstructTypeOfCustomDynamics`, `replaceOldWithNewExpressions`, `replaceOldWithNewOrnaments`, `resetRestVerticalOffset`, `splitArticulations`, `resetArticulationOffsets`, `resetStemLengthsForTwoNoteTrems`, `replaceStaffTextWithCapo` |
   | 420 | `addMissingInitKeyForTransposingInstrument`, `resetFramesExclusionFromParts` |
   | 440 | `mapHeaderFooterStyles` |
   | 450 | `convertTextLineToNoteAnchoredLine`, `convertLaissezVibArticToTie` |
   | 500 | `removeMMRestElements` |

   Helper statics also used by tag readers: `migrateOffset500`, `migrateOffsetPre302`, `setPositionStylesFromAlign` (align→position style, `ALIGN_VALS_TO_CONVERT`), `setMusicSymbolSize470`, `convertPre470FrameRadius/ImageSize`, `doMigrateNoteParens`, `readChordLinePath`, `translateToNewArticulationAnchor`.
2. **In `NotationProject::doLoad`** (`notationproject.cpp:217,232`): `EngravingCompat::doPreLayoutCompatIfNeeded` (`src\engraving\compat\engravingcompat.cpp:54`: `<400` pre-400 style compat; `<440` pedal end points / dynamic pos on vocal staves; `<460` marker font size, rest offsets, VBox distances; `<470` text compat + note parens; `<500` center-between-staves …) *before* layout, and `doPostLayoutCompatIfNeeded` (`:405`: `<300` reset positions, `<=206` reset cross-beams, `<440` cross-staff beams, `<470` lyric line visibility, `<500` offset/alignment migration …) *after* layout (may force relayout).
3. **`ProjectMigrator`** (UI-driven, `<400` only) — §6.3.

### 5.2 Style defaults by file version (`ReadStyleHook`, `rw\compat\readstyle.cpp`)
The master `score_style.mss` / `<Style>` stores only what the file says; for pre-4.0 files the **defaults** must match the *creating* version. `MscLoader::readMasterScore` (`mscloader.cpp:247`) calls `styleHook->setupDefaultStyle()` when `mscVersion < 400`: `readStyleDefaultsVersion` scans the raw score bytes for a `<defaultsVersion>` tag, falling back to `styleDefaultByMscVersion` (`readstyle.cpp:62`): `302<v<400`→302; `206<v<=301`→301; `114<v<=206`→206; `v<=114`→114; else current. `DefaultStyle::resolveStyleDefaults(v)` (`style\defaultstyle.cpp:121`) loads `src\engraving\data\styles\legacy-style-defaults-v{450,302,v3,v2,v1}.mss` (static cached) or the base style. The chosen value is stored as `Sid::defaultsVersion` (visible in the sample file: `<defaultsVersion>301</defaultsVersion>`). For ≥ 400 the entire style is in `score_style.mss`, so nothing to do. Part scores copy the master's `defaultStyleVersion` (`setupDefaultStyle(Score*)`, `:105`).

## 6. The project lifecycle (`src\project`)

### 6.1 Roles
- `INotationProject` (`project\inotationproject.h`) — `load`, `createNew`, `save(path, SaveMode, createBackup)`, `savePage`, `writeToDevice`, `canSave`, `metaInfo`, `masterNotation`, `audioSettings`, autosave flags.
- `NotationProject` (`internal\notationproject.cpp`, 1282 lines) — impl; owns `EngravingProject` (`m_engravingProject`), `MasterNotation`, `ProjectAudioSettings`.
- `EngravingProject` (`src\engraving\engravingproject.cpp`) — thin wrapper: `loadMscz` → `MscLoader`; `writeMscz` → `MscSaver`; `setupMasterScore` (padding tables, `setLayoutAll/update`, `initAutomation`, `checkCorrupted` → `sanityCheck`, `:111`).
- Registries: `INotationReadersRegister` (`NotationReadersRegister`, suffix→reader map, `notationreadersregister.cpp`) for **imports** (MusicXML, MIDI, GP, …; e.g. `musicxmlmodule.cpp:59` registers `xml/musicxml/mxl`) and `INotationWritersRegister` for **exports** + native `mscz/mscx` writers (`notationmodule.cpp:67-68`). The native `.mscz` open path does *not* go through the readers register — `NotationProject::doLoad` drives `MscReader` + `EngravingProject::loadMscz` directly.
- Scenario classes (`openprojectscenario`, `saveprojectscenario`, `closeprojectscenario`, `exportprojectscenario`, `convertfiletoscorescenario`) implement UI flows; `projectsession`, `projectcreator`, `recentfilescontroller`, `templatesrepository`, `projectautosaver`, `projectconfiguration`, `mdlmigrator`, `projectmigrator`, `mscmetareader`.

### 6.2 Load: `NotationProject::load` (`notationproject.cpp:120`)
```
load(path, openParams, format)
 ├─ setupProject()                     // new EngravingProject, MasterNotation, ProjectAudioSettings
 ├─ !isMuseScoreFile(format) → doImport()   // MusicXML/MIDI/etc via NotationReadersRegister; sets isNewlyCreated+isImported, originalFormat metaTag
 └─ doLoad(path, openParams, format):
     1. MscReader(params{filePath, mode = mscIoModeBySuffix(format)}).open()
     2. EngravingProject::loadMscz(reader, &inOutData{forcePageMode}, forceMode)
          = MscLoader::loadMscz:
              a. [container] read score_style.mss → masterScore->style()
              b. [container] read chordlist.xml (or fall back to style's chordStyle/default list)
              c. [container, !MScore::noImages] Pictures/* → imageStore
              d. readScoreFile → ReadStyleHook(defaults) → XmlReader → readMasterScore (version parse/gate/dispatch → ReadNNN::readScoreFile)
              e. [ok && mscVersion>=400 && container] for each Excerpts/*.mscx: createScore, setupDefaultStyle, read <name>.mss, readScoreFile with the SAME-version reader,
                 Excerpt::linkMeasures, name fallback (<name> tag → "partName" metaTag → filename), addExcerpt
              f. CompatUtils::doCompatibilityConversions
              g. read automation.json → AutomationData
     3. lockUpdates(true); EngravingProject::setupMasterScore(forceMode)   // layout, sanity check (forceMode ⇒ ignore corruption)
     4. migrator()->migrateEngravingProjectIfNeed()  // <400 only — dialog (§6.3); lockUpdates(false) around it (needs 2nd layout)
     5. optional style file (openParams.stylePath) → EditStyle::loadStyle
     6. EngravingCompat::doPreLayoutCompatIfNeeded → updateCapo → [unrollRepeats | setLayoutAll+update] → doPostLayoutCompatIfNeeded
     7. audio settings (audiosettings.json; on failure makeDefault + use inOutData.settingsCompat later)
     8. cloud info from metaTags (source, sourceRevisionId, audio.com url)
     9. m_masterNotation->setMasterScore(...)
    10. viewsettings.json + solo/mute for master; per excerpt from "Excerpts/<name>/" (only when mscVersion>=440 and excerpt hasFileName, else default solo/mute)
    11. apply compat solo/mute from settingsCompat
 after doLoad: treatAsImported = (mscVersion < 400 && !isCloudProject) || format == "mscz~"
        → setNeedSave(treatAsImported), isNewlyCreated = isImported = treatAsImported
```
⭐ **So opening any pre-4.0 file marks the project "imported/needs save"** — Save then prompts for a path (it's treated like a newly created doc) and, as a side effect, loads from `.mscz~` backups are treated the same way.

Error handling at the UI (`OpenProjectScenario::shouldRetryLoadAfterError`, `openprojectscenario.cpp:795`): `FileTooOld`/`FileOld300Format` → "Open anyway?" dialog (retries with `forceMode`), `FileTooNew` → warning and retry only if `configuration()->disableVersionChecking()`, `FileCorrupted` → "open anyway", `FileCriticallyCorrupted` → hard warning. Error codes: `Err::File*` = 2001–2010 (`engravingerrors.h:32-51`). ⚠ The `FileTooOld` text says "older than 2.0.0" (`engravingerrors.h:81`) but the gate is `version < 114` (1.x *is* readable) — message and code disagree.

### 6.3 Migration (`ProjectMigrator`, `internal\projectmigrator.cpp`)
- Applies **only if `mscVersion < 400`** (`migrateEngravingProjectIfNeed`, `:67-69`). Pre-migration it records the default-style version (`styleDefaultByMscVersion`) because migration makes `mscVersion` current (`:72-74`).
- `MigrationType`: `Pre_3_6` (<302) or `Ver_3_6` (302–399) (`:53`). Options come from `configuration()->migrationOptions(type)`: `isAskAgain`, `isApplyMigration`, `isApplyLeland`, `isApplyEdwin`, `isRemapPercussion`.
- `askAboutMigration` opens the sync dialog `musescore://project/migration` (`MIGRATION_DIALOG_URI`, `:49`); in builds without sync interactivity (web) it auto-accepts everything (`:111-120`).
- `migrateProject` runs inside `score->startCmd("Migrate project")…endCmd()`: **Leland** style (`:/engraving/styles/migration-306-style-Leland.mss`) and/or **Edwin** style (`…-Edwin.mss`) are loaded into every excerpt score and the master via `EditStyle::loadStyle(…, overlap=true)`; **`MdlMigrator(score).remapPercussion()`** (`mdlmigrator.cpp`) remaps MDL (marching-percussion) drums.
- It is an *undoable score edit*, not a file-format rewrite; saving afterwards writes a normal 5.00 file.

### 6.4 Save: `NotationProject::save` → `saveScore` → `doSave` (`notationproject.cpp:504,655,668`)
`SaveMode`: `Save`, `SaveAs`, `SaveCopy`, `SaveSelection`, `SavePage`, `AutoSave`.
1. `saveScore` — non-MuseScore suffix ⇒ `exportProject` (export writers register); else `mscIoModeBySuffix(suffix)`.
2. **`doSave` — crash-safe atomic-ish write:**
   - Step 1: writability check; for `Dir` mode create the container dir.
   - Step 2: write to **`<targetContainerPath>_saving`** (`savePath`): `MscWriter` (zip into an in-memory `Buffer` then `fileSystem()->writeFile`; for `Dir` mode writes members directly). Dev hook: a name containing ` - ALL_ZEROS_CORRUPTED.mscz` in dev mode writes an all-zeros buffer (`AllZerosBufferCorruptor`, `:706-714`). `msczWriter.hasError()` ⇒ fail. **Autosave** hands the serialized bytes to `Concurrent::run` to write/copy/remove off the main thread (`:735-761`, `MUSE_THREADS_SUPPORT`).
   - Step 3: optional backup (`makeBackup`, `:841`): only for `.mscz`; copies old file to `configuration()->projectBackupPath()` (hidden dir+file attribute).
   - Step 4: replace target — `Dir`: move each file out of the temp dir, delete temp; zip: `copy(savePath→target, overwrite)`, then **`checkSavedFileForCorruption`** (re-opens the zip, must yield a non-empty main `.mscx`; on failure also validates the `_saving` temp, `:812-824`; skipped for autosave) and removes the temp.
   - Final: `setPermissions` owner RW + others R.
3. `writeProject(MscWriter&, createThumbnail, ctx)` (`:894`): `EngravingProject::writeMscz` (style → score → excerpts → chordlist → images → thumbnail → automation, §1.1) → `ProjectAudioSettings::write` (→ `audiosettings.json`) → master `viewState()->write` (`viewsettings.json`) → (unless range-save) each excerpt's `viewsettings.json` + solo/mute `audiosettings.json` under `Excerpts/<fileName>/`.
4. `SaveMode::SaveSelection`/`SavePage`: build a `write::WriteRange`/`WriteContext` (selection staves/measures + `SelectionFilter`, or a page's measures) and call `writeProject(path, &ctx)` — the **range is honoured by the XML writer** (`Writer::write` loops only `[staffStart,staffEnd)`/`[measureStart,measureEnd)`, parts outside the range are skipped, excerpts are skipped: `mscsaver.cpp:91`). Asserts `path != m_path` (`:881`) — you can't range-save over the open file.
5. After success: `markAsSaved` (not for `SaveCopy`), re-layout headers/footers that contain timestamps, `m_saved.send(path, mode)`.
6. `writeToDevice(QIODevice*)` (`:616`): zip into memory then dump to a device (cloud upload path).
7. `canSave()` (`:1164`): requires parts (`NoPartsError`) and `EngravingProject::checkCorrupted()` (`MasterScore::sanityCheck`).

**Part export:** `MscSaver::exportPart(partScore, writer)` (`mscsaver.cpp:200`) writes a single part as a *standalone* `.mscz`: part style as the main `score_style.mss`, the part score as the main `.mscx`, metaTags = master tags overridden by part tags (restored afterwards), thumbnail.

**Excerpt serialization is parallel** when `MUSE_THREADS_SUPPORT`: each excerpt's style+score is serialized in a `std::async` task with a *copy* of `masterWriteOutData`, but results are added to `MscWriter` sequentially ("MscWriter is not thread-safe", `mscsaver.cpp:121-140`). `excerpt->updateFileName(index)` assigns the `Excerpts/<name>` folder name.

### 6.5 Autosave (`ProjectAutoSaver`, `internal\projectautosaver.cpp`)
- Single-shot `QTimer` (interval `autoSaveIntervalMinutes()*60000`, restart after every tick via `DEFER`, `:35-60,151`). Disabled/re-enabled via config signals.
- `onTrySave`: needs a project, `needAutoSave()`, `canSave()`; target = `projectAutoSavePath(projectPath)` = `containerPath(path) + AUTOSAVE_SUFFIX` (`:118`) — i.e. a **sidecar file next to the score** — or, for *newly-created, never-saved* projects, `configuration()->newProjectTemporaryPath()` (`:176,191`). Saves with `SaveMode::AutoSave` (no thumbnail, no backup, threaded write, no corruption check) and clears `needAutoSave`.
- On project change: a new, non-imported project is autosaved immediately (`:63-70`). `update()` removes the previous project's autosave file when the tracked path changes/clears (`:128-145`). `projectHasUnsavedChanges(path)` = autosave sidecar exists (used by `OpenProjectScenario` to offer recovery, and to delete the sidecar when the user opts out, `openprojectscenario.cpp:785-788`).
- `NotationProject::save(AutoSave)` strips `AUTOSAVE_SUFFIX` to find the real suffix; a suffix-less path means `mscx` folder (`notationproject.cpp:550-561`).

### 6.6 New / template / recent
- `createNew`: no template ⇒ fresh `MasterScore` + `setupScoreMetaTags` + `MasterNotation::setupNewScore`; sets `isNewlyCreated` and `markAsUnsaved`. With `templatePath` ⇒ `loadTemplate`: **`load(templatePath)`** (a template is just a `.mscz`), then `setPath(title)`, new metaTags (`creationDate` set from today when from template), `applyOptions(…, createdFromTemplate=true)` under `undoStack()->lock()`, and `setNeedSave(false)` (`:362-425`).
- `TemplatesRepository` (`internal\templatesrepository.*`) scans template directories (`readTemplates(dirPath)`) for the new-score dialog; `RecentFilesController` keeps the recent list and uses `MscMetaReader` for thumbnails/meta (UI detail → `ui-qml-mvvm.md`/appshell; not source-deep-passed here).

## 7. Round-trip & byte-exactness notes
- **Not byte-stable between versions by design.** The writer always emits `5.00`, drops default-valued properties, may add `<eid>` to items that lacked one (§4.3), and compat conversions can restructure content (e.g. staff text → capo/play-technique, text-line → note-anchored line, laissez-vib artic → tie). Re-saving an old file is a *migration*, and that is why `<400`/`mscz~` opens are flagged `needSave`.
- **Within one version (5.00→5.00)** the engraving tests round-trip against `*-ref.mscx` files in `src\engraving\tests\*_data\` (the `.mscx` text includes the indentation quirk — closing tags are indented one level deeper, e.g. `      </Style>` at 6 spaces, see `tuplet1-ref.mscx:9`; that is `XmlStreamWriter`'s formatting, harmless to readers).
- **Test mode vs production divergences** (`MScore::testMode`): no `programVersion/Revision`, no `<Synthesizer>`, EIDs deterministic, `Style` possibly in-file (`testWriteStyleToScore`), excerpts nested in the main file, `platform/creationDate` metaTags dropped in `saveTemplateMode`. Tests exercise a *different file shape* than production (`WriteScoreHook`, `writescorehook.cpp`, tagged "tests have not yet been adapted to the new format").
- **Unknown-tag loss:** a 5.0 file opened by a hypothetical newer-minor same-reader build would drop unknown tags on resave (§3). Across *major* mismatches `FileTooNew` blocks the open unless `disableVersionChecking`/forceMode.
- **Range saves stamp nothing:** `Writer::writeScore` does not update `m_mscVersion/m_mscoreVersion` for range saves (`writer.cpp:74`).
- **Writer mutates for layout:** `MscSaver::createThumbnail` switches the score to page mode, paints, then restores the layout mode and re-lays-out (`mscsaver.cpp:247-285`) — saving can trigger a layout.

## 8. Per-element XML tags — the `TRead` / `TWrite` handlers (deep pass 2, 2026-11-08)

Sources: `src\engraving\rw\read500\tread.cpp` (4.8k lines, `TRead::read(X*, …)` / `readProperties(X*, …)`) and `src\engraving\rw\write\twrite.cpp` (3.8k lines, `TWrite::write(const X*, …)` / `writeProperties`). Read side TRACED function-by-function for the elements below; the write side was read for every element listed. Tag names are the **literal XML strings**; where a tag is just a `Pid`, its name comes from the `propertyList` table in `src\engraving\dom\property.cpp` (`propertyName(Pid)`, :595; reverse `propertyId(name)`, :541).

### 8.1 Common mechanism

**Dispatch.** `TRead::readItem(EngravingItem*, XmlReader&, ReadContext&)` (`tread.cpp:159`) and `TWrite::writeItem` (`twrite.cpp:180`) are giant `switch (item->type())` ladders over `ElementType` calling the per-type overload; unknown types hit `UNREACHABLE` + `LOGE`. **The XML element name is the element's `typeName()`** (`XmlWriter::startElement(const EngravingObject*)`, `xmlwriter.cpp:55`) and on read it is mapped back with `TConv::fromXml(tag, ElementType)` (e.g. `tryCreateSpanner`, `tread.cpp:590`). Adding an element type means: a `TWrite::write`, a `TRead::read`, and a case in **both** switches.

**Reader shape (every element).** `while (e.readNextStartElement()) { tag = e.name(); if (known) … else e.unknown(); }`. Handlers are **layered by C++ inheritance** — each `readProperties(Derived*)` tries its own tags, then calls the base's `readProperties`, ending in `readItemProperties` (`tread.cpp:550`). Example chain: `Chord` → `ChordRest` (`:2565`) → `readItemProperties`; `Hairpin`/`Ottava`/`Volta`/`TextLine` → `TextLineBase` (`:4665`) → `SLine` (`:3690`) → `Spanner` (`:3868`) → `readItemProperties`; `Dynamic`/`Harmony`/`Marker`/`Jump`/`Lyrics`/`StaffText`… → `TextBase` (`:4592`) → `readItemProperties`. Unknown tags are skipped (`XmlReader::unknown`, §3) — never an error.

**`readItemProperties` — the tags EVERY element accepts** (`tread.cpp:550-588`): `eid`, `sizeIsSpatiumDependent`, `offset`, `minDistance`, `autoplace`, `track` (`+ ctx.trackOffset()`, used by paste), `color`, `visible`, `linkedTo`, `positionLinkedToMaster`, `appearanceLinkedToMaster`, `excludeFromParts`, `voice`, `tag` (legacy; skipped), `placement`, `z`, `parentheses`, and a nested `<Parenthesis>` child element.

**Property read pipeline** — `readProperty(item, tag, xml, ctx, Pid)` (`:494`) matches `tag == propertyName(pid)`; `readProperty(item, xml, ctx, pid)` (`:503`) does:
1. `readPropertyValue(pid, xml, ctx)` (`:363-492`) — a switch on `propertyType(pid)` (`P_TYPE`): `BOOL`=`readInt`, `INT/SIZE_T`, `REAL`, `SPATIUM`, `FRACTION`, `COLOR` (`r,g,b[,a]` attrs), `POINT` (`x`,`y` attrs), `SIZE` (`w`,`h`), `SCALE`, `STRING`, `SYMID` (SMuFL name, `TConv::fromXml(…, SymId)`), and ~35 enum types via `TConv::fromXml(text, default)` (alignment, placement, barline/notehead/clef/dynamic/line/hook/key-mode/text-style/glissando/marker/tie-placement/auto-on-off/voice-assignment…). `BEAM_MODE` returns 0 (read separately as `BeamMode`). An unhandled `P_TYPE` → `ASSERT_X("unhandled PID type")`.
2. Unit fix-ups: `P_TYPE::ABSOLUTE` (stored as spatium in XML, held as mm) → `toAbsolute(spatium)`; `POINT` is multiplied by `spatium` (or `DPMM` if `!item->offsetIsSpatiumDependent()`).
3. Compat: pre-4.4 `placement` on items with voice-assignment props → `Pid::DIRECTION` (`:523`); `Pid::OFFSET` with `mscVersion < 500` → `CompatUtils::migrateOffset500` (`:528`).
4. `if (!ctx.shouldSkipProperty(pid))` (`ReadInOutData::propertiesToSkip`) → `item->setProperty(pid, v)` and, **if the pid was styled, flip its flag to `PropertyFlags::UNSTYLED`** (`:534`). *This is how "this element overrides the style" survives a round trip: a tag present in the file ⇒ UNSTYLED.* `readStyledProperty` (`:540`) just loops `item->styledProperties()` and calls the above.

**Property write pipeline** — `TWrite::writeProperty(item, xml, pid, force=false)` (`twrite.cpp:402-453`), with the header comment *"styled properties are never written; other properties are written if forced or if different from default value"*:
- `if (item->isStyled(pid)) return;` — the symmetric half of read step 4.
- value = `getProperty(pid)`; default = `force ? invalid : propertyDefault(pid)`; equal ⇒ nothing written (`XmlWriter::tagProperty`, `xmlwriter.cpp:101`, `val == def` ⇒ return).
- `FONT_STYLE` is **split into four tags** `bold` / `italic` / `underline` / `strike`, each written only if it differs from the default (`:414-430`); the reader reassembles them (`tread.cpp:4613-4652`).
- `ABSOLUTE` → `Spatium::fromAbsolute`; `POINT` → divided by spatium (or `DPMM`) and written as `<offset x=".." y=".."/>` (`tagProperty(name, P_TYPE::POINT, …)`, `xmlwriter.cpp:169`). `BOOL` is written as `0`/`1`.
- `writeStyledProperties(item, xml)` (`:644`) = `writeProperty` for each `item->styledProperties()` entry — so it emits **only the ones the user has un-styled**. Used by Rest, Tuplet, Beam-like, Articulation, Bend, SLine, TextBase, TimeSig-style elements.
- Many handlers gate the call with `if (item->propertyFlags(pid) == PropertyFlags::NOSTYLE)` (Hairpin, Ottava, SLine, GradualTempoChange, TextLineBase, `writeItemProperties`) — properties that are *never* styled-capable are written when non-default; styled-capable ones go through `writeProperty`'s own `isStyled` check.

**`TWrite::writeItemProperties`** (`twrite.cpp:651-705`) — the common tail (most elements call it; the order within an element varies):
`<eid>` (assigns a new EID if missing — saving mutates, §4.3) → `autoplace` (forced through the style `autoplaceEnabled` flip so a per-item `autoplace=false` is not dropped when the score-wide style is off) → `<linkedTo>` (EID of the main element; skipped in clipboard mode) → `<track>` **only if** `item->track() != ctx.curTrack()` (not for Beam/Tuplet — recomputed at layout) **or** for note-anchored spanners / segment spanners in clipboard mode → `color`/`visible`/`z` only if their flag is `NOSTYLE` → `offset` if non-null → `placement` (unless the element uses voice-assignment props, which write `direction` instead) → `positionLinkedToMaster`, `appearanceLinkedToMaster`, `excludeFromParts` → `parentheses` + any non-generated `<Parenthesis>` children.

**`<tick>`/`<location>` and tick bookkeeping.** `writeProperties(const ChordRest*)` advances `ctx.incCurTick(globalTicks / timeStretch)` for non-grace chords (`twrite.cpp:1242-1248`) — writing *is* the tick walk; `<location>` is emitted by `writeSegments`/`writeVoiceMove` only when the running position differs (§4.2). `Location` element (`twrite.cpp:2349`): tags `staves`, `voices`, `measures`, `fractions`, `grace`, `notes`, `timeTick`, each omitted when equal to the relative default; read at `tread.cpp:3064`. A chord with *no writable notes* (selection filter) is replaced by a `<location>`, or — inside a tuplet — by a dummy `<Rest>` (HACK, PR #30178, `twrite.cpp:1065-1096`).

**Order sensitivity.** Reading is tag-name-driven, so most children are order-free, **except**: `Tuplet` — `bold/italic/underline/strike` must come *after* `<Number>` (comment `tread.cpp:1588`); `SlurTie`/`Spanner` `linkedTo` must reference an already-read main (hence the SpannerMap main-first rule, §4.2); `Spanner` `startElement`/`endElement` of **note-anchored** spanners are deferred to end-of-file (`ctx.addNoteAnchoredSpannerStartEl/EndEl`, `tread.cpp:3892-3918`, resolved by `ctx.connectNoteAnchoredSpanners()`, §5).

### 8.2 Element → tags tables (Score-level containers are in §4; here: everything below `<Measure>`/`<Part>`)

Notation: **R** = read handler (`tread.cpp` line), **W** = write handler (`twrite.cpp` line). *Pid* in `code` = written/read through `writeProperty`/`readProperty` (tag = `propertyName(Pid)`, shown in parentheses where I verified it in `property.cpp`). "+item" = also the §8.1 `writeItemProperties`/`readItemProperties` common tags. "+Text" = the full TextBase set (§8.2.6).

#### 8.2.1 Chord-rest family

| Element (`<tag>`) | Distinctive child tags → field/Pid | Cites |
|---|---|---|
| `Chord` | **Before the element:** grace chords (each its own `<Chord>`), then the `<Beam>` if this chord is the beam's first element and the beam is non-generated (or test mode) (`writeChordRestBeam`, W:1202). **Inside:** ChordRest tags (below); `Articulation`/`Ornament`/`Tapping` children; note-type flag tags (empty elements) `acciaccatura`, `appoggiatura`, `grace4`, `grace16`, `grace32`, `grace8after`, `grace16after`, `grace32after`; `noStem` (int) *or* `<Stem>`; `<Hook>` (only if user-modified); `showStemSlash`; `<StemSlash>`; `StemDirection` (`Pid::STEM_DIRECTION`); one `<Note>` per writable note; `<NoteParenGroup>` (`<Parenthesis>`×2 + `<Notes><NoteEID>`…) ; `<Arpeggio>`; `<TremoloSingleChord>` / `<TremoloTwoChord>` (written only on the first chord of a two-chord tremolo); `combineVoice`; then `el()` children (Fingering/Symbol/ChordLine/…, but a note-owned `ChordLine` is written by the Note). Legacy `<Tremolo>` is read via `compat::TremoloCompat`. `ChordBracket` is a chord child. | R:2445/2455 W:1055 |
| ChordRest base (Chord, Rest, MMRest, MeasureRepeat) | +item; `BeamMode` (written only if ≠ default: rest default `NONE`, chord `AUTO`); `small`; `dots`; `staffMove` (`Pid::STAFF_MOVE`); `durationType` (`TConv::toXml(DurationType)` e.g. `quarter`, `measure`); `duration` (fraction — only when ticks ≠ the type's nominal fraction, i.e. measure-rests/tuplet-scaled); `Lyrics` children. Reader also accepts legacy `pos` (point × spatium → offset). | R:2565 W:1210 |
| `Note` | +item first (so `eid`, `linkedTo`, `track` come first); then `<Accidental>`, `el()` children (Fingering/Text/Symbol/Image/Bend/…), `<NoteDot>` ×n (only if any dot has non-default offset/visibility/colour), `<LaissezVib>`, `<PartialTie>` (incoming and outgoing), `<Tie>` (back-tie, unless its start is before the clipboard range), `<Events><Event>` (play events, only if the chord's `playEventType != Auto`); then the property loop `pitch`, `centOffset`, `tpc`, `tpc2`, `small`, `mirror` (`Pid::MIRROR_HEAD`), `dotPosition`, `headScheme`, `head` (`Pid::HEAD_GROUP`), `velocity` (`USER_VELOCITY`), `play`, `tuning`, `fret`, `string`, `ghost`, `dead`, `headType`, `fixed`, `fixedLine`; then note-anchored spanners that *end* here (`spannerBack()` — `Glissando`, `GuitarBend`, `NoteLine`, `HammerOnPullOff`…), the note's `ChordLine`, `overrideBendVisibilityRules`, `hideGeneratedParentheses`. Reader also: legacy `line`, `veloType`, `offset` (→ `readItemProperties`), `Text`, `Symbol`, `Image` (skipped if `MScore::noImages`), and **any note-anchored spanner tag** via `tryCreateSpanner(tag, requireNoteAnchor=true)`; `tpc` sets *both* tpc1+tpc2 (a file with only `<tpc>` = non-transposing). After the loop `n->setupAfterRead(tick, pasteMode)`. | R:3215/3230 W:2450 |
| `Rest` | styled-but-unstyled props (`writeStyledProperties`), `alignWithOtherRests`, ChordRest tags, `el()` children, `<NoteDot>` (non-default only). **Gap rests are never written** (`isGap`, W:2800). | R:3646 W:2798 |
| `MMRest` | written as a `<Rest>` element ("for compatibility", W:2432): ChordRest tags + `mmRestNumberOffset`, `mmRestNumberVisible` + `el()`. | R:3169/3203 W:2430 |
| `MeasureRepeat` | `subtype` (`Pid::SUBTYPE`), ChordRest tags, `measureRepeatNumberOffset`, `el()`. | R:3156 W:2415 |
| `Beam` | **omitted if it has no elements** (W:887). +item, `StemDirection`, `noSlope`, `growLeft`, `growRight`, `crossStaffMove` (`BEAM_CROSS_STAFF_MOVE`); if user-modified: `<Fragment><y1/><y2/></Fragment>` (spatium units, index = direction); test-mode-only `l1`/`l2` (read: skipped). | R:1657 W:885 |
| `Tuplet` | +item, `normalNotes`, `actualNotes`, `p1`, `p2` (user points ×spatium on read), `baseNote` (duration type), `baseDots`, `<Number>` (a `<Text>`-like child: `style` + `text`), then unstyled tuplet props. Closed by a bare `<endTuplet/>` in the voice stream (`writeTupletEnd`, W:1259 — recursive for nested tuplets). Reader recomputes `ticks = baseLen × ratio.denominator`. | R:1578 W:3491 |
| `Stem` / `StemSlash` / `Hook` | written only when user-modified (Stem: `userLength`, +item). | R:4274/4296/2998 W:3169/3178/1913 |
| `Lyrics` | `no` (`Pid::VERSE`), `syllabic` (omitted if `single`), `ticks_f` (`LYRIC_TICKS`), `centerBetweenStaves`, +Text, optional `<LyricsLine>` separator. | R:3089/3130 W:2365 |
| `LyricsLine` / `PartialLyricsLine` | SLine tags (§8.2.4); Partial adds `isEndMelisma`, `no`, `centerBetweenStaves`. | R:3098/3528 W:2385/2724 |
| `Arpeggio` | `subtype` (`ARPEGGIO_TYPE`), +item, `userLen1`, `userLen2` (÷spatium), `span` (if ≠1), `play`, `timeStretch`. | R:1862/1874 W:752/781 |
| `Articulation` | `channel name=".."` (see ⚠ below), `direction`, `subtype` (text-type name **or** SMuFL symbol name; omitted for Tapping), `play`, `ornamentStyle`, unstyled styled props, +item; reader also `anchor`. | R:1946/1955 W:797/811 |
| `Ornament` (written as `<Ornament>`) | optional cue-note `<Chord>` *or* `<Accidental>` above/below; `intervalAbove`, `intervalBelow`, `ornamentShowAccidental`, `ornamentShowCueNote`, `startOnUpperNote`; then the Articulation tags. | R:1907/1916 W:830 |
| `Tapping`, `TappingHalfSlur` | Articulation-like / Slur-like (guitar); R:1987/2007 W:3306/3323. |  |
| `TremoloSingleChord` / `TremoloTwoChord` | `subtype` (`TREMOLO_TYPE`), `strokeStyle` (two-chord only), `play`, +item; two-chord user-modified adds `<Fragment><y1/><y2/>` (test mode: `l1`,`l2`). Legacy `<Tremolo>` handled by `TremoloCompat`. | R:4476/4483/4490 W:3411/3425 |
| `ChordLine` | `subtype` (`CHORD_LINE_TYPE`), `straight`, `wavy`, `play`, +item, bezier offsets; legacy `<Path>` via `CompatUtils::readChordLinePath`. | R:2614 W:1267 |
| `Fermata` | `subtype` (SMuFL symbol name), `timeStretch`, `play`, `minDistance`, +item. | R:1513/1522 W:1384 |
| `Accidental` | `bracket`, `role`, `small`, `subtype` (`ACCIDENTAL_TYPE`), `stackingOrderOffset`, +item. | R:1746 W:707 |
| `NoteDot`, `NoteHead` (→ Symbol), `NoteEvent` (`<Event>`: `pitch`, `ontime`, `len`) | | R:3381/3390/3365 W:2536/2543/2527 |
| `Breath` | `symbol`, `pause`, +item. | R:2317 W:1043 |
| `Fingering`, `Sticking`, `Expression`, `RehearsalMark`, `SystemText`, `PlayCountText`, `PlayTechAnnotation` | Text-derived: +Text (plus type-specific, e.g. PlayCountText `playCountTextSetting`/`playCountCustomText`). | R:2713/965/863/1001/970/975/988 |

⚠ **TRACED bug — Articulation channel is lost on round trip.** The writer emits the tag as **`channe`** (`twrite.cpp:814`: `xml.tag("channe", {{"name", …}})`) but `Read500` (and no other reader) reads `channel` (`tread.cpp:1967`); a grep of `src\engraving\rw` shows `"channe"` appears only in `twrite.cpp`. Net: an articulation's `channelName` written by 5.0 is dropped (logged as an unknown tag) on re-read; older files that wrote `channel` still load. Not fixed here (read-only task).

#### 8.2.2 Clefs, keys, times, bars, frames

| Element | Tags | Cites |
|---|---|---|
| `Clef` | `concertClefType`, `transposingClefType` (`ClefType` xml names), `clefToBarlinePos`, `isHeader` (when true), `isCourtesy`, `showCourtesyClef` (only when false), `forInstrumentChange`, +item. Reader sets `ClefType::G` if still `INVALID`. ⚠ The writer emits `isCourtesy` via `Pid::IS_COURTESY` but `TRead::read(Clef*)` has no `isCourtesy` branch (falls to `readItemProperties` → unknown) — HYPOTHESIS that this only matters for non-generated courtesy clefs. | R:2640 W:1281 |
| `KeySig` | +item, `concertKey` (int), `actualKey` (only if ≠ concert), `custom`, `<CustDef><sym/><def degree xAlt octAlt/></CustDef>`, `mode`, `showCourtesySig`, `isCourtesy`, `forInstrumentChange`; atonal ⇒ only `custom=1`. Legacy: `subtype` → `initFromSubtype`, `<CustDef><pos x y/>`. | R:1322 W:2270 |
| `TimeSig` | `subtype` (`TIMESIG_TYPE`), +item, `sigN`, `sigD`, `stretchN`/`stretchD` (if ≠1/1), `textN`/`textD` (numerator/denominator override strings), `<Groups><Node pos action/></Groups>`, `showCourtesySig`, `isCourtesy`, `scale`. Legacy 1.x: `den`, `nom1..nom4` (summed). | R:4409 W:3387 |
| `BarLine` | `subtype` (`BARLINE_TYPE`), `span`, `spanFromOffset`, `spanToOffset`, `el()` children, clipboard-only `playCount`, +item. | R:2030 W:864 |
| `Bracket` / `BracketItem` | Layout item `<Bracket [type="brace|square|line|group"]>` with `level` + item tags (not written for `NO_BRACKET`) vs the persisted staff-level `<BracketItem>`: `type`, `bracketSpan`, `level` (forced), `visible`, group-bracket text/show props + `<StaffLabel>`. | R:2301/4166 W:1011/2960 |
| `Spacer` | `subtype`, +item, `space` (spatium). | R:3937 W:2886 |
| `LayoutBreak` | +item, `subtype` (`LAYOUT_BREAK`), `pause`, `startWithLongNames`, `startWithMeasureOne`, `firstSystemIndentation`, `showCourtesySig`. | R:3003 W:2314 |
| `Marker` | +Text, `label`, `markerType`, `markerCenterOnSymbol`. | R:1769 W:2395 |
| `Jump` | +Text, `jumpTo`, `playUntil`, `continueAt`, `playRepeats`. | R:1797 W:2259 |
| `MeasureNumber` / `MMRestRange` | +Text only. | R:1815/1838 W:2405/2440 |
| `HBox` | `createSystemHeader` + Box props; `VBox`/`TBox`/`FBox` see Box props. **Box props:** `height`, `width`, `topGap`, `bottomGap`, `leftMargin`, `rightMargin`, `topMargin`, `bottomMargin`, `boxAutoSize`, `sizeIsSpatiumDependent` (`height` forced for VBox/FBox, `width` for HBox), VBox-base `paddingToNotationAbove/Below`, +item, `el()` children (Text/Image/Symbol…). `FBox` adds `fretFrame*` (scale/gaps/chordsPerRow/horizontalAlign); `TBox` adds nested `<Text>`. | R:2092–2164 W:935–1009 |
| `Symbol`, `FSymbol`, `Image` | Symbol: SMuFL name + BSymbol props (`W:1924` — scale/angle etc. + item); Image: BSymbol, `path` (**hash name of the `Pictures/` member**, §1.1), `autoScale`, `size`, `lockAspectRatio`, `sizeIsSpatium`. | R:2361/2425/1543 W:3231/3244/1932 |
| `Ambitus` | `head`, `headType`, `mirror`, `hasLine`, `lineWidth`, `topPitch`, `topTpc`, `bottomPitch`, `bottomTpc`, `<topAccidental><Accidental/>`, `<bottomAccidental>`, +item. | R:1693 W:726 |
| `StaffState` | `subtype`; embedded `<Instrument>` when type is instrument. | R:1417 W:2982 |
| `StaffTypeChange` | embedded `<StaffType>` + item. | R:4048 W:3159 |
| `InstrumentChange` | embedded `<Instrument>` + Text. | R:1284 W:2248 |
| `Capo` | `active`, `fretPosition`, `generateText`, `capoTransposeMode`, `<string no="n"><apply>0</apply></string>` for ignored strings, then StaffTextBase tags. | R:2669 W:1301 |
| `SoundFlag`, `SystemDivider`, `Page`, `System` | small structural elements; R:2406/1843/3439/4346 W:3283/3266/2573/3254. |  |

#### 8.2.3 Staff / Part / Instrument (in `<Part>`, written by `TWrite::write(const Part*)`)

| Element | Tags | Cites |
|---|---|---|
| `<Part id="N">` | attribute `id` (uint64 `Part::id`); child `eid`; then (in `writeProperties(Part)`) one `<Staff>` per staff in range; `show` (only when hidden), `soloist`, `hideWhenEmpty`, `hideStavesWhenIndividuallyEmpty`, `preferSharpFlat` (`sharps`/`flats`/`none`), the (single) `<Instrument>`; plus `<sharedPart>`(EID of its SharedPart). Reader legacy: `id` child, `name`, `shortName`, `color` (skipped). | R:3475/3554 W:2604/2659 |
| `<SharedPart id="N">` **(5.00)** | `eid`, `sharedPartEnabled`, `<StaveSharingState tick="n/d">` (only user-modified: `isReset`, `<trackMap origin shared/>`), then the Part tags. | R:3512 W:2640 |
| `<Staff>` (inside Part) | `eid`, `linkedTo`, clipboard `transposeDiatonic/Chromatic`, `<StaffType group="…">`, `defaultClef` *or* `defaultConcertClef`+`defaultTransposingClef`, `hideWhenEmpty`, `cutaway`, `showIfSystemEmpty`, `hideSystemBarLine`, `mergeMatchingRests`, `isStaffVisible`, `<BracketItem>`s, `barLineSpan`, `barLineSpanFrom`, `barLineSpanTo`, `distOffset`, `playbackVoice1..4`, `showMeasureNumbers`. Legacy read-only: `invisible`, `keylist`, `bracket` (attrs), `mag`, `color`. Distinguish from the score-level `<Staff id="N">` content container (§4.2). | R:4063/4072 W:2895 |
| `<StaffType group="standard|percussion|tablature">` | `name`, `<StaffLabel>`, `lines` (≠5), `lineDistance`, `yoffset`, `mag`, `small`, `stepOffset`, `clef`, `stemless`, `barlines`, `timesig`, `invisible`, `color`, `noteheadScheme`, `instrumentLabelVisibility`; standard/percussion: `keysig`, `ledgerlines`; **tablature** adds `durations`, `durationFontName/Size/Y`, `symbolRepeat`, `fretUseTextStyle`, `fretTextStyle` / (`fretPresetIdx`, `fretFontSize`, `fretFontY`), `linesThrough`, `minimStyle`, `onLines`, `showRests`, `stemsDown`, `stemsThrough`, `upsideDown`, `showTabFingering`, `useNumbers`, `showBackTied`. | R:3951 W:3068 |
| `<Instrument id="…">` | `soundId`, `<InstrumentLabel>`, `trackName`, `minPitchP/maxPitchP/minPitchA/maxPitchA`, `transposeDiatonic`, `transposeChromatic`, `instrumentId` (MusicXML id), `useDrumset` + `<Drumset>`, `clef` / `concertClef`+`transposingClef` (attribute `staff="i+1"` for staff i>0), `singleNoteDynamics`, `glissandoStyle`, `<StringData>`, `<MidiAction>` (NamedEventList), `<Articulation>` (MidiArticulation), `<Channel>` ×n (`InstrChannel`: name attr, controllers, `program`, `note-on/off` midi events, `synti`, mute/solo/etc.). **Long-tail:** InstrChannel/Drumset/StringData inner tags are not enumerated here (W:2067-2250, R:1139-1260). | R:1006/1039 W:1947 |

#### 8.2.4 Spanner family

Common spanner tail — `writeProperties(const Spanner*)` (W:1652): `play`; if `anchor == SEGMENT`: `track2`, `startTick`, `ticks` (fractions; `startTick` has a special `-1/0` default so tick 0 is writable); else (CHORDREST / MEASURE / NOTE anchors, **except GuitarBendHold**, which is regenerated at layout): `startElement` / `endElement` = **EIDs** of the anchor elements (asserting they exist unless palette/PartialTie/LaissezVib); then +item. Read: `track2 (+trackOffset)`, `startTick (+tickOffset)`, `ticks`, `startElement`, `endElement` (paste-mode EIDs are remapped via `ctx.resolvePastedEID`), `play`, then `readItemProperties` (R:3868).
`SLine::writeProperties` (W:1615): Spanner tail, `diagonal`, unstyled `lineWidth`/`lineStyle`/`dashLineLength`/`dashGapLength`, `writeStyledProperties`, and — **only if the line is user-modified** — one `<Segment><subtype/><offset/><off2/><minDistance/>…</Segment>` per `SpannerSegment`. Read `Segment` tag: `createLineSegment` (R:3690-3700); plus `lineColor`/`color`.

| Element | Tags (beyond the common spanner/SLine tail) | Cites |
|---|---|---|
| `Hairpin` | `subtype` (`int(hairpinType)`), then NOSTYLE-gated: `veloChange`, `hairpinCircledTip`, `singleNoteDynamics`, `veloChangeMethod`, `voiceAssignment`, `direction`, `centerBetweenStaves`, `snapBefore`, `snapAfter`, `hairpinHeight`, `hairpinContHeight`, then TextLineBase tail. | R:2852 W:1724 |
| `Ottava` | `subtype` (`OTTAVA_TYPE`), `numbersOnly`, TextLineBase tail. | R:3400/3414 W:2558 |
| `Pedal`, `TextLine`, `LetRing`, `PalmMute`, `Volta`, `WhammyBar`, `NoteLine`, `GradualTempoChange`, `Rasgueado`, `Vibrato`, `Trill` | TextLineBase/SLine tails. `TextLine` adds attr `system="1"` when `systemFlag`. `Volta` adds `endings` (comma int list, W:3533). `Vibrato`: `subtype`. `Trill`: `subtype` (named), `ornamentStyle`, embedded `<Ornament>`. `GradualTempoChange`: `tempoChangeType`, `tempoEasingMethod`, `tempoChangeFactor`, `snapAfter`. | R:3609/4376/3052/3452/4722/3400/4708/4676/2753 W:2740/3365/2339/2582/3529/3537/2548/1689/3518/3472 |
| `TextLineBase` tail | `Pid`s in `TextLineBase::textLineBasePropertyIds()` when NOSTYLE — the `beginText*`/`continueText*`/`endText*` (text, align, position, place, font face/size/style, musical-symbol scale), `begin/endHookType`, `begin/endHookHeight`, line-arrow/filled-arrow heights/widths, `gapBetweenTextAndLine`, `lineVisible`, `lineStyle`… (exact list = the `Pid`s named in `property.cpp`, Appearance group, lines ~118-152 of the table). | R:4665 W:1704 |
| `Slur` | `partialSpannerDirection`, SlurTie tail. | R:3714/3723 W:2839 |
| `Tie` / `LaissezVib` / `PartialTie` | `tiePlacement` (+ `minLength` for LV, `partialSpannerDirection` for PartialTie), SlurTie tail. | R:4398/1405/3542 W:3379/2305/2715 |
| `HammerOnPullOff` | `partialSpannerDirection` + Slur tail; per-segment `<HammerOnPullOffText idx="i">` Text children (only user-modified). | R:2883 W:1752/1767/1778 |
| SlurTie tail | Spanner tail; `<SlurSegment no="i">` / `<TieSegment>` / `<LaissezVibSegment>` / `<PartialTieSegment>` / `<HammerOnPullOffSegment>` / `<TappingHalfSlurSegment>` (only user-modified: `o1..o4` bezier offsets (`Pid::BEZIER_OFFSET1..4`), +item); `up` (`SLUR_DIRECTION`), `lineType`, `maskSlurTie`. The segment element name is the *segment's* `typeName()`; the reader recognises all six by name (R:3751). | R:3734–3782 W:2853/2865 |
| `Glissando` | `text`, `isHarpGliss` (clipboard only), `glissandoShift`, `easeInSpin`, `easeOutSpin`, SLine tail; type via `subtype`. | R:2722 W:1545 |
| `GuitarBend` (+ `GuitarBendHold`) | `guitarBendType`, `bendStartTimeFactor`, `bendEndTimeFactor`, `bendTargetTimeFactor`, `direction`, `bendShowHoldLine`, dive-only `guitarDiveTabPos`/`vibratoLineType`/`guitarDiveIsSlack`/`guitarBendAmount`; SLine tail; nested `<GuitarBendHold>` (its own SLine tail). Segment extra: `bendVertexOff`, `<GuitarBendText>`. | R:2799/2826/2837 W:1566/1603 |

#### 8.2.5 Annotations & text

**TextBase set** — `writeProperties(const TextBase*, writeText)` (W:1356-1382): if voice-assignment-capable: `voiceAssignment`, `direction`, `centerBetweenStaves`; `style` (`TEXT_STYLE`); +item; `writeStyledProperties`; then every pid of that text style's `textStyle()` list that is **neither styled nor the undefined sentinels** (`UNDEFINED_FONT_SIZE`/`FAMILY`); `<text>` (rich-text XML via `xml.writeXml`, newlines → `<br/>`; **omitted when `writeText=false`**, e.g. `Dynamic` without custom text); `textLinkedToMaster`. Read (R:4592): loops `TextBasePropertyId` first (with the pre-4.7 `align`→`position` migration for non-Marker/Jump/Harmony), then `text` (`lineBreakFromTag`, `checkCustomFormatting`), `bold`/`italic`/`underline`/`strike`, `textLinkedToMaster`, `voiceAssignment`, `direction`, `centerBetweenStaves`, `musicSymbolSize`, `musicalSymbolsScale`, then `readItemProperties`. `Text`, `StaffText` (+`<SoundFlag>`), `SystemText`, `RehearsalMark`, `Sticking`, `Expression`, `MeasureNumber`, `MMRestRange`, `Fingering`, `StaveSharingLabel` use only this (StaffText family adds the block below).

| Element | Distinctive tags | Cites |
|---|---|---|
| `StaffTextBase` family (`StaffText`, `Capo`, `StaveSharingLabel`, `SystemText`…) | `<MidiAction channel name/>`, `<channelSwitch voice name/>`, `<aeolus group>`, `<swing unit ratio/>`, then TextBase. | R:744/770 W:3034 |
| `Dynamic` | `subtype` (`DYNAMIC_TYPE`), `velocity`, `avoidBarLines`, `centerOnNotehead`, `play`, `anchorToEndOfPrevious`, `veloChange`/`veloChangeSpeed` (if velocity-change available), TextBase (text only if custom). | R:838 W:1324 |
| `TempoText` | `play`, `tempo` (quarter-notes/sec, fixed precision `TEMPO_PRECISION`), `followText`, `type` (`aTempo`/`tempoPrimo`), TextBase. | R:677 W:3333 |
| `Harmony` | `harmonyType`, `play`, `rootCase`/`bassCase` (only if ≠ capital), one `<harmonyInfo>` per chord symbol (`extension`, `name`, `root`, `bass` as **TPC ints**; leading `=` marks minor), `<degree><degree-value/><degree-alter/><degree-type/></degree>`, `harmonyDoNotStackModifiers`, `excludeVerticalAlign`, then TextBase (text omitted; voicing/duration/literal are text-style pids). Skipped if `chords().empty()` (W:1830). Read calls `h->afterRead()`. | R:2910 W:1828 |
| `FretDiagram` | W:1464 (string/dot/barre/marker/fingering substructure) — **long-tail, not enumerated**. | R:875 |
| `FiguredBass` / `FiguredBassItem` | `ticks`, `<FiguredBassItem><prefix/><digit/><suffix/><continuationLine/>` + parenthesis structure. | R:1433/1467 W:1399/1428 |
| `Bend`, `TremoloBar` | `<point time pitch vibrato/>` list; Bend: styled props + `play`; TremoloBar: `mag`, `lineWidth`, `play`. | R:2071/942 W:923/3459 |
| `HarpPedalDiagram`, `StringTunings`, `BagpipeEmbellishment`, `HarmonicMark`, `PickScrape`, `Image`… | long-tail; handlers at R:2974/4327/2018/2969 W:1894/3215/857/1884/2750. |  |

#### 8.2.6 Segments / ids

`<Segment>` (`TRead::read(Segment*)`, R:3663): only `leadingSpace` (and a skipped `subtype`); written **only** when `extraLeadingSpace` is non-zero, and the writer's `written()` flag stops duplicate output (W:2825). `<eid>` is on every item including Score/Part/Staff/Measure. EID reader/writer: R:629 / W:585; link: R:648 / W:603.

### 8.3 Reading the remainder (what is NOT enumerated)
Everything not shown above follows one of the three skeletons in §8.1 (`startElement(item)` → `writeProperty` list → `writeItemProperties` → children → `endElement`; and the symmetrical reader loop). The precise unshown remainder, each a short self-contained handler you can read directly: `FretDiagram` body (R:875 / W:1464), `HarpPedalDiagram` (R:2974 / W:1894), `StringTunings`/`StringData` (R:4305-4345 / W:3195-3231), `InstrChannel` + `MidiArticulation` + `NamedEventList` + `Drumset` inner tags (R:1139-1283 / W:2067-2250), `Instrument`'s `readProperties` tail (R:1039), `BagpipeEmbellishment`, `HarmonicMark`, `PickScrape`, `Rasgueado`, `Ornament` cue-note details, `System`/`Page` (layout-time containers, rarely persisted), and the per-tag `Pid` *names* for the Appearance/Text groups (see `property.cpp` table). Measure/Staff/voice-level tags (`startRepeat`, `endRepeat`, `irregular`, `noOffset`, `measureNumberMode`, `breakMultiMeasureRest`, `stretch`, `vspacerUp/Down/Fixed`, `visible`, `stemless`, `hideIfEmpty`, `measureRepeatCount`, `multiMeasureRest`, `mmRestLast`) are in `measurewrite.cpp` / `measureread.cpp` (§4.2) — not in `tread/twrite`.

### 8.4 Mechanics worth remembering (editing the format)
- **Add a persisted property to an existing element:** add a `Pid` row to `property.cpp` (the `static_assert(isPropertyListConsistent())` at :535 enforces table/enum sync), add `writeProperty(item, xml, Pid::X)` to the element's `TWrite::write` and `readProperty(…, Pid::X)` (or a literal-tag branch) to its `TRead::readProperties`. Because unknown tags are skipped (§3), old readers silently ignore it; because the writer omits default/styled values, old files stay byte-small. Whether a version bump is needed is policy (§Gotchas "bump discipline"), not mechanical.
- **Reader/writer asymmetry is the main bug class** (the `channe` typo above; the Clef `isCourtesy` question). When touching a tag, grep **both** `tread.cpp` and `twrite.cpp` for the literal.
- **Property tag ≠ field name.** Examples: `Pid::TIMESIG_TYPE`/`BARLINE_TYPE`/`ACCIDENTAL_TYPE`/`OTTAVA_TYPE`/`DYNAMIC_TYPE`/`GLISS_TYPE`/`LAYOUT_BREAK`/`CHORD_LINE_TYPE`/`TREMOLO_TYPE` are **all** `subtype`; `SLUR_DIRECTION` = `up`; `MIRROR_HEAD` = `mirror`; `HEAD_GROUP` = `head`; `USER_VELOCITY`/`VELOCITY` = `velocity`; `TPC1` = `tpc`; `FONT_FACE` = `family`; `FONT_SIZE` = `size`; `STEM_DIRECTION` = `StemDirection`; `SHOW_COURTESY` = `showCourtesySig`; `NUMERATOR_STRING` = `textN`; `LYRIC_TICKS` = `ticks_f`; `VERSE` = `no`; `STAFF_USERDIST` = `distOffset`; `STAFF_BARLINE_SPAN` = `barLineSpan`; `BARLINE_SPAN` = `span`; `BARLINE_SPAN_FROM/TO` = `spanFromOffset/spanToOffset`; `HAS_PARENTHESES` = `parentheses`; `EXCLUDE_FROM_OTHER_PARTS` = `excludeFromParts`.
- **Spatium/absolute handling** lives only in `readProperty`/`writeProperty` and in handlers that call `xml.tag(…/spatium)` manually (e.g. `LedgerLine`, `Beam::Fragment`, `Arpeggio::userLen*`, `Segment::leadingSpace`) — a handler that reads a raw `readDouble()` must multiply by `spatium` itself.

## Cross-subsystem dependencies
- `engraving-dom.md` — element semantics behind every tag; `TRead`/`TWrite` are the serialization of `EngravingItem` properties (`Pid`/`PropertyValue`, `propertyName(Pid)`).
- `style-system.md` — `MStyle::read/write/save`, `Sid::defaultsVersion`, legacy defaults `.mss` files, `DefaultStyle`.
- `notation-layer.md` — `MasterNotation`, undo stack interaction (`listenIfNeedSaveChanges` ties `needSave` to undo-stack cleanliness, `notationproject.cpp:1066`), `MscNotationWriter`, view state/solo-mute JSON.
- `importexport.md` — non-native formats go through `INotationReadersRegister`/`INotationWritersRegister` (`doImport`/`exportProject`), not `rw`. `converter` (CLI) → `mscore-cli.md` drives `NotationProject::load/save`.
- `playback-and-audio.md` — `audiosettings.json`, `automation.json`.
- `framework-services.md` — `muse::ZipReader/ZipWriter` (`serialization\`), `IFileSystem`, `Concurrent`.
- `testing-and-vtest.md` — compat tests (`compat114/206`, `*_data\*-ref.mscx`), `vtest`.
- `plugin-api.md` — `engravingpluginapihelper` in `src\project\internal` (plugin-driven project ops).

## Non-obvious gotchas
- ⛔ **Changing the written XML = bump discipline.** There is *no* automatic version-bump check: `MSC_VERSION` (`constants.h:31`) is manually bumped. To change the format: bump `MSC_VERSION`/`MSC_VERSION_STR`; copy the previous reader dir to a new `readNNN` (as 410→460→500 did), add a `version < NNN` rung in `RWRegister::reader`, add `doCompatibilityConversions` / `EngravingCompat` rungs keyed on `< NNN`; keep `MStyle::write` in sync. Editing `read114–read400` affects *all* of 1.x–4.0 (shared `read400` bodies).
- ⛔ **`read400` is shared by 114/206/302** — a "harmless" change in `read400\tread.cpp` alters 1.x/2.x/3.x imports. Conversely `read410` is *not* shared and may be pruned.
- ⚠ Only version `300` is rejected as the "dev 3.0" format (`mscloader.cpp:66`); 301+ (and 115..207) are routed by the `RWRegister` ladder, with no gate for odd in-between values.
- ⚠ Version parsing (`sl[0]`, `sl[1]`) has no length/`ok` guard (`mscloader.cpp:230-231`); a malformed `version` attribute can index out of range. UNVERIFIED whether `StringList::operator[]` is bounds-checked.
- ⚠ `ReadStyleHook::readStyleDefaultsVersion` **re-parses the entire score bytes** just to find `<defaultsVersion>` (`readstyle.cpp:42-55`) — an extra full XML scan on every pre-4.0 open.
- ⚠ `DirWriter::open` wipes the directory (`removeRecursively`, `mscwriter.cpp:331`). Never point `MscWriter(Dir)` at a user folder directly — go through `NotationProject::doSave`'s `_saving` temp dir.
- ⚠ Writing assigns EIDs (`writeItemEid`) — a "pure" save mutates the score; with the undo stack this is not undoable but flips nothing visible.
- ⚠ `XmlFileWriter` silently drops non-`mscx/json/mss` members (`mscwriter.cpp:440`) — an `.mscs` is **lossy** (no chordlist `.xml`, no images, no thumbnail).
- ⚠ Excerpt reading uses `makeReader(masterScore->mscVersion(), …)` (`mscloader.cpp:180`) — **parts are assumed to be the same version as the master**; excerpts are only read for `mscVersion >= 400` and only for containers (`:157`). Pre-4.0 parts live nested in the master `.mscx` as inner `<Score>` (read by `Read302`'s recursion).
- ⚠ Part export / range save skip excerpts and don't write per-excerpt json (`mscsaver.cpp:91`, `notationproject.cpp:922`).
- ⚠ A `mscz` temp name is literally `<containerPath>_saving` — a crash mid-save can leave `foo.mscz_saving` beside the score; `checkSavedFileForCorruption` validates both.
- The `FileTooOld` user message ("older than 2.0.0") contradicts the gate (`<114`) — cosmetic bug (`engravingerrors.h:81` vs `mscloader.cpp:62`).

## UNCLEAR — needs verification
- **Links in `read500`**: confirm (trace `read500\readcontext.*` + `TRead::readItemLink`) that EID-based linking fully replaced `ReadLinks`/`LinksIndexer` for ≥5.00 files, and that `data->links` being left empty for the master is intentional for excerpt loading (`mscloader.cpp:178`).
- **MM-rest write/read symmetry in 5.0**: writer still emits generated MM-rest measures (`staffwrite.cpp:50`), reader still builds them (`staffread.cpp:64-78`), yet `removeMMRestElements` runs for `<500` (`compatutils.cpp:199`). Needs a trace of `removeMMRestElements` and `Measure::mmRest` ownership to state the 5.0 invariant.
- **`TRead` per-element tag tables:** now covered in §8 (mechanism + category tables). Only the long-tail listed in §8.3 (FretDiagram, HarpPedalDiagram, StringData/Tunings, InstrChannel/Drumset inner tags, a few small elements) remains un-tabulated; `Read400/410/460` per-element differences vs `Read500` were not diffed.
- **Clef `isCourtesy`** (§8.2.2): writer emits it, `TRead::read(Clef*)` has no branch — confirm whether it is harmless (generated courtesy clefs not persisted) or a real loss.
- **`defaultsVersion` for 4.x files** (`LEGACY_MSC_VERSION_V450` style file exists, `defaultstyle.cpp:133`) — what writes `450` defaults and when it is selected for ≥400 files was not traced (the `styleDefaultByMscVersion` path returns `MSC_VERSION` for ≥400).
- **Cloud/legacy-cloud project persistence** (`isLegacyCloudProject`, `SOURCE_TAG`, `m_cloudInfo`) and `RecentFilesController`/`TemplatesRepository` internals only skimmed.
- **MuseScore 5.0 `main` vs release file version**: 5.00 is the *dev-line* format; whether it will be bumped before release is a product decision — unknowable from source.
- `tools\ziprw` is a dev helper; no CMake/CI consumer traced.

*Authored from (2026-11-08): `src\engraving\rw\{README.md,rwregister.*,mscloader.*,mscsaver.*,ireader.h,iwriter.h,inoutdata.h,xmlreader.*,xmlwriter.*,read{114,206,302,400,500}\*,read460\read460.cpp (diffed vs read500),compat\*,write\{writer,staffwrite,measurewrite,twrite(partial),writecontext}*}`, `src\engraving\infrastructure\{mscio.h,mscreader.*,mscwriter.*,eid.h}`, `src\engraving\{engravingproject.cpp,engravingerrors.h,compat\*,style\{style,defaultstyle}.cpp}`, `src\project\{inotationproject.h, internal\{notationproject,projectmigrator,projectautosaver,mscmetareader,notationreadersregister,openprojectscenario(part)}}.cpp`, `src\notation\internal\mscnotationwriter.cpp`, `tools\ziprw\main.cpp`, a 5.00 test `.mscx`. Pass 2 additionally read: `read500\tread.cpp` (dispatch, property pipeline, Chord/ChordRest/Note/Tuplet/Beam/KeySig/TimeSig/Clef/Capo/Staff/Part/SharedPart/Harmony/Articulation/Spanner/SLine/SlurTie/TextBase), `write\twrite.cpp` (dispatch, `writeProperty`, `writeItemProperties`, and all element writers cited in §8), `rw\xmlwriter.cpp` `tagProperty`, `dom\property.cpp` (`propertyName`/`propertyId`, selected rows). Not exhaustively read: `read114/206/400/410/460` element bodies, remaining `tread/twrite` long-tail (§8.3), `mdlmigrator`, `projectsession`, `recentfilescontroller`. **If this contradicts the live source, the source wins.***
