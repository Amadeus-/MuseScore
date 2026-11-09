# MuseScore — Engraving DOM (the score object model)

> **What this file is.** Scope: `src\engraving\dom\` — the in-memory score object model: `EngravingObject`/`EngravingItem`, the property system, the score spine (`Score` → `Measure` → `Segment` → `ChordRest`/`Note`), spanners, text, `Staff`/`Part`, and the ~150-class `ElementType` inventory. This is the heart of the engraving module; layout/painting (`layout-and-rendering.md`), style (`style-system.md`), and read/write (`file-format-and-rw.md`) act **on** this model but live elsewhere. It also covers the **editing/undo framework** (`src\engraving\editing\`, §3A). **Live source wins** — re-Read/Grep before asserting a member or editing. Start at `00-core.md`.
>
> **Status: ✅ complete at header level (polish pass 2026-11-08).** Spine + property system + 155-type inventory (§1-§3); per-element member tables for rhythm/beaming, articulations/ornaments, concrete spanners, bar/clef/key/time, harmony/figured-bass/fret, instruments/staff/part, system/page/intermediate bases, and `TextBase` rich text (§2.7-§2.14); **every remaining concrete element class — lyrics, arpeggio, tremolos, fermata, accidental, frames, guitar bend/tapping/hopo and guitar lines, expression/tempo/marker/jump, symbols/images, legacy bend, annotations (staff/system/rehearsal/play-tech/capo/…), fingering/sticking/breath/ambitus/chord line/dots/ledger lines, layout breaks/spacer/staff state, measure-repeat/MMRest, brackets, indicators, anchors, editor-only items — has a member table or explicit note in §2.15**; complete `P_TYPE`↔storage table (§2.15.16); the transaction/undo system (§3A). Remaining unresolved items in §6 are `.cpp`-behaviour traces, not missing tables.

## 0. 60-second orientation

- **Location:** `src\engraving\dom\` (≈179 `.h` + matching `.cpp`; `dom.cmake` lists the sources — roughly one `.h/.cpp` pair per class). The enums/value-types it depends on are in `src\engraving\types\` (`types.h`, `propertyvalue.h`, `fraction.h`, `symid.h`, `pitchvalue.h`, `spatium.h`).
- **The five files that matter most:**
  - `engravingobject.h` — `EngravingObject`, the root of the tree: type tag, ownership parent/children, links, the pure-virtual property interface, and the giant `is*()`/`to*()` type-check/cast macro blocks.
  - `engravingitem.h` — `EngravingItem`, the base of everything **visible/layoutable**: geometry (`LayoutData`), flags, track/staff/voice, color, placement, drag/edit, the concrete `getProperty/setProperty`.
  - `property.h` + `types/propertyvalue.h` — the **property system**: `Pid` (≈470 property ids), `PropertyValue` (a tagged variant), `P_TYPE`, `PropertyFlags`.
  - `types/types.h` — the authoritative **`ElementType`** enum (155 enumerators) that tags every object; also `track_idx_t`/`staff_idx_t`/`voice_idx_t` aliases and dozens of domain enums.
  - `score.h` / `masterscore.h` — `Score` (the container; note it derives from `EngravingObject`, **not** `EngravingItem`) and `MasterScore` (owns undo stack, sigmap, tempomap, repeat list).
- **The spine (ownership tree):** `MasterScore` → `MeasureBaseList` of `MeasureBase` (→ `Measure` or a box) → `SegmentList` of `Segment` → one `EngravingItem*` per **track** (`ChordRest` = `Chord`/`Rest`/`MMRest`, or a `Clef`/`KeySig`/`TimeSig`/`BarLine`…) → `Chord` owns `Note`s, `Stem`, `Hook`, `Beam`, articulations.
- ⭐ **Two orthogonal hierarchies.** (1) *C++ inheritance* — `EngravingObject` → `EngravingItem` → … (what methods an object has). (2) *Ownership/containment* — `parent()`/`children()` (where an object sits in the score). They do **not** coincide: e.g. a `Measure` is **owned** by its `Score` but **laid out on** a `System` (`layoutParent()` ≠ owner).
- **Creation** is centralised in `factory.h` (`Factory::createItem(ElementType, parent)` and per-type `createXxx`/`makeXxx`). You almost never `new` a DOM item directly.

---

## 1. Mechanism — the object model

### 1.1 `EngravingObject`: the tree node (`engravingobject.h:254`)

Every score object is an `EngravingObject`. It carries:

- **A type tag** `m_type` (`ElementType`), read via `type()` / `isType(t)` (`engravingobject.h:262`). This is how RTTI is done throughout engraving — not `dynamic_cast` but the `is*()` helpers built on `type()`.
- **An ownership parent + child list** (`engravingobject.h:275-285`, `357-360`):
  ```cpp
  EngravingObject* parent() const;            // raw tree link: real parent, or the dummy it's parked on
  EngravingObject* ownershipParent() const;   // the parent it's *attached* to; null while parked on the dummy
  const EngravingObjectList& children() const;// EngravingObjectList = std::vector<EngravingObject*>
  ```
  ⭐ **The dummy-parent idiom.** A detached object is not left with a null parent; it is parked on a per-score `DummyParent` (`score()->dummy()`, `score.h:306`). `parent()` returns that dummy; `ownershipParent()` reports "attached to nothing" as `nullptr`. The `DummyParentOr<...>` template (`engravingobject.h:225`) is the constructor-parameter type that forces call sites to say "real parent of type X **or** the dummy" rather than slipping in a wrong pointer. `addChild`/`removeChild` (protected, `:343`) maintain the child vector; `moveToDummy()` detaches.
- **Context:** `score()` / `masterScore()` (`:291-292`), `setScore()` (virtual, pushes score down a subtree), `style()` (`:295`, returns the owning score's `MStyle`), `isInScoreTree()`.
- **Traversal:** `scanElements(std::function<void(EngravingItem*)>)` (`:287`) — the universal visitor. The base is a no-op; containers override it to recurse (e.g. `EngravingItem::scanElements` visits `this`; `Measure`/`Segment`/`Chord` recurse into children). This, plus `Score::scanElements`, is how layout, painting, and "apply to every element" operations iterate the score.
- **Links** (`:327-335`, and `linkedobjects.h`): an object can be *linked* to clones of itself (linked staves, the score↔parts relationship, linked tabs). `linkList()` returns all peers; `links()` is the shared `LinkedObjects` list (a `std::list<EngravingObject*>`); `linkTo()`/`unlink()` manage membership; `LinkedObjects::mainElement()` is the canonical master. Property changes propagate across this list (see §1.3).

### 1.2 `EngravingItem`: the visible element (`engravingitem.h:147`)

`EngravingItem : public EngravingObject` is the base of every element that has a position and can be drawn, selected, or edited. Key surfaces:

| Area | Members (selected) | Notes |
|---|---|---|
| Identity/clone | `clone()` (pure virtual, `:159`), `linkedClone()` | every concrete item implements `clone()`. |
| Geometry | `pos()/x()/y()`, `offset()/setOffset()`, `pagePos()/canvasPos()`, `width()/height()`, `shape()`, `bbox()` via `ldata()` | see `LayoutData` below. |
| Layout data | `ldata()` / `mutldata()` (`:646`), nested `struct LayoutData` (`:518`) | **computed layout lives in a separate `LayoutData` object**, not in the item's logical fields — a deliberate split so the DOM holds "what", layout holds "where". `ld_field<T>` tracks whether a value was computed (`LD_ACCESS`). |
| Flags | `ElementFlags m_flags` (`:744`); `ElementFlag` enum (`:88`) | bit-flags: `INVISIBLE`, `SELECTED`, `GENERATED`, `MOVABLE`, `ON_STAFF`, `NO_AUTOPLACE`, `SYSTEM`, measure/segment flags (`REPEAT_END`, `ENABLED`, `EMPTY`…). Accessed via `visible()`, `selected()`, `generated()`, `autoplace()`, etc. |
| Position in score | `track()`/`setTrack()` (`:326`), `staffIdx()`, `voice()`, `staff()`, `part()`, `tick()`/`rtick()` (`:258`) | `m_track = staffIdx * VOICES + voice`. |
| Placement | `placement()`/`setPlacement()` (`PlacementV::ABOVE/BELOW`), `placeAbove()` | stored in the `PLACE_ABOVE` flag. |
| Appearance | `color()`/`setColor()`, `z()` (stacking), `mag()`/`magS()`, `spatium()` | `spatium()` is the staff-space unit; most sizes are spatium-relative. |
| Property interface | `getProperty`/`setProperty`/`propertyDefault` **concrete** here (`:438`) | implements the `EngravingObject` pure virtuals for the common properties; subclasses extend. |
| Interaction | `acceptDrop`/`drop` (`:380-391`), `startDrag`/`drag`/`endDrag`, `startEdit`/`edit`/`endEdit`, grips | drag/drop/edit all take `EditData`; `drop` takes a `Transaction&`. |
| Navigation | `nextElement`/`prevElement`, `nextSegmentElement`/`prevSegmentElement` | drive keyboard navigation & accessibility. |
| Linked-to-master | `isPositionLinkedToMaster` etc. (the `M_PROPERTY2` trio at `:149`) | per-item control of what propagates score↔part. |

The `M_PROPERTY`/`M_PROPERTY2` macros (`property.h:45`) generate `field + getter + setter` in one line — you'll see them at the top of many class bodies.

### 1.3 The PROPERTY system (central — read this)

Almost everything settable on an element is reachable **generically** through a `(Pid, PropertyValue)` interface, in addition to the typed accessors. This is what the Inspector UI, undo stack, read/write, and the plugin API all use.

- **`Pid`** (`property.h:71`) — an `enum class : short` of **≈470** property ids (`SUBTYPE`, `COLOR`, `VISIBLE`, `PITCH`, `TPC1`, `LINE`, `OFFSET`, `PLACEMENT`, `BEAM_MODE`, `DURATION`, `TEXT`, `FONT_FACE`, … ending at `END`). Each `Pid` names one logical property, shared across all element types that have it.
- **`PropertyValue`** (`types/propertyvalue.h:41`+) — a **tagged variant** carrying any property's value plus a `P_TYPE` discriminator (`P_TYPE`: `BOOL`, `INT`, `REAL`, `STRING`, `POINT`, `SPATIUM`, `FRACTION`, `COLOR`, `SYMID`, `DIRECTION_V`, `PLACEMENT_V`, `BARLINE_TYPE`, … `property.h`/`propertyvalue.h`). Replaces the old `QVariant` usage; convertible to/from `QVariant` when Qt is present.
- **The virtual interface** (on `EngravingObject`, `engravingobject.h:297-301`):
  ```cpp
  virtual PropertyValue getProperty(Pid) const = 0;
  virtual bool          setProperty(Pid, const PropertyValue&) = 0;
  virtual PropertyValue propertyDefault(Pid) const;
  virtual void          resetProperty(Pid id);
  virtual EngravingObject* propertyDelegate(Pid) const; // forward a Pid to another object
  ```
  `EngravingItem` provides concrete implementations for the common properties (`engravingitem.h:438`); each subclass overrides, handles its own `Pid`s in a `switch`, and falls through to its base for the rest. `propertyDelegate` lets a segment-owning spanner, say, forward a property to its segments.
- **Styled properties.** A property can be *styled* (its default comes from the score style, `Sid`). `styledProperties()` returns the `ElementStyle*` (a `Pid`↔`Sid` table), `initElementStyle()` installs it, `isStyled(pid)` / `getPropertyStyle(pid)` / `styleValue(pid, sid)` query it, and `PropertyFlags` (`property.h:63`: `NOSTYLE`/`UNSTYLED`/`STYLED`) record, per item per property, whether the value is still tracking the style or has been overridden. See `style-system.md`.
- **Undo-aware mutation.** `undoChangeProperty(Pid, value, flags)` (`engravingobject.h:321`) routes a change through the undo stack (and across the link list) instead of setting the field directly — this is the normal way to change a property at runtime. `undoResetProperty`, `undoPushProperty` likewise.
- **Propagation score↔parts.** `PropertyPropagation` (`property.h:534`: `NONE`/`PROPAGATE`/`UNLINK`) and `PropertyGroup` (`POSITION`/`TEXT`/`APPEARANCE`/`NONE`) decide, per property, whether a change in the master score flows to the linked part (and vice-versa). `propertyLink(Pid)`, `propertyGroup(Pid)`, `EngravingItem::propertyPropagation()` implement it.
- **Free functions** (`property.h:550-558`): `propertyType(Pid)`, `propertyName(Pid)`, `propertyFromString`/`propertyToString` (used by read/write), `propertyId(name)`, `propertyUserName(Pid)`.

**To change an element's property at runtime:** prefer `item->undoChangeProperty(Pid::X, PropertyValue(v))` (undoable, propagates to links/parts). To read: `item->getProperty(Pid::X)`. Typed accessors (`note->setPitch`, `chord->setStemDirection`) exist for convenience and are what `set/getProperty` dispatch to internally.

### 1.4 Tracks, voices, staves (the coordinate system)

- **Aliases** (`types/types.h`): `staff_idx_t`, `track_idx_t`, `voice_idx_t`, `page_idx_t` (all `size_t`); the "no index" sentinel is `muse::nidx`.
- **`VOICES`** = 4. A **track** indexes (staff, voice): `track = staffIdx * VOICES + voice`. `Score::ntracks()` = `nstaves() * VOICES` (`score.h:351`; the `@P ntracks` doc at `score.h:269` says "staves * 4").
- A `Segment` stores exactly one `EngravingItem*` per track in `m_elist` (size `nstaves * VOICES`, `segment.h:375`). Voice-0-only elements (Clef/KeySig/TimeSig) live at `staffIdx * VOICES`.
- `vStaffIdx()` (`engravingitem.h:336`) is the *visual* staff after cross-staff moves; `ChordRest::vStaffIdx() = staffIdx() + m_staffMove` (`chordrest.h:96`).

### 1.5 The tick/time model (`types/fraction.h`)

- Musical time is an exact **`Fraction`** (numerator/denominator of a whole note), not a float — `Fraction(1,4)` = a quarter note. `numerator()`/`denominator()`, `reduced()`, `absValue()`, `toDouble()`, `ticks()` (→ integer MIDI ticks, 480 PPQ via `Constants`), `fromTicks()` (`fraction.h:48,112,234,249`).
- Positions: `EngravingItem::tick()` is the **absolute** score position; `rtick()` is relative to the enclosing measure. `Segment::tick()` = `measure()->tick() + rtick()` (`segment.h:216`). `MeasureBase::tick()`/`ticks()`/`endTick()` (`measurebase.h:141-147`).
- The master `TimeSigMap* sigmap()` (`masterscore.h:105`) maps tick→time signature; `TempoMap`/`tempomap()` maps tick→tempo; both feed playback. See `playback-and-audio.md`.

### 1.6 Pitch / TPC / line (the Note number model)

A `Note` (`note.h:132`) stores pitch in **three** parallel representations (reconcile them, don't set one in isolation):

- **`pitch()`** — MIDI pitch 0–127 (`m_pitch`, `note.h:538`). What's played.
- **`tpc1()` / `tpc2()`** — **Tonal Pitch Class** (`m_tpc[2]`, `note.h:537`): the spelled note on the circle of fifths (so C♯ ≠ D♭ even at the same MIDI pitch). `tpc1` = concert pitch, `tpc2` = transposed (written) pitch; `tpc()` returns whichever matches the current concert-pitch view. `Tpc::TPC_INVALID` sentinel; helpers in `pitchspelling.h`. `setTpcFromPitch(Prefer)` derives a spelling.
- **`line()`** — vertical staff line/step (`m_line`, `note.h:531`; 0 = top line), derived from tpc + clef. `string()`/`fret()` for tablature.
- Plus `centOffset`, `tuning`, `userVelocity`, head group/type/scheme, `tieFor()`/`tieBack()`, `accidental()`, `dots()`, `el()` (attached fingerings/bends/etc.), `spannerFor()`/`spannerBack()`. (Historical notes: `docs\old_docs\pitch.md`, `tpc.md`, `ticklength.md`.)

---

## 2. Inventory — the score spine (member tables)

### 2.1 `Score` (`score.h:276`) / `MasterScore` (`masterscore.h:82`)

`Score : public EngravingObject, public muse::Contextable` — **not** an `EngravingItem`. A `Score` is either the `MasterScore` or an excerpt/part score; `isMaster()` distinguishes them, `masterScore()` reaches the master.

| Member / accessor | Type | Meaning |
|---|---|---|
| `staves()` / `staff(n)` / `nstaves()` | `std::vector<Staff*>` | the score's staves. |
| `parts()` / `partById()` | `std::vector<Part*>` | instruments. |
| `ntracks()` | `size_t` | `nstaves * VOICES`. |
| `measures()` | `MeasureBaseList*` | the linked list of measures/frames (the spine root). |
| `first()/last()`, `firstMeasure()/lastMeasure()`, `firstMeasureMM()` | `MeasureBase*`/`Measure*` | spine endpoints (`MM` = multimeasure-rest view). |
| `firstSegment(SegmentType)` / `lastSegment()` | `Segment*` | first/last segment across the whole score. |
| `tick2segment` (on `Measure`), `crMeasure(idx)` | | tick↔object lookup. |
| `selection()` | `Selection&` | current selection (see `select.h`). |
| `inputState()` | `InputState&` | note-input cursor state (`input.h`). |
| `style()` | `MStyle&` | the score style (`m_style`, `score.h:979`). |
| `systems()` / `pages()` / `npages()` | `std::vector<System*>` / `Page*` | **layout** output (rebuilt by layout, not the logical spine). |
| `rootItem()` / `dummy()` | `RootItem*` / `DummyParent*` | the detached-object anchors. |
| `excerpt()` | `Excerpt*` | non-null if this score is a part. |
| `scanElements(func)` | | visit every element in the score. |
| `doLayout()` / `doLayoutRange(st,et)` | | trigger layout (see `layout-and-rendering.md`). |
| `undoAddElement`/`undoRemoveElement`/`undoChangeProperty`/… | | the undo-aware edit API (dozens of `undo*` methods). |

**`MasterScore`** adds the engine-wide singletons: `undoStack()` (`UndoStack*`, `masterscore.h:104`), `sigmap()` (`TimeSigMap*`), `tempomap()`, `repeatList()` (`masterscore.h:120`; expanded repeat playback order), plus the list of linked excerpts. Edits ultimately go through the master's `undoStack()`.

### 2.2 `MeasureBase` (`measurebase.h:75`) and `Measure` (`measure.h:153`, `final`)

`MeasureBase : public EngravingItem` — the doubly-linked node of the spine; concrete subclasses are `Measure` and the frames (`HBox`/`VBox`/`TBox`/`FBox`, all in `box.h`).

| `MeasureBase` member | Type | Meaning |
|---|---|---|
| `next()/prev()`, `nextMM()/prevMM()` | `MeasureBase*` | spine links (`MM` skips into multimeasure rests). |
| `nextMeasure()/prevMeasure()` | `Measure*` | next/prev that is actually a `Measure`. |
| `system()` / `setSystem()` | `System*` | **layout placement** (not ownership); `layoutParent()` returns it. |
| `tick()` / `ticks()` / `endTick()` | `Fraction` | position & nominal length (`m_tick`, `m_len`). |
| `el()` | `ElementList&` | measure-relative annotations outside the staff (frame text, layout breaks…). |
| `lineBreak()/pageBreak()/sectionBreak()/noBreak()` | `bool` | break flags; `undoSetBreak()` to change. |
| `index()/measureIndex()` | `int` | ordinal. |

`Measure` adds the real content:

| `Measure` member | Type | Meaning |
|---|---|---|
| `segments()` / `first()/last()`, `first(SegmentType)` | `SegmentList&` / `Segment*` | the measure's segment list — the inner spine. |
| `findSegment(SegmentType, tick)` / `getSegment(…)` / `undoGetSegment(…)` | `Segment*` | find-or-create a segment at a tick. |
| `mstaves()` | `std::vector<MStaff*>` | per-staff measure data (visibility, measure-number element, corruption flag). |
| `timesig()` / `ticks()` (`m_len`) | `Fraction` | nominal vs actual length; `isIrregular()` when they differ (pickup/anacrusis). |
| `measureNumber()`, `measureNumberMode()` | `int` / enum | numbering. |
| `repeatStart()/repeatEnd()/repeatCount()` | | repeats (flag-backed). |
| `mmRest()/hasMMRest()/mmRestCount()` | `Measure*`/`int` | multimeasure-rest grouping. |
| `firstChordRest(track)/lastChordRest(track)` | `ChordRest*` | content access. |
| `findChord(tick,track)`, `findChordRest(tick,track)` | | lookups. |

### 2.3 `Segment` (`segment.h:119`, `final`)

`Segment : public EngravingItem`. A segment holds all vertically-aligned elements **of one type** that start at the same tick; it stores one `EngravingItem*` per track.

| Member | Type | Meaning |
|---|---|---|
| `segmentType()` | `SegmentType` | which kind (see enum below); drives ordering within a tick. |
| `elist()` / `element(track)` / `cr(track)` | `std::vector<EngravingItem*>` | per-track content (size `nstaves*VOICES`); `cr()` casts to `ChordRest`. |
| `setElement(track,e)` / `removeElement(track)` | | mutate a track slot. |
| `annotations()` | `std::vector<EngravingItem*>` | non-track-slot attachments at this tick (dynamics, text, fingering lists…). |
| `next()/prev()`, `next1()/prev1()` | `Segment*` | linked list; `next()` stops at measure boundary, `next1()` crosses it; `*MM` / `*enabled` / `*Active` variants. |
| `nextCR(track)` / `nextChordRest(track)` | | next chord/rest. |
| `measure()` / `system()` | `Measure*`/`System*` | owner / placement. |
| `rtick()` / `tick()` / `ticks()` | `Fraction` | measure-relative, absolute, duration. |
| `isChordRestType()/isClefType()/isTimeSigType()/…` | `bool` | per-type predicates. |

**`SegmentType`** (`segment.h:41`) is a **bit-flag** enum (so a mask can select several): `Invalid, BeginBarLine, HeaderClef, KeySig, Ambitus, Breath, TimeSig, StartRepeatBarLine, Clef, BarLine, TimeTick, ChordRest, EndBarLine, KeySigAnnounce, TimeSigAnnounce`, the `*RepeatAnnounce`/`*StartRepeatAnnounce` courtesy variants, and composite masks (`BarLineTypes`, `TimeSigTypes`, `ClefTypes`, `Duration = TimeTick | ChordRest`, `All = -1`). The numeric order of the bits is the **left-to-right draw order** of segments sharing a tick.

### 2.4 `DurationElement` → `ChordRest` → `Chord`/`Rest`

**`DurationElement`** (`durationelement.h:41`): base for anything with a musical duration — `Chord`, `Rest`, `Tuplet`. Holds `m_duration` (`Fraction`, `ticks()`/`setTicks()`), `tuplet()`/`setTuplet()` (nesting), `actualTicks()` (duration after tuplet scaling), `endTick()`.

**`ChordRest`** (`chordrest.h:57`): shared base of `Chord`, `Rest`, `MMRest`. (`DECLARE_CLASSOF(ElementType::INVALID)` — it is abstract, never instantiated directly.)

| Member | Type | Meaning |
|---|---|---|
| `segment()` | `Segment*` | owning segment (= `ownershipParent()`). |
| `durationType()` / `setDurationType()` | `TDuration` | the written note-value+dots (`durationtype.h`). |
| `dots()` / `setDots()` | `int` | augmentation dots. |
| `beam()` / `setBeam()`, `beamMode()` | `Beam*` / `BeamMode` | beaming. |
| `lyrics()` | `std::vector<Lyrics*>` | attached lyrics by verse. |
| `el()` | `ElementList&` | attached items (fermatas via `addFermata`, etc.). |
| `tuplet()` (inherited) | `Tuplet*` | tuplet membership. |
| `up()` / `setUp()` | `bool` | stem/placement direction (lives in `ChordRest::LayoutData`). |
| `staffMove()` / `vStaffIdx()` | `int` | cross-staff. |
| `isGrace()/isGraceBefore()/isGraceAfter()` | `bool` | grace-note tests. |

**`Chord`** (`chord.h:137`, `final`): a chord (a single note is a degenerate chord).

| Member | Type | Meaning |
|---|---|---|
| `notes()` | `std::vector<Note*>` | the note heads (sorted by decreasing line/step). `upNote()`/`downNote()`. |
| `stem()` / `hook()` / `stemSlash()` | `Stem*`/`Hook*`/`StemSlash*` | stem apparatus; `stemSlash` is for grace acciaccaturas. |
| `beam()` (inherited) | `Beam*` | beam, if beamed. |
| `arpeggio()` / `spanArpeggio()` | `Arpeggio*` | arpeggio starting on / spanning this chord. |
| `tremoloTwoChord()`/`tremoloSingleChord()` | `Tremolo*…` | tremolos. |
| `articulations()` | `std::vector<Articulation*>` | staccato/accent/ornaments/etc. |
| `graceNotes()`, `graceNotesBefore()/After()` | `std::vector<Chord*>`/`GraceNotesGroup&` | grace chords. |
| `ledgerLines()` | `std::vector<LedgerLine*>` | computed ledger lines. |
| `stemDirection()` / `setStemDirection()` | `DirectionV` | AUTO/UP/DOWN. |
| `noteType()` | `NoteType` | NORMAL or a grace-note kind (`chord.h:53`). |
| `noStem()`, `showStemSlash()` | `bool` | |
| `startingSpanners()/endingSpanners()` | `std::set<Spanner*>` | spanners anchored here. |

**`Rest`** (`rest.h:62`): a rest. `isGap()` (voice-filler gap rest), `dotList()`/`dotLine()`, `deadSlapped()`, `isWholeRest()/isBreveRest()`, `isFullMeasureRest()` (inherited), `alignWithOtherRests()`. **`MMRest`** (`mmrest.h`) and **`MeasureRepeat`** (`measurerepeat.h`) are the other `ChordRest`-family members (`isRestFamily()`).

### 2.5 `Note` (`note.h:132`, `final`)

Owned by a `Chord` (`chord()` = `ownershipParent()`). Core members: `pitch()`, `tpc1()/tpc2()/tpc()`, `line()`, `string()/fret()` (TAB), `accidental()`, `dots()`, `headGroup()/headType()/headScheme()`, `tieFor()/tieBack()`, `el()` (fingerings/bends/symbols), `spannerFor()/spannerBack()`, `play()`, `ghost()/deadNote()`, `userMirror()/userDotPosition()`, `playEvents()` (`NoteEventList` for playback), `tiedNotes()/firstTiedNote()/lastTiedNote()`. See §1.6 for the pitch/tpc/line triad.

### 2.6 Cross-cutting families

- **`Spanner`** (`spanner.h:169`) — base for anything spanning a tick range (slurs, ties, lines). Holds `tick()`+`ticks()` (→`tick2()`), `track2()` (end track), `startElement()/endElement()`, `startCR()/endCR()`, and a vector of **`SpannerSegment`**s (`spannerSegments()`, one per system the spanner crosses). `anchor()` (`Anchor`: `SEGMENT/MEASURE/CHORDREST/NOTE`) says what the ends attach to. Abstract subtree: `SlurTie` → `Slur`/`Tie`; `SLine` (`line.h:100`) → the many line types (hairpins, ottava, pedal, volta, trill, text lines, glissando, note line, guitar bend, let-ring, palm-mute, vibrato, lyrics line…). Each line's on-screen pieces are `LineSegment`s (`line.h:42`, a `SpannerSegment`). `isSpanner()`/`isSLine()`/`isSpannerSegment()` helpers live on `EngravingObject`.
- **`TextBase`** (`textbase.h:308`) — base of all text (`Text`, `StaffText`, `SystemText`, `Dynamic`, `Expression`, `Fingering`, `Lyrics`, `Harmony`, `RehearsalMark`, `TempoText`, `Marker`, `Jump`, `InstrumentName`, `MeasureNumber`, `FiguredBass`, …). Rich text fragments, `TEXT`/`HTML_TEXT`/font/align/frame `Pid`s. `StaffTextBase` (`stafftextbase.h`) is the intermediate base for the channel/playback-carrying texts (`isStaffTextBase()`).
- **`Staff`** (`staff.h`) — a staff line-group: `part()`, `idx()`, `clefType(tick)`, `key(tick)`/`setKey()`, `timeStretch(tick)`, `staffType(tick)` (`StaffType*`: lines, TAB, percussion), `show()`, cross-staff links. Keyed, time-varying data lives in per-staff maps (`ClefList m_clefs`, `KeyList m_keys`, `StaffTypeList`).
- **`Part`** (`part.h:61`, an `EngravingObject`) — an instrument: `nstaves()`, `staff(idx)`, `instrument(tick)` (`Instrument*`; a part can change instrument mid-score), `instruments()` (`InstrumentList` = tick→Instrument map), `midiProgram()`, `partName()`, `show()`.
- **`Selection`** (`select.h:107`), **`InputState`** (`input.h`), **`Interval`** (`interval.h`, transposition), **`Excerpt`** (`excerpt.h`, a part). These are DOM support types, **not** `EngravingItem`s. (**`BracketItem`**, `bracketitem.h:38`, *is* an `EngravingItem` — `EngravingItem(ElementType::BRACKET_ITEM, parent)`, `bracketitem.cpp:35` — as is `Staff`, see §2.12.)

### 2.7 Rhythm / beaming: `BeamBase`, `Beam`, `Tuplet`, `Stem`, `StemSlash`, `Hook`

**`BeamBase`** (`beambase.h:71`, abstract, `ElementType::INVALID`) — shared by `Beam` and `TremoloTwoChord`'s beam. **`Beam`** (`beam.h:49`, `final`) — a beam has **no owner**; it is parented to the score's dummy (`Beam::Beam(Score*)` → `BeamBase(ElementType::BEAM, parent->dummy())`, `beam.cpp:60`) and its lifetime is driven by the chord/rests it beams (`ChordRest::removeDeleteBeam`, `beam.h:71-73`).

| Member / accessor | Type | Meaning |
|---|---|---|
| `crossStaffMove` (`M_PROPERTY2`, `beambase.h:76`) | `int` | cross-staff shift property (default 0). `crossStaffIdx()/defaultCrossStaffIdx()/acceptCrossStaffMove(int)`; pure-virtual `minCRMove()/maxCRMove()` (`:80-81`). |
| `up()/setUp()`, `direction()/doSetDirection()`, `directionIdx()` | `bool` / `DirectionV` / 0-1 | resolved stem side vs user direction; `directionIdx` is 0 for AUTO/DOWN, 1 for UP (`:101`) — indexes `m_userModified[2]` and `BeamFragment::py1/py2[2]`. |
| `userModified()/setUserModified(bool)` | `bool` | per-direction "user dragged the beam" flag (`m_userModified[2]`, `:198`). |
| `beamSegments()` / `clearBeamSegments()` | `vector<BeamSegment*>` | the drawn pieces: `BeamSegment` (`:53`) = `line` + `level` + `above` + `startTick/endTick` + `isBeamlet`/`isBefore` + `parentElement`. |
| `startAnchor()/endAnchor()` | `PointF` | computed beam end points. |
| `BeamBase::LayoutData` (`:162`) | struct | `beamType` (`INVALID/BEAM/TREMOLO`), `beam`/`trem`, `up`, `tick`, `slope`, `isGrace`, `beamSpacing/beamDist/beamWidth`, `elements`, `notePositions`, `tab`, `isBesideTabStaff`, `crossStaffBeamPos` (`INVALID/BETWEEN/ABOVE/BELOW`), `limitingChordAbove/Below`. |
| `Beam::elements()` | `vector<ChordRest*>` (sorted by tick, `beam.h:193`) | the beamed chords/rests; `add/remove(EngravingItem*)`, `contains(cr)`, `empty()`, `ticks()`. |
| `Beam::beamPos()/setBeamPos(PairF)` | `PairF` | user-edited left/right Y (property `BEAM_POS`). |
| `growLeft()/growRight()` | `double` (def 1.0) | "feathered" beam fan-out; `setAsFeathered(bool slower)` (`:97`). |
| `beamDist()`, `beamWidth()`, `beamSpacing()` (def 3 quarter-spaces) | `double`/`int` | layout metrics. |
| `noSlope()/setNoSlope()`, `slope()/computeAndSetSlope()` | `bool` / `double` | flat-beam flag and computed slope (`MAX_SLOPES` 0-7, `:188`). |
| `isGrace()`, `cross()`, `fullCross()` | `bool` | grace beam; cross-staff / entirely cross-staff. |
| `minCRMove()/maxCRMove()` | `int` | set in layout (`:211`). |
| `beamFragments()` | `vector<BeamFragment*>` | per-system splits (a `BeamFragment` holds `py1[2]/py2[2]`, `beambase.h:44`). |
| `tab()/isBesideTabStaff()/notePositions()/tremAnchors()` | misc | TAB-beam and tremolo-anchor data. |
| `findChordWithCustomStemDirection()`, `topLevelSegmentForElement(cr)` | queries | layout helpers. |
| `acceptDrop/drop(Transaction&, EditData&)` | | beam-mode icons; `actionIconTypeForBeamMode(BeamMode)` static (`:150`). |

**`Tuplet`** (`tuplet.h:45`, `final`, `DurationElement`, owner = a `Measure`, `tuplet.h:74`). Example in the header (`:36-43`): 1/8 triplet = `baseLen` 1/8, `ratio` 3/2; total duration = `baseLen * ratio.denominator()`.

| Member / accessor | Type | Meaning |
|---|---|---|
| `ratio()/setRatio()` | `Fraction` | actual:normal (3/2 for a triplet). |
| `baseLen()/setBaseLen()` | `TDuration` | unit the tuplet is measured in. |
| `elements()` / `contains()` | `vector<DurationElement*>` (`m_currentElements`) | members really in the tuplet; `m_allElements` (private `std::set`) = everything whose `tuplet()` is this one (`:168-171`). |
| `numberType()` | `TupletNumberType` (`SHOW_NUMBER/SHOW_RELATION/NO_TEXT`) | number display. |
| `bracketType()`, `hasBracket()`, `bracketWidth()` | `TupletBracketType` (`AUTO_BRACKET/SHOW_BRACKET/SHOW_NO_BRACKET`), `bool`, `Spatium` | bracket policy / resolved flag (`calcHasBracket`). |
| `number()` | `Text*` | the numeral text child. |
| `direction()`, `isUp()`, `isSmall()` | `DirectionV`, `bool`, `bool` | placement. |
| `tick()/setTick()`, `elementsDuration()`, `sortElements()` | `Fraction` | stored absolute tick (`m_tick`). |
| `p1()/p2()`, `userP1()/userP2()`, `bracketL[4]`, `bracketR[3]` | `PointF` | bracket ends (layout) and user offsets (edited via grips, `gripsCount()`). |
| `cross()`, `vStaffIdx()` | | spans staves. |
| `sanitizeTuplet()`, `addMissingElements()` | | repair helpers (used by read/paste). |
| `computeTupletDenominator(num, totalDuration)` (static) | `int` | normal-note count. |

**`Stem`** (`stem.h:31`, owner `Chord`): `baseLength()` (layout), `userLength()` (user adjust via grip, 1 grip), `lineWidth()`, `length()` = `absoluteFromSpatium(base+user)`, `flagPosition()`, `up()`; `LayoutData{ LineF line; double beamCorrection }`. **`StemSlash`** (`stemslash.h:36`, acciaccatura slash): no members beyond `chord()`; `LayoutData{ line, stemWidth }`. **`Hook`** (`hook.h:33`, a `Symbol`, owner `Chord`): `hookType()/setHookType(int)` (number of flags; negative = down), `smuflAnchor()`, static `symIdForHookIndex(int index, bool straight)`.

### 2.8 Articulations and ornaments

**`Articulation`** (`articulation.h:104`, owner `ChordRest`; also the base of `Ornament` and `Tapping`). Per-articulation symbol identity is a `SymId`, not an enum of its own.

| Member / accessor | Type | Meaning |
|---|---|---|
| `symId()/setSymId()` | `SymId` | the glyph (e.g. `articStaccatoAbove`); `setSymId` recomputes `m_categories`. |
| `direction()` | `DirectionV` | user up/down. |
| `anchor()` | `ArticulationAnchor` (`TOP/BOTTOM/AUTO`) | forced placement (`:59`). |
| `ornamentStyle()` | `OrnamentStyle` (`DEFAULT/BAROQUE`) | trill/mordent style. |
| `playArticulation()` | `bool` | whether playback honours it. |
| `channelName()` | `String` | MIDI channel/articulation name override. |
| `textType()/text()` | `ArticulationTextType` / `Text*` | optional attached text. |
| `isStaccato()/isTenuto()/isAccent()/isMarcato()/isDouble()/isLuteFingering()/isLaissezVib()/isHandbellsArticulation()` | `bool` | from `m_categories` (`ArticulationCategories` flag set, `:45-57`). |
| `articulationName()` / `symId2ArticulationName(SymId)` | `String` | MIDI-rendering name. |
| `layoutCloseToNote()`, `isHiddenOnTabStaff()`, `isOnCrossBeamSide()` | `bool` | layout hints; TAB visibility uses `m_showOnTabStyles` (`Sid` pair). |
| `chordRest()/segment()/measure()/system()/page()` | navigation | walk owner chain. |
| `LayoutData` | `ld_field<bool> up`, `ld_field<SymId> symId` (`:187`) | resolved side and glyph. |
| free fns | `updateArticulations / splitArticulations / joinArticulations / flipArticulations` (`:93-97`) | combine/split compound glyph sets (e.g. staccato+tenuto) and flip above/below. |

**`Ornament`** (`ornament.h:29`, `final`, derives `Articulation`): adds `M_PROPERTY`s `intervalAbove/intervalBelow` (`OrnamentInterval`), `showAccidental` (`OrnamentShowAccidental`: `DEFAULT/ANY_ALTERATION/ALWAYS`), `startOnUpperNote` (`ornament.h:86-89`); `noteAbove()/noteBelow()` (`std::array<Note*,2>` = the cue notes it generates), `accidentalAbove()/accidentalBelow()`, `cueNoteChord()`, `showCueNote()` (`AutoOnOff`), `hasIntervalAbove/Below()`, `computeNotesAboveAndBelow(AccidentalState*)`; static `fromTrillType(TrillType)`. `Ornament` overrides `scanElements`/`remove` (`:56-58`) because it carries child items (cue-note chord, above/below accidentals).

### 2.9 Spanners (concrete)

Spanner machinery (`Spanner` `spanner.h:169`, `SpannerSegment` `spanner.h:41`, `SLine` `line.h:100`, `LineSegment` `line.h:42`) is summarised in §2.6; below are the concrete types. `anchor()` per type: `Slur` → `CHORDREST`, `Tie` → `NOTE`, `Hairpin`/`Ottava`/`Pedal`/`Trill` → `SEGMENT`, `Volta` → `MEASURE`, `Glissando` → `NOTE` (`slur.h:82`, `tie.h:96`, `hairpin.h:113`, `ottava.h:119`, `pedal.h:72`, `trill.h:84`, `volta.h:72`, `glissando.h:81`).

**`TextLineBase`** (`textlinebase.h:89`, derives `SLine`; base of Hairpin/Ottava/Pedal/Volta/TextLine/LetRing/PalmMute/…). Its ~45 `M_PROPERTY`s (`:93-140`; id list `textLineBasePropertyIds()` returns `std::array<Pid,45>`, `:153`) are the shared line-appearance model:

| Property group | Members | Type |
|---|---|---|
| line | `lineVisible` | `bool` |
| hooks | `beginHookType/endHookType` (`HookType`: `NONE/HOOK_90/HOOK_45/HOOK_90T/ARROW/ARROW_FILLED/ROSETTE`), `beginHookHeight/endHookHeight` | `HookType`, `Spatium` |
| arrows | `begin/endLineArrowHeight/Width`, `begin/endFilledArrowHeight/Width` | `Spatium` |
| spacing | `gapBetweenTextAndLine`, `textSizeSpatiumDependent` | `Spatium`, `bool` |
| begin text | `beginText`, `beginTextAlign`, `beginTextPosition`, `beginTextPlace` (`TextPlace`, def `AUTO`), `beginFontFamily/Size/Style`, `beginTextMusicalSymbolsScale` (def 1.0), `beginTextMusicSymbolsSize` (def 12.0), `beginTextOffset` | `String`, `Align`, `AlignH`, … |
| continue text | `continueText…` (same set; shown on 2nd+ system) | |
| end text | `endText…` (same set) | |

`TextLineBase` overrides `propertyFlags/setPropertyFlags` (`:150-151`) — its text properties are styled and the flags are tracked per text part. **`TextLineBaseSegment`** (`:40`): `twoLines()`, `text()/endText()` (the `Text*` children), `LayoutData{ points[6], joinedHairpin, beginArrow, endArrow, npoints, lineLength }`; static `boundingBoxOfLine(...)`. `isSystemTextLine(element)` (`:161`) = `isTextLineBase() && systemFlag()`.

**`SlurTie`** (`slurtie.h:157`) / **`SlurTieSegment`** (`:90`): shared by `Slur`, `Tie` and the subclasses.

| Member | Type | Meaning |
|---|---|---|
| `SlurTie::up()` | `bool` | actual (resolved) direction (`m_up`). |
| `slurDirection()/setSlurDirection()/undoSetSlurDirection()` | `DirectionV` | user direction. |
| `styleType()` | `SlurStyleType` (`Undefined/Solid/Dotted/Dashed/WideDashed`) | line style; `subtype()` = `int(styleType)+1`. |
| `maskSlurTie()` | `AutoOnOff` | mask staff lines behind it. |
| `newSlurTieSegment()` (pure), `scalingFactor()` (pure), `fixupSegments(n)` | | segment factory / sizing. |
| `SlurTieSegment::ups(Grip)` | `UP{ PointF p; PointF off }` ×`Grip::GRIPS` (`m_ups`, `:147`) | the 4 Bezier control points: layout position + user offset (`pos()` = `p+off`). `setSlurOffset(grip, pt)`. |
| `LayoutData` (`:133`) | `ld_field<PainterPath> path`, `ld_field<double> midThickness` | computed curve. |
| `endWidth()/midWidth()/dottedWidth()` (pure) | `double` | stroke widths. |
| `SlurTiePos` (`:34`) | `p1,p2,system1,system2` | layout end points in system coords. |

**`Slur`** (`slur.h:73`): `connectedElement` (`M_PROPERTY2`, `NONE/GLISSANDO` — Guitar Pro import hack, `:96-99,137`), `partialSpannerDirection` (`PartialSpannerDirection`: `NONE/INCOMING/OUTGOING/BOTH`, `:138`); `isIncoming()/isOutgoing()` (+`undoSet*`/`set*`), `isCrossStaff()`, `hasCrossBeams()`, `stemFloated()` (`{left,right}` flags: end point attached to stem but floated to the note, `:84-93`), `undoChangeStartEndElements(scr, ecr)`. **`SlurSegment`** (`:35`): `extraHeight`, `endPointOff1/2` (`M_PROPERTY2`), `isEdited()`, `isEndPointsEdited()`. Subtypes sharing `isSlur()`: `HammerOnPullOff`, `TappingHalfSlur`.

**`Tie`** (`tie.h:85`): `startNote()/endNote()` (start/end *elements* are `Note`s), `isInside()`, `tiePlacement()` (`TiePlacement`: `AUTO/INSIDE/OUTSIDE`), `isOuterTieOfChord(Grip)`, `hasTiedSecondInside()`, `isCrossStaff()`, jump-point API for ties across repeats — outgoing: `updatePossibleJumpPoints()`, `addTiesToJumpPoints()`, `undoRemoveTiesFromJumpPoints()`, `tieJumpPoints()`, `allJumpPointsInactive()`; incoming: `jumpPoint()/setJumpPoint()`, `startTie()`, `updateStartTieOnRemoval()`; static `changeTieType(oldTie, endNote)`. `isInSpannerMap()` returns **false** (`:148`) — ties live on notes, not in the score spanner map (same for `Glissando`, `glissando.h:106`). **`TieSegment`** (`:35`): `staffMove`, `adjustmentOffset(Grip)` (`m_adjustmentOffsets`, folded into the user offset by `consolidateAdjustmentOffsetIntoUserOffset()`), `minShoulderHeight()/maxShoulderHeight()`, `LayoutData{ allJumpPointsInactive }`. Subclasses `LaissezVib`, `PartialTie` (`isTie()` true for all three).

**`Hairpin`** (`hairpin.h:103`, `final`; `HairpinType`: `INVALID=-1, CRESC_HAIRPIN, DIM_HAIRPIN, CRESC_LINE, DIM_LINE`, `:33`):

| Member | Type | Meaning |
|---|---|---|
| `hairpinType()` | `HairpinType` | `isCrescendo()/isDecrescendo()/isLineType()` derive from it; `subtype()` = `int(type)`. |
| `veloChange()` | `int` | playback velocity delta. |
| `veloChangeMethod()` | `ChangeMethod` (`NORMAL/EXPONENTIAL/EASE_IN/EASE_OUT/EASE_IN_OUT`) | curve. |
| `singleNoteDynamics()` | `bool` | per-note dynamics playback. |
| `hairpinHeight()/hairpinContHeight()` | `Spatium` | opening at end / on continuation system. |
| `hairpinCircledTip()` | `bool` | niente circle. |
| `voiceAssignment()` | `VoiceAssignment` (def `ALL_VOICE_IN_INSTRUMENT`) | which voices it applies to. |
| `direction()`, `centerBetweenStaves()` | `DirectionV`, `AutoOnOff` | placement. |
| `snapToItemBefore()/snapToItemAfter()` | `bool` (def true) | snap ends to adjacent dynamics/expressions. |
| `dynamicTypeFrom()/dynamicTypeTo()`, `dynamicSnappedBefore()/After()` | | resolved neighbours. |

`HairpinSegment` (`:45`): `drawCircledTip()/circledTip()/circledTipRadius()`, `findElementToSnapBefore/After`, `getDragGroup`, private `acceptDrop/drop(Transaction&, EditData&)` overrides (`:87-88`).

**`Dynamic`** (`dynamic.h:51`, `final`, `TextBase`, owner `Segment`): `dynamicType()` (`DynamicType`: `OTHER`, `PPPPPP … FFF …`, full list `types.h:771`), `velocity()` (`m_velocity` default −1 = unset; stock values via static `dynamicVelocity(type)`), `changeInVelocity()` (def 128 = "unset"), `velChangeSpeed()` (`DynamicSpeed`: `SLOW/NORMAL/FAST`), `playDynamic()`, `avoidBarLines`, `centerOnNotehead` (`M_PROPERTY`s), `leftHairpin()/rightHairpin()` (adjacent hairpins, `findAdjacentHairpins()`), left/right drag offsets, `snappedExpression()`; static `dynamicList()` = `DYN_LIST` of `Dyn{type, velocity, changeInVelocity, accent, text}` (`dynamic.h:36`), `dynamicText(type)`, `dynInfo(type)`. `hasCustomText()` = displayed text differs from the type's stock text.

**`Ottava`** (`ottava.h:108`; `OttavaType`: `OTTAVA_8VA/8VB/15MA/15MB/22MA/22MB`): `ottavaType()`, `numbersOnly()` (print "8" not "8va"), `pitchShift()` (from `ottavaDefault[]` table: ±12/24/36, `:67-74`), `setPlacement()`; overrides `undoChangeProperty` to re-style (`updateStyledProperties`). `OttavaSegment::canBeExcludedFromOtherParts()` true. **`Pedal`** (`pedal.h:53`): no data members; static `PEDAL_SYMBOL/STAR_SYMBOL` strings, `findNextInStaff()`, `connect45HookToNext()` (joined pedal changes). **`Volta`** (`volta.h:58`): `endings()` (`vector<int>` repeat numbers, `m_endings`), `hasEnding(n)`, `firstEnding()/lastEnding()`, `isFirstVolta()`, `text()/setText()`; deprecated `Type{OPEN,CLOSED}` (`voltaType()`). **`Trill`** (`trill.h:72`, `SLine`, **not** a TextLineBase): `trillType()` (`TrillType`: `TRILL_LINE/UPPRALL_LINE/DOWNPRALL_LINE/PRALLPRALL_LINE`), `ornamentStyle()`, `accidental()`, `cueNoteChord()`, `ornament()` (the owned `Ornament`); `TrillSegment::symbols()` (`SymIdList` glyph strip). **`Glissando`** (`glissando.h:58`, `SLine`): `M_PROPERTY`s `text`, `glissandoType` (`GlissandoType`: `STRAIGHT/WAVY`), `glissandoStyle` (`GlissandoStyle`: `CHROMATIC/WHITE_KEYS/BLACK_KEYS/DIATONIC/PORTAMENTO`), `glissandoShift`, `fontFace/fontSize/fontStyle`, `showText`, `easeIn/easeOut`; `isHarpGliss()` (`std::optional<bool>`); static `guessInitialNote(Chord*)`, `pitchSteps(spanner, offsets)`.

### 2.10 Bar line, clef, key signature, time signature

| Class (`file:line`) | Members | Type / meaning |
|---|---|---|
| **`BarLine`** (`barline.h:79`, owner `Segment`) | `barLineType()` | `BarLineType` bit values `NORMAL=1, DOUBLE=2, START_REPEAT=4, END_REPEAT=8, BROKEN=0x10, END=0x20, END_START_REPEAT=0x40, DOTTED=0x80, REVERSE_END=0x100, HEAVY=0x200, DOUBLE_HEAVY=0x400` (`types.h:563-582`; aliases `SINGLE/LEFT_REPEAT/RIGHT_REPEAT/DASHED/FINAL`). |
| | `spanStaff()`, `spanFrom()`, `spanTo()` | `bool`, `int`, `int` — span to next staff; start/end line numbers (presets `BARLINE_SPAN_*` constants, `:34-49`). `isTop()/isBottom()`. |
| | `playCount()` | `int` (−1 = unset; "for copy & paste") ; `playCountText()`. |
| | `el()` | `ElementList` — fermatas/other attached items (`m_el`). |
| | `showTips()` | tick/short bar line tips. |
| | `LayoutData{ y1, y2 }` | computed vertical extent (`calcY()`); `BarLineEditData{ yoff1, yoff2 }` for the grip edit. |
| | `barLineTable` | static `vector<BarLineTableItem{type,userName}>`. |
| **`Clef`** (`clef.h:88`, owner `Segment`) | `clefType()`, `concertClef()/transposingClef()`, `clefTypeList()` | `ClefTypeList{concertClef, transposingClef}` (`:44`) — the clef pair for concert vs transposed display. |
| | `showCourtesy()`, `isCourtesy()`, `isSmall()`, `isTrailer()`, `forInstrumentChange()` | `bool` flags. |
| | `clefToBarlinePosition()` | `ClefToBarlinePosition` (`AUTO/BEFORE/AFTER`) — which side of a barline a clef-change sits. |
| | `isHeader()`, `isMidMeasureClef()` | derived from segment type / tick. |
| | `ClefInfo::clefTable[]` (`:62`) | static table per `ClefType`: `m_line`, `m_pitchOffset`, `m_lines[14]` (pitch→line map), `m_symId`, `m_staffGroup`. Accessors `ClefInfo::line/pitchOffset/symId/lines/staffGroup(ClefType)`. |
| **`KeySig`** (`keysig.h:33`, owner `Segment`) | `keySigEvent()` | `KeySigEvent` (`key.h:62`): `key()` (transposed) / `concertKey()`, `mode()` (`KeyMode`: `UNKNOWN/NONE/MAJOR/MINOR/DORIAN/…/LOCRIAN`), `custom()`, `isAtonal()`, `forInstrumentChange()`, `customKeyDefs()` (`vector<CustDef{degree, sym, xAlt, octAlt}>`). |
| | `key()`, `concertKey()`, `setKey(concert, transposed)` | `Key` = `C_B=-7 … C=0 … C_S=7` (`types.h:1434`). `subtype()` = `int(key)+7`. |
| | `isCourtesy` (`M_PROPERTY2`), `showCourtesy()`, `hideNaturals()` | flags (hideNaturals overrides style in layout). |
| | `isChange()`, `changeKeySigEvent(e)` | change vs. first. |
| | `LayoutData{ vector<KeySym> keySymbols }` | `KeySym{sym, line, xPos}` (`key.h:40`) per accidental drawn. |
| | `AccidentalState` (`key.h:111`) | `uint8_t m_state[75]` per absolute staff line (+`TIE_CONTEXT` bit) + `forceRestateAccidental`; `init(Key)`/`init(KeySigEvent)`; the measure-level accidental tracker used by layout and `Note` spelling. |
| **`TimeSig`** (`timesig.h:50`, owner `Segment`) | `sig()` | `Fraction` shown value (e.g. 3/4); `numerator()/denominator()`. |
| | `stretch()` | `Fraction` local÷global — `isLocal()` when ≠ 1/1 (irregular/local meter). |
| | `timeSigType()` | `TimeSigType` (`NORMAL/FOUR_FOUR/ALLA_BREVE/CUT_BACH/CUT_TRIPLE`, `:37`). |
| | `numeratorString()/denominatorString()` | custom text override (`ssig()` for `"3/4"` string). |
| | `groups()` | `Groups` (`groups.h`) beam-grouping pattern for this meter. |
| | `showCourtesySig()`, `isCourtesy` (`M_PROPERTY2`), `largeParentheses()` | flags. |
| | `timeSigPlacement()`, `timeSigStyle()` | `TimeSigPlacement` (`NORMAL/ABOVE_STAVES/ACROSS_STAVES`), `TimeSigStyle` (`NORMAL/NARROW/LARGE`) — style-derived. `showOnThisStaff()`, `isAboveStaves()`, `isAcrossStaves()`. |
| | `scale()` | `ScaleF` digit scale. |
| | `LayoutData{ ns, ds, pz, pn, pointLargeLeftParen/RightParen }` | digit glyph lists and positions. |

### 2.11 Harmony / chord symbols, figured bass, fretboard diagrams

**`Harmony`** (`harmony.h:212`, `final`, `TextBase`; parent = `Segment` **or** `FretDiagram`, `:218`). A chord symbol can hold several chords (polychord).

| Member | Type | Meaning |
|---|---|---|
| `chords()` / `isPolychord()` / `addChord(HarmonyInfo*)` | `vector<HarmonyInfo*>` | the chord(s); polychord = more than one (`:265-267`). |
| `harmonyType()` | `HarmonyType` (`STANDARD/ROMAN/NASHVILLE`) | rendering/transposition/export mode; `subtype()` = `int(type)`. |
| `degreeList()/addDegree(HDegree)` | `vector<HDegree>` | explicit add/alter/subtract degrees. |
| `rootCase()/bassCase()` | `NoteCaseType` | as-typed letter case (`rootRenderCase()/bassRenderCase()` resolve). |
| `bassScale()` | `double` (def 1.0) | bass-note scale. |
| `play()`, `isRealizable()`, `isPlayable()` | `bool` | playback. |
| `realizedHarmony()` / `getRealizedHarmony()` | `RealizedHarmony&` (mutable member) | voiced notes for playback (below). |
| `doNotStackModifiers()` | `bool` | per-symbol override of stacked modifiers. |
| `harmonyName()/displayText()` | `String` | canonical name / what is drawn. |
| `getParentSeg()/getParentFretDiagram()`, `findNext()/findPrev()`, `ticksTillNext(utick, stopAtMeasureEnd)` | | navigation / duration. |
| `fontStyle()/family()/size()` | overridden accessors | stored **in `Harmony` itself** (`m_fontSize` 10.0, `m_fontFamily`, `m_fontStyle`, `:353-356`) — "applied to whole item", not per-fragment. |
| `render()`, `afterRead()`, `setHarmony(String)` | | parse + build render list. `parseHarmony/parseSingleHarmony` private. |
| `LayoutData` (`:318`) | `harmonyHeight`, `polychordDividerLines/Offset`, `baseline`, `fontList`, `renderItemList` (`vector<HarmonyRenderItem*>`) | layout output. |

**`HarmonyInfo`** (`harmony.h:156`) is one chord: `id()` (>0 = `ChordList` id; −1 invalid; < −10000 = private generated id, `:193-195`), `rootTpc()/bassTpc()` (TPC, `TPC_INVALID` if none), `textName()`, `parsedChord()` (`ParsedChord*`), `descr()/getDescription()/generateDescription()` (→ `ChordDescription`), `hasModifiers()`. `HarmonyRenderItem` (`:53`) is the render-list base — subclasses `TextSegment` (string + font) and `ChordSymbolParen` (a `Parenthesis*`).

**Chord list** (`chordlist.h`): `ChordList : std::map<int, ChordDescription>` (`:461`) loaded from `chords_*.xml`; members `fonts` (`ChordFont`), `renderListRoot/Function/Bass/BassOffset`, `chordTokenList`, flags `autoAdjust/stackModifiers/excludeModsHAlign`, `nominalMag/Adjust`, `chordPreset()` (`ChordStylePreset`: `STANDARD/JAZZ/LEGACY/CUSTOM`), `customChordList()` (true ⇒ saved inside the score), `read/write`, `description(id)`, `symbol(name)`, `token(name, class)`. `ChordDescription` (`:400`): `id` (Band-in-a-Box number), `names`, `parsedChords`, MusicXML `xmlKind/xmlText/xmlSymbols/xmlParens/xmlDegrees`, `chord` (`HChord` C-based pitch-class bitmask, 12 bits, `:74`), `renderList`/`renderListStacked`. `ParsedChord` (`:343`): `parse(string, ChordList*, syntaxOnly, preferMinor)` splits into `quality()/extension()/modifiers()/modifierList()` and builds a `renderList` of `RenderAction`s (`SET, MOVE, MOVEXHEIGHT, PUSH, POP, NOTE, ACCIDENTAL, STOPHALIGN, SCALE, PAREN`, `:125`); `HDegree{value, alter, HDegreeType UNDEF/ADD/ALTER/SUBTRACT}`.

**`RealizedHarmony`** (`realizedharmony.h:59`): `voicing()` (`Voicing`: `AUTO/ROOT_ONLY/CLOSE/DROP_2/SIX_NOTE/FOUR_NOTE/THREE_NOTE`), `duration()` (`HDuration`: `UNTIL_NEXT_CHORD_SYMBOL/STOP_AT_MEASURE_END/SEGMENT_DURATION`), `literal()` (use only the listed notes), `notes()` → `PitchMap` = `std::multimap<pitch, tpc>`, lazily rebuilt (`valid()` = `!m_dirty && m_harmony`); `update(rootTpc, bassTpc, transposeOffset)`, `generateNotes(...)`, `getActualDuration(utick, type)`.

**`FiguredBass`** (`figuredbass.h:246`, `final`, `TextBase`, owner `Segment`) holds `items()` (`vector<FiguredBassItem*>`, one per stacked line), `ticks()` (`Fraction` duration for continuation lines), `onNote()` (on a note vs between notes), static `addFiguredBassToSegment(seg, track, extTicks, &isNew)`, `regenerateText()` (normalised text used while editing), `nextFiguredBass()`, `additionalContLineX(y)`. Header architecture note (`:32-68`): the **text** is what is edited; each line is parsed into a `FiguredBassItem` (never edited directly, derived from `EngravingItem`; the header comment says it "returns INVALID as type" but the class actually declares `DECLARE_CLASSOF(ElementType::FIGURED_BASS_ITEM)`, `:94` — stale comment). **`FiguredBassItem`** (`:91`): `prefix()/suffix()` (`Modifier`: `NONE/DOUBLEFLAT/FLAT/NATURAL/SHARP/DOUBLESHARP/CROSS/BACKSLASH/SLASH`), `digit()` (0-9; `FBIDigitNone=-1`), `contLine()` (`ContLine`: `NONE/SIMPLE/EXTENDED`), `parenth1()…parenth5()` (`Parenthesis`: `NONE/ROUNDOPEN/ROUNDCLOSED/SQUAREDOPEN/SQUAREDCLOSED`, 5 slots between the 4 parts), `ord()` (line ordinal), `parse(String&)`, `normalizedText()`, per-field `undoSet*`; `LayoutData{ displayText, textWidth }`. Font tables: `FiguredBassFont` + `FBFonts()`/`readConfigFile()`.

**`FretDiagram`** (`fret.h:137`, `final`, `EngravingItem`, owner `Segment`; strings 0-indexed, **frets 1-indexed**, `:134`):

| Member | Type | Meaning |
|---|---|---|
| `strings()/frets()/fretOffset()/maxFrets()` | `int` | geometry; `fretOffset` = starting fret number shown. |
| `dots()` | `DotMap` = `map<int string, vector<FretItem::Dot>>` | `Dot{fret, FretDotType NORMAL/CROSS/SQUARE/TRIANGLE, fingering, isPartOfSlurBarre}`. |
| `markers()` | `MarkerMap` = `map<int string, Marker>` | `Marker{FretMarkerType NONE/CIRCLE/CROSS}` (open/muted). |
| `barres()` | `BarreMap` = `map<int fret, Barre>` | `Barre{startString, endString}`. |
| `harmony()` | `Harmony*` | attached chord-name text (owned child). |
| `orientation()` | `Orientation` (def `VERTICAL`) | |
| `showNut()`, `userMag()` (0.1–10), `showFingering()`, `fingering()` | `bool/double/bool/vector<int>` | |
| `setDot/setBarre/setMarker/clear`, `undoSetFretDot/undoSetFretMarker/undoSetFretBarre/undoFretClear` | | mutators (the `undo*` forms create `FretDataChange`/`FretDot`/`FretMarker`/`FretBarre`/`FretClear` commands — `CommandType`, `undoablecommand.h:106-111`). |
| `patternFromDiagram()`, `harmoniesFromPattern`, `patternsFromHarmony`, `updateDiagram(harmonyName)`, `makeFromHarmonyOrFretDiagram` | | diagram↔chord-name database (`DiagramInfo{harmonyName, diagramXml, diagramPattern}`). |
| `isInFretBox()` | `bool` | lives in an `FBox` (fretboard-diagram frame). |

### 2.12 Instruments, drumset, strings, staff types, `Staff`, `Part`

**`Instrument`** (`instrument.h:262`) — a **value type**, not an `EngravingObject`. Owned by a `Part`'s `InstrumentList` (`std::map<const int, Instrument*>` keyed by tick, `instrument.h:417`; `instrument(tick)` / `setInstrument(inst, tick)`; main instrument at `Part::MAIN_INSTRUMENT_TICK`).

| Member | Type | Meaning |
|---|---|---|
| `id()`, `soundId()`, `musicXmlId()`, `family()/group()` | `String` | instrument id (from `instruments.xml`), sound id (playback), MusicXML id. |
| `instrumentLabel()` / `longName()/shortName()/number()/transposition()` | `InstrumentLabel` / `String` | staff-name text. |
| `trackName()` | `String` | mixer/MIDI track name. |
| `transpose()` | `Interval` | written→sounding transposition. |
| `minPitchA/maxPitchA/minPitchP/maxPitchP` | `int` | amateur / professional pitch ranges. |
| `useDrumset()/drumset()` | `bool` / `Drumset*` | percussion mapping. |
| `stringData()/hasStrings()` | `StringData` | fretted-instrument tuning. |
| `channel()` / `playbackChannel(idx, MasterScore*)` | `vector<InstrChannel*>` | MIDI channels (≥1). |
| `midiActions()`, `articulation()` | `vector<NamedEventList>`, `vector<MidiArticulation{name, descr, velocity, gateTime}>` | MIDI event sequences / articulation modifiers. |
| `clefType(staffIdx)` | `ClefTypeList` | default clef per staff of the part. |
| `trait()` | `Trait{name, TraitType Unknown/Tuning/Transposition/Course, isDefault, isHiddenOnScore}` | |
| `isPrimary()`, `singleNoteDynamics()`, `glissandoStyle()` | | |
| `Instrument::fromTemplate(InstrumentTemplate*)` | static | build from catalogue. |

`InstrChannel` (`:79`): `name()` (`DEFAULT_NAME`, `HARMONY_NAME`, `PALM_MUTE_NAME` constants), `synti()`, `color()`, `volume()/pan()/chorus()/reverb()`, `program()/bank()/channel()`, `userBankController()`, `initList()` (MIDI init events from `A` enum `HBANK…REVERB`), notifier/listener mechanism (`Prop` enum) with `PartChannelSettingsLink` mirroring main↔excerpt channels.

**`Drumset`** (`drumset.h:100`): fixed `DrumInstrument m_drums[128]` indexed by MIDI pitch (`DRUM_INSTRUMENTS=128`). `DrumInstrument` (`:52`): `name`, `notehead` (`NoteHeadGroup`; custom → `noteheads` array of `SymId` per `NoteHeadType`), `line` (staff line), `stemDirection`, `voice`, `shortcut`, `panelRow/panelColumn` (percussion-panel position), `variants` (`DrumInstrumentVariant{pitch, TremoloType, articulationName}`). Accessors: `isValid(pitch)`, `noteHead/noteHeads/line/voice/stemDirection/name/shortcut/variants(pitch)`, `pitchForShortcut`, `defaultPitchForLine(line)`, `nextPitch/prevPitch`, `findVariant(pitch, articulations, tremType)`, `percussionPanelColumns()` (def 8); `smDrumset` = global default.

**`StringData`** (`stringdata.h:55`): `stringList()` (`vector<instrString{pitch, open, startFret, useFlat}>`, **index 0 = lowest string**), `frets()`, `useFlats()`; conversions `convertPitch(pitch, pitchOffset|staff[,tick], &string, &fret [, CapoParams])`, `getPitch(string, fret, pitchOffset|staff[,tick])`, `fret(pitch, string, staff[,tick])`, `fretChords(Chord*)` (re-fret a chord's notes, with a family of private conflict-resolvers), static `pitchOffsetAt(staff[,tick[,string]])` (transposition+capo offset), `configBanjo5thString()/isFiveStringBanjo()/adjustBanjo5thFret()`.

**`StaffType`** (`stafftype.h:132`) — value type; one per tick-range per `Staff` (`StaffTypeList`). Common: `group()` (`StaffGroup`: `STANDARD/PERCUSSION/TAB`), `type()` (`StaffTypes` preset enum: `STANDARD, PERC_1LINE…PERC_5LINE, TAB_6SIMPLE…TAB_10SIMPLE`, `types.h:1331`), `lines()`, `stepOffset()`, `lineDistance()` (`Spatium`), `genClef()/genTimesig()/genKeysig()`, `showBarlines()`, `showLedgerLines()`, `stemless()`, `invisible()`, `isSmall()`, `userMag()` (0.1–10), `yoffset()`, `color()`, `noteHeadScheme()`, `instrumentLabelVisibility()`, `staffLabel()`. TAB-only: `durationFont*`, `fretFont*`, `genDurations()`, `linesThrough()`, `minimStyle()` (`TablatureMinimStyle`), `symRepeat()` (`TablatureSymbolRepeat`), `onLines()`, `showRests()`, `stemsDown()`, `stemThrough()`, `upsideDown()`, `showTabFingering()`, `useNumbers()`, `showBackTied()`, `fretString(fret, string, deadNote)`, `physStringToVisual/visualStringToPhys`. Statics: `preset(StaffTypes)`, `getDefaultPreset(group)`, `presetFromXmlName`, `initStaffTypes(color)`, `presets()`. Predicates `isTabStaff()/isDrumStaff()/isSimpleTabStaff()/isCommonTabStaff()`.

**`Staff`** (`staff.h:59`, `final`) — **an `EngravingItem`** (`Staff::Staff(Part*) : EngravingItem(ElementType::STAFF, parent)`, `staff.cpp:71`; the §0/§5 claim that it is an `EngravingObject`-only class is wrong for this tree). Time-varying data is keyed by tick:

| Member | Type | Meaning |
|---|---|---|
| `part()` / `idx()` / `rstaff()` / `id()` | `Part*` / `staff_idx_t` / idx within part / `ID` | |
| `clefType(tick)`, `clef(tick)`, `clefList()` | `ClefTypeList`/`ClefType`/`ClefList` (`m_clefs`) | `setClef(Clef*)`, `nextClefTick`, `currentClefTick`; `defaultClefType()`. |
| `keySigEvent(tick)`, `key(tick)`, `concertKey(tick)`, `keyList()` | `KeySigEvent`/`Key`/`KeyList` (`m_keys`) | `setKey(tick, ev)`, `prevKey`, `nextKeyTick`. |
| `timeSig(tick)`, `timeStretch(tick)`, `group(tick)` | `TimeSig*` (`m_timesigs` `map<int,TimeSig*>`) | `addTimeSig/removeTimeSig`; `group` = beam `Groups`. |
| `staffType(tick)` / `setStaffType` / `removeStaffType` | `StaffType*` (`m_staffTypeList`) | `lines(tick)`, `isTabStaff/isDrumStaff/isPitchedStaff(tick)`, `staffMag(tick)`, `spatium(tick)`. |
| `transpose(tick)`, `pitchOffset(tick)` | `Interval` / `int` | instrument + capo + ottava offsets (`updateOttava()`). |
| `channel(tick, voice)`, `swing(tick)`, `capo(tick)` | `m_channelList[VOICES]`, `m_swingMap`, `m_capoMap` (tick-keyed `std::map`s) | playback state per tick. |
| `show()`, `cutaway()`, `showIfEntireSystemEmpty()`, `hideWhenEmpty()` (`AutoOnOff`), `mergeMatchingRests()` (`AutoOnOff`) | | visibility policy. |
| `barLineSpan()/barLineFrom()/barLineTo()`, `hideSystemBarLine()` | `bool/int/int/bool` | bar-line span defaults. |
| `userDist()` | `Spatium` | user vertical distance. |
| `staffList()` / `primaryStaff()` / `isPrimaryStaff()` | linked-staff group | via the link list. |
| `playbackVoice(v)`, `visibilityVoices()`, `isVoiceVisible(v)` | `bool[VOICES]` | per-voice flags. |
| `isSystemObjectStaff()`, `hasSystemObjectsBelowBottomStaff()` | `bool` | system-object (tempo/rehearsal) display staves. |
| `undoSetShowMeasureNumbers(bool)`, `shouldShowMeasureNumbers()` | | `m_showMeasureNumbers` (`AutoOnOff`). |

**`Part`** (`part.h:61`, `EngravingObject`, `ElementType::PART`): `staves()`/`nstaves()`/`staff(idx)`/`insertStaff/removeStaff`, `instrument(tick)`/`instruments()`/`setInstrument(...)`/`removeInstrument`, `longName/shortName/instrumentName/instrumentId(tick)` (`tick = {-1,1}` ⇒ current), `midiProgram()/midiChannel()/midiPort()`, `show()/setShow` (shown in this score), `soloist()`, `partName()`, `trackRange()` (`TrackRange{startTrack,endTrack}`), `stringTunings()` (`map<int,StringTunings*>`, `stringData(tick, staffIdx)`), `harpDiagrams()` (`map<int,HarpPedalDiagram*>`, `currentHarpDiagram(tick)`), `hideWhenEmpty()` (`AutoOnOff`), `hideStavesWhenIndividuallyEmpty()`, `preferSharpFlat()`, `sharedPart()`, `masterPart()/originPart(track,tick)`, `lyricCount()/harmonyCount()`, `hasPitchedStaff/hasTabStaff/hasDrumStaff()`, `updateHarmonyChannels(...)`/`harmonyChannel()`.

### 2.13 Layout anchors and intermediate bases: `System`, `Page`, `SysStaff`, `MeasureNumberBase`, `StaffTextBase`

**`System`** (`system.h:99`, `final`, `EngravingItem`) — **owned by `Score`**, *placed* on a `Page` (`setOwnershipParent(Score*)` hides the base overload, `:125-128`; `page()`/`setPage()` is the layout placement, `m_page` "not owned, not copied", `:285`; `layoutParent()` → the page).

| Member | Type | Meaning |
|---|---|---|
| `measures()` / `first()/last()/firstMeasure()/lastMeasure()` | `vector<MeasureBase*>` (`m_ml`) | the measures/frames laid out here. `appendMeasure/removeMeasure`, `endTick()`. |
| `staves()` / `staff(idx)` | `vector<SysStaff*>` | one `SysStaff` per score staff (even hidden). |
| `brackets()` | `vector<Bracket*>` | drawn brackets. |
| `spannerSegments()` | `list<SpannerSegment*>` | spanner pieces belonging to this system. |
| `systemDividerLeft()/Right()` | `SystemDivider*` | |
| `leftMargin()` | `double` | room for names + brackets. |
| `systemHeight()`, `distance()` | `double` | layout results/temp. |
| `firstVisibleStaff()/nextVisibleStaff()/prevVisibleStaff()/lastVisibleStaff()`, `firstVisibleSysStaffOfPart(part)`, `visibleStavesOfPart(part)` | `staff_idx_t` | visibility-aware navigation. |
| `staffYpage(idx)` / `staffCanvasYpage(idx)` | `double` | staff top in page / canvas coordinates. |
| `searchStaff(y,…)`, `snap(tick, p)`, `snapNote(...)` | | hit-testing. |
| `topDistance/bottomDistance(staffIdx, SkylineLine)`, `minTop()/minBottom()`, `upSpacer/downSpacer` | | skyline spacing. |
| `isLocked()` / `systemLock()` (`RangeLock*`), `systemLockIndicators()`, `pageLockIndicator()` | | system/page locks (see `editsystemlocks.h`, `editpagelocks.h`). |
| `staffVisibilityIndicator()` | `StaffVisibilityIndicator*` | the "hidden staves" icon. |
| `LayoutData` (`:234`) | `useLongNames`, `instrumentNameOffset`, `firstColumnWidth/secondColumnWidth/totalNamesWidth`, `groupBracketsWidth` map, `partsWithGroupName` map | name-column layout. |

**`SysStaff`** (`system.h:48`, plain class): `bbox()` (staff-lines box), `y()` (= bbox.y + `m_yOff`), `yOffset()`, `yBottom()`, `skyline()`, `show()`, `instrumentNames()` (`unordered_map<InstrumentNameRole, InstrumentName*>`), `continuousDist()`, `saveLayout()/restoreLayout()`.

**`Page`** (`page.h:48`, `final`, `EngravingItem`): `systems()` (`vector<System*>`), `pageNumber()` (0-based `page_idx_t`) / `getDisplayPageNumber()` (honours page-number offset), `isOdd()`, margins `tm()/bm()/lm()/rm()`, `headerText(i)/footerText(i)` (`Text*`, `MAX_HEADERS`/`MAX_FOOTERS`), `items(RectF)`/`items(PointF)` (hit-test through the lazily rebuilt `BspTree`, `invalidateBspTree()`), `elements()`, `tbbox()` (tight bbox), `firstMeasure()/firstMeasureBase()/lastMeasureBase()`, `endTick()`, `isLocked()/pageLock()`. `pagePos()` is `(0,0)`.

**`MeasureNumberBase`** (`measurenumberbase.h:35`, `TextBase`): owner `Measure` (`measure()`); `isEditable()` false and `positionRelativeToNoteheadRest()` false — concrete types `MeasureNumber`, `MMRestRange`. **`StaffTextBase`** (`stafftextbase.h:41`, `TextBase`): playback-carrying texts (`StaffText`, `SystemText`, `PlayTechAnnotation`, `Capo`, `StringTunings`, `TripletFeel`, `StaveSharingLabel`). Members: `channelName(voice)` (`m_channelNames[4]`), `channelActions()` (`vector<ChannelActions{channel, midiActionNames}>`), `swing()/swingParameters()` (`SwingParameters{swingUnit, swingRatio}`), `capo()`, Aeolus organ stops (`setAeolusStop/aeolusStop/getAeolusStop`, `setAeolusStops()`, `m_aeolusStops[4]`), `segment()`; `added()/removed()` hooks call `Score::updateSwing()` when `swing()` is set (`stafftextbase.cpp:88-106`). `positionRelativeToNoteheadRest()` true. (`TextLineBase` is in §2.9.)

### 2.14 `TextBase` rich-text internals (`textbase.h`)

Text is stored as **XML-ish rich text** (`xmlText()`; `plainText()` strips tags; `setPlainText` escapes then `setXmlText`). The editable/layout model is **blocks → fragments**, rebuilt from `m_text` by `createBlocks()` (`textbase.h:491`) into `TextBase::LayoutData::blocks` (`vector<TextBlock>`, `:478`) — i.e. the parsed form lives in **layout data**, not in the item. `m_textInvalid` (`:582`) / `isTextInvalid()` mark that `m_text` must be regenerated (`genText()`) from the blocks after edits.

| Type (`line`) | Role | Key members |
|---|---|---|
| `CharFormat` (`:83`) | per-run character format | `style()` (`FontStyle` bits Bold/Italic/Underline/Strike), `valign()` (`VerticalAlignment`: `AlignUndefined/AlignNormal/AlignSuperScript/AlignSubScript`), `fontSize()`, `fontFamily()`; generic `formatValue(FormatId)`/`setFormatValue`. `FormatId`: `Bold, Italic, Underline, Strike, Valign, FontSize, FontFamily`; `FormatValue` = `variant<monostate,bool,int,double,String>`. |
| `TextFragment` (`:224`) | a run of text sharing one format | `format` (mutable `CharFormat`), `pos` (`PointF`, y relative to the block), `text`; `split(column)`, `font(const TextBase*)`, `columns()`, `changeFormat`. |
| `TextBlock` (`:258`) | one line | `fragments()` (`std::list<TextFragment>`), `shape()`, `boundingRect()`, `y()`, `lineSpacing()`, `eol()`; `insert(cursor, str)`, `remove(...)`, `split(column, cursor)`, `column(x, TextBase*)`, `xpos(col)`, `formatAt(col)`, `changeFormat(id, val, start, n)`; private `simplify()` merges equal-format neighbours. |
| `TextCursor` (`:127`) | editing caret + selection (owned by the `TextBase`, `m_cursor`) | `row()/column()` (caret) and `selectLine()/selectColumn()` (selection anchor), `hasSelection()`, `format()` (`CharFormat` to apply to typed text), `editing()/visible()`, `movePosition(MoveOperation, MoveMode, count)` (`Start/Up/StartOfLine/Left/WordLeft/End/Down/EndOfLine/NextWord/Right`; `MoveAnchor/KeepAnchor`), `selectWord()`, `selectedText(withFormat)`, `currentPosition()` (linear index), `selectionRange()`, `changeSelectionFormat(id,val)`, `updateCursorFormat()`. |

`TextBase` item-level properties: `align()` (`Align`), `position()` (`AlignH`), `frameType()` (`FrameType`: `NO_FRAME/RECTANGLE/CIRCLE`), `frameWidth/paddingWidth/frameRound` (`Spatium`), `bgColor/frameColor`, `textLineSpacing()` (def 1.0), `textStyleType()` (`TextStyleType` — the style preset that supplies defaults, see `style-system.md`), `fontStyle()/family()/size()` (virtual — `Harmony` overrides), `symbolSize()/symbolScale()` (music-symbol runs), `textAngle()`, `voiceAssignment()`, `direction()`, `centerBetweenStaves()`, `anchorToEndOfPrevious()`, `isTextLinkedToMaster` (`M_PROPERTY2`, def true, `:312`). Editing entry points (`startEdit/edit/endEdit/editInsertText/paste/deleteSelectedText`) manipulate blocks via the cursor and record undo through `TextEditUndoableCommand` (§3A.5).

### 2.15 Remaining element classes — member tables (minor / leftover families)

Every table below was written from a full read of the named header (`src\engraving\dom\`, 2026-11-08). "Owner" = the `ownershipParent()` type the accessor casts to. `M_PROPERTY2` members are listed as stated in the header. Enum value lists come from `types\types.h`. **There is no `tremolo.h`, no `StretchedBend`, and no `Fret*` element beyond `FretDiagram` in this tree** (§2.11): the old single `Tremolo` class is split into `TremoloSingleChord` + `TremoloTwoChord`, and the old stretched bend is gone (`Bend` is marked OBSOLETE, `bend.h:38-42`).

#### 2.15.1 Lyrics (`lyrics.h`)

**`Lyrics`** (`:38`, `final`, `TextBase`, owner `ChordRest` — `segment()` is `ownershipParent()->ownershipParent()`, `:59`).

| Member | Type | Meaning |
|---|---|---|
| `verse()/setVerse(int)`, `subtype()` | `int` (`m_verse`, "row index") | verse number; `subtype()` returns it; `isEven()` = `verse % 2`. |
| `syllabic()/setSyllabic()` | `LyricsSyllabic` (`SINGLE/BEGIN/END/MIDDLE`, `types.h:1372`) | hyphenation state. |
| `ticks()/setTicks()`, `endTick()` | `Fraction` (`m_ticks`) | `> 0` ⇒ draw a melisma underline to `tick()+ticks`. `TEMP_MELISMA_TICKS = Fraction::eps()` (`:51`) marks "underscore entered but only first chord spanned" — the header calls it "a horrible hack". |
| `separator()/setSeparator()` | `LyricsLine*` | the dash/melisma line that follows this syllable (owned child). `isMelisma()`. |
| `avoidBarlines()` | `bool` (def true) | per-item barline-avoidance. |
| `yRelativeToStaff()/setYRelativeToStaff(double)` | `double` | vertical position relative to the staff. |
| `needRemoveInvalidSegments()`, `removeInvalidSegments()` | `bool` | cleanup of stale separator segments. |
| `adjustPrevious()`, `removeFromScore()`, `paste(String)` | | edit helpers (split on typed `-`/`_`). |
| `allowTimeAnchor()` | `false` | |
| overrides | `getProperty/setProperty/propertyDefault`, `undoChangeProperty(Pid, v, flags)`, `reset()`, `triggerLayout()` | the style-driven props of `TextBase` plus lyrics-specific placement. `positionRelativeToNoteheadRest()` true. |

**`LyricsLine`** (`:130`, `SLine`, `anchor() = SEGMENT`; `isInSpannerMap()` **false**, `:159`): `lyrics()` (= `toLyrics(ownershipParent())`), `nextLyrics()/setNextLyrics()`, `isEndMelisma()` (true when the owner's `ticks()` is non-zero), `isDash()` = `!isEndMelisma()`; `getPropertyStyle(Pid)`. **`LyricsLineSegment`** (`:171`, `LineSegment`): `lyricsLine()`, virtuals `lyrics()/verse()/lyricsPlaceAbove()/lyricsAddToSkyline()/lineSpacing()/baseLineShift()/color()` (all delegate to the owning `Lyrics`); `propertyDelegate(Pid)`; `isEditAllowed` false; `LayoutData{ vector<LineF> dashes }` (`addDash/clearDashes`). **`PartialLyricsLine`** (`:216`, `final`, `LyricsLine`): a line with no owning `Lyrics` (`lyrics()` returns `nullptr`), **is** in the spanner map (`:250`); stores `isEndMelisma` (`bool`), `verse` (`int`), `centerBetweenStaves` (`AutoOnOff`, def `AUTO`); `findLyricsInPreviousRepeatSeg()`, `findAdjacentLyricsOrDefault()`. **`PartialLyricsLineSegment`** (`:258`) reads verse/place/colour from its line. `Pid::LYRICS_DASH_SYSTEM_START` (`LyricsDashSystemStart`: `STANDARD/UNDER_HEADER/UNDER_FIRST_NOTE`) and `LyricsStackingOrder` (`LYRICS_AFTER_PEDALS` < 5.0 / `LYRICS_BEFORE_DYNAMICS` ≥ 5.0) are in `types.h:1376-1385`.

#### 2.15.2 Arpeggio and chord bracket (`arpeggio.h`, `chordbracket.h`)

**`Arpeggio`** (`arpeggio.h:41`, `EngravingItem`, owner `Chord`).

| Member | Type | Meaning |
|---|---|---|
| `arpeggioType()/setArpeggioType()` | `ArpeggioType` (`NORMAL, UP, DOWN, BRACKET, UP_STRAIGHT, DOWN_STRAIGHT`, `types.h:839`) | glyph style; `subtype()` = `int(type)`. |
| `span()/setSpan(int)`, `endTrack()` | `int` (`m_span`, "how many voices", def 1) / `track_idx_t` (= `track()+span-1`) | multi-staff/voice reach; `crossStaff()`, `vStaffIdx()`. |
| `userLen1()/userLen2()` | `double` | user stretch of the top/bottom ends (grip-edited). |
| `Stretch()/setStretch()` | `double` (def 1.0) | |
| `playArpeggio()/setPlayArpeggio()` | `bool` (def true) | playback. |
| `findAndAttachToChords()`, `detachFromChords(strack, etrack)` | | bind to the covered chords (`Chord::spanArpeggio`). |
| `rebaseStartAnchor/rebaseEndAnchor(AnchorRebaseDirection UP/DOWN)` | | move the anchor chord by keyboard/drag. |
| grips | `gripsCount()` = 3, `defaultGrip()` = `MIDDLE`, `initialEditModeGrip()` = `END` | |
| `LayoutData` | `top, bottom, magS, maxChordPad, minChordX` (cache) + `symbols (SymIdList), symsBBox, arpeggioHeight` (out) | |

**`ChordBracket`** (`chordbracket.h:29`, `final`, derives `Arpeggio`, `ElementType::CHORD_BRACKET`): `hookLength()` (`Spatium`, def 1), `hookPos()` (`DirectionV`; `AUTO` = hooks both up and down), `rightSide()` (`bool`). Overrides `getProperty/setProperty/propertyDefault/reset`.

#### 2.15.3 Tremolo (`tremolosinglechord.h`, `tremolotwochord.h`)

`TremoloType` (`types.h:1260`): `INVALID_TREMOLO=-1`; one-note (repeat) `R8, R16, R32, R64, R128, R256, BUZZ_ROLL`; two-note (change) `C8 … C256`. `isTremoloTwoChord(type)` ⇔ `type >= C8`. `TremoloStyle` (`DEFAULT/TRADITIONAL/TRADITIONAL_ALTERNATE`) applies only to minim two-note tremolos in non-TAB staves. `TremoloChordType` (`TremoloNone/TremoloSingle/TremoloFirstChord/TremoloSecondChord`) tags a chord's role.

| Member | `TremoloSingleChord` (`final`, `EngravingItem`, owner `Chord`, `:35`) | `TremoloTwoChord` (`final`, **`BeamBase`**, owner `Chord`, `:37`) |
|---|---|---|
| `tremoloType()/setTremoloType()` | `TremoloType` (`m_tremoloType`, def `INVALID_TREMOLO`); `subtype()` = `int(type)` | same |
| `lines()` | `int` — beam-stroke count derived from the subtype | same |
| `durationType()/setDurationType(TDuration)`, `tremoloLen()` | `TDuration` / `Fraction` | same |
| `playTremolo()/setPlayTremolo()` | `bool` (def true) | same |
| `chordMag()`, `minHeight()` | `double` | same |
| `isBuzzRoll()` | `type == BUZZ_ROLL` | — |
| `chord1()/chord2()/setChords(c1,c2)` | — | `Chord*` ×2 — the two spanned chords (`m_chord1/m_chord2`); `chord()` is the owner. |
| `tremoloStyle()/setTremoloStyle()`, `customStyleApplicable()` | — | `TremoloStyle` |
| `beamFragment()/setBeamFragment()`, `beamPos()` (private `setBeamPos`) | — | `BeamFragment` (`m_beamFragment`, one fragment only — header comment: a vector like `Beam` would be needed if tremolos could cross systems), `PairF` |
| `chordBeamAnchor(ChordRest*, ChordBeamAnchorType)` | — | `PointF` — where the beam attaches on a chord. |
| `crossStaffBeamBetween()`, `minCRMove()/maxCRMove()`, `clearBeamSegments()`, `setDirection(DirectionV)` | — | `BeamBase` overrides. |
| `basePath(double stretch)`, `path()/setPath()`, `computeShape()` | `PainterPath` (the stroke glyph path; "for palettes") | same |
| edit | `isMovable()` true | `isMovable()`/`isEditable()` true; `gripsCount()`, `dragGrip`, `drag(EditData&)` |

Both override `scanElements` (they own no children but participate in the traversal) and `accessibleInfo`; `TremoloSingleChord` also overrides `spatiumChanged/localSpatiumChanged/styleChanged`. `Chord::tremoloSingleChord()`/`tremoloTwoChord()` (§2.4) point at them.

#### 2.15.4 Fermata (`fermata.h`)

**`Fermata`** (`:42`, `final`, `EngravingItem`, owner `Segment` — `segment()` = `toSegment(ownershipParent())`; also attached through `ChordRest::el()`/`BarLine::el()` lists). Members: `symId()/setSymId(SymId)` (`m_symId`, def `noSym`), `setSymIdAndTimeStretch(SymId)`, `fermataType()` (**derived** from the symId — `FermataType`: `Undefined=-1, VeryShort, Short, ShortHenze, Normal, Long, LongHenze, VeryLong`, `types.h:1188`), `subtype()`; `timeStretch()/setTimeStretch(double)` (`m_timeStretch`, def −1.0 = use per-type default; playback pause multiplier); `play()/setPlay(bool)` (def true); `mag()`, `defaultPosSid()` (per-type style position), `resetProperty(Pid)`, `measure()/system()/page()`.

#### 2.15.5 Accidental (`accidental.h`)

**`AccidentalType`** (`:36`, `unsigned char`, "keep in sync with `accList` in accidentals.cpp") is a **long** enum: `NONE, FLAT, NATURAL, SHARP, SHARP2, FLAT2, SHARP3, FLAT3, NATURAL_FLAT, NATURAL_SHARP, SHARP_SHARP`, then microtonal families — Gould arrows, Stein-Zimmermann, Arel-Ezgi-Uzdilek (AEU), Extended Helmholtz-Ellis (1-3 arrows ±), septimal/undecimal/tridecimal commas, equal-tempered 12-EDO variants, 17/19/23/31/53-EDO, `EQUALS_ALMOST/EQUALS/TILDE`, Persian `SORI/KORON`, Wyschnegradsky twelfths, (Spartan) Sagittal, Turkish folk commas — ending at `END`. `isMicrotonal(t)` ⇔ `t > FLAT3`. `AccidentalBracket` (`NONE/PARENTHESIS/BRACKET/BRACE`; BRACE deprecated, kept for compatibility). `AccidentalRole` (`AUTO` layout-created / `USER`).

**`Accidental`** (`:232`, `final`, `EngravingItem`, owner `Note`; ctor parent is `DummyParentOr<EngravingItem>` because ornament/ambitus accidentals are owned elsewhere).

| Member | Type | Meaning |
|---|---|---|
| `accidentalType()/setAccidentalType()` | `AccidentalType` | `subtype()` = `int(type)`. `setSubtype(AsciiStringView)` parses a name. |
| `role()/setRole()` | `AccidentalRole` (def `AUTO`) | `USER` ⇒ explicitly entered, not removed by re-spelling. `startEdit` clears `generated`. |
| `bracket()/setBracket()`, `parentNoteHasParentheses()` | `AccidentalBracket` (def `NONE`) / `bool` | |
| `isSmall()/setSmall()` | `bool` | cue-size. |
| `symId()`, `line()`, `note()` | `SymId`, `int`, `Note*` | glyph, staff line, owning note. |
| `stackingOrderOffset()/setStackingOrderOffset(int)`, `stackingOrder()` | `int` | user nudge added to the layout `stackingNumber`. |
| statics | `subtype2value(type)` → `AccidentalVal` (pitch offset), `subtype2symbol`, `subtype2name`, `value2subtype`, `value2MicrotonalSubtype(val, quarterOff)`, `name2subtype`, `subtype2centOffset`, `centOffset2Subtype` | the converters used by `Note::setTpcFromPitch` / read-write. Free fn `sym2accidentalVal(SymId)`. |
| `LayoutData` | `syms (vector<Sym{sym,x,y}>)`, `stackingNumber`, `verticalSubgroup`, `column`, `octaves`, `seconds` (all `ld_field`) | column-stacking algorithm output; `isValid()` requires non-empty `syms`. |

`AccidentalVal` (`types.h:1162`): `SHARP3=3 … NATURAL=0 … FLAT3=-3`. `AccidentalState` (the per-measure tracker) is in `key.h` (§2.10).

#### 2.15.6 The frame family (`box.h`)

`Box` is a **`MeasureBase`** (so frames live in the same spine as measures, §2.2) and is `Box(const ElementType&, Score*)` — owned directly by the `Score`.

**`Box`** (`:36`, abstract-ish base; no `DECLARE_CLASSOF`):

| Member | Type | Meaning |
|---|---|---|
| `boxWidth()/boxHeight()` | `Spatium` | "only valid for HBox" / "only valid for VBox" (`:94-95`). |
| `topGap()/bottomGap()` | `Spatium` | distance from previous system / to next system (left/right border for an HBox); initialised from `Sid::systemFrameDistance` / `Sid::frameSystemDistance`. |
| `leftMargin/rightMargin/topMargin/bottomMargin` | `double` (mm) | inner margins. |
| `isAutoSizeEnabled()` | `bool` (def true) | |
| `contentRect()`, `isTitleFrame()`, `copyValues(Box*)` | `RectF`, `bool` | |
| edit | `isEditable` true, 1 grip (`START`); `edit/startDragGrip/dragGrip`; `acceptDrop/drop/add`; `canBeExcludedFromOtherParts()` true; `manageExclusionFromParts(bool)` | |

**`HBox`** (`:112`, `final`, `ElementType::HBOX`): `createSystemHeader()` (`bool`, def true — whether the system header (clef/key) is generated after it), `drag(EditData&)`, `isMovable()`, `computeMinWidth()`.

**`VBox`** (`:145`, `ElementType::VBOX`; has a protected `VBox(const ElementType&, Score*)` so subclasses reuse it): `minHeight()/maxHeight()`, `paddingToNotationAbove()/paddingToNotationBelow()` (`Spatium`); overrides `add`, `acceptDrop`, `startDragGrip`, `gripsPositions`.

**`TBox`** (`:249`, `ElementType::TBOX`, derives `VBox`): owns one `Text* m_text` (`text()`; `scanElements/add/remove` manage it, `drop()` accepts text items); `needStartEditingAfterSelecting()` false.

**`FBox`** (`:186`, `ElementType::FBOX`, derives `VBox`) — fretboard-diagram frame:

| Member | Type | Meaning |
|---|---|---|
| `textScale()/diagramScale()` | `double` | |
| `columnGap()/rowGap()` | `Spatium` | |
| `chordsPerRow()` | `int` | |
| `contentHorizontalAlignment()` | `AlignH` (def `HCENTER`) | |
| `addAtIdx(FretDiagram*, idx)`, `add(EngravingItem*)`, `init()` | | build the diagram list. |
| `diagramsOrder()/reorderElements(StringList)/undoReorderElements(StringList)` | `StringList` (`m_diagramsOrderInScore`) | user ordering by chord name. |
| `needsRebuild()/setNeedsRebuild(bool)` | `bool` | regenerate diagrams from the score's harmonies. |

#### 2.15.7 Guitar bends, hammer-on/pull-off, tapping (`guitarbend.h`, `hammeronpulloff.h`, `tapping.h`)

**`GuitarBend`** (`guitarbend.h:50`, `final`, `SLine`, `anchor() = NOTE`; `isInSpannerMap()` **false**; `allowTimeAnchor()` false). `M_PROPERTY2`s (`:55-59`): `direction` (`DirectionV`, def `AUTO`), `bendAmountInQuarterTones` (`int`, def 4), `showHoldLine` (`GuitarBendShowHoldLine`: `AUTO/SHOW/HIDE`), `startTimeFactor` (`float`, 0), `endTimeFactor` (`float`, 1).

| Member | Type | Meaning |
|---|---|---|
| `bendType()/setBendType()` | `GuitarBendType` (`BEND, PRE_BEND, GRACE_NOTE_BEND, SLIGHT_BEND, DIVE, PRE_DIVE, DIP, SCOOP`, `:30`) | |
| `startNote()/endNote()`, `startNoteOfChain()` | `Note*` | the two notes (the end note is created by `createEndNote(Note*, GuitarBendType)`, static). |
| `isReleaseBend()/isFullRelease()/angledPreBend()/isDive()/isFullReleaseDive()` | `bool` | derived classification. |
| `computeBendAmount()`, `totBendAmountIncludingPrecedingBends()`, `computeBendText()`, `changeBendAmount(bend, startBend)`, `setEndNotePitch(pitch, qtOffset)` | | `SLACK_BEND_AMOUNT = -33` (`:95`). |
| `isInvalid()`, `isBorderlineUnplayable()`, `computeIsInvalidOrNeedsWarning()` | `bool` | validity flags (warnings in TAB). |
| `holdLine()/setHoldLine(GuitarBendHold*)`, `updateHoldLine()` | `GuitarBendHold*` | the dashed hold line to the next note. |
| `findPrecedingBend()`, `findFollowingPreBendOrDive()`, `overlappingBendOrDive()`, `findOverlappingWhammyBar(tickA, tickB)` | | chain/overlap queries. |
| `diveTabPos()` | `DirectionV` | where a dive is drawn on TAB. |
| `dipVibratoType()` | `VibratoType` | vibrato during a dip. |
| `isSlack()`, `targetTimeFactor()/setTargetTimeFactor(float)` | `bool`, `std::optional<float>` | |
| statics | `fixNotesFrettingForGraceBend/StandardBend`, `adaptBendsFromTabToStandardStaff(Staff*)`, `bendTypeFromActionIcon(ActionIconType)` | |
| `LayoutData` | `up, isInside, bendDigit (String), diveLevels (set<int>), aboveStaff` | |

**`GuitarBendSegment`** (`:177`, `LineSegment`): `guitarBend()`, `vertexPointOff()` (user offset of the curve vertex), `bendText()/setBendText(GuitarBendText*)`, `lineWidth()`, `isUserModified()`, `gripsCount()`; `LayoutData{ vertexPoint, path (PainterPath), arrow (PolygonF) }`. **`GuitarBendHold`** (`:248`, `final`, `SLine`, owner = the `GuitarBend`; `guitarBend()`, `startNote()/endNote()`, `lineWidth()`) and **`GuitarBendHoldSegment`** (`:278`): `startBendSeg()/endBendSeg()`, `dashLength()` (def 3.0), `LayoutData{ SymIdList symIds }`. **`GuitarBendText`** (`:320`, `final`, `TextBase`, parent = a `GuitarBendSegment`): `isEditable` false, `positionRelativeToNoteheadRest` false.

**Hammer-on/pull-off** (`hammeronpulloff.h`): **`HammerOnPullOff`** (`:111`, `final`, `Slur` — `isSlur()` is true) with `newSlurTieSegment()` → `HammerOnPullOffSegment`. **`HammerOnPullOffSegment`** (`:68`, `final`, `SlurSegment`): owns `hopoText()` (`vector<HammerOnPullOffText*>`, one per start/end chord region, rebuilt by `updateHopoText()`), `isValid()`, `isUserModified()`. **`HammerOnPullOffText`** (`:28`, `final`, `TextBase`, parent = the segment): `startChord()/endChord()` (`Chord*`), `isHammerOn()` (`bool`; else pull-off), `isValid()`, `isEditAllowed` false.

**Tapping** (`tapping.h`): **`Tapping`** (`:34`, **derives `Articulation`**, owner `ChordRest`): `hand()` (`TappingHand`: `INVALID=-1/LEFT/RIGHT`), `text()` (`TappingText*`), `halfSlurAbove()/halfSlurBelow()` (`TappingHalfSlur*`), `lhShowItems()` (`LHTappingShowItems`: `HALF_SLUR/SYMBOL/BOTH`), `mag()`, `styleChanged()`. Related enums `LHTappingSymbol` (`DOT/CIRCLED_T`), `RHTappingSymbol` (`T/PLUS`). **`TappingHalfSlur`** (`:97`, `Slur`; `isInSpannerMap()` **false**; `tapping()`, `isHalfSlurAbove()`), **`TappingHalfSlurSegment`** (`:84`, `SlurSegment`), **`TappingText`** (`:120`, `final`, `TextBase`, owner `Tapping`; `positionRelativeToNoteheadRest` true).

**Other guitar line classes** (`ChordTextLineBase` = a `TextLineBase` with `anchor() = SEGMENT` and `doComputeEndElement()`, `chordtextlinebase.h:33`). `LetRing`, `PalmMute`, `Rasgueado`, `HarmonicMark`, `PickScrape` each derive **`ChordTextLineBase`** (correcting the shorthand in the §3 table), `final`, each with a same-header `…Segment : TextLineBaseSegment` exposing only the typed back-pointer (`letRing()/palmMute()/rasgueado()/harmonicMark()/pickScrape()`); the line classes override `createLineSegment/propertyDefault/defaultPosSid` (+ `getPropertyStyle` except `PalmMute`). `PalmMute::setChannel()` switches the playback channel; `HarmonicMark` adds a private `String m_text` and overrides `setProperty`. **`WhammyBar`** (`whammybar.h:54`, `final`) is also a `ChordTextLineBase` (carries the whammy-bar text; overlapped by `GuitarBend::findOverlappingWhammyBar`). **`Vibrato`** (`vibrato.h:63`, `final`, **plain `SLine`**, `anchor` SEGMENT): `vibratoType()` (`VibratoType`: `NONE=-1, GUITAR_VIBRATO, GUITAR_VIBRATO_WIDE, VIBRATO_SAWTOOTH, VIBRATO_SAWTOOTH_WIDE`), `segment()`, `linePos(Grip, System**)`; **`VibratoSegment`**: `symbols()` (`SymIdList`), `symbolLine(start, fill[, end])`. **`TextLine`** (`textline.h:56`, `final`, `TextLineBase`; `TextLine(parent, bool system=false)`): `initStyle()`, `allowTimeAnchor()`, `undoChangeProperty` override; **`TextLineSegment`** `initStyle()`, `propertyDelegate`. **`NoteLine`** (`noteline.h:45`, `final`, `TextLineBase`, `anchor() = NOTE`, **not** in the spanner map): `lineEndPlacement()` (`NoteLineEndPlacement`: `LEFT_EDGE/OFFSET_ENDS`, def `OFFSET_ENDS`), `enforceMinLength()` (`!= LEFT_EDGE`); **`NoteLineSegment`**: `noteLine()`, `propertyDelegate`. **`GradualTempoChange`** (`gradualtempochange.h:33`, `TextLineBase` directly, `anchor` SEGMENT): `tempoChangeType()` (`GradualTempoChangeType`: `Undefined=-1, Accelerando, Allargando, Calando, Lentando, Morendo, Precipitando, Rallentando, Ritardando, Smorzando, Sostenuto, Stringendo`), `easingMethod()` (`ChangeMethod`), `tempoChangeFactor()` (`double`, from `std::optional<float>`), `snapToItemAfter()` (`bool`, def true), `adjustForRehearsalMark(bool start)`; **`GradualTempoChangeSegment`**: `tempoChange()`, `findElementToSnapBefore()` (→ segment), `findElementToSnapAfter()` (→ `TempoText*`).

#### 2.15.8 Tempo, expression, markers and jumps (`expression.h`, `tempotext.h`, `marker.h`, `jump.h`)

**`Expression`** (`expression.h:30`, `final`, `TextBase`, owner `Segment`): one `M_PROPERTY` `snapToDynamics` (`bool`); `snappedDynamic()` (`Dynamic*`), `computeDynamicExpressionDistance(Dynamic*)`, `getDragGroup(...)`, `acceptDrop/drop`, `mapPropertiesFromOldExpressions(StaffText*)` (import of pre-Expression text), `hasVoiceAssignmentProperties()` true, `positionRelativeToNoteheadRest()` true.

**`TempoText`** (`tempotext.h:45`, `final`, `TextBase`, owner `Segment`):

| Member | Type | Meaning |
|---|---|---|
| `tempoTextType()` | `TempoTextType` (`NORMAL, A_TEMPO, TEMPO_PRIMO`) | `isNormal/isATempo/isTempoPrimo`. |
| `tempo()/setTempo()`, `tempoBpm()` | `BeatsPerSecond` (`m_tempo`) / `double` | the playback tempo, **quarter-note beats per second**. |
| `isRelative()`, `relativeValue()`, `setRelative(double)` | `bool` / `double` | a relative tempo (e.g. "a tempo" ×factor). |
| `followText()` | `bool` (def false) | parse the displayed text to derive the tempo (`findTempoDuration`, static). |
| `playTempoText()` | `bool` (def true) | |
| `duration()` | `TDuration` | note value of the metronome mark. |
| statics | `findTempoDuration(String, &len, &dur)`, `duration2tempoTextString`, `duration2userName`, `DEFAULT_SYM_SIZE_RATIO = 5/3` | |
| `updateTempo()`, `tempoInfo()`, `commitText()` (protected) | | re-derive tempo after edits. |

**`Marker`** (`marker.h:35`, `final`, `TextBase`, owner `Measure`): `markerType()` (`MarkerType`: `SEGNO, VARSEGNO, CODA, VARCODA, CODETTA, FINE, TOCODA, TOCODASYM, DA_CODA, DA_DBLCODA, USER`, `types.h:1311`; def `FINE`), `label()` (`String`, def `u"fine"` — the name a `Jump` refers to), `centerOnSymbol()` (`bool`, def true), predicates `isSegno()/isCoda()/isToCoda()/isRightMarker()` (`RIGHT_MARKERS` = `FINE, TOCODA, TOCODASYM, DA_CODA, DA_DBLCODA`), `symbolString()`. Table `markerTypeTable` (`vector<MarkerTypeTableItem{type, text, label, rightAligned}>`, `:104-111`).

**`Jump`** (`jump.h:42`, `final`, `TextBase`, owner `Measure`): `jumpType()` (`JumpType`: `DC, DC_AL_FINE, DC_AL_CODA, DS_AL_CODA, DS_AL_FINE, DS, DC_AL_DBLCODA, DS_AL_DBLCODA, DSS, DSS_AL_CODA, DSS_AL_DBLCODA, DSS_AL_FINE, USER`, `types.h:1295`), `jumpTo()`, `playUntil()`, `continueAt()` (`String`s naming `Marker` labels), `playRepeats()` (`bool`, def false). Table `jumpTypeTable` (`JumpTypeTableItem{type, text, jumpTo, playUntil, continueAt}`). Both `Marker` and `Jump` override `nextSegmentElement/prevSegmentElement` and have `positionRelativeToNoteheadRest()` false. These feed `RepeatList` (playback).

#### 2.15.9 Symbols and images (`bsymbol.h`, `symbol.h`, `image.h`)

**`BSymbol`** (`bsymbol.h:37`, `EngravingItem`, abstract base of `Symbol`/`FSymbol`/`Image`; owner `Segment` by default): `align()/setAlign(Align)` (def `{LEFT, BASELINE}`), `leafs()` (`vector<EngravingItem*>` — items **attached** to the symbol, e.g. another symbol dropped on it; `add/remove/scanElements` manage them), `pagePos()/canvasPos()` (override to follow the parent), `dragAnchorLines()`, `acceptDrop/drop/drag`.

**`Symbol`** (`symbol.h:46`, `BSymbol`, `ElementType::SYMBOL`; **also the base of** `Hook`, `NoteHead`, `SystemDivider`): `sym()/setSym(SymId, font)` (`SymId`, def `noSym`; `subtype()` = `int(sym)`), `scoreFont()`, `symbolsSize()` (`double`, def 1.0), `symAngle()` (`double`, def 0), `symName()`, `baseLine()` = 0, `segment()` virtual. **`FSymbol`** (`:93`, `final`, `BSymbol`, `ElementType::FSYMBOL`): a font glyph by code point — `font()` (`draw::Font`), `code()` (`char32_t`), `toString()`.

**`Image`** (`image.h:52`, `final`, `BSymbol`; has a `GlobalInject<IImageProvider>`):

| Member | Type | Meaning |
|---|---|---|
| `imageType()` | `ImageType` (`NONE/RASTER/SVG`) | |
| `storePath()/setStorePath()`, `storeItem()` | `std::string`, `ImageStoreItem*` | key in the score's image store (embedded in the `.mscz`). |
| `loadFromStore/loadFromFile/loadFromData(suffix, bytes)`, `init()` | | decoders. |
| `size()/setSize(SizeF)`, `sizeIsSpatium()` | `SizeF` (absolute or spatium per flag) | |
| `lockAspectRatio()`, `autoScale()` | `bool` (def false/false) | `autoScale` ⇒ fill the parent frame. |
| `isImageFramed()`, `imageAspectRatio()`, `imageSize()`, `updateImageHeight/Width`, `pixel2size/size2pixel` | | geometry helpers. |
| `rasterImage()`, `svgRenderer()`, `isValid()` | `shared_ptr<Pixmap>`, `SvgRenderer*` | decoded data. |
| `buffer()/setBuffer`, `dirty()/setDirty` | `mutable Pixmap`, `mutable bool` | cached render. |
| grips | 2 (`defaultGrip` = `Grip(1)`), `needStartEditingAfterSelecting` true | resize grips. |

#### 2.15.10 Legacy bend and tremolo bar (`bend.h`, `tremolobar.h`)

**`Bend`** (`bend.h:53`, `final`, `EngravingItem`, owner `Note`) — **OBSOLETE** (pre-4.2; replaced by `GuitarBend`, `bend.h:38-42`). `BendType` (`BEND, BEND_RELEASE, BEND_RELEASE_BEND, PREBEND, PREBEND_RELEASE, CUSTOM`). Members: `points()` (`PitchValues` = vector of `PitchValue{time, pitch, vibrato}`), `playBend()` (`bool`, def true), `fontFace()/fontSize()/fontStyle()`, `lineWidth()` (`Spatium`), `font(double)`; `LayoutData{ notePos, noteWidth, noteHeight }`. Kept for reading old files. **`TremoloBar`** (`tremolobar.h:47`, `final`, `EngravingItem`): `TremoloBarType` (`DIP, DIVE, RELEASE_UP, INVERTED_DIP, RETURN, RELEASE_DOWN, CUSTOM`), `points()` (`PitchValues`), `userMag()` (`double`, 0.1-10, def 1.0), `lineWidth()` (`Spatium`), `play()` (`bool`, def true); `LayoutData{ PolygonF polygon }`.

#### 2.15.11 Staff / system / play-technique annotations (`stafftext.h`, `systemtext.h`, `rehearsalmark.h`, `playtechannotation.h`, `capo.h`, `stringtunings.h`, `tripletfeel.h`, `stavesharinglabel.h`, `soundflag.h`, `playcounttext.h`, `instrchange.h`, `harppedaldiagram.h`)

Base `StaffTextBase` members are in §2.13. Per-class additions:

| Class (`file:line`) | Base / owner | Members |
|---|---|---|
| **`StaffText`** (`stafftext.h:29`, `final`) | `StaffTextBase` / `Segment` | `soundFlag()/setSoundFlag(SoundFlag*)/hasSoundFlag()` (an owned `SoundFlag` child, `m_soundFlag`); `add/remove/scanElements/setTrack` propagate to it; `linkedClone()`. Ctor default `TextStyleType::STAFF`. |
| **`SystemText`** (`systemtext.h:33`) | `StaffTextBase` / `Segment` | ctor `(parent, TextStyleType::SYSTEM, ElementType type = SYSTEM_TEXT)` — subclassed by `TripletFeel`; `canBeExcludedFromOtherParts()` true. System-level (cloned to the top staff of every score by `undoAddElement`, §3A.3). |
| **`TripletFeel`** (`tripletfeel.h:42`, `final`) | `SystemText` | `getTripletFeelType()` (`TripletFeelType`: `NONE, TRIPLET_8TH, TRIPLET_16TH, DOTTED_8TH, DOTTED_16TH, SCOTTISH_8TH, SCOTTISH_16TH`); `setTripletProperty()` fills the swing parameters from private constants `eightDivision=2, sixteenthDivision=4, tripletRatio=66, dottedRatio=75, scottishRatio=25, zeroRatio=0`. |
| **`RehearsalMark`** (`rehearsalmark.h:33`, `final`) | `TextBase` / `Segment` | `type()` (`RehearsalMark::Type`: `Main`, `Additional`); `allowTimeAnchor()` false; `positionRelativeToNoteheadRest()` false; `styleChanged()` re-applies the type style (`applyTypeStyle`). System-level type. |
| **`PlayTechAnnotation`** (`playtechannotation.h:30`, `final`) | `StaffTextBase` / `Segment` | `techniqueType()` (`PlayingTechniqueType`: `Undefined=-1, Natural, Pizzicato, Open, Mute, Tremolo, Detache, Martele, ColLegno, SulPonticello, SulTasto, Vibrato, Legato, Distortion, Overdrive, Harmonics, JazzTone`, then handbell techniques `HandbellsSwing/SwingUp/SwingDown/Echo1/Echo2/Damp/LV/R`), `playPlayTechAnnotation()` (`bool`, def true), `isHandbellsSymbol()` (⇒ not editable). |
| **`Capo`** (`capo.h:28`, `final`) | `StaffTextBase` / `Segment` | `params()` (`CapoParams{ ignoredStrings (unordered_set<string_idx_t>), fretPosition (int), transposeMode (PLAYBACK_ONLY/STANDARD_ONLY/TAB_ONLY), active (bool) }`, `types.h:1477`), `shouldAutomaticallyGenerateText()`, `generateText(stringCount)`; `added()/removed()` both call `Score::updateCapo()` (`capo.cpp:182-198`), which rebuilds the staff capo maps (`Staff::capo`, §2.12). |
| **`StringTunings`** (`stringtunings.h:30`, `final`) | `StaffTextBase` / `Segment` | `stringData()/setStringData()/undoStringData()` (`StringData`, §2.12), `preset()` (`String` id), `visibleStrings()` (`vector<string_idx_t>`), `noStringVisible()`, `updateText()`, `screenReaderInfo()`. Registered in `Part::stringTunings()` (§2.12). |
| **`StaveSharingLabel`** (`stavesharinglabel.h:27`, `final`) | `StaffTextBase` / `Segment` | no data members; `isEditAllowed` false; style `TextStyleType::STAVE_SHARING`. |
| **`SoundFlag`** (`soundflag.h:30`, `final`) | `EngravingItem` (not text), parent `DummyParentOr<EngravingItem>` (normally a `StaffText`) | `soundPresets()` (`StringList`), `playingTechnique()` (`String` code), `play()` (`bool`, def true), `applyToAllStaves()` (`bool`, def true), `clear()`, `shouldHide()`, `undoChangeSoundFlag(presets, technique)`, `iconCode()/iconFont()/iconBackgroundColor()`; `canBeExcludedFromOtherParts()` false. |
| **`PlayCountText`** (`playcounttext.h:28`, `final`) | `TextBase` / `Segment` (its `barline()` finds the bar line) | `playCountTextSetting()` (`AutoCustomHide`: `AUTO/CUSTOM/HIDE`), `playCountCustomText()` (`String`), `endEdit`, `allowTimeAnchor()` false; default style `REPEAT_PLAY_COUNT`. System-level type. |
| **`InstrumentChange`** (`instrchange.h:37`, `final`) | `TextBase` / `Segment` | `instrument()` (`Instrument*`; "Staff holds ownership if part of score"), `setupInstrument(Instrument*, updateText)`, `keySigs(all)`, `clefs()`, `init()` (`bool`: set by user), `placeMultiple()` false. |
| **`HarpPedalDiagram`** (`harppedaldiagram.h:46`, `final`) | `TextBase` / `Segment` | `getPedalState()/setPedalState(array<PedalPosition,7>)` (`PedalPosition`: `FLAT/NATURAL/SHARP/UNSET`; indexed by `HarpStringType` `D,C,B,E,F,G,A`; `HARP_STRING_NO = 7`), `setPedal(string, pedal)`, `isDiagram()` (diagram vs text-form), `createDiagramText()/updateDiagramText()`, `playableTpcs()/isTpcPlayable(tpc)`, `undoChangePedalState(...)`; registered in `Part::harpDiagrams()`. `isEditable` false. |

#### 2.15.12 Fingering, sticking, breath, ambitus, chord line, dots, ledger lines, dead-slap (`fingering.h`, `sticking.h`, `breath.h`, `ambitus.h`, `chordline.h`, `notedot.h`, `ledgerline.h`, `deadslapped.h`)

| Class (`file:line`) | Base / owner | Members |
|---|---|---|
| **`Fingering`** (`fingering.h:37`, `final`) | `TextBase` / `Note` | `note()`, `layoutType()` (`ElementType` — returns `CHORD` or `NOTE`, i.e. which kind of item it is laid out against, `fingering.cpp:70-78`), `calculatePlacement()` (`PlacementV`), `isOnCrossBeamSide()`; ctor flag `ElementFlag::ON_STAFF`; `positionRelativeToNoteheadRest` false. |
| **`Sticking`** (`sticking.h:34`, `final`) | `TextBase` / `Segment` | `segment()/measure()`; no state (percussion sticking text "R/L"); `positionRelativeToNoteheadRest` true. |
| **`Breath`** (`breath.h:46`, `final`) | `EngravingItem` / `Segment` | `symId()/setSymId()` (def `breathMarkComma`; `subtype()` = `int(symId)`), `pause()/setPause(double)` (playback pause), `isCaesura()`; static `BREATH_LIST` = `vector<BreathType{ SymId id, bool isCaesura, double pause }>`; `mag()`. |
| **`Ambitus`** (`ambitus.h:38`, `final`) | `EngravingItem` / `Segment` | pitch range: `topPitch()/bottomPitch()`, `topTpc()/bottomTpc()` (+ setters with `applyLogic`), `topOctave()/bottomOctave()`; `noteHeadGroup()` (def `HEAD_NORMAL`), `noteHeadType()` (def `HEAD_AUTO`), `direction()` (`DirectionH`, def `AUTO`), `hasLine()` (def true), `lineWidth()` (`Spatium`, def 0.12 sp), `topAccidental()/bottomAccidental()` (owned `Accidental*`s, `scanElements/remove` handle them), `noteHead()`, `headWidth()`; `estimateRanges()` (private) scans the staff to the next section break; `LINEOFFSET_DEFAULT = 0.8 sp`; statics `accidentalType(tpc, Key)`, `staffLine(tpc, pitch, ClefType)`; `LayoutData{ topPos, bottomPos, line }`. |
| **`ChordLine`** (`chordline.h:44`, `final`) | `EngravingItem` / `Chord` | `chordLineType()` (`ChordLineType`: `NOTYPE, FALL, DOIT, PLOP, SCOOP`), `isStraight()`, `isWavy()`, `note()` (the note it hangs from), `playChordLine()` (def true), the 4-point bezier model `Point{START,BEZIER1,BEZIER2,END}` with `defaultPoints()` (spatium) + `bezierOffsets()` (absolute user offsets; `bezierOffsetPid(Point)`), `isToTheLeft()` (PLOP/SCOOP), `isBelow()` (SCOOP/FALL), `waveSym()`; grips = `gripsCount()` (default = last); `LayoutData{ PainterPath path }`. |
| **`NoteDot`** (`notedot.h:37`, `final`) | `EngravingItem` / `Note` **or** `Rest` | `note()` (null if parent is a rest), `rest()` (null if parent is a note), `elementBase()`, `mag()`. Created via `Factory`, one per dot. |
| **`LedgerLine`** (`ledgerline.h:40`, `final`) | `EngravingItem` / `Chord` (x-origin chord, y-origin `SysStaff`) | `len()/setLen(double)`, `vertical()/setVertical(bool)` (vertical ledger lines for percussion), `chord()`; `LayoutData{ lineWidth }`. Computed by layout; listed in `Chord::ledgerLines()`. |
| **`DeadSlapped`** (`deadslapped.h:34`) | `EngravingItem` / `Rest` (ctor `DeadSlapped(Rest*)`) | no members; `LayoutData{ path1, path2 }` (the two crossing strokes). Owned by `Rest::deadSlapped()` (§2.4). |

#### 2.15.13 Layout & staff-level markers (`layoutbreak.h`, `spacer.h`, `staffstate.h`, `stafftypechange.h`, `stafflines.h`, `systemdivider.h`)

| Class (`file:line`) | Base / owner | Members |
|---|---|---|
| **`LayoutBreak`** (`layoutbreak.h:36`, `final`) | `EngravingItem` / `MeasureBase` (`measure()` returns `MeasureBase*`) | `layoutBreakType()` (`LayoutBreakType`: `PAGE, LINE, SECTION, NOBREAK`, `types.h:553`; def `NOBREAK`; `subtype()` = int), `isPageBreak/isLineBreak/isSectionBreak/isNoBreak()`, section-break options `pause()` (`double`, seconds), `startWithLongNames()`, `startWithMeasureOne()`, `firstSystemIndentation()`, `showCourtesy()` (all `bool`, def false), `iconCode()` (`char16_t`) / `font()` (icon-font glyph used to draw it). `acceptDrop/drop`. |
| **`Spacer`** (`spacer.h:44`, `final`) | `EngravingItem` / `Measure` | `spacerType()` (`SpacerType`: `UP, DOWN, FIXED`; def `UP`), `gap()/setGap(Spatium)`, `absoluteGap()`; 1 grip (`START`); `LayoutData{ PainterPath path }`; `triggerLayout()` override. |
| **`StaffState`** (`staffstate.h:44`, `final`) | `EngravingItem` / `Segment` | legacy "staff state" marker: `staffStateType()` (`StaffStateType`: `INSTRUMENT, TYPE, VISIBLE, INVISIBLE`; def `INVISIBLE`), `instrument()` (owned `Instrument*`), `acceptDrop/drop`; `LayoutData{ lw, path }`. |
| **`StaffTypeChange`** (`stafftypechange.h:35`, `final`) | `EngravingItem` / `Measure` (ctor takes `MeasureBase`) | `staffType()` (`const StaffType*`), `setStaffType(StaffType*, bool owned)`, `lw()`; registers the change in `Staff::staffType(tick)` (§2.12). |
| **`StaffLines`** (`stafflines.h:37`, `final`) | `EngravingItem` / `Measure` | `lines()` (`vector<LineF>`), `lw()` (line width), `y1()`, `hitBBox()/hitShape()`, `pagePos()/canvasPos()`, `collectForDrawing()`. One per staff per measure; geometry is layout output (`setLines`). |
| **`SystemDivider`** (`systemdivider.h:36`, `final`) | **`Symbol`** / `System` | `dividerType()` (`SystemDividerType`: `LEFT/RIGHT`), `system()`, `segment()` = null, `styleChanged()`, `getPropertyStyle`. Drawn between systems (`System::systemDividerLeft/Right`, §2.13). |

#### 2.15.14 Measure repeat, multimeasure rest, text variants (`measurerepeat.h`, `mmrest.h`, `mmrestrange.h`, `text.h`, `measurenumber.h`, `instrumentname.h`)

**`MeasureRepeat`** (`measurerepeat.h:33`, `final`, derives **`Rest`**, owner `Segment`): `numMeasures()` (`int`, 1-`MAX_NUM_MEASURES`=4; `subtype()` returns it), `numberOffset()` (`Spatium`), `firstMeasureOfGroup()`, `referringMeasure(Measure*)`, `ticks()` (overridden — the measure's span) vs `actualTicks()` (= `Rest::ticks()`), `placeMultiple()` true only for 1-measure repeats; `LayoutData{ symId, numberSym, extenderLineLeft/Right }`.

**`MMRest`** (`mmrest.h:30`, `final`, derives `Rest`): `numberOffset()` (`Spatium`), `showNumber()/shouldShowNumberByDefault()`, `isOldStyle()`, `numberRect()/numberPos()/yNumberPos()`; private `m_numberVisible`; `LayoutData{ number, yNumberPos, numberSym, restSyms (old-style), symsWidth, restWidth }`; `linkedClone()` makes a linked copy (ctor `bool link`).

**`MMRestRange`** (`mmrestrange.h:34`, derives `MeasureNumberBase`, owner `Measure`): `M_PROPERTY` `bracketType` (`MMRestRangeBracketType`: `BRACKETS` `[18-24]`, `PARENTHESES` `(18-24)`, `NONE`); `setXmlText` override. **`MeasureNumber`** (`measurenumber.h:33`, derives `MeasureNumberBase`, default style `MEASURE_NUMBER`): `isSystemObjectBelowBottomStaff()`. **`Text`** (`text.h:33`, `final`, `TextBase`): the generic text item — `propertyDelegate` (frame texts delegate), `setLayoutToParentWidth(bool)`, `voiceAssignment()`, `readXmlText(XmlReader&, Score*)` (static). **`InstrumentName`** (`instrumentname.h:43`, `final`, `TextBase`, owner `System`): `instrumentNameType()` (`InstrumentNameType`: `LONG/SHORT`), `instrumentNameRole()` (`InstrumentNameRole`: `STAFF, SHARED_STAFF, PART, GROUP`; def `PART`), `sysStaff()` (`SysStaff*`), `largestStaffSpatium()`, `effectiveStaffIdx()`; `LayoutData{ column, endIdxOfGroup (one-after-last spanned staff, GROUP only) }`; `isEditable` false.

#### 2.15.15 Brackets, indicators, anchors and editor-only items (`bracket.h`, `bracketitem.h`, `parenthesis.h`, `anchors.h`, `indicatoricon.h`, `systemindicatoricon.h`, `staffvisibilityindicator.h`, `systemlockindicator.h`, `pagelockindicator.h`, `stavesharingchange.h`, `sharedpart.h`, `lasso.h`, `shadownote.h`, `tabdurationsymbol.h`, `actionicon.h`, `bagpembell.h`, `rootitem.h`, `dummyparent.h`, `chord.h`/`note.h` embedded classes)

| Class (`file:line`) | Base / owner | Members |
|---|---|---|
| **`BracketItem`** (`bracketitem.h:38`, `final`) | `EngravingItem` (`ElementType::BRACKET_ITEM` set in the ctor, `bracketitem.cpp:35`; **no `DECLARE_CLASSOF`**) | the *spec* of a staff bracket, stored per staff: `bracketType()` (`BracketType`: `NORMAL, BRACE, SQUARE, LINE, GROUP, NO_BRACKET=-1`), `bracketSpan()`, `column()`, `startStaffIdx()/startStaff()`, `label()` (`StaffLabel{longName, shortName}`; default "GROUP"/"GR."), `showText()`, `showBracket()`, `intersects(...)`. |
| **`Bracket`** (`bracket.h:39`, `final`) | `EngravingItem` / `System` | the drawn bracket: `bracketItem()` (`BracketItem*`, `bracketType()` forwards), `firstStaff()/lastStaff()`, `setStaffSpan(a,b)`, `labelType()` (`InstrumentNameType`), `braceSymbol()`, `magx()` (horizontal brace scale), `measure()`, `text()` (`Text*` label), `intersects(Bracket*)`, `contains(staffIdx)`; `color()` from the item; 1 grip; `undoChangeProperty` routes to `ChangeBracketProperty` (§3A.3); `LayoutData{ braceSymbol, path, shape, bracketHeight, bracketWidth }`. |
| **`Parenthesis`** (`parenthesis.h:27`) | `EngravingItem` (`ElementType::PARENTHESIS` set in ctor, `parenthesis.cpp:31`; no `DECLARE_CLASSOF`); ctor parent is `DummyParentOr<EngravingItem>` (several owner kinds) | `M_PROPERTY2` `direction` (`DirectionH`, def `LEFT`); `segment()` (null if the parent is not a segment), `followParentCurColor()/setFollowParentColor(bool)`; `PARENTHESIS_END_WIDTH = 0.1`; `LayoutData{ path, startY, height, midPointThickness, endPointThickness, shoulderWidth, intrinsicMag, symId, symScale }`. `NoteParenthesisInfo` (`chord.h:119`) pairs left/right `Parenthesis*` with the `vector<Note*>` they enclose. |
| **`TimeTickAnchor`** (`anchors.h:66`) | `EngravingItem` (type set in ctor, `anchors.cpp:574`) / `Segment` | invisible segment anchor for items placed between notes: `segment()`, `voiceIdx()`, `drawRegion()` (`DrawRegion`: `OUT_OF_RANGE/EXTENDED_REGION/MAIN_REGION`), `LayoutData{ darker }`. Helper classes in the same header: `EditTimeTickAnchors` (`updateAnchors`, `createTimeTickAnchor`) and `MoveElementAnchors` (keyboard/drag re-anchoring). |
| **`IndicatorIcon`** (`indicatoricon.h:30`) | `EngravingItem` abstract | `iconCode()` pure (`char16_t`), `font()`; `undoChangeProperty` is a **no-op** ("not editable"); `LayoutData{ rangeRect }`. |
| **`SystemIndicatorIcon`** (`systemindicatoricon.h:28`) | `IndicatorIcon` / `System` | `system()`, `tick()`. Subclasses: **`StaffVisibilityIndicator`** (icon `0xF4AE`), **`SystemLockIndicator`** (`systemLock()` → `RangeLock*`, icon `0xF487`, `formatBarsAndBeats()`), **`PageLockIndicator`** (`pageLock()`, `page()`, icon `0xF4C3`, extra `LayoutData::innerRangeRect`). |
| **`StaveSharingChange`** (`stavesharingchange.h:30`, `final`) | `IndicatorIcon` / `Segment` | `staveSharingState()` (`const StaveSharingState*`), `iconCode()`. |
| **`SharedPart`** (`sharedpart.h:73`, `final`) | `Part` (`ElementType::SHARED_PART`) | `originParts()` (`vector<Part*>`), `addOriginPart/removeOriginPart`, `originPart(sharedTrack, tick)`, `originPartForStaff(staffIdx, tick)`, `enabled()`, `isSameInstruments()`, and the **per-tick `StaveSharingState`** (`staveSharingStateAtTick/exact…/set…/remove…/…BetweenTicks`, `nextUserModifiedStaveSharingTick`). `StaveSharingState{ userTrackMap (optional `SharedTrackMap` = `map<origin track, shared track>`), isReset, sharedTrackMap (computed at layout), isUserMapValid }`. |
| **`Lasso`** (`lasso.h:27`) | `EngravingItem`, ctor `Lasso(Score*)` | rubber-band selection rectangle: `isEmpty()` (empty bbox), **8 grips** (default/initial = grip 7), `dragGrip`. |
| **`ShadowNote`** (`shadownote.h:41`, `final`) | `EngravingItem`, ctor `ShadowNote(Score*)` | note-input cursor preview: `setState(noteSymbol, TDuration, isRest, beyondScore, AccidentalType, set<SymId> articulations)`, `tick()/setTick`, `lineIndex()` (def −1), `isRest()`, `duration()`, `noteheadSymbol()`, `accidentalType()`, `articulationIds()`, `computeUp()`, `hasStem()/hasFlag()/flagSym()`, `ledgerLinesVisible()`, `isBeyondScore()`; draws articulations itself. |
| **`TabDurationSymbol`** (`tabdurationsymbol.h:40`, `final`) | `EngravingItem` / `ChordRest` | TAB rhythm glyph: `tab()` (`const StaffType*`), `text()` (`String` glyph), `isRepeat()`, `setDuration(DurationType, dots, StaffType*)`; `TabBeamGrid` (`NONE, INITIAL, MEDIALFINAL`) in `LayoutData{ beamGrid, beamLength, beamLevel }`; `isEditable` false. |
| **`ActionIcon`** (`actionicon.h:34`, `final`) | `EngravingItem` ("Dummy element, used for drag&drop") | `actionType()` (`ActionIconType`), `title()`, `icon()` (`char16_t`), `iconFont()`; palette cell that triggers an edit (beam-mode icons, bend-type icons, `GuitarBend::bendTypeFromActionIcon`). |
| **`BagpipeEmbellishment`** (`bagpembell.h:59`, `final`) | `EngravingItem` ("dummy element, used for drag&drop") | `embelType()` (`EmbellishmentType`, an unscoped `unsigned char` enum), `resolveNoteList()` (`vector<int>` of note ids), static `BAGPIPE_NOTEINFO_LIST[]` (`BagpipeNoteInfo{ name, line, pitch }`), `mag()`; `LayoutData{ isDrawBeam, isDrawFlag, spatium, headsym, flagsym, stemLineW, notesData (map<note index, NoteData{headXY, stemLine, flagXY, ledgerLine}>), beamData }`. |
| **`NoteHead`** (`note.h:81`, `final`) | **`Symbol`** / `Note` | only `headGroup()` (→ `NoteHeadGroup`); the drawable head glyph of a `Note`. |
| **`GraceNotesGroup`** (`chord.h:85`, `final`) | `std::vector<Chord*>` **and** `EngravingItem` | the layout cluster of grace chords before/after a chord: `parent()` (`Chord*`), `appendedSegment()/setAppendedSegment()` (the segment it is laid out against), `addToShape()`, `setPos(x,y)`. |
| **`RootItem`** (`rootitem.h:33`) | `EngravingItem` (`ElementType::ROOT_ITEM` set in the ctor, `rootitem.cpp:35`) | heads the accessibility tree: `Kind{ Score, Dummy }`, `accessibleChildren()`, `clone()` returns `nullptr`, `getProperty` returns an invalid `PropertyValue`, `setProperty` returns false. |
| **`DummyParent`** (`dummyparent.h:35`) | `EngravingObject` (not an item) | `rootItem()` (`RootItem*`); anchors every detached object (§1.1); `getProperty`/`setProperty` are inert. |

> **Not tabulated on purpose (not `EngravingItem`s, §2.6):** `Selection`, `InputState`, `Excerpt`, `Instrument`/`StaffType`/`Drumset`/`StringData` (value types, §2.12), `StaveSharingState`, `CapoParams`.

#### 2.15.16 `P_TYPE` ↔ storage type (complete; `types/propertyvalue.h:41-141` enum, `:149-384` constructors)

`PropertyValue` stores its payload as a `std::shared_ptr<IArg>` pointing at an `Arg<T>`; `P_TYPE m_type` is the discriminator. The **constructor chosen by the C++ argument type sets `m_type`** (the table is therefore also the list of types that can be stored). `isEnum()` is true when `T` is an enum; `value<T>()` has a set of "temporary hacks" (`:405-480`): int→enum, enum→int, bool↔int, size_t→int, real→`Spatium`, `Spatium`→double, `ABSOLUTE`→double, `Fraction`→`String`/`QString`, `String`→`QString`. Equality compares `Arg<T>::v` after a `dynamic_cast`. (`value()` of `UNDEFINED` returns `T()`.)

| Group | `P_TYPE` → C++ storage type |
|---|---|
| Base | `BOOL` → `bool`; `INT` → `int`; `INT_VEC` → `std::vector<int>`; `SIZE_T` → `size_t`; `REAL` → `double`; `STRING` → `String` (`const char*` and `QString` ctors convert to `String`) |
| Geometry | `POINT` → `PointF` (saved absolute or spatium depending on `sizeIsSpatiumDependent()`); `SIZE` → `SizeF`; `DRAW_PATH` → `PainterPath`; `SCALE` → `ScaleF`; `SPATIUM` → `Spatium`; **`ABSOLUTE` → has *no* `PropertyValue` constructor** — it exists only as a `propertyType(Pid)` tag (value held as `double`, read through the `ABSOLUTE→double` hack); `PAIR_REAL` → `PairF` |
| Draw | `SYMID` → `SymId`; `COLOR` → `Color`; `ORNAMENT_STYLE` → `OrnamentStyle`; `ORNAMENT_INTERVAL` → `OrnamentInterval`; `ORNAMENT_SHOW_ACCIDENTAL` → `OrnamentShowAccidental`; `GLISS_STYLE` → `GlissandoStyle`; `GLISS_TYPE` → `GlissandoType` |
| Layout | `ALIGN` → `Align`; `ALIGN_H` → `AlignH`; `PLACEMENT_V` → `PlacementV`; `PLACEMENT_H` → `PlacementH`; `TEXT_PLACE` → `TextPlace`; `DIRECTION_V` → `DirectionV`; `DIRECTION_H` → `DirectionH`; `ORIENTATION` → `Orientation`; `SHARED_LABEL_ORIENTATION` → `SharedLabelOrientation`; `BEAM_MODE` → `BeamMode`; `ACCIDENTAL_ROLE` → `AccidentalRole`; `TIE_PLACEMENT` → `TiePlacement`; `TIE_DOTS_PLACEMENT` → `TieDotsPlacement`; `TIMESIG_PLACEMENT` → `TimeSigPlacement`; `TIMESIG_STYLE` → `TimeSigStyle`; `TIMESIG_MARGIN` → `TimeSigVSMargin`; `NOTE_SPELLING_TYPE` → `NoteSpellingType`; `CHORD_PRESET_TYPE` → `ChordStylePreset`; `PARENTHESES_MODE` → `ParenthesesMode`; `PLAY_COUNT_PRESET` → `RepeatPlayCountPreset` |
| Sound | `FRACTION` → `Fraction`; `DURATION_TYPE_WITH_DOTS` → `DurationTypeWithDots`; `CHANGE_METHOD` → `ChangeMethod`; `PITCH_VALUES` → `PitchValues`; `TEMPO` → `BeatsPerSecond` |
| Types | `LAYOUTBREAK_TYPE` → `LayoutBreakType`; `VELO_TYPE` → `VeloType`; `BARLINE_TYPE` → `BarLineType`; `NOTEHEAD_TYPE/SCHEME/GROUP` → `NoteHeadType/NoteHeadScheme/NoteHeadGroup`; `CLEF_TYPE` → `ClefType`; `CLEF_TO_BARLINE_POS` → `ClefToBarlinePosition`; `DYNAMIC_TYPE` → `DynamicType`; `DYNAMIC_SPEED` → `DynamicSpeed`; `LINE_TYPE` → `LineType`; `HOOK_TYPE` → `HookType`; `KEY_MODE` → `KeyMode`; `TEXT_STYLE` → `TextStyleType`; `PLAYTECH_TYPE` → `PlayingTechniqueType`; `TEMPOCHANGE_TYPE` → `GradualTempoChangeType`; `SLUR_STYLE_TYPE` → `SlurStyleType`; `NOTELINE_PLACEMENT_TYPE` → `NoteLineEndPlacement`; `LYRICS_DASH_SYSTEM_START_TYPE` → `LyricsDashSystemStart`; `PARTIAL_SPANNER_DIRECTION` → `PartialSpannerDirection`; `MARKER_TYPE` → `MarkerType`; `ACTION_TYPE` → `ActionIconType`; `LH_TAPPING_SYMBOL` → `LHTappingSymbol`; `RH_TAPPING_SYMBOL` → `RHTappingSymbol`; `VIBRATO_LINE_TYPE` → `VibratoType`; `VOICE_ASSIGNMENT` → `VoiceAssignment`; `AUTO_ON_OFF` → `AutoOnOff`; `AUTO_CUSTOM_HIDE` → `AutoCustomHide`; `MEASURE_NUMBER_PLACEMENT` → `MeasureNumberPlacement`; `CAPO_TRANSPOSE_MODE` → `CapoParams::TransposeMode`; `INSTRUMENT_NAMES_ALIGN` → `InstrumentNamesAlign`; `INSTRUMENT_NAMES_FORMAT` → `InstrumentNamesFormat`; `INSTRUMENT_LABEL_VISIBILITY` → `InstrumentLabelVisibility` |
| Other | `GROUPS` → `GroupNodes` (beam-grouping pattern, `groups.h`) |

⚠ Gotchas read off the source: (1) **Equality is hack-ridden** (`propertyvalue.cpp:40-65`): `UNDEFINED` equals only `UNDEFINED`; if *either* side is `BOOL` the comparison is `value<bool>()`; if either side is `INT` it is `value<int>()` (so an `INT` and an enum-typed value holding the same number compare equal); if either side is `SPATIUM` it compares as `double` (`RealIsEqual`); `FRACTION` uses `identical()` and **asserts** the other side is also `FRACTION`. Only after those hacks does it fall through to type-wise `Arg<T>::equal`. (2) Build values with the typed enum rather than a bare `int` — an `int` is stored as `INT`, and reads are rescued only by the int→enum hack. (3) A `Spatium` property written via the `double` ctor is stored as `REAL`, not `SPATIUM`; reads are rescued by the real→`Spatium` hack, but the stored tag and `propertyType(Pid)` can disagree. (4) `ABSOLUTE` has no constructor, so nothing can *produce* that tag from a typed value — it is purely a `propertyType(Pid)` marker.

---

## 3. Inventory — the `ElementType` → class map (complete)

⭐ **`ElementType`** (`types/types.h:68`, `enum class : unsigned char`) is the authoritative registry: **155 enumerators** `INVALID = 0 … DUMMY` with `MAXTYPE` as the count sentinel (`TOT_ELEMENT_TYPES = 155`, `types.h:230`). The enum **order encodes canvas stacking order** ("keep in sync with array in TConv"). Every concrete item maps 1:1 to an enumerator via `DECLARE_CLASSOF(ElementType::X)` in its header and an `isX()` helper in `engravingobject.h`. Table below maps **every** enumerator to its class, header file, and base (bases abbreviated: EI = `EngravingItem`, EO = `EngravingObject`, CR = `ChordRest`, SpS = `SpannerSegment`, LS = `LineSegment`, TLB = `TextLineBase`, TLBS = `TextLineBaseSegment`, TB = `TextBase`, STB = `StaffTextBase`, BSym = `BSymbol`).

> Note: the `*_SEGMENT` enumerators are the per-system on-screen pieces of the correspondingly-named spanner; unless noted they are `LineSegment`/`SpannerSegment` subclasses declared in the **same header** as their spanner.

| ElementType | Class | File | Base / role |
|---|---|---|---|
| INVALID | — | — | sentinel (no class). |
| BRACKET_ITEM | BracketItem | bracketitem.h | EI (`bracketitem.h:38`); staff-group bracket spec. |
| PART | Part | part.h | EO; instrument. |
| SHARED_PART | SharedPart | sharedpart.h | Part; shared-part linkage. |
| STAFF | Staff | staff.h | EI (`staff.cpp:71`); a staff. |
| SCORE | Score | score.h | EO+Contextable; the score. |
| TEXT | Text | text.h | TB; generic/frame text. |
| LAYOUT_BREAK | LayoutBreak | layoutbreak.h | EI; line/page/section break. |
| MEASURE_NUMBER | MeasureNumber | measurenumber.h | MeasureNumberBase/TB. |
| MMREST_RANGE | MMRestRange | mmrestrange.h | MeasureNumberBase/TB; mmrest range text. |
| INSTRUMENT_NAME | InstrumentName | instrumentname.h | TB; staff name. |
| BAR_LINE | BarLine | barline.h | EI. |
| STAFF_LINES | StaffLines | stafflines.h | EI; the 5 staff lines. |
| SYSTEM_DIVIDER | SystemDivider | systemdivider.h | Symbol. |
| SLUR | Slur | slur.h | SlurTie/Spanner. |
| SLUR_SEGMENT | SlurSegment | slur.h | SlurTieSegment/SpS. |
| TIE | Tie | tie.h | SlurTie/Spanner. |
| TIE_SEGMENT | TieSegment | tie.h | SlurTieSegment/SpS. |
| LAISSEZ_VIB | LaissezVib | laissezvib.h | Tie; l.v. tie. |
| LAISSEZ_VIB_SEGMENT | LaissezVibSegment | laissezvib.h | TieSegment. |
| PARTIAL_TIE | PartialTie | partialtie.h | Tie. |
| PARTIAL_TIE_SEGMENT | PartialTieSegment | partialtie.h | TieSegment. |
| STEM_SLASH | StemSlash | stemslash.h | EI; grace slash. |
| ARPEGGIO | Arpeggio | arpeggio.h | EI. |
| CHORD_BRACKET | ChordBracket | chordbracket.h | Arpeggio; bracket across chord. |
| ACCIDENTAL | Accidental | accidental.h | EI. |
| LEDGER_LINE | LedgerLine | ledgerline.h | EI. |
| STEM | Stem | stem.h | EI. |
| HOOK | Hook | hook.h | Symbol; flag/hook. |
| NOTE | Note | note.h | EI. |
| CLEF | Clef | clef.h | EI. |
| KEYSIG | KeySig | keysig.h | EI. |
| AMBITUS | Ambitus | ambitus.h | EI. |
| TIMESIG | TimeSig | timesig.h | EI. |
| REST | Rest | rest.h | CR. |
| MMREST | MMRest | mmrest.h | Rest; multimeasure rest. |
| DEAD_SLAPPED | DeadSlapped | deadslapped.h | EI; dead-slap glyph. |
| SYMBOL | Symbol | symbol.h | BSym; SMuFL symbol. |
| BREATH | Breath | breath.h | EI; breath/caesura. |
| MEASURE_REPEAT | MeasureRepeat | measurerepeat.h | Rest; repeat sign. |
| ARTICULATION | Articulation | articulation.h | EI. |
| ORNAMENT | Ornament | ornament.h | Articulation. |
| FERMATA | Fermata | fermata.h | EI. |
| CHORDLINE | ChordLine | chordline.h | EI; fall/doit/scoop. |
| DYNAMIC | Dynamic | dynamic.h | TB (STB family). |
| EXPRESSION | Expression | expression.h | TB. |
| BEAM | Beam | beam.h | BeamBase/EI. |
| LYRICS | Lyrics | lyrics.h | TB. |
| FIGURED_BASS | FiguredBass | figuredbass.h | TB. |
| FIGURED_BASS_ITEM | FiguredBassItem | figuredbass.h | EI; one figure. |
| MARKER | Marker | marker.h | TB; coda/segno/etc. |
| JUMP | Jump | jump.h | TB; D.C./D.S. |
| FINGERING | Fingering | fingering.h | TB. |
| TUPLET | Tuplet | tuplet.h | DurationElement. |
| TEMPO_TEXT | TempoText | tempotext.h | TB. |
| STAFF_TEXT | StaffText | stafftext.h | STB. |
| SYSTEM_TEXT | SystemText | systemtext.h | STB. |
| STAVE_SHARING_LABEL | StaveSharingLabel | stavesharinglabel.h | STB. |
| SOUND_FLAG | SoundFlag | soundflag.h | EI; playback sound switch. |
| PLAY_COUNT_TEXT | PlayCountText | playcounttext.h | TB. |
| PLAYTECH_ANNOTATION | PlayTechAnnotation | playtechannotation.h | STB; play-technique text. |
| CAPO | Capo | capo.h | STB. |
| STRING_TUNINGS | StringTunings | stringtunings.h | STB. |
| TRIPLET_FEEL | TripletFeel | tripletfeel.h | SystemText (→ STB). |
| REHEARSAL_MARK | RehearsalMark | rehearsalmark.h | TB. |
| INSTRUMENT_CHANGE | InstrumentChange | instrchange.h | TB. |
| STAFFTYPE_CHANGE | StaffTypeChange | stafftypechange.h | EI. |
| FRET_DIAGRAM | FretDiagram | fret.h | EI. |
| HARMONY | Harmony | harmony.h | TB; chord symbol. |
| HARP_DIAGRAM | HarpPedalDiagram | harppedaldiagram.h | TB. |
| BEND | Bend | bend.h | EI (legacy guitar bend). |
| TREMOLOBAR | TremoloBar | tremolobar.h | EI. |
| VOLTA | Volta | volta.h | TLB/SLine. |
| VOLTA_SEGMENT | VoltaSegment | volta.h | TLBS. |
| HAIRPIN | Hairpin | hairpin.h | TLB/SLine. |
| HAIRPIN_SEGMENT | HairpinSegment | hairpin.h | TLBS. |
| OTTAVA | Ottava | ottava.h | TLB/SLine. |
| OTTAVA_SEGMENT | OttavaSegment | ottava.h | TLBS. |
| TRILL | Trill | trill.h | SLine. |
| TRILL_SEGMENT | TrillSegment | trill.h | LS. |
| LET_RING | LetRing | letring.h | ChordTextLineBase (→ TLB/SLine). |
| LET_RING_SEGMENT | LetRingSegment | letring.h | TLBS. |
| GRADUAL_TEMPO_CHANGE | GradualTempoChange | gradualtempochange.h | TLB/SLine. |
| GRADUAL_TEMPO_CHANGE_SEGMENT | GradualTempoChangeSegment | gradualtempochange.h | TLBS. |
| VIBRATO | Vibrato | vibrato.h | SLine. |
| VIBRATO_SEGMENT | VibratoSegment | vibrato.h | LS. |
| PALM_MUTE | PalmMute | palmmute.h | ChordTextLineBase (→ TLB/SLine). |
| PALM_MUTE_SEGMENT | PalmMuteSegment | palmmute.h | TLBS. |
| WHAMMY_BAR | WhammyBar | whammybar.h | ChordTextLineBase (→ TLB/SLine). |
| WHAMMY_BAR_SEGMENT | WhammyBarSegment | whammybar.h | TLBS. |
| RASGUEADO | Rasgueado | rasgueado.h | ChordTextLineBase (→ TLB/SLine). |
| RASGUEADO_SEGMENT | RasgueadoSegment | rasgueado.h | TLBS. |
| HARMONIC_MARK | HarmonicMark | harmonicmark.h | ChordTextLineBase (→ TLB/SLine). |
| HARMONIC_MARK_SEGMENT | HarmonicMarkSegment | harmonicmark.h | TLBS. |
| PICK_SCRAPE | PickScrape | pickscrape.h | ChordTextLineBase (→ TLB/SLine). |
| PICK_SCRAPE_SEGMENT | PickScrapeSegment | pickscrape.h | TLBS. |
| TEXTLINE | TextLine | textline.h | TLB/SLine. |
| TEXTLINE_SEGMENT | TextLineSegment | textline.h | TLBS. |
| PEDAL | Pedal | pedal.h | TLB/SLine. |
| PEDAL_SEGMENT | PedalSegment | pedal.h | TLBS. |
| LYRICSLINE | LyricsLine | lyrics.h | SLine; melisma/dash line. |
| LYRICSLINE_SEGMENT | LyricsLineSegment | lyrics.h | LS. |
| PARTIAL_LYRICSLINE | PartialLyricsLine | lyrics.h | LyricsLine. |
| PARTIAL_LYRICSLINE_SEGMENT | PartialLyricsLineSegment | lyrics.h | LyricsLineSegment. |
| GLISSANDO | Glissando | glissando.h | SLine. |
| GLISSANDO_SEGMENT | GlissandoSegment | glissando.h | LS. |
| NOTELINE | NoteLine | noteline.h | TLB. |
| NOTELINE_SEGMENT | NoteLineSegment | noteline.h | TLBS. |
| STAFF_VISIBILITY_INDICATOR | StaffVisibilityIndicator | staffvisibilityindicator.h | SystemIndicatorIcon. |
| SYSTEM_LOCK_INDICATOR | SystemLockIndicator | systemlockindicator.h | SystemIndicatorIcon. |
| SPACER | Spacer | spacer.h | EI; vertical spacer. |
| STAFF_STATE | StaffState | staffstate.h | EI. |
| NOTEHEAD | NoteHead | note.h | Symbol (`NoteHead` in note.h). |
| NOTEDOT | NoteDot | notedot.h | EI; augmentation dot. |
| IMAGE | Image | image.h | BSym. |
| MEASURE | Measure | measure.h | MeasureBase. |
| SELECTION | Selection | select.h | standalone selection container (not an EI). |
| LASSO | Lasso | lasso.h | EI; rubber-band select. |
| SHADOW_NOTE | ShadowNote | shadownote.h | EI; input-cursor preview note. |
| TAB_DURATION_SYMBOL | TabDurationSymbol | tabdurationsymbol.h | EI; TAB duration glyph. |
| FSYMBOL | FSymbol | symbol.h | Symbol; font (char) symbol. |
| PAGE | Page | page.h | EI; a layout page. |
| PAGE_LOCK_INDICATOR | PageLockIndicator | pagelockindicator.h | SystemIndicatorIcon. |
| TEXTLINE_BASE | TextLineBase | textlinebase.h | SLine; abstract base (has a type tag for RW). |
| BRACKET | Bracket | bracket.h | EI; drawn staff bracket. |
| SEGMENT | Segment | segment.h | EI. |
| SYSTEM | System | system.h | EI; a line of music. |
| CHORD | Chord | chord.h | CR. |
| HBOX | HBox | box.h | Box/MeasureBase; horizontal frame. |
| VBOX | VBox | box.h | Box/MeasureBase; vertical frame. |
| TBOX | TBox | box.h | VBox; text frame. |
| FBOX | FBox | box.h | VBox; fretboard-diagram frame. |
| ACTION_ICON | ActionIcon | actionicon.h | EI; palette/toolbar action cell. |
| BAGPIPE_EMBELLISHMENT | BagpipeEmbellishment | bagpembell.h | EI. |
| STICKING | Sticking | sticking.h | TB; percussion sticking. |
| GRACE_NOTES_GROUP | GraceNotesGroup | chord.h | EI + `vector<Chord*>`; grace cluster. |
| GUITAR_BEND | GuitarBend | guitarbend.h | SLine. |
| GUITAR_BEND_SEGMENT | GuitarBendSegment | guitarbend.h | LS. |
| GUITAR_BEND_HOLD | GuitarBendHold | guitarbend.h | SLine. |
| GUITAR_BEND_HOLD_SEGMENT | GuitarBendHoldSegment | guitarbend.h | LS. |
| GUITAR_BEND_TEXT | GuitarBendText | guitarbend.h | TB. |
| TREMOLO_TWOCHORD | TremoloTwoChord | tremolotwochord.h | BeamBase (→ EI); between two chords. |
| TREMOLO_SINGLECHORD | TremoloSingleChord | tremolosinglechord.h | EI; on one chord. |
| TIME_TICK_ANCHOR | TimeTickAnchor | anchors.h | EI; invisible tick anchor. |
| PARENTHESIS | Parenthesis | parenthesis.h | EI. |
| HAMMER_ON_PULL_OFF | HammerOnPullOff | hammeronpulloff.h | Slur. |
| HAMMER_ON_PULL_OFF_SEGMENT | HammerOnPullOffSegment | hammeronpulloff.h | SlurSegment. |
| HAMMER_ON_PULL_OFF_TEXT | HammerOnPullOffText | hammeronpulloff.h | TB. |
| TAPPING | Tapping | tapping.h | Articulation. |
| TAPPING_HALF_SLUR | TappingHalfSlur | tapping.h | Slur. |
| TAPPING_HALF_SLUR_SEGMENT | TappingHalfSlurSegment | tapping.h | SlurSegment. |
| TAPPING_TEXT | TappingText | tapping.h | TB. |
| STAVE_SHARING_CHANGE | StaveSharingChange | stavesharingchange.h | IndicatorIcon (→ EI). |
| ROOT_ITEM | RootItem | rootitem.h | EI; anchor for detached trees. |
| DUMMY | DummyParent | dummyparent.h | EO; the per-score dummy parent. |

*(Base classes noted "TLB/SLine" are `TextLineBase`, which itself derives from `SLine`→`Spanner`. A few bases above — e.g. `MeasureNumberBase`, `BeamBase`, `SlurTie` — are intermediate abstract classes with their own headers but no `ElementType` of their own, except `TEXTLINE_BASE` which does carry a tag.)*

---

## 3A. The editing / undo system (`src\engraving\editing\`)

⭐ **Naming reality check.** There are **no** classes called `UndoCommand`, `UndoMacro` (zero hits for either under `src\`). The 5.0 `main` equivalents live in `src\engraving\editing\transaction\`: `UndoableCommand` (one atomic low-level change), `UndoableTransaction` (the "macro" = one user-visible undo step), `UndoStack`, plus the newer safety wrappers `Transaction` and `TransactionManager`. The ~70 other headers in `editing\` are mostly concrete `UndoableCommand` subclasses (`editproperty.h`, `addremoveelement.h`, `editstaff.h`, `editpart.h`, `editmeasures.h`, `textedit.h`, `editstyle.h`, `editchord.h`, `editspanner.h`, …) and the high-level edit operations that create them (`edit.cpp`, `cmd.cpp`, `paste.cpp`, `noteinput.cpp`, `transpose.h`, `flip.h`, …).

### 3A.1 The classes

| Class (`file:line`) | Role | Key members |
|---|---|---|
| `UndoableCommand` (`transaction/undoablecommand.h:186`) | abstract atomic change | `undo()`/`redo()` both **call `flip()`** by default (`undoablecommand.cpp:60-69`) — most commands implement only `flip()` and swap stored/live state. Also `cleanup(bool wasDone)` (free owned elements; `:194`), `objectItems()` (what changed → UI refresh), `name()`, `type()` → `CommandType`, `changedRange()` → optional `ChangedRange{tickFrom,tickTo,staffIdxFrom,staffIdxTo}`, `matchesFilter(UndoableCommandFilter, target)`. Macros `UNDO_TYPE/UNDO_NAME/UNDO_CHANGED_OBJECTS` (`:175-177`). |
| `CommandType` (`:34`) | enum tag per command kind | 79 values (+`Unknown=-1`) grouped Parts / Staves / MStaves / Instruments / Measures / Elements (`AddElement, RemoveElement, Link, Unlink, ChangeElement, ChangeParent`) / Notes / ChordRest / Brackets / Fret / Harmony / KeySig / Clef / Tremolo / Spanners / Ties / Style (`ChangeStyle, ChangeStyleValues`) / `ChangeProperty` / Voices / Excerpts / MetaInfo / `TextEdit` / Automation / `InsertTime`. |
| `UndoableCommandFilter` (`:165`) | enum for selective removal | `TextEdit, AddElement, AddElementLinked, Link, RemoveElement, RemoveElementLinked, ChangePropertyLinked` — used by `UndoableTransaction::hasCommandsMatchingFilter/removeCommandsMatchingFilter` (`undostack.cpp:293-332`) to prune a transaction. |
| `UndoableTransaction` (`transaction/undostack.h:44`) | ordered list of commands = one undo step | `m_commands` (`appendCommand`, `commands()`), snapshot of `InputState` + `SelectionInfo` before (`m_undo*`) and after (`m_redo*`), `actionName()`, `undo()`, `redo()`, `unwind(cleanUp)`, `cleanup(wasDone)`, `append(UndoableTransaction&&)` (merge), `changesInfo(undo)` → `ChangesInfo{changedObjectTypes, changedObjects (object→set<CommandType>), changedStyleIdSet, changedPropertyIdSet, isTextEditing}`. Destructor deletes the commands (`undostack.cpp:252-257`). |
| `UndoStack` (`undostack.h:115`) | the history | `m_transactions` (vector), `m_currentIndex` (= count of *done* transactions; `canUndo()` ⇔ `>0`, `canRedo()` ⇔ `< size`), `m_states`/`m_cleanState` (clean-state tracking for the dirty flag; `isClean()`), `m_activeTransaction` (the one being recorded), `m_isLocked`. Methods `beginTransaction(Score*, name)`, `endTransaction(rollback)`, `undo(EditData*)`, `redo()`, `reopen()`, `mergeTransactions(startIdx)`, `cleanRedoStack()`, `last()/prev()/next()`. |
| `Transaction` (`transaction/transaction.h:104`) | handle passed to code that wants to mutate | non-copyable; `push(cmd)` (append **then** perform), `pushWithoutPerforming(cmd)` (record a command whose effect was already applied), private `dummy()` static. Constructed only by `TransactionManager`. |
| `TransactionManager` (`transaction.h:126`) | the front door; owned by `MasterScore` | `transaction(description, fn(Transaction&))` (begin → fn → end), `beginTransaction`, `endTransaction(rollback, layoutAllParts, keepRolledBackElements)`, `reopen()`, `currentTransaction()` (null if none), `currentOrDummyTransaction()`, `undoRedo(bool undo, EditData*)`, `undoStack()`. Created in `MasterScore`'s ctor (`masterscore.cpp:70`; accessor `masterscore.h:103`); the `UndoStack` is a separate raw-pointer member created next to it (`masterscore.cpp:71`, `masterscore.h:104`). |
| `CmdState` (`editing/cmd.h:50`) | what to relayout/repaint | `layoutFlags`, update mode (`DoNothing…Layout/Update/UpdateAll`), `startTick/endTick/startStaff/endStaff`, `setTick/setStaff/setElement`, `deleteLater(obj)` + `takePostponedDeletions()` (deletes deferred until after layout, `cmd.cpp:257-271`), `lock()/unlock()`. Reset at begin and end of every transaction. Accessed via `MasterScore::cmdState()` (`masterscore.h:139`). |

The header comment of `transaction.h:37-102` states the design goal: *eliminate `Score::undo`* so all undoable commands run inside a `Transaction&`, never forget start/end, never nest. Two temporary compatibility escape hatches remain: **`Score::undo(UndoableCommand*)`** (marked "Deprecated: use Transaction::push instead", `cmd.cpp:304-324`) and **`TransactionManager::currentOrDummyTransaction()`**. Still-open concepts per that comment: failing/rollback transactions, "long-running" transactions (style dialog, text editing, dragging), plugin safety.

### 3A.2 Lifecycle of one user edit

```
Score::startCmd(name)            cmd.cpp:279  → TransactionManager::beginTransaction(name)   transaction.cpp:158
   - no-op if stack->isLocked(); no-op (LOGD) if a transaction is already active
   - MScore::setError(NO_ERROR); masterScore->cmdState().reset()
   - UndoStack::beginTransaction(): new UndoableTransaction(score, name)    undostack.cpp:120-132
       (ctor snapshots InputState and the Selection → m_undoInputState / m_undoSelectionInfo, undostack.cpp:246-250)
   - m_currentTransaction = new Transaction(stack->activeTransaction())
