#include "tui-text.h"

#include "engine.h"

QString tuiText(const char *key)
{
    const QString name = QString::fromUtf8(key);
    // Engine::translate() already answers with the key when the table has no
    // entry, so the only case left is being asked before the engine exists.
    return Sanguosha != nullptr ? Sanguosha->translate(name) : name;
}
