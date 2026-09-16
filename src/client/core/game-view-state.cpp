#include "game-view-state.h"
#include "client-prompt.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <algorithm>

namespace {
QString safeText(QString value);

QString playerLabel(const QString &name, const GameViewFormatOptions &options)
{
    const QString translated = options.playerLabel ? options.playerLabel(name) : QString();
    return safeText(translated.isEmpty() ? name : translated);
}

QString cardLabel(int id, const QVariantMap &card, const GameViewFormatOptions &options)
{
    const QString translated = options.cardLabel ? options.cardLabel(id) : QString();
    if (!translated.isEmpty())
        return safeText(translated);
    const QString name = card.value(QStringLiteral("object_name")).toString();
    return name.isEmpty() ? QCoreApplication::translate("GameViewState", "卡牌 %1").arg(id) : safeText(name);
}

QString safeText(QString value)
{
    // Remove markup before decoding escaped literals, so "&lt;牌&gt;" stays text.
    value.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]+")), QStringLiteral(" "));
    value.remove(QRegularExpression(QStringLiteral("[\\x{200b}-\\x{200f}\\x{202a}-\\x{202e}\\x{2066}-\\x{2069}]")));
    return value.simplified();
}

QString translatedLabel(const QString &key, const GameViewFormatOptions &options)
{
    const QString translated = options.translate ? options.translate(key) : QString();
    return safeText(translated.isEmpty() ? key : translated);
}

QString pileText(const QVariantMap &piles)
{
    QStringList values;
    for (auto it = piles.constBegin(); it != piles.constEnd(); ++it) {
        const QVariantMap pile = it.value().toMap();
        QStringList cards;
        for (const QVariant &entry : pile.value(QStringLiteral("cards")).toList())
            cards.append(entry.toMap().value(QStringLiteral("label")).toString());
        const int unknownCount = qMax(0, pile.value(QStringLiteral("count")).toInt() - cards.size());
        if (unknownCount > 0)
            cards.append(QCoreApplication::translate("GameViewState", "未知 %1 張").arg(unknownCount));
        values.append(QStringLiteral("%1：%2").arg(safeText(it.key()), cards.isEmpty()
            ? QCoreApplication::translate("GameViewState", "%1 張").arg(pile.value(QStringLiteral("count")).toInt())
            : cards.join(QStringLiteral("、"))));
    }
    return values.isEmpty() ? QCoreApplication::translate("GameViewState", "無")
                            : values.join(QStringLiteral("、"));
}

QList<int> visibleZoneCards(const ClientGameState &state, const QString &owner,
                            int place)
{
    QList<int> result;
    for (int id : state.cardsForPlayer(owner, place)) {
        if (state.isKnownCardId(id))
            result.append(id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

QList<GameViewCard> makeCards(const ClientGameState &state, const QList<int> &ids,
                              const QString &place, const GameViewFormatOptions &options)
{
    QList<GameViewCard> result;
    for (int id : ids) {
        const QVariantMap data = state.card(id);
        result.append({id, cardLabel(id, data, options), place, true});
    }
    return result;
}

QString cardNames(const QList<GameViewCard> &cards)
{
    QStringList names;
    for (const GameViewCard &card : cards)
        names.append(card.label);
    return names.isEmpty() ? QCoreApplication::translate("GameViewState", "無")
                           : names.join(QStringLiteral("、"));
}
}

QJsonObject GameViewCard::toJson() const
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("place"), place}, {QStringLiteral("visible"), visible}};
}

