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

#include "pdfresolutiondetector.h"

#include <cstdlib>

#include <QByteArray>
#include <QFile>

using namespace mu::reprise;

namespace {
//! Read cap: enough to cover the first page's objects on a typical scanned PDF without slurping a huge file.
static constexpr int64_t MAX_READ_BYTES = 4 * 1024 * 1024;

//! Sane embedded-resolution bounds. Anything outside this is treated as a parse artefact, not a real scan dpi.
static constexpr int32_t MIN_SANE_DPI = 50;
static constexpr int32_t MAX_SANE_DPI = 1200;

//! Window (bytes) searched around a "/Subtype /Image" marker for that image dict's /Width and /Height.
static constexpr int64_t IMAGE_DICT_WINDOW = 4096;

//! Skip PDF whitespace (incl. NUL and form-feed) forward from pos.
static int64_t skipWs(const QByteArray& data, int64_t pos)
{
    while (pos < data.size())
    {
        const char c = data.at(static_cast<int>(pos));
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0')
        {
            ++pos;
            continue;
        }

        break;
    }

    return pos;
}

//! Parse a PDF number (optionally signed / fractional) starting at pos. Returns false if no digits are present.
static bool parseNumber(const QByteArray& data, int64_t pos, double& outValue, int64_t& outEnd)
{
    const int64_t start = pos;
    if (pos < data.size() && (data.at(static_cast<int>(pos)) == '+' || data.at(static_cast<int>(pos)) == '-'))
    {
        ++pos;
    }

    bool anyDigit = false;
    while (pos < data.size())
    {
        const char c = data.at(static_cast<int>(pos));
        if (c >= '0' && c <= '9')
        {
            anyDigit = true;
            ++pos;
            continue;
        }

        if (c == '.')
        {
            ++pos;
            continue;
        }

        break;
    }

    if (!anyDigit)
    {
        return false;
    }

    outValue = QByteArray(data.constData() + start, static_cast<int>(pos - start)).toDouble();
    outEnd = pos;

    return true;
}

//! Read the integer immediately following a key (after whitespace), where the key occurs within [searchFrom, searchTo).
static bool intAfterKey(const QByteArray& data, const QByteArray& key, int64_t searchFrom, int64_t searchTo, int32_t& outValue)
{
    const int from = static_cast<int>(searchFrom < 0 ? 0 : searchFrom);
    const int to = static_cast<int>(searchTo > data.size() ? data.size() : searchTo);
    const int idx = data.indexOf(key, from);
    if (idx < 0 || idx >= to)
    {
        return false;
    }

    const int64_t pos = skipWs(data, idx + key.size());
    double value = 0.0;
    int64_t end = 0;
    if (!parseNumber(data, pos, value, end))
    {
        return false;
    }

    outValue = static_cast<int32_t>(value + 0.5);

    return true;
}

//! First /MediaBox [ llx lly urx ury ] -> page width in points (urx - llx). False if not found / malformed.
static bool parseFirstMediaBoxWidthPts(const QByteArray& data, double& outWidthPts)
{
    const int idx = data.indexOf("/MediaBox");
    if (idx < 0)
    {
        return false;
    }

    int64_t pos = skipWs(data, idx + 9);   // 9 == len("/MediaBox")
    if (pos >= data.size() || data.at(static_cast<int>(pos)) != '[')
    {
        return false;
    }

    ++pos;   // past '['

    double nums[4] = { 0.0, 0.0, 0.0, 0.0 };
    for (int i = 0; i < 4; ++i)
    {
        pos = skipWs(data, pos);
        double v = 0.0;
        int64_t end = 0;
        if (!parseNumber(data, pos, v, end))
        {
            return false;
        }

        nums[i] = v;
        pos = end;
    }

    const double width = nums[2] - nums[0];
    if (width <= 0.0)
    {
        return false;
    }

    outWidthPts = width;

    return true;
}

//! First image XObject (a /Subtype whose next token is /Image) -> its /Width in pixels. False if none parses cleanly.
static bool parseFirstImagePixelWidth(const QByteArray& data, int32_t& outPixelWidth)
{
    int from = 0;
    while (true)
    {
        const int sub = data.indexOf("/Subtype", from);
        if (sub < 0)
        {
            return false;
        }

        const int64_t tok = skipWs(data, sub + 8);   // 8 == len("/Subtype")
        if (tok + 6 <= data.size() && QByteArray(data.constData() + tok, 6) == "/Image")
        {
            const int64_t winStart = sub > IMAGE_DICT_WINDOW ? sub - IMAGE_DICT_WINDOW : 0;
            const int64_t winEnd = sub + IMAGE_DICT_WINDOW;
            int32_t w = 0;
            int32_t h = 0;
            if (intAfterKey(data, "/Width", winStart, winEnd, w)
                && intAfterKey(data, "/Height", winStart, winEnd, h)
                && w > 0 && h > 0)
            {
                outPixelWidth = w;

                return true;
            }
        }

        from = sub + 8;
    }
}
}

PdfResolutionResult mu::reprise::detectPdfResolution(const muse::io::path_t& pdfPath)
{
    PdfResolutionResult result;   // dpi = 300, matched = false

    QFile file(pdfPath.toQString());
    if (!file.open(QIODevice::ReadOnly))
    {
        return result;
    }

    const QByteArray data = file.read(MAX_READ_BYTES);
    file.close();
    if (data.isEmpty())
    {
        return result;
    }

    double widthPts = 0.0;
    int32_t pixelWidth = 0;
    if (!parseFirstMediaBoxWidthPts(data, widthPts) || !parseFirstImagePixelWidth(data, pixelWidth))
    {
        return result;
    }

    const double widthInches = widthPts / 72.0;
    if (widthInches <= 0.0)
    {
        return result;
    }

    const int32_t dpi = static_cast<int32_t>((static_cast<double>(pixelWidth) / widthInches) + 0.5);
    if (dpi < MIN_SANE_DPI || dpi > MAX_SANE_DPI)
    {
        return result;
    }

    result.dpi = dpi;
    result.matched = true;

    return result;
}

int32_t mu::reprise::snapToTier(int32_t dpi)
{
    static const int32_t TIERS[3] = { 200, 300, 400 };

    int32_t best = TIERS[0];
    int32_t bestDist = std::abs(dpi - TIERS[0]);
    for (int i = 1; i < 3; ++i)
    {
        const int32_t dist = std::abs(dpi - TIERS[i]);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = TIERS[i];
        }
    }

    return best;
}
