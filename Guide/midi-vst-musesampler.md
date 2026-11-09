# MuseScore — MIDI, VST, MuseSampler & MPE

> **What this file is.** Deep pass (2026-11-08) over `muse\framework\mpe`, `midi`, `midiremote`, `vst`, `musesampler` (plus the `FluidSequencer` as the reference MPE consumer). **Live source wins** over this Guide. Start at `00-core.md`. The overall audio pipeline (engine, mixer, `ISynthResolver`, tracks, threads) is owned by `playback-and-audio.md`; the engraving to MPE conversion (`src\engraving\playback`) is owned by that file / `engraving-dom.md`. This file owns the **MPE event model, MIDI model + ports + remote, the VST3 host, and the MuseSampler binding**. Cites are relative to `C:\Dev\+MuseScore+`.

## 0. 60-second orientation

- **MPE (Muse Playback Events)** = the lingua franca. `README`: "our own event format with the utmost precision ... without losing compatibility with MIDI and VST" (`muse\framework\mpe\README.md`). Engraving renders a score into `mpe::PlaybackEventsMap` (`timestamp (us) -> list of PlaybackEvent`) plus per-layer dynamic automation curves; **every synth (Fluid, VSTi, MuseSampler) re-encodes those MPE events into its own native protocol** inside its own `*Sequencer` (`AbstractEventSequencer` subclass).
- **MIDI** (`muse\framework\midi`) is *only* an I/O + data-type module: UMP-based `midi::Event`, in/out hardware ports (WinMM / ALSA / CoreMIDI / dummy), config. File import/export of `.mid` is `src\importexport\midi`, not here. Playback does **not** go through MIDI; the FluidSynth path builds `midi::Event`s from MPE.
- **MIDI remote** (`midiremote`) maps incoming MIDI notes/CCs to rcommand commands and decodes MMC SysEx transport.
- **VST3 host** (`vst`): scan/meta-read (`VstPluginsScanner`/`VstPluginMetaReader`), module cache (`VstModulesRepository`), per-use instances (`VstPluginInstance` + `VstInstancesRegister`), the realtime `VstAudioClient`, the instrument synth `VstSynthesiser` + `VstSequencer`, the FX `VstFxProcessor`, editor UI (`VstView` QML / `VstViewDialog` QWidget). Steinberg SDK is built as `vst3sdk` (`vst\sdk\CMakeLists.txt`).
- **MuseSampler**: *not* built in. It is an **external proprietary shared library** (`MuseSamplerCoreLib.dll/.so/.dylib`) `dlopen`ed at startup via a hand-rolled C-API table (`MuseSamplerLibHandler`). `MuseSamplerResolver` enumerates its instruments; `MuseSamplerWrapper` is the `ISynthesizer`; `MuseSamplerSequencer` maps MPE to the C API.

Key files: `mpe\mpetypes.h`, `mpe\events.h`, `midi\midievent.h`, `vst\internal\vstaudioclient.cpp`, `vst\internal\synth\vstsequencer.cpp`, `musesampler\internal\libhandler.h`, `musesampler\internal\musesamplersequencer.cpp`.

## 1. Build / wiring / module registration

| Module | Option (default) | Registered exports | Notes |
|---|---|---|---|
| mpe | `MUSE_MODULE_MPE` ON (`muse\framework\cmake\MuseDeclareOptions.cmake:100`) | `IArticulationProfilesRepository` (global) `mpemodule.cpp:38-43` | OFF in VTEST config (`SetupConfigure.cmake:221`) |
| midi | `MUSE_MODULE_MIDI` ON (`:98`) | `IMidiConfiguration`, `IMidiOutPort`, `IMidiInPort` (`midimodule.cpp:48-69`) | per-OS port impl chosen at compile time: ALSA (Linux/FreeBSD), WinMM (Win), CoreMIDI (macOS), else Dummy. `init()` of ports only for `RunMode::GuiApp` (`:75-78`) |
| midiremote | `MUSE_MODULE_MIDIREMOTE` ON (`:99`) | global `IMidiRemoteConfiguration`, `IMMCDecoderFactory`; **per-context** `IMidiRemote` (`midiremotemodule.cpp:45-81`) | mappings are per window context, file shared |
| vst | `MUSE_MODULE_VST` **OFF** (`:128`) | `IVstConfiguration`, `IVstModulesRepository`, `IVstInstancesRegister`; in `resolveImports` registers `VstiResolver` (`AudioSourceType::Vsti`), `VstFxResolver` (`AudioFxType::VstFx`), scanner + meta-reader into `audioplugins` registers, commands (`vstmodule.cpp:59-96`) | `ninja_build.sh:48` defaults `MUSESCORE_BUILD_VST_MODULE=OFF`; CI scripts set ON (`buildscripts\ci\*\build.*`). Pulls `vst3sdk` source dep (`muse\buildscripts\cmake\ExtDepsManifest.cmake:3-5`) |
| musesampler | `MUSE_MODULE_MUSESAMPLER` ON (`:105`), forced OFF if no `MUSE_MODULE_AUDIO` (`MuseSetupConfiguration.cmake:3-5`) | `IMuseSamplerConfiguration`, `IMuseSamplerInfo` (= the resolver); registers resolver as `AudioSourceType::MuseSampler` (`musesamplermodule.cpp:53-74`) | **not even added in debug builds** unless `MUSE_MODULE_MUSESAMPLER_LOAD_IN_DEBUG` (`src\app\appfactory.cpp:333-345` and `473-485`) |

Config presets that switch these off: `APP-WEB` turns MIDI, MIDIREMOTE, MUSESAMPLER, VST, AUDIOPLUGINS off (`SetupConfigure.cmake:150-158`); `VTEST` turns MIDI, MIDIREMOTE, MPE, MUSESAMPLER, VST off (`:219-232`); `UTEST` turns MUSESAMPLER + VST off unless code coverage (`:297,300`). When a module is off, `muse\framework\stubs\*` provides stub exports (`stubs\CMakeLists.txt:53-89`).

**VST3 SDK source**: recipe `muse_deps\recipes\vst3sdk\spec.cmake` pins version `3.7.12_build_20` from three `steinbergmedia` GitHub tarballs (`vst3_base`, `vst3_pluginterfaces`, `vst3_public_sdk`); `meta.cmake` also supports "system" mode, searching `$VST3SDK`, `/usr/src/vst3sdk`, `/usr/share/vst3sdk` for `pluginterfaces/base/funknown.h`. `vst\sdk\CMakeLists.txt` hand-lists the SDK `.cpp` files into a `vst3sdk` target (`-w`, `/Zc:char8_t-` on MSVC because C++20 `u8""` breaks `module_win32.cpp`, `-fobjc-arc` on clang).

## 2. MPE — the event model (central)

### 2.1 Units and scalar types (`mpe\mpetypes.h`)

| Type | Def | Meaning |
|---|---|---|
| `usecs_t`/`timestamp_t`/`duration_t` | `int64_t` | **microseconds** (`:45,63-64`). `INFINITE_DURATION = INT64_MAX` marks an open-ended event (`:70`) |
| `percentage_t` | `int_fast16_t`, `ONE_PERCENT=100`, `HUNDRED_PERCENT=10000` | **fixed point: 10000 = 100 %** (`:46-50`). Used for durations factors, positions within a note, dynamics, pitch |
| `duration_percentage_t` | `percentage_t` | position inside an event, 0..10000 |
| `pitch_level_t` | `percentage_t`; `PITCH_LEVEL_STEP = 50` | **50 units per semitone** (so a cent = 0.5 unit). `pitchLevel(class, octave) = 50*12*octave + 50*class` (`:164`). Level 0 = C0 = MIDI 12 (`ZERO_PITCH_LEVEL_MIDI_EQUIVALENT=12`, `:162`), so `midi = 12 + level/50` |
| `dynamic_level_t` | `percentage_t`; `MAX=10000`, `MIN=0`, `DYNAMIC_LEVEL_STEP = 500` | 0..10000 |
| `layer_idx_t` | `uint_fast32_t` | `makeLayerIdx(staff, voice) = staff*MAX_VOICES(4) + voice` (`:77-82`) — the *track/channel key* for sequencers |
| `PitchClass` | `Undefined=-1, C=0 ... B=11` | |

