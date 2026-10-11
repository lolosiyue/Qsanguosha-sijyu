#include "ai-strategy-round23-qiuxin.h"
#include "ai-strategy-round22-olclan-plan.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QSet>

bool aiRound23QiuxinSource(const AIRequest &request, const ServerPlayer *viewer,
                          const Card *card, const QString &sourceKey, QString *error)
{
    const auto reject = [&](const char *why) {
        if (error) *error = QString::fromLatin1(why);
        return false;
    };
    if (sourceKey != "kezuqiuxin" || !card || card->getId() >= 0
        || card->getSkillName(false) != "_kezuqiuxin" || !card->getSubcards().isEmpty()
        || card->getSuit() != Card::NoSuit || card->getNumber() != 0)
        return reject("Qiuxin profile requires its original issued source clone");
    // Reuse the exact finite producer admission, including the current AG
    // question, current owner skill, offered public name and request revision.
    const QJsonObject spec{{"source_key", sourceKey}, {"name", card->objectName()},
        {"skill", card->getSkillName(false)}, {"suit", int(Card::NoSuit)},
        {"number", 0}, {"subcards", QJsonArray{}}};
    return aiRound22OlClanConstruction(request, viewer, spec, error);
}

QJsonObject aiRound23QiuxinTargetFilter(Room &room, ServerPlayer *viewer,
                                      const AIRequest &request, const Card *card,
                                      const QString &sourceKey, const QJsonObject &query)
{
    const auto reject = [](const QString &why) {
        return QJsonObject{{"error", why}, {"key", "qiuxin_target_query"}};
    };
    QString error;
    if (!viewer || viewer->getRoom() != &room
        || !aiRound23QiuxinSource(request, viewer, card, sourceKey, &error))
        return reject(error.isEmpty() ? QStringLiteral("Qiuxin owner is absent") : error);
    if (query.size() != 2 || !query.value("prefix").isArray() || !query.value("target").isString())
        return reject("Qiuxin target query fields are malformed");
    const auto names = query.value("prefix").toArray();
    if (names.size() > room.getAlivePlayers().size()) return reject("Qiuxin prefix exceeds current roster");
    QList<const Player *> prefix;
    QSet<QString> seen;
    for (const auto &name : names) {
        if (!name.isString() || seen.contains(name.toString())) return reject("Qiuxin prefix is not a distinct roster list");
        const auto *target = room.findPlayerByObjectName(name.toString());
        if (!target || !target->isAlive()) return reject("Qiuxin prefix contains an absent player");
        seen.insert(name.toString()); prefix << target;
    }
    const auto *target = room.findPlayerByObjectName(query.value("target").toString());
    if (!target || !target->isAlive()) return reject("Qiuxin next target is absent");
    const auto revision = room.roomRuntime()->stateRevision();
    // Native targetFilter sees precisely the donor dummy(...,99) overlay;
    // this does not change the card, the live player, or an outer range flag.
    AITianyiQueryScope extra(viewer, card, 99, false);
    const bool accepted = card->targetFilter(prefix, target, viewer);
    if (room.roomRuntime()->stateRevision() != revision) return reject("Qiuxin target query changed state");
    return {{"accepted", accepted}};
}
