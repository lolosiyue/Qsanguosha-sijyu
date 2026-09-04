#ifndef TUI_TEXT_H
#define TUI_TEXT_H

#include <QString>

// Every player-visible string the text client prints lives in
// lang/<language>/TUICommon.lua and is fetched here by key. Nothing is spelled
// out in the sources, so a translator changes one Lua table rather than
// grepping ten .cpp files, and the desktop's own translation table -- loaded
// by the same lua/sanguosha.lua pass -- is the single mechanism for both
// clients.
//
// A key with no entry comes back as the key itself. That is deliberate: a
// missing line has to be loud on screen rather than silently blank, and a
// compiled-in fallback would put the text back in the sources this exists to
// keep it out of. The engine has to be up before any of this resolves, which
// is why tui-main's own command-line strings -- printed before
// EngineBootstrap::initialize() -- are the one place still holding literals.
QString tuiText(const char *key);

#endif