`DynamicType` (`:385-409`) is an enum whose **values are dynamic levels**: `ppppppppp=0`, `pppppppp=750`, `ppppppp=1250`, `pppppp=1750`, `ppppp=2250`, `pppp=2750`, `ppp=3250`, `pp=3750`, `p=4250`, `mp=4750`, `Natural=5000`, `mf=5250`, `f=5750`, `ff=6250`, `fff=6750`, `ffff=7250`, `fffff=7750`, `ffffff=8250`, `fffffff=8750`, `ffffffff=9250`, `fffffffff=10000`. `dynamicLevelFromNormalized(real_t)` = `lround(x*10000)` (`:380`).

### 2.2 Curves and patterns

- `ValuesCurve<T>` = `SharedMap<duration_percentage_t, T>` with helpers `amplitudeValuePoint()` (max by `|value|`), `maxAmplitudeLevel()`, `attackPhaseDuration()`, `releasePhaseDuration()`, `velocityFraction()` (`log10(amp/duration)`-based, `:90-134`). `PitchCurve = ValuesCurve<pitch_level_t>`, `ExpressionCurve = ValuesCurve<dynamic_level_t>`.
- **Pattern** = the *template* an articulation applies, normalized to the note length. `ArticulationPatternSegment { ArrangementPattern{durationFactor, timestampOffset}; PitchPattern{pitchOffsetMap}; ExpressionPattern{dynamicOffsetMap} }` (`:433-530`). `ArticulationPattern = SharedMap<duration_percentage_t, Segment>` — several segments keyed by the *position within a multi-note articulation* where they start (`:532`).
- `ArticulationsProfile { supportedFamilies; patterns: ArticulationType -> ArticulationPattern }` (`:534-586`). `pattern(type, fallback)` returns an empty pattern if missing. A profile is per **family** (`ArticulationFamily = SoundCategory`: Keyboards, Strings, Winds, Percussions, Voices; `:182`, `soundid.h:273`).
- `ArticulationMeta { type, pattern, timestamp, overallDuration, overallPitchChangesRange, overallDynamicChangesRange }` (`:590-639`): one articulation instance on the score timeline (`hasStart()` = duration>0; `hasEnd()` = duration != INFINITE).
- `ArticulationAppliedData { meta, appliedPatternSegment, occupiedFrom/To, occupiedPitch/DynamicChangesRange }` (`:643-710`): the slice of a (possibly multi-note) articulation that one note covers. `updateOccupiedRange`: full `0..100%` -> first segment; otherwise `meta.pattern.lower_bound(occupiedFrom)` (fallback first), and the pitch/dynamic ranges are scaled by `occupiedTo`.
- `ArticulationMap : SharedHashMap<ArticulationType, ArticulationAppliedData>` (`:712-939`) — the set of articulations on a note, with **`preCalculateAverageData()`** blending them: one articulation -> its segment verbatim; several -> `durationFactor` averaged over *all*, `timestampOffset`/amplitude/pitch/dynamic averaged only over those that actually change (counted `timestampChangesCount`/`dynamicChangesCount`/`pitchChangesCount`), offset curves summed point-by-point over `EXPECTED_SIZE = 11` samples (0,10 %..100 %) and divided (`:789-930`). Cached in `m_average*`.

### 2.3 `ArticulationType` inventory (`mpetypes.h:184-333`)

Grouped as in source (the enum is the full set; `Undefined=-1`, `Last` sentinel):

| Group | Members |
|---|---|
| Basic single-note | Standard, Staccato, Staccatissimo, Tenuto, Marcato, Accent, SoftAccent, LaissezVibrer, Subito, FadeIn, FadeOut |
| Technique | Harmonic, JazzTone, PalmMute, Mute, Open, Pizzicato, SnapPizzicato, RandomPizzicato, UpBow, DownBow, Detache, Martele, Jete, ColLegno, SulPont, SulTasto |
| Noteheads (used as playback hints) | GhostNote, CrossNote, CrossLargeNote, CrossOrnateNote, CircleNote, CircleCrossNote, CircleDotNote, TriangleLeft/Right/Up/Down/RoundDownNote, DiamondNote, MoonNote, PlusNote, SlashNote, SquareNote, SlashedBackwards/ForwardsNote |
| Brass/jazz | Fall, QuickFall, Doit, Plop, Scoop, BrassBend, SlideOutDown/Up, SlideInAbove/Below, VolumeSwell |
| Vibrato | Vibrato, WideVibrato, MoltoVibrato, SenzaVibrato |
| Tremolo | Tremolo8th/16th/32nd/64th, TremoloBuzz |
| Ornaments | TrillBaroque, Upper/LowerMordent(+Baroque), PrallMordent, MordentWithUpperPrefix, Up/DownMordent, Tremblement, UpPrall, PrallUp, PrallDown, LinePrall, Slide, Turn, InvertedTurn |
| Grace | PreAppoggiatura, PostAppoggiatura, Acciaccatura |
| Guitar/other | TremoloBar, Distortion, Overdrive, Slap, Pop, LeftHandTapping, RightHandTapping, ContinuousGlissando, Breath |
| Handbells | MalletBellOnTable, MalletBellSuspended, MalletLift, Pluck, PluckLift, Gyro, Martellato, MartellatoLift, HandMartellato, MutedMartellato, ThumbDamp, BrushDamp, Ring, RingTouch, SingingBell, SingingVibrate, Swing, Echo |
| **Multi-note** | Trill, Crescendo, Diminuendo, DiscreteGlissando, Legato, Pedal, Multibend, Arpeggio, ArpeggioUp/Down/StraightUp/StraightDown |

`isMultiNoteArticulation` = exactly the last row (`:337-355`); `isRangedArticulation` = Legato, Pedal, Multibend only (`:362-371`). Profile JSON names differ from enum names for three entries: `UpperMordent` <-> `"ShortTrill"`, `LowerMordent` <-> `"Mordent"`, `SulPont` <-> `"SulPonticello"` (`mpe\internal\articulationstringutils.h:40-130`).

### 2.4 Events (`mpe\events.h`)

| Type | Fields | Producer / consumer |
|---|---|---|
| `NoteEvent` (`:99`) | `ArrangementContext {nominal/actual timestamp+duration, voiceLayerIndex, staffLayerIndex, bps}`, `PitchContext {nominalPitchLevel, pitchCurve}`, `ExpressionContext {articulations: ArticulationMap, nominalDynamicLevel, expressionCurve, optional velocityOverride}` | the workhorse. Built in `src\engraving\playback\renderingcontext.h:127 buildNoteEvent`, `playbackeventsrenderer.cpp:83,167,208`, metronome `:251-310`, `bendsrenderer.cpp:195` |
| `ControllerChangeEvent` (`:264`) | `type {Modulation, SustainPedalOnOff, PitchBend}`, `val` in `[0;1]`, `layerIdx` | Fluid + VST sequencers (main stream); MuseSampler only in off-stream/audition |
| `TextArticulationEvent` (`:286`) | text (technique code), layerIdx, flag `StartsAtPlaybackPosition`; special text `ORDINARY_PLAYING_TECHNIQUE_CODE = "ordinary_technique"` (`mpetypes.h:373`) | MuseSampler only |
| `SoundPresetChangeEvent` (`:304`) | preset code, layerIdx | MuseSampler only |
| `SyllableEvent` (`:316`) | lyric text, layerIdx, flags `StartsAtPlaybackPosition`, `HyphenedToNext` | MuseSampler only |

`using PlaybackEvent = std::variant<std::monostate, NoteEvent, TextArticulationEvent, SoundPresetChangeEvent, SyllableEvent, ControllerChangeEvent>` (`:335`); `PlaybackEventsMap = SharedMap<timestamp_t, PlaybackEventList>` (`:343`).

