# MuseScore — Guide CORE / Index

> **What this file is.** The entry point and connective tissue for the MuseScore developer Guide — a point-in-time synthesis of how the **MuseScore 5.0 (`main`) source at `C:\Dev\+MuseScore+`** is built, so an AI assistant (or a human) can fix bugs, add features, write plugins, and answer questions about the whole codebase. **Read this file first every session, then load ONLY the topic file(s) the task needs** (the map in §3). This is connective tissue, not a content dump.
>
> **Authoritative-source precedence (high → low):**
> 1. **The live source tree** `C:\Dev\+MuseScore+` (and its submodules `muse\` + `muse_deps\`) — ground truth. **Re-Read/Grep before asserting current contents or editing.** If a Guide file contradicts the live code, **the live code wins** — re-verify and fix the Guide.
> 2. In-repo docs: the ADRs (`muse\docs\adr\`), per-module `README.md`s, `CONTRIBUTING.md`, `AI_POLICY.md`, `src\engraving\rw\README.md`, `Doxyfile.plugins`.
> 3. Upstream online docs (handbook, developer handbook, plugin API reference) — secondary; versions drift.
> 4. This Guide — a synthesis; always re-verify fragile detail (class layouts, file paths, line numbers) against (1).

---

## 1. Project identity

- **App:** MuseScore — open-source music notation software (scorewriter + playback). GPL-licensed.
- **Local version:** **5.0.0** dev line, branch `main`. (MuseScore 3.x and 4.x differ **greatly**; this Guide documents the local 5.x `main` tree only. Never assume 3.x/4.x behavior.)
- **Repo:** origin `https://github.com/Amadeus-/MuseScore` (the user's fork) · upstream `https://github.com/musescore/MuseScore`.
- **Submodules (TWO — update SEPARATELY on every upstream sync):** `muse\` = the **Muse Framework** (submodule `muse_framework`, `github.com/musescore/muse_framework.git`, ~40 modules under `muse\framework\`) · `muse_deps\` = third-party dependency recipes/manifests (submodule `muse_deps`, `github.com/musescore/muse_deps.git`, ~60 libs). ⛔ The superproject pins exact commits; a `git pull` does NOT move the submodule trees — after ANY upstream sync run `git submodule update --init --recursive` or you build against stale framework/deps. Mechanics: the agent's `playbooks\git-and-upstream-sync.md`.
- **Tech stack:** C++ (C++17/20) + **Qt 6** (Quick/QML primary UI, minimal Widgets) + **CMake/Ninja**. Architecture: **modular** with an **IoC / dependency-injection** container and **MVVM** QML view-models. Design rationale is written down in the **ADRs** (`muse\docs\adr\`).

## 2. Who uses this Guide / how to use it

- **Audience:** the `MuseScore-Expert` agent (and the author). The agent is pointed here by its definition file; it reads `00-core.md` first, then dispatches to topic files via §3.
- **Domain vs workflow split:** this **Guide** holds **MuseScore domain knowledge** (how the code works). **Operational playbooks** (how to build, troubleshoot, add a feature, develop a plugin, sync the fork) live with the agent at `~/.claude/agents/musescore/playbooks/` — not here.
- **Citing:** this Guide is for internal dev use against full source — cite `file:line` freely (e.g. `src\engraving\dom\note.cpp:1234`).

## 3. Guide file map — load ONLY what the task needs

> Status legend: ✅ deep (source-verified) · 🟡 partial · ⬜ STUB (orientation only — deep pass pending). Keep the **§8 status table** in sync as passes complete.

| File | Load when… |
|---|---|
| **`00-core.md`** (this) | always first — identity, precedence, the file map, conventions, status/known-gaps. |
| `architecture-and-ioc.md` | understanding the module system, the IoC/DI container (`modularity`), `IModuleSetup`, interface/impl/view-model layering, the ADR map. |
| `app-shell-and-startup.md` | the entry point (`src\app`), GUI vs console split, `src\appshell` (main window, startup scenario, menus, sessions, commands controller). |
| `engraving-dom.md` | the score object model — `Score` → `Measure` → `Segment` → `ChordRest`/`Note` and the ~150 `EngravingItem` classes (`src\engraving\dom`). |
| `layout-and-rendering.md` | layout passes & painting (`src\engraving\rendering`: `score\`, `single\`, `editmode\`). |
| `style-system.md` | the style system (`src\engraving\style`) — `Sid`, `MStyle`, style defaults/propagation. |
| `file-format-and-rw.md` | `.mscz`/`.mscx` on disk, `src\engraving\rw` versioned readers (`read114…read500`)/writers, and the `src\project` layer. |
| `notation-layer.md` | the notation facade over engraving (`src\notation`): `INotation`, selection, note input, undo stack, parts/excerpts, the commands system. |
| `playback-and-audio.md` | playback (`src\playback`) + the Muse **audio engine** (`muse\framework\audio`: engine/mixer/synth), and engraving→playback event conversion. |
| `midi-vst-musesampler.md` | MIDI (`muse\framework\midi`, `midiremote`), the **VST3 host** (`muse\framework\vst`), **MuseSampler** integration, and **MPE** (expression model). |
| `ui-qml-mvvm.md` | the Qt Quick/QML UI, MVVM view-models, `muse\framework\ui` + `uicomponents`, theming/navigation, `dockwindow` (KDDockWidgets). |
| `framework-services.md` | cross-cutting framework services: `global` (IoC/logging/settings/async/io/serialization), `actions`, `shortcuts`, `workspace`, `cloud`, `network`, `update`, `extensions`. |
| `importexport.md` | `src\importexport\*` — MusicXML, MIDI, GuitarPro, MEI, MNX, Capella, BWW, etc.; plus export (audio/image/lyrics/video). |
| `plugin-api.md` | the **QML plugin API** (`src\engraving\api\v1`: `QmlPluginApi`, cursor, elements, score, enums), version differences (2→3→4→5), bundled plugins in `share\plugins`. |
| `extensions-system.md` | the **extensions** framework (`muse\framework\extensions`): the extension/plugin manager, installer, QML + native-code plugins (ADR 00102). |
| `testing-and-vtest.md` | gtest unit tests (per-module `tests\`), `vtest\` visual regression, `test\` legacy data. |
| `ci-and-release.md` | `.github\workflows\*`, `buildscripts\` packaging per platform, Transifex translations, Sentry/crashpad. |
| `contributing-and-conventions.md` | code style (clang-format/uncrustify vs the user's own style), `CONTRIBUTING.md`, **`AI_POLICY.md`**, commit/PR conventions, the fork↔upstream relationship. |
| `mscore-cli.md` | the `mscore`/`MuseScore` command line — batch conversion (`-o`, `-j` job file, `-S` style), the `converter` module, headless use. |

*(Operational HOW-TO lives in the agent's `playbooks/`: `build-from-source.md`, `troubleshooting.md`, `add-feature-or-fix.md`, `plugin-development.md`, `git-and-upstream-sync.md`.)*

## 4. Per-file skeleton (every topic file follows this)

1. `> **What this file is.**` blockquote — scope + the precedence reminder.
2. `## 0. 60-second orientation` — what the subsystem is, where it lives, the 3–5 files/classes that matter most.
3. The **mechanism** — how it works, with `file:line` cites and short fenced code excerpts (function/class named in a comment).
4. The **inventory** — exhaustive tables (class/member/type/meaning; option/effect; event/trigger) once the deep pass is done.
5. `## Cross-subsystem dependencies` — what this leans on / feeds, with `see \`other-file.md\`` pointers.
6. `## Non-obvious gotchas` and `## UNCLEAR — needs verification`.
7. Footer: what the file was authored from + the "live source wins" reminder.

## 5. Conventions used across this Guide

- **Markers:** ⛔ = never/hazard · ⭐ = key fact · `UNCLEAR — needs verification` = not yet source-confirmed · STUB = orientation only, deep pass pending.
- **Cites:** `path\to\file.cpp:NN` relative to `C:\Dev\+MuseScore+`. Submodule paths start `muse\` or `muse_deps\`.
- **Code style (when WRITING/EDITING MuseScore source):** the user's global style rules apply to code authored for this fork (Allman braces, fixed-width integer types, etc.). Note that **upstream MuseScore uses its own `.clang-format`/uncrustify style** which conflicts; see `contributing-and-conventions.md` for how to reconcile per-change (match surrounding file for upstream-bound edits; user style for author-private code). Flag — don't silently pick.
- **Flag bugs, don't silently "fix":** if a deep pass spots a real bug in MuseScore source, record it in that file's gotchas and surface it — do not change source outside the task.

## 6. Architecture one-screen (orientation)

- **Everything is a module.** Each module has a `*module.h/.cpp` implementing `IModuleSetup`, registering interfaces (`I*.h` at the module root) into the **IoC container** (`muse\framework\global\modularity`: `ioc.h`, `imodulesetup.h`). Implementations live in `internal\`; QML view-models in `qml\`. Modules are wired via `muse\framework\cmake\MuseModules.cmake`. → `architecture-and-ioc.md`.
- **Two trees:** `src\*` = MuseScore app modules (engraving, notation, project, appshell, importexport, playback, palette, …). `muse\framework\*` = the reusable framework (global, ui, audio, vst, extensions, …).
- **The notation stack:** `engraving` (the DOM + layout + style + rw, the heavy lifting) ← `notation` (interaction/undo/commands facade) ← `notationscene`/QML UI. → `engraving-dom.md`, `notation-layer.md`.
- **The audio stack:** `src\playback` → `muse\framework\audio` (engine/mixer/synth) + `vst`/`musesampler`/`midi`/`mpe`. → `playback-and-audio.md`.
- **Read/write:** `.mscz` (zip) wraps `.mscx` (XML); `src\engraving\rw` has versioned readers; `src\project` owns the file lifecycle. → `file-format-and-rw.md`.

## 7. The ADRs (read these early — they justify the architecture)

`muse\docs\adr\` — Architecture Decision Records. Key ones: `00001` Modularity · `00002` Dependency-injection · `00003` AppShell · `00004` MVVM · `00005` Interaction-workflow · `00006` Async interaction · `00101` Commands (replacing actions) · `00102` Extensions (native-code plugins) · `00103` Audio automation · `00104` Command-first · `00108` Shortcuts scope. Cite them in `architecture-and-ioc.md`.

## 8. Status / known-gaps table (KEEP IN SYNC)

> This doubles as the deep-pass progress tracker. Target end state: **every file ✅ (exhaustive, source-verified)**.

| File | Status | Notes |
|---|---|---|
| 00-core.md | 🟡 | Index authored; refine as topic files land. |
| architecture-and-ioc.md | ✅ | Deep pass 1 (2026-11-08): module model, kors_modularity DI (`GlobalInject`/`ContextInject`), BaseApplication boot phases, contexts=windows, CMake wiring, 14 ADRs. |
| app-shell-and-startup.md | ✅ | Deep pass 3 (2026-11-08): entry (main→appfactory real/stub→gui/console), startup scenario (5 StartupModeTypes + Recovery), appshell module exports, AppshellCommandsController, app menu, sessions/recovery, CLI. |
| engraving-dom.md | ✅ | Deep pass 1+2+5 (2026-11-08): base classes, property system, full score spine, all 155 ElementTypes, member tables for EVERY concrete element class (§2.15, ~95 more), complete P_TYPE→storage table, editing/undo. Residual: per-element getProperty/setProperty switch bodies + behavioral .cpp traces. |
| layout-and-rendering.md | ✅ | Deep pass 2+5 (2026-11-08): renderers, LayoutContext, dirty-range trigger, pipeline, spacing, paint path, + per-element algorithms (TLayout, chord/accidental/stem/ledger, beam slant math, slur avoidCollisions bezier, tuplets, text/lyrics/harmony, header/footer, stave-sharing). |
| style-system.md | ✅ | Deep pass 2 (2026-11-08): MStyle store + Sid (2055 ids), Pid↔Sid styled-property resolution + STYLED/UNSTYLED lifecycle, 75 text styles, DefaultStyle + legacy defaults, .mss read/write + shipped files, undo commands. Flagged: v450 legacy-defaults missing from engraving.qrc; Sid table is positional/unchecked. |
| file-format-and-rw.md | ✅ | Deep pass 2+5 (2026-11-08): container, version ladder, project lifecycle, + per-element XML read/write mechanism and tag tables (6 element groups), order sensitivities, add-a-property guide. Flagged: Articulation `channe` write/read typo loses channel on round-trip. |
| notation-layer.md | ✅ | Deep pass 2+5 (2026-11-08): INotation facade + sub-interfaces, shared undo stack, rcommand dispatch, + interaction/editing state machine (EditData/grips), drag/drop resolvers, text/lyrics editing, impl-body tables. Residual: ~150 selection-op bodies tabulated by idiom. |
| playback-and-audio.md | ✅ | Deep pass 3 (2026-11-08): main↔engine RPC boundary, Hybrid work-mode (render in driver callback), per-track node graph (synth→FX→automation→meter→mixer), PlaybackModel event production, PlaybackController, sound profiles/flags. Flagged: possible driver-callback null-deref race. |
| midi-vst-musesampler.md | ✅ | Deep pass 3 (2026-11-08): MPE event model (variant events, articulation profiles), MIDI 2.0/UMP ports + remote/MMC, VST3 host (separate-process scan, instance keying, state), MuseSampler dlopen binding (~65 symbols). Flagged: 7 suspected bugs. |
| ui-qml-mvvm.md | ✅ | Deep pass 3 (2026-11-08): per-window UiEngine+IoC ctx, MVVM walk-through, qt_add_qml_module/QML_FOREIGN registration, 4-theme system, Navigation accessibility tree, interactive dialog flow, uicomponents, dock_v2 (confirmed built). 724 .qml files. |
| framework-services.md | ✅ | Deep pass 3 (2026-11-08): global foundation (async/kors_async, Settings, Ret/Val/Uri types, io, serialization), actions↔rcommand↔shortcuts_v2 triad (ADR 00101/00104/00108), cloud/network/update/workspace. Flagged: ADR 00107 vs commandtypes.h availability-default drift. |
| importexport.md | ✅ | Deep pass 4+5+6 (2026-11-08): framework, inventory, + §8 full per-format internals — MusicXML (pass1/pass2, notations, directions, exporter writers, element→DOM table), MIDI (MidiFile parser, quantization/tuplet/hand-split/voice/clef DPs, MTrack/MidiChord, exportmidi), GuitarPro (GP1-7 + GPX tables + bend/dive + PowerTab), MEI (read/write), MNX, Capella/OVE/BB/BWW/MuseData/TablEdit, svggenerator + video. COMPLETE — §10 cleared (round-4 leaf bodies done; nothing substantive outstanding). Flagged: BB single-voice/dropped-notes; dead GP SPLIT_CHORD_DURATIONS path; no video piano overlay; MNX skips TAB parts. |
| plugin-api.md | ✅ | Deep pass 1 (2026-11-08): PluginAPI/Cursor/Score surface, 107 enums, worked example, discovery/enable/launch, user-plugins dir (Documents\MuseScoreStudio5\Plugins). |
| extensions-system.md | ✅ | Deep pass 4 (2026-11-08): manifest model, manager/provider + install flow, JS/QML MuseApi runtime, UI integration, bundled extensions, legacy-v1-vs-modern. Flagged: native-code plugins (ADR 00102) are design-only / not implemented in this tree. |
| testing-and-vtest.md | ✅ | Deep pass 4 (2026-11-08): gtest framework (gmain/Environment, module tests\, declare/build/run), test idioms (ScoreRW/ScoreComp), the TWO vtest mechanisms (checked-in drawdata vs CI base-vs-head AppImage PNG compare), shared test data. |
| ci-and-release.md | ✅ | Deep pass 4 (2026-11-08): 22 workflows, platform build/package matrix, build-number scheme (yymmddHHM), build-mode/channel mapping, deploy.yml release flow, Transifex, Crashpad→Sentry. Flagged: `$/` in reusable uses:, deploy sentry_project 'sandbox' mismatch, stale 4.7 constants. |
| contributing-and-conventions.md | ✅ | Deep pass 4 (2026-11-08): contributing workflow, uncrustify 0.74 style (K&R control / Allman funcs, 4-space, width 140; no root .clang-format), AI_POLICY (Assisted-by: trailer — NOT Co-authored-by; PR AI box), commit/PR conventions, fork↔upstream. |
| mscore-cli.md | ✅ | Deep pass 4 (2026-11-08): full flag reference (every addOption w/ file:line), convertFile algorithm, -j batch job-file schema + examples, -S/transpose/region/meta, error codes, headless notes (Linux needs MU_QT_QPA_PLATFORM + QT_QPA_PLATFORM = offscreen). |

*Authored from: the MuseScore 5.0 `main` source tree recon (top-level layout, module map, ADR titles, build system) on 2026-11-08. Everything marked STUB is orientation-only until its deep, source-verified pass. **If this contradicts the live source, the source wins.***
