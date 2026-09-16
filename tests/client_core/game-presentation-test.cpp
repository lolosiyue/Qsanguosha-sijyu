// QtCore-only contract checks for the shared game presentation projections.
#include "../../src/client/core/client-game-state.h"
#include "../../src/client/core/game-action-model.h"
#include "../../src/client/core/game-event-stream.h"
#include "../../src/client/core/game-view-state.h"
#include "../../src/core/protocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

namespace {
int failures = 0;

void check(bool condition, const char *message)
{
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

void viewStateContracts()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("self"));
    state.setPlayerNames({QStringLiteral("self"), QStringLiteral("other")});
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("self"));
    state.setGameValue(QStringLiteral("current_phase"), QStringLiteral("play"));
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("seat"), 1);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("hp"), 3);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("max_hp"), 4);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("hand_count"), 1);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("hand_max"), 4);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("offensive_distance"), 1);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("defensive_distance"), 2);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("faceup"), false);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("chained"), true);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("role"), QStringLiteral("lord"));
    state.setPlayerMark(QStringLiteral("self"), QStringLiteral("@round"), 3);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("skills"), QStringList{QStringLiteral("visible_skill")});
    state.setPlayerValue(QStringLiteral("other"), QStringLiteral("seat"), 2);
    state.setPlayerValue(QStringLiteral("other"), QStringLiteral("hp"), 2);
    state.setPlayerValue(QStringLiteral("other"), QStringLiteral("max_hp"), 3);
    state.setPlayerValue(QStringLiteral("other"), QStringLiteral("hand_count"), 5);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("piles"), QVariantMap{
        {QStringLiteral("hidden"), QVariantList{-1, 12, 13}}});
    state.setCardValue(11, QStringLiteral("owner"), QStringLiteral("self"));
    state.setCardValue(11, QStringLiteral("place"), 0);
    state.setCardValue(11, QStringLiteral("object_name"), QStringLiteral("slash"));
    state.setCardValue(21, QStringLiteral("owner"), QStringLiteral("other"));
    state.setCardValue(21, QStringLiteral("place"), 0);
    state.setCardValue(21, QStringLiteral("object_name"), QStringLiteral("private-secret"));
    state.setCardValue(31, QStringLiteral("owner"), QStringLiteral("other"));
    state.setCardValue(31, QStringLiteral("place"), 1);
    state.setCardValue(31, QStringLiteral("object_name"), QStringLiteral("armor"));
    state.setCardValue(12, QStringLiteral("owner"), QStringLiteral("self"));
    state.setCardValue(12, QStringLiteral("place"), 4);
    state.setCardValue(12, QStringLiteral("pile"), QStringLiteral("hidden"));
    state.setCardValue(12, QStringLiteral("object_name"), QStringLiteral("current-pile-card"));
    state.setCardValue(13, QStringLiteral("owner"), QStringLiteral("self"));
    state.setCardValue(13, QStringLiteral("place"), 4);
    state.setCardValue(13, QStringLiteral("pile"), QStringLiteral("old-pile")); // stale pile identity
    state.setCardValue(13, QStringLiteral("object_name"), QStringLiteral("stale-pile-card"));
    state.setCardIdSpace(100);

    GameViewFormatOptions options;
    options.phaseLabel = [](const QString &phase) { return QStringLiteral("Play translated"); };
    options.cardLabel = [](int id) { return QStringLiteral("Card-%1").arg(id); };
    options.translate = [](const QString &text) {
        if (text == QStringLiteral("ask")) return QStringLiteral("請求 %src");
        if (text == QStringLiteral("visible_skill")) return QStringLiteral("Visible Skill");
        if (text == QStringLiteral("@round")) return QStringLiteral("Round");
        if (text == QStringLiteral("lord")) return QStringLiteral("Lord");
        return text;
    };
    options.distanceLabel = [](const QString &, const QString &) { return QString(); };
    InteractionRequest request;
    request.requestId = Q_UINT64_C(18446744073709551610);
    request.prompt = QStringLiteral("ask:other");
    const GameViewState view = GameViewState::fromState(state, &request,
        Q_UINT64_C(18446744073709551609), 12, options);
    const QByteArray json = QJsonDocument(view.toJson()).toJson(QJsonDocument::Compact);
    check(view.sessionGeneration == Q_UINT64_C(18446744073709551609)
          && view.presentationRevision == 12,
          "view projection carries session and revision");
    check(view.players.size() == 2 && view.players.first().seat == 1
          && view.players.first().hp == 3 && view.players.first().hand.size() == 1,
          "view includes public player fields and own visible hand");
    check(view.players.first().handMax == 4 && view.players.first().offensiveDistance == 1
          && view.players.first().defensiveDistance == 2
          && view.players.first().faceUp.toBool() == false
          && view.players.first().chained.toBool()
          && view.players.first().role == QStringLiteral("Lord")
          && view.players.first().marks.value(QStringLiteral("Round")).toInt() == 3
          && view.players.first().skills.contains(QStringLiteral("Visible Skill")),
          "inspector projection includes recipient-visible dashboard details");
    check(view.players.last().hand.isEmpty() && view.players.last().handCount == 5,
          "opponent hand exposes count without card identities");
    check(!json.contains("private-secret") && !json.contains("Card-21"),
          "serialized view never leaks hidden opponent hand identity");
    check(view.phaseLabel == QStringLiteral("Play translated")
          && view.toPlainText().contains(QStringLiteral("self 手牌：Card-11")),
          "translation callback reaches readable text snapshot");
    const QVariantMap pile = view.players.first().piles.value(QStringLiteral("hidden")).toMap();
    const QVariantList pileCards = pile.value(QStringLiteral("cards")).toList();
    check(view.prompt == QStringLiteral("請求 other")
          && pile.value(QStringLiteral("count")).toInt() == 2
          && pileCards.size() == 1
          && pileCards.first().toMap().value(QStringLiteral("id")).toInt() == 12
          && !json.contains("stale-pile-card"),
          "prompt translation and sanitization preserve unknown pile count but omit stale identities");
    check(view.toJson().value(QStringLiteral("session_generation")).toString()
              == QStringLiteral("18446744073709551609")
          && view.toJson().value(QStringLiteral("request_id")).toString()
              == QStringLiteral("18446744073709551610"),
          "serialized correlation values preserve full quint64 precision");
    const auto actionText = view.toPlainText();
    check(actionText.contains(QStringLiteral("本人：")) && actionText.contains(QStringLiteral("階段：")),
          "plain-text snapshot uses translatable Chinese labels");
    check(actionText.contains(QStringLiteral("Card-12"))
          && actionText.contains(QStringLiteral("未知 1 張")),
          "plain-text snapshot names authorized pile cards and counts unknown cards");

    options.operatingPlayer = QStringLiteral("other");
    options.authorizedHandPlayers = {QStringLiteral("other")};
    const GameViewState controlledView = GameViewState::fromState(state, &request, 2, 3, options);
    check(controlledView.players.last().handVisible
          && controlledView.toPlainText().contains(QStringLiteral("目前玩家：self"))
          && controlledView.toPlainText().contains(QStringLiteral("操作角色：other"))
          && controlledView.toPlainText().contains(QStringLiteral("other 手牌：Card-21")),
          "text snapshot includes current player, operating player and explicitly authorized hand");

    request.prompt = QStringLiteral("markup");
    options.translate = [](const QString &) { return QStringLiteral("<b>bad</b>\nInjected"); };
    const QString sanitizedPrompt = GameViewState::fromState(state, &request, 1, 1, options).prompt;
    check(!sanitizedPrompt.contains(QLatin1Char('<')) && !sanitizedPrompt.contains(QLatin1Char('\n')),
          "translated prompt strips markup and control-line injection");
}

