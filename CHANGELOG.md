# Changelog

All notable changes to Reprise are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and Reprise aims to follow [Semantic Versioning](https://semver.org/). Everything
below `1.0.0` is pre-release.

## [Unreleased]

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

### Fixed
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

[0.1.0]: https://github.com/Amadeus-/Reprise/tree/reprise-main
