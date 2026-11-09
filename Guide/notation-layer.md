# MuseScore — Notation Layer

> **What this file is.** Scope: `src\notation\` — the **facade over engraving** (`INotation` and its ~15 sub-interfaces: interaction, selection, note input, undo, parts/excerpts, style, elements, painting, playback, MIDI input, accessibility) — **plus the command-dispatch side that lives in `src\notationscene\`** (`NotationActionController`, `NotationCommandsRegister`/`NotationCommandsState`, typed commands), because that is where the notation facade is actually driven. **Live source wins** over this file (precedence: source → in-repo docs/ADRs → online docs → this Guide). Start at `00-core.md`. The score object model itself is in `engraving-dom.md` — this file points at it, it does not repeat it.
>
> **Status: ✅ deep pass 1 + 2 (2026-11-08).** Pass 1 source-verified: every `INotation*` interface header, `Notation`/`MasterNotation`/`ExcerptNotation`, `NotationUndoStack`, the engraving `Transaction`/`TransactionManager`/`UndoStack` it wraps, `NotationSelection::select`, `NotationNoteInput` (start/end/add/put), `NotationStyle`, the whole `NotationActionController::init()` registration table + the `registerCommand` templates, `NotationCommandsState`, the notation module setup, and ADR 00101/00104. **Pass 2 (this revision) added §7–§13:** the interaction/editing state machine and `EditData`, drag, drop (`startDrop*`/`updateDrop*`/`dropSingle`/`dropRange` and the three `prepareDrop*` target resolvers), palette application, text + lyrics editing, grip/element editing, layout-lock handling, and the full impl bodies of `NotationParts`/`MasterNotationParts`/`NotationPlayback`/`NotationPainting`/`NotationAccessibility`/`NotationElements`/`NotationViewState`/`NotationAutomation`, plus the `doAddNote` tail (now TRACED, §5.5). The ~150 one-liner "do X to selection" bodies of `notationinteraction.cpp` are tabulated by idiom, not read one by one (see UNCLEAR).

## 0. 60-second orientation

- **What it is.** `src\notation\` is a thin-ish *editing session* layer. One `Notation` object wraps **one engraving `Score*`** (the master score or one excerpt/part score) and hands the UI a set of narrow sub-interfaces. It owns **no score data** — all state (selection, input state, undo history) is stored *in engraving* and merely exposed + notified about here.
- **The shape you must internalise** (all verified):
  1. `INotation` = pure aggregate: getters returning `shared_ptr` to sub-interfaces (`inotation.h:40-105`).
  2. `Notation` (impl) constructs all sub-objects in its ctor and wires their change signals into one `notationChanged` channel (`internal\notation.cpp:50-100`).
  3. `MasterNotation` = `Notation` + `IMasterNotation` (excerpt list, playback, automation, new-score setup); `ExcerptNotation` = `Notation` + `IExcerptNotation` (one part score).
  4. **Every undoable edit funnels into `INotationUndoStack`**, which is a thin wrapper over the **MasterScore's** `TransactionManager`/`UndoStack` — **there is exactly one undo stack per project, shared by master and all excerpts** (`internal\notationundostack.cpp:286-294`).
  5. **UI → notation is driven by *commands* (not actions)** registered in `NotationActionController` (module `notationscene`), dispatched through `muse::rcommand::ICommandDispatcher` (ADR 00101/00104).
- **Where the code is:**

| Path | Contents |
|---|---|
| `src\notation\inotation*.h`, `imasternotation.h`, `iexcerptnotation.h` | the interfaces (root of module) |
| `src\notation\internal\` | implementations (`notation.*`, `masternotation.*`, `excerptnotation.*`, `notationinteraction.*` (8.4k lines), `notationnoteinput.*`, `notationselection*.*`, `notationundostack.*`, `notationparts.*`/`masternotationparts.*`, `notationstyle.*`, `notationelements.*`, `notationpainting.*`, `notationplayback.*`/`notationplaybackstub.*`, `notationmidiinput.*`, `notationaccessibility.*`, `notationautomation.*`, `notationviewstate.*`, `notationsolomutestate.*`, `scorecallbacks.*`, `previewmeasure.*`, `searchcommandsparser.*`, `igetscore.h`) |
| `src\notation\types\`, `notationtypes.h`, `notationerrors.h`, `utilities\` | value types, enums, helpers |
| `src\notation\notationmodule.cpp` | module setup (config + writers only — **does not create `INotation`s**) |
| `src\notationscene\` | commands: `notationcommands.h`, `notationcommandsenums.h`, `internal\notationactioncontroller.*`, `internal\notationcommandsregister.*`, `internal\notationcommandsstate.*`; QML/widgets (see `ui-qml-mvvm.md`) |
| `src\project\internal\notationproject.cpp:116` | where the `MasterNotation` is created (see §2) |

## 1. The facade model — `INotation` and what it aggregates

`INotation` (`src\notation\inotation.h`) is pure virtual. Its surface, grouped:

| Group | Members (all `const` getters unless noted) | Returns / notes |
|---|---|---|
| Identity | `iocContext()`, `project()`, `masterNotation()`, `isMaster()` | `project()` → `project::INotationProject*`; `masterNotation()` → `IMasterNotationPtr` (for an excerpt, its owner) |
| Naming | `name()`, `projectName()`, `projectNameAndPartName()`, `workTitle()`, `projectWorkTitle()`, `projectWorkTitleAndPartName()` | master: file name w/o extension; part: excerpt name. `workTitle` = score `workTitle` meta tag, falls back to `masterScore()->name()` (`notation.cpp:174-200`) |
| Open state | `isOpen()`, `setIsOpen(bool)`, `openChanged()` | delegates to `Score::isOpen/setIsOpen` (`notation.cpp:216-235`) |
| View mode | `viewMode()`, `setViewMode()`, `viewModeChanged()` | delegates to `painting()` (`notation.cpp:267-280`) |
| Sub-interfaces | `painting()`, `viewState()`, `soloMuteState()`, `interaction()`, `midiInput()`, `undoStack()`, `style()`, `elements()`, `accessibility()`, `parts()` | each a `shared_ptr<INotationXxx>` (aliases in `inotation_fwd.h`) |
| Change feed | `notationChanged()` | `async::Channel<RectF>` — the **single** "something repaint-worthy changed" signal; empty `RectF` = whole view |
| Misc | `hasVisibleParts()` | true if any `Part::show()` |

⭐ **Not on `INotation`:** `playback()` and `automation()` — these are **master-only** and sit on `IMasterNotation` (`imasternotation.h:71,74`). Excerpts have **no** playback object; their tracks come from the master.

### 1.1 The sub-interface inventory

| Interface | File | One-line purpose | Impl (`internal\`) |
|---|---|---|---|
| `INotationInteraction` | `inotationinteraction.h` | **the big one** (~300 methods): hit-test, select, drag/drop, text/grip edit, ~150 "do X to selection" edits, navigation, layout locks | `notationinteraction.*` |
| `INotationNoteInput` | `inotationnoteinput.h` | the note-entry mode state machine (obtained via `interaction()->noteInput()`) | `notationnoteinput.*` |
| `INotationSelection` (+`Range`, `Filter`) | `inotationselection.h`, `…selectionrange.h`, `…selectionfilter.h` | read-only view of `Score::selection()` + `select(...)` impl | `notationselection*.*` |
| `INotationUndoStack` | `inotationundostack.h` | transaction begin/commit/rollback, undo/redo, stack state | `notationundostack.*` |
| `INotationParts` | `inotationparts.h` | instruments/parts/staves/system-object staves CRUD + `NotifyList`s | `notationparts.*`, `masternotationparts.*` |
| `INotationStyle` | `inotationstyle.h` | get/set/reset style values, load/save `.mss` | `notationstyle.*` |
| `INotationElements` | `inotationelements.h` | `msScore()`, search, filtered element lists, measure/page lookup | `notationelements.*` |
| `INotationPainting` | `inotationpainting.h` | paint to view/PDF/print/PNG, page size/count | `notationpainting.*` |
| `INotationViewState` | `inotationviewstate.h` | zoom, matrix, view mode, per-score UI state **persisted into the `.mscz`** (`read/write(MscReader/MscWriter)`) | `notationviewstate.*` |
| `INotationMidiInput` | `inotationmidiinput.h` | `onMidiEventReceived`, `notesReceived()`, `onRealtimeAdvance()` | `notationmidiinput.*` |
| `INotationAccessibility` | `inotationaccessibility.h` | `accessibilityInfo()`, `setMapToScreenFunc`, `setEnabled` | `notationaccessibility.*` |
| `INotationSoloMuteState` | `inotationsolomutestate.h` | per-track solo/mute | `notationsolomutestate.*` |
| `INotationPlayback` (**master only**) | `inotationplayback.h` | engraving→playback bridge: track data, loop, tempo multiplier, metronome/count-in, sound flags | `notationplayback.*` (or `notationplaybackstub.*`) |
| `INotationAutomation` (**master only**) | `inotationautomation.h` | automation mode flag + curve edit | `notationautomation.*` |
| `IMasterNotation` / `IExcerptNotation` | `imasternotation.h`, `iexcerptnotation.h` | see §3 | `masternotation.*`, `excerptnotation.*` |
| `INotationConfiguration` | `inotationconfiguration.h` | **global** settings (colors, wallpapers, default note-input method, canvas orientation, styles path…) | `notationconfiguration.*` |
| `INotationContextConfiguration` | `inotationcontextconfiguration.h` | **per-window-context** settings | `notationcontextconfiguration.*` |

`MasterNotation` is only compiled with a real `NotationPlayback` when `MUE_BUILD_ENGRAVING_PLAYBACK` is defined; otherwise `NotationPlaybackStub` (`masternotation.cpp:57-61, 89-93`). See `playback-and-audio.md`.

### 1.2 Construction & signal wiring (`Notation::Notation`, `notation.cpp:50-100`)

```cpp
// Notation::Notation — creation order matters: undoStack before interaction, interaction before parts
m_painting    = std::make_shared<NotationPainting>(this);
m_viewState   = std::make_shared<NotationViewState>(this, iocCtx);
m_soloMuteState = std::make_shared<NotationSoloMuteState>();
m_undoStack   = std::make_shared<NotationUndoStack>(this, m_notationChanged);
m_interaction = std::make_shared<NotationInteraction>(this, m_undoStack);
m_midiInput   = std::make_shared<NotationMidiInput>(this, m_interaction, m_undoStack, iocContext());
m_accessibility = std::make_shared<NotationAccessibility>(this);
m_style       = std::make_shared<NotationStyle>(this, m_undoStack);
m_parts       = std::make_shared<NotationParts>(this, m_interaction, m_undoStack, m_style);
m_elements    = std::make_shared<NotationElements>(this);
```

- Sub-objects take **`IGetScore*`** (= the `Notation`, `internal\igetscore.h`) — *not* a `Score*` — so they can be built before a score exists. `IGetScore` has just `score()` and `scoreInited()`.
- The ctor then connects `noteInput()->noteAdded`, `dragChanged`, `textEditingChanged`, `dropChanged`, `midiInput->notesReceived`, `style->styleChanged`, `parts->partsChanged` → `notifyAboutNotationChanged()` (i.e. `m_notationChanged.send(RectF())`) (`notation.cpp:65-91`).
- It also listens to `configuration()->canvasOrientation()` and re-lays-out when `autoLayoutEnabled()` (`:93-97`).
- ⛔ `~Notation` nulls the sub-objects **before** dropping `m_score` so none outlive the Score (`:102-118`). The *master* score is deleted later from `~EngravingProject()`; excerpt scores in `~MasterScore()` (comment at `:115-116`).

## 2. How it sits ON TOP of engraving (the boundary)

### 2.1 Ownership & lifetime

- `Notation::m_score` is a **raw, non-owning `engraving::Score*`** (`notation.h:104`). `setScore()` stores it and fires `scoreInited()` (`notation.cpp:135-143`), which `NotationInteraction` subscribes to (`notationinteraction.cpp:310-312`) to re-attach shadow note / preview measure / element-destroyed hooks.
- **Who creates the `MasterNotation`:** `NotationProject` (`src\project\internal\notationproject.cpp:116`): `m_masterNotation = std::shared_ptr<MasterNotation>(new MasterNotation(this, iocContext()))`. The ctor is **private** with `friend class project::NotationProject` (`masternotation.h:79-80`) — nobody else can make one. The `EngravingProject` owns the `MasterScore`; `MasterNotation::setMasterScore(score)` (`masternotation.cpp:163-177`) then points the notation at it, calls `score->updateSwing()`, inits playback and builds excerpt notations.
- `NotationModule` does **not** register any `INotation` — only `INotationConfiguration` (global) + `IInstrumentsRepository`, plus `INotationContextConfiguration` per context (`notationmodule.cpp:48-59, 107-115`). `INotation`s are reached through `project::IProjectSession` (`currentNotation()`, `masterNotation()`, with `currentNotationChanged()` / `masterNotationChanged()` notifications — `src\project\iprojectsession.h:42-47`).

### 2.2 Master vs excerpt

| | `MasterNotation` | `ExcerptNotation` |
|---|---|---|
| Score | `MasterScore` (`masterScore()` = `dynamic_cast<MasterScore*>(score())`, `masternotation.cpp:179-182`) | `m_excerpt->excerptScore()` set in `init()` (`excerptnotation.cpp:45-54`) |
| Parts impl | `MasterNotationParts` (assigned over `Notation`'s default in `MasterNotation` ctor, `masternotation.cpp:87`) | plain `NotationParts` |
| Playback / automation | yes | **no** |
| Undo stack | MasterScore's | **the same one** (see 2.3) |
| `~dtor` | drops parts, `unloadExcerpts(m_potentialExcerpts)` | `setScore(nullptr)` only — **does not delete** the score: *"it may be stored in UndoStack (after opening an excerpt via the Parts dialog and pressing ctrl+z)"* (`excerptnotation.cpp:38-43`) |

### 2.3 The undo stack boundary (⭐ most important fact)

`NotationUndoStack` holds an `IGetScore*` and resolves everything **through the master score** every time:

```cpp
// notationundostack.cpp:286-294
TransactionManager* NotationUndoStack::transactionManager() const
{
    return score() ? score()->masterScore()->transactionManager() : nullptr;
}
UndoStack* NotationUndoStack::undoStack() const
{
    return score() ? score()->masterScore()->undoStack() : nullptr;
}
```

→ Opening/editing a part score pushes onto the *same* stack as the master; ctrl-Z in a part undoes master-level history. `MasterScore::undoStack()` / `transactionManager()` are `masterscore.h:103-104`. Engraving-side semantics (what an `UndoableCommand` is, `CmdState`, etc.) → `engraving-dom.md` (its "undo-command classes" sub-pass is still pending there).

### 2.4 Engraving's `Transaction` model that the notation stack wraps

(`src\engraving\editing\transaction\transaction.h/.cpp`, `undostack.h`)

| Type | Role |
|---|---|
| `Transaction` | handle passed to editing functions; `push(cmd)` appends **then** performs (`redo()`), so nested pushes land after their parent (`transaction.cpp:107-120`). A **dummy** transaction just performs + deletes (no undo record) (`:100-113`) |
| `TransactionManager` | per-`MasterScore`. `transaction(desc, fn)`, `beginTransaction`, `endTransaction(rollback, layoutAllParts, keepRolledBackElements)`, `currentTransaction()`, `currentOrDummyTransaction()`, `undoRedo(undo, EditData*)`, `reopen()` |
| `UndoStack` / `UndoableTransaction` | the history; each `UndoableTransaction` records undo/redo `InputState` **and** `SelectionInfo` so undo restores note-input + selection (`undostack.h:44-113`) |

`endTransaction` (`transaction.cpp:178-229`) does, in order: bail if stack locked; force **rollback** if score `readOnly()` or `MScore::_error` set and not a warning; `m_masterScore->update(false, layoutAllParts)` **inside** the transaction (so layout-generated commands are recorded); build `ScoreChanges`; close the stack transaction (`endTransaction(isEmpty)`); `invalidateRepeatList()` if dirty; and **only if non-empty and not rolled back** → `updateAutomation(changes)` + `changesChannel().send(changes)`.

The header comment is explicit that this is a transitional design: *"The aim is to eliminate Score::undo"*, with `currentOrDummyTransaction()` kept "for calling methods that do need a `Transaction&` in a context that does not have it yet" (`transaction.h:43-63`). Expect both styles (`transaction(...)` lambdas and `startEdit/apply` + `Score::undo`) to coexist in notation code.

## 3. Master/excerpt notation surfaces

### 3.1 `IMasterNotation` (`imasternotation.h:40-75`)

| Method | Meaning |
|---|---|
| `project()`, `notation()` | owner project; the `INotationPtr` view of itself (`shared_from_this`) |
| `setupNewScore(MasterScore*, ScoreCreateOptions)` | new-score wizard path: `undoStack()->lock()`, `parts()->setParts(...)`, `checkChordList/updateSwing/updateCapo`, `applyOptions`, `initAutomation`, `initAfterSettingScore`, `addExcerptsToMasterScore`, `unlock()` (`masternotation.cpp:266-291`). ⭐ undo is **locked** so the creation isn't undoable |
| `applyOptions(score, options, fromTemplate)` | clears template measures (if template), `createMeasures`, title/subtitle/composer/lyricist VBox, optional tempo text, `doLayout`, `playback->reload()` (`:293-465`). Copied from MU3 (`:264-265`) |
| `masterScore()`, `setMasterScore(score, disablePlayback)`, `mscVersion()` | score access |
| `createEmptyExcerpt(name)` | new `Excerpt` + `ExcerptNotation` (not yet in the score) |
| `excerpts()` / `excerptsChanged()` / `potentialExcerpts()` | live part-score notations / change notification / **lazily computed** per-part candidates (`updatePotentialExcerpts()` runs on every `potentialExcerpts()` call, `masternotation.cpp:795-800`) |
| `initExcerpts`, `setExcerpts`, `resetExcerpt`, `sortExcerpts`, `setExcerptIsOpen` | excerpt lifecycle (below) |
| `parts()`, `hasParts()`, `hasPartsChanged()` | instruments |
| `playback()`, `initNotationSoloMuteState(notation)`, `automation()` | master-only objects |

**Excerpt change sync.** `initAfterSettingScore` subscribes to `score->changesChannel()` and calls `updateExcerpts()` unless `changes.isTextEditing` (`masternotation.cpp:150-154`). `updateExcerpts()` returns early unless `masterScore()->excerptsChanged()`; it keeps notations for excerpts still in the score, creates + inits notations for new ones (laying out those that are open), calls `doSetExcerpts`, then clears the flag (`:626-668`). So undoing "Add/remove parts" automatically adds/removes the notation wrappers.

`setExcerpts(list)` wraps its work in **one** named transaction `"Add/remove parts"` via `undoStack()->prepareChanges`/`commitChanges`, deleting dropped excerpts, re-ordering kept ones, `initAndAddExcerpt` new ones (`:496-544`). `resetExcerpt` = delete the old `Excerpt`, add a copy-constructed new one, `reinit` the wrapper, under `"Reset part"` (`:546-569`).

`initNotationSoloMuteState` mutes in an excerpt every track of a master part that is absent/invisible in that excerpt (`:771-793`).

### 3.2 `IExcerptNotation` (`iexcerptnotation.h`)

`isInited/isCustom/isEmpty`, `name/setName` (**not undoable**) vs `undoSetName` (**undoable**, pushes `ChangeExcerptTitle` inside `"Rename part"`, `excerptnotation.cpp:101-118`), `nameChanged()`, `hasFileName/fileName`, `notation()`, `clone()` (copy `Excerpt`, `markAsCustom()`).

### 3.3 `INotationParts` (`inotationparts.h`) — instruments/parts/staves

Reads: `partList()` / `staffList(partId)` return **`NotifyList<const Part*>`/`<const Staff*>`** (a list that also emits item added/removed/changed; `MasterNotation` subscribes to it to mark potential excerpts dirty, `masternotation.cpp:101-112`), `hasParts`, `part/partExists`, `staff/staffExists`, `staffConfig(staffId, tick)`, `scoreOrder()`, `systemObjectStaves()`.

Writes (grouped): `setParts(PartInstrumentList, ScoreOrder)`; `setScoreOrder`; `setPartVisible/setVoiceVisible/setStaffVisible`; `setPartSharpFlat`; `setInstrumentName/Abbreviature/GroupNameOptions/Number`; `setStaffType/setStaffConfig`; `setSharedPartEnabled`/`toggleStaveSharing`; `removeParts/removeStaves`; `moveParts/moveStaves(InsertMode::Before|After)`; `appendStaff/appendStaffLinkedToMaster/appendLinkedStaff`; `insertPart`; `replacePart/replaceInstrument/replaceDrumset(undoable)`; system objects (`addSystemObjects/removeSystemObjects/moveSystemObjects/moveSystemObjectLayer{Below,Above}BottomStaff`). Notifications: `partsChanged`, `scoreOrderChanged`, `sharedPartsChanged`, `systemObjectStavesChanged`.

- Each mutating method does `startEdit(name)` → work → `apply()` where `apply()` = `undoStack()->commitChanges()` **then** `m_partsChanged.notify()` (`notationparts.cpp:933-948`).
- `NotationParts::listenUndoStackChanges()` subscribes to `m_undoStack->changesChannel()`; if the change touched `SCORE/STAFF/PART/SHARED_PART` it re-snapshots `m_parts`/`m_systemObjectStaves` and fires the right notifiers; text-editing changes and `m_ignoreUndoStackChanges` are skipped (`:293-339`). So **undo/redo also refreshes the instruments panel**.
- **`MasterNotationParts`** overrides the structural ops to use `startGlobalEdit/endGlobalEdit` (`masternotationparts.h:55-56`) — one outer transaction that also reshapes every excerpt's parts (`excerptsParts()`), e.g. `"Add/remove instruments"`, `"Remove instruments"`, `"Remove staves"`, `"Add staff"`, `"Add linked staff"` (`masternotationparts.cpp:76-187`). `appendStaffLinkedToMaster` returns `false` on the master (`masternotationparts.h:41`) — it is meaningful only in an excerpt.

## 4. The editing surfaces

### 4.1 `INotationInteraction` — grouped inventory (`inotationinteraction.h`, 349 lines)

| Group | Key members |
|---|---|
| Note input | `noteInput()` → `INotationNoteInputPtr` (§4.3) |
| Shadow note / preview | `shadowNote()`, `showShadowNoteForPosition(PointF)`, `showShadowNoteForMidiPitch`, `hideShadowNote()`, `shadowNoteRect/previewMeasureRect`, `shadowNoteChanged()` |
| Hit testing | `hitElement(pos,width)`, `hitElements`, `hitStaff`, `hitElementContext()/setHitElementContext()` (`HitElementContext{element, staff}`) |
| Selection | `selection()`, `selectionChanged()`, `select(items, SelectType, staffIdx)`, `select(SelectionTarget)`, `addToSelection(SelectionTarget)`, `clearSelection()`, `selectAndStartEditIfNeeded`, `findAndSelectChordRest(tick)`, `moveSelectionAvailable(MoveSelectionType)`, `selectionFilter()`, `contextItem()` |
| Drag (in-score) | `isDragStarted`, `startDrag(elems, offset, IsDraggable)`, `drag(from,to,DragMode)`, `endDrag`, `dragChanged()` |
| Drag (out → OS) | `isOutgoingDragElementAllowed`, `startOutgoingDragElement/Range`, `endOutgoingDrag` |
| Drop | `startDropSingle/Range/Image`, `updateDropSingle/Range`, `dropSingle`, `dropRange`, `setDropTarget`, `setDropRects`, `endDrop`, `dropChanged()` |
| Palette | `applyPaletteElement(element, modifiers)` |
| Undo shortcuts | `undo()`, `redo()`, `undoRedoToIndex(idx)` (delegate to the undo stack, §4.4) |
| Move/nudge | `movePitch(dir, PitchMode)`, `nudge`, `nudgeAnchors`, `moveChordRestToStaff`, `swapChordRest`, `toggleSnapToPrevious/Next` |
| Text edit | `startEditText`, `editText(QInputMethodEvent*)`, `endEditText`, `changeTextCursorPosition`, `selectText`, `editedText()`, `isTextEditingStarted`, `textEditingStarted/Changed/Ended()` |
| Grip/element edit | `startEditGrip`, `endEditGrip`, `isHitGrip`, `startEditElement`, `changeEditElement`, `isEditAllowed(QKeyEvent*)`, `editElement(QKeyEvent*)`, `endEditElement`, `isEditingElement()`, `isEditingElementChanged()` |
| ~150 "do X to selection" | `copySelection/pasteSelection(scale)/deleteSelection/repeatSelection/swapSelection`, `flipSelection`, `toggleTieForSelection`, `addSlurToSelection`, `addOttavaToSelection`, `addHairpinsToSelection`, `toggleAccidentalForSelection`, `toggleArticulationForSelection`, `addGraceNotesToSelectedNotes`, `addTupletToSelectedChordRests`, `addBeamToSelectedChordRests`, `increaseDecreaseDuration`, `transpose`, `swapVoices`, `changeSelectedElementsVoice`, `insertClef`, `addTimeSignature`, `explode/implodeSelectedStaff`, `removeSelectedMeasures/Range`, `fillSelectionWithSlashes`, `spellPitches*`, `regroupNotesAndRests`, `resetShapesAndPosition`, `resetToDefaultLayout`, … |
| Layout locks/breaks | `toggleLayoutBreak(LayoutBreakType)`, `moveMeasureToPrev/NextSystem`, `toggleSystem/Score/PageLock`, `makeIntoSystem/Page`, `applySystemLock/PageLock`, `moveSystemToPrev/NextPage`, `addRemoveSystemLocks` |
| Boxes | `addBoxes(BoxType, count, AddBoxesTarget)` / `(…, beforeBoxIndex, insertAfter)` |
| Text/lyrics/harmony navigation | `navigateToLyrics`, `navigateToLyricsVerse`, `navigateToNextSyllable`, `navigateToNearHarmony`/`…FiguredBass`/`…Text`, `…InNearMeasure`, by-`Fraction` variants |
| Text style | `toggleBold/Italic/Underline/Strike/SubScript/SuperScript` |
| Score config | `scoreConfig()`, `setScoreConfig()`, `scoreConfigChanged()` (`ScoreConfigType`: show invisible/unprintable/frames/page margins/sound flags, mark irregular measures) |
| Escape hatch | `execute(void (Score::*)(), actionName)` — run an arbitrary `Score` method as one named undoable edit |
| View | `showItem(item, staffIdx)`, `showItemRequested()`, `setGetViewRectFunc(fn)` |
| Errors | `checkAndShowError()` (reads engraving `MScore` error state → `MScoreErrorsController`) |

`NotationInteraction` (impl) holds `m_selection` (`NotationSelection`), `m_selectionFilter`, `m_noteInput`, `m_editData` (`engraving::EditData` — the live drag/edit state), `m_dragData`, `ScoreCallbacks m_scoreCallbacks` (an engraving `MuseScoreView` subclass that routes `dataChanged/updateAll/setDropTarget/changeEditElement/adjustCanvasPosition` back into the interaction, `scorecallbacks.h`), `m_previewMeasure`, a text-cursor blink `QTimer`. It `Inject`s the single-item and edit-mode renderers (`engravingRenderer`, `editModeRenderer`) and `ISelectInstrumentsScenario` (`notationinteraction.h:67-72`).

### 4.2 The edit idiom (⭐ how every edit becomes undoable)

Three coexisting patterns in `notationinteraction.cpp` (and `NotationNoteInput`/`NotationParts`):

```cpp
// (a) startEdit/apply — explicit begin/commit  (notationinteraction.cpp:371-392)
void NotationInteraction::startEdit(const TranslatableString& n){ m_notifyAboutDropChanged = false; m_undoStack->prepareChanges(n); }
void NotationInteraction::apply(){ m_undoStack->commitChanges(); /* then notify drop- or notation-changed */ }
void NotationInteraction::rollback(){ m_undoStack->rollbackChanges(); }

