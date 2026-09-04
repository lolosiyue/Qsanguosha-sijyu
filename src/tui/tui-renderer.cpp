#include "tui-renderer.h"

#include "client-prompt.h"
#include "core/client-game-state.h"
#include "core/interaction-model.h"
#include "protocol.h"

#include <QCoreApplication>
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

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

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
                 option.enabled ? QString() : tr("（禁用）")));
    }
}

// Turns the engine's opinion about a card's targets into the tail of its menu
// line, numbered the way the target list below is numbered.
QString targetNote(const TuiRenderer::CardTargets &advice, const QStringList &targetOrder)
{
    if (!advice.known)
        return QString();
    if (advice.targetFixed)
        return tr("  无需选目标");
    if (advice.targets.isEmpty())
        return tr("  无可选目标");
    QStringList numbers;
    for (const QString &name : advice.targets) {
        const qsizetype index = targetOrder.indexOf(name);
        // A legal target the prompt did not offer is the server's call, not
        // ours; name it rather than dropping it.
        numbers << (index >= 0 ? QStringLiteral("[%1]").arg(index + 1)
                               : TuiRenderer::sanitize(name, 64));
    }
    return tr("  可选目标：%1").arg(numbers.join(QString()));
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
                                         : tr("牌 %1").arg(cardId);
        QString note = disabled.contains(cardId) ? tr("（禁用）") : QString();
        const bool serverDisabled = !note.isEmpty();
        if (note.isEmpty() && hint)
            note = TuiRenderer::sanitize(hint(cardId), 64);
        // Only worth asking where the card could go if it can be played at all.
        if (!serverDisabled && note.isEmpty() && targets)
            note += targetNote(targets(cardId), targetOrder);
        lines->append(tr("  [%1] %2（ID=%3）%4").arg(startIndex + i).arg(display).arg(cardId)
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
    static const QHash<int, QString> labels{
        {QSanProtocol::S_COMMAND_SPEAK, tr("聊天")},
        {QSanProtocol::S_COMMAND_TRUST, tr("托管")},
        {QSanProtocol::S_COMMAND_ADD_ROBOT, tr("加入电脑玩家")},
        {QSanProtocol::S_COMMAND_SURRENDER, tr("投降")}};
    const QString label = labels.value(command);
    const QString detail = sanitize(message, 512);
    if (!success) {
        const QString what = label.isEmpty()
            ? tr("命令 %1").arg(command) : label;
        return detail.isEmpty() ? tr("%1失败").arg(what)
                                : tr("%1失败：%2").arg(what, detail);
    }
    // Delay probes and other housekeeping succeed constantly; only confirm what
    // the player actually asked for.
    if (label.isEmpty())
        return QString();
    return detail.isEmpty() ? tr("%1完成").arg(label)
                            : tr("%1完成：%2").arg(label, detail);
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
    return tr("牌 %1").arg(cardId);
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
    static const QHash<QString, QString> labels{
        {QStringLiteral("waiting"), tr("等待中")},
        {QStringLiteral("active"), tr("进行中")},
        {QStringLiteral("game_over"), tr("已结束")}};
    const auto fixed = labels.constFind(status.toLower());
    return fixed != labels.cend() ? fixed.value() : nameText(status);
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
    return kingdom.isEmpty() ? tr("未知") : nameText(kingdom);
}

QString TuiRenderer::playerLabel(const QString &objectName) const
{
    const QString shown = playerText(objectName);
    return shown == objectName ? sanitize(objectName, 128)
                               : tr("%1（%2）").arg(sanitize(shown, 128),
                                                   sanitize(objectName, 128));
}

QString TuiRenderer::nameText(const QString &name) const
{
    if (name.isEmpty())
        return name;
    static const QHash<QString, QString> labels{
        {QStringLiteral("idle"), tr("闲置")},
        {QStringLiteral("connecting"), tr("连接中")},
        {QStringLiteral("reconnecting"), tr("重连中")},
        {QStringLiteral("handshake"), tr("握手中")},
        {QStringLiteral("active"), tr("已连接")},
        {QStringLiteral("disconnected"), tr("已中断")},
        {QStringLiteral("failed"), tr("失败")},
        {QStringLiteral("waiting"), tr("等待中")},
        {QStringLiteral("online"), tr("上线")},
        {QStringLiteral("offline"), tr("离线")},
        {QStringLiteral("robot"), tr("机器人")},
        {QStringLiteral("trust"), tr("托管")},
        {QStringLiteral("not_active"), tr("未行动")},
        {QStringLiteral("round_start"), tr("回合开始")},
        {QStringLiteral("start"), tr("准备阶段")},
        {QStringLiteral("judge"), tr("判定阶段")},
        {QStringLiteral("draw"), tr("摸牌阶段")},
        {QStringLiteral("play"), tr("出牌阶段")},
        {QStringLiteral("discard"), tr("弃牌阶段")},
        {QStringLiteral("finish"), tr("结束阶段")}};
    const auto fixed = labels.constFind(name.toLower());
    if (fixed != labels.cend())
        return fixed.value();
    return sanitize(m_resolvers.name ? m_resolvers.name(name) : name, 256);
}

QString TuiRenderer::interactionTitle(const InteractionRequest &request) const
{
    switch (request.type) {
    case InteractionType::ChooseRole:
        return tr("分配身份");
    case InteractionType::ChooseGeneral:
    case InteractionType::AskGeneral:
        return tr("选择武将");
    case InteractionType::ChooseDirection:
        return tr("选择座次方向");
    case InteractionType::PlayCard:
        return tr("出牌");
    case InteractionType::ResponseCard:
        return tr("打出牌");
    case InteractionType::DiscardCard:
        return tr("弃牌");
    case InteractionType::ExchangeCard:
        return tr("换牌");
    case InteractionType::AskPeach:
        return tr("求桃");
    case InteractionType::Nullification:
        return tr("无懈可击");
    case InteractionType::Choice:
        return tr("选择");
    case InteractionType::SkillInvoke:
        return tr("发动技能");
    case InteractionType::ChoosePlayer:
        return tr("选择角色");
    case InteractionType::ChooseCard:
        return tr("选择卡牌");
    case InteractionType::ChooseSuit:
        return tr("选择花色");
    case InteractionType::ChooseKingdom:
        return tr("选择势力");
    case InteractionType::AmazingGrace:
        return tr("五谷丰登");
    case InteractionType::SkillGuanxing:
        return tr("观星");
    case InteractionType::SkillGongxin:
        return tr("攻心");
    case InteractionType::SkillYiji:
        return tr("遗计");
    case InteractionType::Pindian:
        return tr("拼点");
    case InteractionType::TriggerOrder:
        return tr("技能发动顺序");
    case InteractionType::ArrangeGeneral:
        return tr("排列武将");
    case InteractionType::LuckCard:
        return tr("手气卡");
    case InteractionType::Surrender:
        return tr("投降");
    default:
        return tr("互动：%1").arg(interactionTypeName(request.type));
    }
}

QString TuiRenderer::answerHint(const InteractionRequest &request) const
{
    switch (request.responseSchema) {
    case InteractionResponseShape::Assignment: {
        const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>();
        if (value == nullptr || value->playerNames.isEmpty())
            return tr("作答：<玩家>=<身份>  例如 1=主公 2=反贼");
        QStringList parts;
        for (int i = 0; i < value->playerNames.size(); ++i) {
            const QString role = (value->roles.size() == value->playerNames.size())
                ? nameText(value->roles.at(i)) : QStringLiteral("?");
            parts << QStringLiteral("%1=%2").arg(i + 1).arg(role);
        }
        return tr("作答：每位玩家写 <编号>=<身份>，同一行以空白隔开\n示例：%1")
            .arg(parts.join(QLatin1Char(' ')));
    }
    case InteractionResponseShape::Option:
        return tr("作答：输入选项编号，或括号后的原文（例如 1 或 lord）");
    case InteractionResponseShape::Players:
        return tr("作答：输入玩家编号，可多选（例如 2 或 1 3）");
    case InteractionResponseShape::Cards:
        if (request.type == InteractionType::PlayCard) {
            return tr("作答：<编号> -> <目标编号>   例如 1 -> 2\n"
                      "技能牌：先写 <技能编号> <手牌编号>，再按提示选目标   例如 8 1\n"
                      "无目标只写编号或技能编号。结束出牌：pass 或 /cancel");
        }
        if (request.type == InteractionType::ChooseCard) {
            const auto *value = request.payloadAs<CardInteractionPayload>();
            if (value != nullptr && value->hiddenHandCount > 0) {
                return tr("作答：输入编号（例如 1）\n"
                          "未知手牌选任一张即可，无需知道牌面");
            }
            return tr("作答：输入牌编号（例如 1）");
        }
        if (request.type == InteractionType::DiscardCard
            || request.type == InteractionType::ExchangeCard) {
            return tr("作答：输入牌编号，可多选（例如 1 3 或 1-3）");
        }
        if (request.type == InteractionType::AskPeach) {
            return tr("作答：输入桃的编号（例如 1）");
        }
        return tr("作答：输入牌编号（例如 1）");
    case InteractionResponseShape::Rearrangement:
        return tr("作答：<放回牌堆顶的编号> | <放回牌堆底的编号>");
    case InteractionResponseShape::Distribution:
        return tr("作答：cards <牌编号> -> <玩家编号>");
    case InteractionResponseShape::GeneralArrangement:
        return tr("作答：依序输入武将编号");
    case InteractionResponseShape::Custom:
        return tr("作答：一个 JSON 对象或数组");
    default:
        return QString();
    }
}

QString TuiRenderer::renderState(const ClientGameState &state) const
{
    const QVariantMap connection = state.connection();
    const QVariantMap setup = state.setup();
    const QVariantMap game = state.game();
    QStringList lines{heading(tr("QSanguosha 终端客户端"))};
    lines << tr("连接：%1  %2:%3  延迟=%4ms  重连=%5")
        .arg(nameText(connection.value(QStringLiteral("state"), QStringLiteral("idle")).toString()),
             connection.value(QStringLiteral("host")).toString(),
             connection.value(QStringLiteral("port")).toString(),
             connection.contains(QStringLiteral("latency_ms"))
                 ? connection.value(QStringLiteral("latency_ms")).toString()
                 : QStringLiteral("?"),
             connection.value(QStringLiteral("reconnected")).toBool()
                 ? tr("是") : tr("否"));
    lines << tr("服务器：%1  版本=%2  MOD=%3")
        .arg(sanitize(setup.value(QStringLiteral("server_name")).toString(), 256),
             sanitize(connection.value(QStringLiteral("game_version")).toString(), 128),
             sanitize(connection.value(QStringLiteral("mod_name")).toString(), 128));
    lines << tr("房间：模式=%1 玩家=%2 就绪=%3 状态=%4")
        .arg(nameText(setup.value(QStringLiteral("game_mode")).toString()))
        .arg(setup.value(QStringLiteral("player_count")).toInt())
        .arg(connection.value(QStringLiteral("ready")).toBool()
                 ? tr("是") : tr("否"),
             gameStatusText(game.value(QStringLiteral("status"),
                 QStringLiteral("waiting")).toString()));
    lines << tr("自己：%1  当前玩家：%2  阶段：%3  轮次：%4  牌堆：%5  弃牌：%6")
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
        lines << tr("游戏结束：胜方=%1 平局=%2")
            .arg(sanitize(winners.join(tr("、")), 512),
                 result.value(QStringLiteral("standoff")).toBool()
                     ? tr("是") : tr("否"));
    }
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderPlayers(const ClientGameState &state) const
{
    QStringList lines{heading(tr("玩家"))};
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
        const QString activity = name == current ? tr(" <回合>")
            : focus.contains(name) ? tr(" <焦点>") : QString();
        const QString stateName = player.value(QStringLiteral("state")).toString();
        lines << tr("[%1] 座位=%2 %3（%4）%5")
            .arg(i + 1).arg(player.value(QStringLiteral("seat")).toInt())
            .arg(sanitize(player.value(QStringLiteral("screen_name"), name).toString(), 128),
                 sanitize(name, 128), activity);
        const QString deputy = nameText(
            player.value(QStringLiteral("deputy_general")).toString());
        const QString generals = deputy.isEmpty()
            ? nameText(player.value(QStringLiteral("general")).toString())
            : tr("%1/%2").arg(
                nameText(player.value(QStringLiteral("general")).toString()), deputy);
        lines << tr("    武将=%1 势力=%2 体力=%3/%4 生存=%5 网络=%6 身份=%7 手牌=%8")
            .arg(generals, kingdomText(player))
            .arg(player.value(QStringLiteral("hp")).toInt())
            .arg(player.value(QStringLiteral("max_hp")).toInt())
            .arg(player.value(QStringLiteral("alive"), true).toBool()
                     ? tr("存活") : tr("死亡"),
                 stateName.isEmpty() ? tr("未知") : nameText(stateName),
                 sanitize(player.value(QStringLiteral("role")).toString().isEmpty()
                         ? tr("隐藏")
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
        lines << tr("    装备=[%1] 判定=[%2] 私有牌堆=[%3] 标记=[%4]%5")
            .arg(sanitize(equipText.join(QStringLiteral(", ")), 1024),
                 sanitize(judgeText.join(QStringLiteral(", ")), 1024),
                 sanitize(pileSummary.join(QStringLiteral(", ")), 512),
                 sanitize(marks.join(QStringLiteral(", ")), 512),
                 flags.isEmpty() ? QString()
                     : tr(" 标志=[%1]").arg(sanitize(flags, 256)));
    }
    if (names.isEmpty())
        lines << tr("（等待玩家）");
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderHand(const ClientGameState &state) const
{
    QStringList lines{heading(tr("手牌"))};
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
        lines << tr("[%1] ID=%2 %3%4")
            .arg(++index).arg(cardId)
            .arg(cardText(state, cardId),
                 m_resolvers.handHint ? sanitize(m_resolvers.handHint(cardId), 64) : QString());
    }
    if (index == 0)
        lines << tr("（没有可见的手牌）");
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderInteraction(const InteractionRequest &request) const
{
    QStringList lines{heading(interactionTitle(request))};
    if (!request.skillName.isEmpty())
        lines << tr("技能：%1").arg(nameText(request.skillName));
    if (!request.prompt.isEmpty()) {
        // The %src/%dest slots are object names, so they resolve to screen
        // names rather than through the lang table.
        const QString formatted = formatPrompt(request.prompt,
            [this](const QString &key) { return nameText(key); },
            [this](const QString &name) { return playerText(name); });
        lines << tr("提示：%1").arg(sanitize(plainText(formatted), 1024));
    }

    const int minimum = request.minSelection();
    const int maximum = request.maxSelection();
    if (request.responseSchema != InteractionResponseShape::Assignment
        && request.type != InteractionType::PlayCard
        && (minimum > 0 || maximum > 0)) {
        lines << tr("须选 %1 至 %2 项").arg(minimum).arg(maximum);
    }
    if (request.timeoutMs > 0)
        lines << tr("限时 %1 秒").arg((request.timeoutMs + 999) / 1000);

    if (const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>()) {
        lines << tr("为每位玩家指定一个身份。");
        QMap<QString, int> roleCounts;
        for (const QString &role : value->roles)
            roleCounts[role] += 1;
        if (!roleCounts.isEmpty()) {
            QStringList needed;
            for (auto it = roleCounts.constBegin(); it != roleCounts.constEnd(); ++it)
                needed << tr("%1×%2").arg(nameText(it.key())).arg(it.value());
            lines << tr("本局需要：%1").arg(needed.join(tr("、")));
        }
        lines << tr("玩家：");
        appendPlayers(&lines, value->playerNames,
                      [this](const QString &name) { return playerText(name); });
        QStringList uniqueRoles;
        for (const QString &role : value->roles) {
            if (!uniqueRoles.contains(role))
                uniqueRoles.append(role);
        }
        if (!uniqueRoles.isEmpty()) {
            lines << tr("身份：");
            for (const QString &role : uniqueRoles)
                lines << QStringLiteral("  %1 = %2").arg(role, nameText(role));
        }
        if (value->playerNames.isEmpty())
            lines << tr("（尚未收到玩家名单）");
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
        if (chooseCard && !value->sourcePlayer.isEmpty()) {
            lines << tr("目标：%1")
                .arg(sanitize(playerText(value->sourcePlayer), 128));
        }
        if (chooseCard && value->hiddenHandCount > 0) {
            lines << tr("手牌（%1 张，牌面未知）：").arg(value->hiddenHandCount);
            for (int i = 0; i < value->hiddenHandCount; ++i)
                lines << tr("  [%1] 未知手牌").arg(i + 1);
        }
        if (playCard)
            lines << tr("可选：");
        if (!value->selection.selectableCards.isEmpty()) {
            if (chooseCard && value->hiddenHandCount > 0)
                lines << tr("可见：");
            appendCards(&lines, value->selection.selectableCards,
                        value->selection.disabledCards, m_resolvers.card,
                        playCard || request.type == InteractionType::ResponseCard
                            || request.type == InteractionType::AskPeach
                            || request.type == InteractionType::Nullification
                            || request.type == InteractionType::DiscardCard
                            ? m_resolvers.cardHint : TuiRenderer::CardHintResolver(),
                        chooseCard ? value->hiddenHandCount + 1 : 1,
                        playCard ? m_resolvers.cardTargets : TuiRenderer::CardTargetResolver(),
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
                    ? QString() : tr("，%1").arg(sanitize(hint, 64));
                lines << (skill.instanceId > 0 && sameName > 1
                    ? tr("  [%1] %2（技能 #%3%4）").arg(skillStart + i)
                        .arg(sanitize(shown, 128)).arg(skill.instanceId).arg(mark)
                    : tr("  [%1] %2（技能%3）").arg(skillStart + i)
                        .arg(sanitize(shown, 128), mark));
            }
        }
        if (playCard && value->selection.selectableCards.isEmpty()
            && value->skillCandidates.isEmpty()) {
            lines << tr("  （尚无手牌列表，可先打 /hand）");
        }
        if (!value->fixedTargets.isEmpty()) {
            lines << tr("固定目标：");
            appendPlayers(&lines, value->fixedTargets,
                          [this](const QString &name) { return playerText(name); },
                          m_resolvers.playerHint);
        }
        if (!value->optionalTargets.isEmpty()) {
            lines << (request.type == InteractionType::PlayCard
                ? tr("目标玩家：") : tr("可选目标："));
            appendPlayers(&lines, value->optionalTargets,
                          [this](const QString &name) { return playerText(name); },
                          m_resolvers.playerHint);
        }
        if (!value->selection.pattern.isEmpty()
            && value->selection.pattern != QLatin1String(".")) {
            lines << tr("模式：%1").arg(sanitize(value->selection.pattern, 256));
        }
    } else if (const auto *value = request.payloadAs<GongxinInteractionPayload>()) {
        QList<int> disabled;
        for (int cardId : value->visibleCards) {
            if (!value->selectableCards.contains(cardId))
                disabled.append(cardId);
        }
        appendCards(&lines, value->visibleCards, disabled, m_resolvers.card, {});
        lines << tr("可选：%1").arg(joinIntegers(value->selectableCards));
    } else if (const auto *value = request.payloadAs<YijiInteractionPayload>()) {
        appendCards(&lines, value->cardIds, {}, m_resolvers.card, {});
        lines << tr("接收者：");
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
        lines << tr("自定义类型：%1").arg(sanitize(value->typeName, 128));
    }
    const QString hint = answerHint(request);
    if (!hint.isEmpty())
        lines << hint;
    if (request.cancelable && request.type != InteractionType::PlayCard)
        lines << tr("可输入 /cancel 放弃");
    lines << QStringLiteral("> ");
    return lines.join(QLatin1Char('\n'));
}
