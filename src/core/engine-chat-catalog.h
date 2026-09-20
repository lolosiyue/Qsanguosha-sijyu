#ifndef QSAN_ENGINE_CHAT_CATALOG_H
#define QSAN_ENGINE_CHAT_CATALOG_H

#include <QStringList>
#include <QList>

class Engine;
struct EasyTextItem;

// Read-only owner for quick-chat discovery. The base list keeps the legacy
// process cache; skill/audio entries are rebuilt for each requested general.
class EngineChatCatalog final
{
public:
    static QStringList easyTexts(const Engine &engine);
    static QList<EasyTextItem> easyTextItems(const Engine &engine,
                                             const QString &generalName);
};

#endif
