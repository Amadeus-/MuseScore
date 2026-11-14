<p align="center"><img src="share/icons/reprise_readme_banner.png" alt="Reprise" width="760"></p>

**From scan to score.** Open a PDF of sheet music, recognize it (OMR), correct it, and export.

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.en.html)

Reprise is a focused music-scanning and correction tool built on
[MuseScore Studio](https://github.com/musescore/MuseScore). It takes a scanned PDF
of sheet music, runs optical music recognition (OMR) with
[Audiveris](https://github.com/Audiveris/audiveris), and gives you a streamlined
workflow to correct the result and export clean **MusicXML** — or hand the score
onward to MuseScore to finish.

> **Pre-release (0.9.0) — work in progress.** Reprise is early software under
> active development. Expect rough edges and changing behavior.

See [`CHANGELOG.md`](CHANGELOG.md) for the version history.

## Relationship to MuseScore

Reprise is a **fork of MuseScore Studio** and is built directly on MuseScore's
notation, engraving, and playback engine — the editing you do in Reprise is
MuseScore-native. All of MuseScore's copyright notices, author credits, and
license terms are preserved in this repository (see
[`LICENSE.txt`](LICENSE.txt) and the per-file source headers). Nothing in
MuseScore's attribution has been removed.

- Upstream project: https://github.com/musescore/MuseScore

Reprise is an **independent fork** and is **not affiliated with, sponsored by, or
endorsed by** MuseScore or Muse Group. *"MuseScore"* is a trademark of its
respective owners; Reprise uses the name only to describe its origin and
compatibility.

## Features

**Working now**
- **Import a PDF or image** — *File ▸ Import Scan* (or the **New** tile) runs Audiveris
  OMR on a PDF or a scan image (PNG/JPG/JPEG/TIFF/BMP/GIF) and opens the recognized score.
  Pick a page range (`all`, `1-5`, `1,3,8`) and a **Recognition quality** — *Faster /
  Standard / Best*. Reprise reads the PDF's own resolution and pre-selects the matching
  quality for you.
- **Live recognition progress** — a progress window shows each page as Audiveris
  works, with a recognition log and a cancel option.
- **Streamlined, correction-focused UI** — the interface is pared down around
  scanning and correcting: a **Scans** home, a dedicated **Edit** workspace, and
  menus trimmed to what the workflow needs.
- **Easy setup** — Preferences auto-detects your Audiveris and Tesseract installs on
  first run, with manual override.
- **Fully offline** — no account, sign-in, or network connection required; the
  online/cloud module is compiled out entirely.
- **Built on MuseScore's editor** — full score editing, **MusicXML / MIDI import and
  export**, and **playback**.

**Temporary behavior**
- Imported scores currently default **every part to Grand Piano**, overriding the
  instruments the OMR guesses. A deliberate stopgap until instrument selection is
  added to the import flow.

**Planned**
- A two-pane **scan ↔ score** correction view — the scanned page shown alongside
  the editable score, kept position-synced.
- A **bad-timing navigator** and a correction-focused keypad for fast fixes.
- Richer import/OMR options and lyric/text OCR.

## License

Reprise is licensed under **GPL-3.0-only** — the same license as MuseScore Studio.
See [`LICENSE.txt`](LICENSE.txt). MuseScore's original license and attribution are
retained in full.

## Requirements

- **[Audiveris](https://github.com/Audiveris/audiveris)** (installed separately) —
  performs the PDF optical music recognition. Reprise auto-detects it at its
  default install location.
- **Tesseract** OCR language data — needed only for lyric/text recognition; note
  recognition does not require it.

## Building & running

> **Status: WIP / TBD.** Full, platform-general build instructions are still being
> written. Reprise uses the standard MuseScore build system (Qt 6, a C++ toolchain,
> and the `muse` + `muse_deps` git submodules).

The repository includes fork-local Windows helper scripts for a one-click flow:

- `+Build (release)+.bat` — configure and build
- `+Deploy (standalone)+.bat` — make the built executable double-clickable
- `+Run (dev)+.bat` — launch the development build
- `+Update (from upstream)+.bat` — sync from upstream MuseScore

For the underlying build system and dependencies, see MuseScore's
[developer setup guide](https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment).

## Credits

- **[MuseScore Studio](https://github.com/musescore/MuseScore)** — the notation,
  engraving, and playback engine Reprise is built on.
- **[Audiveris](https://github.com/Audiveris/audiveris)** — optical music
  recognition.
- **[Tesseract](https://github.com/tesseract-ocr/tesseract)** — OCR for text and
  lyrics.

---

*Reprise is built on MuseScore Studio (GPLv3). "MuseScore" © its authors — see
[`LICENSE.txt`](LICENSE.txt).*
