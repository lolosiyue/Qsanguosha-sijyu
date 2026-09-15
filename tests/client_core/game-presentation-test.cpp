// QtCore-only contract checks for the shared game presentation projections.
#include "../../src/client/core/client-game-state.h"
#include "../../src/client/core/game-action-model.h"
#include "../../src/client/core/game-event-stream.h"
#include "../../src/client/core/game-view-state.h"

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
        return text == QStringLiteral("ask") ? QStringLiteral("請求 %src") : text;
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

void actionModelContracts()
{
    GameActionModel model;
    model.sessionGeneration = 4;
    model.presentationRevision = 8;
    model.requestId = 16;
    model.request.requestId = Q_UINT64_C(18446744073709551610);
    model.request.type = InteractionType::Choice;
    model.cards.append({QStringLiteral("card:7"), QStringLiteral("Display only"), true, true, {}});
    check(model.isCurrentFor(4, 8, 16) && !model.isCurrentFor(4, 9, 16),
          "selection draft is rejected when its presentation revision becomes stale");
    const QJsonObject entry = model.toJson().value(QStringLiteral("cards")).toArray().first().toObject();
    check(entry.value(QStringLiteral("id")).toString() == QStringLiteral("card:7")
          && entry.value(QStringLiteral("label")).toString() == QStringLiteral("Display only"),
          "action entries keep stable ids separate from display labels");
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
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    viewStateContracts();
    actionModelContracts();
    eventStreamContracts();
    return failures == 0 ? 0 : 1;
}
