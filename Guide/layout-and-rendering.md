# MuseScore — Layout & Rendering

> **What this file is.** Deep-pass (✅) description of how the MuseScore 5.0 `main` engraving layer turns the DOM into positioned geometry (layout) and then pixels/vector ops (paint): `src\engraving\rendering\` (`score\`, `single\`, `editmode\`), the layout-trigger machinery in `src\engraving\dom` + `src\engraving\editing`, and the drawing abstraction in **`muse\framework\draw`** (NOT `src\engraving\draw`). Authored from the live source; **if this contradicts the code, the code wins** — fix this file. Start at `00-core.md`. The DOM being laid out is in `engraving-dom.md`. **Map:** §0-14 = pipeline/trigger/spacing/paint; **§15-24 = per-element layout algorithms** (TLayout pattern, chords/accidentals/stems, articulations, beams, slurs/ties, tuplets, text/lyrics/harmony, header/footer, staff sharing).

## 0. 60-second orientation

- ⭐ **Layout and paint are two separate worlds.** Layout *writes* geometry into each item's `LayoutData` (`ldata()`: `pos`, `shape`/`bbox`, `mag`, plus per-class fields). Paint *reads* `ldata()` and issues `muse::draw::Painter` calls. Items carry no draw logic themselves — it is all in static dispatchers `TLayout::layoutItem` (layout) and `TDraw::drawItem` (paint) in `src\engraving\rendering\score\`.
- ⭐ **Three renderers, three interfaces**, all registered as global-IoC exports in `src\engraving\engravingmodule.cpp:113-115`:
  | Interface | Impl | Purpose |
  |---|---|---|
  | `IScoreRenderer` (`rendering\iscorerenderer.h:98`) | `score::ScoreRenderer` | full-score layout (`layoutScore`) + paint (`paintScore`, `paintItem`, `drawItem`) |
  | `ISingleRenderer` (`isinglerenderer.h:34`) | `single::SingleRenderer` | one detached item (palette cells, previews, drag ghosts) — `layoutItem` + `drawItem`, **no score context** |
  | `IEditModeRenderer` (`ieditmoderenderer.h:36`) | `editmode::EditModeRenderer` | draws **only the edit-mode overlay** (grips, text cursor/selection, handle polylines); **does not lay out** |
- ⭐ **Entry chain for layout:** `MasterScore::update()` → `Score::doLayoutRange(st, et)` (`dom\score.cpp:5014`) → `IScoreRenderer::layoutScore` → `ScoreRenderer::layoutScore` → `ScoreLayout::layoutRange` (`rendering\score\scorelayout.cpp:50`) → one of three **view-mode drivers** (page / horizontal / vertical) → `SystemLayout::collectSystem` → `MeasureLayout` / `HorizontalSpacing` / many `*Layout` helper classes.
- ⭐ **Entry chain for paint (score):** `NotationPainting::doPaint` (`src\notation\internal\notationpainting.cpp:114`) → `IScoreRenderer::paintScore` → `Paint::paintScore` (`rendering\score\paint.cpp:38`) → per page `Page::items(rect)` (BSP tree) → sort by z → `Paint::paintItem` → `TDraw::drawItem` → `Painter`.
- Source directory map (`src\engraving\rendering\`):
  - root: `iscorerenderer.h`, `isinglerenderer.h`, `ieditmoderenderer.h`, `layoutoptions.h` (`LayoutMode`, `LayoutOptions`), `paintoptions.h`, `paddingtable.*`.
  - `score\` (build list in `rendering.cmake`): drivers `scorerenderer`, `scorelayout`, `scorepageviewlayout`, `scorehorizontalviewlayout`, `scoreverticalviewlayout`; state `layoutcontext`; passes `passbase`, `passresetlayoutdata`, `passlayoutindependentitems`; structure `systemlayout`, `systemheaderlayout`, `pagelayout`, `headerfooterlayout`, `measurelayout`, `segmentlayout`, `modifydom`, `mmrestlayout`, `stavesharinglayout`, `horizontalspacing`; per-element `tlayout`/`tdraw` (the two giant switch files) plus helpers `chordlayout`, `beamlayout`, `beamtremololayout`, `stemlayout`, `restlayout`, `tupletlayout`, `slurtielayout`, `tremololayout`, `arpeggiolayout`, `chordbracketlayout`, `accidentalslayout`, `lyricslayout`, `harmonylayout`, `dynamicslayout`, `textlayout`, `boxlayout`, `markerlayout`, `measurenumberlayout`, `parenthesislayout`, `masklayout`, `guitarbend/divelayout`, `tappinglayout`, `alignmentlayout`, `autoplace`, `verticalgapdata`; paint `paint`, `tdraw`; debug `debugpaint`, `paintdebugger`, `dumplayoutdata`.
  - `single\`: `singlerenderer`, `singlelayout`, `singledraw`. `editmode\`: `editmoderenderer`.

## 1. Layout modes (`LayoutMode`) and `LayoutOptions`

`rendering\layoutoptions.h:34-47`:

```cpp
enum class LayoutMode : char { PAGE, FLOAT, LINE, SYSTEM, HORIZONTAL_FIXED };
struct LayoutOptions { LayoutMode mode = PAGE; bool isShowVBox = true; double noteHeadWidth = 0.0;
  bool isLinearMode() const { return mode == LINE || mode == HORIZONTAL_FIXED; } };
