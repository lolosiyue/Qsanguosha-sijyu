#ifndef TUI_TEXT_H
#define TUI_TEXT_H

#include <QString>

// Player-facing strings come from lang/<language>/TUICommon.lua; missing keys stay visible, while pre-engine CLI text uses tr().
QString tuiText(const char *key);

// The terminal client shows Simplified Chinese only. Converts character by
// character; text containing kana is Japanese and comes back untouched.
QString tuiToSimplified(const QString &text);
// Rewrites every Traditional entry of the loaded engine translation table in
// place. Call once, after EngineBootstrap::initialize().
void tuiSimplifyTranslations();

#endif