`NoteEvent::setUp()` (`:167-257`) derives the "actual" values **from the articulation averages** at construction:

```cpp
// events.h NoteEvent::calculateActualDuration / Timestamp
actualDuration  = nominalDuration * percentageToFactor(avgDurationFactor);
actualTimestamp = nominalTimestamp + nominalDuration * percentageToFactor(avgTimestampOffset);
```
Pitch curve: if a `requiredPitchCurve` is not given, `pitchCurve = averagePitchOffsetMap`, each value scaled by `avgPitchRange/50 * 0.5` (`:207-222`). Expression curve: `amplifyFactor = (artLevel - 5000)/500`; `diff = max(avgDynamicRange, 500) * amplifyFactor`; `actualLevel = nominal + diff`; each curve point multiplied by `actualLevel/artLevel` (`:224-257`). `velocityOverride` is set when `requiredVelocityFraction != 0`.

`PlaybackData` (`:351`) = `{ originEvents, setupData, dynamics (DynamicAutomationLayers), mainStream, offStream }`:
- `mainStream` is `async::Channel<PlaybackEventsMap, DynamicAutomationLayers>` — score changes pushed live to the synth; `offStream` is `Channel<PlaybackEventsMap, bool flush>` — **off-stream = ad-hoc "audition" events** (note-entry preview, clicks) played while the transport is *not* running.
- `DynamicAutomationLayers = SharedMap<layer_idx_t, AutomationCurve<timestamp_t>>` (`:345-346`): per-layer *dynamics automation*, separate from per-note dynamics.

### 2.5 Dynamics automation (`mpe\automationpoint.h`, 2026)

`AutomationPoint { InValue inValue (ArrivalFromPrevious | ExplicitArrival{value, Ease}); outValue }` (`:36-69`). Values are normalized `[0;1]`. `evaluateAt` (`:105`) draws the segment to a point as a line, or as **two quadratic Beziers meeting at the ease/bend point** (`Ease{t, value}`, default 0.5/0.5 = none). `evaluateCurveAt(curve, pos)` (`:140`) holds the first point's value backwards in time. `resampleCurve(curve, stepInterval, cb)` (`:163`) — for consumers that cannot evaluate on read; **every sequencer resamples at `STEP_INTERVAL_US = 30000` (30 ms)**.

### 2.6 Setup data / sound identity (`soundid.h`, `playbacksetupdata.h`)

`SoundId` (~230 instruments: Accordion ... Metronome), `SoundCategory` (Keyboards, Strings, Winds, Percussions, Voices), `SoundSubCategory` (~200: nationalities, `FX_*`, `In_C..In_B_flat`, etc.). Each has `ID_STRINGS`/`CATEGORY_STRINGS`/`SUBCATEGORY_STRINGS` maps; `*FromString` is a **linear `find_if`** (`:708,746,944`), unknown -> `Unknown`, empty -> `Undefined`.

`PlaybackSetupData { id (string), subCategories, scoreId?, musicXmlSoundId?, category, supportsSingleNoteDynamics }` (`:31-140`). Canonical string form `category.id[.sub1:sub2]` (`toString :142`, `fromString :160`) — e.g. a violin is `strings.violin`. This string is what MuseSampler's `getMatchingInstrumentList` receives. `GENERIC_SETUP_DATA` = (`Last`,`Last`,{`Last`}) is the "any sound" sentinel (`:187`). `supportsSingleNoteDynamics` ("SND") switches Fluid/VST sequencers into expression-CC/gain mode (see sections 3.5, 5.4).

### 2.7 Articulation profiles (`mpe\internal\articulationprofilesrepository.cpp`)

`IArticulationProfilesRepository` (global): `createNew`, `defaultProfile(family)`, `loadProfile(path)`, `saveProfile(path, p)`, `profileChanged()`. Defaults are Qt resources `:/mpe/resources/general_{keyboard,strings,winds,percussion,voice}_articulations_profile.json` (`:38-45`), lazily loaded + cached (`:73-92`). `PlaybackModel` fetches `defaultProfile(setupData.category)` (`src\engraving\playback\playbackmodel.cpp:319,1203`).