QJsonObject GameViewPlayer::toJson() const
{
    QJsonArray handJson, equipmentJson, judgingJson;
    for (const GameViewCard &card : hand) handJson.append(card.toJson());
    for (const GameViewCard &card : equipment) equipmentJson.append(card.toJson());
    for (const GameViewCard &card : judging) judgingJson.append(card.toJson());
    return {{QStringLiteral("name"), name}, {QStringLiteral("label"), label},
            {QStringLiteral("seat"), seat}, {QStringLiteral("hp"), hp},
            {QStringLiteral("max_hp"), maxHp}, {QStringLiteral("hand_count"), handCount},
            {QStringLiteral("distance_from_operating_player"), distanceFromOperatingPlayer},
            {QStringLiteral("alive"), alive}, {QStringLiteral("self"), self},
            {QStringLiteral("hand_visible"), handVisible},
            {QStringLiteral("face_up"), QJsonValue::fromVariant(faceUp)},
            {QStringLiteral("chained"), QJsonValue::fromVariant(chained)},
            {QStringLiteral("removed"), QJsonValue::fromVariant(removed)},
            {QStringLiteral("role"), role},
            {QStringLiteral("kingdom"), kingdom},
            {QStringLiteral("hand_max"), handMax},
            {QStringLiteral("offensive_distance"), offensiveDistance},
            {QStringLiteral("defensive_distance"), defensiveDistance},
            {QStringLiteral("marks"), QJsonObject::fromVariantMap(marks)},
            {QStringLiteral("skills"), QJsonArray::fromStringList(skills)},
            {QStringLiteral("hand"), handJson}, {QStringLiteral("equipment"), equipmentJson},
            {QStringLiteral("judging"), judgingJson},
            {QStringLiteral("piles"), QJsonObject::fromVariantMap(piles)}};
}

