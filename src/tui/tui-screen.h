#ifndef TUI_SCREEN_H
#define TUI_SCREEN_H

#include "tui-text-width.h"

#include <QString>
#include <QVector>

struct TuiRect
{
    int row = 0;
    int col = 0;
    int rows = 0;
    int cols = 0;
};

// Colour carries meaning that the text already carries; it never carries meaning
// alone. toPlainText() drops it, and that is what golden tests compare.
enum class TuiAttr
{
    Normal,
    Dim,
    Bold,
    Kingdom,
    Current,
    Danger,
    Dead,
};

// A character grid the board draws into. Nothing reaches stdout until flush(),
// which emits only the cells that changed since the previous frame.
class TuiScreen
{
public:
    void resize(int rows, int cols);
    void clear();
    void putText(int row, int col, const QString &text,
                 TuiAttr attr = TuiAttr::Normal, int maxWidth = -1);
    void drawBox(const TuiRect &rect, const QString &title = {});
    QString flush();
    QString toPlainText() const;
    int rows() const { return m_rows; }
    int cols() const { return m_cols; }

private:
    struct Cell
    {
        QChar glyph = QLatin1Char(' ');
        TuiAttr attr = TuiAttr::Normal;
        // The trailing half of a wide glyph. Nothing may be written here on its
        // own; overwriting it blanks the head cell too.
        bool continuation = false;
        bool operator==(const Cell &other) const;
    };

    int index(int row, int col) const { return row * m_cols + col; }
    void writeCell(int row, int col, QChar glyph, TuiAttr attr, bool continuation);

    int m_rows = 0;
    int m_cols = 0;
    QVector<Cell> m_current;
    QVector<Cell> m_previous;
    bool m_fullRepaint = true;
};

#endif
