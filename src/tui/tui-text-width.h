#ifndef TUI_TEXT_WIDTH_H
#define TUI_TEXT_WIDTH_H

#include <QString>

// How many terminal columns a code point occupies: 0 for combining marks, 2 for
// East Asian Wide and Fullwidth, 1 otherwise. Every grid calculation in the
// board UI goes through here -- counting QString::size() instead is what makes
// a table of Chinese names tear.
int tuiCharWidth(char32_t code);
int tuiDisplayWidth(const QString &text);
// Cuts to maxWidth columns, appending U+2026 when anything was dropped. Never
// splits a wide character: dropping a whole one is correct, half of one is a
// torn cell.
QString tuiElide(const QString &text, int maxWidth);
// Exactly width columns: elided when too long, space padded when too short.
QString tuiPadTo(const QString &text, int width);

#endif
