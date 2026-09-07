#include "tui-screen.h"

#include <QStringList>

namespace {

// The SGR code for each attribute, applied on its own (never stacked -- see
// the reset-before-apply comment in TuiScreen::flush()). Normal needs none:
// a run that never leaves Normal never emits an SGR at all, which is most of
// the board's own text. The actual colours are a placeholder palette, but
// TuiAttr is no longer just a "does it round-trip" placeholder itself --
// tui-board-view.cpp now applies Bold/Danger/Current/Dead for real (the
// self cell, notices, the input cursor, dying/dead players), so the
// distinctions below are load-bearing: kingdom banners in gold, the current
// player's turn in reverse video, lethal HP and dead generals both reading
// as "faded" in different ways.
QString attrToSgr(TuiAttr attr)
{
    switch (attr) {
    case TuiAttr::Normal:
        return QString();
    case TuiAttr::Dim:
        return QStringLiteral("\x1b[2m");
    case TuiAttr::Bold:
        return QStringLiteral("\x1b[1m");
    case TuiAttr::Kingdom:
        return QStringLiteral("\x1b[33m");
    case TuiAttr::Current:
        return QStringLiteral("\x1b[7m");
    case TuiAttr::Danger:
        return QStringLiteral("\x1b[31m");
    case TuiAttr::Dead:
        return QStringLiteral("\x1b[90m");
    }
    return QString();
}

} // namespace

bool TuiScreen::Cell::operator==(const Cell &other) const
{
    return glyph == other.glyph && attr == other.attr && continuation == other.continuation;
}

void TuiScreen::resize(int rows, int cols)
{
    m_rows = rows;
    m_cols = cols;
    m_current = QVector<Cell>(rows * cols);
    m_previous = QVector<Cell>(rows * cols);
    // The new previous frame is empty, but there is nothing meaningful to diff
    // it against yet -- force flush() to paint the whole thing once instead of
    // trusting a comparison against a grid that describes a screen this size
    // has never actually shown.
    m_fullRepaint = true;
}

void TuiScreen::clear()
{
    m_current.fill(Cell());
}

void TuiScreen::writeCell(int row, int col, QChar glyph, TuiAttr attr, bool continuation)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols)
        return;

    const int idx = index(row, col);
    const Cell old = m_current[idx];

    // Continuation-cell rule, direction one: a continuation cell carries no
    // glyph of its own -- the whole character lives in the head cell one
    // column to its left. Overwriting the continuation half without touching
    // the head would leave that head still claiming a partner that no longer
    // agrees with it, i.e. half a wide character stuck on screen.
    if (old.continuation && col > 0)
        m_current[index(row, col - 1)] = Cell();

    // Direction two, the mirror case: if the cell being overwritten used to be
    // the *head* of a wide glyph, its old continuation cell is now an orphan.
    // Left marked as a continuation, toPlainText() would keep silently
    // swallowing that column even though it no longer continues anything,
    // which shifts every character after it one column left in the text.
    if (!old.continuation && col + 1 < m_cols) {
        const int nextIdx = index(row, col + 1);
        if (m_current[nextIdx].continuation)
            m_current[nextIdx] = Cell();
    }

    Cell &cell = m_current[idx];
    cell.glyph = glyph;
    cell.attr = attr;
    cell.continuation = continuation;
}

void TuiScreen::putText(int row, int col, const QString &text, TuiAttr attr, int maxWidth)
{
    if (row < 0 || row >= m_rows || col < 0 || col >= m_cols)
        return;

    // A negative maxWidth means "whatever is left on this row": the caller is
    // not tracking the right edge itself, so elide against it here instead of
    // wrapping, which would tear the board's fixed-width layout.
    const int width = maxWidth >= 0 ? maxWidth : m_cols - col;
    const QString elided = tuiElide(text, width);

    int cursor = col;
    for (auto it = elided.begin(); it != elided.end(); ++it) {
        if (cursor >= m_cols)
            break; // Clipping, not wrapping: a torn row is worse than a cut word.

        QChar glyph = *it;
        char32_t code = glyph.unicode();
        if (glyph.isHighSurrogate() && (it + 1) != elided.end() && (it + 1)->isLowSurrogate()) {
            // A non-BMP code point needs both surrogate units to identify, but
            // Cell::glyph is a single QChar and cannot hold the pair. The
            // board's own alphabet -- Han, kana, Hangul, box drawing, the suit
            // glyphs -- is entirely inside the BMP, so this lossy fallback
            // (keep the lead surrogate as the visible glyph) never actually
            // fires for anything this client draws; it only keeps a stray
            // emoji from corrupting iteration instead of rendering correctly.
            code = QChar::surrogateToUcs4(glyph, *(it + 1));
            ++it;
        }

        const int glyphWidth = tuiCharWidth(code);
        if (glyphWidth == 0)
            continue; // A combining mark with no base glyph in this grid to attach to.

        if (glyphWidth == 2 && cursor + 1 >= m_cols) {
            // Exactly one column left and this glyph needs two. Writing just
            // the head would draw half a character; stopping here clips a
            // whole glyph instead, which is the same rule tuiElide already
            // uses to avoid tearing.
            break;
        }

        writeCell(row, cursor, glyph, attr, false);
        ++cursor;
        if (glyphWidth == 2) {
            writeCell(row, cursor, QLatin1Char(' '), attr, true);
            ++cursor;
        }
    }
}