JSON schema (`:47-66`, `:185-313`): `{ supportedFamilies:[...], patterns:{ "<ArticulationName>":[ { patternPosition, arrangementPattern:{durationFactor,timestampOffset}, pitchPattern:{pitchOffsets:[{offsetPosition,offsetValue}]}, expressionPattern:{dynamicOffsets:[{offsetPosition,offsetValue}]} } ] } }`. Numbers are in the 10000-based units, e.g. `Accent.durationFactor = 9000` (= 90 %), dynamic offsets at positions 0,1000..10000. Constants `MAX_AMPLITUDE_LEVEL_KEY`/`AMPLITUDE_TIME_SHIFT` are declared but **unused** — max amplitude is *derived* from the curve. QML editor for profiles lives in `mpe\qml\Muse\Mpe\` (`ArticulationsProfileEditorModel`, pattern plots).

Tests: `mpe\tests\` (`singlenotearticulationstest`, `multinotearticulationstest`, `automationpointtest`, mock repo).

## 3. MIDI model, ports, remote

### 3.1 `midi::Event` (`midi\midievent.h`)

A **Universal MIDI Packet (UMP)** (spec M2-104-UM) stored in `std::array<uint32_t,4> m_data` (`:55,1241`). `MessageType` = top nibble of word 0 (`Utility 0, SystemRealTime 1, ChannelVoice10 2, SystemExclusiveData 3, ChannelVoice20 4, Data 5`); `Opcode` = bits 20-23 (`NoteOff 8, NoteOn 9, PolyPressure A, ControlChange B, ProgramChange C, ChannelPressure D, PitchBend E`, plus MIDI 2.0 only `RegisteredPerNoteController 0, AssignablePerNoteController 1, RegisteredController 2, AssignableController 3, RelativeRegistered 4, RelativeAssignable 5, PerNotePitchBend 6, PerNoteManagement F`) (`:56-83`). Accessors assert opcode/message type in debug (`assertOpcode`).

- Conversion: `fromMidi10Package(uint32)` / `toMidi10Package()` / `fromMidi10Bytes` / `toMidi10Bytes` (`:112-172`), `fromMidi10SysExBytes` chunks SysEx into 6-byte UMP packets (COMPLETE/START/CONTINUE/END status, `:174-240`), `toMIDI10()` (2.0 -> list of 1.0 events; RPN/NRPN become CC 101/100/99/98+6+38, bank+program become CC0 pairs, `:802-888`), `toMIDI20(chain)` (`:891-975`).
- **MIDI 2.0 NoteOn carries a `Pitch` attribute** (`AttributeType::Pitch`): `setPitchNote(note, tuning)` packs `note<<9 | tuning*512` (`:425`), `pitchTuning()` returns semitones; this is how microtonal/cent offsets from MPE reach FluidSynth (`FluidSequencer::addNoteEvent`, `fluidsequencer.cpp:199-206`). Velocity is 16-bit in 2.0 (`velocity16`), 7-bit in 1.0 (`velocity7`).
- Types (`midi\miditypes.h`): `MidiDevice{id,name}`; `NONE_DEVICE_ID = "-1"`; device ids are `"index:mid:pid"` strings (`makeUniqueDeviceId`/`splitDeviceId`, `:141-163`); `MidiData{MidiMapping{division 480, tempo map, programs}, MidiStream{lastTick, control/main/background channels}}` (`:77-124`, legacy sequencer streams); CC constants `MODWHEEL 1, EXPRESSION 11, SUSTAIN 64, SOSTENUTO 66` (`:44-47`).
- Errors: `midi\midierrors.h` `Err::MidiInvalidDeviceID 620 ... MidiSendError 624`.

### 3.2 Ports (`imidiinport.h`, `imidioutport.h`)

Both global interfaces: `availableDevices()`/`availableDevicesChanged()`, `connect(id)`, `disconnect()`, `isConnected()`, `deviceID()`, `deviceChanged()`. In-port adds `eventReceived(): Channel<tick_t, Event>`. Out-port adds `supportsMIDI20Output()` and `sendEvent(const Event&)` ("if the receiver does not support MIDI 2.0, it's the output port's responsibility to convert to 1.0", `imidioutport.h:48-52`).

| Platform | In | Out | MIDI 2.0 out |
|---|---|---|---|
| Windows (WinMM) | `WinMidiInPort` — `midiInOpen` callback; `MIM_DATA` -> `fromMidi10Package().toMIDI20()`, `MIM_LONGDATA` SysEx -> UMP chunks; 1024-byte SysEx buffer (`winmidiinport.cpp:128-166,250-273`) | `WinMidiOutPort` — `midiOutShortMsg`; **filters out `MOD_SWSYNTH` devices** (`winmidioutport.cpp:102`); always `toMIDI10()` | `false` (`:182`) |
| Linux | ALSA seq | ALSA | `false` (`alsamidioutport.cpp:223`) |
| macOS | CoreMIDI | CoreMIDI; uses MIDI 2.0 packets when `supportsMIDI20Output() && configuration()->useMIDI20Output()` (macOS 11+, `coremidioutport.cpp:271-292`) | version-dependent |
| other | dummy | dummy | |

Device hot-plug: `MidiDevicesListener` polls `availableDevices()` on a thread every **5000 ms** and fires `devicesChanged` if the list differs (`midideviceslistener.cpp:66-87`); ports disconnect if the active device vanished (`winmidiinport.cpp:71-84`). Every in-port normalizes incoming 1.0 to 2.0 via `toMIDI20()` so consumers only see UMP 2.0.

`MidiConfiguration` (`midiconfiguration.cpp:33-57`): settings keys `io/midi/useRemoteControl` (default true), `io/inputDeviceId` (default `""` = "first available"), `io/outputDeviceId` (default `"-1"` none), `io/midi/useMIDI20Output` (true). `midiPortIsAvalaible()` always `true`.

**Who connects ports**: `src\notationscene\internal\midiinputoutputcontroller.cpp` (`init` `:36`, `checkConnection` `:98-135`: reconnect to preferred device if present, else keep current if still present, else first available, else `NONE_DEVICE_ID`). Incoming events: `onMidiEventReceived` (`:137-157`) first offers the event to `IMidiRemote::process`; only if it returns `Ret::Code::Undefined` (not a mapped command) is it forwarded to `notation->midiInput()->onMidiEventReceived` (note entry). **Outgoing**: only `FluidSynth` holds `IMidiOutPort` (`fluidsynth.h:33,42,114`); in `FluidSynth::handleEvent` it mirrors every synthesized event to the hardware port (`fluidsynth.cpp:249-261`, optionally remapping channel to `staff % 16` when `STAFF_TO_MIDIOUT_CHANNEL`), and on `flushSound` sends CC123 (all notes off), CC64=0, pitch-bend center on connected ports (`:181-218`). So "MIDI out" = Fluid events echoed; VST and MuseSampler tracks are not sent to MIDI out.

### 3.3 MIDI remote (`midiremote\`)

- `RemoteEvent{type Note|Controller, value}` (`midiremotetypes.h:42`); `MidiControlsMapping{rcommand::Command, RemoteEvent}`; `remoteEventFromMidiEvent` takes NoteOn/NoteOff -> `Note(note())`, ControlChange -> `Controller(index())` (`:102-116`). Note: this is **command-based** (`rcommand::Command`, ADR 00101), not action-code-based.
- `MidiRemote` (per context; `midiremote.cpp`): mappings persisted to `<userAppDataPath>/midi_mappings.xml` (`midiremoteconfiguration.cpp:33,43`) as `<MidiMapping><Event><key/><EventType/><EventValue/></Event>...` (`:39-47,142-238`), guarded by `multiwindows` `MIDI_MAPPING` resource lock and re-read when another window changes it (`:49-57`). `process(ev)` (`:115-140`): ignore per `needIgnoreEvent` -> SysEx goes to the MMC decoder -> else first mapping whose `RemoteEvent` equals the event dispatches its command via `commandDispatcher()->dispatch`.
- `needIgnoreEvent` (`:240-274`): ignored while in *setting mode* (mapping UI), ignores anything but NoteOn/NoteOff/CC; **releases** (NoteOff or CC value 0) are ignored unless `advanceToNextNoteOnKeyRelease` (default true, key `io/midi/advanceOnRelease`) *and* the event equals the mapping for `command://notation/realtime-advance` (`:45,257-271`).
- **MMC** (MIDI Machine Control): `MMCDecoder` reassembles SysEx (UMP chunks or raw F0..F7 buffers; `SysExAssembler`), recognizes `7F <dev> 06 <cmd>` (`mmcdecoder.cpp:77-95`); `MMCCommand` enum Stop 01, Play 02, DeferredPlay 03, FF 04, Rewind 05, RecordStrobe 06, RecordExit 07, RecordPause 08, Pause 09, Eject 0A, Chase 0B, CommandErrorReset 0D, MMCReset 0F, Locate 44, Shuttle 47 (`mmctypes.h`). `MidiRemote::processMMC` handles **Play/Pause/Stop/Locate only** -> `command://playback/{play,pause,stop,rewind position=...}` (`midiremote.cpp:287-307`). `locateToSeconds` supports SMPTE only (fps code 24/25/29.97/30, `mmcdecoder.cpp:243-294`). The same decoder is reused by `VstAudioClient` for **plugin -> host transport** (section 4.6).
- `setCurrentActionEvent` is `NOT_IMPLEMENTED` (`midiremote.cpp:109-113`). UI: `midiremote\qml\Muse\MidiRemote\` (`MidiDeviceMappingPage`, `EditMidiMappingDialog`).
- Tests: `midi\tests\midievent_tests.cpp`, `midiremote\tests\mmcdecoder_tests.cpp`.

## 4. The VST3 host (`muse\framework\vst`)

### 4.1 Object model

| Class | Role | Threading |
|---|---|---|
| `VstPluginsScanner` | `IAudioPluginsScanner`: paths from `PluginModule::getModulePaths()` (SDK default locations) + user dirs scanned for `*.vst3` (`vstpluginsscanner.cpp:54-80`) | scan |
| `VstPluginMetaReader` | `canReadMeta` = suffix `vst3`; `readMeta` loads the module and emits **one** `PluginMeta` for the **first** class of category `kVstAudioEffectClass` then `break`s (`vstpluginmetareader.cpp:47-78`). `meta.id` = file basename (`io::completeBasename`), `type="VstPlugin"`, attrs `categories` = SDK subcategory string, `hasNativeEditorSupport=true`, `vendor` | validation subprocess |
| `VstModulesRepository` (`IVstModulesRepository`) | cache `resourceId -> PluginModulePtr` (the loaded DLL); `exists()` consults `IKnownAudioPluginsRegister`; `instrumentModulesMeta()`/`fxModulesMeta()` filter *Validated* `VstPlugin` entries by whether `categories` contains `"Instrument"` (`vstmodulesrepository.cpp:126-153`). `refresh()` is empty. Holds the SDK `PluginContext` (`HostApplication`) set into `PluginContextFactory` at `init` (`:28-33`). Wraps loads in `IAudioPluginsLoadGuard::beginLoad/endLoad` (crash tracking, `:79-81`) | `addPluginModule`/`removePluginModule` main thread only; `exists`/meta lists audio thread only |
| `VstInstancesRegister` | `map<{Type Instrument|Effect, resourceId, trackId, chainOrder}, instance>`; master FX uses `trackId=-1`; `makeAndRegister{Instr,Fx,MasterFx}Plugin` creates `VstPluginInstance`, registers, calls `load()` (`vstinstancesregister.cpp:34-98`) | mutex |
| `VstPluginInstance` (`IVstPluginInstance`) | owns `PluginModulePtr`, `VstPluginProvider`, `VstComponentHandler`, state buffers; `id()` from a static counter | created on audio thread; **load + all plugin lifecycle on the main thread** via `Async::call(..., mainThreadId)` |
| `VstPluginProvider` | wraps SDK `PlugProvider`; `component()`, `controller()`, lazy `IMidiMapping` via `controller->queryInterface` (`vstpluginprovider.cpp`) | |
| `VstComponentHandler` | `IComponentHandler` + `IComponentHandler2` (`VstAdvancedHandler`); `performEdit`->`pluginParamChanged`, `endEdit`/`setDirty`/`finishGroupEdit`->`pluginParamsChanged` notify unless suppressed; `restartComponent` is a no-op returning OK (`vstcomponenthandler.cpp:63-132`) | |
| `VstAudioClient` | the realtime bridge: `ProcessData`, bus activation, event/param queues, process | audio thread |
| `VstSynthesiser` + `VstSequencer` + `VstiResolver` | instrument source | audio engine |
| `VstFxProcessor` + `VstFxResolver` | track/master FX | audio engine |
| `VstActionsController`/`VstCommands*` | open editor commands, old/new view switch | main |

### 4.2 Plugin loading sequence (instrument)

1. `VstiResolver::resolveSynth` -> `createSynth`: `pluginModulesRepo()->exists(params.resourceMeta.id)` else `nullptr`; `new VstSynthesiser(trackId, params)`; `init(outputSpec)` (`vstiresolver.cpp:45-55`). `hasCompatibleResources` always `true` (`:35`). `resolveSoundPresets` empty. `clearSources` -> `unregisterAllInstrPlugin` (`:67-70`).
2. `VstSynthesiser::init` (`vstsynthesiser.cpp:52-90`): `instancesRegister()->makeAndRegisterInstrPlugin` (starts async load), `m_vstAudioClient->init(Instrument, instance)`, then `onPluginLoaded` (immediately if loaded, else on `loadingCompleted`): `updatePluginConfig(params.configuration)` (restore saved state), `setOutputSpec`, `loadSupportedParams`, `m_sequencer.init(paramsMapping(SUPPORTED_CONTROLLERS), useDynamicEvents)`, `m_inited=true`. Until then `isValid()` is false.
3. `VstPluginInstance::load` (`vstplugininstance.cpp:122-170`, **main thread**): fetch/create module, find first `kVstAudioEffectClass`, build `VstPluginProvider`, `initialize()`, `controller->setComponentHandler`, `syncControllerToComponentState()` (copy component state into the controller — needed by e.g. Roland ZENOLOGY, comment `:174-178`), `m_isLoaded=true`, `loadingCompleted.notify()`.
4. **Destruction** (`~VstPluginInstance`, `:67-100`): sets `m_isLoaded=false`, moves provider/module out, and on the **main thread** removes the module from the repo, `component->setActive(false)`, then drops provider and module — order matters and `~VstAudioClient` deliberately does *not* deactivate (`vstaudioclient.cpp:67-73`) so an open editor view dies first.

### 4.3 State save/restore

State is two binary blobs inside `AudioUnitConfig` (a `map<string,string>`): keys `"componentState"` and `"controllerState"` (`vstplugininstance.cpp:37-38`). `rescanParams()` (main thread, triggered by handler notifications) serializes `component->getState` + `controller->getState` and emits `pluginSettingsChanged` -> synth/fx update their `m_params.configuration` and fire `paramsChanged` (so the project saves them) (`:200-249`; `vstsynthesiser.cpp:78-85`). `setPluginConfig` restores with `suppressNotify` set (to avoid echo), `component->setState` + `controller->setComponentState`, then `controller->setState` (`:251-306`). There is **no preset (`.vstpreset`) file handling** in this module (the `PluginPreset = PresetFile` typedef at `vsttypes.h:68` is unused here) — presets are whatever the plugin's own editor does, persisted via state blobs.

### 4.4 `VstAudioClient` — process path (`vstaudioclient.cpp`)

- `setOutputSpec` -> `updateProcessSetup` (`:419-446`): deactivate, `IAudioProcessor::setupProcessing` (`kSample32`, `maxSamplesPerBlock`, sample rate, process mode), `setUpProcessData`, flush buffers, reactivate. `setProcessMode`: `kOffline` for `ProcessMode::PlayingOffline` (export), `kRealtime` otherwise (`vstsynthesiser.cpp:161-165`).
- `setUpProcessData` (`:346-417`): `HostProcessData::prepare`, then activates **main** buses that are `kDefaultActive` (inputs and outputs); if none, activates bus 0. Aux buses are left inactive.
- `process(output, samples, playbackPositionSamples)` (`:249-298`): returns 0 if inactive; sets `numSamples`, `projectTimeSamples`; grows spec if a bigger block arrives; for FX copies interleaved input into the bus (`extractInputSamples`); `IAudioProcessor::process`; then **instrument: `output[...] += sample * m_volumeGain` (mixes into the buffer, clears input events + param queue)**; **FX: `output[...] = sample * gain` (replaces)**; finally `processOutputEvents`.
- **Transport state**: `setIsPlaying` toggles `ProcessContext::kPlaying`; `projectTimeSamples` is set from the synth's position.
- **Lifecycle**: `ensureActivity()` = `component->setActive(true)` + `processor->setProcessing(true)` (lazy on first event, `:548-568`); `disableActivity()` runs one more `process` if state change pending, then `setProcessing(false)`, `setActive(false)`.
- **Note tracking / flush**: `handleEvent` stores NoteOn in `m_playingNotes` keyed by hash(pitch,channel), erases on NoteOff; `flushSound` (`:203-247`) zeroes buffers, clears queues, emits NoteOffs for every sounding note, and resets every touched param to its `defaultNormalizedValue`.
- **Plugin -> host MMC** (`processOutputEvents` `:505-546`): output `kDataEvent/kMidiSysEx` events are decoded by `MMCDecoder` and converted to `TransportEvent::{play,pause,stop,seek}` dispatched to `ITransportEventsDispatcher` — a plugin (e.g. a DAW-style sampler UI) can drive MuseScore transport.
- `paramsMapping(controllers)` (`:300-326`): for each active input bus and each wanted `CtrlNumber`, asks the plugin's `IMidiMapping::getMidiControllerAssignment(bus, ch 0, ctrl, &paramId)`. Controllers a plugin does not map are **silently dropped** (see `VstSequencer::addParamChange` `:240-243`).

### 4.5 `VstSynthesiser`

`AbstractSynthesizer` subclass. `SUPPORTED_CONTROLLERS` = `kCtrlVolume, kCtrlExpression, kCtrlSustainOnOff, kCtrlSustenutoOnOff, kPitchBend` (`vstsynthesiser.cpp:32-38`). `setupSound` only stores `supportsSingleNoteDynamics`. `setMode`: active iff playing mode; `m_sequencer.setActive`, `toggleVolumeGain` (inactive -> gain 0.5), `setIsPlaying/Active` on the client. `process` (`:199-234`) pulls an `EventSequenceMap` from the sequencer for the block and processes sub-blocks between event timestamps (`processSequence`: dispatch `VstEvent` -> `handleEvent`, `ParamChangeEvent` -> `handleParamChange`, `gain_t` -> `setVolumeGain`, then client `process`).

### 4.6 `VstSequencer` — MPE to VST3 (`vstsequencer.cpp`)

Sequence element type = `std::variant<VstEvent, ParamChangeEvent, gain_t>` (`vstsequencer.h:30`). Handles **only `NoteEvent` and `ControllerChangeEvent`** (`addPlaybackEvents :111-126`; text articulations, presets, syllables are ignored).

| MPE input | VST output |
|---|---|
| `NoteEvent` start (`arrangementCtx.hasStart()`) at `actualTimestamp` | `kNoteOnEvent`: `noteId=-1`, `channel=0`, `busIndex=0`, `flags=kIsLive`, `pitch = clamp(12 + level/50, 0..127)`, `tuning` in **cents** (`noteTuning`), `velocity = expressionLevel(...)` in 0..1 (`buildEvent :362-388`) |
| `NoteEvent` end (`hasEnd`) at `actualTimestamp + actualDuration` | `kNoteOffEvent` |
| velocity | `velocityOverride` if set, else the dynamic level (curve max or nominal) mapped linearly from `ppp(3250)..fff(6750)` to `0..1` rounded to 2 dp; below ppp clamps to a tiny value (`:406-434`) |
| `Pedal` articulation | param change on `kCtrlSustainOnOff` 1 at start / 0 at end (`:208-217`) |
| `LaissezVibrer` | `kCtrlSustenutoOnOff` pulses starting 10 % into the note (`:200-204,316-334`) |
| `Multibend`/`ContinuousGlissando` with a pitch curve | `kPitchBend` param, interpolated with `lerp`, bend range assumed **+-2 semitones** (`pitchBendLevel`, `SEMITONE_RANGE=2`, `:436-445`); reset to 0.5 at end |
| `ControllerChangeEvent` Modulation/Sustain/PitchBend | param change on `kCtrlModWheel`/`kCtrlSustainOnOff`/`kPitchBend` |
| dynamics automation (only if `useDynamicEvents`/SND) | `gain_t` steps at 30 ms resolution, max across layers (VST has one gain) (`addDynamicEvents :128-160`); also per-note `expressionLevel(nominalDynamicLevel)` gain at note start |

`currentGain()` returns 0.5 when not SND, else the max over layers of the curve at the playback position (`:86-109`). **Keyswitch hack**: `sortNoteOnEventsByPitch` stable-sorts NoteOns sharing a timestamp ascending by pitch so keyswitch notes (low) precede the played note (`:336-360`, upstream issue 32150).

### 4.7 FX (`vst\internal\fx`)

`VstFxResolver` creates/unregisters track and master FX through `VstInstancesRegister` (`vstfxresolver.cpp:50-90`). `VstFxProcessor` owns a client of `PluginType::Fx`; `process` -> `client.process`; `shouldProcessDuringSilence` only for `FxGenerator` category plugins (`vstfxprocessor.cpp:118-121`). `name()` = `"Vst/<resourceId>"`.

### 4.8 Editor UI and commands

`vstcommands.h` / `VstActionsController` (`vstactionscontroller.cpp`): commands `VST_OPEN_INSTRUMENT_EDITOR_COMMAND`, `VST_OPEN_FX_EDITOR_COMMAND` (params `resourceId`, `trackId`, `chainOrder`, `operation open|close`, `sync`) open URI `muse://vst/editor?instanceId=%1&modal=false&floating=true` (`:37,72-164`). Two views switchable at runtime: **new** (`muse://vst/editor` -> QML `Muse.Vst.VstEditorDialog`, `VstView` = `QQuickItem` + `IPlugFrame`) vs **old** (`VstViewDialog` QWidget); setting `application/vst/view` default `"newview"` (`vstconfiguration.cpp:33,37`); **Linux supports only the new view** (`:74-79,108-115`). User VST dirs: setting `application/paths/myVSTs` (`:32`).