// docs/ui-roadmap.md 2.3: the L0 tier must be permanently readable without any
// interaction, and the judgement it states is "miss one and the player has to
// remember or guess". The list below is that tier, transcribed; a shell can only
// honour it if the shared projection carries every item, so this locks the
// projection rather than any one shell's rendering.
void l0DensityContracts()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("self"));
    state.setPlayerNames({QStringLiteral("self")});
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("self"));
    state.setGameValue(QStringLiteral("current_phase"), QStringLiteral("play"));
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("hp"), 3);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("max_hp"), 4);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("hand_count"), 2);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("role"), QStringLiteral("loyalist"));
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("kingdom"), QStringLiteral("shu"));
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("alive"), true);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("faceup"), false);
    state.setPlayerValue(QStringLiteral("self"), QStringLiteral("chained"), true);
    state.setPlayerMark(QStringLiteral("self"), QStringLiteral("@rescue"), 1);
    state.setCardValue(41, QStringLiteral("owner"), QStringLiteral("self"));
    state.setCardValue(41, QStringLiteral("place"), 1);
    state.setCardValue(41, QStringLiteral("object_name"), QStringLiteral("crossbow"));
    state.setCardValue(42, QStringLiteral("owner"), QStringLiteral("self"));
    state.setCardValue(42, QStringLiteral("place"), 2);
    state.setCardValue(42, QStringLiteral("object_name"), QStringLiteral("lightning"));

    const QJsonObject view = GameViewState::fromState(state).toJson();
    const QJsonObject player = view.value(QStringLiteral("players")).toArray().first().toObject();

    // 回合／階段 lives on the state, the rest per player.
    check(!view.value(QStringLiteral("phase_id")).toString().isEmpty()
          && !view.value(QStringLiteral("current_player_name")).toString().isEmpty(),
          "L0 keeps the round/phase and the current player on the shared state");

    // hp / max hp / hand count / role / kingdom / alive / face-up / chained /
    // marks / equipment / judging zone.
    static const char *const kL0Keys[] = {
        "hp", "max_hp", "hand_count", "role", "kingdom", "alive",
        "face_up", "chained", "marks", "equipment", "judging"
    };
    QStringList missing;
    for (const char *key : kL0Keys) {
        if (!player.contains(QLatin1String(key)))
            missing << QLatin1String(key);
    }
    const QByteArray l0Message = missing.isEmpty()
        ? QByteArrayLiteral("L0 projects every permanently visible player field")
        : QByteArrayLiteral("L0 is missing ") + missing.join(QLatin1String(", ")).toUtf8();
    check(missing.isEmpty(), l0Message.constData());

    // Presence alone would pass on a default-constructed field, so pin the values
    // that came from the state for the two that carry no default.
    check(player.value(QStringLiteral("kingdom")).toString() == QStringLiteral("shu")
          && player.value(QStringLiteral("role")).toString() == QStringLiteral("loyalist"),
          "L0 identity and kingdom carry the values the server set, not defaults");
    check(player.value(QStringLiteral("marks")).toObject().contains(QStringLiteral("@rescue"))
          && player.value(QStringLiteral("equipment")).toArray().size() == 1
          && player.value(QStringLiteral("judging")).toArray().size() == 1,
          "L0 marks, equipment and judging zone survive the projection");
}