void TuiScreen::drawBox(const TuiRect &rect, const QString &title)
{
    if (rect.rows < 2 || rect.cols < 2)
        return; // Nothing sane to draw for a box with no interior.

    const int top = rect.row;
    const int bottom = rect.row + rect.rows - 1;
    const int left = rect.col;
    const int right = rect.col + rect.cols - 1;

    const QChar topLeft(0x250C);     // ┌
    const QChar topRight(0x2510);    // ┐
    const QChar bottomLeft(0x2514);  // └
    const QChar bottomRight(0x2518); // ┘
    const QChar horizontal(0x2500);  // ─
    const QChar vertical(0x2502);    // │

    writeCell(top, left, topLeft, TuiAttr::Normal, false);
    writeCell(top, right, topRight, TuiAttr::Normal, false);
    writeCell(bottom, left, bottomLeft, TuiAttr::Normal, false);
    writeCell(bottom, right, bottomRight, TuiAttr::Normal, false);

    for (int col = left + 1; col < right; ++col) {
        writeCell(top, col, horizontal, TuiAttr::Normal, false);
        writeCell(bottom, col, horizontal, TuiAttr::Normal, false);
    }
    for (int row = top + 1; row < bottom; ++row) {
        writeCell(row, left, vertical, TuiAttr::Normal, false);
        writeCell(row, right, vertical, TuiAttr::Normal, false);
    }

    if (!title.isEmpty()) {
        // rect.cols - 4 is the two border columns plus the one space of
        // padding on each side of the title; the title text is what tuiElide
        // is allowed to cut, drawBox() draws over the top edge afterwards so
        // the framed title always wins over the dashes underneath it.
        const QString elidedTitle = tuiElide(title, rect.cols - 4);
        const QString framed = QLatin1Char(' ') + elidedTitle + QLatin1Char(' ');
        putText(top, left + 2, framed, TuiAttr::Normal);
    }
}

QString TuiScreen::flush()
{
    // Turns one row segment into the escapes and text needed to paint it,
    // sharing the attribute-run logic between the full-repaint path and the
    // diff path below. A member lambda rather than a private method: this is
    // pure rendering detail with no reason to appear in the header.
    auto renderRun = [this](int row, int startCol, int endCol) -> QString {
        QString out;
        TuiAttr current = TuiAttr::Normal;
        for (int col = startCol; col < endCol; ++col) {
            const Cell &cell = m_current[index(row, col)];
            if (cell.continuation)
                continue; // No character to send: the head's wide glyph already advanced the terminal cursor two cells.
            if (cell.attr != current) {
                // Reset before applying the next attribute rather than layering
                // SGR codes on top of each other -- otherwise a run that walks
                // Bold into Danger would come out bold *and* red instead of
                // just red.
                out += QStringLiteral("\x1b[0m");
                out += attrToSgr(cell.attr);
                current = cell.attr;
            }
            out += cell.glyph;
        }
        out += QStringLiteral("\x1b[0m"); // Every emitted segment ends clean, whatever attribute it started other text with.
        return out;
    };

    QString output;

    if (m_fullRepaint) {
        // Nothing to diff the first frame against: paint everything. One
        // "go home" escape plus a plain newline BETWEEN rows is enough,
        // because every row is being repainted in full anyway. The
        // separator goes between rows only, never after the last one: a
        // terminal whose height exactly equals the screen's own row count
        // has no scrollback room to absorb a trailing "\r\n" after the
        // bottom row, so emitting one there scrolls the alternate screen up
        // by a line, carrying the top border off screen and leaving every
        // absolute-position escape the diff path emits afterwards addressed
        // against a screen that has silently shifted underneath it.
        output += QStringLiteral("\x1b[H");
        for (int row = 0; row < m_rows; ++row) {
            if (row > 0)
                output += QStringLiteral("\r\n");
            output += renderRun(row, 0, m_cols);
        }
    } else {
        // The property that makes the board usable over a slow link: only the
        // cells that actually changed get sent, as one cursor move plus
        // content per contiguous run -- not one escape per cell, and not
        // whole rows just because one cell in them moved.
        for (int row = 0; row < m_rows; ++row) {
            int col = 0;
            while (col < m_cols) {
                const int idx = index(row, col);
                if (m_current[idx] == m_previous[idx]) {
                    ++col;
                    continue;
                }
                const int start = col;
                while (col < m_cols && !(m_current[index(row, col)] == m_previous[index(row, col)]))
                    ++col;
                output += QStringLiteral("\x1b[%1;%2H").arg(row + 1).arg(start + 1);
                output += renderRun(row, start, col);
            }
        }
    }

    m_previous = m_current;
    m_fullRepaint = false;
    return output;
}

QString TuiScreen::toPlainText() const
{
    QStringList lines;
    lines.reserve(m_rows);
    for (int row = 0; row < m_rows; ++row) {
        QString line;
        line.reserve(m_cols);
        for (int col = 0; col < m_cols; ++col) {
            const Cell &cell = m_current[index(row, col)];
            // The continuation cell holds no glyph of its own; the head cell
            // already produced the whole (possibly wide) character.
            if (cell.continuation)
                continue;
            line += cell.glyph;
        }
        // Only the right end is trimmed: leading columns are meaningful
        // (indentation before a name, e.g. "  曹操"), but trailing padding out
        // to the fixed grid width is a rendering detail a golden test should
        // not have to spell out.
        int end = line.size();
        while (end > 0 && line.at(end - 1).isSpace())
            --end;
        line.truncate(end);
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}