### 4.9 Registration of plugins from the CLI

Plugin discovery/validation lives in `muse\framework\audioplugins` (`IKnownAudioPluginsRegister`, `IRegisterAudioPluginsScenario`). Each plugin path is validated in a **separate `--register-audio-plugin <path> --register-audio-plugin-out <file>` process** (`audioplugins\internal\registeraudiopluginsscenario.cpp:399`; parsed in `src\app\internal\commandlineparser.cpp:183-185,297-300`; run in `src\app\internal\consoleapp.cpp:324-329`), so a crashing plugin cannot take down the app; `markCrashedPluginsAsBroken()` runs at startup unless in `RunMode::AudioPluginRegistration` (`audiopluginsmodule.cpp:78-85`). See `mscore-cli.md`.

## 5. MuseSampler integration (`muse\framework\musesampler`)

### 5.1 Library discovery and loading

- Path (`musesamplerconfiguration.cpp:36-64`): env **`MUSESAMPLER_PATH`** if set, else `<genericDataPath>/MuseSampler/lib/` + `libMuseSamplerCoreLib.so` (Linux) / `.dylib` (macOS) / `MuseSamplerCoreLib.dll` (Windows). `minSupportedVersion()` = **0.105.0** (`:66-69`). Setting `museSampler/useLegacyAudition` (default false) picks `ms_MuseSampler_init` over `_init_2`.
- `MuseSamplerResolver::init` (`musesamplerresolver.cpp:81-134`): absolute path that doesn't exist -> log + stay unloaded; `loadLib` (`muse::loadLib`, `dlib.h`); `loadApi(minVersion, legacyAudition)`; `init()`; install `ms_set_logging_callback` mapping levels to LOGD/I/W/E. State flags: `isLoaded()` (handler non-null), `initFailed()` (lib loaded + API ok but `ms_init` failed), `version()`, `buildNumber()`.
- `MuseSamplerLibHandler::loadApi` (`libhandler.h:47-204`): resolves `ms_get_version_{major,minor,revision}` (+ optional build number); rejects `< minSupportedVersion` ("too old") and **`major > 0`** ("too new -- update MuseScore Studio", `maximumMajorVersion = 0`, `:75-80`); picks `ms_init_3` (returns `ms_InitResult`, >= 0.106) else `ms_init_2` (`:83-93`); then `dlsym`s ~65 functions into `std::function`/pointer members. `isValid()` requires **all** of them (`:249-309`) — a single missing symbol rejects the lib (`printApiStatus` logs which). Optional/wrapped: note events use `ms_MuseSampler_add_track_note_event_6` with `ms_NoteEvent_5`; audition uses `start_audition_note_5`.
- Init result codes (`apitypes.h:36-50`): ProtectionRejected, ConfigurationMissing/Unreadable/Invalid, InstrumentMapMissing, ReceiptMissing/Rejected, DatabaseOpenFailed, InstrumentMapReadFailed, NoInstalledInstruments, NoUsableInstruments. After init `ms_disable_reverb()` is called (`libhandler.h:224-228`) — MuseScore applies its own reverb; `defaultReverbLevel(soundId)` reads the library's `ms_Instrument_get_reverb_level/100` (`musesamplerresolver.cpp:289-305`).
- `deinit`: `ms_deinit` + `closeLib` (`:233-247`). Commands `command://musesampler/check` (shows detected/unsupported/not-found status, optional build number in dev mode) and `/reload` (`ms_reload_all_instruments`, for sound developers) (`musesampleractioncontroller.cpp`).

