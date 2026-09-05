#include "tui-renderer.h"
#include "tui-text.h"

#include "client-prompt.h"
#include "core/client-game-state.h"
#include "core/interaction-model.h"
#include "protocol.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <functional>
#include <utility>

// Card::HandlingMethod only; nothing here calls into the engine.
#include "card.h"

namespace {

QString joinIntegers(const QList<int> &values)
{
    QStringList result;
    for (int value : values)
        result.append(QString::number(value));
    return result.join(QLatin1Char(' '));
}

QStringList stringList(const QVariant &value)
{
    QStringList result = value.toStringList();
    if (!result.isEmpty())
        return result;
    for (const QVariant &entry : value.toList())
        result.append(entry.toString());
    return result;
}

void appendOptions(QStringList *lines, const QList<InteractionOption> &options)
{
    for (int i = 0; i < options.size(); ++i) {
        const InteractionOption &option = options.at(i);
        lines->append(QStringLiteral("  [%1] %2%3").arg(i + 1)
            .arg(option.label.isEmpty() ? option.value : option.label,
                 option.enabled ? QString() : tuiText("tui_card_disabled")));
    }
}

// Turns the engine's opinion about a card's targets into the tail of its menu
// line, numbered the way the target list below is numbered.
QString targetNote(const TuiRenderer::CardTargets &advice, const QStringList &targetOrder)
{
    if (!advice.known)
        return QString();
    if (advice.targetFixed)
        return tuiText("tui_targets_not_needed");
    if (advice.targets.isEmpty())
        return tuiText("tui_targets_unavailable");
    QStringList numbers;
    for (const QString &name : advice.targets) {
        const qsizetype index = targetOrder.indexOf(name);
        // A legal target the prompt did not offer is the server's call, not
        // ours; name it rather than dropping it.
        QString entry = index >= 0 ? QStringLiteral("[%1]").arg(index + 1)
                                   : TuiRenderer::sanitize(name, 64);
        const int votes = advice.maxVotes.value(name, 1);
        if (votes > 1)
            entry += tuiText("tui_targets_votes").arg(votes);
        numbers << entry;
    }
    return tuiText("tui_targets_optional").arg(numbers.join(QString()));
}

void appendCards(QStringList *lines, const QList<int> &cards, const QList<int> &disabled,
                 const TuiRenderer::CardResolver &resolver,
                 const TuiRenderer::CardHintResolver &hint, int startIndex = 1,
                 const TuiRenderer::CardTargetResolver &targets = {},
                 const QStringList &targetOrder = {})
{
    for (int i = 0; i < cards.size(); ++i) {
        const int cardId = cards.at(i);
        const QString display = resolver ? TuiRenderer::sanitize(resolver(cardId), 512)
                                         : tuiText("tui_card_unknown").arg(cardId);
        QString note = disabled.contains(cardId) ? tuiText("tui_card_disabled") : QString();
        const bool serverDisabled = !note.isEmpty();
        if (note.isEmpty() && hint)
            note = TuiRenderer::sanitize(hint(cardId), 64);
        // Only worth asking where the card could go if it can be played at all.
        if (!serverDisabled && note.isEmpty() && targets)
            note += targetNote(targets(cardId), targetOrder);
        lines->append(tuiText("tui_card_line").arg(startIndex + i).arg(display).arg(cardId)
            .arg(note));
    }
}

void appendPlayers(QStringList *lines, const QStringList &names,
                   const std::function<QString(const QString &)> &resolve,
                   const TuiRenderer::PlayerHintResolver &hint = {})
{
    for (int i = 0; i < names.size(); ++i) {
        const QString objectName = names.at(i);
        const QString shown = resolve(objectName);
        const QString note = hint ? TuiRenderer::sanitize(hint(objectName), 64) : QString();
        lines->append(shown == objectName
            ? QStringLiteral("  [%1] %2%3").arg(i + 1)
                .arg(TuiRenderer::sanitize(objectName, 128), note)
            : QStringLiteral("  [%1] %2（%3）%4").arg(i + 1)
                .arg(TuiRenderer::sanitize(shown, 128), TuiRenderer::sanitize(objectName, 128),
                     note));
    }
}

} // namespace

TuiRenderer::TuiRenderer(bool ansiEnabled, Resolvers resolvers)
    : m_ansiEnabled(ansiEnabled), m_resolvers(std::move(resolvers))
{
}

