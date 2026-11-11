# Changelog

All notable changes to Reprise are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and Reprise aims to follow [Semantic Versioning](https://semver.org/). Everything
below `1.0.0` is pre-release.

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