### 5.2 Instrument resolution

- `resolveResources()` lists the library's instruments as `AudioResourceMeta { id = to_string(instrumentId), type = "MuseSamplerSoundPack", vendor = "MuseSounds", attributes{ playbackSetupData = mpe sound id, museCategory, musePack (falls back to category), museVendorName, museName, museUID = instrument id, isOnline=1 if online } }` (`musesamplerresolver.cpp:181-224`). The app side (`src\musesounds`) layers the online catalog/updates on top (`imusesoundsrepository.h`, ...).
- `resolveSynth` (`:159-168`): `findInstrument` linearly walks `ms_get_instrument_list` matching **`attributeVal("museUID") == instrumentId`** (`:61-79`); no match -> `nullptr` (no synth). `hasCompatibleResources` = library loaded.
- **Fallback by sound identity** in `MuseSamplerWrapper::setupSound` (`musesamplerwrapper.cpp:215-245`, `resolveInstrument :512-549`): if `addTrack(instrumentId)` fails, call `ms_get_matching_instrument_list(setupData.toString(), setupData.musicXmlSoundId)` and take the **first valid** instrument (comment: "TODO: let the user choose").
- `instruments()` (`:317-364`) parses each instrument's `ms_Instrument_get_info_json` (`FriendlyName`, `Abbreviation`, `Category`, `Vendor`, `StaffLines`, `DefaultStaffType`, `DefaultClef`) into `muse::musesampler::Instrument` (used by instrument-picker code to create parts). `drumMapping(id)` returns the library's drum map string. `resolveSoundPresets` lists presets (each gets `playing_techniques` text-articulation list from `ms_get_text_articulations`), `result.front().isDefault = true`; if the lib returns none, a synthetic `"Default"` preset is added (`:226-260`).

