#include "tui-text-width.h"

#include <iterator>

namespace {

struct Range
{
    char32_t first;
    char32_t last;
};

// Unicode 15 East Asian Wide and Fullwidth, trimmed to the blocks this client
// can actually print: Han, kana, Hangul, CJK punctuation and the fullwidth
// forms. Widening more than this would be wrong for the box drawing and card
// suit glyphs the board draws with.
constexpr Range wideRanges[] = {
    {0x1100, 0x115F}, {0x2E80, 0x303E}, {0x3041, 0x33FF},
    {0x3400, 0x4DBF}, {0x4E00, 0x9FFF}, {0xA000, 0xA4CF},
    {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F}, {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6},
    {0x1F000, 0x1F0FF}, {0x1F300, 0x1F64F}, {0x1F900, 0x1F9FF},
    {0x20000, 0x3FFFD},
};

// Combining marks and other zero-width code points.
constexpr Range zeroRanges[] = {
    {0x0300, 0x036F}, {0x200B, 0x200F}, {0xFE00, 0xFE0F},
    {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF},
};

bool inRanges(char32_t code, const Range *ranges, int count)
{
    for (int i = 0; i < count; ++i) {
        if (code >= ranges[i].first && code <= ranges[i].last)
            return true;
    }
    return false;
}

} // namespace

int tuiCharWidth(char32_t code)
{
    if (code == 0)
        return 0;
    if (inRanges(code, zeroRanges, int(std::size(zeroRanges))))
        return 0;
    if (inRanges(code, wideRanges, int(std::size(wideRanges))))
        return 2;
    return 1;
}

int tuiDisplayWidth(const QString &text)
{
    int width = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        char32_t code = it->unicode();
        if (it->isHighSurrogate() && (it + 1) != text.end() && (it + 1)->isLowSurrogate()) {
            code = QChar::surrogateToUcs4(*it, *(it + 1));
            ++it;
        }
        width += tuiCharWidth(code);
    }
    return width;
}

QString tuiElide(const QString &text, int maxWidth)
{
    if (maxWidth <= 0)
        return QString();
    if (tuiDisplayWidth(text) <= maxWidth)
        return text;
    if (maxWidth == 1)
        return QString(QChar(0x2026));

    // One column is reserved for the ellipsis. Stopping before a wide character
    // that would not fit whole is the point: half a glyph tears the cell.
    const int budget = maxWidth - 1;
    QString result;
    int width = 0;
    for (auto it = text.begin(); it != text.end(); ++it) {
        QString unit(*it);
        char32_t code = it->unicode();
        if (it->isHighSurrogate() && (it + 1) != text.end() && (it + 1)->isLowSurrogate()) {
            code = QChar::surrogateToUcs4(*it, *(it + 1));
            ++it;
            unit.append(*it);
        }
        const int next = tuiCharWidth(code);
        if (width + next > budget)
            break;
        result.append(unit);
        width += next;
    }
    return result + QChar(0x2026);
}

QString tuiPadTo(const QString &text, int width)
{
    const QString fitted = tuiElide(text, width);
    const int pad = width - tuiDisplayWidth(fitted);
    return pad > 0 ? fitted + QString(pad, QLatin1Char(' ')) : fitted;
}