... edit code calls score->undoChangeProperty / undoAddElement / tx.push(new SomeCommand(...)) ...
Score::endCmd(rollback, layoutAllParts, keep)   cmd.cpp:299 → TransactionManager::endTransaction     transaction.cpp:178
   - rollback is forced if the score is read-only or MScore::_error is set and not a warning
   - if rollback: UndoableTransaction::unwind()  (undo all in reverse, THEN free them, undostack.cpp:266-285)
   - masterScore->update(false, layoutAllParts)   ← layout runs INSIDE the transaction so layout-generated commands are recorded
   - build ScoreChanges from CmdState + commands' changedRange() + changesInfo()   (buildScoreChanges, transaction.cpp:34-89)
   - UndoStack::endTransaction(isEmpty): empty ⇒ delete; else drop the redo tail (cleanup(false) on dropped transactions)
         then push_back + ++m_currentIndex                                                        undostack.cpp:171-197
   - invalidateRepeatList() if dirty; cmdState().reset()
   - if non-empty and not rolled back: masterScore->updateAutomation(changes); changesChannel().send(changes)
```
`TransactionManager::transaction(desc, fn)` (`:146-156`) is the preferred wrapper for this. The `changesChannel` is a `muse::async::Channel<ScoreChanges>` (`masterscore.h:106,238`) that the notation layer subscribes to.

**Undo / redo** (`TransactionManager::undoRedo`, `transaction.cpp:262-312`; notation-side callers `notation\internal\notationundostack.cpp:54,76,96,99`): refuses if read-only or a transaction is active; **order matters** — for undo, `changesInfo(true)` is computed *before* `stack->undo()`; for redo it is computed *after* `stack->redo()` (comment `:289-291`). `UndoStack::undo` (`undostack.cpp:216-232`): if the element being edited is a `TextBase` and `TextEditData::startUndoIdx == m_currentIndex` (nothing typed since edit began) it does nothing; else `--m_currentIndex` and `m_transactions[m_currentIndex]->undo()`. `redo` = `m_transactions[m_currentIndex++]->redo()` (`:234-240`). `UndoableTransaction::undo()` (`:382-398`): save redo-side InputState/selection, `deselectAll()`, call `undo()` on commands **in reverse**, restore the undo-side InputState and selection. `redo()` (`:400-416`) mirrors it forward. After either, `update(false)`, `invalidateRepeatList()`, `updateSelection()`, then `ScoreChanges` is broadcast. When undo is reported, `changesInfo` maps a command type to its inverse (`COMMAND_TYPE_INVERSION`: `AddElement↔RemoveElement`, `InsertMeasures↔RemoveMeasures`, `InsertStaff↔RemoveStaff`, `Link↔Unlink`, …, `undostack.cpp:56-86`) so listeners see "element removed" when an add is undone.

`UndoStack::reopen()` (`:199-214`) pops the last transaction back to *active* (used to extend the previous step); `mergeTransactions(startIdx)` (`:155-169`) folds later steps into the one at `startIdx` (`UndoableTransaction::append`) — used for text editing.

### 3A.3 How `undoChangeProperty` and friends create commands

- **`EngravingObject::undoChangeProperty(Pid, v)`** (`engravingobject.cpp:474`) → overload with `PropertyFlags` (`:479-514`): returns if value *and* flags are unchanged; special pre-steps for `Pid::TEXT_STYLE` (resets every property of the new text style to the style value), `Pid::EXCLUDE_FROM_OTHER_PARTS` (`manageExclusionFromParts`), `Pid::VOICE_ASSIGNMENT` (forces `VOICE` to 0 unless `CURRENT_VOICE_ONLY`); then **`changeProperties(this, id, v, ps)`** (`:425-468`) and finally also sets `Pid::GENERATED=false` (unless the change *is* `GENERATED`).
- **`changeProperties`** iterates `linkListForPropertyPropagation()`; for the object itself calls `changeProperty`; for each linked item asks `item->propertyPropagation(linked, pid)` (`PropertyPropagation::NONE` skip / `PROPAGATE` apply / `UNLINK` → `unlinkPropertyFromMaster(pid)`); then (unless the property group is `POSITION`) also propagates to shared-tree `originItems()` / `sharedItem()` (`:455-467`).
- **`changeProperty`** (`:409-419`): if the current value or flags differ, `e->score()->undo(new ChangeProperty(e, pid, v, flags))` (or `ChangeBracketProperty(staff, level, …)` for a `BracketItem`).
- **`ChangeProperty`** (`editing/editproperty.h:30`): fields `element, id, property, flags`; `flip()` (`editproperty.cpp:39-51`) swaps the stored value/flags with the object's current ones via `setProperty` + `setPropertyFlags` — so one routine serves undo *and* redo. `objectItems()` = `compoundObjects(element)` (adds a chord's compound notes / a fret diagram's `Harmony`, `undoablecommand.cpp:32-58`). Subclasses `ChangeBracketProperty` (re-looks the bracket up by staff+level each flip), `ChangeTextLineProperty` (re-`initStyle()`s a `TextLine`).
- **`Score::undo(cmd)`** (`cmd.cpp:309-324`): `currentTransaction()->push(cmd)`; **outside a transaction** it logs a warning (silent during `ScoreLoad::loading()`), `redo()`s the command immediately and deletes it — i.e. the change happens but is *not* undoable.
- **`EngravingObject::undoPushProperty(Pid)`** (`engravingobject.cpp:520-526`): `pushWithoutPerforming(new ChangeProperty(this, id, getProperty(id)))` — snapshots the **current** value so a later direct `setProperty` (e.g. during a drag) becomes undoable. **`undoResetProperty`** (`:386`) = `undoChangeProperty(id, propertyDefault(id), STYLED-if-was-UNSTYLED)`. **`EngravingObject::reset()`** (`:532`) resets every styled property.
- **`Score::undoPropertyChanged`** (`editing/edit.cpp:3406-3452`) is the "value was already set, now record it" variant: records the item with `pushWithoutPerforming`, and pushes (performing) `ChangeProperty` on linked items with `PROPAGATE`.
- **`Score::undoAddElement`** (`edit.cpp:4079`): uses `currentOrDummyTransaction()`; for system-level types (`REHEARSAL_MARK, SYSTEM_TEXT, TRIPLET_FEEL, JUMP, MARKER, TEMPO_TEXT, PLAY_COUNT_TEXT`, system text-lines) clones onto the top staff of every score (+ system-object staves), otherwise onto linked staves; each placement is a pushed `AddElement`. **`AddElement`** (`addremoveelement.h:30`): ctor asserts the item is not `generated()` and shares the parent's score (`addremoveelement.cpp:77-84`); `redo()` → `Score::addElement`, `undo()` → `Score::removeElement` (+ `disconnectSharedTree`), tuplet bookkeeping in `endUndoRedo` (`:94-132`); **`cleanup(wasDone=false)` deletes the element** (`:86-92`) — the command owns an un-done element. `RemoveElement` is the mirror image; `ChangeElement`, `ChangeParent`, `ChangeSegmentParent`, `LinkUnlink` (`Link`/`Unlink`) complete the element family. **`Score::undoRemoveElement`** `edit.cpp:4897`.
- **Style**: `Score::undoChangeStyleVal(Sid, v)` / `undoChangeStyleValues(map)` → `undo(new ChangeStyleValues(this, map))` (`edit.cpp:3454-3465`); `ChangeStyle` in `editing/editstyle.h`.
- **Concrete command families** by header (largest first, by count of `UndoableCommand` mentions): `editstaff.h`, `editpart.h`, `textedit.h`, `editfretboarddiagram.h`, `editmeasures.h`, `editchord.h`, `editexcerpt.h`, `editnote.h`, `editscoreproperties.h`, plus single-command headers (`editclef.h`, `editkeysig.h`, `edittie.h`, `edittremolo.h`, `editsoundflag.h`, `editautomationpoints.h`, `inserttime.h`, …).

### 3A.4 Reading the rules off the code (practical guidance)

- **Mutate through a transaction.** Inside `startCmd`/`endCmd` (or `transactionManager()->transaction(...)`) call `item->undoChangeProperty(...)`, `score->undoAddElement(...)`, or `tx.push(new Cmd(...))`. A raw setter is *not* recorded.
- **Order inside a transaction is the undo order**: `Transaction::push` appends *before* performing so any commands pushed from inside a command's `redo()` land after it and are undone first (`transaction.cpp:115-119` comment).
- **Ownership**: a command that holds a not-yet-inserted (or removed) element owns it and frees it in `cleanup(false)`; the stack calls `cleanup` for dropped redo steps (`undostack.cpp:186-191`) and on destruction (`:101-108`, `wasDone = idx < m_currentIndex`).
- **Rollback** (`endTransaction(rollback=true)`) `unwind()`s the whole step without adding to history; `keepRolledBackElements` leaves rolled-back elements alive for an external owner (`transaction.cpp:196-201`).
- **Dummy transaction**: with no active transaction, `currentOrDummyTransaction()` returns a static dummy whose `push` = `redo()` + `delete`, `pushWithoutPerforming` = `delete` (`transaction.cpp:100-130`) — used for load/layout-time mutations that must not hit history.

### 3A.5 Text editing and the undo stack

`TextEditUndoableCommand` (`editing/textedit.h:66`, `CommandType::TextEdit`) stores a `TextCursor` copy; `matchesFilter(TextEdit, target)` lets the editor find/remove its own commands. `TextEditData` (`:35`, an `ElementEditData`) carries `oldXmlText`, **`startUndoIdx`** (stack index when editing began), selection strings. `UndoStack::undo` short-circuits when `startUndoIdx == m_currentIndex` (above); `changesInfo` sets `isTextEditing` from `cursor().editing()` (`undostack.cpp:477-479`). This is the "complex unsafe direct UndoStack manipulation" the `transaction.h` comment wants to replace (nested stacks would remove the need for `mergeTransactions`).

---

## 4. Cross-subsystem dependencies

- **Layout & rendering** (`layout-and-rendering.md`): the DOM holds logical content; `src\engraving\rendering\score\` computes each item's `LayoutData` (position/shape/bbox) and paints it. `Score::doLayout()` / `triggerLayout()` kick it off. Items declare their own `LayoutData` struct via `DECLARE_LAYOUTDATA_METHODS`.
- **Style** (`style-system.md`): property defaults and "styled" values come from `MStyle`/`Sid`; `ElementStyle` tables bind `Pid`↔`Sid`. `Score::style()`.
- **Read/write** (`file-format-and-rw.md`): `src\engraving\rw\` serialises the DOM to/from `.mscx` XML using the property system (`propertyFromString`/`propertyToString`, `Pid` names) and `EID` object ids. `EngravingItem::readType`/`mimeData` handle clipboard.
- **Notation facade & undo** (`notation-layer.md`): interactive edits go through `src\notation` and the undo stack reached via `MasterScore::undoStack()`; the `undo*` methods on `Score`/`EngravingObject` build undoable commands. The command/undo objects live in `src\engraving\editing\` (`transaction\` for the stack/transaction machinery) — see §3A. `NotationUndoStack` (`src\notation\internal\notationundostack.cpp`) calls `transactionManager()->undoRedo(...)`.
- **Playback** (`playback-and-audio.md`): `Note::playEvents()`, `sigmap()`/`tempomap()`, `repeatList()` convert the DOM into timed audio events.
- **Plugin API** (`plugin-api.md`): `src\engraving\api\v1` wraps DOM items (`EngravingItem`, `Note`, `Chord`, `Segment`, cursor) for QML; it has its **own** `apitypes.h` `ElementType` mirror — do not confuse it with `dom`'s.

---

## 5. Non-obvious gotchas

- ⛔ **`Score` is an `EngravingObject`, not an `EngravingItem`.** It has no bbox/position; don't treat it as a drawable item. Same for `Part` (`part.h:61`) and `DummyParent`. ⚠ **`Staff` (`staff.h:59`) and `BracketItem` (`bracketitem.h:38`) ARE `EngravingItem`s** in this tree (an earlier revision of this Guide wrongly listed them as `EngravingObject`-only).
- ⭐ **RTTI is `type()`-based, not `dynamic_cast`.** Always use `e->isChord()` / `toChord(e)` (the macro helpers in `engravingobject.h`), never `dynamic_cast`. `toX()` asserts the type in debug and is a plain cast in release — passing the wrong type is UB in release.
- ⭐ **`is*()` groups are hand-maintained unions.** e.g. `isTie()` is true for `TIE`, `LAISSEZ_VIB`, **and** `PARTIAL_TIE`; `isSlur()` includes `HAMMER_ON_PULL_OFF` and `TAPPING_HALF_SLUR`; `isChordRest()` = rest-family ∪ chord. Read the actual predicate (`engravingobject.h:520-639`) before assuming a single type.
- **`parent()` vs `ownershipParent()` vs `layoutParent()`** are three different things (tree link incl. dummy / real attachment / visual placement). A `Measure`'s owner is the `Score` but its `layoutParent()` is a `System`. Using the wrong one is a classic bug source.
- **Computed vs logical state.** On-screen geometry lives in `LayoutData` (`ldata()`), separate from logical fields. Reading `ldata()->pos()` before layout has run triggers an `LD_ACCESS` warning and may be uninitialised. The `up` flag on `ChordRest` is in its `LayoutData`, not a plain member.
- **Segment track slots can be null.** `segment->element(track)` is `nullptr` for an empty voice; iterate defensively. The slot vector size is `nstaves*VOICES` and is resized on staff insert/remove.
- **`.cpp`-only files.** `dom\check.cpp`, `dom\midimapping.cpp` have no header — don't assume a `.h/.cpp` pair for every file.
- **Change properties via `undoChangeProperty`, not the field setter**, at runtime — otherwise the change isn't undoable and won't propagate to linked staves/parts.
- **Three pitch representations must stay consistent.** Setting `pitch` without fixing `tpc`/`line` (or vice-versa) yields wrong spelling/position. Use `setPitch(pitch,tpc1,tpc2)` or `setTpcFromPitch()`.

## 6. UNCLEAR — needs verification (follow-up sub-pass)

- **Member tables now exist for every concrete element class** (§2.4-§2.15; headers read in full). What remains unresolved is **behaviour, not inventory**: the `Pid` `switch` contents of each class's `getProperty/setProperty/propertyDefault`, layout algorithms, and `drop()`/`acceptDrop()` rules were **not** traced (only headers were read for §2.15). `Note` (§2.5) and `Chord`/`Rest` (§2.4) are summarised by accessor list rather than exhaustive member tables; `Spanner`/`SLine` are covered at §2.6 level; the non-element support types `Selection`, `InputState`, `Excerpt` were deliberately not tabulated.
- **Base-class column of the §3 table** was authored from class declarations; the `STAFF`/`BRACKET_ITEM` rows were found wrong and fixed in this pass. Other rows were not each re-verified against their `: public` line.
- **Editing/undo (§3A)**: the framework (`transaction\`, `ChangeProperty`, `AddElement`, `Score::undo*` entry points) is traced; the ~70 individual command classes in `editing\` were only inventoried by header; the edit-mode machinery (`ElementEditData`, `EditData`, `editposition.h`, drag/grip flow and how it uses `undoPushProperty`) and the rest of the `Score::undo*` family in `edit.cpp` were not traced. `UndoStack::mergeTransactions` callers not enumerated.
- ~~Exact `P_TYPE` ↔ C++ storage-type table~~ — resolved in §2.15.16 (full enum + all constructors read). Not traced: `propertyvalue.cpp` `toQVariant/fromQVariant` and the `propertyType(Pid)` table in `property.cpp` (which `Pid` carries which tag).

---

*Authored from end-to-end reads of `engravingobject.h`, `engravingitem.h`, `property.h`, `types/propertyvalue.h`, `types/types.h` (full `ElementType`), `segment.h`, `measurebase.h`, `measure.h`, `chordrest.h`, `chord.h`, `rest.h`, `durationelement.h`, `note.h`, `spanner.h`, `line.h`, `score.h`, `masterscore.h`, `staff.h`, `part.h`, `factory.h`, `fraction.h`, `linkedobjects.h`, and class-declaration greps across `src\engraving\dom` (2026-11-08, MuseScore 5.0 `main`). The `ElementType→class` table is complete against the enum. **Deep pass (2026-11-08, same tree):** full reads of `beambase.h`, `beam.h`, `tuplet.h`, `hook.h`, `stem.h`, `stemslash.h`, `articulation.h`, `ornament.h`, `slurtie.h`, `slur.h`, `tie.h`, `textlinebase.h`, `hairpin.h`, `dynamic.h`, `ottava.h`, `pedal.h`, `volta.h`, `trill.h`, `glissando.h`, `barline.h`, `clef.h`, `keysig.h`, `key.h`, `timesig.h`, `harmony.h`, `chordlist.h`, `realizedharmony.h`, `figuredbass.h`, `fret.h`, `instrument.h`, `drumset.h`, `stringdata.h`, `stafftype.h`, `staff.h`, `part.h`, `system.h`, `page.h`, `measurenumberbase.h`, `stafftextbase.h`, `textbase.h`; and for §3A `editing\transaction\{undoablecommand,transaction,undostack}.{h,cpp}`, `editing\editproperty.{h,cpp}`, `editing\addremoveelement.{h,cpp}` (first ~160 lines), `editing\cmd.{h,cpp}` (relevant parts), `editing\textedit.h` (head), and the `undo*` functions in `dom\engravingobject.cpp` / `editing\edit.cpp`. **Polish pass (2026-11-08, same tree):** full reads of `lyrics.h`, `arpeggio.h`, `chordbracket.h`, `tremolosinglechord.h`, `tremolotwochord.h`, `fermata.h`, `accidental.h`, `box.h`, `guitarbend.h`, `hammeronpulloff.h`, `tapping.h`, `chordtextlinebase.h`, `letring.h`, `palmmute.h`, `whammybar.h`, `rasgueado.h`, `harmonicmark.h`, `pickscrape.h`, `vibrato.h`, `textline.h`, `noteline.h`, `gradualtempochange.h`, `expression.h`, `tempotext.h`, `marker.h`, `jump.h`, `bsymbol.h`, `symbol.h`, `image.h`, `bend.h`, `tremolobar.h`, `stafftext.h`, `systemtext.h`, `tripletfeel.h`, `rehearsalmark.h`, `playtechannotation.h`, `capo.h`, `stringtunings.h`, `stavesharinglabel.h`, `soundflag.h`, `playcounttext.h`, `instrchange.h`, `harppedaldiagram.h`, `fingering.h`, `sticking.h`, `breath.h`, `ambitus.h`, `chordline.h`, `notedot.h`, `ledgerline.h`, `deadslapped.h`, `layoutbreak.h`, `spacer.h`, `staffstate.h`, `stafftypechange.h`, `stafflines.h`, `systemdivider.h`, `measurerepeat.h`, `mmrest.h`, `mmrestrange.h`, `measurenumber.h`, `text.h`, `instrumentname.h`, `bracket.h`, `bracketitem.h`, `parenthesis.h`, `anchors.h`, the indicator headers, `stavesharingchange.h`, `sharedpart.h`, `lasso.h`, `shadownote.h`, `tabdurationsymbol.h`, `actionicon.h`, `bagpembell.h`, `rootitem.h`, `dummyparent.h`, `types/propertyvalue.h` (whole), the relevant enums in `types/types.h`, and spot-checks of `propertyvalue.cpp`, `fingering.cpp`, `capo.cpp`, `rootitem.cpp`, `parenthesis.cpp`, `anchors.cpp`. Statuses: header-level member tables ✅ for all element classes; behavioural (`.cpp`) tracing of individual classes remains per §6. **If this contradicts the live source, the source wins.***
