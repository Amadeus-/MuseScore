# MuseScore — Style System

> **What this file is.** The deep-pass reference for the **score style system** in the MuseScore 5.0 `main` tree: `src\engraving\style\` (`Sid`, `MStyle`, defaults, text styles, page style), how an element's *styled properties* resolve against it (`dom\engravingobject.cpp`, `dom\textbase.cpp`), how style is changed undoably (`editing\editstyle.*`), and how it is read/written (`.mss` files, `score_style.mss` inside `.mscz`). Everything here was read from source on 2026-11-08. Precedence per `00-core.md`: **the live source wins** — if this disagrees with the code, fix this file. Start at `00-core.md`; the property system it plugs into is `engraving-dom.md` §1.3.

## 0. 60-second orientation

- **A score's style is one flat table**: an `MStyle` (`style\style.h:44`) holds one `PropertyValue` per style id. The ids are the `Sid` enum (`style\styledef.h:50`, `enum class Sid : short`), **2055 entries** (`NOSTYLE = -1`, then `0 … STYLES-1`). The same 2055 entries exist as rows of `StyleDef::styleValues` (`style\styledef.cpp:40`) giving each id its **XML tag name** (the `Sid` identifier stringified) and its **default value + value type**.
- **Each `Score` owns exactly one `MStyle`** (`dom\score.h:979` `MStyle m_style`; `EngravingObject::style()` returns `score()->style()`, `engravingobject.cpp:330`). Parts (excerpts) are separate `Score`s with their **own** style.
- **Elements do not store style; they cache it.** Each element class declares an `ElementStyle` table (`std::vector<{Sid, Pid}>`), installs it with `initElementStyle()`, and every styled `Pid` carries a per-item `PropertyFlags` (`NOSTYLE`/`UNSTYLED`/`STYLED`). While `STYLED`, the member value is *re-pushed from style* whenever the style changes (`Score::styleChanged()` → `EngravingItem::styleChanged()`); the moment the user overrides it the flag flips to `UNSTYLED` and it stops tracking.
- **Text styles** (`TextStyleType`, 75 values incl. `DEFAULT`) are bundles of 16 `{TextStylePropertyType, Sid, Pid}` rows (`style\textstyle.cpp`), each pointing at a family of `Sid`s (`titleFontFace`, `titleFontSize`, …). A `TextBase` combines its own `ElementStyle` **plus** the table of its current `TextStyleType`.
- **Files:** a style serializes as `<museScore version="5.00"><Style>…</Style></museScore>` — the `.mss` file, and also `score_style.mss` / `<part>.mss` inside an `.mscz`. Shipped `.mss` files: `share\styles\` (MuseJazz + 5 MSN) and the resource-embedded `src\engraving\data\styles\` (legacy-defaults, migration, Guitar Pro).
- **Key files:** `style\style.h/.cpp` (MStyle: value store + XML read/write + back-compat), `style\styledef.h/.cpp` (Sid + defaults table), `style\defaultstyle.h/.cpp` (the three builtin styles + legacy defaults), `style\textstyle.h/.cpp` (text style tables), `style\pagestyle.h/.cpp` (page-geometry Sid subset + accessors), `editing\editstyle.h/.cpp` (undo commands), `rw\compat\readstyle.*` (read hook).
- **Plugin exposure:** `api\v1\style.h` (`apiv1::MStyle : QObject`) wraps an engraving `MStyle` as `Score.style` — see `plugin-api.md`.

## 1. `MStyle` + `Sid` — the value store

### 1.1 The store (`style\style.h:44–99`)

```cpp
// class MStyle  (style.h:97-98)
std::array<PropertyValue, size_t(Sid::STYLES)> m_values;          // sparse-by-validity: invalid == "use the Sid default"
std::array<double,        size_t(Sid::STYLES)> m_precomputedValues; // spatium * value, ONLY for SPATIUM-typed sids
```

- **Read** — `MStyle::value(Sid)` (`style.cpp:48`): `NOSTYLE` → a static empty `PropertyValue`; else if `m_values[idx].isValid()` return it, **otherwise fall back to `StyleDef::styleValues[idx].defaultValue`**. So a default-constructed `MStyle` (`MStyle() = default`, `style.h:47`) is already "all defaults" with no copying — that is how `DefaultStyle::m_baseStyle` is "the builtin style" (§4).
- **Typed accessors** (`style.h:49–60`): `styleV` (raw `PropertyValue`), `styleS` → `Spatium`, `styleAbsolute` → `double` (precomputed spatium×value, i.e. an absolute length in the engine's units), `styleSt` → `String`, `styleB` → `bool`, `styleD` → `double`, `styleI` → `int` (**no assert** — "can be int or enum"). The others `assert(MStyle::valueType(idx) == P_TYPE::…)`, so a type mismatch is a debug-build assert, silent in release.
- **Value type is not stored separately** — `StyleDef::StyleValue::valueType()` = `defaultValue.type()` (`styledef.h:2320`). The **default's C++ type in the table defines the Sid's type**; `MStyle::valueType(Sid)` (`style.cpp:834`) exposes it.
- **Write** — `MStyle::set(Sid, v)` (`style.cpp:72`): stores `m_values[idx] = v`; if the sid is `Sid::spatium` it calls `precomputeValues()` (recomputes every SPATIUM-typed entry); else if the sid's type is `SPATIUM` it refreshes just that entry's `m_precomputedValues`. **No change notification, no undo** — `set` is the raw primitive. Undoable changes go through `Score::undoChangeStyleVal` (§3.3).
- **Spatium special case**: `spatium()`/`setSpatium()` (`style.h:67-68`) wrap `Sid::spatium` (REAL, default `1.75 /*mm*/ * DPMM`, `styledef.cpp:806`). Anything typed `SPATIUM` (literal suffix `_sp`, e.g. `0.18_sp`) is stored in staff-spaces and converted by `precomputeValues()`; elements ask `styleAbsolute` (via `P_TYPE::ABSOLUTE` properties, §2.3).
- **Name ↔ id**: `valueName(Sid)` → the XML tag, `styleIdx(name)` → linear scan of `styleValues` (`style.cpp:839-857`; O(2055) per call).
- **Defaults-version**: `Sid::defaultsVersion` (default `Constants::MSC_VERSION` = 500, `styledef.cpp:2186`; `constants.h:31`) records *which release's defaults this score's style was authored against*. `MStyle::isDefault(sid)` compares the value to `DefaultStyle::resolveStyleDefaults(defaultStyleVersion())` — i.e. "default **for that version**", not for today (`style.cpp:105`). See §4.2.

### 1.2 The `Sid` enum and the definition table

- **Two parallel lists that MUST stay in the same order.** The comment says it: *"Keep in sync with styleTypes[] in styledef.cpp"* (`styledef.h:37`) / *"Keep in sync with Sid in styledef.h"* (`styledef.cpp:39`). The table is a `std::array<StyleValue, size_t(Sid::STYLES)>` indexed **positionally** by `size_t(sid)` (`MStyle::value`, `style.cpp:60`), and the `styleDef(x, v)` macro (`styledef.cpp:37`) builds `{ Sid::x, "x", v }` so the *name* and the *enumerator* cannot drift — but the **row position** can. I found **no** `static_assert`/runtime check that `styleValues[i].sid == Sid(i)` (grep of `styledef.cpp` and `tests\` for `static_assert|styleValues` = no hits). The counts match today (2055 enumerators in `styledef.h`, 2055 `styleDef(` rows in `styledef.cpp`; the array size is `Sid::STYLES`, so a missing row would be value-initialized, an extra row a compile error) but **an out-of-order insert silently mis-assigns every following default**. ⛔
- **`Sid` is also a Qt meta-enum**: unless `ENGRAVING_NO_API`, it is declared in `namespace _Sid { Q_NAMESPACE; … Q_ENUM_NS(Sid) }` + `using _Sid::Sid` (`styledef.h:40-43, 2285-2289`). That is what makes the plugin API's `style.value("barWidth")` work: `apiv1::MStyle::keyToSid` uses `QMetaEnum::fromType<Sid>().keyToValue` (`api\v1\style.cpp:43`). **The Sid enumerator name is a public plugin key.**
- Other items in `styledef.h:2291-2333`: `using StyleId = Sid`, `StyleIdSet = unordered_set<Sid>`, `struct StyledProperty { Sid sid; Pid pid; }`, **`typedef std::vector<StyledProperty> ElementStyle`**, `struct StyleDef { StyleValue{sid, xmlName, defaultValue}; static const array<StyleValue, STYLES> styleValues; DEFAULT_SMUFL_POINT_SIZE() }` (SMuFL point size = 4 × default spatium in points).
- **`Sid` family naming convention** (not enforced, but near-universal): `<element><Thing>` — and a **text-style family** of 13–17 sids per text style: `<x>FontFace, FontSize, LineSpacing, FontSpatiumDependent, FontStyle, Color, Align, Offset, OffsetType, FrameType, FramePadding, FrameWidth, FrameRound, FrameFgColor, FrameBgColor, Position` (see `defaultFontFace…defaultPosition`, `styledef.h:872-889`; `user1…user12` also have a `Name`). The `<x>MusicalSymbolSize` ids (`styledef.h:2117-2191`) were added in 4.7 and are **grouped at the end**, not with their families.

### 1.3 Sid inventory — categories, ranges, representative rows

Line numbers are in `styledef.h` (enum) / `styledef.cpp` (defaults). Units: `_sp` = staff-spaces (type `SPATIUM`); `in` = inches (page geometry, REAL); mm values are `× DPMM`.

| Category | `styledef.h` lines | Representative Sids → default (type) |
|---|---|---|
| **Page geometry** | 53–62 | `pageWidth` `210/INCH`, `pageHeight` `297/INCH` (A4), `pagePrintableWidth` `180/INCH`, `page{Even,Odd}{Left,Top,Bottom}Margin` `15/INCH`, `pageTwosided` `true` (all REAL/BOOL) |
| **Staff/system distances, spread** | 64–118 | `staffUpperBorder` 7.0_sp, `staffDistance` 6.5_sp, `akkoladeDistance` 6.5_sp, `minSystemDistance` 8.5_sp, `maxSystemDistance` 15.0_sp, `enableVerticalSpread` true, `spreadSystem` 2.5 |
| **Instrument names / stave sharing** | 74–102 | `instrumentNamesFormatLong` `NAME_IN_TRANSP_NUM` (enum `InstrumentNamesFormat`), `instrumentNamesAlignLong` `RIGHT_RIGHT`, `textForUnisonLabel` `"a "`, `unisonLabelRestateOnNewSystem` `AutoOnOff::AUTO`, `windsNameByGroup` true |
| **Lyrics** (layout + odd/even text styles) | 120–180 | `lyricsPlacement` `BELOW`, `lyricsPosBelow` `(0,3)`, `lyricsDashMaxLength` 0.6_sp, `lyricsStackingOrder` int-enum, `lyricsOddFontFace` `"Edwin"`, `lyricsOddAlign` `Align(HCENTER,BASELINE)` |
| **Frames/system spacing** | 182–191 | `figuredBassFontFamily` `"MScoreBC"`, `systemFrameDistance` 7.0_sp, `minMeasureWidth` 8.0_sp |
| **Barlines** | 193–205 | `barWidth` 0.18_sp, `endBarWidth` 0.55_sp, `doubleBarDistance` 0.37_sp, `dashBarDash` 0.6_sp, `startBarlineMultiple` true, `maskBarlinesForText` true |
| **Brackets, system dividers** | 207–229 | `bracketWidth` 0.45_sp, `akkoladeWidth` 1.5_sp, `groupBracketTextOrientation` `VERTICAL`, `dividerLeft` false, `dividerLeftSym` `"systemDivider"` |
| **Clef/key/time-sig margins & time-sig styles** | 231–271 | `clefLeftMargin`, `keysigLeftMargin`, `timeSigPlacement` (enum), `timeSigNormalStyle/Scale/NumDist/Y`, `timeSigAboveStyle…`, `timeSigAcrossStyle…` |
| **Stems, spacing, staff lines** | 273–299 | `stemWidth` 0.10_sp, `minNoteDistance` 0.35_sp, `spacingDensity` 1.0, `measureSpacing` 1.5 (*the legacy name; actually the spacing ratio*, comment `styledef.h:287`), `staffLineWidth` 0.11_sp |
| **Accidentals, beams** | 300–315 | `accidentalDistance`, `keysigAccidentalDistance`, `beamWidth` 0.5_sp, `useWideBeams` false, `beamNoSlope`, `frenchStyleBeams` |
| **Dots, articulations, hairpins, pedal** | 317–375 | `dotMag`, `articulationAnchorDefault` `int(ArticulationAnchor::AUTO)`, `hairpinPlacement` `BELOW`, `hairpinLineWidth` 0.12_sp, `pedalLineWidth` |
| **Chord symbols / harmony** | 437–575 | `chordSymbolAFontFace…`, `chordSymbolSpelling` `NoteSpellingType::STANDARD`, `chordStyle` `ChordStylePreset::STANDARD`, `chordDescriptionFile` `"chords_std.xml"` (STRING), `verticallyAlignChordSymbols`, polychord ids |
| **Note magnification** | 530–532 | `smallNoteMag` 0.7, `graceNoteMag` 0.7 |
| **Transposition, multi-measure rests, hide-empty-staves** | 577–610 | `concertPitch` false, `createMultiMeasureRests` false, `minEmptyMeasures`, `hideEmptyStaves` false, `firstSystemInstNameVisibility` `InstrumentLabelVisibility::LONG`, `gateTime`/`slurGateTime` |
| **Arpeggio, chord bracket, slurs/ties** | 612–651 | `slurEndWidth` 0.05_sp, `slurMidWidth` 0.21_sp, `tieEndWidth` 0.05_sp, `tiePlacementSingleNote`/`tiePlacementChord` `OUTSIDE`, `maskSlurs`, `laissezVibMinDistance` |
| **Fonts** | 653–654 | `musicalSymbolFont` `"Leland"`, `musicalTextFont` `"Leland Text"` (STRING) |
| **Header/footer** | 656–674 | `showHeader` true, `headerFirstPage`, `evenHeaderL/C/R`, `oddFooterC`, … |
| **Voltas, ottava, TAB, tremolo, tuplets** | 676–808 | `voltaLineWidth`, `ottavaPosAbove` `(0,-2)`, `tabClef`, `tremoloStyle` int-enum, `tupletOutOfStaff` true |
| **Global misc** | 810–834 | `minVerticalDistance`, `skylineMinHorizontalClearance`, **`spatium`** (816), `dynamicsPlacement` `BELOW`, `dynamicsPosAbove` `(0,-1)`, `autoplaceHairpinDynamicsDistance` |
| **Text-line styles** | 836–866 | `textLine*`, `systemTextLine*` (placement, pos, line width/style, frame…) |
| **Text-style families** (the bulk) | 872 → ~1970 | `defaultFont*…` (872), `title*` (891), `harpPedalDiagram*` (~1076), `dynamics*` (1193), `tempo*` (1228), `measureNumber*` (1295), `header*` (1648), `user1…12*` (→1973) and per-element line styles `letRing*` (1975), `palmMute*`, `whammyBar*`, `bend*`, … |
| **TAB / guitar show-flags** | ~2000–2112 | `turnShowTabCommon`, `wahShowTabSimple`, `tabShowTiedFret`, `tabFretPadding`, `chordlineThickness` |
| **Musical-symbol-size per text style** | 2114–2191 | `dummyMusicalSymbolsScale` 1.0 (2106 default), `dummyMusicalSymbolSize` 12.0, `defaultMusicalSymbolSize` 10.0, one `<x>MusicalSymbolSize` per text style |
| **Engine flags + versioning** | 2193–2204 | **`autoplaceEnabled`** true, **`defaultsVersion`** (`Constants::MSC_VERSION`), `changesBeforeBarlineRepeats`, `placeClefsBeforeRepeats`, `harmonyParenUseSmuflSym`, `repeatPlayCountPreset` `N_X`, `systemObjectsBelowBottomStaff` |
| **Line arrows** | ~2206–2281 | `<line>{Begin,End}{Line,Filled}Arrow{Height,Width}` for gradualTempoChange/hairpin/noteLine/ottava/pedal/textLine/palmMute — line arrows 1.0_sp × 0.5_sp, filled arrows 1.0_sp × 0.85_sp |
| *sentinel* | 2282 | `STYLES` (= array size, **not a real style**; loops use `int(Sid::NOSTYLE)+1 … int(Sid::STYLES)`) |

⭐ **Value types in the table are drawn from the `P_TYPE` enum (dozens of kinds)** (`types\propertyvalue.h:41`): `BOOL`, `INT`, `REAL`, `STRING`, `SPATIUM`, `POINT`, `SIZE`, `SCALE`, `COLOR`, `ALIGN`, `ALIGN_H`, `PLACEMENT_V/H`, `DIRECTION_V/H`, `ORIENTATION`, `LINE_TYPE`, `HOOK_TYPE`, `TIE_PLACEMENT`, `TEXT_STYLE`, `AUTO_ON_OFF`, … Many "enums" are stored as **plain `int(Enum::X)`** (`FontStyle`, `FrameType`, `OffsetType`, `ArticulationAnchor`, `LyricsStackingOrder`, `TremoloStyle`, `InstrumentLabelVisibility`) — these are P_TYPE `INT`, not their own type, and are written as ints in XML.

### 1.4 `pagestyle.*` — the page-geometry subset

`pageStyles()` (`style\pagestyle.cpp:27`) returns the 11 Sids that describe a page: `pageWidth, pageHeight, pagePrintableWidth, pageEvenTopMargin, pageEvenBottomMargin, pageEvenLeftMargin, pageOddTopMargin, pageOddBottomMargin, pageOddLeftMargin, pageTwosided, spatium`. `PageSizeGetAccessor` / `PageSizeSetAccessor` (`pagestyle.h:32-84`) are thin `styleD(...)`/`set(...)` wrappers; `Score::pageSize()` hands them out (`score.h:586-587`). Note `twosided()`/`setTwosided()` use `double`/`styleD` although `pageTwosided` defaults to a BOOL (`pagestyle.cpp:96,211`; `styledef.cpp:50`) — a type mismatch (debug assert in `styleD`; release behavior not traced). The same 11 are the set `Score(MasterScore*, …)` **re-borrows from `DefaultStyle::defaultStyle()`** for parts (`score.cpp:208-224`) and the set `DefaultStyle::init` page-size override touches (§4.1).

## 2. How styled properties resolve (Pid ↔ Sid, STYLED flag, default fallback)

⭐ Ties to `engraving-dom.md` §1.3 (property system). The full mechanism, traced:

### 2.1 The three pieces

| Piece | Where | Meaning |
|---|---|---|
| `ElementStyle` = `vector<{Sid,Pid}>` | `styledef.h:2298-2303` | per-**class** static table: "my property `Pid` is driven by style `Sid`" |
| `m_elementStyle` (pointer) + `m_propertyFlagsList` (`PropertyFlags*`) | `engravingobject.h:346-347` | per-**instance**: the table pointer, plus a heap array of one flag per table row (same index) |
| `PropertyFlags { NOSTYLE, UNSTYLED, STYLED }` | `dom\property.h:63` | `STYLED` = tracks style; `UNSTYLED` = user override (has a style but is detached); `NOSTYLE` = this Pid isn't in the table at all |

**Declaring a table** — a file-static `ElementStyle` in the element's `.cpp`, installed in its constructor. 82 such tables across 58 files in `dom\`. Example, `Stem` (`dom\stem.cpp:38-47`):

```cpp
static const ElementStyle stemStyle {
    { Sid::stemWidth, Pid::LINE_WIDTH }
};
Stem::Stem(DummyParentOr<Chord> parent) : EngravingItem(ElementType::STEM, parent)
{
    initElementStyle(&stemStyle);
    resetProperty(Pid::USER_LEN);
}
```

`StaffText` (`dom\stafftext.cpp:32-40`) has `{staffTextPlacement→PLACEMENT}, {staffTextMinDistance→MIN_DISTANCE}` — and the *font/frame/align* come from its **text style** (§3), not this table.

### 2.2 The lifecycle (all `dom\engravingobject.cpp`)

| Step | Function (line) | What it does |
|---|---|---|
| **Install** | `initElementStyle(ss)` (351) | `m_elementStyle = ss`; allocate `PropertyFlags[n]`, **all `STYLED`**; then for each row `setProperty(pid, styleValue(pid, getPropertyStyle(pid)))` — the element's members are initialized from the *current score style*. |
| **Look up** | `getPropertyStyle(pid)` (690) | linear scan of `*m_elementStyle` → the row's `Sid`, else `Sid::NOSTYLE`. `getPropertyFlagsIdx` (virtual, ~636) does the same scan returning the row index. |
| **Flag read/write** | `propertyFlags(pid)` (653) / `setPropertyFlags(pid,f)` (672) | first defer to `propertyDelegate(pid)` if any; `-1` index → `NOSTYLE`. `isStyled(pid)` (399) = `propertyFlags==STYLED`. |
| **Default** | `propertyDefault(pid)` (337) | `Sid s = getPropertyStyle(pid); if (s != NOSTYLE) return styleValue(pid, s); else return PropertyValue()` — *the default of a styled property is the current style value.* Subclasses override and fall through to this (`EngravingItem::propertyDefault` handles `PLACEMENT`/`MIN_DISTANCE` by trying the style first, `engravingitem.cpp:1752-1769`, and defaults to `ownershipParent()->propertyDefault(pid)` for unknown Pids, :1784-1796). |
| **Convert** | `styleValue(pid, sid)` (769) | converts the style value to the **property's unit**: `P_TYPE::ABSOLUTE` → `style().styleAbsolute(sid)` (precomputed absolute length); `P_TYPE::POINT` → `styleV(sid)` × `spatium()` (× staff mag if the item is on a non-system staff) when `offsetIsSpatiumDependent()`, else × `DPMM`; everything else → `styleV(sid)` verbatim. |
| **Reset** | `resetProperty(pid)` (370) | `setProperty(pid, propertyDefault(pid))`; if flag was `UNSTYLED` → back to `STYLED`. `undoResetProperty` (386) is the undoable twin; `reset()` (532) loops the table. |
| **React to a style change** | `styleChanged()` (704) | for each table row **whose flag is `STYLED`**: `setProperty(pid, styleValue(pid, getPropertyStyle(pid)))`. `UNSTYLED` rows are skipped — that is the entire "override survives style change" mechanism. |

### 2.3 Where does the value actually live? (⚠ common misconception)

`EngravingItem::getProperty(pid)` (`engravingitem.cpp:1181`) just returns the **member** (e.g. `m_offset`, `m_placement`); it does **not** consult `MStyle`. "Fallback to style default" really means: (a) at construction `initElementStyle` **copies** style into the member; (b) `styleChanged()` re-copies it for `STYLED` rows; (c) `propertyDefault()`/`resetProperty()` re-derive it from style. The member is therefore a **cache of the style value** until it is flagged `UNSTYLED`. (TRACED: `engravingobject.cpp:351-364, 704-712`.)

### 2.4 How a property becomes `UNSTYLED`

- **User edit through the inspector** — `PropertiesPanelAbstractModel` reads the flag and, if `STYLED`, passes `UNSTYLED` into `item->undoChangeProperty(pid, value, ps)` (`propertiespanel\qml\…\propertiespanelabstractmodel.cpp:503-510`). It also reports "modified" when `flag == UNSTYLED || current != default` (`:861-870`).
- **`undoChangeProperty(pid, v, ps)`** (`engravingobject.cpp:479`) → `changeProperties` → `changeProperty` → `ChangeProperty` undo command (`editing\editproperty.cpp:39`), whose `flip()` swaps **both** value and `PropertyFlags` (`setProperty` + `setPropertyFlags`). The single-arg overload keeps the current flag (`:474`); `undoResetProperty` forces `UNSTYLED→STYLED` (`:386`).
- **Reading a file** — `TRead::readProperty` (`rw\read500\tread.cpp:532-537`): after `item->setProperty(pid, v)`, `if (item->isStyled(pid)) item->setPropertyFlags(pid, UNSTYLED)`. **Presence in the XML ⇒ override.** Absence ⇒ stays `STYLED` and will follow the style.
- **Writing a file** — `TWrite::writeProperty` (`rw\write\twrite.cpp:402-406`): `if (item->isStyled(pid)) return;` — **STYLED properties are never written.** `writeStyledProperties` (`:644`) loops the item's table through `writeProperty`; so only `UNSTYLED` ones (and non-default `NOSTYLE` ones) hit disk. Also: `ABSOLUTE` props are written as `Spatium` and skipped if within 0.0001 of the default (`:432-439`); `FONT_STYLE` is written as separate `bold/italic/underline/strike` tags, only where it differs from default (`:414-430`).
- **Changing `TEXT_STYLE`** — special-case in `undoChangeProperty` (`engravingobject.cpp:484-495`): for every row of the new text style's table, change the property to `style().styleV(p.sid)` with flag `STYLED` (so switching text style *discards* overrides of those 16 props). Note the loop `break`s on `p.sid == Sid::NOSTYLE` — but see the 15-row-table gotcha (§6).

### 2.5 Whole-score style change

`Score::styleChanged()` (`dom\score.cpp:1089`): `scanElements(updateStyle)` where `updateStyle` calls `e->styleChanged()` while **preserving the `generated` flag** (`:1077-1082`), then every `StaffType::styleChanged()`, `updatePaddingTables()`, `setLayoutAll()`. "Must be called after every style change" (comment, `:1086`) — the undo commands do (§3.3). Layout reads style straight from the score (`score->style()`, or `ctx.conf().styleD(...)` in `rendering\score`, e.g. `systemheaderlayout.cpp:366`) and from the baked element members — see `layout-and-rendering.md`.

## 3. Text styles (`TextStyleType` / `TextStyle` tables)

### 3.1 The enum and the tables

- `enum class TextStyleType : unsigned char` (`types\types.h:970-1063`): `DEFAULT`; **page** (`TITLE, SUBTITLE, COMPOSER, LYRICIST, TRANSLATOR, FRAME, INSTRUMENT_EXCERPT, INSTRUMENT_LONG, INSTRUMENT_SHORT, INSTRUMENT_CHANGE, GROUP_BRACKET, HEADER, FOOTER, COPYRIGHT, PAGE_NUMBER`); **measure** (`MEASURE_NUMBER, MEASURE_NUMBER_ALTERNATE, MMREST_RANGE`); **system** (`TEMPO, TEMPO_CHANGE, METRONOME, REPEAT_PLAY_COUNT, REPEAT_LEFT, REPEAT_RIGHT, REHEARSAL_MARK, SYSTEM`); **staff** (`STAFF, STAVE_SHARING, EXPRESSION, DYNAMICS, HAIRPIN, LYRICS_ODD, LYRICS_EVEN, HARMONY_A, HARMONY_B, HARMONY_ROMAN, HARMONY_NASHVILLE`); **note** (`TUPLET, ARTICULATION, STICKING, FINGERING, TAB_FRET_NUMBER, LH/RH_GUITAR_FINGERING, HAMMER_ON_PULL_OFF, STRING_NUMBER, STRING_TUNINGS, FRET_DIAGRAM_FINGERING/_FRET_NUMBER, HARP_PEDAL_DIAGRAM, HARP_PEDAL_TEXT_DIAGRAM`); **line** (`TEXTLINE, SYSTEM_TEXTLINE, NOTELINE, VOLTA, OTTAVA, GLISSANDO, PEDAL, BEND, LET_RING, WHAMMY_BAR, PALM_MUTE`); **`USER1…USER12`**; sentinels `TEXT_TYPES` (=75) and `IGNORED_TYPES`. **75 real styles** (`DEFAULT` + 74).
- Each style is one `const TextStyle` in `style\textstyle.cpp` (75 tables, lines 30–1678), looked up by the big `switch` in `textStyle(TextStyleType)` (`:1680-1772`; the two sentinels return `nullptr`, then `UNREACHABLE`). **Add a text style = new enum value + a `TextStyle` table + a `case` + a family of Sids + a row in the user-visible lists.**
- `struct TextStyle` (`textstyle.h:62`): `std::array<TextStyleProperty, 16> properties` + `OffsetSids offsetSids {above, below}`. `TEXT_STYLE_SIZE = 16` (`:60`). `TextStyleProperty = {TextStylePropertyType type; Sid sid; Pid pid;}`. The 16 slots are, in order for most styles: `FontFace→FONT_FACE`, `FontSize→FONT_SIZE`, `LineSpacing→TEXT_LINE_SPACING`, `SizeSpatiumDependent→SIZE_SPATIUM_DEPENDENT`, `FontStyle→FONT_STYLE`, `Color→COLOR`, `TextAlign→ALIGN`, `FrameType→FRAME_TYPE`, `FramePadding→FRAME_PADDING`, `FrameWidth→FRAME_WIDTH`, `FrameRound→FRAME_ROUND`, `FrameBorderColor→FRAME_FG_COLOR`, `FrameFillColor→FRAME_BG_COLOR`, `MusicalSymbolsScale→MUSICAL_SYMBOLS_SCALE`, `MusicalSymbolsSize→MUSIC_SYMBOL_SIZE`, `Position→POSITION`. (`defaultTextStyle`, `textstyle.cpp:30-50`.)
- **`offsetSids`** gives the Sid(s) for the style's default **offset**, one for "above" one for "below": a plain style repeats one Sid (`{Sid::titleOffset, Sid::titleOffset}`); placement-aware ones differ (`lyricsOdd/Even` → `{lyricsPosAbove, lyricsPosBelow}`, `stringTunings` → `{staffTextPosAbove, staffTextPosBelow}`). `TextBase::defaultPosSid()` picks `placeAbove() ? above : below` (`dom\textbase.cpp:2835`).
- **Sid sharing between styles** — styles may alias another style's Sids: `stringTuningsStyle` reuses `staffText*` sids except font size (`textstyle.cpp:316-336`); **`dynamicsTextStyle` is wired to the `expression*` Sids** (comment: *"after 4.1 Dynamics use Expression text style"*, `:537-544`), which is why `editableTextStyles()` removes `DYNAMICS` (`:1854`); `groupBracketTextStyle` takes its `Position` from **`longInstrumentPosition`** (`:532`).
- Accessors: `allTextStyles()` (74: `DEFAULT+1 … TEXT_TYPES-1`, lazily built, `:1828`), `primaryTextStyles()` (a fixed 45-entry list incl. all 12 user styles, `:1776-1822` — the ones shown first in the text-style UI), `editableTextStyles()` (= all − `DYNAMICS`, 73).
- User styles: `Sid::userNName` (STRING, default `""`); `Score::getTextStyleUserName(tid)` returns the user's name for `USER1..12` if non-empty else `TConv::userName(tid)` (`score.cpp:4678-4690`).

### 3.2 How `TextBase` picks up a text style

`TextBase` (`dom\textbase.cpp`) extends the property machinery to look in **two** tables, element table first, then the text-style table:

| Override | Line | Behavior |
|---|---|---|
| ctor | 1553 | `TextBase(type, parent, TextStyleType tid, flags)` stores `m_textStyleType = tid` (hard defaults for frame/align set, then overwritten when the subclass calls `initElementStyle`). |
| `initElementStyle(ss)` | 3009 | flag array sized `ss->size() + TEXT_STYLE_SIZE` (**+16**), all `STYLED`; sets every property from **both** tables. |
| `getPropertyFlagsIdx(id)` | 2813 | index = position in `*m_elementStyle`, **continuing** into `*textStyle(textStyleType())` (`++i` carried across). |
| `getPropertyStyle(id)` | 2963 | element table, then text-style table, else `NOSTYLE`. |
| `styleChanged()` | 2982 | like the base, but iterates both tables with a running `i` over `m_propertyFlagsList`. |
| `propertyDefault(id)` | 2765 | `Z` → base; composition parent's default if valid; **`getPropertyStyle`→`styleValue`**; hard-coded fallbacks for `TEXT_STYLE`(`DEFAULT`), `TEXT`, `DIRECTION`, …; finally the **`DEFAULT` text style's** row for the Pid, then `EngravingItem::propertyDefault`. |
| `setProperty(Pid::TEXT_STYLE, v)` | 2682 | → `initTextStyleType(v)`. |
| `initTextStyleType(tid)` | 3060 | `setTextStyleType(tid)`, then for each row `setProperty(pid', styleValue(pid, sid))` (with a textline remap `FONT_FACE→BEGIN_FONT_FACE` etc. when the parent is a `TextLineBaseSegment`, `:3062-3081`). |
| `initTextStyleType(tid, preserveDifferent)` | 3031 | used when a style is *applied over existing text*: keeps custom face/size/style if any fragment has custom formatting, and only overwrites props still equal to their `propertyDefault`. |

⭐ Consequence: a text element has **per-Pid `STYLED/UNSTYLED` flags** exactly like any element, so "Text style = Title but I made *this* title red" is `COLOR == UNSTYLED`; change the score's `titleColor` and only the un-overridden ones follow.

## 4. Style read/write, defaults, and the `.mss` files

### 4.1 `DefaultStyle` — three builtin `MStyle`s (`style\defaultstyle.h/.cpp`)

| Member | Meaning | Source |
|---|---|---|
| `m_baseStyle` | "builtin initial style": a **never-modified, default-constructed** `MStyle` → every value falls back to the `styleValues` table. | `defaultstyle.h:49` |
| `m_defaultStyle` | "builtin modified by preferences": starts as base, then `applyPageSizeToStyle(page size)` (A4 or Letter — `EngravingConfiguration::defaultPageSize` tries the `PAPERSIZE` env var first, then other detection; see `engravingconfiguration.cpp:220-256`), then **loaded over** by the user's *default style file* setting (`engraving/style/defaultStyleFile`), then `precomputeValues()`. | `defaultstyle.cpp:60-75` |
| `m_defaultStyleForParts` | optional; only when the user set a *part style file* (`engraving/style/partStyleFile`). `nullptr` on load failure. | `defaultstyle.cpp:77-90` |

`DefaultStyle::init` is called once from `EngravingModule` after the engraving config is initialized (`engravingmodule.cpp:254`). Preferences UI: `preferences\…\DefaultStyleSection.qml` (default + part style `.mss` pickers).

**Who gets which:**
- `new Score(iocCtx)` → `m_style = DefaultStyle::defaultStyle()` (`score.cpp:182`); `MasterScore` creation via `compat\scoreaccess.cpp:40` uses `baseStyle()` (ref-score for reading files) and `:46` uses `defaultStyle()` (new user scores).
- A **part** score (`Score(MasterScore*, forcePartStyle)`) takes `*defaultStyleForParts()` if set; **otherwise** inherits `parent->style()`, re-borrows the 11 **page Sids** from `defaultStyle()`, and (if `forcePartStyle`) forces `concertPitch=false`, `createMultiMeasureRests=true`, `dividerLeft/Right=false` (`score.cpp:196-239`). Then `precomputeValues()`.
- `Score::resetStyleValue(s)` / `resetStyleValues(set)` reset to **`DefaultStyle::defaultStyle()`** values (user-preference-aware) through the undoable path (`score.cpp:4630-4650`); `Reset::resetAllStyles` loops `NOSTYLE+1 … STYLES` minus an exception set (`editing\reset.cpp:235`). The plugin `Style.resetValue` does the same (`api\v1\style.cpp:88`).

### 4.2 Legacy defaults and `defaultsVersion`

Opening an older score must not silently adopt *today's* defaults. `ReadStyleHook::setupDefaultStyle()` (`rw\compat\readstyle.cpp:88`) chooses a defaults version — for a master: scan the score XML for a `defaultsVersion` tag, else `styleDefaultByMscVersion(mscVersion)` (`:62-86`: `<=114`→114, `≤206`→206, `≤301`→301, `302–399`→302, else current 500; note there is **no 450 bucket here**) — then `score->setStyle(DefaultStyle::resolveStyleDefaults(v))` and `setDefaultStyleVersion(v)`. For a part: inherits the master's `defaultStyleVersion()`.

`DefaultStyle::resolveStyleDefaults(v)` (`defaultstyle.cpp:121-161`): `switch` on 450 / 302 / 301 / 206 / 114 → a **lazily-loaded static `MStyle`** read from an embedded resource (`:/engraving/styles/legacy-style-defaults-v{450,302,3,2,1}.mss`); anything else → `baseStyle()`. Files on disk: `src\engraving\data\styles\legacy-style-defaults-v1/v2/v3/v302/v450.mss`.

⚠ **`legacy-style-defaults-v450.mss` is NOT registered in `engraving.qrc`** (the qrc lists v1, v2, v3, v302 only — `engraving.qrc:6-11`; a repo-wide grep finds the v450 name only at `defaultstyle.cpp:42`). **TRACED:** the path `:/engraving/styles/legacy-style-defaults-v450.mss` has no resource entry. **HYPOTHESIS (not run):** `resolveStyleDefaults(450)` therefore fails `doLoadStyle` (`LOGE "failed load style"`), returns an all-invalid static style (⇒ plain `StyleDef` defaults) and — because `loaded_flag` stays false — retries and re-logs on every call. Whether 4.5 scores hit this depends on whether a 4.5 file writes `defaultsVersion=450`; UNCLEAR (§7). Surface to the user before relying on it.

### 4.3 `MStyle` XML — read

Entry points (all `style\style.cpp`):
- `bool read(IODevice*, bool ign=false)` (423): `<museScore version="X.YY">` → version digits → int (`"4.60"`→`460`) → find `<Style>` → private `read(XmlReader&, hook, mscVersion)`. (`ign` is `UNUSED`.) `isValid(device)` (443) = has `museScore/Style`.
- Private `read(e, readChordListHook, mscVersion)` (458): loop over child tags; a long `else if` ladder of **back-compat renames/typos/obsolete tags** (pre-4.4 `system*`/`staff*` text-style tags → `systemText*`/`staffText*`; `ArpeggioNoteDistance`→`arpeggioNoteDistance`; slur width tags copy into the matching tie sids when `<430`; `measureNumberHPlacement` PlacementH→AlignH `<460`; `useGermanNoteNames` etc. collapsed into `chordSymbolSpelling`; `beamDistance`→`useWideBeams`; `Spatium`/`spatium` ×`DPMM`; …), ending in **`readProperties(e)`** else `e.unknown()`. Then `applyCompatStyleVals(mscVersion)` and `readChordListHook->validate()`.
- **`readProperties`** (120–270): linear scan of `StyleDef::styleValues` for `xmlName == tag`, then a `switch (t.valueType())` that reads the right shape per type (`SPATIUM`→`Spatium(readDouble)`, `POINT`→`x/y` attrs, `COLOR`→`r/g/b/a` attrs, `ALIGN`→`TConv::fromXml`, …). **A new `P_TYPE` used by a Sid needs a `case` here** or it hits `ASSERT_X("unhandled type")` (`:261`). Falls back to `readStyleValCompat` (`tempoOffset`; and `readTextStyleValCompat` merging legacy `…FontBold/Italic/Underline/Strike` booleans into `…FontStyle` flags, `:303-344`).
- **`applyCompatStyleVals(mscVersion)`** (346-421): version-gated fixups after load, e.g. `<500`: `windsNameByGroup/vocalsNameByGroup/maskSlurs/maskTies=false`, `dashBarWidth=barWidth`, `lyricsStackingOrder=LYRICS_AFTER_PEDALS`; `<470`: `compat::CompatUtils::setMusicSymbolSize470`, `setPositionStylesFromAlign`; `<460`: `verticallyAlignChordSymbols` derived, hook-height sign fixes, `repeatPlayCountShow=false`; `==450`: `spacingDensity=1.30`; `<450`: new courtesy/mask flags `false`; `<420 && !testMode`: `tiePlacementChord=INSIDE`. **When you add a Sid whose new default would change old scores, add a gated line here** (and/or a legacy-defaults entry).

### 4.4 `MStyle` XML — write

- `write(IODevice*)` (742): `<museScore version="${Constants::MSC_VERSION_STR}">` + `save(xml, false)` — the **full** style (every Sid).
- `save(XmlWriter&, bool optimize)` (752-828): `<Style>`; skips `Sid::spatium` in the loop; with `optimize`, skips sids where `isDefault(sid)` (and `ALIGN` equal to the table default); writes by type (`Spatium` as bare double, enums via `TConv::toXml`, `DirectionV` as int, `LINE_TYPE` via `tagProperty`, other enums coerced to int for compat); **finishes with `<spatium>` = `spatium / DPMM`** (mm) at the end.
- Who calls which: **`.mscz` saving** writes the master style as the full `score_style.mss` (`rw\mscsaver.cpp:69-70`, `MscWriter` file name `score_style.mss`, `infrastructure\mscwriter.cpp:137`) and each part as `<excerpt>.mss` (`mscsaver.cpp:112, 206-208`; `mscwriter.cpp:166`). `save(xml, true)` (diff-only, inline in the `.mscx` `<Style>`) is only used by `WriteScoreHook::onWriteStyle302` for **non-master scores** and the test-mode path (`compat\writescorehook.cpp:35-52`). **Standalone user `.mss` export** is `NotationStyle::saveStyle` → `score()->style().write(&file)` (`notation\internal\notationstyle.cpp:134-141`; appends `.mss` if no suffix) = full style.
- **Loading** a `.mscz`: `MscLoader` reads `score_style.mss` into the master's style (`rw\mscloader.cpp:90-93`) *before* the score body, then `ReadStyleHook::setupDefaultStyle` / the `<Style>` tag in the body (`read500.cpp:152` → `ReadStyleHook::readStyleTag` → `score->style().read(e, &ChordListHook, score->mscVersion())`) and per-excerpt `partScore->style().read(&excerptStyleBuf)` (`mscloader.cpp:162-170`). Details of the container/versioned readers: `file-format-and-rw.md`.

### 4.5 Changing style at runtime (undo commands) — `editing\editstyle.*`

| API | What | Notes |
|---|---|---|
| `Score::undoChangeStyleVal(Sid, v)` / `undoChangeStyleValues(map)` | push `ChangeStyleValues` (`edit.cpp:3454-3465`) | the normal single/bulk setter (UI, plugin `setValue`, `NotationStyle::setStyleValue`) |
| `ChangeStyleValues::flip()` | `editstyle.cpp:141` | for each `(sid,value)`: if different, `changeStyleValue` → `style.set`, then **per-Sid side-effects** (`changeStyleValue`, `:108-139`): chord-list Sids (`chordExtensionMag…musicalTextFont`) → `changeChordStyle`; **`spatium` → `score->spatiumChanged(old,new)`**; `defaultsVersion` → `setDefaultStyleVersion`; `createMultiMeasureRests` true→false → remove system/page locks containing MM rests. Swaps old/new (`pair.second = oldValue`) so the same command undoes. Calls **`score->styleChanged()` once** at the end if anything changed. |
| `ChangeStyle(score, MStyle, overlap)` | whole-style replacement (loading a `.mss`, apply-to-parts) | `flip()` (`:78`): handles `concertPitch` change (`cmdConcertPitchChanged`), `musicalSymbolFont` change (`setEngravingFont`), `score->setStyle(style, overlap)`, `changeChordStyle`, `spatiumChanged`, `styleChanged`, then swaps. `changedIds()` diffs. `overlap=true` applies only the **non-default** sids of the incoming style (`Score::setStyle`, `score.cpp:4656-4672`) — "overlay". `undo()` forces `overlap=false` so undo restores fully. |
| `EditStyle::loadStyle(tx, score, dev, ign, overlap)` | `editstyle.cpp:171` | reads a `.mss` into a **copy** of the score style (so unspecified sids keep current values), then pushes `ChangeStyle`. `NotationStyle::loadStyle`/`applyToAllParts` wrap it (`notationstyle.cpp:93-132`). |

⭐ The notation layer fronts this with `INotationStyle` (`src\notation\inotationstyle.h` → `notationstyle.cpp`): `styleValue`, `defaultStyleValue` (= `DefaultStyle::defaultStyle().value`), `setStyleValue` (special-cases `concertPitch` → `cmdConcertPitchChanged`; else `undoChangeStyleVal` + `score()->update()` + a `styleChanged` notification), `resetStyleValue(s)`, `resetAllStyleValues`, `loadStyle`/`saveStyle`, `canApplyToAllParts`/`applyToAllParts` (parts only). See `notation-layer.md`.

### 4.6 The `.mss` files — what ships where

**`share\styles\`** (installed to `${MUSE_APP_INSTALL_RESOURCES_LOCATION}/styles`, `share\styles\CMakeLists.txt:21-26`) — user-selectable styles:

| File | Notes |
|---|---|
| `MuseJazz.mss` | the jazz-look style (contents not read in this pass) |
| `MSN\16mm_MSN.mss`, `18mm_`, `20mm_`, `22mm_`, `25mm_MSN.mss` | **Modified Stave Notation** (RNIB) styles at five staff heights. `MSN\README.md` states the conventions: tags in MuseScore's **writer order**, each tag 0–1 times, **all MSN files carry identical tag sets**, omit tags that are same-everywhere *and* equal MuseScore's default; keep intrinsic values even if they match the default. (`share\styles\CMakeLists.txt:26` `add_subdirectory(MSN)`; `MSN\CMakeLists.txt` not read.) |

**`src\engraving\data\styles\`** (compiled into the binary via `engraving.qrc` → `:/engraving/styles/…`) — internal, not user-listed:

| Resource | Use |
|---|---|
| `legacy-style-defaults-v1/v2/v3/v302.mss` (+ `v450.mss` on disk, **unregistered**, §4.2) | per-era defaults for opening old scores (`DefaultStyle::resolveStyleDefaults`) |
| `migration-306-style-Leland.mss`, `migration-306-style-Edwin.mss` | applied by `project\internal\projectmigrator.cpp:50-51` when migrating 3.x scores to the 4.x look (Leland vs Edwin choice) |
| `gp-style.mss` | applied by the Guitar Pro importer (`importexport\guitarpro\internal\guitarproreader.cpp:45`) |

Test data: `src\engraving\tests\**\*.mss` (score-style fixtures), `vtest\small.mss`.

## 5. Cross-subsystem dependencies

- **`engraving-dom.md`** — property system (`Pid`, `PropertyValue`, `PropertyFlags`, `EngravingObject::getProperty/setProperty/propertyDefault`); every element's `ElementStyle`. `Score::style()` / `m_style` (`score.h:583, 979`).
- **`layout-and-rendering.md`** — layout reads style by `Sid` constantly (`score->style().styleS/styleD/styleB`, `ctx.conf()`); `Score::styleChanged` ends in `setLayoutAll`; `updatePaddingTables` rebuilds from style.
- **`file-format-and-rw.md`** — `score_style.mss`/`<part>.mss` in the `.mscz` container (`MscReader/MscWriter`), `rw\compat\readstyle.*`, versioned readers (`read114…read500`) which set `UNSTYLED` on every explicitly-present property; `Constants::MSC_VERSION`.
- **`notation-layer.md`** — `INotationStyle`/`NotationStyle`, style dialog models, text-style editing, apply-to-parts.
- **`plugin-api.md`** — `Score.style` → `apiv1::MStyle` (`value/setValue/resetValue`, key = `Sid` name via `Q_ENUM_NS`); `Engraving.Sid` enum is the plugin's key list.
- **`mscore-cli.md`** — `-S/--style` loads a `.mss` into the converted score.
- **`ui-qml-mvvm.md`** — Preferences `DefaultStyleSection.qml`, the Style dialog (`src\notationscene`/`src\appshell`), properties panel (`src\propertiespanel`) which sets `UNSTYLED`.

## 6. Non-obvious gotchas

- ⛔ **Adding a Sid = coordinated edits, and ORDER matters.** (1) add the enumerator in `styledef.h` **at the exact position** you will put the row, (2) add `styleDef(name, default)` at the **same position** in `styledef.cpp` (positional array, no order check), (3) if its type is a new `P_TYPE`, add a `case` in `MStyle::readProperties` (and `save` if it needs bespoke writing), (4) if old scores must keep old behavior, add an `applyCompatStyleVals` gate and/or edit the `legacy-style-defaults-*.mss`, (5) wire an `ElementStyle` row `{Sid::x, Pid::Y}` (or a text-style row) in the consuming class, (6) UI (properties panel / style dialog). The enumerator **name is the XML tag and the plugin key** — renaming it breaks files and plugins (use a compat rename in `style.cpp:472+` as the existing typos do).
- **Positional table, no assertion** (§1.2): inserting a `styleDef` row in the wrong place shifts every later default silently.
- **Defaults are *per defaultsVersion*, not "current".** `isDefault` and `save(optimize=true)` compare against `resolveStyleDefaults(defaultStyleVersion())` (`style.cpp:105-108`).
- **v450 legacy defaults not in the qrc** (§4.2) — TRACED qrc gap, HYPOTHESIZED runtime effect.
- **`MStyle::set` has no notification** — only the undo commands call `Score::styleChanged()`. A raw `style().set(...)` leaves items stale until something else triggers `styleChanged`/layout (`twrite.cpp:655-659` does exactly this transiently to write AUTOPLACE, then restores).
- **`styleI` has no type assert; other typed getters assert only in debug.** Use `styleS` for staff-space values and `styleAbsolute` for the absolute length (`styleS`×spatium); `styleD` is asserted `REAL` only, so pairing it with a `SPATIUM` Sid is a debug assert (release behavior depends on `PropertyValue::toReal` — not traced). `styleAbsolute`/`valueAbsolute` read `m_precomputedValues`, **valid only after `set`/`precomputeValues`** (a fresh `MStyle` that has only fallen back to defaults has indeterminate precomputed doubles; `DefaultStyle::init` explicitly calls `precomputeValues()` on each builtin, `defaultstyle.cpp:62,74,88`, and `Score(MasterScore*)` does too, `score.cpp:234`). HYPOTHESIS for the indeterminate-value claim (default-init of `std::array<double>` in a non-static `MStyle`); not run.
- **`STYLED` ≠ "equals default".** An `UNSTYLED` property whose value happens to equal the style default is still written to the file and still ignores later style changes; conversely reading a file marks any *present* styled property `UNSTYLED`. Use `isStyled(pid)` for "is it tracking", `getProperty != propertyDefault` for "differs".
- **Six of the 75 `TextStyle` tables have only 15 initializers** (`TextStylePropertyType::Position` appears in 69 of 75 tables — `articulation`, `tabFretNumber`, `hammerOnPullOff`, `stringTunings`, `fretDiagramFretNumber`, … omit it; e.g. `articulationTextStyle`, `textstyle.cpp:406-425`). The array is 16 long, so the 16th element is **value-initialized = `{Undefined, Sid(0)=pageWidth, Pid(0)=SUBTYPE}`**, not `NOSTYLE`. Loops over `*textStyle(tid)` (`initTextStyleType`, `TextBase::styleChanged`, the `TEXT_STYLE` branch of `undoChangeProperty` which breaks on `p.sid == NOSTYLE`) will visit that padding row. TRACED from C++ aggregate-init rules + the table text; whether it has an observable effect is UNCLEAR (§7). Consider it when adding or editing text-style tables.
- **`TextBase` flag index spans two tables.** `m_propertyFlagsList` has `elementStyle.size() + 16` slots and `getPropertyFlagsIdx` continues the counter across both (`textbase.cpp:2813-2829`). Any subclass that overrides one of the three (`getPropertyFlagsIdx`/`styleChanged`/`initElementStyle`) without the others will mis-index flags.
- **Switching `TEXT_STYLE` resets overrides** of that style's 16 properties to `STYLED` (`engravingobject.cpp:484-495`); use `initTextStyleType(tid, true)` (`preserveDifferent`) to keep custom face/size/style fragments.
- **`dynamicsTextStyle` aliases `expression*`** — editing "Dynamics" font in the UI actually edits Expression sids; `DYNAMICS` is excluded from `editableTextStyles()`.
- **Parts have their own style.** Changing the master's style does not touch parts; `NotationStyle::applyToAllParts` is explicitly parts-only (`canApplyToAllParts` = `!isMaster`).
- **`spatium` is stored internally in DPI-based units** (`1.75 * DPMM`) but written to XML in **mm** (`save`: `value(spatium)/DPMM`; read: `readDouble() * DPMM`, `style.cpp:472-473, 826`). `spatium` is never written inside the main loop (`:758`).
- **Pre-4.4 typo'd tags are still accepted** (e.g. `lyricsDashMaxLegth`, `dontHidStavesInFirstSystm`, `minSpreadSpread`) — do not "clean up" these `else if` branches; they exist so old files load.
- **`ChangeStyleValues` is the only path with per-Sid side effects.** If you add a Sid that needs a recompute on change (like the chord list or `spatium`), hook it in `changeStyleValue`, not in the UI model.

## 7. UNCLEAR — needs verification

- Whether any shipped/old score actually produces `defaultsVersion == 450` (so the unregistered `legacy-style-defaults-v450.mss` is hit) — and the real runtime behavior of `resolveStyleDefaults(450)` (not run). Also whether `styleDefaultByMscVersion` intentionally has no 450 bucket.
- Observable effect (if any) of the value-initialized 16th row in the six 15-entry `TextStyle` tables (§6).
- Whether `m_precomputedValues` of a stack-constructed `MStyle` is zero or indeterminate in practice (affects `styleAbsolute` before `precomputeValues`).
- Exact Sid line ranges for the middle of the enum (~tempo/measure-number/sticking/guitar-line families, lines ~1076–1960 and ~2000–2112) — the table above gives anchors verified at specific Sids but not a per-family boundary for every line; per-Sid default values for the long tail were not tabulated (the table lives in `styledef.cpp:41–2262`; read it directly).
- The style **dialog/UI layer** (`src\notationscene` page models, `src\appshell` style dialogs) and `Reset::resetTextStyleOverrides` / the text-style editing UI were not traced.
- `readStyleDefaultsVersion` scans the whole score XML for any element named `defaultsVersion` (`readstyle.cpp:42-55`) — behavior for scores whose style lives only in `score_style.mss` (4.x+ containers) vs inline `<Style>` was not separately verified.

*Authored from: whole-file reads of `src\engraving\style\{style.h,style.cpp,defaultstyle.h,defaultstyle.cpp,pagestyle.h,pagestyle.cpp,textstyle.h}`, `textstyle.cpp` (tables sampled + the whole lookup/accessor tail + counts), `styledef.h` (header/footer, the `Sid` skeleton by region, tail, `StyleDef`), `styledef.cpp` (head + defaults spot-checks + tail; counts), plus targeted reads of `dom\engravingobject.{h,cpp}`, `dom\textbase.cpp`, `dom\engravingitem.cpp`, `dom\score.cpp`, `editing\editstyle.*`, `editing\edit.cpp`, `editing\editproperty.cpp`, `editing\reset.cpp`, `rw\compat\readstyle.cpp`, `rw\mscloader.cpp`, `rw\mscsaver.cpp`, `rw\write\twrite.cpp`, `rw\read500\tread.cpp`, `api\v1\style.*`, `notation\internal\notationstyle.cpp`, `engraving.qrc`, and `share\styles\` (2026-11-08). **If this contradicts the live source, the source wins.***