// (b) execute(Score::*fn) — wrap a Score method  (:8441-8446)
void NotationInteraction::execute(void (Score::*function)(), const TranslatableString& actionName)
{ startEdit(actionName); (score()->*function)(); apply(); }

// (c) transaction(lambda)  — the new style, passes a Transaction&  (:366-369, :8391-8396)
void NotationInteraction::insertClef(ClefType type)
{ transaction(TranslatableString("undoableAction","Add clef"), [&](Transaction& tx){ EditClef::insertClef(tx, score(), type); }); }
```

- Action names are `TranslatableString("undoableAction", "…")` — these are what the Edit menu / undo history show (`topMostUndoActionName()` etc.).
- The actual document mutation lives in `src\engraving\editing\*` (`EditNote`, `EditChord`, `EditClef`, `NoteInput`, `Transpose`, `Reset`, …); the notation layer is only the transaction + notification shell. When adding a feature, put the logic in `engraving\editing` and the begin/commit/notify shell here (see playbook `add-feature-or-fix.md`).
- `NotationUndoStack::prepareChanges/rollbackChanges/commitChanges` are **silent no-ops while the stack is locked** (`notationundostack.cpp:124-126, 137-139, 150-152`) — `setupNewScore` relies on that.
- Drag is one long-running transaction: `startDrag` … `drag` … `endDrag()` → `doEndDrag(); apply();` and, for a hairpin created by dragging from a dynamic, it **merges the two undo steps** with `mergeTransactions(currentStateIndex()-2)` (`:1257-1273`). `clearSelection()` during a drag does `doEndDrag(); rollback();` (`:1023-1027`).

### 4.3 Selection

- **State lives in engraving**: `NotationSelection` reads `score()->selection()` (`engraving::Selection`) — there is no separate notation-side selection model. `INotationSelection` (`inotationselection.h:53-85`): `isNone/isSingle/isRange/state()` (`SelectionState` = `engraving::SelState`), `canCopy`, `mimeData`/`qMimeData`, `element()`, `elements()`, `notes(NoteFilter{All,WithTie,WithSlur})`, `canvasBoundingRect`, `range()` (→ `INotationSelectionRange`: start/end staff/segment/tick/box, `measureBaseRange`, `selectedParts`, `boundingArea`, `containsPoint/Item`, `containsMultiNoteChords`), `lastElementHit`, `startMeasureBase/endMeasureBase/measureBasesInSelection/selectedSystems/pagesContainingSelection`, `elementsSelected(ElementTypeSet)`.
- **Mutation path:** `NotationInteraction::select(elements,type,staff)` first ends text/element editing if the new selection requires it, then calls `m_selection->select(...)` (`notationinteraction.cpp:976-985`). `NotationSelection::select` snapshots the old selection (elements, state, tick/staff/box range), calls `doSelect` → `Score::select(...)`, and **only fires `selectionChanged` if something actually differed** (else `score()->setSelectionChanged(false)`) (`notationselection.cpp:400-434`). `doSelect` handles `SelectType::REPLACE` (`deselectAll` then `ADD`), Ctrl+click toggle (**reads `QGuiApplication::keyboardModifiers()` directly**, `:440-450`), and key/time-signature "select the same item across all staves of the segment" (`:457-471`).
- `select(SelectionTarget)` / `addToSelection(SelectionTarget)` are the **keyboard navigation** entry points (next/prev item/segment/track/frame/system, up/down note in chord, top staff, similar…, all, section; expand to begin/end of system/score). Enums in `notationtypes.h:113-167`: `SelectionTarget` (35 values incl. `Undefined=0`), `MoveSelectionType`, `MoveDirection`, `PlayMode{NoPlay,PlayNote,PlayChord}`; `SelectType` is `engraving::SelectType` (`mscore.h:106`).
- `selectionChanged()` is the **only** selection signal. It also fires after undo/redo: `m_undoStack->stackChanged()` → `m_selection->notifyAboutSelectionChangedIfNeed()` (`notationinteraction.cpp:295-298`) — because `UndoableTransaction` restores `SelectionInfo`.
- `INotationSelectionFilter`: per-type filter flags (`SelectionFilterTypesVariant`) + `includeSingleNotes`.

### 4.4 Undo stack — `INotationUndoStack` (`inotationundostack.h`)

| Method | Behaviour (impl `notationundostack.cpp`) |
|---|---|
| `canUndo/canRedo`, `isStackClean`, `undoRedoActionCount`, `currentStateIndex` | straight from `UndoStack` (`canUndo()` = `m_currentIndex > 0`) |
| `undo(EditData*)`, `redo(EditData*)` | `transactionManager()->undoRedo(...)` then **three** notifications: `notationChanged(RectF())`, `undoRedoNotification`, `stackChanged` (`:48-59`). `TransactionManager::undoRedo` asserts **no active transaction** and bails if `readOnly()` (`transaction.cpp:262-285`) |
| `undoRedoToIndex(idx, EditData*)` | loops `undoRedo` until index reached (`:83-105`); `NotationInteraction::undoRedoToIndex` calls `endEditElement()` first (`notationinteraction.cpp:4512-4516`) |
| `transaction(name, fn)` | `TransactionManager::transaction` (asserts if one is already active, then runs `fn` inside it with a warning) then `stackChanged` |
| `prepareChanges(name)` / `commitChanges()` / `rollbackChanges()` | begin / `endTransaction(false)` / `endTransaction(true)`; **no-op if locked**; `commitChanges` also fires `stackChanged` (`:118-157`) — *note `rollbackChanges` does **not** fire `stackChanged`* |
| `mergeTransactions(startIdx)` | collapse several steps into one (used by hairpin-drag) |
| `lock()/unlock()/isLocked()` | `UndoStack::setLocked` — while locked, begin/end are ignored (used by `setupNewScore`) |
| `topMostUndoActionName/RedoActionName`, `lastActionNameAtIdx` | `TranslatableString`s for the menu/history panel |
| `stackChanged()` | notification — drives Undo/Redo enabled state (see §5) |
| `changesChannel()` | `Channel<ScoreChanges>` — **forwards `score()->changesChannel()`** (`:262-269`); this is the high-level "what element types/properties/style IDs changed" feed that parts, excerpts, properties panel, and playback react to |
| `undoRedoNotification()` | fires only on undo/redo (not on normal commits) |

### 4.5 Note input — `INotationNoteInput` (`inotationnoteinput.h`)

- **State lives in the Score:** `state()` returns `score()->inputState()` (`NoteInputState` = alias of `engraving::InputState`, `types\noteinputtypes.h:32`); `isNoteInputMode()` = `inputState().noteEntryMode()`. `NoteInputMethod` = `engraving::NoteEntryMethod` {`BY_NOTE_NAME, BY_DURATION, REPITCH, RHYTHM, REALTIME_AUTO, REALTIME_MANUAL, TIMEWISE`, `UNKNOWN`}.
- Lifecycle: `startNoteInput(method, focus)` (`notationnoteinput.cpp:108-172`) → resolve start position (`resolveNoteInputStartPosition`: selection, else top-left CR on the page in view via `m_getViewRectFunc`, else last input segment, else first note in voice 0), **refuses in a shared part**, selects it, defaults duration to quarter, clears accidental/rest, sets `noteEntryMode(true)` + method (falls back to `BY_NOTE_NAME` if the method is unavailable — `BY_DURATION` is unavailable on TAB staves, `:70-77`), sets TAB string, fires `noteInputStarted(focus)` + `stateChanged`, announces via accessibility. `endNoteInput(resetState)` clears mode, slur, optionally track/segment/notes, fires `noteInputEnded`, announces "Normal mode" (`:389-418`).
- Edits: `addNote(params, NoteAddingMode{CurrentChord,NextChord,InsertChord})` wraps `NoteInput::addPitch(...)` in `startEdit/apply` with action names "Add note to chord"/"Insert note"/"Enter note" (`:456-486`); `putNote(pos, replace, insert)` (mouse), `removeNote(pos)`; duration/rest/dots go through `applyNoteValueChange(name, fn)` → `NoteInput::setDuration/toggleRest/toggleDots` (`:488-523`). By-duration extras: `setInputNote(s)`, `moveInputNotes(up, PitchMode)`, `setAccidental/Articulation/DrumNote/CurrentVoice/CurrentTrack`, `addTuplet/Slur/Tie/LaissezVib`, `double/halveNoteInputDuration`.
- Signals: `noteInputStarted(bool focus)`, `noteInputEnded`, `noteAdded`, `stateChanged` (the one command-state code listens to). `stateChanged` is also re-fired on selection changes in input mode and after undo/redo (`notationinteraction.cpp:295-304`).
- `INotationMidiInput` is a **sibling**, not a child of note input: it holds interaction + undo stack and uses its own transactions (`prepareChanges "Enter note"`, `transaction "Realtime advance"`, `notationmidiinput.cpp:326-329, 509`).

### 4.6 Style, elements, painting, view state (brief)

- `INotationStyle`: `styleValue/defaultStyleValue/setStyleValue/resetStyleValue(s)/resetAllStyleValues/canApplyToAllParts/applyToAllParts/styleChanged/loadStyle/saveStyle`. ⚠ **`setStyleValue` does NOT open a transaction itself** — it calls `score()->undoChangeStyleVal(...)` then `update()` (`notationstyle.cpp:56-70`) (concert pitch special-cased via `cmdConcertPitchChanged`). The **caller must bracket it** with `undoStack()->prepareChanges/commitChanges` — which is exactly what dialogs/models do (`editstyle.cpp`, `pagesettings.cpp`, `engravingstylemodel.cpp`, …). `loadStyle` opens its own "Load style" transaction (`:119-126`); `resetAllStyleValues` uses `currentOrDummyTransaction()` (`:109`). `applyToAllParts` is parts-only (`!isMaster()`) and pushes `ChangeStyle` directly on each excerpt score (`:93-105`). Details of `Sid`/`MStyle` → `style-system.md`.
- `INotationElements`: `msScore()` is the **sanctioned raw escape hatch to the `Score*`** for UI code (`NotationActionController::currentNotationScore()` uses it). `search(text)`, `elements(FilterElementsOptions)` (type, staff range, voice, system, duration, beat, measure, subtype), `measure(idx)`, `pages()`, `pageByPoint`.
- `INotationPainting`: `paintView/paintPdf/paintPrint/paintPng` taking `IScoreRenderer::ScorePaintOptions`; view mode lives here. `NotationInteraction::paint` additionally draws preview measure, input preview, shadow note, anchors, text edit mode, selection range, grips, lasso, drop (`notationinteraction.cpp:431-447`). Rendering internals → `layout-and-rendering.md`.
- `INotationViewState`: matrix/zoom/view mode in memory, plus read/write into the project file (`MscReader/MscWriter`). ⚠ **CORRECTED in pass 2:** only the **view mode** is persisted (`"notation": {"viewMode": …}` JSON via `readViewSettingsJsonFile`/`writeViewSettingsJsonFile`, `notationviewstate.cpp:85-108`) — the zoom/matrix and the style-dialog page indices are **not** written (details §13.6; → `file-format-and-rw.md`).

## 5. The commands layer (dispatch from UI to the facade)

> ⭐ **The desktop UI no longer uses "actions".** ADR **00101** (*Commands: Replacing actions with commands*, 2026-06-12, Accepted, Igor Korsukov): actions are being fully replaced by **commands** that (i) **return a result and can return data** (queries as well as actions), (ii) carry an **input schema** (name/type/min/max per param), (iii) have **no context baked in** — availability is computed in one class, shortcut context moves to the shortcut config, (iv) split **register** (static info) from **state** (enabled/checked) into two services, (v) are **asynchronous**. Motivation: an MCP server (`rcontrol` module) plus internal action debt. Implemented by new framework modules `rcommand` (command type, register, state, dispatcher) and `rcontrol`. Each module supplies: `…commands.h` constants, a `…CommandsRegister`, a `…CommandsState`.
> ADR **00104** (*Command-first*, 2026-08-21): **only a `Command` can be the key** for info/state/shortcut; dynamic things (extensions, workspaces, effects) get **their own command URI** (`command://workspace/select/code`) instead of a query-parameter; **query params carry data only, never identity** (`command://move?x=100&y=200`).
> In this tree only `src\web\…` still references the old `IActionsDispatcher`/`action://` (6 files, by grep count) — the desktop notation path is fully on commands. See `framework-services.md` for `rcommand` internals (not yet deep-passed).

