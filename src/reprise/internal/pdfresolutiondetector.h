/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>

#include "global/io/path.h"

namespace mu::reprise {
//! Result of sniffing a PDF's native scan resolution. matched == true ONLY when BOTH a page /MediaBox and an
//! embedded image XObject's pixel dimensions were cleanly parsed AND the derived dpi is sane; otherwise dpi falls
//! back to the Standard tier (300) and matched stays false.
struct PdfResolutionResult
{
    int32_t dpi = 300;
    bool matched = false;
};

//! Sniff the first page's native image resolution from a PDF by parsing the first /MediaBox + first /Image XObject
//! near the file head (bounded read). Deliberately shallow: no xref walking, no FlateDecode -- a PDF-1.5 compressed
//! object/xref stream, a vector/born-digital score, or any parse miss simply returns matched == false, dpi == 300.
PdfResolutionResult detectPdfResolution(const muse::io::path_t& pdfPath);

//! Snap an arbitrary dpi to the nearest recognition tier: one of 200 (Faster) / 300 (Standard) / 400 (Best).
int32_t snapToTier(int32_t dpi);
}