```

| Mode | Meaning (from the header comment + driver) | Driver |
|---|---|---|
| `PAGE` | normal page view; honors page/line/section breaks, system & page locks | `ScorePageViewLayout` |
| `FLOAT` | "reflow": ignores page+line breaks (`collectSystem` forces `lineBreak=false`, `systemlayout.cpp:291`), also `firstSystem` not set from section break | `ScorePageViewLayout` |
| `SYSTEM` | "never-ending page": page breaks become line breaks; `breakPages=false` in `collectPage` (`pagelayout.cpp:137`); page height = content height (`:425-429`) | `ScoreVerticalViewLayout` |
| `LINE` | "panoramic": the **entire score is ONE system on ONE page**, no pagination | `ScoreHorizontalViewLayout` |
| `HORIZONTAL_FIXED` | like LINE but with duration-proportional fixed cell widths (practice mode) | `ScoreHorizontalViewLayout` |

Stored in `Score::m_layoutOptions` (`dom\score.h:1008`); set by `Score::setLayoutMode` (`score.h:752`). `LayoutConfiguration` (`layoutcontext.h:91`) is the *read-only* façade the layout code uses to read mode/style (`conf().isLinearMode()`, `conf().styleD(Sid::…)`).

## 2. The LayoutContext (all state of a layout run)

`rendering\score\layoutcontext.h:390` — `class LayoutContext : IGetScoreInternal`, constructed from a `Score*` (non-copyable). It aggregates three views over the same score:

| Member | Class | Role |
|---|---|---|
| `conf()` | `LayoutConfiguration` | read-only: view mode, style (`styleV/styleD/styleB/styleAbsolute`), `spatium()`, page size (`loWidth/loHeight` = `styleD(pageWidth)*DPI`), `isShowVBox`, `noteHeadWidth`, palette/printing flags, engraving font |
| `dom()` / `mutDom()` | `DomAccessor` | narrow DOM façade: `parts()`, `staves()`, `systems()`, `pages()` (mutable), `tick2measure`, `firstMeasure`, `systemLocks()/pageLocks()`, `paddingTable()`, `addElement/removeElement/undoAddElement…`, `spannerMap()` |
| `state()` / `mutState()` | `LayoutState` | **mutable progress state** (below) |

`LayoutState` fields (`layoutcontext.h:299-326`): `m_firstSystem`, `m_firstSystemIndent`, `m_startWithLongNames`, `m_tick`, `m_startTick`, `m_endTick`, **`m_isLayoutAll`**, `m_page`, `m_pageIdx`, **`m_systemList`** ("reusable systems"), `m_prevSystem`/`m_curSystem`, `m_prevMeasure`/`m_curMeasure`/`m_nextMeasure` (a 3-measure sliding window), `m_systemOldMeasure`, `m_pageOldMeasure`, `m_measureNumber`, **`m_rangeDone`**, `m_systemsNeedingCentering`, `m_mustRecomputeHeadersFooters`, `m_totalBracketsWidth` (cache).

Debug call-tree tracing: `LAYOUT_CALL()` macros (`layoutcontext.h:38-55`) compile to nothing unless `MUE_ENABLE_ENGRAVING_RENDER_DEBUG` (CMake option default **OFF**, `CMakeLists.txt:183`).

## 3. How layout is TRIGGERED (dirty-range / incremental layout)

### 3.1 Marking dirty: `triggerLayout*` → `CmdState`

Edits never call layout directly. Mutating code calls `item->triggerLayout()` (virtual, `dom\engravingitem.h:493`), which records a **tick range + staff range + element** in the `MasterScore`'s `CmdState`:

```cpp
// dom\engravingitem.cpp:2282  EngravingItem::triggerLayout
if (isInScoreTree()) { score()->setLayout(tick(), staffIdx(), this); }
// :2297  triggerLayoutAll  -> score()->setLayoutAll(staffIdx(), this)   // "extremely expensive and likely unnecessary"
// :2304  triggerLayoutToEnd -> setLayout(tick(), score()->endTick(), staffIdx(), staffIdx(), this)
```

Overrides widen the range: `Measure::triggerLayout` (`measure.cpp:3411`) uses `[tick(), endTick()]` over all staves; `MeasureBase::triggerLayout` (`measurebase.cpp:534`) uses the top-level MB; also overridden in `Beam`, `Lyrics`, `Spacer`, `Spanner`/`SpannerSegment`, `Staff`, `StringTunings`, `TremoloTwoChord`.

`Score::setLayout*` forward to the master (`score.cpp:5333-5337`); `MasterScore::setLayout` / `setLayoutAll` (`dom\masterscore.cpp:370-419`):

- `setLayoutAll` = `setTick(0)` + `setTick(Fraction::max())` (+ staff/element).
- `setLayout(t, …)` = `setTick(t)` (+ staff/element).

`CmdState` (`src\engraving\editing\cmd.h:50`, impl `cmd.cpp:111-255`) **accumulates a min/max tick**: `setTick` widens `[m_startTick, m_endTick]` and calls `setUpdateMode(UpdateMode::Layout)`; `setStaff` widens the staff range; `setElement` tracks "the" element (or its MeasureBase if several share one). `setTick/setStaff/setElement` are no-ops while `m_locked`. `UpdateMode` (`types\types.h:1514`): `DoNothing < Update < UpdateAll < Layout` (only ever increases; `CmdState::setUpdateMode` `cmd.cpp:234`). `LayoutFlag` (`types.h:1525`): `PLAY_EVENTS`, `REBUILD_MIDI_MAPPING`.

### 3.2 Flushing: `MasterScore::update`

`TransactionManager::endTransaction` (`editing\transaction\transaction.cpp:178-205`) calls `m_masterScore->update(...)`. `MasterScore::update(resetCmdState, layoutAllParts)` (`editing\cmd.cpp:350`):

```cpp
if (m_cmdState.layoutRange()) {
    for (Score* s : scoreList()) {
        if (s != this && !s->isOpen() && scoreList().size() > 1 && !layoutAllParts) { continue; }  // skip closed parts
        s->doLayoutRange(m_cmdState.startTick(), m_cmdState.endTick());
    }
    updateAll = true;
}
```

So **one layout pass per score in `scoreList()` (master + excerpts/parts)**, only for open parts unless `layoutAllParts`. Then `updateTicksAndTimeSigMap` if needed, then viewers' `updateAll()` / `dataChanged(rect)`, then `m_cmdState.reset()`. `MasterScore::lockUpdates` (`cmd.cpp:411`) makes `update()` a no-op (`m_updatesLocked`).

### 3.3 `Score::doLayoutRange` / `doLayout`

`dom\score.cpp:5007-5041`: `doLayout()` = `doLayoutRange(Fraction(0,1), Fraction(-1,1))` (i.e. layout all). `doLayoutRange` additionally (1) **widens the range by overlapping spanners** (`spannerMap().findOverlapping(st.ticks(), et.ticks())` → `start=min(st, spanner->tick())`, `end=max(et, spanner->tick2())`, skipping spanners on invisible staves), (2) reloads the engraving font and `m_layoutOptions.noteHeadWidth`, (3) rebuilds the MIDI mapping if `LayoutFlag::REBUILD_MIDI_MAPPING`, (4) calls `renderer()->layoutScore(this, start, end)`.

### 3.4 `ScoreLayout::layoutRange` — range → "layout all?"

`scorelayout.cpp:50-96`:

1. RAII `CmdStateLocker` **locks the CmdState for the whole layout** (so layout code that calls `triggerLayout` can't re-dirty the score).
2. Builds a `LayoutContext(score)`.
3. **Empty score** (no `last()`, or linear mode with no first measure): delete all systems+pages, `PageLayout::getNextPage`, return.
4. `isLayoutAll = stick <= 0 && (etick < 0 || etick == Fraction::max())`; clamp `stick<0 → 0`, `etick<0/max → score->last()->endTick()`; `ctx.mutState().setIsLayoutAll(isLayoutAll)`.
5. `switch (ctx.conf().viewMode())` → `PAGE/FLOAT` → `ScorePageViewLayout::layoutPageView`; `LINE/HORIZONTAL_FIXED` → `ScoreHorizontalViewLayout::layoutHorizontalView`; `SYSTEM` → `ScoreVerticalViewLayout::layoutVerticalView`.

### 3.5 Range layout = restart at the first affected system, stop at stability

This is the incremental mechanism (traced in `ScorePageViewLayout`, `scorepageviewlayout.cpp`; the vertical driver is a near-copy):

- **`initLayoutContext`** (`:45`): finds `m = score->tick2measure(stick)`, **steps back one measure** ("handle clefs and cautionary elements", `:60-69`), steps to a real measure, and if it's inside an mmrest uses `mmRest()`. If `!isLayoutAll && m->system()`: the *current system* becomes `m->system()`, `state.setSystemList(mid(score->systems(), systemIndex))` (those systems become the **reusable pool**), restores page/pageIdx, `nextMeasure` (= first measure after the previous system), and recovers `measureNumber` and `tick` from the preceding measure (honoring `startWithMeasureOne` section breaks, `:106-125`). Otherwise `nextMeasure = first/firstMeasure`.
- **`prepareScore`** (`:133`): partial → erase `score->systems()` from `systemIndex` to end (they live on in `state.systemList()` for reuse); full → clear brackets' selection, null every MeasureBase's system, `DeleteAll(systems)` and `DeleteAll(pages)`.
- **`PassResetLayoutData`** (`passresetlayoutdata.cpp:45`) **always runs**: if layoutAll it recursively `mutldata()->reset()` over the whole score tree; otherwise only for MeasureBases from `nextMeasure` while `tick <= endTick`. `LayoutData::reset()` clears `m_shape` and `m_mask` (position reset is deliberately commented out, `engravingitem.h:521-527`).
- `doLayout` (`:200`) loops `getNextMeasure → collectSystem`, then `do { getNextPage; collectPage } while (curSystem && !(rangeDone && lmb == pageOldMeasure))`. **Stop condition** (comment at `:219-228`): end of score, OR (range done AND this page's last measure equals the last measure from the *previous* layout of that page — "point of stability"). `rangeDone` is set inside `collectSystem` when the processed system ends where the old one did (`systemlayout.cpp:343-396`: `endTick < prevMeasure->tick()` and `prevMeasure == systemOldMeasure` and no joined beams into the next measure); in that case later systems are *reused unchanged* (`collectPage` takes `systemList` entries instead of re-collecting, `pagelayout.cpp:231-243`).
- `layoutFinished` (`:234`): drop unused pooled systems and trailing pages if the score ended; append leftover reusable systems back to `score->systems()`; recompute headers/footers if they contain a page count (`mustRecomputeHeadersFooters`).
- ⚠ In **linear modes** only `[startTick, endTick]` measures are fully re-laid-out; measures outside the range keep their layout and are merely *translated* (`scorehorizontalviewlayout.cpp:370-396`, `MeasureLayout::layoutMeasure` early-returns outside the range `measurelayout.cpp:194`).

### 3.6 `needLayout` per measure

`MeasureLayout::getNextMeasure` sets `ldata()->setNeedLayout(true)` on every measure it advances onto (`measurelayout.cpp:388-401`); `layoutMeasure` early-returns when `!needLayout()` and clears it. This is how a measure collected twice in one system-building loop (the "add measure, check width, remove if overflow" probing) is only expensively laid out once.

## 4. The layout PIPELINE (score layout, PAGE mode), in order

```
Score::doLayoutRange                                   dom\score.cpp:5014
 └ ScoreRenderer::layoutScore                          rendering\score\scorerenderer.cpp:49
    └ ScoreLayout::layoutRange                         scorelayout.cpp:50
       └ ScorePageViewLayout::layoutPageView           scorepageviewlayout.cpp:169
          1 initLayoutContext   (restart point, reusable systems/pages)
          2 prepareScore        (discard stale systems/pages)
          3 PassResetLayoutData (reset ldata)
          3b PassLayoutIndependentItems  (ONLY #ifdef MUE_ENABLE_ENGRAVING_LD_PASSES, off by default; :186-191)
          4 doLayout:
             MeasureLayout::getNextMeasure                       (advance window, tick/measure#, create mmrests)
             SystemLayout::collectSystem                         (FIRST system; see 4.1)
             loop { PageLayout::getNextPage; PageLayout::collectPage (see 4.4) }
             SystemLayout::centerPendingSystems
          5 layoutFinished
```

### 4.1 `SystemLayout::collectSystem` (`systemlayout.cpp:102-488`) — the heart

Builds ONE system and returns it; `collectPage` calls it repeatedly. Stages, in order:

1. **Section/first-system setup** (`:110-128`): look at the previous system's last measure `mbWithPrecedingSectionBreak()`; sets `firstSystem`, `firstSystemIndent`, `startWithLongNames` from `LayoutBreak` properties.
2. `getNextSystem` (`:568`): take a system from the reusable `systemList` (record `systemOldMeasure`) or `Factory::createSystem`; push to `dom.systems()`; `adjustStavesNumber`; mirror staff `show`. Skylines cleared. `targetSystemWidth = styleD(pagePrintableWidth) * DPI` (`:139`). A **system lock** (`dom().systemLocks()->lockStartingAt(curMeasure)`, PAGE/SYSTEM modes only) forces the exact measure set and disables the width-based break.
3. **Measure-collection loop** (`:158-341`): for each `curMeasure`
   - `system->appendMeasure`; stave-sharing update (`StaveSharingLayout::updateStaveSharingForLastAddedMeasure`, if no lock);
   - `MeasureLayout::layoutMeasure` (§5) — measure-local, system-independent work;
   - for real measures: `computePreSpacingItems`, cross-beam fixups, first-in-system → `SystemLayout::layoutSystem` + `MeasureLayout::addSystemHeader`; `createEndBarLines(m,true)`; `addSystemTrailer` (cautionary clef/key/time) unless `noBreak`/locked; `setRepeatCourtesiesAndParens`; `updateGraceNotes`; **`HorizontalSpacing::updateSpacingForLastAddedMeasure`** returns running `curSysWidth`;
   - HBox → just spacing; VBox → `getNextMeasure`, `layout2`, `return system` (a frame is a system by itself);
   - **overflow test** (`:225`): `doBreak = !systemLock && measures.size()>1 && curSysWidth > targetSystemWidth && !prevMeasure->noBreak()` → remove the last measure (and walk back over `noBreak` runs) and `break`;
   - forced break (`:285-296`): in PAGE/SYSTEM `pageBreak||lineBreak||sectionBreak||end-of-system-lock||end-of-page-lock||next starts a lock`; in FLOAT/LINE/HORIZONTAL_FIXED never.
   - After each accepted measure the previous one gets its *non-last* end barline / trailer removed and key signatures updated (`:260-280`).
4. **Range-stability check** (`:343-397`) → sets `rangeDone` and restores the pre-probe state of the next system's first measure(s) (`prevMeasureState`, `restoreMeasure`, `restoreBeams`).
5. **Finalize** — "SYSTEM NOW HAS A COMPLETE SET OF MEASURES" (`:404`): `BeamLayout::breakCrossMeasureBeams`; `hideEmptyStaves`; recreate all segment shapes (`seg.createShapes()`); `layoutSystem` again (header widths/brackets/names for the final staff set); final `createEndBarLines` + `addSystemTrailer` on the last measure; `updateTimeSigAboveStavesXPos`; **`HorizontalSpacing::computeSpacingForFullSystem`** (authoritative spacing); if too wide → `squeezeSystemToFit`; if `shouldBeJustified` → `justifySystem`; `clearBigTimeSigNotShown`.
6. **Place elements** (`:456-476`): per measure `MeasureLayout::layoutMeasureElements` + `layoutStaffLines` (+ brackets after HBox headers); then **`layoutSystemElements`** (§6) and **`SystemLayout::layout2`** (vertical staff distances, §7).

### 4.2 Pipeline summary table (what runs when)

| Phase | Function | Scope | Notes |
|---|---|---|---|
| a. Dom normalization | `ModifyDom::*` | per measure | segment sort, stems created, cross-measure flags |
| b. Measure-local layout | `MeasureLayout::layoutMeasure` | per measure | chord/beam/accidental layout, shapes |
| c. Horizontal | `HorizontalSpacing::*` | per system | springs, kerning, squeeze/justify |
| d. System elements | `SystemLayout::layoutSystemElements` | per system | ordered sequence, builds skylines |
| e. Vertical (inside system) | `SystemLayout::layout2` | per system | staff y offsets from skylines |
| f. Page | `PageLayout::collectPage`/`layoutPage` | per page | system y, vertical justification |
| g. Cross-staff/post | `PageLayout::collectPage` tail | per page | cross-staff beams/tuplets/slurs, barline2, centering, masks |

## 5. Measure-local layout (`MeasureLayout::layoutMeasure`, `measurelayout.cpp:182`)

Guards: skip if `!needLayout`, if outside linear range, or if all staves are invisible. Then, in order:

1. `checkStaffMoveValidity` (cross-staff legality, **must precede** `cmdUpdateNotes`, `:204-206`).
2. **Modify-DOM block** (`:208-215`, `modifydom.cpp`): `ModifyDom::setCrossMeasure`, `connectTremolo`, `cmdUpdateNotes` (pitch→line/accidental), `createStems`, `setTrackForChordGraceNotes`, `sortMeasureSegments` (large segment-ordering routine, `modifydom.cpp:211-531`).
3. Per staff × segment (`:224-251`): `SegmentLayout::layoutMeasureIndependentElements`, then for ChordRest segments `setChordMag`, `layoutChordDrumset`, `computeChordsUp` (stem direction), `layoutChordsStem`.
4. `BeamLayout::createBeams` then a **dummy `layoutNonCrossBeams`** pass — the in-code HACK comment (`:255-259`): real beams need final x-positions, but spacing needs stem extensions that depend on beams, so beams are computed *before* spacing just to extend stems.
5. Per visible staff × ChordRest segment (`:274-296`): `ChordLayout::layoutChords1` (note/accidental/dot collision placement), `RestLayout::resolveVerticalRestConflicts`, lyrics `TLayout::layoutLyrics`.
6. Breaths, `ChordBracketLayout::layoutSegment`, symbol/harmony/fretdiagram annotations (`:298-313`).
7. Start-repeat barline (`:315-330`).
8. **`Segment::createShapes()`** for every non-end-barline segment (`:332-337`) → per-staff `Shape` (`segment.cpp:2604`; `setVisible(false)` first, then `createShape(staffIdx)` per staff). Then `measure->computeTicks()` (must come after createShapes — it relies on segment visibility, `:339-340`).

`MeasureLayout` also owns courtesy clef/key/time signatures (`setCourtesyTimeSig/KeySig/Clef`), repeat courtesy parentheses, **system header/trailer** (`addSystemHeader`, `addSystemTrailer`, `createSystemBeginBarLine`), `stretchMeasureInPracticeMode`, `layoutMeasureNumber`, `layoutPlayCountText`, `layoutTimeTickAnchors`, and `layout2` (after page layout: spacers, layout-break elements, cross-staff ties — `:83`).

## 6. Horizontal spacing (`HorizontalSpacing`, `horizontalspacing.cpp`)

Model: **springs + shapes + padding table**.

- `computeSpacingForFullSystem(system, stretchReduction=1, squeezeFactor=1, overrideMinMeasureWidth=false)` (`:49`): builds a `HorizontalSpacingContext`, walks measures; HBoxes get `computeMinWidth`; runs of Measures are "measure groups" passed to `spaceMeasureGroup`. `systemIsFull=true`.
- `updateSpacingForLastAddedMeasure(system, startOfContinuousLayoutRegion)` (`:92`): **incremental** — only re-spaces the last two measures (secondToLast+last) using the already-known `xCur`; this is why the width probe in `collectSystem` is cheap.
- `spaceMeasureGroup` (`:226`) → `spaceSegments` (`:264`) → `moveRightAlignedSegments` → `setPositionsAndWidths` → `enforceMinimumMeasureWidths`. `spaceSegments` places each segment against previous ones (`spaceAgainstPreviousSegments`), adds `extraLeadingSpace`, enforces `systemHeaderMinStartOfSystemDistance` for the first chord of a system, and adds the ChordRest segment's **natural width**.
- **Natural width** (`chordRestSegmentNaturalWidth`, `:734`): `3.5sp × durationStretch × userStretch(clamped 0.1..10) × stretchReduction / spacingDensity`. **Duration stretch** (`computeSegmentDurationStretch` `:752`, `durationStretchForTicks` `:809`): `slope ^ log2(ticks / quarter)` with `slope = Sid::measureSpacing`; handles polyrhythms (uses shortest ChordRest ticks), mmrests (`durationStretchForMMRests` `:787`, uses `mmRestConstantWidth`/`mmRestMaxWidthIncrease`), small-note scaling.
- **Minimum distance between segments** (`minHorizontalDistance(const Segment*, const Segment*, squeeze)` `:1300`, and the `Shape`-level overload `:1222`): for each staff, for every pair of shape elements that vertically overlap (`computeVerticalClearance`), distance = `r1.right() − r2.left() + padding`, with **kerning** (`KerningType`: `NON_KERNING`, `KERN_UNTIL_LEFT_EDGE/CENTER/RIGHT_EDGE`, `ALLOW_COLLISION`; `computeKerning` `:1630`). Padding comes from `PaddingTable` (`rendering\paddingtable.*`, `computePadding` `:1452`), with special cases for notes, ledger rests, lyrics, chord brackets; system-header gap rules (`needsHeaderSpacingExceptions` `:1418`, `systemHeaderDistance` styles); mmrest minimums; hanging tie/glissando widths (`computeHangingLineWidth`).
- **Fitting** (called from `collectSystem`): if `curSysWidth > target` → `squeezeSystemToFit` (`:137`) — first reduces shape padding and duration stretch in steps (`SQUEEZE_STEP=0.2`, `STRETCH_REDUCTION_STEP=0.33`) re-running full spacing each time; if still too wide, **gives up and allows collisions** by shrinking ChordRest segment widths (`smallerStep = 0.25*SQUEEZE_STEP`, `:163-184`). If narrower and `shouldBeJustified` → `justifySystem` (`:187`): distributes the remaining width over ChordRest segments as **springs** (`springConst = 1/segment.stretch()`, `stretchSegmentsToWidth` `:1093`), then `respaceSegments()` and re-sets measure x's. Last-system rule `shouldBeJustified` (`systemlayout.cpp:490`): don't justify if fill ratio < `Sid::lastSystemFillLimit` (unless followed by more measures/HBox and no section break) or if `MScore::noHorizontalStretch`.

## 7. Vertical layout inside a system: element placement, skylines, staff distance

### 7.1 `SystemLayout::layoutSystemElements` (`systemlayout.cpp:1225`)

Ordering matters: **the skyline grows as elements are laid out, so what is laid out first ends up closest to the staff** (comment `:1288-1291`). Sequence (abridged, exact order from source):
measure numbers / mmrest ranges / play-count / time-tick anchors → collect everything to lay out (`collectElementsToLayout` `:1452`) → grace-note groups, tab duration symbols → `BeamLayout::layoutNonCrossBeams` (real beams now) → `RestLayout::alignRests` + `checkFullMeasureRestCollisions` → barline shapes → **`createSkylines`** (`:1702`) → ties & bends (`layoutTiesAndBends`) → articulations (1,2) + chord fingering → tuplets → slurs (`processLines`) + `layoutArticulations3` + hammer-on alignment → trills → stickings → fermatas/tremolo bars → figured bass → lyrics (position depends on `Sid::lyricsStackingOrder`: before dynamics for 5.0, or after pedals for <5.0 compat) → dynamics/expressions/hairpins → other spanners → measure numbers into skyline → mmrest ranges → ottavas → pedals → harp diagrams → harmonies / fret diagrams → staff text → instrument changes → playing-technique/capo/string-tunings/triplet-feel → system text → voltas → rehearsal marks → tempo text + tempo-change lines (aligned via snapping chain) → play counts → markers/jumps → images → stave-sharing changes → parentheses + big time sigs.

`processLines` (`:1917`) lays out spanners **system-by-system** via `TLayout::layoutSystem(spanner, system, ctx)` (creates/lays out the `SpannerSegment` for this system), optionally aligns Y across the group (pedals), fixes slur overlaps and harmonic-mark/vibrato overlaps, then **adds each segment's shape to the staff skyline**.

### 7.2 Shapes and Skylines (`src\engraving\infrastructure\`)

- **`Shape`** (`infrastructure\shape.h:69`) = vector of `ShapeElement` (`:46`; a `RectF` + `const EngravingItem*` + `ignoreForLayout`). Types `Fixed`/`Composite`. Ops: `add/remove/translate/scale/pad/transform`, `minVerticalDistance`, `verticalClearance`, `topAtX/bottomAtX`, `intersects`, `rightMostEdgeAtHeight`, `paint` (debug). Each item's shape lives in `ldata()->m_shape` (`engravingitem.h:635`); `EngravingItem::shape()` returns it. `bbox()` of an item is derived from its shape (`LayoutData::bbox` / `setBbox` / `addBbox`).
- **`Skyline`** (`infrastructure\skyline.h:102`) = `SkylineLine _north` + `_south` (`SkylineLine` `:44` wraps a `Shape` plus staff-line edge map). `add(Shape/RectF,item)`, `minDistance(otherSkyline, minHorizontalClearance)`, `top/bottom(startX,endX)`. One `Skyline` per **`SysStaff`** (`system->staff(i)->skyline()`).
- `createSkylines` (`systemlayout.cpp:1702-1817`): for each staff: staff lines → barlines → time sigs (unless `ACROSS_STAVES` placement) → every enabled segment element whose `addToSkyline()` (`engravingitem.h:433`: not invisible, not `NO_AUTOPLACE`, not `isSkipDraw`) with grace notes, ornament cue notes, **minus** cross-staff arpeggio/chord-bracket (re-added with clipping), plus two-chord tremolo and beam skylines (`Beam::addSkyline`).
- **Autoplace** (`rendering\score\autoplace.cpp`): `Autoplace::autoplaceSegmentElement/MeasureElement/SpannerSegment` push an item away from the staff skyline until `distanceToMoveItem` clears it (`itemsShouldIgnoreEachOther` filters), then optionally `add` the moved shape.

### 7.3 `SystemLayout::layout2` (`:2212`) — staff distances

Given finished skylines, sets each visible `SysStaff`'s y and bbox: base distance = staff height + `akkoladeDistance` (same part) or `staffDistance` (different parts) [or `minStaffSpread` if vertical spread enabled] + the next staff's `userDist`; **vertical spacers** (`SpacerType::FIXED` replaces, others max); otherwise `max(dist, skyline.minDistance + minVerticalDistance)` and cross-staff-beam minimum (`minVertSpaceForCrossStaffBeams` `:2353`). In `LINE` mode the per-staff `continuousDist` only grows between partial layouts (comment `:2306-2315`) — it shrinks only on a full layout. Finishes with system height, `setMeasureHeight`, bracket verticals, instrument-name vertical position.

`SystemLayout::minDistance(top, bottom, ctx)` (`:2480`) = minimum gap between two systems from their skylines; used by `PageLayout`.

### 7.4 Centering between staves

Not done per system during collection: `layoutSystemElements` adds the system to `state.systemsNeedingCentering` (not in linear mode, `:1234`); `PageLayout::collectPage` calls `SystemLayout::centerElementsBetweenStaves(system)` per page (`pagelayout.cpp:433-436`) and `centerPendingSystems(ctx)` at the very end of `doLayout` (`scorepageviewlayout.cpp:231`). Implementation: `collectCenterableItems`, `centerItemsInGap`, `centerMMRestBetweenStaves`, `centerBigTimeSigsAcrossStaves` (`systemlayout.cpp:2611-3245`). Continuous (LINE) view does **not** center.

## 8. Page layout (`PageLayout`, `pagelayout.cpp`)

- **`getNextPage`** (`:74`): create or reuse `state.page()` for `pageIdx`; for a reused page it truncates `page->systems()` from the current system on (or clears) and records `pageOldMeasure` (last measure of the old layout of this page — the stability marker); sets page bbox `(0,0,loWidth,loHeight)`; positions the page (`x` = prev page + width + `horizontalPageGapOdd/Even` by parity incl. `pageNumberOffset`; or `y` with `MScore::verticalOrientation()`/`verticalPageGap`).
- **`collectPage`** (`:121-450`): header/footer layout (`HeaderFooterLayout::layoutHeaderFooter`); `y` starts at `page->tm()` (or restores previous systems' positions via `restoreLayout2`); loop: distance from previous system via `SystemLayout::minDistance` (first system: `staffUpperBorder`, `minTop`, header extension, fixed up-spacers); `setPos(page->lm(), y)`; `page->appendSystem`; obtain the **next system** (`collectSystem` if `!rangeDone`, else pop one from the reusable list); page break when `!curSystem` OR (breakPages && (prev `pageBreak()` || end-of-page-lock || next page-lock start)) OR `y + dist >= endY` (`:286`; page locks suppress the height test). On break → `layoutPage(restHeight…)` and exit loop. Tail: fix the last system's header; **post-pass per measure on this page** (`:317-406`): cross-staff beams (`TLayout::layoutBeam` + `checkCrossPosAndStemConsistency`), tuplets with nested tuplets, grace beams/spanners/slurs, arpeggio2, chord-bracket vertical geometry, `ChordLayout::layoutSpanners`, cross-staff tremolo `computeUp`, `TLayout::layoutBarLine2`, then `MeasureLayout::layout2` and lock indicators; re-layout first barlines of the next page if this was the last laid-out page; then `layoutCrossStaffElements` (cross-staff slurs/artic/fingering), `centerElementsBetweenStaves`, big-time-sig centering, **`MaskLayout::computeMasks`**, `layoutSystemDividers`, `page->invalidateBspTree()`.
- **`layoutPage`** (`:558`) — vertical justification: if no stretchable gaps, `MScore::noVerticalStretch`, vertical spread, or SYSTEM mode → nothing (or `distributeStaves` for vertical-spread PAGE mode, `:653`); else it **normalizes inter-system distance**: sort gaps ascending by (distance−height), raise the shortest up to the next tier, repeat, then distribute leftovers equally, never exceeding `maxSystemDistance`; `compactStaves` when `restHeight<=0`.
- `HeaderFooterLayout` handles page headers/footers (`layoutHeaderFooter`, `scoreHasTimestampHeadersFooters`); `ScoreRenderer::layoutHeadersFooters` re-runs only that (PAGE/FLOAT only, `scorerenderer.cpp:54-62`).

## 9. LINE / HORIZONTAL_FIXED and SYSTEM drivers

- **`ScoreHorizontalViewLayout::layoutHorizontalView`** (`scorehorizontalviewlayout.cpp:66`): same restart-measure logic, then `PassResetLayoutData`, then `layoutLinear` = `resetSystems` (layoutAll: delete everything and make **one `Page` with one `System`**) → **`collectLinearSystem`** (`:286`: ALL measures are appended to the single system; per measure `layoutMeasure`; header on the first measure; in-range measures get full layout + `HorizontalSpacing::updateSpacingForLastAddedMeasure(system, firstMeasureInLayout)` (continuous-region start flag), `HORIZONTAL_FIXED` instead uses `layoutSegmentsWithDuration` (`WIDTH_OF_SEGMENT_CELL = 3` spatiums per quantum, `computeCellWidth`) + `stretchMeasureInPracticeMode`; out-of-range measures are only repositioned; VBoxes are detached (`setSystem(nullptr)`), `hideEmptyStaves`, `system->setWidth`) → `layoutLinear(ctx)` (`:162`: `layoutSystemElements`, `layout2`, per-measure cross-beam/tuplet/slur/tremolo second passes and `MeasureLayout::layout2`, page size = `lm + system.width + rm` × `tm + system.height + bm`, masks, BSP invalidate). **There is no page loop and no `centerPendingSystems`.** Target width is `nmeasures × styleAbsolute(minMeasureWidth)` (`:299`).
- **`ScoreVerticalViewLayout::layoutVerticalView`** (`scoreverticalviewlayout.cpp:44`): same restart logic as page view (partial restart using `systemList`), then `PassResetLayoutData`, `getNextMeasure`, `collectSystem`, **`PassLayoutIndependentItems` on layoutAll** (note: unlike page view this one is NOT wrapped in `MUE_ENABLE_ENGRAVING_LD_PASSES` — `:151-154`), then the same `getNextPage`/`collectPage` loop (`:159`). Because `breakPages=false` in SYSTEM mode, everything lands on one growing page.

## 10. Per-element layout and the shared helper classes

- **`TLayout::layoutItem(EngravingItem*, LayoutContext&)`** (`tlayout.cpp:196`) is a single `switch (item->type())` dispatching to ~150 `TLayout::layoutX(const X* item, X::LayoutData* ldata, ctx)` functions; the **`default:` case logs "not found in layout types" and `DO_ASSERT(false)`** (`:516-519`) — a new `ElementType` MUST be added here (and to `TDraw::drawItem`, and `SingleLayout::layoutItem`/`SingleDraw` if palette-able). Signature convention: `item` is `const`; results go to the **mutable `ldata`** (`item->mutldata()`).
- `ScoreRenderer::doLayoutItem` (`scorerenderer.cpp:84`) = `LayoutContext ctx(item->score()); TLayout::layoutItem(item, ctx);` — i.e. lay out one item *in its score* (used e.g. by edit commands). `IScoreRenderer::layoutItem<T>` is a template with a `Supported` variant type check in debug (`iscorerenderer.h:132-184`) — a temporary compatibility whitelist.
- Helper classes (each `static` methods taking `LayoutContext&`): `ChordLayout` (chord/note layout, `layoutChords1/2/3` collision resolution, dots, ledger lines, `computeUp`, articulations 1/2/3, grace notes, `fillShape`), `BeamLayout` (`createBeams`, `layoutNonCrossBeams`, cross-staff, `breakCrossMeasureBeams`, `restoreBeams`), `StemLayout`, `TremoloLayout`, `TupletLayout` (`layoutTupletAndNestedTuplets`), `SlurTieLayout` (`layoutSystem(Slur)`, `layoutTieFor/Back`, `computeBezier`, `avoidCollisions`, partial ties, laissez-vibrer), `RestLayout`, `AccidentalsLayout`, `LyricsLayout`, `HarmonyLayout`, `DynamicsLayout`, `TextLayout`, `ParenthesisLayout`, `ChordBracketLayout`, `ArpeggioLayout`, `BoxLayout`, `MMRestLayout` (`createMultiMeasureRestsIfNeed`, `breakMultiMeasureRest`; mmrest `Measure` objects replace measure ranges and reparent measure elements), `SystemHeaderLayout` (brackets, instrument names, header widths), `AlignmentLayout`, `MaskLayout`, `StaveSharingLayout` (5.0 shared-stave feature: computes shared track maps and "shared items" while collecting systems).

## 11. Single-item rendering (`rendering\single\`)

- Used by **palettes, previews, drag ghosts, keyboard/clef editors, shadow notes** — callers include `palette\widgets\palettewidget.cpp:634`, `notationscene\utilities\engravingitempreviewpainter.cpp:80-107`, `notation\internal\notationinteraction.cpp` (via `IScoreRenderer`/`ISingleRenderer`).
- `SingleRenderer::layoutItem` → `SingleLayout::layoutItem` (`singlelayout.cpp:121`): builds a tiny **`SingleLayout::Context`** (`singlelayout.h:143`: just style + engraving font + dummy parent, "dontUseScore" is a temporary escape hatch) and switches on type to overloaded `layout(X*, ctx)`. `drawItem` → `SingleDraw::drawItem`.
- **Difference from score layout:** no `LayoutState`, no system/segment/measure context, no skyline/autoplace, no horizontal spacing — each item is laid out in isolation from its own properties and style (e.g. `layout(Clef)`, `layout(BarLine)`, `layoutGroupBracket`, `layoutLineSegment`). Many element types have *two* layout implementations (the contextual one in `TLayout`/helpers and the isolated one in `SingleLayout`) — a fix to one is not automatically applied to the other.

## 12. Edit-mode rendering (`rendering\editmode\`)

- **Paint only.** `EditModeRenderer::drawItem(item, painter, EditData, currentViewScaling, opt)` (`editmoderenderer.cpp:38`) switches on type: `BAR_LINE` → `drawBarline` (temporarily offsets `ldata->y1/y2` by the edit deltas, draws via `TDraw::drawItem` at `canvasPos`, restores them — `:155-172`), `DYNAMIC` → text edit or grips, slur/tie-like segments → `drawSlurTieSegment` (polyline through the 7 grip centers + grips), every `TextBase` descendant → `drawTextBase` (selection highlight per `TextBlock`, cursor rect, then a 2-px frame at `2.0/currentViewScaling`), default → `drawEngravingItem` (grip rects; current grip filled with `scoreGreyColor`).
- Called from `NotationInteraction::paint` (`notation\internal\notationinteraction.cpp:3681, 3756`) *after* the score paint (`NotationPainting::doPaint` → `NotationInteraction::paint`, only if `!isPrinting`). There is **no edit-mode layout**: moving a grip changes properties/offsets → `triggerLayout` → the normal range layout (§3).

## 13. The PAINT path

### 13.1 Score paint: `Paint::paintScore` (`paint.cpp:38-178`)

1. Early outs: null score / no pages. `DEVICE_DPI = opt.deviceDpi>0 ? … : mu::engraving::DPI`.
2. `pageSize = pageSizeInch(score)` (first page's bbox / `DPI`, or style page size if no pages — `:180-194`). `painter->setAntialiasing(true)`. If `opt.isSetViewport` (offscreen/PDF/print; **not** for on-screen): `setViewport(pageSize*DEVICE_DPI)` + `setWindow(pageSize*engraving::DPI)`.
3. Sets `MScore::pdfPrinting` and `score->setPrinting(opt.isPrinting)` (restored at end).
4. For `copy` × pages `[fromPage..toPage]`: page rect = `page->ldata()->bbox()` (or `tbbox` adjusted by `trimMarginPixelSize`); if `opt.frameRect` valid, **cull pages** not intersecting it; `onNewPage()` for non-first pages; `painter->beginObject("page_N")`; clip to page when printing; `translate(pagePos)` if `isMultiPage`; draw sheet via `onPaintPageSheet` callback or white fill; **`page->items(drawRect.translated(-pagePos))`** — a **BSP-tree spatial query** (`Page::items`, `dom\page.cpp:98`; tree rebuilt lazily by `doRebuildBspTree` `:173` from every item with `collectForDrawing()`, after layout calls `invalidateBspTree()`); `paintItems`; `DebugPaint::paintPageDebug` when not printing.
5. `paintItems` (`:234`): **sort with `elementLessThan`** (z-order, `engravingitem.cpp:1140`), skip `!isInteractionAvailable()`, `paintItem`.
6. `Paint::paintItem` (`:220`): skips `ldata()->isSkipDraw()`, clears `itemDiscovered`, `painter.translate(item->pagePos())`, **`TDraw::drawItem(item, &painter, opt)`**, translate back. So every `TDraw::draw(...)` draws in **item-local coordinates**.

### 13.2 `TDraw::drawItem` and `PaintOptions`

`tdraw.cpp:184` — `switch (item->type())` → `TDraw::draw(item_cast<const X*>(item), painter, opt)` (`default:` at `tdraw.cpp:436-438` logs `NOT_IMPLEMENTED` and hits `UNREACHABLE` — so a new type must be added here too). Each draw reads `item->ldata()` and `item->curColor(opt)`; examples: `draw(Note)` `:2341`, `draw(StaffLines)` `:2763` (`painter->drawLines(item->lines())` with a flat-cap `Pen`, honoring `setMask` for mask shapes), `draw(Stem)` `:2853` (`painter->drawLine(ldata->line)`; hides second chord of cross-measure pairs).

`PaintOptions` (`rendering\paintoptions.h:32`): `isPrinting`, `invertColors`, `ignoreDisplayedDefaultColor`, `overrideItemColor(item, defaultColor)` callback. **Color resolution** `EngravingItem::curColor(opt)` (`engravingitem.cpp:691-745`): printing → the item's literal `color()`; drop-target → highlight color; selected/marked → per-voice selection color (voice `VOICES` when voice-assignment is not "current voice only"); invisible → `invisibleColor`; default-colored items → `displayedDefaultColor(invertColors)` (the user's theme "black"), unless `ignoreDisplayedDefaultColor`; `overrideItemColor` applied to selection/default colors.

`ScorePaintOptions` (`iscorerenderer.h:110`) extends `PaintOptions` with `isSetViewport`, `isMultiPage`, `printPageBackground`, `frameRect`, `fromPage/toPage`, `copyCount`, `trimMarginPixelSize`, `deviceDpi`, `onPaintPageSheet`, `onNewPage`.

### 13.3 `muse::draw::Painter` abstraction (`muse\framework\draw\`)

- `Painter` (`painter.h:45`) is a thin façade over an **`IPaintProvider`** (`ipaintprovider.h:38`): pen/brush/font state, `save/restore`, transform (`worldTransform`, `translate/scale/rotate`, window/viewport), primitives (`drawPath`, `drawPolygon`, `drawLines`, `drawText`, `drawSymbol(point, char32_t)`, `drawPixmap`, `setClipRect`, `setMask`, `beginObject/endObject`). Constructors: from `IPaintProviderPtr`, or (Qt builds, not `NO_QT_SUPPORT`) from `QPaintDevice*` / `QPainter*` (wrap the Qt-backed provider).
- Providers: **`QPainterProvider`** (`internal\qpainterprovider.*`, real Qt rasterization — screen, QImage, printer, PDF/SVG devices), **`BufferedPaintProvider`** (`bufferedpaintprovider.h:32`, records calls into a **`DrawData`** tree instead of drawing — used by `engraving\devtools\drawdata\drawdatagenerator.cpp:131,178` and **vtest/visual-regression** comparison; `beginObject(name)` labels pages/items), plus `qimagepainterprovider`. `Painter::extended` (static) is a test provider hook ("not ready to use DI here yet", `painter.h:157`).
- `beginObject/endObject` and the `TRACE_DRAW_ITEM` macro (`painter.h:284`) record the item type name into draw data **only if `MUSE_MODULE_DRAW_TRACE` is defined**; otherwise no-ops.
- The draw module also exports `IFontProvider`, `IImageProvider`, `IFontsDatabase` (`drawmodule.cpp:48-63`) and the geometry/pen/brush/`PainterPath`/`Transform` value types in `draw\types\`. Its README is one line: "interfaces and types for drawing … and working with fonts, e.g. text metrics".
- ⭐ `Shape`/`Skyline` (`engraving\infrastructure`) depend on `muse::draw` geometry types (`RectF`, `Transform`) and expose debug `paint(Painter&)`; fonts metrics used during layout come from the draw module + the engraving font (`IEngravingFont`, SMuFL).

### 13.4 Other paint callers (not the main canvas)

`NotationPainting::doPaint` (screen/print/export), `mscsaver.cpp:276` (thumbnail), `svgwriter.cpp` (`paintItem` per element for SVG export with ids/links), `continuouspanel.cpp` (LINE-mode sticky header re-draws staff lines/clefs/keysigs/timesigs via `drawItem`), `notationinteraction.cpp` (shadow note, drag ghosts, lasso, edit mode), `engravingitempreviewpainter.cpp` / palette widgets (single renderer).

## 14. `LayoutData` (`ldata()`) — the bridge

`EngravingItem::LayoutData` (`dom\engravingitem.h:518`): `m_pos` (`ld_field<PointF>`, "reference position relative to parent, set by autoplace"), `m_shape`, `m_mask`, `m_mag`, `m_isSkipDraw`, snapped-item links (`m_itemSnappedBefore/After` — the "snapping chain" aligned by `AlignmentLayout`), and stave-sharing links (`m_sharedItem`, `m_originItems`). Each concrete class declares `struct LayoutData : Base::LayoutData` and the `DECLARE_CLASSOF/…` macro (`engravingitem.h:49-51`) gives it typed `ldata()`/`mutldata()` and `createLayoutData()`. `ldata()` **lazily creates** the object (`engravingitem.cpp:2863`). Fields are `ld_field<T>` wrappers (`infrastructure\ld_access.h`) that can assert on read-before-set; access modes `LD_ACCESS::CHECK / BAD / MAYBE_NOTINITED / PASS` (`ld_access.h:31`). `pos()` of an item = `ldata()->pos() + m_offset` (user offset, a *property*, not layout data, `engravingitem.h:691`). `isValid()` = shape present with valid bbox.

Debug: `DumpLayoutData::dump(score)` (commented-out call at `scorelayout.cpp:98`); `DebugPaint`/`PaintDebugger` paint bboxes/skylines when not printing.

## 15. Per-element layout ALGORITHMS — the `TLayout::layoutX` pattern

(Sections 15-24 were added in the algorithm pass. All line numbers are `src\engraving\rendering\score\` unless prefixed.)

**The pattern.** `TLayout::layoutItem` (`tlayout.cpp:196-520`) is one `switch(item->type())`. Each `case` either (a) calls a leaf `layoutX(const X*, X::LayoutData*, conf-or-ctx)` that depends only on the item itself, or (b) calls a `layoutX(X*, LayoutContext&)` that forwards to a helper class (`TLayout::layoutBeam` → `BeamLayout::layout` `:1289`; `layoutChord` → `ChordLayout::layout`; `layoutRest` → `RestLayout::layoutRest` `:4678`; `layoutTuplet` → `TupletLayout`; `layoutLyrics` → `LyricsLayout::layout` `:3857`; `layoutHarmony` → `HarmonyLayout` `:3310`). The leaf style reads `item` as `const` and writes the mutable `ldata` (`ctx.conf()` only for style/font lookups). Three conventions to recognize when reading any `layoutX`:

1. **Geometry goes through `ldata`**: `ldata->setPos/moveY`, `ldata->setBbox`, `ldata->setShape(Shape)`. The *shape* (not the bbox) is what spacing/autoplace/skylines consume, so most leaf functions end with a `fillXShape` (`fillNoteShape` `:4210`, `fillArticulationShape` `:1001`, `ChordLayout::fillShape` `chordlayout.cpp:3394`).
2. **Layout-data access contracts**: `LD_INDEPENDENT` (`ld_access.h:51`, empty macro) marks a function that needs no other item laid out first; `LD_CONDITION(val)` (`ld_access.h:41`, logs "BAD ACCESS" only in debug-LD builds, else `(void)val`) documents an *ordering precondition* (e.g. `layoutOrnament` requires `ldata->symId.has_value()`, `:4283`). They are documentation for the future "layout by passes" refactor, not enforcement in release.
3. **Two-stage dependent elements**: items that depend on page/system geometry are laid out in two calls — a first "tentative" call during measure layout and a final call after systems are known (articulations 1→2→3, §17; ties/slurs, §19; tuplets, §20; beams, §18).

Examples of the leaf style: `layoutNote` (`:4138`) sets the head group for dead/harmonic notes, caches `cachedNoteheadSym`, takes the bbox from `symBbox(noteHead)` (or, on TAB, builds `fretString` and a `tabHeadWidth` box), then `fillNoteShape` unions bbox + dots + accidental + laissez-vibrer + attached `el()` items (excluding fingerings not anchored to the note) + guitar-bend approximations. `layoutArticulation` (`:968`) creates the optional text for text-articulations and calls `fillArticulationShape`, which special-cases accents (3-rect cutout shape so slurs/dynamics can nest into the "<") — a nice example of why *shape ≠ bbox*. `layoutOrnament` (`:4280`) = `layoutArticulation` + ornament accidentals at `ornamentAccidentalMag = 0.6`, stacked above/below via `Shape::minVerticalDistance` + `vertMargin = 0.35sp`, then a composite shape; `layoutOrnamentCueNote` (`:4330`) lays out the cue-note chord with `layoutChords3/layoutLedgerLines/layoutAccidentals/layoutChord`, then places it at `HorizontalSpacing::minHorizontalDistance(parentChordShape, noteShape)` (max over the other voices' chordrests in the segment).

## 16. Chord layout (`chordlayout.cpp`, 3552 lines)

### 16.1 Single chord: `ChordLayout::layout` → `layoutPitched` (`:81`, `:100`)

`layout` rebuilds `graceIndex` (HACK comment, `:88-91`) and dispatches to `layoutTablature` (`:342`) or `layoutPitched`. `layoutPitched` (grace notes first, recursively):

1. For each note: `TLayout::layoutNote`; accumulate **`lll`** (space to the left of the chord origin) = max(−x of note, −x of its accidental − `accidentalDistance`), **`rrr`** (right) = max(note right edge), `lhead` (notehead-only left extent) (`:131-166`).
2. **Arpeggio handshake** (`:168-262`): a chord may *own* an arpeggio and also be *spanned* by another's. For each such arpeggio it computes the gap to the chord from `PaddingTable` (`arpeggio→NOTE/LEDGER_LINE/ACCIDENTAL` × `mag`), takes `maxChordPad = max(lll + width + gap)` and `minChordX`; only the *last* chord of the span (`endDownNote->track() == item->track()`) writes the arpeggio's final x offset. This is how an arpeggio ends up left of every accidental in its span.
3. Dots: `rrr = max(rrr, dotPosX + dotNoteDistance + (dots-1)*dotDotDistance + dotWidth)`; hook: removed if beamed, else laid out and `rrr` extended by `hook.right + stemflagpos.x`.
4. `setSpaceLw(lll)`/`setSpaceRw(rrr)` (these are the **left/right "spacing widths"** that segment shapes inherit), `layoutNote2` per note (dots x, attached symbols/fingerings), generic `el()` items (chordlines widen `spaceLw/Rw`), left-hand guitar fingerings aligned to the leftmost x, `layoutLvArticulation`, parentheses, then `fillShape`.

`layoutNote2` (`:3151`) positions the **dots**: `x = chord.dotPosX − note.x − chord.x`, first dot at `+dotNoteDistance×mag`, subsequent at `+dotDotDistance×mag`; if the chord is stem-up with a visible hook that the top dot would hit, the first-dot distance becomes the hook width (`:3197-3208`); invisible dots are packed separately at 0.1sp steps. (Remember: `dotPosX` is decided in `layoutChords3`/`setDotX`, §16.3.)

### 16.2 Multi-voice collision: `layoutChords1` (`:2222`) — the segment/staff-level routine

Called per (segment, staff) from `layoutMeasure` and again whenever a stem flips (`BeamLayout::layout1`, `checkCrossPosAndStemConsistency`). It looks at **all tracks of the whole part** (`part->trackRange()`, `:2236-2239` — so cross-staff notes of a piano staff pair collide correctly). Pipeline (`:2261-2294`):

```cpp
posInfo = calculateChordPosInfo(...)   // :1664 partition chords into up-stem / down-stem groups, max mag, max dots, hooks, grace-before
calculateMaxNoteWidths(posInfo,...)    // :1716 sort each group by line, layoutChords2(...) -> max NON-mirrored head width
offsetInfo = centreChords(...)         // :1806 center oversize/undersize heads vs the nominal head width
calculateChordOffsets(...)             // :1876 the up-vs-down voice conflict resolver
applyChordOffsets(...)                 // :1743 moveX per chord, then layoutChords3(posInfo.chords, notes, ...)
layoutLedgerLines; AccidentalsLayout::layoutAccidentals(...)   // grace chords get their own accidental pass
layoutSegmentElements(...)             // lay out the non-chord elements of the segment
```

**`layoutChords2`** (`:2306`) decides **notehead mirroring** (side-of-stem flips): walks the sorted notes in the *direction that gives the first-encountered head priority* (up-stem group bottom→top, down-stem top→bottom). A *conflict* = adjacent-or-same line (`|prevLine−line| < 2`), same staff, both visible, same track or `Chord::combineVoice`. `isLeft` toggles after a conflict or at the first note after one; `mirror = (chord->up() != isLeft)` unless the user `DirectionH` override; result stored in `note->ldata->mirror`; the stem is re-laid-out because mirroring moves `stemPosX` (`:2395-2398`). Returns the widest non-mirrored head.

**`calculateChordOffsets`** (`:1876`) only does work when *both* an up-stem and a down-stem group exist. `separation = topDownNote.line − bottomUpNote.line`:

- `separation == 1` (a second): down group shifts right by `maxUpWidth` (minus the stem width so stems align) — unless only the up group has dots, in which case the *up* group moves right by `maxDownWidth + 0.1sp`.
- `separation < 1` (overlap/unison): builds `overlapNotes` (up notes at/above `topDownNote−1` plus down notes at/below `bottomUpNote+1`), classifies unison/second conflicts, and decides **`shareHeads`** (`:1935-2051`): heads can be shared only when same line + same tpc + same head group, unmirrored, and (either same dots/both stemmed/same head type/full size, **or** the user forced a match by setting a non-AUTO head type or hiding one head). When shared it hides one set of dots (`setDotsHidden`) by voice parity/on-line rule (`:2063-2086`); otherwise `upOffset`/`downOffset` are chosen by a priority ladder (`:2101-2184`): upDots-only → move up group; unison → down group right by `maxUpWidth+0.15sp`; second-up-higher; down-stem hook vs ledger/flag overlap (`ledgerOverlapAbove/Below` computed from stem lengths vs first ledger line); second-down-higher; else "parts can overlap, just clear the stems/ledgers" (`clearLeft/clearRight`, tuck under a flag `useStraightNoteFlags ? pad 2 : 1`).
- Finally `dotAdjust` (`:2188-2213`): if only one side has dots they must be pushed past the other voice's heads.

**`centreChords`** (`:1806`): only on pitched staves; a head wider than nominal (`noteHeadWidth × staffMag`, threshold 0.01sp) is centered (`centerUp = −diff/2`), a narrower one centered in the other direction; records `oversizeUp` (chord/note `mag` > staff mag) because `stemPosX` assumes the nominal width.

### 16.3 Dots: `layoutChords3` (`:2675`), `placeDots` (`:2458`), `setDotX` (`:2607`)

`layoutChords3` is run over the *combined* note list ("superchord"). Pass 1 (`:2691-2744`) assigns each note a `DirectionV dotPosition` (user override else resolve: on a **line** with a note a second above → dot goes DOWN, second below → UP; unison → defer; on a space → unison tie-break by voice parity); AUTO falls back to `voice & 1 ? DOWN : UP`. `placeDots` then sets each dot's relative line (`setDotRelativeLine` `:2556`: `y = dotMove/2 × spatium × lineDistance`, also creates/removes `NoteDot` items to match `chord->dots()`) while avoiding two dots landing on the same space (`anchoredDots`, bottom-up then top-down lists from `getNoteListForDots` `:2840`; invisible notes ignored for visible ones; notes on spaces stay at the notehead's y except in-chord unisons which move ±2). Pass 2 (`:2750-2819`) sets each note's final `x` (`noteX`: mirrored → `stemPosX − overlapMirror` for up, `−headBodyWidth + overlapMirror` for down; non-mirrored up-stem → `noteHeadWidth − headBodyWidth`) and **`y = (line + stepOffset) × stepDistance`** (re-laying-out the stem/hook if y changed), and collects per-voice and per-direction dot X. `setDotX` (`:2607`) then aligns: if an up/down-stem pair conflicts *and* the voices combine, all dots go to `max(upDotPosX, downDotPosX)`; otherwise each chord keeps its own dot column.

### 16.4 Stems and ledger lines

- **`ChordLayout::layoutStem`** (`:1224`): hook type from direction, `defaultStemLength = StemLayout::calcDefaultStemLength` (computed *even when there is no stem*, because tremolos use "where the stem would be"), stem `posX = StemLayout::stemPosX`, `TLayout::layoutStem`, flag position, stem slash (only on the first chord of a beam). See §18.4 for the length rules.
- **Stem direction**: `ChordLayout::computeUp` (`:1525`) priority: TAB stemless/stems-beside → fixed; explicit `stemDirection` (when not in a real beam/two-chord tremolo); UI items up; **beam case** (`computeUpBeamCase` `:1266`: cross-staff → `isBelowCrossBeam`, user-modified → `isChordPosBelowBeam`, else `beam->up()`); two-chord tremolo case; **multiple voices → `track % 2 == 0` is up**; grace notes up; cross-staff chord (`staffMove`) → up iff `staffMove > 0`; else `computeAutoStemDirection(noteDistances)` (`:1615`): walk from both extremes inward summing the distances from the middle line and take the sign of the first non-zero pair sum (balanced chords look at the next pair inward; a perfectly symmetric chord → 0 → "not up").
- **`updateLedgerLines`** (`:1277`): returns early (deleting all) if `downLine + stepOffset ≤ lineBelow+1 && upLine + stepOffset ≥ −1`. Otherwise scans notes **twice from the outside in** (below-staff pass from the bottom note, above-staff pass from the top; each pass stops at the first note without ledger lines), keeps per-line `{minX, maxX, visible}`: line numbers round to even toward 0; one visible note makes *all* lines between it and the staff visible; `minX/maxX = note x ± extraLen×mag (ledgerLineLength)` and widen all inner lines (a ledger line spans the widest notehead on that side). Then creates `LedgerLine` items at `y = line × spatium × stepDistance`.

### 16.5 Accidentals (`accidentalslayout.cpp`) — column-stacking

Called from `layoutChords1` with all chords of the (segment, staff) (and separately per grace chord). `layoutAccidentals` (`:75`): collect (an accidental is **redundant** if another has the same type *and* line, `:130`; ones with `!addToSkyline` are just positioned left of the head), reset pos, `TLayout::layoutAccidental`, then `doAccidentalPlacement` (`:141`):

1. `createChordsShape`: ledger lines + note-head symbol shapes **with SMuFL cutouts** + stems of all chords → the obstacle `Shape`.
2. `findOctavesAndSeconds` per accidental; `splitIntoSubChords` (`:175`): break the line-sorted list into **sub-chords** wherever the next accidental is ≥ 6 lines (a seventh, `LINE_DIFF_OF_SEVENTH`) away; merge neighbouring tiny groups (`mergeAdjacentSubGroupsIfTooSmall`, unless the next starts on an offset/mirrored note); optionally merge groups that contain octave partners (`alignOctavesAcrossSubChords`).
3. Per sub-chord: `computeOrdering` → priority groups → `determineStackingOrder` (`:527`): small group → **standard order** (alternately pick from top then bottom, pulling in seconds/octaves — the classic "zig-zag"), large group or octave-aligned → **compact order** (fills each column greedily with whatever `canFitInSameColumn`), mid-size → compute both and **keep the one with fewer columns** (`:541-571`).
4. `stackAccidental` (`:291`): take the accidental's shape at its note's y; push it left until it clears the chord shape (`minAccidentalToChordDistance`, padding from the table, kerning-aware), then push it left until it clears the already-stacked accidentals' shape (`minAccidentalToAccidentalGroupDistance`, with special cases for fourths/naturals-sixths); record `column`.
5. `verticallyAlignAccidentals` (`:953`): aligns accidentals that sit in the same column *across* sub-chords/vertical sets so the columns look tidy.

### 16.6 Cross-staff

A chord's `staffMove()` ≠ 0 makes it a cross-staff chord: `vStaffIdx() = staffIdx + staffMove` is used everywhere collision logic asks "same staff" (`layoutChords2` conflict test `:2357`, `calculateChordPosInfo` `:1673`, `updateLedgerLines` uses `staffIdx + staffMove` `:1291`); its stem direction follows the move (`computeUp` `:1586-1590`: `up = staffMove > 0`, i.e. a chord moved to the staff below keeps its stem pointing up toward its home staff); beams/slurs/tuplets/articulations touching it are deferred to the page post-pass (`PageLayout::collectPage` tail, §8) because staff distances are not final until `layout2`. `layoutChords1` scans the whole part's tracks precisely so moved notes meet their new neighbours.

## 17. Articulations and ornaments — three passes

- **Pass 1 `ChordLayout::layoutArticulations`** (`:771`; during measure layout): decides direction per articulation (`anchor`≠AUTO wins; with multiple voices → stem side `item->up()`; else ornaments up for even tracks, **marcato-family always above** (Gould p.117), others on the notehead side `!item->up()`), then for "close to note" ones (`Articulation::layoutCloseToNote()`, `dom\articulation.cpp:263`: single staccato or tenuto, never `isDouble`, and not staccato on TAB) places them: head side → in the **next staff space** beyond the outermost note (`y = ((line & ~1) + 3) × lineDist/2 − height/2`, one step further if a tie needs room — `leaveSpaceForTie` `:3372`), stem side → past the stem/hook tip, snapped to a space, enforcing `propertyDistanceStem` and (for beamed/trem stems) a minimum even inside the staff. Stacked articulations of the same direction go `prev.height + articulationMinDistance` further out. `articulationStemHAlign` (STEM/NOTEHEAD/AVERAGE) picks x on the stem side; `articulationKeepTogether` aligns close articulations to the staff-anchored ones.
- **Pass 2 `layoutArticulations2`** (`:1009`; called from `layoutSystemElements` after systems exist): the staff-anchored ones (above/below the staff, i.e. not close-to-note) are stacked from `staffTopY/staffBotY = min/max(staff edge ∓ propertyDistance, chord extreme)`, with a 0.2sp "kerning" nudge for accent-over-staccato; then close ones are added to the staff skyline directly, others go through `Autoplace::autoplaceSegmentElement`.
- **Pass 3 `layoutArticulations3`** (`:1174`; after slurs): if a slur's shape now collides with an articulation (`verticalClearance < articulationMinDistance`), the articulation *and all later ones* are moved out by the clearance and the skyline/segment shape entries are shifted (`SystemLayout::updateSkylineForElement`). This is why articulations "jump" when a slur is added.

## 18. Beams (`beamlayout.cpp`, `beamtremololayout.cpp`, `stemlayout.cpp`)

### 18.1 Phases and the two layouts

```
layoutMeasure: BeamLayout::createBeams (:622)   -> builds Beam objects from Groups::actualBeamMode, calls layout1 per beam
               BeamLayout::layoutNonCrossBeams  -> "dummy" real layout() BEFORE spacing   (measurelayout.cpp:253-272, HACK comment)
layoutSystemElements: layoutNonCrossBeams again -> final layout() with real x positions  (systemlayout.cpp, see §7.1)
PageLayout::collectPage tail: cross-staff beams -> TLayout::layoutBeam + checkCrossPosAndStemConsistency (:903)
```

The call chain is `TLayout::layoutBeam` → `BeamLayout::layout` (`:59`; sorts elements by tick, splits chordrests **per system** into `BeamFragment`s with SpannerSegmentType BEGIN/MIDDLE/END/SINGLE, calls `layout2` per fragment, unions the segment shapes into `ldata->shape`, then recomputes `computeUp` for every element and re-lays-out one-chord tremolos). **Why twice** (code comment `measurelayout.cpp:255-259`): spacing needs stem extensions, extensions depend on beams, beams need final x — so a pre-spacing pass with provisional x only to extend stems. The anchor math guards the degenerate state (`endAnchor.x() > startAnchor.x()` check, `beamtremololayout.cpp:752-755`; zero-length guards `:110, :168`).

### 18.2 `createBeams` and `layout1` (stem direction of a beam)

`createBeams` (`:622`) walks each track's ChordRest segments: pre-pass computing `beatSubdivision` for X/4 meters; `Groups::actualBeamMode(cr, prev, &beatSubdivision)` yields BEGIN/MID/END/NONE; a CR "can be beamed" if duration < quarter **or it is a rest**; handles cross-measure beams (continues `prevBeam` unless a line/page/section break, `beamNoContinue`, or prev is quarter-or-longer; removes the previous hook), grace-note beams (`beamGraceNotes`), and deletes beams with ≤1 element or only rests. `layout1` (`:137`) computes **only the direction** (`up`), cross-staff move range (`minMove/maxMove`, `cross = min≠max`, `fullCross`), `notePositions` (sorted `(line, staff)` of every note) and mag; direction priority: forced cross (`computeUpForMovedCross`) → user `direction` → `maxMove > 0` (a chord moved to the staff below) → stems down → `minMove < 0` → stems up → grace (voice parity) → multiple voices (`track%2==0`) → a chord with a custom stem direction → **`computeAutoStemDirection` over the beam's note lines** (the same "extreme-pair" rule as a chord, now over the whole beam). If any chord flips, `layoutChords1` + `createShape` are re-run for it (`:289-299`).

### 18.3 Slant and anchor math (`BeamTremoloLayout::calculateAnchors`, `beamtremololayout.cpp:602`)

Shared by beams and two-chord tremolos (`BeamBase`). All vertical math is in **quarter-spaces (integers)** to avoid rounding drift. The anchor is the **middle of the outermost beam stroke**, not the stem tip. Steps (`:697-793`):

1. Start from each end chord's default stem tip (`chordBeamAnchorY` `:1355` = far-note y ± `defaultStemLength` ∓ half beam width). The end whose note is more extreme (highest note for stem-up beams, lowest for stem-down; `isStartDictator`, `:716`) is the **dictator** — its position anchors the beam — and the other end is the **pointer**, set to `dictator ± slant`.
2. **`getTargetStaffLine`** (`:1072`): the staff position beams must at least reach (`StemLayout::minStaffOverlap`, ~middle line for full size; 2nd line from bottom for small/grace; shifted for cross-staff above/below beams; ±1 because the anchor is mid-stroke).
3. **`computeDesiredSlant`** (`:1105`): 0 if both ends already extend to the target line, if start/end notes are equal, if `SlopeConstraint::FLAT`, or `Beam::noSlope`. `getSlopeConstraint` (`:1168`) forces FLAT when any inner note is more extreme than the higher/lower end (or two notes tie with the end and aren't neighbours) and returns `SMALL_SLOPE` (¼sp) for the "single neighbour equals the extreme" exception. Otherwise `slant = min(maxSlope(width), _maxSlopes[min(interval,7)])` with `_maxSlopes = {0..7}` (`:49`): the slant in quarter-spaces equals the **note interval, capped by `getMaxSlope`** which tiers by beam width in sp: `<3 →1, <5 →2, <7.5 →3, <10 →4, <15 →5, <20 →6, else 7` (`:1263-1291`).
4. `pointer = dictator ± slant` (sign by direction).
5. Adjust: `setSmallInnerBeamPos` (flat small beams with outside notes: keep innermost stroke ≥½sp inside the staff), `offsetBeamWithAnchorShortening` (full-size only: shorten the dictator's stem toward the target line down to the **minimum stem length per stroke count** `{11,13,15,18,21,24,27,30,33}` quarter-spaces, else extend; each candidate must satisfy `isValidBeamPosition`), `offsetBeamToRemoveCollisions` (`:103`: for every *inner* chord, raise the line (dictator and/or pointer, preferring the one that keeps slope) until the beam clears that chord's stem end with the per-slope **reduction** 0.25/0.5/0.75sp and the inner stem keeps `minStemLength(cr) × mag`, with a 1-step exception when the inner note shares the line with an end note).
6. `setValidBeamPositions` (`:457`) — the **staff-line aesthetics**: a beam stroke must not *float* in a staff space (`yPos%4==2` is "floater" → invalid inside the staff); only "sitting" on a line (`%4==3`) or "hanging" (`%4==1`) in the right slant direction, or exactly on a line, or flat; also checks the *outermost* stroke of multi-stroke beams (`getOuterBeamPosOffset`, `findValidBeamOffset`, 64th-note exception `is64thBeamPositionException`). `addMiddleLineSlant` (`:524`): a beam that ends exactly on the target line gets nudged 1–2 steps to avoid a flat-looking near-slope; `add8thSpaceSlant` (`:541`): 3-stroke beams get a 0.125sp offset and `beamDist += 0.0625sp`.
7. Convert back: `y = quarterSpace × position + pagePos.y`; x from `chordBeamAnchorX` (`:1304`: stem x ∓ half/whole stem width depending on direction and Start/Middle/End).

**Cross-staff "between" beams** (`calculateAnchorsCross` `:795`): flat beam at the midpoint of the highest-bottom/lowest-top stem tips (shifted by the average of limiting-segment distances), slope only if non-conflicting: slants of the two groups and "beam-side"/"staff" switches must agree; two-note one-per-staff case uses `min(|Δy|/sp, maxSlope) × ¼sp`.

### 18.4 Stem length (`StemLayout::calcDefaultStemLength`, `stemlayout.cpp:42`)

All in quarter-spaces: `default = stemLength(style)×4 + stemLengthBeamAddition` (3 strokes +2, ≥4 strokes +(n−3)×(wide?4:3), hooks 0; ×1.5 on TAB). Then if the stem end falls **outside the staff by the "short-stem" threshold** (`shortStemStartLocation`) it shortens by `maxReduction` (table by strokes × extension outside staff, `:352-358`; 0 unless `shortenStem`); else if inside, an *optical adjustment* −1 when the end sits on an even (line-ish) position for ≤2 strokes (`stemOpticalAdjustment` `:399`); clamp to `shortestStem` (12 with wide beams). Final = `max(ideal, calcMinStemLength)` where the minimum accounts for one-chord tremolo height + paddings + hook geometry and, for beamed/two-chord-trem chords, inner-stem minimums `{10,9,8,7}` + stroke heights (`:415-491`); 4-beam exception table `calc4BeamsException` (`:493`). Finally, for stems that would end short of the middle line (small mag / non-standard line distance) an `extraLength` extends them to the target line (`:123-158`).

### 18.5 Beam segments, beamlets, stem extension

`createBeamSegments` (`:945`) loops **level by level** (0 = primary). For each level it walks the chordrests, calls `Beam::calcBeamBreaks` (16th/32nd sub-beam breaks), and starts/ends runs; rests may be skipped *through* when both neighbouring chords have higher beam counts (`skipRest`). A run of ≥2 chords → `createBeamSegment` (`:1163`); a lone chord → `createBeamletSegment` (`:1314`) with direction from `calcIsBeamletBefore` (`:1057`: first/last of beam or tuplet, next-note break, neighbour with more beams, beat-subdivision end with `BEAM_TUPLET_TOLERANCE = 6` ticks). A segment's line is the anchor line (`slope × (x − startAnchor.x) + startAnchor.y`) shifted by `beamDist × (level − extraBeamAdjust)` — with **feathered beams** interpolating `growLeft→growRight`. Level 0 calls `extendStem` (`:302` of beamtremololayout: `chord->setBeamExtension(...)` + re-layout stem/slash and replace the stem's shape element) and sets the stem's `beamCorrection = ±beamWidth/2` ("HACK … bbox of the stem covers also the beam position", `:1278-1280`); levels > 0 extend stems further by `computeStemLengthAdjustmentSteps` (French-style beams shorten visually but keep the full bbox length for collisions). `verticalAdjustBeamedRests` (`:844`) pushes rests inside a beam group off the beam by `0.35sp` (`0.2sp` for ≤32nd), in line-distance steps, then re-resolves rest-vs-chord/rest conflicts.

## 19. Slurs and ties (`slurtielayout.cpp`, 3288 lines)

### 19.1 Slur segment per system: `layoutSystem(Slur*, System*)` (`:61`)

Called by `processLines` (§7.1) once per system the slur touches. It (1) gets/creates the `SlurSegment` for that system; (2) on the first system computes **direction `computeUp`** (`:2607`: user direction; else `up = !startCR->up()` (opposite the stem); cross-beam start → up; end chord in another system → temporarily `computeUp` it (HACK, `:2632-2639`); **multiple voices → voice>0 slurs go down else up** (slurs live on the stem side); mixed stem directions between start and end → up (grace notes exempt)); (3) decides segment type SINGLE/BEGIN/MIDDLE/END from system tick bounds; (4) `slurPos` (`:394`) gives the default end points; (5) tie-aware tweaks — a slur starting/ending on a tied note keeps `tieClearance = 0.65sp` from the tie's endpoint (moves the slur endpoint vertically) or, if the tie ends there, shifts `horizontalTieClearance = 0.35sp`; (6) system-boundary end points: **start of a continued segment** anchors at `system->firstNoteRestSegmentX` with y from the first chord's shape top/bottom (minus accidentals) ± 0.4sp unless above its own stem; **end of a system** at `endingXForOpenEndedSlurTie` with y following the last chord but clamped to 2.5sp of the start y; (7) optionally `adjustSlurFloatingEndPointAngles` for hanging ends (`Sid::angleHangingSlursAwayFromStaff`); (8) `layoutSegment` (`:3204`) → `computeBezier`.

`slurPos` end-point rules (`:394-870`): attach to the **notehead** by default (x = chord pos + note x, y at the outer note); attach to the **stem tip** (`SlurAnchor::STEM`) when the slur and the chord's stem go the same direction (and the chord isn't a beam-interior/tremolo case, `stemSideStartForBeam/EndForBeam`): point = stem end + offset `(max(0.35·mag, 0.2)sp` horizontally toward the slur, `0.5sp` vertically clear of the stem); special-cased around flags (traditional hooks use `fakeCutout` slope `1.0`, bulky fonts `1.5`; straight flags push to `beamClearance 0.35sp`), around the **end beam** (`yadj = 0.75` if the end chord is not the first in its beam / second of a tremolo), and `fixArticulations` (`:891`).

### 19.2 The shape: `computeBezier(SlurSegment*)` (`:2767`)

1. `adjustEndPoints` (`:927`): nudge each end point out of the "bad zone" within `staffLineMargin = 0.175 + ½ staffLineWidth` of a staff line (direction-dependent).
2. Work in **slur coordinates**: translate to start point, rotate by `slurAngle = atan(Δy/Δx)` so the chord is horizontal; `d = length/spatium`.
3. Default shoulders: `shoulderW = 0.60 (d<2), 0.50 (<10), 0.60 (<18), else 0.70`; `shoulderH = √(d/4) × spatium` (`computeShoulderHeight` `:2912`; ×0.75 when the slur goes over beams, `isOverBeams`), sign by direction. Bezier control points `p3 = (c1, −H)`, `p4 = (c2, −H)` with `c1 = (len − len×W)/2`, `c2 = c1 + len×W` (plus any user shoulder offset). `d < 0` (end before start) is the pre-spacing temporary state → return (`:2816-2821`).
4. If `autoplace` (and not a tapping half-slur): **`avoidCollisions`** (§19.3).
5. Re-run `adjustEndPoints`, store the delta in `endPointOff1/2` (so a user-edited endpoint can be re-applied), recompute handles (`DRAG` = mid chord, `SHOULDER` = mid p3/p4), `computeMidThickness` (`:3225`: thickness tapers on short slurs/ties between `minTieLength` and `4sp`), build the painter path (outer + inner cubic for Solid style), `fillShape` (`:3260`: 20–50 rects sampled with `sin²` spacing along the curve).

### 19.3 `avoidCollisions` (`:972`) — iterative relaxation

Input: the 4 bezier points in slur coordinates. Algorithm:

1. **Obstacle shape**: `getSegmentShapes` (`:1163`) = union of `getSegmentShape` for every enabled segment between start and end (system bounds for continued segments), skipping barlines/breaths/time-tick/big timesigs; each adds the staff shape (plus the second staff's, y-offset, for cross-staff slurs), tie/two-chord-trem shapes; then **filters out** what a slur must not avoid (`:1266-1325`: its own start chord's items, lyrics/text, ledger lines, articulations on the end chord, bends, other voices at the end ticks, "inside" voices, fermatas, fret diagrams, big time sigs…). `addMinClearanceToShapes` inflates the rest by 0.4sp (notes), 0.2sp (articulations/ornaments/tapping), 0.1sp (else), toward the slur side.
2. **Clearance arc**: `arcClearance = −upSign × clamp(0.04 × lengthInSp × cos²(angle), 0.1, 0.5) × spatium` (`computeArcClearance` `:1361`) — the curve it tests is the real bezier displaced by this.
3. `leftBalance/rightBalance` (`computeAdjustmentBalance` `:1373`): per end, how a correction is split between **moving the end point (balance→1) and bending the curve (balance→0)** (code comment `:998-1000`). Left end: `0.1` (mostly bend) if the segment is a BEGIN piece, has a slur-side articulation on the start chord, or is cross-staff; `0.4` for a normal single-system slur; `0.9` (mostly move the end point) for an *open* end — a MIDDLE/END segment's left end or a stem-floated end. Right end mirrors it (`END` piece/artic/cross → 0.1; normal → 0.4; BEGIN/MIDDLE open end or stem-floated → 0.9).
4. `step = upSign × 0.30sp` (scaled up to ≤1.5sp for slurs > 16sp, `computeAdjustmentStep` `:1426`).
5. **Loop (≤ `MAX_ITER = 30`)**: sample the clearance bezier into 19 rects; classify collisions into **left / mid / right thirds**; **even iterations bend the shape** (left: move `p3` up/out by `(1−leftBalance)×step/2` and `p4` half as much; mid: both up; right: mirror), **odd iterations move end points** (unless `isEndPointsEdited`): tilt around the opposite end (`pp1`/`p2`), with slant-reduction scaling for long, steep slurs on mid-only collisions; slurs steeper than 45° are gently compensated. After every iteration enforce **"non-ugliness"**: control points not higher than the slur is long (`maxRelativeHeight = |p2.x|`) and not outside `[pp1.x, p2.x]` (tangent rule). Stops when no section collides or the iteration cap hits.

`adjustOverlappingSlurs` (`:1838`, run after all slurs of the system exist): same-vertical-point conflicts — a slur ending where the next starts at the same height: pull ends 0.2sp apart; two slurs sharing a start/end x: push the *outer* one's endpoint 0.65sp outward and recompute; ties count as the inner one.

### 19.4 Ties

`layoutTieFor`/`layoutTieBack` (`:1566`/`:1639`): `calculateDirection` (`:3054`: user; else if the chord has voices → stem direction; single note → opposite the stem (up if the two stems differ); chord → find the **pivot** (a second/unison pair of tied notes: lower of them goes down) else compare ties above vs below the note: more above → down, equal → opposite to the stem), `calculateIsInside`, default end points from the note optical center (`computeDefaultStartOrEndPoint` `:1722`), cross-staff and `forceHorizontal` corrections, `adjustX` (collisions with accidentals/ledger/neighbours), `adjustYforLedgerLines`, then **`adjustY`** (`:2394`) = `computeBezier(Tie)` (`:2679`: `shoulderH = minShoulder + 0.3sp×√|len−1|` clamped to `[min,max]`, `shoulderW = 0.6`) + two fixes: (1) end points within `0.15sp` of a staff line are moved to `line ± (0.15sp + half line thickness)`; (2) the **arc apex** must not sit on a staff line (`badArcIntersectionLimit` 0.1/0.15sp): choose to bulge outwards or inwards (`increaseArc`), with small ties first trying a vertical shift, capped at 0.75 × tie height, remainder shifting the whole tie. Stacked ties in a chord are separated by `resolveVerticalTieCollisions` (`:2528`). Laissez-vibrer and partial ties have their own creators/`calculateLaissezVib{X,Y}`/`setPartialTieEndPos`.

## 20. Tuplets (`tupletlayout.cpp`)

`TupletLayout::layout` (`:41`): `createNumber` (`:151`: text "n" or "n:d" or SMuFL tuplet digit symbols, `isSmall` if all contents are small), `computeDirection` (`:212`: user; else vote — each chord ±1, explicit stem direction ±1000; all rests → up unless voices present → track parity), `computeStartEndCR` (descends into nested tuplets to the first/last real ChordRest), `calcHasBracket`, `layoutBracket`, bbox, then `Autoplace::autoplaceMeasureElement` unless cross-staff. Nested tuplets are laid out **inside-out** (`layoutTupletAndNestedTuplets` `:94`, reverse order). **`layoutBracket`** (`:269`): endpoints from the first/last chord's stem (same-direction stem → stem edge ± `tupletStem{Left,Right}Distance`, else notehead edge ± `tupletNote…Distance`; vertical `tupletVStemDistance`/`tupletVHeadDistance`); a rest endpoint copies the other end's y; `tupletOutOfStaff` clamps to the staff edge (only when both ends are on the same staff); **slope capped** at `tupletMaxSlope` (move whichever end keeps the bracket above the notes); then a **collision sweep over inner stems/notes** shifts the whole bracket by the worst overlap; the number is centred on the bracket, on the beam if beamed without a bracket, or on the **rhythmic center** (`tupletNumberRythmicCenter`, only for asymmetric tuplets: `centerTick` `:732`, `computeRhythmicCenter` `:680` interpolates inside the reference segment's width); bracket polyline `bracketL/R` has a gap `numberGap = 0.35sp` either side of the number. `tupletExtendToEndOfDuration` (`:745`) extends the right end to the end of the last element's duration but never closer than `0.6sp` to the next segment's/next same-direction tuplet's edge (uses `Shape::leftMostEdgeAtHeight`).

## 21. Text, lyrics, harmony

- **`TextLayout::layoutBaseTextBase1`** (`textlayout.cpp:54`): for every `TextBlock` → `layoutTextBlock` (`:313`: per-fragment x advance by `FontMetrics::width`, sub/superscript y offsets from `xHeight`, music-font fragments get `lineSpacing = 1.25 × glyph height` instead of the font's huge line spacing, Dynamic text uses SMuFL symbol shapes) accumulate `y += lineSpacing`; `textHorizontalLayout` (`:187`) aligns lines relative to the longest (`position` relative to the reference width; `JUSTIFY` redistributes spaces except on the last line, `justifyLine` `:264`); vertical alignment by `AlignV` vs parent (box margins/page margins for `layoutToParentWidth`); `layoutFrame`; a **high-resolution per-character shape** (`computeTextHighResShape` `:155`, tight char rects — used by lyrics skylines) for everything except dynamics/box text; rotation by `textAngle`; finally `move(defaultPos)`. `layoutBaseTextBase` first resets pos and puts "below" placements at `staffHeight`.
- **Lyrics** (`lyricslayout.cpp`): `layout(Lyrics*)` (`:44`) splits leading verse-number/punctuation and trailing punctuation (`Sid::lyricsAlignVerseNumber`) so only the *letters* are centered under the notehead (`x = −cr.x − centerAdjust/2` for centered, `−leftAdjust` for left aligned; melismas/numbered verses use the melisma alignment style), preserves the Y set by `computeVerticalPositions`, sets `melismaEnd` on the end chordrest. Lines: `layoutMelismaLine` (`:231`: from `lyricsLineStartX` to `lyricsLineEndX`, minimum `lyricsMelismaMinLength`, forced if `lyricsMelismaForce`), `layoutDashes` (`:264`: `dashCount = floor(length / lyricsDashMaxDistance)` (ceil when the first/last gap is a half gap), min 1 if long enough or forced, optional cap; short ones are widened symmetrically (or leftward on a first syllable); dash width ≤ `lyricsDashMaxLength`; "left aligned at system start" style option). **Vertical placement is per system** (`computeVerticalPositions` `:524`, called from `layoutSystemElements`): collect verses above/below per staff → `setDefaultPositions` (`:585`: above = `−(maxVerseAbove − verse) × lineSpacing × lyricsLineHeight`, below = `staffHeight + verse × …`, plus `defaultPos`) → `checkCollisionsWithStaffElements` (`:624`: build a skyline per verse, ensure ≥ `lyricsMinTopDistance` from the staff skyline, moving **this verse and all outer ones**, `moveThisVerseAndOuterOnes` `:687`) → `addToSkyline` (`:712`; padding = `lyricsMinBottomDistance − minVerticalDistance` — flagged HACK in the source).
- **Harmony** (`harmonylayout.cpp`): `layoutHarmony` (`:40`) = `render` (build the `HarmonyRenderItem` list from the `ChordList` render-action program: `renderAction` `:703` interprets SET/MOVE/MOVEXHEIGHT/PUSH/POP/NOTE/ACCIDENTAL/STOPHALIGN/SCALE/PAREN, with `kernCharacters` for glyph kerning; polychords render bottom chord first and stack with a divider line at `polychordDividerSpacing/Thickness`, then are aligned to the longest line; roman numerals have their own path) → `calculateBoundingRect` (`:80`: horizontal alignment relative to `noteheadBlack` width — or the fret diagram's width if attached and visible, per `chordAlignmentToFretboard`; vertical: top of the render bbox, placing below at `staffHeight`, or `harmonyFretDist + nutLineWidth` above a fret diagram) → frame → `layoutParentheses`. Falls back to ordinary text layout when the render list is empty (free-text chord symbols).

## 22. Header/footer layout (`headerfooterlayout.cpp`)

`layoutHeaderFooter` (`:37`; PAGE/FLOAT only) is run per page from `collectPage`. For each of 3 areas (L/C/R) it picks the `odd*`/`even*` style string by display page number (`headerOddEven`/`footerOddEven`), creates or reuses a generated, non-movable `Text` (`layoutToParentWidth = true`, align `{LEFT|HCENTER|RIGHT, TOP|BOTTOM}`), and `updateHeaderFooterText` (`:145`) does a **two-pass formatting**: pass 1 sets the style-formatted XML (escaping bare `&` and `<` followed by space/digit — the string is user XML-ish), pass 2 expands the `$` macros **per `TextBlock`** (`replaceTextMacros` `:244`: `$p` page number except first page, `$P` all pages, `$N` only when >1 page, `$n` page count, `$i/$I` part name, `$f/$F` file name/path, `$d/$D/$m/$M` dates/times, `$c/$C` copyright, `$v/$r` version/revision, `$$`, `$:tag:` metadata; page-number and copyright macros apply their own `CharFormat` fragments via `formatForMacro`), then lays the text out. If the only block is empty the text is removed (so empty lines survive only when something else is present). **`$n`/`$N`/`$p` set `mustRecomputeHeadersFooters`** (page count unknown until the end — §3.5); `scoreHasTimestampHeadersFooters` (`:499`) detects `$d/$D/$m/$M` to refresh on save/time. `headerExtension/footerExtension` (`:444/:468`) = `max(0, tallest text − style offset.y×DPMM)` — space `collectPage` reserves above/below the music. First-page rules: `mustShowHeadersForPage` (`:528`) = `showHeader && (displayPage ≠ 1 || headerFirstPage)`.

## 23. Staff sharing (`stavesharinglayout.cpp`, 2062 lines)

**What it is**: a *shared part* (`SharedPart`, a `Part` subclass) presents several origin parts' music on fewer staves ("a due"/section staves). Layout **generates real notation items in the shared staves** (clones of origin notes/rests/articulations/ties/spanners/clefs/key/time sigs/annotations) and links them (`EngravingItem::connectSharedItem`, `originItems()`, `sharedItem()`), so the rest of layout treats the shared staff like any other. Everything is keyed by a **`SharedTrackMap`: origin track → shared track**, stored per tick as `StaveSharingState`s on the `SharedPart` (user-modified states can override).

**When it runs** (`systemlayout.cpp`): `updateStaveSharingForFullSystem` for system locks (`:155`); `updateStaveSharingForLastAddedMeasure` after every measure appended to a system (`:171`) — if the track map changed, `relayoutMeasuresOnTrackMapChanged` (`:81`: header width + `needLayout` + `layoutMeasure` for each measure); after the *overflow rollback* removes a measure (`:245-251`) the full-system version re-runs; `updateNotationWithoutRecomputingTrackMap` (`:3234`) after temporarily restoring user states. Context `StaveSharingContext` (`:2029`) snapshots all/CR segments of the range, `[sTick,eTick]`, overlapping spanners; `computeSegmentsToUpdate` (`:936`) restricts to the last measure when only one was added and the map didn't change.

**Algorithm** (`updateStaveSharing` `:116`, per visible shared part): `updateTrackMaps` → `updateNotation` (disconnect → `makeSharedNotation` → `cleanup`).

1. **`updateTrackMaps`** (`:179`): keep the old per-tick states; drop non-user states in the range; ensure a computed state at the system start unless a still-valid user map precedes it; for each state: a **user map** is copied if **`isValid`** (`:128`: origin tracks mapped to one shared track must `canGoToSameVoice` pairwise; consecutive voices on one shared stave must `canGoToSameStave`; a shared stave with voices 2-4 must have voice 1), else (and for resets/computed) **`computeTrackMap`**; sets `trackMapChanged` if any state's map differs from before.
2. **`computeTrackMap`** (`:264`): take each origin part's first track; (a) group **unisons** (`isUnison` `:397`: same instrument/transposition at every tick, same rhythm/duration/chord-ness, notes `isExactUnison` + compatible spanners/articulations/grace notes, no clash in clef/key/time/annotations/spanners); (b) group tracks that **can share one voice** (`canGoToSameVoice` `:489`: same instrument and rhythm, notes of the later track never *above* the earlier (`pitch` ordering), equal notes must be exact unisons — and the later track unison with *every* member of the current group; a tracked `localUnisonNotes` set stops a note being a unison with two different voices); (c) assign groups to shared tracks in order: stay on the current shared stave while voice 0 or `canGoToSameStave` (`:889`: same instrument + clef/key/time compatible, and — unless `allowVoiceCrossing` — the lower track's top note must not exceed the upper's bottom note), else jump to the next stave's voice 0; voice 0 → voice 1 increments, otherwise skips to the next stave.
3. **`makeSharedNotation`** (`:1067`): `makeSharedClefKeyTimeSigs` → `makeSharedChordRests` (`:1170`: reuse the shared CR if the type/duration match else delete+clone; replicate **nested tuplets**; `makeSharedNotes` finds an existing exact-unison note or clones, connects, `ctx.sharedUnisonNotes` collects notes with >1 origin; mirrors ties/note-spanners/articulations; grace chords likewise) → `makeSharedBreaths` → `makeSharedAnnotations` → `makeSharedSpanners` → **`makeStaveSharingLabels`** (`:1575`: auto "a 2"/instrument text on unison notes when the origin set changes or must restate on a new system, `unisonLabelRestateOnNewSystem`) → `makeStaveSharingChanges`.
4. **`cleanup`** (`:1834`): remove any shared-staff item *without* `originItems()` (stale clones — annotations, clef/key/time, tuplets, rests (except voice-0 full-measure rests), articulations, ties/spanners, notes, then empty chords), `fillRestsIfNeed` (`:1967`: voice 1 of each shared staff must be rest-filled; handles stale full-measure rests), drop labels/changes not re-produced.

⚠ **Correction to the first-pass note**: `findNearestEnabled` is **not** in `StaveSharingLayout` (and not in `src\engraving` at all). It is `static T* findNearestEnabled(...)` in `muse\framework\ui\internal\navigationcontroller.cpp:65` — keyboard-navigation lookup for the UI (see `ui-qml-mvvm.md`). Nothing in stave-sharing track-map logic uses it; the track-map "nearest"-style logic is `computeTrackMap`/`canGoToSame{Voice,Stave}` above. **Observation to verify (not changed)**: `makeSharedChordRests::makeSharedNotes` (`:1248-1274`) shares only `originChord->upNote()` per origin chord — consistent with `canGoToSameVoice` requiring equal note counts, but a multi-note origin chord (chord/divisi in an origin part) would appear to mirror one note only (HYPOTHESIS — not traced through `Chord::notes()` pairing or whether shared parts forbid multi-note origins).

## 24. Where each algorithm sits in the pipeline (cheat sheet)

| Element | First laid out | Final | Notes |
|---|---|---|---|
| Notes/accidentals/dots/ledgers/stem dir | `layoutMeasure` → `layoutChords1` (§16) | re-run when a stem flips | per (segment, staff) over the whole part |
| Stems/flags | `layoutChordsStem` / `ChordLayout::layoutStem` | beam pass `extendStem` | `defaultStemLength` always computed |
| Beams | dummy `layoutNonCrossBeams` in `layoutMeasure` | `layoutSystemElements`; cross-staff in `collectPage` tail | §18.1 |
| Articulations | pass 1 (measure) | pass 2 (system), pass 3 (after slurs) | §17 |
| Ties | `layoutTiesAndBends` in `layoutSystemElements` | — | §19.4 |
| Slurs | `processLines` in `layoutSystemElements` | cross-staff ones in `layoutCrossStaffElements` | §19.1-19.3 |
| Tuplets | `layoutSystemElements` (after articulations) | cross-staff/nested in `collectPage` tail | §20 |
| Lyrics | per-lyric in `layoutMeasure`; verticals in `layoutSystemElements` | — | §21 |
| Harmony | `layoutMeasure` annotations (positions) / system pass | — | §21 |
| Header/footer | `collectPage` | recomputed at end if page-count macros | §22 |
| Shared staves | `collectSystem` after each measure append | re-run after rollback | §23 |

## Cross-subsystem dependencies

- **`engraving-dom.md`** — the elements, `Score/Measure/Segment/System/Page/SysStaff`, `Pid`; layout code reads DOM and writes `ldata`, but also **mutates the DOM** (ModifyDom, header/trailer segments, courtesy sigs, mmrest measures, stems, beams, spanner segments, shared-stave items).
- **`style-system.md`** — `LayoutConfiguration::styleX(Sid::…)` everywhere; `Sid`s referenced here: `pagePrintableWidth`, `measureSpacing`, `spacingDensity`, `lastSystemFillLimit`, `staffDistance`, `akkoladeDistance`, `minVerticalDistance`, `minStaffSpread`, `staffUpperBorder/LowerBorder`, `skylineMinHorizontalClearance`, `lyricsStackingOrder`, `createMultiMeasureRests`, `timeSigPlacement`, `systemHeader*Distance`, etc. `Score::updatePaddingTables` rebuilds the `PaddingTable` from style (`score.cpp:5043`).
- **`notation-layer.md`** — `INotation`/`NotationPainting`/`NotationInteraction` call `paintScore`/`drawItem`; commands run inside transactions that end in `MasterScore::update` (layout trigger).
- **`ui-qml-mvvm.md`** — the notation canvas (`notationscene`, `NotationPaintView`, `paintedengravingitem`) supplies the `Painter` and `frameRect`.
- **`testing-and-vtest.md`** — `BufferedPaintProvider`/`DrawData` + `drawdatagenerator` give the vtest compare path.
- **`file-format-and-rw.md`** — scores are fully laid out via `Score::doLayout()` after load/creation (one call site `score.cpp:2813-2814`: `setLayoutMode(PAGE); doLayout();` — surrounding function not read, HYPOTHESIS that it is a load/convert path), and on save thumbnails are painted (`mscsaver.cpp:276`).
- **`playback-and-audio.md`** — layout flag `PLAY_EVENTS`/`REBUILD_MIDI_MAPPING`; tick/measure data computed during layout (`computeTicks`, mmrest handling) feeds playback.

## Non-obvious gotchas

- ⛔ **Never call layout from inside layout / edit code outside `update()`.** `CmdStateLocker` locks `CmdState` during `layoutRange` precisely so `triggerLayout()` calls made *by layout code* are ignored. Dirty-marking outside a transaction/`update` cycle won't flush.
- ⭐ **Layout is asymmetric**: the item that "owns" the dirty range is determined by `triggerLayout` overrides; a too-narrow trigger (single item `tick()` only) can leave neighbors stale (beams across measures, spanners, shared items). Use the overriding `Measure::triggerLayout()` / spanner overrides rather than `setLayout` by hand. `triggerLayoutAll()` is deliberately flagged "extremely expensive".
- ⭐ **Adding an `ElementType`** requires touching: `TLayout::layoutItem` (default asserts!), `TDraw::drawItem`, and for palettable items `SingleLayout::layoutItem` + `SingleDraw`; possibly `EditModeRenderer`, `PassLayoutIndependentItems::scan`, and `IScoreRenderer::Supported`.
- ⭐ **Beams are laid out twice**: a *dummy* pass in `layoutMeasure` before spacing (to extend stems), and the real pass in `layoutSystemElements` (and cross-staff beams in the page post-pass). Don't "optimize away" the first.
- ⭐ **Spacing is probed incrementally then recomputed**: `updateSpacingForLastAddedMeasure` during collection, then `computeSpacingForFullSystem` after finalization (hidden staves, final barlines/trailers change widths). Width-related bugs may be in either path.
- `Segment::createShapes()` is called in several places (end of `layoutMeasure`, `computePreSpacingItems`, after `hideEmptyStaves`); anything that changes a segment's item widths after that (courtesy sigs, parentheses) must re-create shapes (comment `systemlayout.cpp:418-419` admits "potential forgotten shape updates").
- Hidden/empty staves change layout: `hideEmptyStaves` (`:727`) runs *after* measure collection and forces `layoutSystem` + shape rebuild; a system's `SysStaff::show()` ≠ `Staff::show()`.
- `LayoutMode::LINE` keeps layout of measures outside the dirty range and only moves them; skylines are partial (see `continuousDist` comment) — stale spacing between staves persists until a full layout (toggling page/continuous view).
- Page-mode `headers/footers` containing a page-count token are recomputed after all pages are laid out (`mustRecomputeHeadersFooters`); `collectPage` initially uses a possibly-wrong count to reserve space.
- Paint coordinates: `paintItem` translates by `item->pagePos()`, so `TDraw::draw` code must use item-local ldata; `EditModeRenderer` instead translates by `canvasPos()` (page-offset included) — mixing the two double-offsets items on multi-page canvases.
- `Painter::drawSymbol(point, char32_t)` takes a **code point**; SMuFL glyph lookup (`SymId` → code point/bbox) goes through the engraving font in the `TLayout` (bbox) and `TDraw` (draw) halves separately.
- **Possible bug to verify (not changed):** in `Score::doLayoutRange` (`score.cpp:5021-5029`), inside the spanner loop `start`/`end` are recomputed from the **original** `st`/`et` (`start = std::min(st, spanner->tick()); end = std::max(et, spanner->tick2());`), not accumulated from `start`/`end`; the result reflects only the **last** overlapping spanner in iteration order rather than the union. For a full layout `(0, -1)` with any overlapping spanner this could also turn `et=-1` into a non-negative `end` and therefore turn `isLayoutAll` false in `ScoreLayout::layoutRange` — depends on whether `findOverlapping(0, -1)` returns anything (UNCLEAR). Record only; do not change without a repro.

## UNCLEAR — needs verification

- Whether `spannerMap().findOverlapping(0, -1)` (full-layout call) ever yields results — determines severity of the `doLayoutRange` note above.
- Exact set of element types `TDraw::drawItem` handles vs. `TLayout::layoutItem` (the two switches were not fully diffed; only the draw `default:` was read — it asserts via `UNREACHABLE`, `tdraw.cpp:436-438`).
- `MUE_ENABLE_ENGRAVING_LD_PASSES` ("layout by passes") is an experimental refactor: only `PassResetLayoutData` (always) and `PassLayoutIndependentItems` (page view gated by the macro, vertical view ungated) exist; the intended full pass architecture beyond that is not implemented in this tree. `PassBase::run` just calls `doRun`.
- Where `BufferedPaintProvider` output (`DrawData`) is serialized for vtest (JSON writer path) and the exact CLI flags (`--diagnostic-gen-drawdata`) — see `testing-and-vtest.md` when it is deepened.
- Algorithm-pass remainder (§15-§24 traced the functions named there; these were only skimmed or not opened): `ChordLayout::layoutTablature` (`chordlayout.cpp:342`, TAB chord layout), `layoutLvArticulation` (`:701`), `layoutChordBaseFingering` (`:3070`), `SlurTieLayout` partial/laissez-vibrer helpers (`:1903-2145`), `adjustX`/`adjustXforLedgerLines` tie-collision details (`:2209-2392`), `AccidentalsLayout::verticallyAlignAccidentals` internals (`accidentalslayout.cpp:953-1130`) and padding/kerning helpers, `BeamLayout::breakCrossMeasureBeams`/`restoreBeams`, `TextLayout` frame/`layoutFrame` internals, `HarmonyLayout` roman-numeral and `kernCharacters` details, and `StaveSharingLayout`'s annotation/spanner/label-text sharing (`:1446-1832`). Also not traced here: `RestLayout`, `TremoloLayout`, `ArpeggioLayout`, `DynamicsLayout`, `AutoPlace` internals, `HairpinSegment`/`Trill`/`Ottava`/`Pedal`/`Volta` line layouts, `SystemHeaderLayout`, `MMRestLayout` (dispatch sites are in §10/§7).
- Shared-staff `makeSharedNotes` only mirroring `originChord->upNote()` (§23) is an untraced HYPOTHESIS about multi-note origin chords.

*Authored (2026-11-08) from the live MuseScore 5.0 `main` source: `src\engraving\rendering\{iscorerenderer,isinglerenderer,ieditmoderenderer,layoutoptions,paintoptions}.h`, `rendering\score\{scorerenderer,scorelayout,scorepageviewlayout,scorehorizontalviewlayout,scoreverticalviewlayout,layoutcontext,pass*,systemlayout,pagelayout,measurelayout,horizontalspacing,paint,tdraw,tlayout (dispatch)}`, `rendering\single\*`, `rendering\editmode\*`, `dom\{score,masterscore,engravingitem,measure,measurebase,page,segment}.cpp/h`, `editing\{cmd,transaction}`, `infrastructure\{shape,skyline,ld_access}.h`, `muse\framework\draw\*`. Algorithm pass (sections 15-24, 2026-11-08) additionally read from `rendering\score\{tlayout (dispatch + layoutNote/Articulation/Ornament), chordlayout, accidentalslayout, stemlayout, beamlayout, beamtremololayout, slurtielayout, tupletlayout, textlayout, lyricslayout, harmonylayout, headerfooterlayout, stavesharinglayout}.cpp`, `dom\articulation.cpp`, `infrastructure\ld_access.h`. **If this contradicts the live source, the source wins.***
