#include "ai-strategy-newstar.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include "structs.h"
#include <memory>
namespace {
QJsonValue participant(Room &room, const ServerPlayer *player)
{
    if (!player) return false;
    for (const ServerPlayer *known : room.getAllPlayers(true))
        if (known == player) return player->objectName();
    return false;
}
QString disclosedName(const AIRequest &request, int id)
{
    const auto find = [id](const QList<AICardView> &cards) {
        for (const auto &card : cards)
            if (!card.virtualCard && (card.cardId == id || card.effectiveId == id)) return card.objectName;
        return QString();
    };
    QString name = find(request.worldView.handCards);
    if (name.isEmpty()) name = find(request.worldView.discardPile);
    const auto playerName = [&](const AIPlayerView &player) {
        QString result = find(player.equips);
        if (result.isEmpty()) result = find(player.judgingArea);
        if (result.isEmpty()) result = find(player.knownCards);
        for (const auto &pile : player.piles)
            if (result.isEmpty() && (pile.open || player.objectName == request.viewerObjectName)) result = find(pile.cards);
        return result;
    };
    if (name.isEmpty()) name = playerName(request.worldView.self);
    for (const auto &player : request.worldView.players)
        if (name.isEmpty()) name = playerName(player);
    return name;
}
QJsonObject failure(const char *reason) { return {{"error", QString::fromLatin1(reason)}}; }
}
void projectAINewstarContext(AIRequest &request, Room &room, const ServerPlayer *viewer)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()) return;
    QJsonObject context = request.choiceOptions.context;
    QJsonObject values = context.value("newstar").toObject();
    // extensions/newstar.lua349 sets this exact owner tag around @@LuaJuece,
    // then removes it. The AI reads only from/to; no hidden card is described.
    if (request.kind == AIRequest::UseCard && request.pattern == "@@LuaJuece") {
        const auto damage = viewer->getTag("LuaJueceDamage").value<DamageStruct>();
        values.insert("juece_damage", QJsonObject{{"from", participant(room, damage.from)}, {"to", participant(room, damage.to)}});
    }
    // Room::damage maintains CurrentDamageStruct while asking LuaQuanbian.
    // Its callback only needs the damaged participant, not the card or tags.
    if (request.kind == AIRequest::SkillInvoke && request.choiceOptions.reason == "LuaQuanbian") {
        const auto damage = room.getTag("CurrentDamageStruct").value<DamageStruct>();
        values.insert("current_damage", QJsonObject{{"to", participant(room, damage.to)}});
    }
    if (!values.isEmpty()) {
        context.insert("newstar", values);
        request.choiceOptions.context = context;
    }
}
QJsonObject evaluateAINengchenPreview(Room &room, ServerPlayer *viewer, const AIRequest &request, int materialId)
{
    if (!viewer || viewer->getRoom() != &room || request.viewerObjectName != viewer->objectName()
        || request.kind != AIRequest::Activate || request.reason != CardUseStruct::CARD_USE_REASON_PLAY
        || request.stateRevision != room.roomRuntime()->stateRevision()) return failure("Nengchen preview request mismatch");
    if (!viewer->hasSkill("LuaNengchen") || !viewer->hasFlag("jx")) return failure("Nengchen preview is not enabled");
    const bool hand = viewer->handCards().contains(materialId);
    const bool pile = viewer->getPile("wooden_ox").contains(materialId);
    if (materialId < 0 || (!hand && !pile) || room.getCardOwner(materialId) != viewer)
        return failure("Nengchen preview material is not disclosed owner material");
    const Card *material = Sanguosha->getCard(materialId);
    if (!material) return failure("Nengchen preview material is missing");
    const int rememberedId = viewer->getMark("luanengchenskill");
    QString name = disclosedName(request, rememberedId);
    const bool estimated = name.isEmpty();
    if (estimated) {
        // Writer newstar.lua73 clears its previous public mark, then publishes
        // the last physically used NDTrick name. Never inspect the remembered
        // card's current hidden wrapper, even to compare it to this old name.
        const QString prefix = "&LuaNengchen+";
        for (const QString &mark : viewer->getMarkNames()) {
            if (!mark.startsWith(prefix) || viewer->getMark(mark) <= 0) continue;
            const int suffix = mark.lastIndexOf('-');
            if (suffix <= prefix.size() || !mark.endsWith("Clear")) continue;
            const QString observed = mark.mid(prefix.size(), suffix - prefix.size());
            if (!name.isEmpty() && name != observed) return failure("Nengchen last public declaration is ambiguous");
            name = observed;
        }
        if (name.isEmpty()) return failure("Nengchen remembered face has no lawful observation");
    }
    std::unique_ptr<Card> preview(Sanguosha->cloneCard(name, material->getSuit(), material->getNumber()));
    if (!preview) return failure("Nengchen declared card cannot be cloned");
    // Fixed donor preview is deliberately NOT the paid view-as card: it has
    // no subcards and no LuaNengchen skill name. Preserve the true third arg.
    const bool limited = viewer->isCardLimited(preview.get(), Card::MethodUse, true);
    QJsonObject result{{"name", preview->objectName()}, {"class_name", preview->getClassName()},
        {"is_iron_chain", preview->isKindOf("IronChain")}, {"limited_use_hand", limited},
        {"available_checked", !limited}, {"estimated", estimated}};
    // Source short-circuits here; do not run availability hooks on a limited
    // preview or describe an unqueried value as known false.
    if (!limited) result.insert("available", preview->isAvailable(viewer));
    return result;
}