### 5.3 `MuseSamplerWrapper` (the `ISynthesizer`)

- One sampler object (`ms_MuseSampler`) per wrapper (per audio track) created lazily in `initSampler` (`:337-366`; stereo only, `AUDIO_CHANNELS_COUNT=2`). `setOutputSpec` re-inits when rate/block change (not in offline mode) (`:131-147`).
- `process` (`:149-192`): applies pending all-notes-off; when **inactive** drains the sequencer's off-stream into `startAuditionNote/stopAuditionNote/addAuditionCCEvent` (`handleAuditionEvents :584-619`); then `ms_MuseSampler_process(sampler, bus, positionSamples)` or `process_offline` in `PlayingOffline`; `extractOutputSamples` **adds** L/R planar buffers into the interleaved output (`:628-638`).
- Position: `setPlaybackPosition` -> `ms_MuseSampler_set_position` is "very CPU-intensive" so it is deferred (`m_pendingSetPosition`) until active or online instrument (`:287-335,621-626`).
- Offline/export: `startOfflineMode(sampleRate)` restarted if the export rate differs (`setMode :100-129`).
- **Online (cloud-rendered) instruments**: `setupOnlineSound` (`:368-399`) sets lazy render, auto-render interval (3 s or off), and a rendering-state callback marshalled to the worker thread; `updateRenderingProgress` (`:401-510`) converts `ms_RenderRangeInfo2` lists into an `InputProcessingProgress` (percentage, chunks, error codes `OnlineSoundsProcessingError`/`OnlineSoundsLimitReached`). `prepareToPlay` / `readyToPlay` poll `ms_MuseSampler_ready_to_play` every 10 ms (`:640-672`); `processInput` triggers render; `clearCache`.

### 5.4 `MuseSamplerSequencer` — MPE to MuseSampler (`musesamplersequencer.cpp`)

**Philosophy difference vs Fluid/VST**: MuseSampler is fed **nominal** timestamp/duration and the *articulation flags*, and does the shaping itself (`event._location_us = nominalTimestamp; _duration_us = nominalDuration`, `:341-342`). Fluid/VST use `actualTimestamp/Duration` and synthesize articulation effects from pattern averages.

- **Track mapping**: `layerIdx = makeLayerIdx(staffLayer, voice)` -> `findOrCreateTrack` (`:196-239`): reuse an already-assigned track, else first unassigned existing track, else `addTrack()`, else fall back to track 0. All tracks are `clear`ed and `finalize`d on each main-stream rebuild (`updateMainStreamEvents :146-158`).
- **Event types consumed (main stream)**: `NoteEvent`, `TextArticulationEvent` (`ms_TextArticulationEvent`, `ordinary_technique` -> `""` to reset), `SoundPresetChangeEvent` (`create_preset_change` per `(track, startUs)` then `add_preset`), `SyllableEvent` (`_hyphened_to_next`) (`loadEvents :266-281`). `ControllerChangeEvent` is **not** consumed in the main stream (only off-stream as CC 1 / 64 / **128 = pitch-bend**, `addAuditionCCEvent :587-609`).
- `NoteEvent` -> `ms_NoteEvent_5 { _voice, _location_us, _duration_us, _pitch (rounded MIDI), _tempo = bps*60, _offset_cents, _articulation (u64 bitmask), _articulation_2 (u64 bitmask), _notehead }` (`apitypes.h:199`). `pitchAndTuning` rounds `12 + level/50` and returns the cent remainder (`:611-621`).
- **Articulation flag mapping** (`parseArticulations :666-711`): looks each `ArticulationType` up in `ARTICULATION_TYPES_PART1` -> `ms_NoteArticulation` (bits 0..59), else `PART2` -> `ms_NoteArticulation2` (handbell extras, Breath, ...), else `NOTEHEAD_TYPES` -> single `ms_NoteHead` (Ghost wins over others). Highlights of PART1 (`:33-99`): Staccato/Staccatissimo/Accent/Tenuto/Marcato/Harmonics/PalmMute/Mute/Open/Pizzicato/SnapPizzicato/ColLegno/SulTasto/SulPonticello; `Legato -> Slur`; `Trill`+`TrillBaroque -> Trill`; `Arpeggio`+`ArpeggioUp -> ArpeggioUp`, `ArpeggioDown`; Tremolo8th/16th/32nd+64th -> Tremolo1/2/3, `TremoloBuzz -> BuzzTremolo`; `DiscreteGlissando -> Glissando`, `ContinuousGlissando`+`Slide -> Portamento`; Scoop/Plop/Doit/Fall; Pre/PostAppoggiatura -> Appoggiatura, Acciaccatura; Upper/UpMordent -> MordentWhole, Lower/DownMordent -> MordentInvertedWhole; Turn -> TurnWholeWhole, InvertedTurn -> TurnInvertedWholeWhole; handbell Martellato family; LaissezVibrer; Left/RightHandTapping. **Unmapped types are silently dropped** (e.g. Vibrato handled separately, FadeIn/Out, Subito, bow directions, ArpeggioStraight*, Distortion...). **Last note of a slur** (Legato with `occupiedFrom != 0 && occupiedTo == 100%`) is skipped — "sampler requirement" (`:680-685`).
- **Ranged articulations** (`isRangedArticulation` from lib): range start emitted *before* the first note of the span (`occupiedFrom==0 && occupiedTo!=100%`), range end *after* the last (`occupiedFrom!=0 && occupiedTo==100%`) (`:322-337,353-363`).
- `Pedal` -> `ms_PedalEvent_2` 1.0 / 0.0 (`addPedalEvent :374-383`). `Multibend` + pitch curve -> `ms_PitchBendInfo` Bezier, clamped +-1200 cents, 1 ms lead-in if starting offset non-zero (`addPitchBends :460-496`). `Vibrato` -> `ms_VibratoInfo`, depth fixed **23 cents** "stand-in data before actual mpe support" (`:498-513`).
- **Dynamics**: automation layers resampled at 30 ms and mapped piecewise-linearly to the library's 0..1 scale (ppppp=1/127, pppp=2, ppp=4, pp=8, p=16, mp=32, mf=64, f=96, ff=120, fff=127) (`dynamicLevelRatio :630-664`), de-duplicated, via `ms_MuseSampler_add_track_dynamics_event_2` on the track of that layer (`loadDynamicEvents :283-304`).
- **Audition (off-stream)**: builds `AuditionStartNoteEvent` with `_active_presets` (`|`-joined cache from preset events, else the default preset), `_active_text_articulation`, `_active_syllable` and "starts at note" flags (`addAuditionNoteEvent :515-568`).

## 6. Reference consumer: FluidSynth (`muse\framework\audio\engine\internal\synthesizers\fluidsynth\fluidsequencer.cpp`)

Included because it defines the *baseline* MPE->MIDI mapping the VST sequencer clones. NoteOn is **MIDI 2.0** with Pitch attribute and 16-bit velocity; CCs/pitch-bend are MIDI 1.0 `ChannelVoice10` events. `expressionLevel` maps `ppp..fff` to CC11 `16..127` in steps of 16 (`:428-454`); SND mode velocity = `expressionCurve.velocityFraction()` (`:417-421`); pitch bend centre 8192, `4096/12` per semitone (range assumed ~+-3 semitones? — see UNCLEAR) (`:456-465`); sostenuto/sustain CCs 66/64; channel assigned by `m_channels.resolveChannelForEvent(noteEvent)` (voice/articulation-aware). Dynamics automation emitted as CC11 on all channels of the layer (`:142-175`). Everything is delivered to the audio engine's `FluidSynth::handleEvent` (then mirrored to the MIDI out port, section 3.2).