### 5.1 The three notation-side command classes (all in `src\notationscene\`)

| Class | File | Job |
|---|---|---|
| **Command definitions** | `notationcommands.h`, `notationcommandsenums.h` | `inline static const Command X_COMMAND("command://notation/…")` constants (e.g. `CANCEL_COMMAND`, `UNDO_COMMAND` at `notationcommands.h:385-386`) **and** typed commands (`struct XCommand { id, title, description, availabilities, annotations, input/output fields }`). `notationcommandsenums.h` holds enum ⇄ string "contracts" (`NoteNameContract`, `NoteInputMethodContract`, `NoteAddingModeContract`, tuplet contracts, …) so params serialize for API/MCP |
| `NotationCommandsRegister` | `internal\notationcommandsregister.cpp` | static `std::vector<CommandInfo>` (command, translated title, description, `InputSchema`, `Decoration`/icon); registered into `ICommandsRegister` in `NotationSceneModule::resolveImports` (`notationscenemodule.cpp:91-94`) |
| `NotationCommandsState` | `internal\notationcommandsstate.cpp` | computes `CommandState{enabled, checked}`; registered into `ICommandsState` per context in `NotationSceneContext::resolveImports` (`:115-121`) |
| **`NotationActionController`** | `internal\notationactioncontroller.*` | **the handlers.** `init()` registers every handler with the dispatcher |

Wiring (`notationscenemodule.cpp:107-130`): per **context** (window) `NotationSceneContext::registerExports` constructs `NotationActionController` and exports it as `INotationCommandsController` (`MODULE_CONTEXT_INTERFACE`); `onInit` calls `m_actionController->init()` and, in `GuiApp` mode only, a `MidiInputOutputController`.

The controller's injects (`notationactioncontroller.h:51-58`): `INotationConfiguration`, `INotationSceneConfiguration`, `IEngravingConfiguration` (global); `ICommandDispatcher`, `ICommandsState`, `IProjectSession`, `IInteractive`, `IPlaybackController` (per-context). It resolves "the current notation" **every call** via `projectSession()->currentNotation()` → `->interaction()` / `->noteInput()` / `->undoStack()` / `->midiInput()` / `->elements()->msScore()` (`notationactioncontroller.cpp:728-796`). No state is cached — switching to a part tab simply changes what `currentNotation()` returns.

### 5.2 Dispatch mechanics

```cpp
// A typed command with a handler taking the parsed struct  (notationactioncontroller.cpp:2929-2940)
template<muse::rcommand::TypedCommand C>
void NotationActionController::registerCommand(muse::Ret (NotationActionController::* handler)(const C&))
{
    commandDispatcher()->onRequest<C>(this, [this, handler](const C& command) {
        if (!commandsState()->commandState(C::id).enabled) { return muse::make_ret(Ret::Code::NotSupported); }
        return (this->*handler)(command);
    });
}
```

Every `registerCommand`/`registerGetCommand`/`registerCommandWithParams`/`registerViewCommand` overload follows the same pattern (`:2907-3171`): **(1) re-check `commandsState()->commandState(cmd).enabled` and return `Ret::Code::NotSupported` if not; (2) optional extra `enabler` member predicate; (3) run the handler; (4) return a `Ret` (or `Response` with params/output for param/get commands).** I.e. enabled-ness is enforced **at dispatch time too**, not just for greying menus.

| Registration helper | Handler shape | Typical use |
|---|---|---|
| `registerCommand<C>(&Controller::fn)` / `registerCommand<C>(lambda)` | `Ret fn(const C&)` / `Ret()` | typed commands (`SelectTargetCommand`, `AddNoteCommand`, `FlipCommand`, all `Select*`/`AddToSelection*` aliases) |
| `registerCommand(cmd, &Interaction::fn [, param] [, PlayMode] [, enabler])` | **pointer-to-member of `INotationInteraction`**: calls `currentNotation()->interaction().get()->*handler(param)`, then `seekSelectedElement()`, then optionally `playSelectedElement(chord?)` | the ~250 one-liners, e.g. `registerCommand(ADD_SLUR…, &Interaction::…)`, `registerCommand(ADD_TRILL_COMMAND, &Interaction::toggleOrnament, SymId::ornamentTrill)` |
| `registerCommand(cmd, lambda / &Controller::fn [, enabler])` | `void()` | controller-level logic (`resetState`, `toggleNoteInput`, `setDuration`, dialogs `openXxxDialog`) |
| `registerCommandWithParams(cmd, fn)` | `Ret fn(const Params&)` | un-typed param commands (`SCREEN_PUT_NOTE`, `ZOOM_TO_PERCENT`, `INSERT_MEASURES`, `ADD_TUPLET`) |
| `registerGetCommand<C>` / `registerGetCommand(cmd, fn)` | `RetVal<Output> fn() const` | queries (e.g. `GetNoteInputStateCommand` → `{enabled, method}`, `Availability::MCP`, `Annotation::ReadOnly`, `notationcommands.h:360-377`) |
| `registerViewCommand(cmd, &INotationViewController::fn [, arg])` | calls the **view controller set via `setViewController`**; `NotSupported` + `LOGE` if null | zoom, view mode, next/prev page/screen (`:3136-3171`) |
| `registerNoteInputCommand(cmd, method)` | `toggleNoteInput(method)` gated by `toggleNoteInputAllowed` | the 8 `TOGGLE_NOTE_INPUT_*` commands |

Command metadata fields in typed commands: `availabilities` (`Availability::UI`, `API`, `MCP`), `annotations` (`Idempotent`, `ReadOnly`), `Decoration` (icon), and `input`/`output` as `FieldsScheme`/`std::tuple` of `Field{name, &Struct::member, description, Presence::Optional}` — the schema used for API/MCP. The header comment: *"A few typed commands as an example. Eventually, all commands should be made typed"* (`notationcommands.h:40`) — so most commands are still bare `Command("command://notation/…")` constants without schema.

### 5.3 What `init()` registers (`notationactioncontroller.cpp:95-715`) — map by area

| Area (comment in source) | Examples (command → handler) |
|---|---|
| global | `CANCEL_COMMAND`→`resetState`; `UNDO/REDO_COMMAND`→`Interaction::undo/redo` |
| navigation & selection | typed `SelectTargetCommand`→`select`; 25 `Select*Command` aliases (`FirstItem`…`Section`) each = `select(SelectionTarget::X, PlayMode::…)`; `AddToSelectionTargetCommand` + 10 aliases (`NextChord`…`EndScore`); `GET_LOCATION`; `OPEN_SELECTION_OPTIONS` |
| text/lyrics navigation & editing | `EDITTEXT_NEXT_WORD/ELEMENT/BEAT…`, `EDITTEXT_ADVANCE_*` (longa…1/64 via `registerNavigationByFractionCommand`), `EDITLYRIC_*`, `EDITTEXT_TOGGLE_BOLD…SUPERSCRIPT` |
| note input | `TOGGLE_NOTE_INPUT*`, `TOGGLE_INSERT_MODE`, `REALTIME_ADVANCE`, `GetNoteInputStateCommand`, `SET_DURATION_LONGA…1024TH`, `SET_(DOUBLE\|HALVE)_DURATION[_DOTTED]`, `TOGGLE_DOT…DOT4`, `TOGGLE_REST`, `TOGGLE_FLAT2…SHARP2` / `ADD_*` accidentals, `TOGGLE_TIE/LV`, `ADD_SLUR`, articulations, `USE_VOICE_1..4`, `SWAP_VOICE_Xnm`, `AddNoteCommand`, `ADD_DRUM_NOTE`, `ENTER_NOTE_A..G` (next chord), `ADD_NOTE_A..G` (current chord), `INSERT_NOTE_A..G` (`NoteAddingMode::InsertChord`), `ENTER_REST`, tuplets (`ADD_TUPLET`, duplet…nonuplet, `OPEN_TUPLET_CONFIGURE`) |
| frames/measures | `INSERT/APPEND_HBOX/VBOX/TEXTFRAME/FRETFRAME/MEASURE[S]`, `SPLIT_MEASURE`, `JOIN_MEASURES`, `REMOVE_SELECTED_RANGE`, `REMOVE_EMPTY_TRAILING_MEASURES` |
| adding things | ottava, dynamics/hairpins, noteline, bows, ~19 add-text commands (`TextStyleType::…`), grace notes, beams, brackets, ornaments, clefs, intervals ±, fretboard, images, figured bass |
| editing | `COPY`, `COPY_PASTE_SWAP`, `COPY_LYRICS`, `CUT`, `PASTE[_HALF/_DOUBLE/_SPECIAL]`, `DELETE` |
| move | `MOVE_LEFT/RIGHT[_QUICKLY]`, `PITCH_UP/DOWN[_OCTAVE/_DIATONIC/_DIATONIC_ALTERATIONS]`, `MOVE_UP/DOWN` (staff), `SWAP_LEFT/RIGHT`, snap-to-prev/next |
| layout | breaks, system/page/score locks, make-into-system/page, move-measure/system to prev/next, stretch ±/reset |
| dialogs | `OPEN_PAGE_SETTINGS`, `OPEN_STAFF_PROPERTIES`, `OPEN_PARTS`, `OPEN_TRANSPOSE`, `OPEN_EDIT_STYLE` (params), `OPEN_EDITGRIDSIZE`, `OPEN_REALIZECHORDSYMBOLS`, … |
| style/reset/show | `LOAD/SAVE_STYLE`, `TOGGLE_CONCERT_PITCH`, `RESET_*`, `FREEZE_CURRENT_PLACEMENT`, `SHOW_INVISIBLE/UNPRINTABLE/FRAMES/PAGEBORDERS/SOUNDFLAGS/IRREGULAR` |
| staff/pitch/other | explode/implode, slashes, pitch spelling, enharmonic, regroup rhythms, unroll repeats, mirror notehead, voice assignment, automation toggle/select, TAB-only commands (fret `ENTER_FRET_0..14`, TAB durations, guitar bends, hammer-on, `GOTO_STRING_ABOVE/BELOW`) |
| screen | `SCREEN_PUT_NOTE`, `SCREEN_REMOVE_NOTE`, `SCREEN_EDIT_TEXT`, `SCREEN_EDIT_ELEMENT`, `EDIT_ELEMENT` |
| view | zoom in/out/page-width/whole-page/two-pages/100%/percent, view mode page/float/continuous/single, next/prev screen/page, top/bottom, context menu of selection, `SHOW_SEARCH`, piano keyboard key count |
| diagnostic/debug | `DIAGNOSTIC_VIEW_REDRAW`, `CHECK_FOR_SCORE_CORRUPTIONS`, and a loop over `s_debuggingCommands` (12 toggles: bounding rects, shapes, skylines, masks, gap rests, …) that flip fields of `IEngravingConfiguration::DebuggingOptions` (`:75-88, 657-664`) |