GameViewState GameViewState::fromState(const ClientGameState &state,
                                      const InteractionRequest *request,
                                      quint64 generation, quint64 revision,
                                      const GameViewFormatOptions &options)
{
    GameViewState view;
    view.sessionGeneration = generation;
    view.presentationRevision = revision;
    view.requestId = request ? request->requestId : 0;
    view.selfName = state.selfName();
    view.selfLabel = playerLabel(view.selfName, options);
    view.currentPlayer = state.gameValue(QStringLiteral("current_player")).toString();
    view.currentPlayerLabel = playerLabel(view.currentPlayer, options);
    view.operatingPlayer = options.operatingPlayer.isEmpty() ? view.selfName : options.operatingPlayer;
    view.operatingPlayerLabel = playerLabel(view.operatingPlayer, options);
    view.phase = state.gameValue(QStringLiteral("current_phase")).toString();
    view.phaseLabel = options.phaseLabel ? options.phaseLabel(view.phase) : view.phase;
    if (view.phaseLabel.isEmpty()) view.phaseLabel = view.phase;
    view.phaseLabel = safeText(view.phaseLabel);
    if (request) {
        view.prompt = formatClientPrompt(request->prompt, options.translate, options.playerLabel);
        view.prompt = safeText(view.prompt);
    }
    const QVariant drawPileCount = state.gameValue(QStringLiteral("draw_pile_count"));
    view.drawPileCount = drawPileCount.isValid() ? drawPileCount.toInt() : -1;
    view.discardPileCount = state.gameValue(QStringLiteral("discard_pile")).toList().size();
    view.playOrderReversed = state.gameValue(QStringLiteral("play_order_reversed")).toBool();
    view.ready = options.stateReady;

    for (const QString &name : state.playerNames()) {
        if (!state.hasPlayer(name))
            continue;
        const QVariantMap data = state.player(name);
        GameViewPlayer player;
        player.name = name;
        player.label = playerLabel(name, options);
        player.seat = data.value(QStringLiteral("seat"), -1).toInt();
        player.hp = data.value(QStringLiteral("hp")).toInt();
        player.maxHp = data.value(QStringLiteral("max_hp")).toInt();
        player.handCount = qMax(0, data.value(QStringLiteral("hand_count")).toInt());
        player.handMax = data.value(QStringLiteral("hand_max"), -1).toInt();
        player.offensiveDistance = data.value(QStringLiteral("offensive_distance"), -1).toInt();
        player.defensiveDistance = data.value(QStringLiteral("defensive_distance"), -1).toInt();
        const QVariantMap marks = data.value(QStringLiteral("marks")).toMap();
        for (auto mark = marks.constBegin(); mark != marks.constEnd(); ++mark)
            player.marks.insert(translatedLabel(mark.key(), options), mark.value());
        const QStringList skills = data.value(QStringLiteral("skills")).toStringList();
        for (const QString &skill : skills)
            player.skills.append(translatedLabel(skill, options));
        if (!options.operatingPlayer.isEmpty() && options.distanceLabel)
            player.distanceFromOperatingPlayer = safeText(options.distanceLabel(options.operatingPlayer, name));
        player.alive = data.value(QStringLiteral("alive"), true).toBool();
        player.faceUp = data.value(QStringLiteral("faceup"));
        player.chained = data.value(QStringLiteral("chained"));
        player.removed = data.value(QStringLiteral("removed"));
        if (data.contains(QStringLiteral("role")))
            player.role = translatedLabel(data.value(QStringLiteral("role")).toString(), options);
        if (data.contains(QStringLiteral("kingdom")))
            player.kingdom = translatedLabel(data.value(QStringLiteral("kingdom")).toString(), options);
        player.self = name == view.selfName;
        // Only the recipient's hand has authorized identities. Opponents expose counts only.
        const bool handVisible = player.self || options.authorizedHandPlayers.contains(name);
        player.handVisible = handVisible;
        if (handVisible)
            player.hand = makeCards(state, visibleZoneCards(state, name, 0), QStringLiteral("hand"), options);
        player.equipment = makeCards(state, visibleZoneCards(state, name, 1), QStringLiteral("equipment"), options);
        player.judging = makeCards(state, visibleZoneCards(state, name, 2), QStringLiteral("judging"), options);

        QVariantMap safePiles;
        const QVariantMap piles = data.value(QStringLiteral("piles")).toMap();
        for (auto it = piles.constBegin(); it != piles.constEnd(); ++it) {
            int visibleCount = 0;
            QVariantList visibleCards;
            for (const QVariant &entry : it.value().toList()) {
                const int id = entry.toInt();
                const QVariantMap card = state.card(id);
                if (id < 0) {
                    ++visibleCount;
                    continue;
                }
                const bool currentPileCard = state.isKnownCardId(id) && !card.isEmpty()
                    && card.value(QStringLiteral("owner")).toString() == name
                    && card.value(QStringLiteral("place")).toInt() == 4
                    && card.value(QStringLiteral("pile")).toString() == it.key();
                if (!currentPileCard) continue; // Discard stale identities from earlier snapshots.
                ++visibleCount;
                if (player.self) {
                    visibleCards.append(QVariantMap{
                        {QStringLiteral("id"), id},
                        {QStringLiteral("label"), cardLabel(id, card, options)},
                        {QStringLiteral("visible"), true}});
                }
            }
            QVariantMap pileView{{QStringLiteral("count"), visibleCount}};
            if (player.self) pileView.insert(QStringLiteral("cards"), visibleCards);
            safePiles.insert(it.key(), pileView);
        }
        player.piles = safePiles;
        if (player.self)
            view.privatePiles = safePiles;
        view.players.append(player);
    }
    // Presentation payloads may contain protocol fields unsuitable for a public
    // snapshot, so the narration keeps carrying text only and the settlement
    // relation below is a named whitelist rather than the payload itself.
    for (const QVariant &raw : state.presentationEvents()) {
        const QVariantMap event = raw.toMap();
        view.recentEvents.append(QVariantMap{
            {QStringLiteral("command"), event.value(QStringLiteral("command"))},
            {QStringLiteral("text"), safeText(event.value(QStringLiteral("text")).toString())}});
        if (event.value(QStringLiteral("command")).toInt() != QSanProtocol::S_COMMAND_LOG_SKILL)
            continue;
        const QVariantMap log = event.value(QStringLiteral("payload")).toMap();
        const QString logType = log.value(QStringLiteral("log_type")).toString();
        if (logType.isEmpty()) continue;
        QVariantMap relation{{QStringLiteral("log_type"), logType}};
        const QString from = log.value(QStringLiteral("from_player")).toString();
        if (!from.isEmpty()) {
            relation.insert(QStringLiteral("from"), from);
            relation.insert(QStringLiteral("from_label"), playerLabel(from, options));
        }
        QStringList targets, targetLabels;
        for (const QString &target : log.value(QStringLiteral("to_players")).toStringList()) {
            if (target.isEmpty()) continue;
            targets << target;
            targetLabels << playerLabel(target, options);
        }
        if (!targets.isEmpty()) {
            relation.insert(QStringLiteral("to"), targets);
            relation.insert(QStringLiteral("to_labels"), targetLabels);
        }
        const QString card = log.value(QStringLiteral("card_string")).toString();
        // 1.4: the log is a narration, not a settlement chain -- the card string
        // is what links a nullification back to the trick it answered.
        if (!card.isEmpty()) relation.insert(QStringLiteral("card"), card);
        view.recentRelations.append(relation);
    }
    return view;
}