## 7. The shared sequencer (`audio\engine\internal\abstracteventsequencer.h`)

All three synth families share `AbstractEventSequencer<Types...>` (owned by `playback-and-audio.md`): holds `mpe::PlaybackData`, subscribes to `mainStream`/`offStream` channels (`load :57-84`), keeps `m_mainStreamEvents` / `m_offStreamEvents` as `map<msecs_t, vector<variant>>` (key is *actually microseconds*, see comments `vstsynthesiser.cpp:179`, `musesamplerwrapper.cpp:293`), `movePlaybackForward(next)` returns the events due in the next block, **off-stream plays only while inactive** (`:167-197`), events are rebuilt lazily when active (or `setUpdateMainStreamWhenInactive`, used for online MuseSampler). Subclasses implement `updateMainStreamEvents(events, dynamics)` and `updateOffStreamEvents(events)`.

## Cross-subsystem dependencies

- **Feeds from**: `src\engraving\playback` (PlaybackModel/renderers/metaparsers -> `PlaybackEventsMap` + setup data; see `engraving-dom.md` and `playback-and-audio.md`), `src\playback` (tracks, `PlaybackData` hand-off to the engine).
- **Plugs into**: `muse\framework\audio` — `synth::ISynthResolver::registerResolver(AudioSourceType::{Fluid,Vsti,MuseSampler}, ...)`, `fx::IFxResolver` (`AudioFxType::VstFx`), `audio\common\audiotypes.h` (`AudioResourceMeta`, `AudioUnitConfig`, `AudioSourceType`, `RESOURCE_TYPE_MAP`: `muse_sampler_sound_pack`, `fluid_soundfont`, `vst_plugin`, `muse_plugin`), `ITransportEventsDispatcher` (MMC from plugins), `audioplugins` (scan/validate/known-plugin cache).
- **UI/other**: `src\notationscene\internal\midiinputoutputcontroller` (ports + remote), `src\preferences` (`audiomidipreferencesmodel` for device pick/MIDI 2.0 toggle), `src\musesounds` (MuseSounds catalog), `rcommand` (commands, ADR 00101), `multiwindows` (mapping file lock), `diagnostics` paths register (`musesampler` lib path, `midiMappingUserAppDataPath`).
- See also `framework-services.md`, `mscore-cli.md` (`--register-audio-plugin`), `ci-and-release.md` (VST ON in CI).

## Non-obvious gotchas

- ⭐ **Units**: `percentage_t` is 10000-based; `pitch_level_t` 50/semitone; timestamps are **microseconds even where the type is named `msecs_t`**.
- ⭐ **Each synth reads a different subset of MPE**: VST/Fluid ignore text articulations/presets/syllables; MuseSampler ignores `ControllerChangeEvent` in the main stream. Adding a new `PlaybackEvent` alternative requires touching all three sequencers.
- ⭐ **MuseSampler uses nominal time; VST/Fluid use actual (articulation-adjusted) time.**
- ⭐ VST module is **OFF by default** locally (`ninja_build.sh:48`); MuseSampler module is **not loaded in debug builds** unless `MUSE_MODULE_MUSESAMPLER_LOAD_IN_DEBUG`.
- VST plugin identity is the **file basename** (`PluginMeta.id`), and only the **first** audio-effect class of a `.vst3` is exposed/instantiated (`vstpluginmetareader.cpp:57-71`, `vstplugininstance.cpp:140-147`) — multi-plugin bundles (shells) expose just one; two plugins with the same file name collide.
- VST lifecycle is thread-sensitive: load, config restore, editor view and teardown run on the **main** thread (`ONLY_MAIN_THREAD`), `controller()`/`midiMapping()`/`pluginSettingsChanged()` are `ONLY_AUDIO_THREAD`.
- Plugins that don't map `kCtrlSustainOnOff`/`kPitchBend` etc. via `IMidiMapping` simply get no pedal/bend (silent drop).
- `ArticulationMap` averaging pairs offset-curve points **by index** (11 expected, 10 % steps), not by position; patterns with other point counts blend wrongly (`mpetypes.h:837-870`).
- Legacy `ms_*` API: a single missing export invalidates the whole MuseSampler library (`isValid`).
- Windows out-port hides software synths (`MOD_SWSYNTH`), so the "Microsoft GS Wavetable" is not offered.
- **Suspected bugs (TRACED code, impact HYPOTHESIS), not fixed**:
  1. `musesampleractioncontroller.cpp:88-90` — `reloadMuseSampler` shows "Could not reload MuseSampler library" when `reloadAllInstruments()` **succeeds** (condition not negated).
  2. `musesamplersequencer.cpp:502` — `(int64_t)0.1 * 1000000` casts first, evaluating to **0**, so `MAX_VIBRATO_STARTOFFSET_US == 0` and the vibrato start offset is always 0.
  3. `winmidiinport.cpp:193` — error text uses `errorString(ret)` (the old OK `Ret`) instead of `openRes`, so open failures are reported as "UNKNOWN".
  4. `midievent.h:1215,1221` — `2 ^ (srcBits - 1)` is C++ **XOR**, not power; `scaleUp` center/mask are wrong (low practical impact: only perturbs low bits of 1.0->2.0 scaling).
  5. `events.h:95` — `ExpressionContext::operator==` compares `velocityOverride == velocityOverride` (self-compare), so velocity differences are never detected.
  6. `vsttypes.h:149-209` — `std::less` specializations for `ParamChangeEvent`/`NoteOnEvent`/`NoteOffEvent`/`VstEvent` use `&&`/`||` chains that are not strict weak orderings (dangerous if ever used as ordered keys; appear unused by current containers).
  7. `libhandler.h:353,355` — `printApiStatus` prints `ms_MuseSampler_add_pitch_bend` twice (cosmetic).

## UNCLEAR — needs verification

- VST3 SDK licensing (Steinberg dual GPLv3/proprietary): the source is pulled from public `steinbergmedia` repos at build time; the licence terms were **not** read from the tarballs.
- Whether `spec.cmake`'s default (non-system) path downloads the three tarballs automatically for developers (the `meta.cmake` hook only handles `mode == "system"`).
- FluidSequencer pitch-bend range: `4096/12` per semitone with centre 8192 implies a +-24 semitone range assumption, not verified against the Fluid-side bend-range setting.
- Exact semantics of MuseSampler `ms_NoteEvent_5._tempo` vs. tempo changes (MuseScore passes `bps*60` per note).
- `MidiRemote::setCurrentActionEvent` unimplemented; role of `isSettingMode` setter callers not traced (UI model).
- Which `RunMode`s/config presets register `IMidiRemote` contexts with MMC (MMC only active when `MUSE_MODULE_MIDIREMOTE`).

*Authored from: full reads of `muse\framework\mpe\{mpetypes,events,automationpoint,soundid,playbacksetupdata}.h` + profiles repository; `midi\{midievent,miditypes,imidi*,midimodule,midiconfiguration,midideviceslistener,win ports}`; `midiremote\{midiremote,types,mmcdecoder,module,configuration}`; `vst\{module,types,repository,scanner,metareader,instance,provider,register,audioclient,synthesiser,sequencer,resolver,fx,handler,configuration,actionscontroller,sdk CMake}`; `musesampler\{module,configuration,libhandler,apitypes,resolver,wrapper,sequencer,actioncontroller}`; `audio\...\fluidsequencer.cpp`, `abstracteventsequencer.h`; build wiring (`SetupConfigure.cmake`, `MuseDeclareOptions.cmake`, `appfactory.cpp`, vst3sdk recipe) (2026-11-08). Not read end-to-end: QML views (`VstView`, `VstEditorDialog`, midiremote QML, mpe profile editor), CoreMIDI/ALSA port bodies, `src\musesounds`, per-profile JSON content beyond a sample. **If this contradicts the live source, the source wins.***

