#include "ai-strategy-nested-cardask.h"
#include "ai.h"
#include "card.h"
#include "engine.h"
#include "lua-runtime.h"
#include "room.h"
#include "room-runtime.h"
#include "serverplayer.h"
#include <QJsonArray>
#include <QDebug>
#include <QSet>
#include <memory>

namespace {
QJsonObject failure(const QString &reason)
{
    return {{"error", "AI_UNSUPPORTED"}, {"reason", reason}, {"key", "nested_askForCard.parse"}};
}
bool inquiry(const AIRequest &request, Room &room, ServerPlayer *viewer, QJsonObject *data = nullptr, QString *contextKey = nullptr)
{
    if (!viewer || viewer->getRoom() != &room || request.kind != AIRequest::SkillInvoke
        || request.viewerObjectName != viewer->objectName()
        || request.worldView.self.objectName != viewer->objectName()
        || request.stateRevision != room.roomRuntime()->stateRevision()) return false;
    const bool lianli = request.choiceOptions.reason == "lianli-slash";
    if (!lianli && request.choiceOptions.reason != "aocai") return false;
    const QString key = lianli ? QStringLiteral("lianli_slash_invoke") : QStringLiteral("aocai_invoke");
    const auto invocation = request.choiceOptions.context.value(key).toObject();
    const auto asked = invocation.value("asked").toArray();
    if (!invocation.value("known").toBool() || asked.size() < 2
        || !asked[0].isString() || !asked[1].isString()) return false;
    if (lianli && (asked.size() < 3 || asked[0] != "slash" || asked.last() != "response"
        || asked[1].toString().contains("lianli-slash"))) return false;
    if (contextKey) *contextKey = key;
    if (data) *data = invocation;
    return true;
}
QSet<int> disclosedOwnerCards(const AIRequest &request)
{
    QSet<int> result;
    const auto append = [&](const QList<AICardView> &cards) {
        for (const auto &card : cards) if (card.effectiveId >= 0) result.insert(card.effectiveId);
    };
    append(request.worldView.handCards);
    append(request.worldView.self.equips);
    append(request.worldView.self.judgingArea);
    for (const auto &pile : request.worldView.self.piles) append(pile.cards);
    return result;
}
QJsonObject classified(const char *outcome)
{
    return {{"kind", "nested_card_parse"}, {"outcome", QString::fromLatin1(outcome)}};
}
}

QJsonObject inspectAINestedCardaskString(Room &room, ServerPlayer *viewer,
                                       const AIRequest &request, const QString &value)
{
    if (!inquiry(request, room, viewer)) return failure("nested parser is outside the current finite nil-method inquiry");
    // Bound native regex parsing and material-list allocation. Rejected inputs
    // stay unknown; the bound is not reported as a donor Card::Parse failure.
    if (value.size() > 4096 || value.contains(QChar::Null)) return failure("nested parser input is outside its bounded text domain");
    const QString copy = value.section(QStringLiteral("->"), 0, 0);
    const bool virtualSyntax = value.startsWith('@') || value.startsWith('$')
        || value.startsWith('#') || value.contains('=');
    if (!virtualSyntax && copy != ".") {
        bool numeric = false;
        const int id = copy.toInt(&numeric);
        if (numeric && (!disclosedOwnerCards(request).contains(id) || room.getCardOwner(id) != viewer))
            return failure("nested physical parser identity is not disclosed and owned by this viewer");
    }
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaScope(room.roomRuntime()->lua(), false);
    // Fixed Card::Parse and LuaSkillCard::Parse only clone native/public
    // prototypes and copy callback references here; no strategy/filter/use
    // callback is invoked. Never inspect a virtual material's live card face.
    const Card *parsed = Card::Parse(value);
    if (!parsed) return classified("invalid");
    if (!parsed->isVirtualCard()) return classified("requires_nil_method");
    std::unique_ptr<const Card> temporary(parsed);
    return classified(parsed->subcardsLength() == 0 ? "empty_virtual" : "requires_nil_method");
}

void projectAINestedCardaskCompulsory(AIRequest &request, Room &room, ServerPlayer *viewer)
{
    QJsonObject invocation;
    QString contextKey;
    if (!inquiry(request, room, viewer, &invocation, &contextKey)) return;
    QString pattern = invocation.value("asked").toArray().at(0).toString();
    if (pattern.endsWith('!')) pattern.chop(1);
    QJsonObject result{{"known", false}, {"pattern", pattern}};
    const QSet<int> disclosed = disclosedOwnerCards(request);
    QList<int> ids;
    for (const Card *card : viewer->getCards("he")) {
        if (!card || card->getEffectiveId() < 0) {
            invocation.insert("compulsory", result);
            request.choiceOptions.context.insert(contextKey, invocation);
            return;
        }
        ids << card->getEffectiveId();
    }
    for (int id : viewer->getHandPile()) if (!ids.contains(id)) ids << id;
    // The lookup map is deliberately independent of candidate filtering and
    // keep order. Lua retains the exact source zone and sorting traversal.
    QJsonObject matches;
    bool complete = true;
    EngineRuntimeContextScope engineScope(*Sanguosha, &room);
    LuaRuntime::Binding luaScope(room.roomRuntime()->lua(), false);
    for (int id : ids) {
        if (!disclosed.contains(id) || room.getCardOwner(id) != viewer) { complete = false; break; }
        const Card *card = Sanguosha->getCard(id);
        if (!card) { complete = false; break; }
        matches.insert(QString::number(id), Sanguosha->matchPattern(pattern, viewer, card));
    }
    if (complete) { result.insert("known", true); result.insert("by_id", matches); }
    invocation.insert("compulsory", result);
    request.choiceOptions.context.insert(contextKey, invocation);
}

QJsonObject reportAILianliCardaskError(Room &room, ServerPlayer *viewer,
                                      const AIRequest &request, const QString &message)
{
    if (request.choiceOptions.reason != "lianli-slash" || !inquiry(request, room, viewer)
        || message.isEmpty() || message.size() > 16384 || message.contains(QChar::Null))
        return failure("Lianli source error diagnostic is outside the current bounded inquiry");
    // Fixed SmartAI.initialize pcall catches an ordinary askForSkillInvoke
    // error, emits these diagnostics, and returns nil. LuaAI converts that
    // successful nil result to false without TrustAI or personality RNG.
    const auto console = [&](const QString &line) {
        room.output(line);
        qWarning("%s", qPrintable(line));
    };
    console(QStringLiteral("[AI_CALLBACK_ERROR] askForSkillInvoke: ") + message);
    console(QStringLiteral("askForSkillInvoke"));
    console(message);
    room.outputEventStack();
    console(QStringLiteral("lianli-slash"));
    return {{"kind", "lianli_source_error"}, {"error_class", "ordinary_lua_error"},
            {"message", message}, {"invoke", false}};
}