QString TuiRenderer::formatPrompt(const QString &prompt,
                                  const std::function<QString(const QString &)> &translate,
                                  const std::function<QString(const QString &)> &playerName)
{
    static const QRegularExpression sgsName(
        QStringLiteral("^sgs\\d+$"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto slotPlayer = [&](const QString &token) {
        if (token.isEmpty())
            return token;
        if (sgsName.match(token).hasMatch() && playerName)
            return playerName(token);
        return translate ? translate(token) : token;
    };
    return formatClientPrompt(prompt, translate, slotPlayer);
}

QString TuiRenderer::sanitize(const QString &text, qsizetype maximumLength)
{
    QString result;
    result.reserve(qMin(text.size(), maximumLength));
    for (QChar character : text) {
        const ushort value = character.unicode();
        if (value == 0x1b || value == 0x7f)
            continue;
        if (value < 0x20 && character != QLatin1Char('\t')
            && character != QLatin1Char('\n')) {
            continue;
        }
        result.append(character);
        if (result.size() >= maximumLength)
            break;
    }
    return result;
}

// Chat, system notices and several lang templates are written for the markup
// the desktop log box renders. A text transcript must not show tags, and a
// line break has to survive as a separator rather than glue two sentences
// together.
QString TuiRenderer::plainText(const QString &text)
{
    static const QSet<QString> breaks{QStringLiteral("br"), QStringLiteral("p"),
        QStringLiteral("div"), QStringLiteral("tr")};

    QString result;
    result.reserve(text.size());
    QString tag;
    bool inTag = false;
    for (QChar character : text) {
        if (character == QLatin1Char('<')) {
            inTag = true;
            tag.clear();
        } else if (inTag && character == QLatin1Char('>')) {
            inTag = false;
            // "br", "br/", "br /" and "/br" all name the same tag.
            const QString name = tag.trimmed().section(QLatin1Char(' '), 0, 0)
                .remove(QLatin1Char('/')).toLower();
            if (breaks.contains(name))
                result.append(QLatin1Char(' '));
        } else if (inTag) {
            tag.append(character);
        } else {
            result.append(character);
        }
    }
    // A lone "<" a player typed in chat is text, not the start of a tag.
    if (inTag)
        result.append(QLatin1Char('<')).append(tag);

    // The same templates escape the characters they cannot spell literally.
    static const QList<QPair<QString, QString>> entities{
        {QStringLiteral("&nbsp;"), QStringLiteral(" ")},
        {QStringLiteral("&lt;"), QStringLiteral("<")},
        {QStringLiteral("&gt;"), QStringLiteral(">")},
        {QStringLiteral("&quot;"), QStringLiteral("\"")},
        {QStringLiteral("&#39;"), QStringLiteral("'")},
        {QStringLiteral("&amp;"), QStringLiteral("&")}};
    for (const auto &entity : entities)
        result.replace(entity.first, entity.second, Qt::CaseInsensitive);

    return result.simplified();
}

QString TuiRenderer::commandResultText(int command, bool success, const QString &message)
{
    // Keys, not text: a static table built on the first call would freeze
    // whatever tuiText() answered back then, including the bare key it answers
    // before the engine is up.
    static const QHash<int, const char *> labels{
        {QSanProtocol::S_COMMAND_SPEAK, "tui_command_label_speak"},
        {QSanProtocol::S_COMMAND_TRUST, "tui_command_label_trust"},
        {QSanProtocol::S_COMMAND_ADD_ROBOT, "tui_command_label_add_robot"},
        {QSanProtocol::S_COMMAND_SURRENDER, "tui_command_label_surrender"}};
    const QString label = labels.contains(command)
        ? tuiText(labels.value(command)) : QString();
    const QString detail = sanitize(message, 512);
    if (!success) {
        const QString what = label.isEmpty()
            ? tuiText("tui_command_label_generic").arg(command) : label;
        return detail.isEmpty() ? tuiText("tui_command_failed").arg(what)
                                : tuiText("tui_command_failed_detail").arg(what, detail);
    }
    // Delay probes and other housekeeping succeed constantly; only confirm what
    // the player actually asked for.
    if (label.isEmpty())
        return QString();
    return detail.isEmpty() ? tuiText("tui_command_done").arg(label)
                            : tuiText("tui_command_done_detail").arg(label, detail);
}

QString TuiRenderer::heading(const QString &text) const
{
    const QString safe = sanitize(text, 256);
    return m_ansiEnabled ? QStringLiteral("\x1b[1;36m%1\x1b[0m").arg(safe)
                         : QStringLiteral("== %1 ==").arg(safe);
}

QString TuiRenderer::cardText(const ClientGameState &state, int cardId) const
{
    if (m_resolvers.card)
        return sanitize(m_resolvers.card(cardId), 512);
    const QVariantMap card = state.card(cardId);
    const QString name = card.value(QStringLiteral("card_name")).toString();
    if (!name.isEmpty()) {
        return QStringLiteral("%1 %2 %3").arg(name,
            card.value(QStringLiteral("suit")).toString(),
            card.value(QStringLiteral("number")).toString()).trimmed();
    }
    return tuiText("tui_card_unknown").arg(cardId);
}

QString TuiRenderer::playerText(const QString &objectName) const
{
    if (objectName.isEmpty() || !m_resolvers.player)
        return objectName;
    const QString shown = m_resolvers.player(objectName);
    return shown.isEmpty() ? objectName : shown;
}

// The room status is its own vocabulary: "active" here means the game is under
// way, not that the socket is up.
QString TuiRenderer::gameStatusText(const QString &status) const
{
    static const QHash<QString, const char *> labels{
        {QStringLiteral("waiting"), "tui_game_status_waiting"},
        {QStringLiteral("active"), "tui_game_status_active"},
        {QStringLiteral("game_over"), "tui_game_status_over"}};
    const auto fixed = labels.constFind(status.toLower());
    return fixed != labels.cend() ? tuiText(fixed.value()) : nameText(status);
}

// The server never broadcasts the kingdom property, so fall back to the
// general's own kingdom the way Player::getKingdom() does.
QString TuiRenderer::kingdomText(const QVariantMap &player) const
{
    QString kingdom = player.value(QStringLiteral("kingdom")).toString();
    if (kingdom.isEmpty() && m_resolvers.kingdom) {
        kingdom = m_resolvers.kingdom(
            player.value(QStringLiteral("general")).toString());
    }
    // "wei+shu" means the player has yet to declare one.
    if (kingdom.contains(QLatin1Char('+'))) {
        const QStringList kingdoms = kingdom.split(QLatin1Char('+'), Qt::SkipEmptyParts);
        QStringList shown;
        for (const QString &one : kingdoms)
            shown << nameText(one);
        return shown.join(QLatin1Char('/'));
    }
    return kingdom.isEmpty() ? tuiText("tui_kingdom_unknown") : nameText(kingdom);
}

QString TuiRenderer::playerLabel(const QString &objectName) const
{
    const QString shown = playerText(objectName);
    return shown == objectName ? sanitize(objectName, 128)
                               : tuiText("tui_player_label").arg(sanitize(shown, 128),
                                                   sanitize(objectName, 128));
}

QString TuiRenderer::nameText(const QString &name) const
{
    if (name.isEmpty())
        return name;
    static const QHash<QString, const char *> labels{
        {QStringLiteral("idle"), "tui_name_idle"},
        {QStringLiteral("connecting"), "tui_name_connecting"},
        {QStringLiteral("reconnecting"), "tui_name_reconnecting"},
        {QStringLiteral("handshake"), "tui_name_handshake"},
        {QStringLiteral("active"), "tui_name_active"},
        {QStringLiteral("disconnected"), "tui_name_disconnected"},
        {QStringLiteral("failed"), "tui_name_failed"},
        {QStringLiteral("waiting"), "tui_name_waiting"},
        {QStringLiteral("online"), "tui_name_online"},
        {QStringLiteral("offline"), "tui_name_offline"},
        {QStringLiteral("robot"), "tui_name_robot"},
        {QStringLiteral("trust"), "tui_name_trust"},
        {QStringLiteral("not_active"), "tui_name_not_active"},
        {QStringLiteral("round_start"), "tui_name_round_start"},
        {QStringLiteral("start"), "tui_name_phase_start"},
        {QStringLiteral("judge"), "tui_name_phase_judge"},
        {QStringLiteral("draw"), "tui_name_phase_draw"},
        {QStringLiteral("play"), "tui_name_phase_play"},
        {QStringLiteral("discard"), "tui_name_phase_discard"},
        {QStringLiteral("finish"), "tui_name_phase_finish"}};
    const auto fixed = labels.constFind(name.toLower());
    if (fixed != labels.cend())
        return tuiText(fixed.value());
    return sanitize(m_resolvers.name ? m_resolvers.name(name) : name, 256);
}

QString TuiRenderer::interactionTitle(const InteractionRequest &request) const
{
    switch (request.type) {
    case InteractionType::ChooseRole:
        return tuiText("tui_interaction_assign_roles");
    case InteractionType::ChooseGeneral:
    case InteractionType::AskGeneral:
        return tuiText("tui_interaction_choose_general");
    case InteractionType::ChooseDirection:
        return tuiText("tui_interaction_choose_direction");
    case InteractionType::PlayCard:
        return tuiText("tui_interaction_play_card");
    case InteractionType::ResponseCard:
        return tuiText("tui_interaction_response_card");
    case InteractionType::DiscardCard:
        return tuiText("tui_interaction_discard_card");
    case InteractionType::ExchangeCard:
        return tuiText("tui_interaction_exchange_card");
    case InteractionType::AskPeach:
        return tuiText("tui_interaction_ask_peach");
    case InteractionType::Nullification:
        return tuiText("tui_interaction_nullification");
    case InteractionType::Choice:
        return tuiText("tui_interaction_choice");
    case InteractionType::SkillInvoke:
        return tuiText("tui_interaction_invoke_skill");
    case InteractionType::ChoosePlayer:
        return tuiText("tui_interaction_choose_player");
    case InteractionType::ChooseCard:
        return tuiText("tui_interaction_choose_card");
    case InteractionType::ChooseSuit:
        return tuiText("tui_interaction_choose_suit");
    case InteractionType::ChooseKingdom:
        return tuiText("tui_interaction_choose_kingdom");
    case InteractionType::AmazingGrace:
        return tuiText("tui_interaction_amazing_grace");
    case InteractionType::SkillGuanxing:
        return tuiText("tui_interaction_guanxing");
    case InteractionType::SkillGongxin:
        return tuiText("tui_interaction_gongxin");
    case InteractionType::SkillYiji:
        return tuiText("tui_interaction_yiji");
    case InteractionType::Pindian:
        return tuiText("tui_interaction_pindian");
    case InteractionType::TriggerOrder:
        return tuiText("tui_interaction_skill_order");
    case InteractionType::ArrangeGeneral:
        return tuiText("tui_interaction_arrange_generals");
    case InteractionType::LuckCard:
        return tuiText("tui_interaction_luck_card");
    case InteractionType::Surrender:
        return tuiText("tui_interaction_surrender");
    default:
        return tuiText("tui_interaction_generic").arg(interactionTypeName(request.type));
    }
}

QString TuiRenderer::answerHint(const InteractionRequest &request) const
{
    switch (request.responseSchema) {
    case InteractionResponseShape::Assignment: {
        const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>();
        if (value == nullptr || value->playerNames.isEmpty())
            return tuiText("tui_answer_role_example");
        QStringList parts;
        for (int i = 0; i < value->playerNames.size(); ++i) {
            const QString role = (value->roles.size() == value->playerNames.size())
                ? nameText(value->roles.at(i)) : QStringLiteral("?");
            parts << QStringLiteral("%1=%2").arg(i + 1).arg(role);
        }
        return tuiText("tui_answer_role_assignment")
            .arg(parts.join(QLatin1Char(' ')));
    }
    case InteractionResponseShape::Option:
        return tuiText("tui_answer_option");
    case InteractionResponseShape::Players:
        return tuiText("tui_answer_players");
    case InteractionResponseShape::Cards:
        if (request.type == InteractionType::PlayCard) {
            return tuiText("tui_answer_play_card");
        }
        if (request.type == InteractionType::ChooseCard) {
            const auto *value = request.payloadAs<CardInteractionPayload>();
            if (value != nullptr && value->hiddenHandCount > 0) {
                return tuiText("tui_answer_choose_card_hidden");
            }
            return tuiText("tui_answer_card_single");
        }
        if (request.type == InteractionType::DiscardCard
            || request.type == InteractionType::ExchangeCard) {
            return tuiText("tui_answer_card_multi");
        }
        if (request.type == InteractionType::AskPeach) {
            return tuiText("tui_answer_ask_peach");
        }
        return tuiText("tui_answer_card_single");
    case InteractionResponseShape::Rearrangement:
        return tuiText("tui_answer_rearrangement");
    case InteractionResponseShape::Distribution:
        return tuiText("tui_answer_distribution");
    case InteractionResponseShape::GeneralArrangement:
        return tuiText("tui_answer_arrangement");
    case InteractionResponseShape::Custom:
        return tuiText("tui_answer_custom");
    default:
        return QString();
    }
}

QString TuiRenderer::renderState(const ClientGameState &state) const
{
    const QVariantMap connection = state.connection();
    const QVariantMap setup = state.setup();
    const QVariantMap game = state.game();
    QStringList lines{heading(tuiText("tui_status_title"))};
    lines << tuiText("tui_status_connection")
        .arg(nameText(connection.value(QStringLiteral("state"), QStringLiteral("idle")).toString()),
             connection.value(QStringLiteral("host")).toString(),
             connection.value(QStringLiteral("port")).toString(),
             connection.contains(QStringLiteral("latency_ms"))
                 ? connection.value(QStringLiteral("latency_ms")).toString()
                 : QStringLiteral("?"),
             connection.value(QStringLiteral("reconnected")).toBool()
                 ? tuiText("tui_yes") : tuiText("tui_no"));
    lines << tuiText("tui_status_server")
        .arg(sanitize(setup.value(QStringLiteral("server_name")).toString(), 256),
             sanitize(connection.value(QStringLiteral("game_version")).toString(), 128),
             sanitize(connection.value(QStringLiteral("mod_name")).toString(), 128));
    lines << tuiText("tui_status_room")
        .arg(nameText(setup.value(QStringLiteral("game_mode")).toString()))
        .arg(setup.value(QStringLiteral("player_count")).toInt())
        .arg(connection.value(QStringLiteral("ready")).toBool()
                 ? tuiText("tui_yes") : tuiText("tui_no"),
             gameStatusText(game.value(QStringLiteral("status"),
                 QStringLiteral("waiting")).toString()));
    lines << tuiText("tui_status_self")
        .arg(playerLabel(state.selfName()),
             playerLabel(game.value(QStringLiteral("current_player")).toString()),
             nameText(game.value(QStringLiteral("current_phase")).toString()))
        .arg(game.value(QStringLiteral("round"), 0).toInt())
        .arg(game.value(QStringLiteral("draw_pile_count"), 0).toInt())
        .arg(game.value(QStringLiteral("discard_pile")).toList().size());
    if (game.value(QStringLiteral("game_over")).toBool()) {
        const QVariantMap result = game.value(QStringLiteral("result")).toMap();
        QStringList winners;
        for (const QString &token
             : stringList(result.value(QStringLiteral("winner_tokens")))) {
            winners << nameText(token);
        }
        lines << tuiText("tui_status_game_over")
            .arg(sanitize(winners.join(tuiText("tui_list_separator")), 512),
                 result.value(QStringLiteral("standoff")).toBool()
                     ? tuiText("tui_yes") : tuiText("tui_no"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderPlayers(const ClientGameState &state) const
{
    QStringList lines{heading(tuiText("tui_section_players"))};
    const QStringList names = state.playerNames();
    const QStringList focus = stringList(
        state.gameValue(QStringLiteral("focus")));
    const QString current = state.gameValue(QStringLiteral("current_player")).toString();
    for (int i = 0; i < names.size(); ++i) {
        const QVariantMap player = state.player(names.at(i));
        const QString name = names.at(i);
        // Flags are engine internals (CurrentPlayer, marshalling); the desktop
        // client never shows them either.
        const QString flags;
        const QList<int> equipment = state.cardsForPlayer(name, 1);
        const QList<int> judging = state.cardsForPlayer(name, 2);
        const QVariantMap piles = player.value(QStringLiteral("piles")).toMap();
        QStringList pileSummary;
        for (auto it = piles.constBegin(); it != piles.constEnd(); ++it)
            pileSummary << QStringLiteral("%1:%2").arg(sanitize(it.key(), 64))
                .arg(it.value().toList().size());
        // Only "@" marks are meant for players; the rest are engine bookkeeping
        // (turn counters, phase-clear helpers) that the desktop client also hides.
        QStringList marks;
        const QVariantMap markValues = player.value(QStringLiteral("marks")).toMap();
        for (auto it = markValues.constBegin(); it != markValues.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1Char('@')))
                continue;
            marks << QStringLiteral("%1=%2")
                .arg(nameText(it.key()), sanitize(it.value().toString(), 64));
        }
        const QString activity = name == current ? tuiText("tui_mark_turn")
            : focus.contains(name) ? tuiText("tui_mark_focus") : QString();
        const QString stateName = player.value(QStringLiteral("state")).toString();
        lines << tuiText("tui_player_line")
            .arg(i + 1).arg(player.value(QStringLiteral("seat")).toInt())
            .arg(sanitize(player.value(QStringLiteral("screen_name"), name).toString(), 128),
                 sanitize(name, 128), activity);
        const QString deputy = nameText(
            player.value(QStringLiteral("deputy_general")).toString());
        const QString generals = deputy.isEmpty()
            ? nameText(player.value(QStringLiteral("general")).toString())
            : tuiText("tui_hp_pair").arg(
                nameText(player.value(QStringLiteral("general")).toString()), deputy);
        lines << tuiText("tui_player_detail")
            .arg(generals, kingdomText(player))
            .arg(player.value(QStringLiteral("hp")).toInt())
            .arg(player.value(QStringLiteral("max_hp")).toInt())
            .arg(player.value(QStringLiteral("alive"), true).toBool()
                     ? tuiText("tui_alive") : tuiText("tui_dead"),
                 stateName.isEmpty() ? tuiText("tui_state_unknown") : nameText(stateName),
                 sanitize(player.value(QStringLiteral("role")).toString().isEmpty()
                         ? tuiText("tui_role_hidden")
                         : nameText(player.value(QStringLiteral("role")).toString()), 64))
            .arg(player.value(QStringLiteral("hand_count"),
                    state.cardsForPlayer(name, 0).size()).toInt());
        QStringList equipText;
        for (int cardId : equipment)
            equipText << QStringLiteral("%1:%2").arg(cardId)
                .arg(cardText(state, cardId));
        QStringList judgeText;
        for (int cardId : judging)
            judgeText << QStringLiteral("%1:%2").arg(cardId)
                .arg(cardText(state, cardId));
        lines << tuiText("tui_player_zones")
            .arg(sanitize(equipText.join(QStringLiteral(", ")), 1024),
                 sanitize(judgeText.join(QStringLiteral(", ")), 1024),
                 sanitize(pileSummary.join(QStringLiteral(", ")), 512),
                 sanitize(marks.join(QStringLiteral(", ")), 512),
                 flags.isEmpty() ? QString()
                     : tuiText("tui_player_flags").arg(sanitize(flags, 256)));
    }
    if (names.isEmpty())
        lines << tuiText("tui_waiting_players");
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderHand(const ClientGameState &state) const
{
    QStringList lines{heading(tuiText("tui_section_hand"))};
    const QString self = state.selfName();
    const QVariantList cards = state.toJson().value(QStringLiteral("cards")).toArray().toVariantList();
    int index = 0;
    for (const QVariant &entry : cards) {
        const QVariantMap card = entry.toMap();
        if (card.value(QStringLiteral("owner")).toString() != self
            || card.value(QStringLiteral("place")).toInt() != 0)
            continue;
        // This used to read game.available_cards, which nothing ever fills:
        // S_COMMAND_AVAILABLE_CARDS has handlers on both clients and no sender
        // anywhere, and the desktop's own available_cards is the whole draw
        // pile from GAME_START (used only to tell whether a game is on), so
        // even a populated one would have marked every card in hand. The
        // engine answers the question that was meant.
        const int cardId = card.value(QStringLiteral("id")).toInt();
        lines << tuiText("tui_hand_line")
            .arg(++index).arg(cardId)
            .arg(cardText(state, cardId),
                 m_resolvers.handHint ? sanitize(m_resolvers.handHint(cardId), 64) : QString());
    }
    if (index == 0)
        lines << tuiText("tui_hand_empty");
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderInteraction(const InteractionRequest &request) const
{
    QStringList lines{heading(interactionTitle(request))};
    if (!request.skillName.isEmpty())
        lines << tuiText("tui_prompt_skills").arg(nameText(request.skillName));
    if (!request.prompt.isEmpty()) {
        // The %src/%dest slots are object names, so they resolve to screen
        // names rather than through the lang table.
        const QString formatted = formatPrompt(request.prompt,
            [this](const QString &key) { return nameText(key); },
            [this](const QString &name) { return playerText(name); });
        lines << tuiText("tui_prompt_tip").arg(sanitize(plainText(formatted), 1024));
    }

    const int minimum = request.minSelection();
    const int maximum = request.maxSelection();
    if (request.responseSchema != InteractionResponseShape::Assignment
        && request.type != InteractionType::PlayCard
        && (minimum > 0 || maximum > 0)) {
        lines << tuiText("tui_prompt_selection_range").arg(minimum).arg(maximum);
    }
    if (request.timeoutMs > 0)
        lines << tuiText("tui_prompt_timeout").arg((request.timeoutMs + 999) / 1000);

    if (const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>()) {
        lines << tuiText("tui_prompt_role_assignment");
        QMap<QString, int> roleCounts;
        for (const QString &role : value->roles)
            roleCounts[role] += 1;
        if (!roleCounts.isEmpty()) {
            QStringList needed;
            for (auto it = roleCounts.constBegin(); it != roleCounts.constEnd(); ++it)
                needed << tuiText("tui_role_count").arg(nameText(it.key())).arg(it.value());
            lines << tuiText("tui_role_requirement").arg(needed.join(tuiText("tui_list_separator")));
        }
        lines << tuiText("tui_label_players");
        appendPlayers(&lines, value->playerNames,
                      [this](const QString &name) { return playerText(name); });
        QStringList uniqueRoles;
        for (const QString &role : value->roles) {
            if (!uniqueRoles.contains(role))
                uniqueRoles.append(role);
        }
        if (!uniqueRoles.isEmpty()) {
            lines << tuiText("tui_label_roles");
            for (const QString &role : uniqueRoles)
                lines << QStringLiteral("  %1 = %2").arg(role, nameText(role));
        }
        if (value->playerNames.isEmpty())
            lines << tuiText("tui_players_unknown");
    } else if (const auto *value = request.payloadAs<OptionInteractionPayload>()) {
        QList<InteractionOption> localized = value->options;
        for (InteractionOption &option : localized) {
            if (option.label.isEmpty() || option.label == option.value)
                option.label = nameText(option.value);
        }
        appendOptions(&lines, localized);
    } else if (const auto *value = request.payloadAs<ChooseOrderInteractionPayload>()) {
        appendOptions(&lines, value->options);
    } else if (const auto *value = request.payloadAs<PlayerInteractionPayload>()) {
        appendPlayers(&lines, value->selection.selectablePlayers,
                      [this](const QString &name) { return playerText(name); });
    } else if (const auto *value = request.payloadAs<CardInteractionPayload>()) {
        const bool playCard = request.type == InteractionType::PlayCard;
        const bool chooseCard = request.type == InteractionType::ChooseCard;
        // A response that is a use is the other prompt where the desktop keeps
        // target selection live (RoomScene::updateStatus turns MethodUse into
        // RespondingUse, and enableTargets() runs there). Every other response
        // takes a card and nothing else, so it gets no target list at all.
        const bool respondingUse = request.type == InteractionType::ResponseCard
            && value->selection.handlingMethod == Card::MethodUse;
        if (chooseCard && !value->sourcePlayer.isEmpty()) {
            lines << tuiText("tui_label_target")
                .arg(sanitize(playerText(value->sourcePlayer), 128));
        }
        if (chooseCard && value->hiddenHandCount > 0) {
            lines << tuiText("tui_hidden_hand_header").arg(value->hiddenHandCount);
            for (int i = 0; i < value->hiddenHandCount; ++i)
                lines << tuiText("tui_hidden_hand_line").arg(i + 1);
        }
        if (playCard)
            lines << tuiText("tui_label_choices");
        if (!value->selection.selectableCards.isEmpty()) {
            if (chooseCard && value->hiddenHandCount > 0)
                lines << tuiText("tui_label_visible");
            appendCards(&lines, value->selection.selectableCards,
                        value->selection.disabledCards, m_resolvers.card,
                        playCard || request.type == InteractionType::ResponseCard
                            || request.type == InteractionType::AskPeach
                            || request.type == InteractionType::Nullification
                            || request.type == InteractionType::DiscardCard
                            ? m_resolvers.cardHint : TuiRenderer::CardHintResolver(),
                        chooseCard ? value->hiddenHandCount + 1 : 1,
                        playCard || respondingUse ? m_resolvers.cardTargets
                                                  : TuiRenderer::CardTargetResolver(),
                        value->optionalTargets);
        }
        // Skills are numbered after the cards wherever the prompt offers any:
        // answering a slash with Wusheng is as much an activation as playing it.
        if (!value->skillCandidates.isEmpty()) {
            const int skillStart = value->selection.selectableCards.size() + 1;
            for (int i = 0; i < value->skillCandidates.size(); ++i) {
                const SkillActivationCandidate &skill = value->skillCandidates.at(i);
                const QString shown = nameText(skill.skillName);
                // The instance id is not the number to type -- that is the menu
                // index -- so printing it beside every skill just puts two
                // numbers on the line and invites the wrong one. It earns its
                // place only when one skill is offered more than once.
                int sameName = 0;
                for (const SkillActivationCandidate &other : value->skillCandidates) {
                    if (other.skillName == skill.skillName)
                        ++sameName;
                }
                const QString hint = m_resolvers.skillHint
                    ? m_resolvers.skillHint(skill.skillName, skill.instanceId) : QString();
                const QString mark = hint.isEmpty()
                    ? QString() : tuiText("tui_skill_hint_suffix").arg(sanitize(hint, 64));
                lines << (skill.instanceId > 0 && sameName > 1
                    ? tuiText("tui_skill_line_instance").arg(skillStart + i)
                        .arg(sanitize(shown, 128)).arg(skill.instanceId).arg(mark)
                    : tuiText("tui_skill_line").arg(skillStart + i)
                        .arg(sanitize(shown, 128), mark));
            }
        }
        if (playCard && value->selection.selectableCards.isEmpty()
            && value->skillCandidates.isEmpty()) {
            lines << tuiText("tui_hand_list_missing");
        }
        if (!value->fixedTargets.isEmpty()) {
            lines << tuiText("tui_label_fixed_targets");
            appendPlayers(&lines, value->fixedTargets,
                          [this](const QString &name) { return playerText(name); },
                          m_resolvers.playerHint);
        }
        if (!value->optionalTargets.isEmpty()
            && (respondingUse || request.type != InteractionType::ResponseCard)) {
            lines << (request.type == InteractionType::PlayCard
                ? tuiText("tui_label_play_targets") : tuiText("tui_label_optional_targets"));
            appendPlayers(&lines, value->optionalTargets,
                          [this](const QString &name) { return playerText(name); },
                          m_resolvers.playerHint);
        }
        if (!value->selection.pattern.isEmpty()
            && value->selection.pattern != QLatin1String(".")) {
            lines << tuiText("tui_label_pattern").arg(sanitize(value->selection.pattern, 256));
        }
    } else if (const auto *value = request.payloadAs<GongxinInteractionPayload>()) {
        QList<int> disabled;
        for (int cardId : value->visibleCards) {
            if (!value->selectableCards.contains(cardId))
                disabled.append(cardId);
        }
        appendCards(&lines, value->visibleCards, disabled, m_resolvers.card, {});
        lines << tuiText("tui_label_choices_inline").arg(joinIntegers(value->selectableCards));
    } else if (const auto *value = request.payloadAs<YijiInteractionPayload>()) {
        appendCards(&lines, value->cardIds, {}, m_resolvers.card, {});
        lines << tuiText("tui_label_recipients");
        appendPlayers(&lines, value->targetPlayers,
                      [this](const QString &name) { return playerText(name); });
    } else if (const auto *value = request.payloadAs<RearrangeCardsInteractionPayload>()) {
        appendCards(&lines, value->cardIds, {}, m_resolvers.card, {});
    } else if (const auto *value = request.payloadAs<AmazingGraceInteractionPayload>()) {
        appendCards(&lines, value->selection.selectableCards,
                    value->selection.disabledCards, m_resolvers.card, {});
    } else if (const auto *value = request.payloadAs<ArrangeGeneralsInteractionPayload>()) {
        for (int i = 0; i < value->generalNames.size(); ++i)
            lines << QStringLiteral("  [%1] %2").arg(i + 1)
                .arg(nameText(value->generalNames.at(i)));
    } else if (const auto *value = request.payloadAs<TriggerOrderInteractionPayload>()) {
        for (int i = 0; i < value->options.size(); ++i)
            lines << QStringLiteral("  [%1] %2 (#%3)").arg(i + 1)
                .arg(nameText(value->options.at(i).skillName))
                .arg(value->options.at(i).instanceId);
    } else if (const auto *value = request.payloadAs<CustomInteractionPayload>()) {
        lines << tuiText("tui_label_custom_type").arg(sanitize(value->typeName, 128));
    }
    const QString hint = answerHint(request);
    if (!hint.isEmpty())
        lines << hint;
    if (request.cancelable && request.type != InteractionType::PlayCard)
        lines << tuiText("tui_cancel_hint");
    lines << QStringLiteral("> ");
    return lines.join(QLatin1Char('\n'));
}