QJsonObject GameViewState::toJson() const
{
    QJsonArray playerArray;
    for (const GameViewPlayer &player : players) playerArray.append(player.toJson());
    return {{QStringLiteral("session_generation"), QString::number(sessionGeneration)},
            {QStringLiteral("presentation_revision"), QString::number(presentationRevision)},
            {QStringLiteral("request_id"), QString::number(requestId)},
            {QStringLiteral("ready"), ready}, {QStringLiteral("self"), selfLabel},
            {QStringLiteral("self_name"), selfName}, {QStringLiteral("current_player"), currentPlayerLabel},
            {QStringLiteral("current_player_name"), currentPlayer},
            {QStringLiteral("operating_player"), operatingPlayer},
            {QStringLiteral("operating_player_label"), operatingPlayerLabel},
            {QStringLiteral("phase"), phaseLabel}, {QStringLiteral("phase_id"), phase},
            {QStringLiteral("prompt"), prompt}, {QStringLiteral("draw_pile_count"), drawPileCount},
            {QStringLiteral("discard_pile_count"), discardPileCount}, {QStringLiteral("players"), playerArray},
            {QStringLiteral("private_piles"), QJsonObject::fromVariantMap(privatePiles)},
            {QStringLiteral("play_order_reversed"), playOrderReversed},
            {QStringLiteral("recent_events"), QJsonArray::fromVariantList(recentEvents)},
            {QStringLiteral("recent_relations"), QJsonArray::fromVariantList(recentRelations)}};
}

QString GameViewState::toPlainText() const
{
    QStringList lines;
    int selfHp = 0, selfMaxHp = 0;
    for (const GameViewPlayer &player : players) {
        if (player.self) { selfHp = player.hp; selfMaxHp = player.maxHp; break; }
    }
    lines << QCoreApplication::translate("GameViewState", "本人：%1　體力 %2/%3")
        .arg(selfLabel).arg(selfHp).arg(selfMaxHp);
    lines << QCoreApplication::translate("GameViewState", "階段：%1").arg(phaseLabel);
    lines << QCoreApplication::translate("GameViewState", "目前玩家：%1").arg(currentPlayerLabel);
    lines << QCoreApplication::translate("GameViewState", "操作角色：%1").arg(operatingPlayerLabel);
    lines << QCoreApplication::translate("GameViewState", "牌堆：%1　棄牌堆：%2")
        .arg(drawPileCount < 0 ? QCoreApplication::translate("GameViewState", "未知") : QString::number(drawPileCount))
        .arg(discardPileCount);
    for (const GameViewPlayer &player : players) {
        if (player.handVisible)
            lines << QCoreApplication::translate("GameViewState", "%1 手牌：%2")
                .arg(player.label, cardNames(player.hand));
        const QString distance = player.distanceFromOperatingPlayer.isEmpty()
            ? QCoreApplication::translate("GameViewState", "未知") : player.distanceFromOperatingPlayer;
        lines << QCoreApplication::translate("GameViewState", "%1｜座位 %2｜體力 %3/%4｜手牌 %5｜距離 %6｜裝備：%7｜判定：%8｜私有牌堆：%9")
            .arg(player.label).arg(player.seat).arg(player.hp).arg(player.maxHp).arg(player.handCount)
            .arg(distance).arg(cardNames(player.equipment)).arg(cardNames(player.judging))
            .arg(pileText(player.piles));
    }
    lines << QCoreApplication::translate("GameViewState", "目前提示：%1").arg(prompt);
    return lines.join(QLatin1Char('\n'));
}
