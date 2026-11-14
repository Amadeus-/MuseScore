# Changelog

All notable changes to Reprise are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and Reprise aims to follow [Semantic Versioning](https://semver.org/). Everything
below `1.0.0` is pre-release.

## [Unreleased]

## [0.9.3] — 2026-11-13

### Changed
- **Import Scan dialog redesigned with a read-only header**: every import now shows a muted
  header with the file name, the page count, and — for images — the native resolution
  (e.g. **2581 x 3495 px (native)**, with a **(~300 dpi)** suffix only when the image file
  actually carries resolution metadata). The settable controls sit below it: a PDF keeps the
  recognition-quality tier (with a caption naming the detected dpi that drove the match) and
  the page range; a single-page image shows no settable control, just a one-line note; a
  multi-page TIFF keeps the page range.

### Fixed
- **Resolution line restored on multi-page TIFF**: the native resolution now appears for all
  images in the header, including multi-frame TIFFs (previously only single images showed it).

## [0.9.2] — 2026-11-13

### Fixed
- **Oversized-image pre-flight now covers TIFF files**: a too-large TIFF was silently
  bypassing the 20-megapixel guard because this build's Qt ships no TIFF image plugin, so
  `QImageReader` reported it as 0×0 and the check passed. The pre-flight now reads TIFF
  dimensions with the in-house TIFF reader (the largest frame, since Audiveris caps
  resolution per sheet) and blocks an oversized TIFF up front, matching the other images.

## [0.9.1] — 2026-11-13

### Fixed
- **Multi-page TIFF page selection**: the intake dialog now shows the page range for a
  multi-page (multi-frame) TIFF. The page count is read by an in-house walk of the TIFF
  frame directories, because this build's Qt ships no TIFF image plugin — so `QImageReader`
  could not read a TIFF at all and the field was wrongly hidden.

### Added
- **Single-image intake shows the scan's native resolution**: for a single image, the
  quality tier (which does not apply to an image) is replaced by a read-only line reading
  e.g. **Scan: 2581 x 3495 px (native resolution)**.

### Changed
- **Page range hidden for single-page PDFs too** (consistent with single images), with a
  safe-fallback to **shown** whenever the page count is uncertain (a compressed
  object-stream PDF, or any ambiguity) so pages are never silently dropped.

## [0.9.0] — 2026-11-13

### Added
- **Import image files (PNG / JPG / JPEG / TIFF / BMP / GIF) alongside PDF** via Open, New,
  and the MCP `reprise.import-pdf` tool — the same Audiveris recognition path now accepts a
  plain scan image, not just a PDF.
- **Multi-page TIFF page selection**: a multi-frame TIFF is treated as multiple sheets, and the
  page-range field appears for it just as it does for a PDF (single-page images skip it).
- **Oversized-image pre-flight**: an image whose pixel count exceeds the recognizer's limit
  (20 megapixels) is now blocked up front with a clear message, in both the dialog and the MCP
  tool, instead of failing mid-recognition with a raw Audiveris exit code.

### Changed
- **"Import PDF" renamed to "Import Scan"** throughout the intake flow.
- **The recognition-quality tier and the page-range field are hidden for single-image inputs**
  (an image has no PDF resolution to pick and is a single sheet).

## [0.8.0] — 2026-11-13

### Added
- Dev MCP tool `reprise.capture-screenshot` (target=window | score) that captures the GUI/rendered score to a PNG and returns its path.

### Changed
- **Recognition-quality tier labels now show the dpi** — **Faster (200 dpi)** / **Standard (300 dpi)** / **Best (400 dpi)**; the descriptions after each label and all behavior are unchanged.
- **Online/cloud module compiled out — Reprise is now an offline-only binary**: the umbrella
  `MUSE_MODULE_CLOUD` is OFF (the muse framework's cloud module is replaced by a stub), Qt
  NetworkAuth is no longer linked, and the account / cloud-scores / save-to-cloud surfaces are
  gone or inert.
- **Synced with upstream MuseScore `main`** (2026-11-13 merge).

### Fixed
- **Agent/headless-launched instances now self-clean their session** on exit, so the user no
  longer gets a spurious "restore previous session?" prompt on the next normal launch.

## [0.7.0] — 2026-11-12

### Added
- **PDF import "Recognition quality" selector** (**Faster** / **Standard** / **Best**)
  that auto-detects the PDF's native resolution and pre-selects the matching tier, showing
  a **"Matched to your PDF"** badge when a real embedded-image resolution is detected (and
  falling back to **Standard** otherwise). The MCP `reprise.import-pdf` shares the same
  auto-detect when no dpi is given.

### Changed
- **Import-PDF intake dialog recognition control reworked**: the free-form DPI input box
  (and its 200–400 range validation) is replaced by the Recognition-quality selector above,
  and all DPI jargon is removed from that UI.

## [0.6.0] — 2026-11-12

### Changed
- **Top menu bar stripped down for Reprise**, continuing the 0.4.0 GUI-strip theme:
  the composition/engraving entries irrelevant to the scan-and-correct workflow are
  gone. **File** loses **Save selection** and **Open parts**, and the **"Save other"**
  submenu is flattened to a direct **Save a copy**. **Add ▸ Text** drops the
  **chord symbol**, **Roman numeral**, **Nashville number**, **figured bass**, and
  **instrument change** items. The **Help** web and update items are kept visible but
  grayed out (disabled).
- **Scans page default view is now List instead of Grid.**

### Removed
- **View menu**: removed the **Instruments** / **Braille** / **Percussion** panel
  toggles and the **Workspaces** entry.
- **Add menu**: removed the **Chords & fretboard diagrams** submenu.
- **Tools menu**: removed **Explode**, **Implode**, **Realize chord symbols**,
  **Fill with slashes**, and **Toggle rhythmic slash notation**.

### Added
- **Build-type chooser** (Release default / Debug / RelWithDebInfo) in the helper
  build scripts.

### Fixed
- **Notation MCP query structs decoupled from upstream's renamed field-declaration
  macro**, fixing a build break after an upstream sync.
- **+Update now builds before pushing** `reprise-main` (+Build propagates the failure
  exit code), so a broken tree is no longer pushed.

## [0.5.0] — 2026-11-12

### Added
- **Agent-testable MCP control surface**: Reprise's built-in MCP server now exposes a
  focused, agent-facing toolset so the Import-PDF pipeline can be driven and verified
  headlessly (no GUI clicks).
  - **Six read-only state queries**: `project.get-state` (whether a scan is open, its
    name/path, unsaved state), `score.get-counts` (measure / part / instrument / staff /
    page counts), `score.get-health` (corrupt and irregular measures), `notation.get-editstate`
    (whether editing is available), and `reprise.get-import-progress` / `reprise.get-import-errors`
    (live Import-PDF / OMR progress and errors).
  - **Headlessly-drivable Import PDF**: `reprise.import-pdf` accepts optional `path` / `pages`
    / `dpi` arguments, so an import can be started directly, skipping the file picker and
    options dialog.
- **Separate Release PDB** carried forward from 0.4.0 lets these be developed and crash-debugged
  with full symbols.

### Changed
- **Curated MCP toolset**: the MCP tool list is trimmed to the agent-facing set (the six
  queries above + `reprise.import-pdf` + the core `project.open` / `close` / `save` /
  `save-as` / `export`). Every other command has only its **MCP** availability bit cleared —
  the plugin API, menus, keyboard shortcuts, and all in-app behaviour are untouched.

## [0.4.0] — 2026-11-11

### Changed
- **Home/shell stripped down for Reprise**: the sidebar is trimmed (removed
  **MuseSounds** and **My accounts**), **"Scores"** is renamed to **"Scans"**, and
  **"New"** / **"New Scan"** are rewired to the **Import PDF** flow. Removed the
  welcome promos, and applied a targeted user-facing **"Score" → "Scan"** rename
  across the shell. The **Learn** page is now a **"Coming soon"** stub.
- **Top ribbon "Score" tab renamed to "Edit"**, and disabled/grayed until a scan is
  open — Reprise is edit-only.
- **Separate PDB for Release builds** (`/Z7`): Release crashes can now be symbolized
  without bloating the shipped binary.

### Removed
- **Publish tab and all cloud/online UI**: the Publish tab and the remaining
  cloud/online surfaces (and their promos) are gone — Reprise is fully offline.

## [0.3.0] — 2026-11-11

### Added
- **Auto-detect OMR paths on first Preferences open**: if the Audiveris or Tesseract path has
  never been set, the Reprise page detects it on open and saves it so it stays; if the program
  is not installed the field reads "Not detected" instead of being blank.
- **Reprise Preferences page**: a new **Reprise** section in Edit > Preferences (placed
  after Import) collecting the recognition settings in one place — the **Audiveris** and
  **Tesseract** program paths (each with a **Detect** button that finds the installed
  program and fills the field), the **scan resolution** (200–400 dpi), and the **"close
  the progress window after a successful import"** toggle.
- **Audiveris/Tesseract auto-detection** matching the original Reprise search order: the
  PATH first, then the standard install folders under Program Files / Program Files (x86) /
  ProgramW6432 / LocalAppData for Audiveris, and `…\Tesseract-OCR\tesseract.exe` for
  Tesseract. A path you set by hand always overrides detection.
- **Tesseract path drives OCR**: when a Tesseract program path is set, `TESSDATA_PREFIX`
  for Audiveris' OCR is derived from it (its sibling `tessdata` folder); otherwise the
  previous automatic detection next to Audiveris is used.

### Changed
- **Detect status is now transient**: the green **"Found: &lt;path&gt;"** confirmation fades
  out after 2 seconds and its status row then auto-collapses, so no empty gap is left behind.
  The detected path itself stays in the field — only the dynamic status line fades.
- **"Not detected" status stays persistent**: when a program is not found the "Not detected"
  status remains visible and does not fade, so the row is never ambiguously blank.

## [0.2.0] — 2026-11-11

### Added
- **Recognition progress popup**: Import PDF now runs Audiveris asynchronously and
  shows a branded progress window — a navy header (Reprise mark + wordmark + title,
  with a gold check badge once finished) over a cream card. It shows the file, page
  range and dpi; a live current-stage line ("Analyzing page X of N — detecting
  staves…"); a gold progress bar on a tan track with a "Page X of N" / percent
  caption; a **Details** expander that reveals the live Audiveris recognition log;
  and a **Cancel** button (gold) that stops recognition and aborts the import. On
  success the step line becomes "Done — recognized M measures across N pages.", the
  score opens, and a navy **Close** button appears; on failure the window stays up
  with the Audiveris error and a tail of its output instead of vanishing.
- **"Close this window after importing"** option in the popup (remembered
  between runs, **off by default**): when checked, the window closes itself
  automatically once the score has opened.
- **Audiveris output in the log file** (internal diagnostics): the recognition
  command line, every stdout/stderr line and the exit code are now also written to
  the Reprise log file, tagged `[audiveris]`, so failed imports can be diagnosed later.

### Fixed
- **Progress popup started at "page 2 of 2" with a full bar on a multi-page PDF with a
  page range**: Audiveris prints `Book reaching PAGE on sheets:[#1#2]` before any sheet
  starts, and the sheet parser read its trailing `#2]` as the current sheet. The current
  sheet is now read only from the leading `[<book>#N]` log tag. The popup also resets all
  per-import state when a run begins.
- **Recognition popup header mark**: the header now shows the full cream-R +
  gold-blade + two-notes mark (the transparent `titlebar-mark` glyph) at the 52px
  size of the approved mockup, instead of the small app badge (a navy rounded square
  whose inner glyph read tiny against the navy header).
- **Progress total / backwards bar on some PDFs**: the sheet total is now fixed up
  front from a reliable source — the requested page count when a range was given,
  otherwise the book's one-time "N sheets" line, locked so a later stray "N sheet(s)"
  line can no longer clobber it down to 1. The current sheet is clamped to the total
  (no more "page 3 of 1") and both the bar and the "page X" caption are monotonic, so
  progress never regresses.
- **Import PDF crash on launch**: the recognition popup's view-model
  (`ImportProgressModel`) resolved its IoC context inside its constructor, but a
  QML-instantiated object has no `QQmlContext` yet during construction — the
  injected `IProjectSession` came back null and the app crashed (SIGSEGV) the moment
  an import began. The context-dependent setup now runs from `load()` (driven by the
  popup's `Component.onCompleted`), matching the other MuseScore QML dialog models.

## [0.1.0] — 2026-11-10

Initial pre-release. Reprise is established as a fork of
[MuseScore Studio](https://github.com/musescore/MuseScore) — rebranded and slimmed —
with the first slice of the PDF scan-and-correct workflow.

### Added
- **Import PDF** (*File ▸ Import PDF*): runs [Audiveris](https://github.com/Audiveris/audiveris)
  optical music recognition on a PDF and opens the recognized score via MuseScore's
  native MusicXML import.
- **Intake dialog**: choose a page range (`all`, `1-5`, `1,3,8`) and a scan
  resolution (200–400 dpi, default 300) before recognition.
- **Last-folder memory**: the Import-PDF picker reopens at the folder you last
  imported from, independent of MuseScore's Open dialog.
- Reprise branding throughout (name, app icon, splash, About) and version `0.1.0`.

### Changed
- Forked from MuseScore Studio; **disabled the auto-update check and all telemetry /
  phone-home endpoints**.
- Replaced the cloud "Convert file…" menu item with the local Audiveris Import-PDF
  flow (Reprise has a single, offline OMR path).
- Slimmed the build: excluded debug DLLs and non-English locales and disabled
  unused modules (import/export formats, cloud/learn/tours/etc.) while keeping the
  editor, playback, and MusicXML core.

### Temporary
- Every imported part currently defaults to **Grand Piano**, overriding the
  instruments OMR guesses — a deliberate stopgap until instrument selection is
  added to the import flow.

[0.9.3]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.9.2]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.9.1]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.9.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.8.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.7.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.6.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.5.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.4.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.3.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.2.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
[0.1.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