// docs/ui-roadmap.md 2.2: the envelope opens with a deadline meter, so the
// countdown has to be one shared projection instead of a subtraction each shell
// repeats. The deadline instant itself is still live -- ClientCore's timer is
// armed for deadlineMs + 1.
void deadlineProjectionContracts()
{
    InteractionRequest request;
    request.type = InteractionType::Choice;
    request.timeoutMs = 15000;
    request.deadlineMs = 15000;
    check(request.remainingMs(0) == 15000 && request.remainingMs(14999) == 1,
          "remaining time counts down from the deadline on the ClientCore clock");
    check(request.remainingMs(15000) == 0 && request.remainingMs(20000) == 0,
          "remaining time never goes negative once the deadline passes");
    check(!request.isExpired(14999) && !request.isExpired(15000) && request.isExpired(15001),
          "the deadline instant is still live and expiry starts one tick later");

    InteractionRequest untimed;
    untimed.type = InteractionType::Choice;
    check(untimed.remainingMs(1 << 20) == -1 && !untimed.isExpired(1 << 20),
          "a request without a deadline reports no countdown and never expires");
}

void actionModelContracts()
{
    GameActionModel model;
    model.sessionGeneration = 4;
    model.presentationRevision = 8;
    model.requestId = 16;
    model.request.requestId = Q_UINT64_C(18446744073709551610);
    model.request.type = InteractionType::Choice;
    model.cards.append({QStringLiteral("card:7"), QStringLiteral("Display only"), true, true, {}});
    GameActionEntry votedPlayer{QStringLiteral("other"), QStringLiteral("Other"), true, true, {}};
    votedPlayer.selectedVotes = 2;
    votedPlayer.maxVotes = 3;
    model.players.append(votedPlayer);
    check(model.isCurrentFor(4, 8, 16) && !model.isCurrentFor(4, 9, 16),
          "selection draft is rejected when its presentation revision becomes stale");
    const QJsonObject entry = model.toJson().value(QStringLiteral("cards")).toArray().first().toObject();
    check(entry.value(QStringLiteral("id")).toString() == QStringLiteral("card:7")
          && entry.value(QStringLiteral("label")).toString() == QStringLiteral("Display only"),
          "action entries keep stable ids separate from display labels");
    const QJsonObject votes = model.toJson().value(QStringLiteral("players")).toArray().first().toObject();
    check(votes.value(QStringLiteral("selected_votes")).toInt() == 2
          && votes.value(QStringLiteral("max_votes")).toInt() == 3,
          "player actions preserve current multi-vote count and legal maximum");
    check(model.toJson().value(QStringLiteral("request")).toObject()
              .value(QStringLiteral("request_id")).toString() == QStringLiteral("18446744073709551610"),
          "embedded request preserves complete quint64 correlation id");
}

