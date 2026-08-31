#ifndef QSAN_TUI_CARD_TEXT_H
#define QSAN_TUI_CARD_TEXT_H

#include <QString>

// Card identity for the text client. Plain text only: no HTML, no markup, and
// no dependency on a room context, so it also works before a game is running.
QString tuiCardDisplayText(int cardId);

#endif