**`resetState` = the Escape key ladder** (`:813-856`): stop playback if playing → if in note input, end it and return → if grip edit, `endEditElement()` and return → if text editing, `endEditElement()` and return (else if editing an element, end it and continue) → clear selection → `automation()->resetEditStateRequested()`.

### 5.4 Enabled/checked state — `NotationCommandsState` (`notationcommandsstate.cpp`)

State is **pushed, not polled**: `init()` subscribes to controller signals and recomputes only the affected command groups (`:309-386`):

| Signal (from `INotationCommandsController`) | Re-evaluates |
|---|---|
| `projectChanged` / `interactive()->opened` | all commands |
| `selectionChanged` | `HAS_SELECTION_REQUIRED`, `ADD_COMMANDS`, rest, voice, note-or-rest-selected, tuplet, move-selection, layout-break, TAB |
| `stackChanged` | `UNDO_REDO_COMMANDS` |
| `textEditingChanged(bool)` | text-edit, lyrics-edit, layout-break, not-text-edit |
| `isNoteInputAllowedChanged` | the 8 note-input toggles (allowed = **not playing**, `notationactioncontroller.cpp:898-901`) |
| `noteInputStateChanged` | note-input toggles, durations, dots, rest, accidentals, ties/slurs, articulations, voices, note commands, tuplets, TAB |
| `scoreConfigChanged`, `notationStyleChanged`, `automationModeEnabledChanged`, `debuggingOptionsChanged` | their respective groups |

`doCommandState(cmd)` (`:422-…`) returns `CommandState{enabled, checked}` per group — e.g. undo `enabled = canUndo()`, durations `checked = (currentDurationType()==X)`, note-input toggle `checked = isNoteInputMode() && method==X`, tuplets disabled when `REPITCH` ("Re-pitch must not change durations", `:508-512`), everything `{false,false}` if no project open (`:424-426`). `updateCommandStates` only emits `commandStateChanged` when the new `CommandState` differs (`:405-420`).

The controller's source signals are wired once per **current-notation change**: it re-subscribes to the new notation's `selectionChanged`, `noteInput()->stateChanged`, `textEditingStarted/Ended`, `scoreConfigChanged`, `undoStack->stackChanged`, `style->styleChanged`, `masterNotation->automation()->automationModeEnabledChanged`, using `Asyncable::Mode::SetReplace` so the old notation's connections are replaced (`notationactioncontroller.cpp:667-710`).

### 5.5 End-to-end trace: "press `C` in note-input mode" (note-by-name method; `doAddNote` tail now TRACED, `notationactioncontroller.cpp:1026-1053`)

1. Shortcut → `ICommandDispatcher::dispatch(ENTER_NOTE_C_COMMAND)` (shortcut config now owns context).
2. `registerNoteCommand(ENTER_NOTE_C_COMMAND, NoteName::C)` (`:260`) lambda → `NotationActionController::addNote(NoteName::C, NoteAddingMode::NextChord)` after the dispatcher-level `enabled` check (`NOTE_COMMANDS` → `isNoteInputActionAllowed()`).
3. Controller builds `NoteInputParams` via `NoteInput::resolveNoteInputParams(currentNotationScore(), note, addFlag, params)` (`:998-1009`; `addFlag = (mode == CurrentChord)`; on failure `LOGE` and return), then `doAddNote` (`:1026-1053`, TRACED end-to-end): (a) bail if no current note input; (b) `startNoteInput()` (`:2718-2724`) = `noteInput->startNoteInput(configuration()->defaultNoteInputMethod())`, which is a **no-op when already in note-input mode** (`notationnoteinput.cpp:112-114`) — so the key also *enters* note input if not already; (c) **only** for `NoteAddingMode::NextChord` **and** method `BY_DURATION`: `setRestMode(false)` + `setInputNote(params)`, optional preview via `playbackController()->playNotes(state.notes(), state.staffIdx(), state.segment())` when `isPlayPreviewNotesInInputByDuration()`, then **return without adding a note** (`:1037-1047`); (d) otherwise `noteInput->addNote(params, addingMode)` then `seekAndPlaySelectedElement()` (`:1050-1052`). `addDrumNote` (`:1011-1024`) builds a `NoteInputParams{drumPitch}` and funnels into the same `doAddNote`.
4. `NotationNoteInput::addNote` → `hideShadowNote`; `startEdit("Enter note")` = `undoStack->prepareChanges` → `TransactionManager::beginTransaction`; `NoteInput::addPitch(currentOrDummyTransaction(), score(), …)` (engraving edits the DOM); `apply()` = `commitChanges` → `endTransaction`: layout, build `ScoreChanges`, `changesChannel().send(changes)`.
5. `noteAdded()` → `Notation::notifyAboutNotationChanged()` → `notationChanged` → view repaints. `stateChanged` → `NotationCommandsState` recomputes duration/rest/etc. check states → menus/toolbars update. Playback reacts via `changesChannel` (→ `playback-and-audio.md`).

## 6. Enum / type quick reference (`src\notation\notationtypes.h`, `types\`)

| Type | Values / meaning |
|---|---|
| `DragMode` | `BothXY, OnlyX, OnlyY` |
| `MoveDirection` | `Undefined, Left, Right, Up, Down` |
| `MoveSelectionType` | `Undefined, EngravingItem, Chord, Measure, Track, Frame, System, String` (`String` = TAB) |
| `SelectionTarget` | 35 values: item/segment/chord/measure/track/frame/system next+prev, above/below staff, up/down/top/bottom note in chord, top staff, empty trailing measure, similar (+in staff/range), notes in chord, all, section, begin/end system/score |
| `PlayMode` (unscoped `enum`) | `NoPlay, PlayNote, PlayChord` |
| `NoteAddingMode` | `CurrentChord, NextChord, InsertChord` |
| `NoteName` | `C..B` (0..6) |
| `BoxType` / `AddBoxesTarget` | `Vertical/Horizontal/Measure/Text/Fret` / `AfterSelection, BeforeSelection, AtStartOfScore, AtEndOfScore` |
| `PastingType` | `Default, Half, Double, Special` |
| `ScoreConfigType` | `ShowInvisibleElements, ShowUnprintableElements, ShowFrames, ShowPageMargins, ShowSoundFlags, MarkIrregularMeasures` |
| `TupletOptions` | `ratio, numberType, bracketType, autoBaseLen, selectedDurationAs` (`types\noteinputtypes.h:47-57`) |
| `FilterElementsOptions` / `FilterNotesOptions` | element-search filters (notes add pitch/string/tpc/notehead/duration/noteType) |
| Aliases | `NoteInputState = engraving::InputState`, `NoteInputMethod = engraving::NoteEntryMethod`, `PitchMode = engraving::UpDownMode`, `SelectType = engraving::SelectType`, plus ~40 `using` aliases of engraving types (`Note`, `Chord`, `Measure`, `Fraction`, …) so UI code needn't include engraving headers |

## 7. The interaction / editing state machine (`NotationInteraction` + `engraving::EditData`) — pass 2

> Everything below is TRACED from `src\notation\internal\notationinteraction.cpp` (line numbers are that file unless stated) and `src\engraving\editing\editdata.h`. The task brief used the old ScoreView names `dragMeasureAnchorElement` / `dropTextBaseElement` / `drop`; in this tree they are `prepareDropMeasureBaseAnchorElement` (comment: *"Copied from ScoreView::dragMeasureAnchorElement"*, `:3143`), `doDropTextBaseAndSymbols` (`:2032`) and `dropSingle` (`:1837`). Much of the file is literally copied from the legacy `ScoreView` (look for `//! NOTE Copied from ScoreView::…`).

### 7.1 `EditData` (`engraving\editing\editdata.h:232-283`)

`EditData` is the **one mutable scratch-struct** that engraving elements receive in every drag/edit/drop callback (`startEdit/edit/endEdit`, `startDrag/drag/endDrag`, `startDragGrip/dragGrip/endDragGrip`, `acceptDrop/drop`, `updateGrips`). It belongs to engraving but is *owned and populated* by the notation layer. `NotationInteraction` holds **three** instances of it: `m_editData` (element/text/grip edit), `m_dragData.ed` (a drag in progress), and `m_dropData.elementDropData->ed` (a drop in progress). All are constructed with `&m_scoreCallbacks` as the `MuseScoreView*` (`:306`, `:1408`, `:1497`).