void eventStreamContracts()
{
    ClientGameState state;
    state.appendPresentationEvent(2, QStringLiteral("same"));
    state.appendPresentationEvent(2, QStringLiteral("same"));
    GameEventStream stream;
    stream.synchronize(state, 9);
    const auto first = stream.since(0);
    stream.synchronize(state, 9);
    check(first.size() == 2 && first.at(0).sequence == 1 && first.at(1).sequence == 2
          && stream.since(0).size() == 2,
          "event sequence preserves identical adjacent events without duplicate imports");
    stream.reset(10);
    check(stream.events().isEmpty() && stream.generation() == 10,
          "new session generation invalidates previous event cursors");
    for (int i = 0; i < 205; ++i)
        stream.append(1, QStringLiteral("event"));
    check(stream.events().size() == GameEventStream::MaximumEvents
          && stream.events().first().sequence == 6,
          "event stream retains at most 200 sequenced events");
}

// docs/focus-relation-protocol-decision.md 1.3: the battle log already carries
// the settlement relation as structured fields, and two projection layers used
// to drop it. Damage and somebody else's nullification are the two cases
// docs/ui-roadmap.md 2.7 names, so both have to reach the shared model.
void settlementRelationContracts()
{
    ClientGameState state;
    state.setSelfName(QStringLiteral("sgs1"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2")});
    state.appendPresentationEvent(QSanProtocol::S_COMMAND_LOG_SKILL,
        QStringLiteral("#Damage sgs1"),
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("log_type"), QStringLiteral("#Damage")},
                    {QStringLiteral("from_player"), QStringLiteral("sgs1")},
                    {QStringLiteral("to_players"), QStringList{QStringLiteral("sgs2")}},
                    {QStringLiteral("arguments"), QStringList{QStringLiteral("1"), QStringLiteral("fire")}}});
    state.appendPresentationEvent(QSanProtocol::S_COMMAND_LOG_SKILL,
        QStringLiteral("#NullificationDetails sgs2"),
        QVariantMap{{QStringLiteral("schema_version"), 1},
                    {QStringLiteral("log_type"), QStringLiteral("#NullificationDetails")},
                    {QStringLiteral("from_player"), QStringLiteral("sgs2")},
                    {QStringLiteral("to_players"), QStringList{QStringLiteral("sgs1")}},
                    {QStringLiteral("card_string"), QStringLiteral("slash:_dismantlement")}});

    GameEventStream stream;
    stream.synchronize(state, 1);
    const QList<GamePresentationEvent> imported = stream.since(0);
    check(imported.size() == 2
          && imported.at(0).payload.toMap().value(QStringLiteral("from_player")).toString()
                 == QStringLiteral("sgs1"),
          "the event stream imports the log payload instead of dropping it");

    const GameViewState view = GameViewState::fromState(state);
    check(view.recentEvents.size() == 2 && view.recentRelations.size() == 2,
          "narration and settlement relation are projected side by side");
    const QVariantMap damage = view.recentRelations.value(0).toMap();
    check(damage.value(QStringLiteral("log_type")).toString() == QStringLiteral("#Damage")
          && damage.value(QStringLiteral("from")).toString() == QStringLiteral("sgs1")
          && damage.value(QStringLiteral("to")).toStringList()
                 == QStringList{QStringLiteral("sgs2")},
          "#Damage keeps its source and target through the projection");
    const QVariantMap nullified = view.recentRelations.value(1).toMap();
    check(nullified.value(QStringLiteral("from")).toString() == QStringLiteral("sgs2")
          && nullified.value(QStringLiteral("to")).toStringList()
                 == QStringList{QStringLiteral("sgs1")}
          && nullified.value(QStringLiteral("card")).toString()
                 == QStringLiteral("slash:_dismantlement"),
          "#NullificationDetails keeps source, target and the card it answered");

    // 2.3: the direction is protocol state now, and absent means not reversed.
    check(!GameViewState::fromState(state).playOrderReversed,
          "play direction defaults to the original order when nothing set it");
    state.setGameValue(QStringLiteral("play_order_reversed"), true);
    check(GameViewState::fromState(state).playOrderReversed
          && GameViewState::fromState(state).toJson()
                 .value(QStringLiteral("play_order_reversed")).toBool(),
          "a reversed seat ring reaches every shell through the shared projection");
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    viewStateContracts();
    l0DensityContracts();
    deadlineProjectionContracts();
    actionModelContracts();
    eventStreamContracts();
    settlementRelationContracts();
    return failures == 0 ? 0 : 1;
}