| Field | Meaning |
|---|---|
| `element` | **the item being edited** (`nullptr` = not editing). This single pointer *is* the state-machine "mode" (see 7.2) |
| `dropElement`, `dragOffset`, `track` | drop-time data: the item being dropped, hot-spot offset read from the MIME payload, target track (`muse::nidx` = none) |
| `grip` (`vector<RectF>`), `grips`, `curGrip` | grip hit rectangles (canvas coords), count, and **current grip** (`Grip::NO_GRIP = -1` ⇒ no grip edit) |
| `pos`, `startMove`, `normalizedStartMove`, `startMovePixel`, `lastPos` | pointer positions (`startMove` for text = cursor-bound click point) |
| `delta` (deprecated), `evtDelta`, `moveDelta` | per-event offset / offset since drag start; `delta` equals `moveDelta` in normal drag and `evtDelta` in grip drag (header comments) |
| `hRaster`, `vRaster` | snap-to-grid flags (set from `configuration()->isSnappedToGrid(...)` in `drag`, `:1177-1178`) |
| `key`, `s`, `modifiers` (`KeyboardModifiers`), `buttons` | keyboard event data (`editElement` copies `event->key()/text()/modifiers()` in, `:4730-4737`) |
| `preeditString` | IME pre-edit text currently inserted in a `TextBase` (§9) |
| `isEditMode` | "refers to the legacy concept of edit mode" — set to `isEditingElement()` in `drag` (`:1185`) |
| `isHairpinDragCreatedFromDynamic` | tells `endDrag` to merge the two undo steps (hairpin created by dragging a dynamic's grip, `:1262-1269`, `:1202-1208`) |
| private `m_data` (`ElementEditData` list) | per-element opaque edit state keyed by element: `getData(e)` / `addData(sp)`; `clear()` = `*this = EditData(m_view)` (full reset incl. the data list) |

`Grip` enum (`editdata.h:212-220`): `NO_GRIP=-1, START=0, END=1, LEFT=START, RIGHT=END, MIDDLE=2, APERTURE=3, BEZIER1=2, SHOULDER=3, BEZIER2=4, DRAG=5, GRIPS=6` — note the **aliases overlap** (e.g. `MIDDLE == BEZIER1 == 2`), so the integer is only meaningful relative to the element type. `EditData::isStartEndGrip()` = `START||END`.

### 7.2 The modes (derived, not stored)

There is **no `enum Mode`**. The current mode is *computed* from three members by predicates:

| Mode | Predicate (source) |
|---|---|
| Idle | `m_editData.element == nullptr` (`isEditingElement()` is `element != nullptr`, `:4633`) |
| Element edit (no grip, not text) | `element != nullptr && curGrip == NO_GRIP && !isTextEditingStarted()` |
| **Grip edit** | `isGripEditStarted()` = `element && curGrip != NO_GRIP` (`:4533`) |
| **Text edit** | `isTextEditingStarted()` = `element->isTextBase() && toTextBase(element)->cursor() && cursor->editing()` (`:4120`) |
| Dragging (in-score) | `isDragStarted()` = `m_dragData.dragGroups.size() > 0 || (m_lasso && !m_lasso->isEmpty())` (`:1053`). ⭐ a **grip drag** is not reliably reflected here: `startDrag` returns before calling `group->startDrag` on the grip path, and groups are only present if the caller passed draggable `elems` — so grip drags are detected through `isGripEditStarted()` (checked first in `drag`/`doEndDrag`) |
| Outgoing OS drag | `isOutgoingDragStarted()` = `m_outgoingDrag != nullptr` (`:1388`) |
| Dropping | `m_dropData.elementDropData.has_value()` (single element/image) or `.rangeDropData.has_value()` (range) |
| Note input | owned by `NotationNoteInput`, orthogonal (§4.5) |

### 7.3 Transitions

```text
                   select(elem)  /  startEditElement(e)
 Idle ────────────────────────────────────────────────────────────────────────────┐
  ▲   e->isTextBase()        → startEditText(e)          ── Text edit             │
  │   e->hasGrips && !Image  → startEditGrip(e,default)  ── Grip edit             │
  │   else                   → e->startEdit(ed); ed.element=e ── Element edit     ▼
  │
  │   endEditElement():  text? → endEditText() (return)
  │                      dragStarted? → doEndDrag(); rollback()
  │                      doEndEditElement() [= element->endEdit(ed); ed.clear()]; resetAnchorLines()
  └── also: clearSelection(), select() when needEndTextEditing/needEndElementEditing, undoRedoToIndex()
```

- `startEditElement(e)` (`:4638-4658`): rejects null / `!isEditable()` / **already editing something** (`isEditingElement()` → return — you must `endEditElement()` or `changeEditElement()` first). Then the three-way dispatch above and `m_isEditingElementChanged.notify()`.
- `changeEditElement(newElement)` (`:4660-4680`): no-op if same element; remembers `curGrip` + whether grip-editing, `doEndEditElement()`, then re-enters as `startEditGrip(new, sameGrip)` or `startEditElement(new)`. Reached from engraving code through `ScoreCallbacks::changeEditElement` (`scorecallbacks.cpp:65-72`, a straight forward to the interaction) when an element's own `edit`/`nextGrip` logic wants to hand edit focus to a related element (which elements do so was not enumerated).
- `select(...)` guards: before calling `m_selection->select`, `needEndTextEditing(newSelected)` (`:6695`) / `needEndElementEditing(newSelected)` (`:6719-6737`) decide whether to end the current edit. **Special case:** a `SoundFlag` whose `ownershipParent()` is the `StaffText` currently being edited does **not** end editing (`:6709-6714`, `:6729-6734`) — selecting the sound flag inside a staff-text edit keeps the edit alive.
- `onElementDestroyed(el)` (`:4854`, subscribed to `score()->elementDestroyed()` in `onScoreInited`, `:338`): nulls `m_editData.element` and `m_hitElementContext` if they point to the dying item (so layout deleting a bracket etc. cannot leave a dangling edit pointer).
- `selectAndStartEditIfNeeded(e)` (`:997-1008`): spanners are replaced by their `frontSegment()`, then `select({e}, SINGLE)`, then `startEditElement(e)` iff `e->needStartEditingAfterSelecting()`. This is what drop / palette / slur-creation call so a freshly created hairpin/slur/text immediately enters grip/text edit.
- `onViewModeChanged` (`:343-364`): switching to LINE or HORIZONTAL_FIXED deselects items inside VBox/FBox and ends the edit if that item was being edited. ⚠ **Probable bug (TRACED code, HYPOTHESIS on intent):** the skip condition is `if (!item->findAncestor(VBOX) || !item->findAncestor(FBOX)) { continue; }` (`:356`). The item is deselected only if it has **both** a VBox and an FBox ancestor — practically never true — so as written the loop effectively deselects nothing. `&&` (skip items that are in neither box type; or an `||` of positives) was probably intended.

### 7.4 The edit-callback bracket (what elements see)

| Phase | Notation call | Element callback(s) |
|---|---|---|
| begin text/element edit | `startEditText` / `startEditElement` / `startEditGrip` | `element->startEdit(m_editData)` |
| keystroke | `editElement(QKeyEvent*)` → `handleKeyPress` | `element->edit(ed)` (text: `TextBase::edit`), else arrow-nudge via `startDragGrip/dragGrip/endDragGrip` or `startDrag/drag/endDrag` |
| mouse drag of grip | `startDrag` (grip branch) / `drag` / `doEndDrag` | `startDragGrip` / `dragGrip` / `endDragGrip` + `endEdit` |
| mouse drag of items | `startDrag` / `drag` / `doEndDrag` | per-`ElementGroup`: `group->startDrag(ed)` / `group->drag(ed)` (returns the refresh rect) / `group->endDrag(ed)` (groups come from `e->getDragGroup(isDraggable)`) |
| end | `doEndEditElement` | `element->endEdit(ed)` then `ed.clear()` |

## 8. Drag, outgoing drag, lasso

### 8.1 In-score drag — `startDrag` / `drag` / `endDrag` (`:1074-1273`)

1. **`startDrag(elems, eoffset, isDraggable)`**: `m_dragData.reset()`; stores `elements`/`elementOffset`; for each element asks the caller-supplied `IsDraggable` predicate (spanners are draggable if their front segment is); builds an `ElementGroup` via `e->getDragGroup(isDraggable)` and keeps it if `g->enabled()`. Then **`startEdit("Drag element(s)")`** (plural-aware) — the whole drag is **one open transaction** — sets `m_scoreCallbacks` selection proximity from `configuration()->selectionProximity()*0.5/scaling`; if `isGripEditStarted()` it calls `m_editData.element->startDragGrip(m_editData)` and returns; else `group->startDrag(m_dragData.ed)` for each group.
2. **`drag(fromPos, toPos, mode)`**: first call latches `beginMove`. Computes `delta`, `evtDelta`, `moveDelta`; `DragMode::OnlyX/OnlyY` zero the other axis — **except** when grip-editing a `BarLine` (`constrainDirection` false, `:1157`). Fills `m_dragData.ed` (rasters, modifiers via `keyboardModifier(QGuiApplication::keyboardModifiers())`, `isEditMode`). Three branches:
   - text editing → `toTextBase(element)->dragTo(m_editData)` (text-cursor selection by mouse), `notifyAboutTextEditingChanged`, return;
   - grip edit → `element->dragGrip(ed)`; if it is a `Dynamic`, `addHairpinOnGripDrag(...)` (creates a hairpin when you drag the dynamic's grip, tracked by `isHairpinDragCreatedFromDynamic`);
   - otherwise each group's `drag(ed)` returns a rect → `score()->addRefresh`.
   Then `score()->update()`, anchor lines (`updateGripAnchorLines()` vs `updateDragAnchorLines()` which collects `e->dragAnchorLines()` over the selection), and if `m_dragData.elements` is empty it is a **rubber-band** → `doDragLasso(toPos)` (`:1118-1131`: lazily `new Lasso`, `score()->lassoSelect(bbox)`). Finally `notifyAboutDragChanged()`.
3. **`endDrag()`** (`:1257`): `doEndDrag()` (grip: `endDragGrip`+`endEdit`; else group `endDrag`s + `endLasso` if the lasso is non-empty; `m_dragData.reset()`; clear drop target / anchor lines) → **`apply()`** → if `isHairpinDragCreatedFromDynamic`, `mergeTransactions(currentStateIndex()-2)` (asserted ≥ 2) → `notifyAboutDragChanged()` → `checkAndShowError()`.
4. **Cancel**: `clearSelection()` / `endEditElement()` during a drag do `doEndDrag(); rollback();` (`:1023-1027`, `:4798-4801`), discarding the open transaction.

### 8.2 Outgoing (OS-level) drag (`:1280-1399`)

`isOutgoingDragElementAllowed(e)` forbids Measure, Note, V/H/T/F-boxes, guitar-bend family and NoteLine (no single-layout/draw support yet). `startOutgoingDragElement(element, dragSource, hotSpot)` ends any previous drag, builds a `QMimeData` with `mimeSymbolFormat` = `element->mimeData()` (a spanner segment sends its *spanner's* mime data), a `QDrag`, renders a pixmap by `engravingRenderer()->drawItem` scaled by `physicalDotsPerInch/DPI` and DPR, and **blocks in `QDrag::exec(CopyAction)`**. `startOutgoingDragRange` requires `selection()->isRange() && canCopy()`, uses `selection()->qMimeData()` with a 1×1 transparent pixmap and `exec(Move|Copy)`. `endOutgoingDrag()` deletes the `QDrag`. These produce the same MIME that `startDropSingle`/`startDropRange` consume (also from palettes and other windows).

## 9. Drop handling (`startDrop*` → `updateDrop*` → `dropSingle` / `dropRange`)

State: `m_dropData` = `std::optional<ElementDropData>` **or** `std::optional<RangeDropData>` (`notationinteraction.h:541-545`, mutually exclusive — each `startDrop*` begins with `resetDropData()`).

```cpp
struct ElementDropData { EditData ed; EngravingItem* dropTarget; std::vector<RectF> dropRects; };
struct RangeDropData   { Fraction sourceTick, tickLength, timeStretch; staff_idx_t sourceStaffIdx; size_t numStaves;
                         bool preserveMeasureAlignment; Segment* targetSegment; staff_idx_t targetStaffIdx; std::vector<RectF> dropRects; };
```

Plus `m_droppableTree` (`engraving::BspTree`) and `m_currentDropPage` as a spatial cache (see 9.3).

### 9.1 Start

| Method | What it does |
|---|---|
| `startDropSingle(QByteArray)` (`:1402`) | `resetDropData`; new `ElementDropData` with `EditData(&m_scoreCallbacks)`; `XmlReader` → `EngravingItem::readType(e, &dragOffset, &duration)` (reads the MIME header) → `Factory::createItem(type, score()->dummy())`; BAR_LINE/BRACKET get `setHeight(5*spatium)`; `RWRegister::reader()->readItem(...)`; `engravingRenderer()->layoutItem(...)` so it has geometry. Returns false (+`resetDropData`) if the type is unknown |
| `startDropRange(QByteArray)` (`:1433`) | parses the `<StaffList tick len timeStretch staff staves>` header only; rejects zero `tickLength` or `numStaves==0` |
| `startDropRange(sourceTick, tickLength, sourceStaffIdx, numStaves, preserveMeasureAlignment)` (`:1460`) | programmatic form (in-app range drag); takes `timeStretch` from `score()->staff(srcStaff)->timeStretch(srcTick)` |
| `startDropImage(QUrl)` (`:1482`) | only `file://`; `Image::loadFromFile`; same `ElementDropData` shape; `setOwnershipParent(nullptr)` |

### 9.2 Update — pick the target (`updateDropSingle`, `:1509-1620`)

`updateDropSingle(pos, modifiers)` is a **big type switch on `dropElem->type()`** that routes to one of **three resolvers**:

| Resolver | Used for | Mechanism |
|---|---|---|
| **`prepareDropMeasureBaseAnchorElement(pos)`** (`:3144`) — "measure-anchored" | BRACKET, GRADUAL_TEMPO_CHANGE, JUMP, KEYSIG, LAYOUT_BREAK, MARKER, MEASURE_REPEAT, MEASURE_NUMBER, TIMESIG, SPACER, STAFFTYPE_CHANGE, STRING_TUNINGS, VOLTA, and ACTION_ICON of type VFRAME/HFRAME/TFRAME/FFRAME/MEASURE/SYSTEM_LOCK/PAGE_LOCK/STAFF_TYPE_CHANGE | `score()->pos2measureBase(pos, scanMeasuresOnly, &staffIdx, …)`. `scanMeasuresOnly = !dropElem->isLayoutBreak()` (for action icons: only PAGE_LOCK/SYSTEM_LOCK may target non-measure bases). Then `staffIdx` is forced to **0 (= "all staves")** for VOLTA/GRADUAL_TEMPO/KEYSIG/TIMESIG *unless Ctrl is held* (Ctrl ⇒ target that one staff), and always for JUMP/LAYOUT_BREAK/MARKER/MEASURE_NUMBER/boxes and all action icons except STAFF_TYPE_CHANGE. Calls `setDropTarget(mb,true)`, sets `ed.track`, `ed.pos` = rect centre, `mb->acceptDrop(ed)`; on accept sets anchor line from the pointer to the rect top-left and `setDropRects(dropHighlightRects(...))`; else `setDropTarget(nullptr)` |
| **`prepareDropTimeAnchorElement(pos)`** (`:3335`) — "time-anchored spanners" | PEDAL, LET_RING, VIBRATO, PALM_MUTE, OTTAVA, TRILL, HAIRPIN, TEXTLINE, WHAMMY_BAR | `score()->pos2measure(pos, &staffIdx, 0, &seg, 0)`; accepts iff `seg->element(track)` exists; anchor line to `(seg x, staff y)`; sets `dropElement->setTrack(track)` (no `dropTarget` is stored — `dropSingle` re-resolves by position) |
| **`prepareDropStandardElement(pos, modifiers)`** (`:3074`) — everything else (~60 types: notes/chords/articulations/dynamics/text/clef/barline/lyrics/harmony/fret diagram/slur/…) | | **Spatial index:** `point2page(pos)`; when starting a drag (`ed.pos.isNull()`), when modifiers changed, or when crossing a page, rebuilds `m_droppableTree` (`BspTree::initialize(pageRect, elemCount)`) by inserting every `page->elements()` entry for which `elem->acceptDrop(ed)` is true — documented as O(m log n); then `nearestNeighbor(posInPage)` is the target. **Clef special case** (`:3113-3133`): if the dragged item is a CLEF and the nearest target sits at `rtick()==0`, retarget the whole **measure** (`staffPageBoundingRect`), `ed.track = trackZeroVoice(...)`, and require `measure->acceptDrop`. Otherwise `setDropTarget(target,true)`, `ed.pos` = target centre, anchor line, highlight rects |

`dropHighlightRects(dropElem, targetElem, modifiers)` (`:3227-3332`) maps drop type → what to tint: whole measure-base rect (boxes, MEASURE_NUMBER, JUMP, MARKER, LAYOUT_BREAK; and VOLTA/GRADUAL_TEMPO/KEYSIG/TIMESIG without Ctrl), a single staff rect (Ctrl variants, BRACKET, MEASURE_REPEAT, SPACER, IMAGE, BAR_LINE, SYMBOL, CLEF, STAFFTYPE_CHANGE, STRING_TUNINGS), and for action icons: frames/MEASURE → measure base; SYSTEM_LOCK → from system's first MB to the target; PAGE_LOCK → one rect per system up to the target system; STAVE_SHARING_CHANGE/RESET → target to system end on that staff. `setDropRects` only repaints when the rect list changed.

**Range:** `updateDropRange(pos, preserveAlignment)` → `dropRangePosition(...)` (`:1651-1764`, static helper): `dragPositionToMeasure` → target start measure+staff; computes target start/end ticks (with `preserveMeasureAlignment` it keeps the source's intra-measure offset, falling back to non-preserving if the offset overflows a shorter target measure); **creates time-tick anchors** across the touched measures/staves (`EditTimeTickAnchors::updateAnchors`) so a segment exists at every drop point; picks `targetStartSegment` via `dragPositionToSegment(... spacingFactor 0.5, useTimeAnchors)`; **fails if any destination measure's `staff->timeStretch(tick)` differs from the source's**; fills `ShowAnchors` (stored on the score and followed by `Page::invalidateBspTree()` for affected pages, `:1788-1813`) and `rdd.dropRects = ScoreRangeUtilities::boundingArea(...)`; `notifyAboutDragChanged()`.

### 9.3 Commit — `dropSingle` (`:1837-1991`)

`edd.ed.pos` falls back to the mouse pos if the resolvers didn't set it; `ed.modifiers`; `dropElement->styleChanged()` (adopt local style). **`startEdit("Drop element: <typeUserName>")`** then dispatch on `dropElement->type()`:

| Group | Handler |
|---|---|
| VOLTA, GRADUAL_TEMPO_CHANGE (⇒ `systemStavesOnly = !Ctrl`, fall through), TEXTLINE, OTTAVA, TRILL, PEDAL, LET_RING, VIBRATO, PALM_MUTE, HAIRPIN, WHAMMY_BAR | `EditSpanner::addSpanner(tx, score(), spanner, pos, systemStavesOnly)` (system-flag spanners are forced system-staves-only) |
| SYMBOL, FSYMBOL, IMAGE (`applyUserOffset = true`, fall through), DYNAMIC, HARMONY, FRET_DIAGRAM | `doDropTextBaseAndSymbols(tx, pos, applyUserOffset)` (`:2032`): target = stored `dropTarget` else `elementAt(pos)`; if none or `StaffLines` → `pos2measure(pos,…)` and add the item directly to that segment/track (`undoAddElement`, `setOffset(offset)` when `applyUserOffset`); else `acceptDrop`+`drop(tx, ed)` and `selectAndStartEditIfNeeded`. FRET_DIAGRAM then runs `doFinishAddFretboardDiagram()` (`:3445`: commits, then a **second** transaction "Add chord symbol to fretboard diagram" adds a `HARMONY_A` text and starts editing it — separate so cancelling empty text rolls back only the text) |
| ~55 types (boxes, KEYSIG/CLEF/TIMESIG, BAR_LINE, ARPEGGIO, BREATH, GLISSANDO, BRACKET, ARTICULATION, TAPPING, FERMATA, CHORDLINE, BEND, ACCIDENTAL, TEXT, FINGERING, TEMPO_TEXT, ORNAMENT, EXPRESSION, STAFF_TEXT, SYSTEM_TEXT, LAYOUT_BREAK, MARKER, JUMP, MEASURE_REPEAT, ACTION_ICON, NOTE, CHORD, SPACER, FIGURED_BASS, LYRICS, HARP_DIAGRAM, STAFFTYPE_CHANGE, …) | `doDropStandard(tx)` (`:1994`): `el = edd.dropTarget ?: dropTarget(edd.ed)`; no target → `dropCanvas` (action icons VFRAME/HFRAME/TFRAME/FFRAME/MEASURE → `score()->insertBox`/`insertMeasure`) and return **false**; else `el->drop(tx, ed)`; if the result is an `InstrumentChange` → `selectInstrument(...)` (**nested `QEventLoop`** over the asynchronous instrument-selection scenario, `:2150-2199`; on cancel `rollback()` and return true); if not in note-entry mode `selectAndStartEditIfNeeded(result)` |
| SLUR, HAMMER_ON_PULL_OFF | `doAddSlur(note->chord(), nullptr, slur)` on the resolved note |
| default | `resetDropData()` |

Epilogue: clear `ed.dropElement/pos/modifiers`; `setDropTarget(nullptr)`; **`apply()`**; if accepted `notifyAboutDropChanged()`; `checkAndShowError()`. `dropTarget(ed)` (`:3055`) = first of `elementsAt(ed.pos)` that `acceptDrop(ed)`, falling back to a `StaffLines`' measure.

⚠ `dropSingle` calls `startEdit` **before** knowing the drop will be accepted and always ends with `apply()` — a rejected drop still opens/commits a (empty) transaction; empty transactions are not recorded (`endTransaction(isEmpty)` in §2.4) so this is harmless but wasteful.

### 9.4 Commit — `dropRange` (`:2079-2148`)

Re-resolves position with `dropRangePosition`; **rejects drops landing inside a tuplet** (`isTupletSubdivisionOnStaff`/`isInsideTupletOnStaff` → `endDrop` and return false; the source comment says the error popup is intentionally *not* shown because it swallows the mouse-release). `startEdit("Move range"|"Copy range")`; for a move, `score()->deleteRange(sourceStart, sourceEnd, srcTracks, selectionFilter, rangeContainsMultiNoteChords)`; then `Paste::pasteStaff(tx, score(), XmlReader(data), segment, staffIdx)`; `endDrop()`; `apply()` on success, **`rollback()`** otherwise (so a failed paste also undoes the delete); `checkAndShowError()`.

### 9.5 Teardown

`endDrop()` (`:3010`): `score()->hideAnchors(); setUpdateAll(); setDropTarget(nullptr,false); resetDropData(); update(); notifyAboutDragChanged()`. `resetDropData()` **deletes** `elementDropData->ed.dropElement` (so `dropSingle` nulls `ed.dropElement` after handing ownership to the score, `:1977`) then `m_dropData = {}`. `setDropTarget(item, notify)` (`:3368`) toggles `EngravingItem::setDropTarget(bool)` highlight flags on old/new, mirrors the new target's track into `ed.track`, clears anchor lines + rects.

## 10. Palette application (`applyPaletteElement`, `:2227-2709`)

Double-click/apply of a palette cell. Takes a **copy** of the selection (`const Selection sel = score->selection()`), returns false if none, `startEdit("Apply palette element: <name>")`, then `applyPaletteElementToList` (list/single selection, `:2264`) **or** `applyPaletteElementToRange` (`:2451`), sets `m_notifyAboutDropChanged = true`, `score->setSelectionChanged(true)`, `apply()`, `setDropTarget(nullptr)`, `checkAndShowError()`.

`applyPaletteElementToList` special cases, in order: drum-staff `CHORD` entry (uses `InputState`, `expandVoice`, `setPlayNote(false)`); `LayoutBreak` → `cmdToggleLayoutBreak`; action icons SYSTEM_LOCK/PAGE_LOCK/PARENTHESES (`EditParentheses::addParenthesesToNotes`); `Slur` → `doAddSlur(template)`; single-chord-rest/two-chord-rest **SLine** (hairpin → `EditHairpin::addHairpin` then `select`+`startEditElement(frontSegment)`; others `EditSpanner::addSpanner`; Pedal ends at next CR, others at `nextSegmentAfterCR(ChordRest|EndBarLine|Clef)`; "system text line" goes on staff 0 unless Ctrl); articulation-on-articulation retargets the chord; note-to-note line/bend/note-line (`applyLineNoteToNote`); clef/timesig/keysig in note-entry mode go before the cursor; fallback `filterTargetElements(sel, element, unique)` → `applyDropPaletteElement` per target.

`applyDropPaletteElement(score, target, e, modifiers, track)` (`:2712`) is the **"same code path as drag&drop"** primitive: new `EditData(&m_scoreCallbacks)` (or `m_editData` itself while text-editing — so palette symbols insert into the text being edited), sets `dropElement/track/pos`, `target->acceptDrop(*dropData)`, then **round-trips the palette element through its MIME bytes** (`e->mimeData()` → `readType` → `Factory::createItem` → `readItem`, `styleChanged()`) to obtain a fresh independent copy, calls `target->drop(tx, ed)`, handles `InstrumentChange` (`selectInstrument`), voice-assignment inheritance (`setInitialTrackAndVoiceAssignment`, Ctrl = whole staff), `selectAndStartEditIfNeeded`, `doFinishAddFretboardDiagram` if exactly one element is selected, `m_notifyAboutDropChanged = true`.

`doAddSlur(template)` (`:2819`) opens its own `startEdit("Add slur")`/`apply()` and handles range (per-track first/last chord), single (note's chord / hammer-on-pull-off over an existing slur), and list selections incl. **partial slurs** from/to a barline across repeat sections (`undoSetOutgoing/Incoming`; `segmentsAreInDifferentRepeatSegments`/`isValidBarLineForRepeatSection`); in note-input mode it hands the slur to `m_noteInput->addSlur`, else selects the new segment and `startEditGrip(segment, Grip::END)`.

## 11. Text editing (and lyrics)

### 11.1 Text-edit lifecycle (`:4106-4500`, `:8252-8330`)

| Method | Behaviour |
|---|---|
| `isTextSelected()` | selected element `isTextBase()` |
| `textEditingAllowed(e)` | `e->isEditable() && (isTextBase() || isTBox())` |
| `startEditText(EngravingItem*, cursorPos)` (`:4131`) | TBox → its inner text; select it if not already; `m_editData.clear()`; `element`, `startMove = bindCursorPosToText(cursorPos, element)`; `element->startEdit(ed)`; `startTextCursorBlinkTimer()`; notify `isEditingElementChanged`, `textEditingStarted`, `textEditingChanged` |
| `startEditText(TextBase*)` (`:8252`) | `doEndEditElement()`, select, then **mm-rest retarget**: if the text's measure `hasMMRest()` and the text has linked copies, edit the linked copy that lives on the mm-rest measure; then `startEditText(text, PointF())` + `cursor()->moveCursorToEnd()` |
| `editText(QInputMethodEvent*)` (`:4158`) | IME entry (copied from `TextBase::inputTransition`): erases the previous pre-edit characters (`cursor->movePosition(Left)` + `curLine().remove`), then either **commit** → `score()->startCmd("Edit text")`/`text->insertText`/`endCmd()`, or **pre-edit** → `editInsertText` (un-undoable provisional text) + `layoutText1` |
| `editElement(QKeyEvent*)` → `handleKeyPress` → `doTextEdit` | `doTextEdit` (`:4304`): `startEdit("Keystroke edit")`, `tb->edit(ed)`; false ⇒ `rollback()` and fall through to non-text handling; true ⇒ `apply()`. **Smart quotes:** if the keystroke was `'`/`"` it opens a *second* "Keystroke edit" transaction that removes the straight quote (`RemoveText`) and inserts the localized curly one (`muse::mtrc("notation", u"‘")` …), choosing open vs close by whether the previous char is non-space — so smart-quoting is a separate undo step |
| `changeTextCursorPosition(pos)` | `startMove` rebound; `textEl->mousePress(ed)`; middle-button pastes the X clipboard |
| `selectText(SelectTextType)` / `selectAllText()` | `text->select(type)` + `endHexState()` + `setPrimed(false)` |
| `endEditText()` (`:4363`) | stop blink timer; `doEndEditElement()`; notify `isEditingElementChanged`, `textEditingEnded(TextBase*)` (carries the edited item), `textEditingChanged`; `m_selection->notifyAboutSelectionChangedIfNeed()` |
| `needEndTextEdit()` | text editing "started" but the cursor is no longer `editing()` |

Cursor blink: a `QTimer m_textCursorBlinkTimer` connected in the ctor (`:318`) to `blinkTextCursor()` (toggle cursor visibility + `notifyAboutNotationChanged(cursorCanvasRect)` for a minimal repaint). `startTextCursorBlinkTimer()` period = `QApplication::cursorFlashTime()/2` (0 ⇒ blinking disabled ⇒ stop). `onTextEditingChanged → updateTextCursorVisibility`: cursor forced visible and blink restarted when there is no selection; blinking stopped while text is selected (`:4422-4452`).

Formatting toggles: `toggleBold/Italic/Underline/Strike` → `toggleFontStyle(style)` (XORs `Pid::FONT_STYLE`, `UNSTYLED`), `toggleSub/SuperScript` → `toggleVerticalAlignment` (toggles `Pid::TEXT_SCRIPT_ALIGN` vs `AlignNormal`), each in `score()->startCmd("Format text"|"Toggle subscript"…)`/`endCmd()` then `notifyAboutTextEditingChanged`.

⚠ **Two undo APIs coexist in text/lyrics code.** Most of `NotationInteraction` uses `m_undoStack->prepareChanges/commitChanges` (via `startEdit/apply`) or `transaction(...)`, but `editText`, `toggleFontStyle`, `toggleVerticalAlignment`, `navigateToLyrics`, `navigateToNextSyllable` and `navigateToLyricsVerse` call **`score()->startCmd(...)`/`endCmd()` directly**, bypassing `NotationUndoStack` — so `NotationUndoStack::stackChanged` is **not** fired by their commit (only the engraving-side `changesChannel` is). HYPOTHESIS (not verified end-to-end) that `Score::startCmd/endCmd` are thin shims onto the same `TransactionManager`; the observable consequence — command-state refresh after a lyrics tab key relies on `textEditingChanged`/selection signals instead — is TRACED.

### 11.2 Lyrics navigation (`:6753-7240`, copied from `ScoreView::lyricsTab/lyricsUpDown`)

All three require `m_editData.element->isLyrics()` (else `LOGW` + return), and all follow: **find the next target → `endEditText()` (commits the current syllable) → mutate syllabic/melisma flags under `startCmd/endCmd` → `score()->select(newLyrics)`, `setLayoutAll()` → `startEditText(newLyrics, PointF())` → select-all or cursor → `showItem(newLyrics)`** (scroll into view).

| Method | Keys (via commands `EDITLYRIC_*`) | Logic |
|---|---|---|
| `navigateToLyrics(back, moveOnly, end)` (`:6754`) | Space / Shift+Space (prev) | Scans `ChordRest` segments on the same **track** for the next chord (or a rest that already has a verse-matching lyric). Captures the current lyric's verse, placement, font style, centering, text style, Y-offset and **clones those onto a newly created `Lyrics`** if the target has none. When moving *forward* and not `moveOnly`, fixes **syllabic**: `nextLyrics` `END→SINGLE`, `MIDDLE→BEGIN`; `fromLyrics` `BEGIN→SINGLE`, `MIDDLE→END`; and zeroes the melisma `LYRIC_TICKS` of `fromLyrics` unless it ends a melisma. `end` ⇒ `selectAll()`; reused (not new) lyric ⇒ cursor End→Start(KeepAnchor) (selects its text) |
| `navigateToNextSyllable()` (`:6913`) | `-` (dash) | Looks across **all 4 voices of the staff** for the next chord that has a verse-matching lyric; requires the next segment to be *adjacent* (`segmentsAreInDifferentRepeatSegments`, `segmentsAreAdjacent`). Repeat-aware: with a following jump item it can leave a dash-with-no-end-syllable (`LYRIC_TICKS=0`, syllabic BEGIN/MIDDLE); with a preceding repeat and no from-lyric it creates an **incoming `PartialLyricsLine`** (dash) plus an END lyric on the *same* chord (cursor does not advance); disallows dashes between non-adjacent repeat sections (e.g. 1st→2nd volta) by dropping `fromLyrics`. New lyric defaults to syllabic `END`; existing `BEGIN→MIDDLE`, `SINGLE→END`; `fromLyrics` `SINGLE→BEGIN`, `END→MIDDLE`, melisma cleared; existing partial dash end moved via `undoMoveEnd` |
| `navigateToLyricsVerse(direction)` (`:7180`) | Up/Down | `Up`: verse−1 (stops at 0); `Down`: verse+1 (stops past `cr->lastVerse(placement)`); end edit, reuse or create the lyric for the new verse on the same chord-rest, edit it |

⚠ **Potential bug (HYPOTHESIS, TRACED control flow):** in `navigateToNextSyllable`, `score()->startCmd("Navigate to next syllable")` is issued at `:7049` and the very next lines `if (!toLyrics && !cr) { return; }` (`:7053-7055`) return **without `endCmd()`**. If reachable, it leaves an open command. Likewise the no-`nextSegment` + `fromLyrics && hasFollowingRepeat` branch correctly ends the cmd, but the `!nextSegment` + no-repeat early return at `:6960-6962` happens before any `startCmd` (safe).

### 11.3 Other text/annotation navigation (headers `inotationinteraction.h`, impl `:7241-7600`)

`navigateToNearHarmony(direction, nearNoteOrRest)`, `navigateToHarmonyInNearMeasure`, `navigateToHarmony(Fraction ticks)`, the figured-bass trio and `navigateToNearText(direction)` follow the same end-edit → find/create → select → `startEditText` pattern for chord symbols (`createHarmony(segment, track, HarmonyType)` at `:8241`; `findHarmonyBySegment`/`findHarmonyByTick` reuse an existing one if track and text style match), figured bass and staff/system text. The `EDITTEXT_ADVANCE_*` commands in §5.3 go through the by-`Fraction` variants.

## 12. Grip & element editing

### 12.1 Grips

- **Grip rects are produced by painting.** `drawGripPoints` (`:3716-3757`, called from `NotationInteraction::paint`, `:444`) is the only place that fills `m_editData.grips` / `m_editData.grip` — it skips if a non-grip drag is in progress, picks `editedElement = m_editData.element` (or a selected `Dynamic`, whose adjacent hairpins it refreshes via `findAdjacentHairpins()`), sizes `gripsCount = element->gripsCount()` rects of 8/scaling px at the page offset, then `element->updateGrips(m_editData)` positions them, then draws via `editModeRenderer()`. ⭐ Consequence: `isHitGrip(pos)` / `startEditGrip(pos)` (`:4557-4569`, using `findGrip` = first rect inflated by half its width containing the point) work only on state left by the **last paint** — a grip is un-clickable until the item has been painted in edit state, and `resetGripEdit()` (`:6739`, zeroes `grips`/`curGrip`, clears `grip`, `resetAnchorLines`) invalidates them.
- `startEditGrip(element, grip)` (`:4571`): no-op if already that element+grip; sets `element`/`curGrip`; `updateGripAnchorLines()` (anchor lines of `gripAnchorLines(curGrip or defaultGrip())`, dropping zero-length lines); `element->startEdit(ed)`; `notifyAboutNotationChanged`. Note it does **not** open a transaction or fire `isEditingElementChanged` (the caller `startEditElement` fires it).
- `endEditGrip()` (`:4586`): only resets `curGrip = NO_GRIP` + repaint (element stays "being edited").
- `isStartEndGripSelected()` = `curGrip` is `START`/`END`.

### 12.2 Keyboard editing — `isEditAllowed` / `editElement` / `handleKeyPress`

- `isEditAllowed(QKeyEvent*)` (`:4682`): copies `m_editData`, fills modifiers/key/text, asks `element->isEditAllowed(ed)`; if the element says no: Alt-modified ⇒ false, text ⇒ false, else only **arrow keys** (plus **Tab/Backtab** if the element has grips) are accepted. This is the predicate the view uses to decide whether to route a key to `editElement`.
- `editElement(QKeyEvent*)` (`:4719`): ignores keys **while dragging**; copies key/text/modifiers into `m_editData`; **brackets** are re-found after the edit by `(system index, bracket index)` because layout destroys/recreates them (`:4739-4780`); Harmony + Return/Enter while text-editing ⇒ `endEditText()`; `needStartEditGrip(event)` (arrow key, element has grips, not yet grip-editing) sets `curGrip = defaultGrip()`; then `handleKeyPress`; on success re-selects the bracket, refreshes grip anchor lines, `event->accept()`; text editing ⇒ `notifyAboutTextEditingChanged()`.
- `handleKeyPress(event)` (`:4220`) — the **keystroke state machine**:
  1. text element → `doTextEdit` (§11.1); if it handled the key, done;
  2. `startEdit("Keystroke edit")`; `element->edit(ed)` true ⇒ `apply()`, handled; else `rollback()`;
  3. Alt held ⇒ not handled; raster: `vRaster/hRaster = 4` for beams, else `MScore::vRaster()/hRaster()`;
  4. `Tab` / `Backtab` ⇒ `element->nextGrip(ed)` / `prevGrip(ed)` (only if `hasGrips()`) + `notifyAboutNotationChanged`;
  5. arrows ⇒ `delta = ±nudgeDistance(ed, raster)` (X: `hRaster`, Y: `vRaster`); `evtDelta = moveDelta = delta`; second `startEdit("Keystroke edit")`; if a grip is current → `ed.pos = grip[curGrip].center()+delta` then `startDragGrip/dragGrip/endDragGrip`; else `startDrag/drag/endDrag` on the element; `apply()`.
  ⚠ Note two `startEdit("Keystroke edit")` per arrow key: the first is rolled back (step 2 failed) before the second opens — the §Gotchas "never nest prepareChanges" rule is honoured here only because of that `rollback()`.
- `nudge(direction, quickly)` (`:4022`, text base / articulation family only): `undoChangeProperty(Pid::OFFSET, offset ± step, UNSTYLED)` with step `MScore::nudgeStep` (or `nudgeStep10` quickly) × spatium; one "Nudge element" transaction. `nudgeAnchors(direction)` (`:4059`): the grip/drag trio on `m_editData.element` with `getHRaster/getVRaster` (bar lines always 4.0 = .25 sp).
- `updateTimeTickAnchors(QKeyEvent*)` (`:4815`) / `moveElementAnchors(QKeyEvent*)` (`:4830`): time-tick anchor display for elements with `allowTimeAnchor()`; `MoveElementAnchors::moveElementAnchors(element, key, modifiers)` inside "Move element anchors".

## 13. Layout locks and sub-interface implementation bodies

### 13.1 Layout locks / breaks (`:5890-6016`)

Every lock/break operation in `NotationInteraction` is the same thin shape — `transaction(TranslatableString("undoableAction", "<name>"), [&](auto& tx){ Edit{System|Page}Locks::fn(tx, score(), …); })` — the logic lives in `src\engraving\editing\editsystemlocks.*` / `editpagelocks.*`:

| Interaction method | Undo name | Engraving call |
|---|---|---|
| `moveMeasureToPrev/NextSystem` | "Move measure to previous/next system" | `EditSystemLocks::moveMeasureToPrev/NextSystem(tx, score, startMeasureBase)` |
| `toggleSystemLock` | "Lock/unlock selected system(s)" | `toggleSystemLock(tx, score, selection()->selectedSystems())` |
| `toggleScoreLock` | "Lock/unlock all systems" | `toggleScoreLock(tx, score)` |
| `makeIntoSystem` | "Create system from selection" | `makeIntoSystem(tx, score, first, last)` (selection start/end measure bases) |
| `applySystemLock` | "Apply system lock to selection" | `applyLockToSelection(tx, score)` |
| `addRemoveSystemLocks(type, interval)` | "Measures per system" | `addRemoveSystemLocks(tx, score, interval, afterEachSystem)`; `interval` forced 0 unless `MeasuresInterval`; `afterEachSystem` ⇔ `AfterEachSystem` |
| `moveSystemToPrev/NextPage` | "Move system to previous/next page" | `EditPageLocks::moveMeasuresToPrev/NextPage(tx, score, startSys->first(), endSys->last())` — expands the selection to whole systems first |
| `togglePageLock` | "Lock/unlock selected page(s)" | `togglePageLock(tx, score, selection()->pagesContainingSelection())` |
| `makeIntoPage` / `applyPageLock` | "Create page from selection" / "Apply page lock to selection" | `makeIntoPage` / `applyLockToSelection` |

These all use the **`transaction(...)` (lambda) idiom**, i.e. `NotationUndoStack::transaction` → one atomic step, no manual `apply()`. Systems/pages containing multimeasure rests: `NotationParts::setPartVisible/setStaffVisible/insertPart` call `EditPageLocks::removePageLocksContainingMMRests` and `EditSystemLocks::removeSystemLocksContainingMMRests` when something becomes visible/added (§13.2) so locks can't straddle a newly-expanded mm-rest. The lock palette cells (SYSTEM_LOCK/PAGE_LOCK `ActionIcon`s) reach the same functions via drop (§9) and palette (§10).

### 13.2 `NotationParts` (`notationparts.cpp`, 1429 lines)

Edit bracket: `startEdit(name)` = `undoStack->prepareChanges`; `apply()` = `commitChanges()` **then** `m_partsChanged.notify()`; `rollback()`. Most setters: look up `Part*/Staff*` by `ID` (`partModifiable` = `score()->partById(id.toUint64())`), **early-return if nothing would change**, `startEdit(name)`, call an `EditPart::…`/`EditStaff::…` function (or `score()->undo(new ChangeX…)`), `apply()`, then fire the per-item `ChangedNotifier` (`notifyAboutPartChanged/StaffChanged/…`).

| Method | Undo name | Notes |
|---|---|---|
| `setParts(PartInstrumentList, ScoreOrder)` (`:196`) | "Add/remove instruments" | `endInteractionWithScore()` (clear selection + `endNoteInput(true)`), `doSetScoreOrder`, `removeMissingParts`, `insertNewParts`, `updateSoloist`, `sortParts`, `setBracketsAndBarlines`, `updatePartsAndSystemObjectStaves`. **The instruments-dialog "OK" path** |
| `removeParts` / `removeStaves` / `moveParts` / `moveStaves` | "Remove instruments"/"Remove staves"/"Move instruments"/"Move staves" | each starts with `endInteractionWithScore()`; `moveStaves` asserts all source staves share the destination staff's part; `removeParts` re-sorts (`sortParts`) and fires `notifyAboutPartRemoved` after `apply()`; `doRemoveParts` → `EditPart::removeParts` + virtual `onPartsRemoved` (empty in base) |
| `insertNewParts(list[, keyList])` (`:1197`) | — | for each non-existing entry: `Instrument::fromTemplate`, new `Part` (soloist, long/short name, transposition **translated** via `muse::qtrc("notation", …)`), `part->setNumber(resolveNewInstrumentNumber(...))`, then `AddPartToExcerpt` (in an excerpt) **or** `score()->undoInsertPart`, `appendStaves(part, template, keyList)` (staff per template staff, `adjustKeySigs`). Existing entries whose instrument is also being newly added get **renumbered** (`ChangeInstrumentNumber(Fraction(-1,1), …)`) |
| instrument numbering (`:1285-1352`) | — | `countExistingInstruments` (by `instrument->id()` over `score()->parts()`); `resolveNewInstrumentNumber`: if some already exist → `count+1`; else count new ones of that id and return `1` if more than one new, **else `0`** (0 = unnumbered); `resolveExistingInstrumentNumber`: ordinal among same-id parts |
| `sortParts(list)` (`:1265`) | — | builds a `staffMapping` (global staff indices in the desired part order) → `score()->undo(new SortStaves(score, staffMapping))` |
| `setPartVisible`, `setStaffVisible` | "Show/Hide instrument", "Show/Hide staff" | on show: remove page/system locks containing MM rests |
| `setVoiceVisible` (`:517`) | "Show/Hide voice N" | `EditPart::setVoiceVisible` may recreate the staff, so `m_ignoreUndoStackChanges = true` around `apply()` (explicit `HACK` comment) to prevent the undo listener firing removed+added and corrupting the Layout panel; then `notifyAboutStaffChanged(staffModifiable(staffId))`; returns false + `rollback()` on failure |
| `setStaffConfig(id, StaffConfig, tick)` / `staffConfig(id, tick)` | "Edit staff properties" | `StaffConfig` ⇄ staff props: visible, userDistance, cutaway, hideSystemBarline, mergeMatchingRests, clefTypeList, reflectTranspositionInLinkedTab, staffType copy; `doSetStaffConfig` pushes `ChangeStaff` + `ChangeStaffType(staff, type, tick)` |
| `setStaffType`, `setPartSharpFlat`, `setInstrumentName/Abbreviature/Number/GroupNameOptions`, `setSharedPartEnabled`, `setScoreOrder` | respective | `setInstrumentNumber` pushes `ChangeInstrumentNumber(tick, part, v)`; shared-part uses `Pid::SHARED_PART_ENABLED` |
| `appendStaff` / `appendLinkedStaff` / `appendStaffLinkedToMaster` | "Add staff" / "Add linked staff" | `doAppendStaff` = `insertStaff` at end of part (`undoInsertStaff`), `adjustKeySigs` from the previous staff's key list, `updateBracesAndBarlines`, `setBracketsAndBarlines`, default clef. Linked variants `staff->setLinks(nullptr)` then `Excerpt::cloneStaff(src, staff)`. ⚠ **`appendStaffLinkedToMaster` calls `startEdit` and `Link`/`cloneStaff2` but never `apply()`** (`:678-699`) — it relies on the caller (`MasterNotationParts::appendStaff`, via `endGlobalEdit`) to commit; calling it on an excerpt's `NotationParts` standalone would leave a transaction open |
| `insertPart(part, index)` / `replacePart` / `replaceInstrument` / `replaceDrumset(…, undoable)` | "Add instrument" / "Reset instrument formatting" / "Replace instrument" / "Edit drumset" | `doInsertPart` strips a part's staves + instruments, inserts the empty part, restores instruments, then **re-creates each staff as a linked copy** (`Link` + `Excerpt::cloneStaff2` over `[firstMeasure.tick, lastMeasure.endTick)`) and `remapBracketsAndBarlines`; `replaceInstrument` splits "main instrument" (`EditPart::replacePartInstrument`, may change staff type) vs mid-score instrument change (`replaceInstrumentAtTick`, rollback on failure); `replaceDrumset(undoable=false)` mutates `Instrument::setDrumset` directly **with no transaction** (used while editing the drumset live) and always re-notifies note-input state |
| system objects: `addSystemObjects`, `removeSystemObjects`, `moveSystemObjects`, `moveSystemObjectLayerBelow/AboveBottomStaff` | "Add/Remove/Move system markings", "Add/Remove system object layer below the bottom staff" | the layer toggles are just `score()->undoChangeStyleVal(Sid::systemObjectsBelowBottomStaff, true/false)` |
| `toggleStaveSharing(on)` | "Toggle stave sharing" | via `undoStack()->transaction(...)` + `EditStaveSharing::toggleStaveSharing(tx, score, on)`, then `m_partsChanged.notify()` |

**Change feeds.** `partList()`/`staffList(partId)` build a fresh `NotifyList` whose notify channel is the shared `m_partChangedNotifier` / per-part `m_staffChangedNotifierMap[partId]` (`ChangedNotifier<…>` with `itemAdded/itemRemoved/itemChanged/itemReplaced`). `updatePartsAndSystemObjectStaves(changes)` (`:322-379`) diffs the cached `m_parts` / `m_systemObjectStaves` against the score and, using `changes.changedObjects` (`CommandType::SortStaves`/`RemoveStaff`/`InsertStaff` per staff), emits `partsChanged`, `systemObjectStavesChanged`, `sharedPartsChanged` (also fired on any `styleChanged`, `listenStyleChanges`, `:381`), and per-staff added/removed; a pure sort with no part-set change emits one `changed()` and returns early.

### 13.3 `MasterNotationParts` (`masternotationparts.cpp`, 353 lines)

The master override family wraps structural ops in a **global edit**: `startGlobalEdit(name)` = `NotationParts::startEdit(name)` **+ `undoStack()->lock()`**, `endGlobalEdit()` = `unlock()` **+ `NotationParts::apply()`**. Because the stack is locked, the *nested* `NotationParts::removeParts/appendStaff/…` calls (whose own `startEdit`/`apply` → `prepareChanges`/`commitChanges`) become **silent no-ops** (§4.2/gotchas) — which is exactly how one outer transaction covers master + every excerpt. Pattern: do the op on the master, loop `for (parts : excerptsParts())` (= `excerpt->notation()->parts()` over `m_excerpts`, injected by `setExcerpts`), `endGlobalEdit()`.

| Method | Excerpt propagation |
|---|---|
| `setParts` | master does the full sequence; each excerpt only `removeMissingParts(partList)` + `sortParts(its own list rebuilt from its current parts)` + `setBracketsAndBarlines()` (excerpts never *gain* parts here — added instruments get their excerpt via the `Excerpts`/`potentialExcerpts` flow, §3.1) |
| `removeParts`, `removeStaves` | same call on every excerpt's parts |
| `appendStaff` | `staff->setId(INVALID_ID)` ("generated later after adding"); master append; per excerpt `staff->clone()` → `appendStaffLinkedToMaster(excerptStaff, staff, destPartId)`; on `false` (no such part in that excerpt) `undoUnlink()` + `delete` |
| `appendLinkedStaff` | per-excerpt clone → `appendLinkedStaff(...)`; on failure `unlink()` + delete (note: `unlink`, **not** `undoUnlink`, unlike `appendStaff` — asymmetric, HYPOTHESIS harmless because the staff was never added) |
| `replaceInstrument` | master + every excerpt; if the transposition `reduced()` interval changed, `Transpose::transpositionChanged(tx, score, part, MAIN_INSTRUMENT_TICK, oldTranspose)`; if it was the part's main instrument and that part has an `Excerpt` (`findExcerpt(partId)` by `initialPartId`), the excerpt is **renamed** to `formatUniqueExcerptName(part->partName(), existingLowercaseNames)` via `ChangeExcerptTitle` |
| `replaceDrumset`, `moveSystemObjects` | master + every excerpt |
| `addSystemObjects`, `removeSystemObjects`, `moveSystemObjectLayer*` | master only (guarded: empty list returns; the layer toggles `IF_ASSERT_FAILED(nstaves() > 1)`) |
| `onPartsRemoved(parts)` (override of the empty base hook) | deletes any master `Excerpt` whose `initialPartId()` is among the removed parts: `master->deleteExcerpt(excerpt)` |

### 13.4 `NotationPlayback` (`notationplayback.cpp`, 628 lines) — master only

A thin façade over `playback::PlaybackModel m_playbackModel` (module `src\playback`, see `playback-and-audio.md`). It owns almost no logic of its own:

| Responsibility | Methods (all `:`-lines in `notationplayback.cpp`) |
|---|---|
| Init / config sync | `init()` (`:71`): pushes `isPlayRepeatsEnabled`, `isPlayChordSymbolsEnabled`, `playPreviewNotesWithScoreDynamics`, `isMetronomeEnabled` into the model, `load(score())`, recomputes total play time on every `tracksDataChanged`, and subscribes each config-changed notifier (repeats/chord symbols ⇒ `setX` + **`reload()`** only when the value actually changed; the other two just set). `score()->loopBoundaryTickChanged()` → `updateLoopBoundaries`. ctor: every `notationChanged` also calls `updateLoopBoundaries` (so loop markers follow edits) |
| Pass-through to model | `reload`, `setSendEventsOnScoreChange(trackId, bool)`, `sendEventsForChangedTracks`, `tracksDataChanged/trackAdded/trackRemoved`, `existingTrackIdSet`, `metronomeTrackId`, `chordSymbolsTrackId(partId)`, `isChordSymbolsTrack`, `trackPlaybackData(trackId)` (= `resolveTrackPlaybackData`), `triggerEventsForItems(items, duration, flushSound)`, `triggerMetronome(tick)`, `triggerCountIn(tick, &secs)` (µs→s), `hasSoundFlags(trackIdSet)` |
| Track naming | `trackName(trackId)` (`:159`): metronome → "Metronome"; chord-symbols track → `"Chords." + partName`; main instrument → `partName`; other instrument → `"(" + instrument->trackName + ")"` |
| Off-stream controller events | `triggerControllers(list, staffIdx, tick)` (`:208`): resolves `InstrumentTrackId{part id, instrumentId at tick}` and sends the list into `data.offStream` (immediate, no flush) |
| Time conversion | `updateTotalPlayTime` (`:261`): `repeatList(expandRepeats).ticks()` → `tempoTimeline(expandRepeats).utick2utime` + **`PLAYBACK_TAIL_SECS = 3`**, fires `totalPlayTimeChanged` only on change; `playedTickToSec`/`secToPlayedTick` (timeline utick↔utime), `secToTick` (utime→utick, then `expandedRepeatList().utick2tick` when repeats are expanded), `playPositionTickByRawTick/ByElement` (`repeatList.tick2utick`; `Err::Undefined` when no score) |
| Loop | `addLoopBoundary(type, tick)` resolves sentinel ticks `BoundaryTick::FirstScoreTick/LastScoreTick/SelectedNoteTick`; `addLoopIn`: if In ≥ Out, Out reset to score end; `addLoopOut`: if Out ≤ In, In reset to 0, Out clamped to score end; `SelectedNoteTick` for Out = `score()->pos() + inputState().ticks()`. `setLoopBoundariesEnabled`, `isLoopEnabled` = `!boundaries.isNull() && enabled`, `loopBoundaries/Changed`, `loopEnabledChanged` |
| Tempo | `multipliedTempo(tick)` (rounded BPM into a cached `m_currentTempo` — returns a **reference to a member**, so callers must copy before the next call); `tempoMultiplier()`; `setTempoMultiplier(m)`: `masterScore()->setTempoMultiplier`, `updateRepeatListTempo()`, `reload()`; `beat(tick)` = `findBeat`, `beatToRawTick(measureIndex, beatIndex)` = `sigmap()->bar2tick` |
| Sound flags | `addSoundFlags(vector<StaffText*>)` (`:484`): for each staff text without one, `Factory::createSoundFlag` + `staffText->add(...)` and a `linkedClone()` onto every linked `StaffText`; then `score()->update()` + `notationChanged`. `removeSoundFlags(trackIdSet)` (`:531`): `collectStaffText(trackIdSet, withSoundFlags=true)` scans every ChordRest segment's annotations of the first measure onward, matches `makeInstrumentTrackId(annotation)`, removes the flag from the text and its links, then `update()`, `reload()`, `notationChanged`. ⚠ **Neither add nor remove opens an undo transaction** (direct `add`/`remove` calls) — sound-flag toggling from here is not undoable via this path (TRACED; whether callers wrap it is UNCLEAR) |

When `MUE_BUILD_ENGRAVING_PLAYBACK` is off, `NotationPlaybackStub` replaces all of this (§1.1).

### 13.5 `NotationPainting` (`notationpainting.cpp`, 238 lines)

| Method | Behaviour |
|---|---|
| `setViewMode(mode)` / `viewMode()` / `viewModeChanged()` | `score()->setLayoutMode(mode)` + **`score()->doLayout()`** (full relayout, not a dirty-range) + notify + `notifyAboutNotationChanged()`; `viewMode()` defaults to `PAGE` with no score. This (not `INotationViewState::viewMode`) is the score-level mode `Notation::viewMode()` delegates to |
| `pageCount()`, `pageSizeInch([opt])` | `npages()`; `scoreRenderer()->pageSizeInch(score[, opt])` |
| `isPaintPageBorder()` | false for LINE/HORIZONTAL_FIXED/SYSTEM; for FLOAT/PAGE true unless `score()->printing()` |
| `doPaint(painter, opt)` (`:114`) | installs `opt.onPaintPageSheet` (→ `paintPageSheet`), calls `scoreRenderer()->paintScore(painter, score, opt)`, and — **only when `!isPrinting`** — **`static_cast<NotationInteraction*>(m_notation->interaction().get())->paint(painter, eopt)`** to overlay the interactive layer (§4.6 list). ⇒ PDF/print/PNG never contain shadow note, selection, grips |
| `paintPageSheet` | printing: white fill only if `printPageBackground`; screen: `foregroundColor` or tiled `foregroundWallpaper` else white; then page border pen `0.07*DPMM` in `borderColor`; if `showPageborders()` also the content rect (page margins; odd/even binding line) in `scoreGreyColor` |
| `paintView(painter, frameRect, isPrinting, isAutomation)` | `Options{isSetViewport=false, isMultiPage=true, frameRect, deviceDpi=uiConfiguration()->logicalDpi(), invertColors=shouldInvertScore()}`; in **automation mode** `overrideItemColor` dims every item to 40% alpha except Dynamics and hairpin segments (so the automation curves stand out) |
| `paintPdf`/`paintPrint`/`paintPng` | `isSetViewport=true`, `isMultiPage=false`, `isPrinting=true` (all three identical; assert `deviceDpi>0`) |

⚠ `doPaint` downcasts the interaction to the concrete `NotationInteraction` (`static_cast`) — painting is not mockable through `INotationInteraction`.

### 13.6 `NotationViewState`, `NotationAccessibility`, `NotationElements`, `NotationAutomation`

| Class | Real responsibility / key methods |
|---|---|
| **`NotationViewState`** (216 lines) | In-memory zoom/matrix + persisted view mode. `matrix()`/`setMatrix(m, sender)` — fires `matrixChanged(Transform, NotationPaintView* sender)` (the sender lets the originating view skip its own echo) and updates `zoomPercentage` from `configuration()->zoomPercentageFromScaling(matrix.m11())`; `isMatrixInited/setMatrixInited`; `zoomType()/setZoomType` (`ValCh<ZoomType>`). ctor: when the notation **closes**, `m_isMatrixInited=false` and matrix reset to identity. **Persistence is view-mode only** (`read`/`write` ↔ JSON `{"notation":{"viewMode": …}}` with string map `page|float|continuous_v(LINE)|continuous_h(SYSTEM)|continuous_h_fixed(HORIZONTAL_FIXED)`, default `PAGE`; `pathPrefix` is passed straight through to the reader/writer). `styleDialogLastPageIndex/SubPageIndex` are held in memory (notify `stateChanged`) but **not** read/written; `makeDefault()` resets them + view mode. ⚠ The persisted `viewMode` here is separate from `NotationPainting::viewMode()` (which drives the score's actual layout mode) — `read` only sets `m_viewMode`; something else must apply it to the score (not traced) |
| **`NotationAccessibility`** (205 lines) | Builds the screen-reader status string. Subscribes to `interaction()->selectionChanged` and `notationChanged`; `updateAccessibilityInfo()`: single → `element->accessibleInfo()` + `formatBarsAndBeats()` + `"Staff N (part name)"` (long name at tick, else `partName`); range → `"Range selection; Start measure: N[; Start displayed measure: M]; Start beat: B; End …"` (end segment = `endSegment->prev1MM()` or `lastSegment()`); list → "List selection"; whitespace-simplified; `ValCh<string>` only fires if the text changed. `setMapToScreenFunc(fn)` and `setEnabled(bool)` act on the **two** `AccessibleRoot`s (the score's `rootItem()` and the **dummy** score's) unless compiled with `ENGRAVING_NO_ACCESSIBILITY`; enabling re-focuses the currently selected element's accessible item on the root that owns it, disabling clears focus |
| **`NotationElements`** (256 lines) | Read-only query service. `msScore()` (raw `Score*`); `search(text)` parses via `SearchCommandsParser` into `Type::{Invalid, RehearsalMark, Measure, MeasureRange, Page}` — rehearsal mark = first one whose `plainText().toLower().startsWith(query)`, measure/page indexes are **1-based in the query, 0-based internally** (`measure(i)` = `score()->crMeasure(i)`, **skipping mm-rest measures**), a measure range returns just the start and end measures, no whole-range list; `elements(FilterElementsOptions)` → `filterElements` (`ElementPattern` + `score()->scanElements` + `Score::collectMatch`) or, for a `FilterNotesOptions` subclass, `filterNotes` (`NotePattern` + `collectNoteMatch`); no options ⇒ `allScoreElements()` (every page's `elements()`); `pages()`, `pageByPoint` = `score()->searchPage`. A source `todo` notes range-search is unimplemented (`:114`). ⚠ `dynamic_cast<const FilterElementsOptions*>(&options)` on a reference already of that type is always non-null — dead check |
| **`NotationAutomation`** (82 lines) | Tiny: `m_isAutomationModeEnabled` flag + `automationModeEnabledChanged`; `automationData()` = `masterScore->automationData()`; `editPoints(key, edits)` = `m_undoStack->transaction("Edit automation points", [&]{ masterScore->editAutomationPoints(key, edits); })`; `resetEditStateRequested()` (the Escape ladder, §5.3); `setMasterScore(ms)`. Not part of `INotation`; reached via `IMasterNotation::automation()` |

## Cross-subsystem dependencies

- **Leans on:** `engraving-dom.md` (the `Score`/`MasterScore`/`Excerpt`/`Selection`/`InputState` it wraps; `src\engraving\editing\*` for the real edit logic, `…\transaction\*` for undo); `layout-and-rendering.md` (`ISingleRenderer`, `IEditModeRenderer`, `IScoreRenderer`, `Score::doLayout`/`update` triggered inside `endTransaction`); `style-system.md` (`INotationStyle` → `MStyle`/`Sid`); `file-format-and-rw.md` (`INotationViewState` persisted in `.mscz`; `MscNotationWriter` registered by `NotationModule::resolveImports` for `mscz`/`mscx`, `PositionsWriter` for `spos`/`mpos`, `notationmodule.cpp:61-70`); `architecture-and-ioc.md` (`GlobalInject`/`ContextInject`, per-context exports).
- **Feeds:** `playback-and-audio.md` (`IMasterNotation::playback()`, `INotationPlayback`, `changesChannel` → track data); `ui-qml-mvvm.md` (every NotationScene/InstrumentsScene/PropertiesPanel/Palette view-model drives these interfaces; `NotationPaintView`; `INotationViewController` implemented by the view); `framework-services.md` (`rcommand` dispatcher/register/state, shortcuts, `IInteractive` for dialogs); `project` module (`NotationProject` creates `MasterNotation`; `IProjectSession` selects the current notation); `plugin-api.md` (`engraving\api\v1` operates on the `Score` and its own transactions — not through `INotation`; cross-check `score.cpp` `prepareChanges` use, `src\engraving\api\v1\score.cpp`).

## Non-obvious gotchas

- ⭐ **One undo stack for the whole project.** Part score edits and master edits share `masterScore()->undoStack()` (`notationundostack.cpp:286-294`). `ExcerptNotation::~ExcerptNotation` therefore must not delete its score (`excerptnotation.cpp:38-43`).
- ⛔ **`commitChanges/prepareChanges/rollbackChanges` silently do nothing while the stack is locked** — an edit made during `setupNewScore` (locked) is applied but never recorded or laid-out-via-transaction; don't call these expecting an error.
- ⚠ **`rollbackChanges()` doesn't emit `stackChanged`**, but `commitChanges()` and `transaction()` do (`notationundostack.cpp:131-157`). If you add UI that depends on `stackChanged` after a rollback, it won't fire. (TRACED; whether this is intentional is UNCLEAR.)
- ⚠ **`INotationStyle::setStyleValue` is not self-transacting** (see §4.6). Forgetting the `prepareChanges/commitChanges` bracket gives a style change that isn't a proper undo step. Contrast `loadStyle`, which brackets itself.
- ⚠ **`TransactionManager::transaction()` asserts on nesting** (`IF_ASSERT_FAILED(!m_currentTransaction)` → logs "transaction already active" and runs `fn` in the existing one, `transaction.cpp:146-156`), whereas `beginTransaction` on an active stack just logs `"cmd already active"` and returns (`:166-169`) — **a second `prepareChanges` while one is open is a silent no-op and its matching `commitChanges` closes the *outer* transaction early.** Keep begin/commit strictly paired and never nest.
- ⚠ **`NotationSelection::doSelect` reads global Qt state** (`QGuiApplication::keyboardModifiers()`, `notationselection.cpp:440`) — selection results depend on the physical Ctrl key, which matters for tests/automation (headless/CLI, MCP-driven selection).
- **`MasterNotation` ctor is private** (`friend NotationProject`) — you cannot construct one in a unit test directly; use the mocks in `src\notation\tests\mocks\` (`masternotationmock.h`, `notationmock.h`, `notationinteractionmock.h`, …).
- **`potentialExcerpts()` is not cached across calls** — it recomputes via `updatePotentialExcerpts()` each call (it reuses existing per-part entries unless `m_potentialExcerptsForcedDirty`, which `onPartsChanged()` sets, `masternotation.cpp:670-725, 795-800`). The "forced dirty" comment: removing then re-adding instruments can reuse part IDs, so IDs alone aren't a safe cache key (`masternotation.h:109-113`).
- **Playback is conditionally compiled**: with `MUE_BUILD_ENGRAVING_PLAYBACK` off, `MasterNotation::playback()` returns a stub (`NotationPlaybackStub`); don't assume real tracks in tooling/CLI builds.
- **Notifications are layered, not unified:** `notationChanged(RectF)` (repaint), `selectionChanged`, `noteInput::stateChanged`, `undoStack::stackChanged`, `undoStack::changesChannel(ScoreChanges)`, `parts::partsChanged`, `style::styleChanged`, `masterNotation::excerptsChanged`. Command state keys off the first five via the controller; the **properties panel and excerpt/part sync key off `ScoreChanges`**. Pick the right one — `notationChanged` carries no semantic info.
- **Command handlers re-check enabled state**, so calling a handler "directly" bypasses nothing, but *dispatching* a command whose `CommandState` is stale-disabled fails with `Ret::Code::NotSupported` even though the underlying interaction call would have worked. If a new feature's command "does nothing", check its `NotationCommandsState` group and the signal that should refresh it.
- **Two command spellings coexist:** typed (`struct XCommand`, schema, API/MCP-visible) vs bare `Command("command://notation/…")` constants. Per ADR 00104 identity is the URI only — never encode variable data in the URI path of a built-in; pass it as `Params`.
- **Style conflict note (upstream vs your style):** `NotationActionController` and the interfaces are upstream code in MuseScore's own brace/format style; edits intended for upstream must match it (see `contributing-and-conventions.md`).

## UNCLEAR — needs verification

*(Pass 2 resolved the former items on the interaction internals, drop/text/grip/lock paths, the sub-interface impl bodies, `NotationParts`/`MasterNotationParts` propagation and the `doAddNote` tail. What genuinely remains:)*

- **The ~150 "do X to selection" bodies** of `notationinteraction.cpp` (roughly lines 2980-3650, 3840-4020, 5000-5900, 6130-6700, 7600-8230, 8450-end: `movePitch`, `moveChordRestToStaff`, `swapChordRest`, `flipSelection`, `addHairpins…`, `explode/implode`, `spellPitches`, `regroupNotesAndRests`, `addBoxes` tail, `showItem`, `checkAndShowError`, the selection-proximity/hit-test helpers `hitElement`/`elementsAt`/`dragPositionToMeasure/Segment`) are classified by idiom (§4.2) but not individually traced — each is a small `startEdit` + engraving call + `apply` or a `transaction` lambda.
- **`searchcommandsparser.cpp`** grammar (accepted query syntax for rehearsal mark / measure / range / page) — only its consumer `NotationElements::search` was read.
- **Who applies `NotationViewState::viewMode` to the score on load.** `read()` only sets `m_viewMode` (§13.6); the hand-off into `NotationPainting::setViewMode` / `Score::setLayoutMode` (likely in `project`/`NotationProject` or the paint view) was not traced.
- **Callers of `NotationPlayback::addSoundFlags/removeSoundFlags`** — the methods mutate the score with no transaction (§13.4); whether the invoking playback/palette code supplies an undo bracket is unverified.
- **`ScoreCallbacks` callers of `changeEditElement`** — which element classes hand edit focus to a related element was not enumerated.
- **Intent of two suspicious spots** flagged in §7.3 (view-mode deselect condition, `notationinteraction.cpp:356`) and §11.2 (`navigateToNextSyllable` returning after `startCmd` without `endCmd`, `:7049-7055`): TRACED control flow, unverified intent/reachability.
- **`IProjectSession` implementation**: only `currentNotation()` / `setCurrentNotation` + `currentNotationChanged` surfaced via grep (`projectsession.cpp:94-115`); how the "current" tab is chosen on project open/part switch not traced.
- **`rcommand` module internals** (`ICommandDispatcher::dispatch` promise semantics, `Availability`/`Annotation` enforcement, how `Command` URIs map to shortcuts) — only the call sites and the two ADRs were read; belongs to `framework-services.md`.
- **Whether `rollbackChanges` not emitting `stackChanged` is intentional** (TRACED behaviour; intent unknown).
- Exact count of registered commands: `notationactioncontroller.cpp:95-715` registers on the order of 400 handlers (HYPOTHESIS from the visible table + loop; not counted programmatically).
- `src\web\…` (WebAssembly build) still on actions — not examined; may have a separate notation-driving path.

*Authored from: full reads of `src\notation\inotation.h`, `inotationundostack.h`, `inotationselection.h`, `inotationselectionrange.h`, `inotationselectionfilter.h`, `inotationnoteinput.h`, `inotationinteraction.h`, `inotationparts.h`, `imasternotation.h`, `iexcerptnotation.h`, `inotationstyle.h`, `inotationelements.h`, `inotationmidiinput.h`, `inotationpainting.h`, `inotationplayback.h`, `inotationaccessibility.h`, `inotationviewstate.h` (head), `inotationautomation.h`, `notationtypes.h`, `types\noteinputtypes.h`, `internal\notation.{h,cpp}`, `internal\masternotation.{h,cpp}`, `internal\excerptnotation.cpp`, `internal\notationundostack.cpp`, `internal\notationselection.{h,cpp}` (select path), `internal\notationnoteinput.cpp` (start/end/add/put), `internal\notationstyle.cpp`, `internal\notationparts.{h,cpp}` (shape + undo listener), `internal\masternotationparts.h`, `notationmodule.cpp`; `src\engraving\editing\transaction\{transaction,undostack}.{h,cpp}`; **pass 2 additionally:** `internal\notationinteraction.{h,cpp}` (lines 290-450, 990-1620, 1622-2250, 2250-2480, 2640-3450, 3660-3760, 4020-4900, 5900-6030, 6695-6750, 6753-7240, 8225-8460), `src\engraving\editing\editdata.h`, `internal\{notationparts,masternotationparts,notationplayback,notationpainting,notationaccessibility,notationelements,notationviewstate,notationautomation}.cpp` (whole files), `internal\notationnoteinput.cpp:108-128`, `internal\scorecallbacks.cpp:65-72`, `src\notationscene\internal\notationactioncontroller.cpp:925-1110, 2715-2725`; `src\notationscene\{notationcommands.h,notationcommandsenums.h,notationscenemodule.cpp,inotationcommandscontroller.h}` and `internal\{notationactioncontroller.{h,cpp},notationcommandsstate.cpp,notationcommandsregister.cpp}`; ADRs `muse\docs\adr\00101-…` and `00104-…`. Deep pass 1 + 2, 2026-11-08. **If this contradicts the live source, the source wins.***
