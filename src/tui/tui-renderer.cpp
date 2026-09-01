#include "tui-renderer.h"

#include "core/client-game-state.h"
#include "core/interaction-model.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QRegularExpression>
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
                 option.enabled ? QString() : tr("（停用）")));
    }
}

void appendCards(QStringList *lines, const QList<int> &cards, const QList<int> &disabled,
                 const TuiRenderer::CardResolver &resolver, int startIndex = 1)
{
    for (int i = 0; i < cards.size(); ++i) {
        const int cardId = cards.at(i);
        const QString display = resolver ? TuiRenderer::sanitize(resolver(cardId), 512)
                                         : tr("牌 %1").arg(cardId);
        lines->append(tr("  [%1] %2（ID=%3）%4").arg(startIndex + i).arg(display).arg(cardId)
            .arg(disabled.contains(cardId) ? tr("（停用）") : QString()));
    }
}

void appendPlayers(QStringList *lines, const QStringList &names,
                   const std::function<QString(const QString &)> &resolve)
{
    for (int i = 0; i < names.size(); ++i) {
        const QString objectName = names.at(i);
        const QString shown = resolve(objectName);
        lines->append(shown == objectName
            ? QStringLiteral("  [%1] %2").arg(i + 1).arg(TuiRenderer::sanitize(objectName, 128))
            : QStringLiteral("  [%1] %2（%3）").arg(i + 1)
                .arg(TuiRenderer::sanitize(shown, 128), TuiRenderer::sanitize(objectName, 128)));
    }
}

} // namespace

TuiRenderer::TuiRenderer(bool ansiEnabled, CardResolver cardResolver,
                         NameResolver nameResolver)
    : m_ansiEnabled(ansiEnabled), m_cardResolver(std::move(cardResolver)),
      m_nameResolver(std::move(nameResolver))
{
}

QString TuiRenderer::formatPrompt(const QString &prompt,
                                  const std::function<QString(const QString &)> &translate,
                                  const std::function<QString(const QString &)> &playerName)
{
    if (prompt.isEmpty())
        return prompt;
    const QStringList texts = prompt.split(QLatin1Char(':'));
    const QString key = texts.at(0);
    QString result = translate ? translate(key) : key;
    if (result == key && key.endsWith(QLatin1String("-jink"))) {
        result = texts.size() >= 3
            ? QStringLiteral("%src 使用了【%dest】，请打出一张【闪】")
            : QStringLiteral("%src 对你使用【杀】，你需使用【闪】抵消之");
    }
    const auto slotText = [&](const QString &token) {
        if (token.isEmpty())
            return token;
        return translate ? translate(token) : token;
    };
    static const QRegularExpression sgsName(
        QStringLiteral("^sgs\\d+$"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto slotPlayer = [&](const QString &token) {
        if (token.isEmpty())
            return token;
        if (sgsName.match(token).hasMatch() && playerName)
            return playerName(token);
        return slotText(token);
    };
    if (texts.size() >= 5)
        result.replace(QStringLiteral("%arg2"), slotText(texts.at(4)));
    if (texts.size() >= 4)
        result.replace(QStringLiteral("%arg"), slotText(texts.at(3)));
    if (texts.size() >= 3)
        result.replace(QStringLiteral("%dest"), slotPlayer(texts.at(2)));
    if (texts.size() >= 2)
        result.replace(QStringLiteral("%src"), slotPlayer(texts.at(1)));
    return result;
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

QString TuiRenderer::commandResultText(int command, bool success, const QString &message)
{
    static const QHash<int, QString> labels{
        {QSanProtocol::S_COMMAND_SPEAK, tr("聊天")},
        {QSanProtocol::S_COMMAND_TRUST, tr("託管")},
        {QSanProtocol::S_COMMAND_ADD_ROBOT, tr("加入電腦玩家")},
        {QSanProtocol::S_COMMAND_SURRENDER, tr("投降")}};
    const QString label = labels.value(command);
    const QString detail = sanitize(message, 512);
    if (!success) {
        const QString what = label.isEmpty()
            ? tr("命令 %1").arg(command) : label;
        return detail.isEmpty() ? tr("%1失敗").arg(what)
                                : tr("%1失敗：%2").arg(what, detail);
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
    if (m_cardResolver)
        return sanitize(m_cardResolver(cardId), 512);
    const QVariantMap card = state.card(cardId);
    const QString name = card.value(QStringLiteral("card_name")).toString();
    if (!name.isEmpty()) {
        return QStringLiteral("%1 %2 %3").arg(name,
            card.value(QStringLiteral("suit")).toString(),
            card.value(QStringLiteral("number")).toString()).trimmed();
    }
    return tr("牌 %1").arg(cardId);
}

QString TuiRenderer::nameText(const QString &name) const
{
    if (name.isEmpty())
        return name;
    static const QHash<QString, QString> labels{
        {QStringLiteral("idle"), tr("閒置")},
        {QStringLiteral("connecting"), tr("連線中")},
        {QStringLiteral("reconnecting"), tr("重連中")},
        {QStringLiteral("handshake"), tr("交握中")},
        {QStringLiteral("active"), tr("已連線")},
        {QStringLiteral("disconnected"), tr("已中斷")},
        {QStringLiteral("failed"), tr("失敗")},
        {QStringLiteral("waiting"), tr("等待中")},
        {QStringLiteral("online"), tr("上線")},
        {QStringLiteral("offline"), tr("離線")},
        {QStringLiteral("robot"), tr("機器人")},
        {QStringLiteral("trust"), tr("託管")},
        {QStringLiteral("not_active"), tr("未行動")},
        {QStringLiteral("round_start"), tr("回合開始")},
        {QStringLiteral("start"), tr("準備階段")},
        {QStringLiteral("judge"), tr("判定階段")},
        {QStringLiteral("draw"), tr("摸牌階段")},
        {QStringLiteral("play"), tr("出牌階段")},
        {QStringLiteral("discard"), tr("棄牌階段")},
        {QStringLiteral("finish"), tr("結束階段")}};
    const auto fixed = labels.constFind(name.toLower());
    if (fixed != labels.cend())
        return fixed.value();
    return sanitize(m_nameResolver ? m_nameResolver(name) : name, 256);
}

QString TuiRenderer::interactionTitle(const InteractionRequest &request) const
{
    switch (request.type) {
    case InteractionType::ChooseRole:
        return tr("分配身分");
    case InteractionType::ChooseGeneral:
    case InteractionType::AskGeneral:
        return tr("選擇武將");
    case InteractionType::ChooseDirection:
        return tr("選擇座次方向");
    case InteractionType::PlayCard:
        return tr("出牌");
    case InteractionType::ResponseCard:
        return tr("打出牌");
    case InteractionType::DiscardCard:
        return tr("棄牌");
    case InteractionType::ExchangeCard:
        return tr("換牌");
    case InteractionType::AskPeach:
        return tr("求桃");
    case InteractionType::Nullification:
        return tr("無懈可擊");
    case InteractionType::Choice:
        return tr("選擇");
    case InteractionType::SkillInvoke:
        return tr("發動技能");
    case InteractionType::ChoosePlayer:
        return tr("選擇角色");
    case InteractionType::ChooseCard:
        return tr("選擇卡牌");
    case InteractionType::ChooseSuit:
        return tr("選擇花色");
    case InteractionType::ChooseKingdom:
        return tr("選擇勢力");
    case InteractionType::AmazingGrace:
        return tr("五穀豐登");
    case InteractionType::SkillGuanxing:
        return tr("觀星");
    case InteractionType::SkillGongxin:
        return tr("攻心");
    case InteractionType::SkillYiji:
        return tr("遺計");
    case InteractionType::Pindian:
        return tr("拼點");
    case InteractionType::TriggerOrder:
        return tr("技能發動順序");
    case InteractionType::ArrangeGeneral:
        return tr("排列武將");
    case InteractionType::LuckCard:
        return tr("手氣卡");
    case InteractionType::Surrender:
        return tr("投降");
    default:
        return tr("互動：%1").arg(interactionTypeName(request.type));
    }
}

QString TuiRenderer::answerHint(const InteractionRequest &request) const
{
    switch (request.responseSchema) {
    case InteractionResponseShape::Assignment: {
        const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>();
        if (value == nullptr || value->playerNames.isEmpty())
            return tr("作答：<玩家>=<身分>  例如 1=主公 2=反賊");
        QStringList parts;
        for (int i = 0; i < value->playerNames.size(); ++i) {
            const QString role = (value->roles.size() == value->playerNames.size())
                ? nameText(value->roles.at(i)) : QStringLiteral("?");
            parts << QStringLiteral("%1=%2").arg(i + 1).arg(role);
        }
        return tr("作答：每位玩家寫 <編號>=<身分>，同一行以空白隔開\n範例：%1")
            .arg(parts.join(QLatin1Char(' ')));
    }
    case InteractionResponseShape::Option:
        return tr("作答：輸入選項編號，或括號後的原文（例如 1 或 lord）");
    case InteractionResponseShape::Players:
        return tr("作答：輸入玩家編號，可多選（例如 2 或 1 3）");
    case InteractionResponseShape::Cards:
        if (request.type == InteractionType::PlayCard) {
            return tr("作答：<編號> -> <目標編號>   例如 1 -> 2\n"
                      "技能＋手牌＋目標：<技能編號> <手牌編號> -> <目標編號>   例如 4 1 -> 2\n"
                      "無目標只寫編號或技能編號。結束出牌：pass 或 /cancel");
        }
        if (request.type == InteractionType::ChooseCard) {
            const auto *value = request.payloadAs<CardInteractionPayload>();
            if (value != nullptr && value->hiddenHandCount > 0) {
                return tr("作答：輸入編號（例如 1）\n"
                          "未知手牌選任一張即可，無需知道牌面");
            }
            return tr("作答：輸入牌編號（例如 1）");
        }
        if (request.type == InteractionType::DiscardCard
            || request.type == InteractionType::ExchangeCard) {
            return tr("作答：輸入牌編號，可多選（例如 1 3 或 1-3）");
        }
        if (request.type == InteractionType::AskPeach) {
            return tr("作答：輸入桃的編號（例如 1）");
        }
        return tr("作答：輸入牌編號（例如 1）");
    case InteractionResponseShape::Rearrangement:
        return tr("作答：<放回牌堆頂的編號> | <放回牌堆底的編號>");
    case InteractionResponseShape::Distribution:
        return tr("作答：cards <牌編號> -> <玩家編號>");
    case InteractionResponseShape::GeneralArrangement:
        return tr("作答：依序輸入武將編號");
    case InteractionResponseShape::Custom:
        return tr("作答：一個 JSON 物件或陣列");
    default:
        return QString();
    }
}

QString TuiRenderer::renderState(const ClientGameState &state) const
{
    const QVariantMap connection = state.connection();
    const QVariantMap setup = state.setup();
    const QVariantMap game = state.game();
    QStringList lines{heading(tr("QSanguosha 終端客戶端"))};
    lines << tr("連線：%1  %2:%3  延遲=%4ms  重連=%5")
        .arg(nameText(connection.value(QStringLiteral("state"), QStringLiteral("idle")).toString()),
             connection.value(QStringLiteral("host")).toString(),
             connection.value(QStringLiteral("port")).toString(),
             connection.contains(QStringLiteral("latency_ms"))
                 ? connection.value(QStringLiteral("latency_ms")).toString()
                 : QStringLiteral("?"),
             connection.value(QStringLiteral("reconnected")).toBool()
                 ? tr("是") : tr("否"));
    lines << tr("伺服器：%1  版本=%2  MOD=%3")
        .arg(sanitize(setup.value(QStringLiteral("server_name")).toString(), 256),
             sanitize(connection.value(QStringLiteral("game_version")).toString(), 128),
             sanitize(connection.value(QStringLiteral("mod_name")).toString(), 128));
    lines << tr("房間：模式=%1 玩家=%2 就緒=%3 狀態=%4")
        .arg(nameText(setup.value(QStringLiteral("game_mode")).toString()))
        .arg(setup.value(QStringLiteral("player_count")).toInt())
        .arg(connection.value(QStringLiteral("ready")).toBool()
                 ? tr("是") : tr("否"),
             nameText(game.value(QStringLiteral("status"),
                 QStringLiteral("waiting")).toString()));
    lines << tr("自己：%1  當前玩家：%2  階段：%3  輪次：%4  牌堆：%5  棄牌：%6")
        .arg(sanitize(state.selfName(), 128),
             sanitize(game.value(QStringLiteral("current_player")).toString(), 128),
             nameText(game.value(QStringLiteral("current_phase")).toString()))
        .arg(game.value(QStringLiteral("round"), 0).toInt())
        .arg(game.value(QStringLiteral("draw_pile_count"), 0).toInt())
        .arg(game.value(QStringLiteral("discard_pile")).toList().size());
    if (game.value(QStringLiteral("game_over")).toBool()) {
        const QVariantMap result = game.value(QStringLiteral("result")).toMap();
        lines << tr("遊戲結束：勝方=%1 平局=%2")
            .arg(sanitize(stringList(result.value(QStringLiteral("winner_tokens")))
                              .join(QLatin1Char(',')), 512),
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
            : focus.contains(name) ? tr(" <焦點>") : QString();
        const QString stateName = player.value(QStringLiteral("state")).toString();
        lines << tr("[%1] 座位=%2 %3（%4）%5")
            .arg(i + 1).arg(player.value(QStringLiteral("seat")).toInt())
            .arg(sanitize(player.value(QStringLiteral("screen_name"), name).toString(), 128),
                 sanitize(name, 128), activity);
        lines << tr("    武將=%1/%2 勢力=%3 體力=%4/%5 生存=%6 網路=%7 身分=%8 手牌=%9")
            .arg(nameText(player.value(QStringLiteral("general")).toString()),
                 nameText(player.value(QStringLiteral("deputy_general")).toString()),
                 nameText(player.value(QStringLiteral("kingdom")).toString()))
            .arg(player.value(QStringLiteral("hp")).toInt())
            .arg(player.value(QStringLiteral("max_hp")).toInt())
            .arg(player.value(QStringLiteral("alive"), true).toBool()
                     ? tr("存活") : tr("死亡"),
                 stateName.isEmpty() ? tr("未知") : nameText(stateName),
                 sanitize(player.value(QStringLiteral("role")).toString().isEmpty()
                         ? tr("隱藏")
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
        lines << tr("    裝備=[%1] 判定=[%2] 私有牌堆=[%3] 標記=[%4]%5")
            .arg(sanitize(equipText.join(QStringLiteral(", ")), 1024),
                 sanitize(judgeText.join(QStringLiteral(", ")), 1024),
                 sanitize(pileSummary.join(QStringLiteral(", ")), 512),
                 sanitize(marks.join(QStringLiteral(", ")), 512),
                 flags.isEmpty() ? QString()
                     : tr(" 旗標=[%1]").arg(sanitize(flags, 256)));
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
        lines << tr("[%1] ID=%2 %3%4")
            .arg(++index).arg(card.value(QStringLiteral("id")).toInt())
            .arg(cardText(state, card.value(QStringLiteral("id")).toInt()),
                 state.gameValue(QStringLiteral("available_cards")).toList().contains(
                     card.value(QStringLiteral("id")))
                     ? tr(" 可用") : QString());
    }
    if (index == 0)
        lines << tr("（沒有可見的手牌身分）");
    return lines.join(QLatin1Char('\n'));
}

QString TuiRenderer::renderInteraction(const InteractionRequest &request) const
{
    QStringList lines{heading(interactionTitle(request))};
    if (!request.skillName.isEmpty())
        lines << tr("技能：%1").arg(nameText(request.skillName));
    if (!request.prompt.isEmpty()) {
        const QString formatted = formatPrompt(request.prompt,
            [this](const QString &key) { return nameText(key); },
            [this](const QString &name) { return nameText(name); });
        lines << tr("提示：%1").arg(sanitize(formatted, 1024));
    }

    const int minimum = request.minSelection();
    const int maximum = request.maxSelection();
    if (request.responseSchema != InteractionResponseShape::Assignment
        && request.type != InteractionType::PlayCard
        && (minimum > 0 || maximum > 0)) {
        lines << tr("須選 %1 至 %2 項").arg(minimum).arg(maximum);
    }
    if (request.timeoutMs > 0)
        lines << tr("限時 %1 秒").arg((request.timeoutMs + 999) / 1000);

    if (const auto *value = request.payloadAs<RoleAssignmentInteractionPayload>()) {
        lines << tr("為每位玩家指定一個身分。");
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
        for (int i = 0; i < value->playerNames.size(); ++i) {
            const QString objectName = value->playerNames.at(i);
            const QString shown = nameText(objectName);
            lines << (shown == objectName
                ? QStringLiteral("  [%1] %2").arg(i + 1).arg(sanitize(objectName, 128))
                : QStringLiteral("  [%1] %2（%3）").arg(i + 1)
                    .arg(sanitize(shown, 128), sanitize(objectName, 128)));
        }
        QStringList uniqueRoles;
        for (const QString &role : value->roles) {
            if (!uniqueRoles.contains(role))
                uniqueRoles.append(role);
        }
        if (!uniqueRoles.isEmpty()) {
            lines << tr("身分：");
            for (const QString &role : uniqueRoles)
                lines << QStringLiteral("  %1 = %2").arg(role, nameText(role));
        }
        if (value->playerNames.isEmpty())
            lines << tr("（尚未收到玩家名單）");
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
        for (int i = 0; i < value->selection.selectablePlayers.size(); ++i)
            lines << QStringLiteral("  [%1] %2").arg(i + 1)
                .arg(sanitize(nameText(value->selection.selectablePlayers.at(i)), 128));
    } else if (const auto *value = request.payloadAs<CardInteractionPayload>()) {
        const bool playCard = request.type == InteractionType::PlayCard;
        const bool chooseCard = request.type == InteractionType::ChooseCard;
        if (chooseCard && !value->sourcePlayer.isEmpty()) {
            lines << tr("目標：%1")
                .arg(sanitize(nameText(value->sourcePlayer), 128));
        }
        if (chooseCard && value->hiddenHandCount > 0) {
            lines << tr("手牌（%1 張，牌面未知）：").arg(value->hiddenHandCount);
            for (int i = 0; i < value->hiddenHandCount; ++i)
                lines << tr("  [%1] 未知手牌").arg(i + 1);
        }
        if (playCard)
            lines << tr("可選：");
        if (!value->selection.selectableCards.isEmpty()) {
            if (chooseCard && value->hiddenHandCount > 0)
                lines << tr("可見：");
            appendCards(&lines, value->selection.selectableCards,
                        value->selection.disabledCards, m_cardResolver,
                        chooseCard ? value->hiddenHandCount + 1 : 1);
        }
        if (playCard) {
            const int skillStart = value->selection.selectableCards.size() + 1;
            for (int i = 0; i < value->skillCandidates.size(); ++i) {
                const SkillActivationCandidate &skill = value->skillCandidates.at(i);
                const QString shown = nameText(skill.skillName);
                lines << (skill.instanceId > 0
                    ? tr("  [%1] %2（技能 #%3）").arg(skillStart + i)
                        .arg(sanitize(shown, 128)).arg(skill.instanceId)
                    : tr("  [%1] %2（技能）").arg(skillStart + i)
                        .arg(sanitize(shown, 128)));
            }
            if (value->selection.selectableCards.isEmpty()
                && value->skillCandidates.isEmpty()) {
                lines << tr("  （尚無手牌清單，可先打 /hand）");
            }
        }
        if (!value->fixedTargets.isEmpty()) {
            lines << tr("固定目標：");
            appendPlayers(&lines, value->fixedTargets,
                          [this](const QString &name) { return nameText(name); });
        }
        if (!value->optionalTargets.isEmpty()) {
            lines << (request.type == InteractionType::PlayCard
                ? tr("目標玩家：") : tr("可選目標："));
            appendPlayers(&lines, value->optionalTargets,
                          [this](const QString &name) { return nameText(name); });
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
        appendCards(&lines, value->visibleCards, disabled, m_cardResolver);
        lines << tr("可選：%1").arg(joinIntegers(value->selectableCards));
    } else if (const auto *value = request.payloadAs<YijiInteractionPayload>()) {
        appendCards(&lines, value->cardIds, {}, m_cardResolver);
        lines << tr("接收者：%1").arg(value->targetPlayers.join(QLatin1Char(' ')));
    } else if (const auto *value = request.payloadAs<RearrangeCardsInteractionPayload>()) {
        appendCards(&lines, value->cardIds, {}, m_cardResolver);
    } else if (const auto *value = request.payloadAs<AmazingGraceInteractionPayload>()) {
        appendCards(&lines, value->selection.selectableCards,
                    value->selection.disabledCards, m_cardResolver);
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
        lines << tr("自訂類型：%1").arg(sanitize(value->typeName, 128));
    }
    const QString hint = answerHint(request);
    if (!hint.isEmpty())
        lines << hint;
    if (request.cancelable && request.type != InteractionType::PlayCard)
        lines << tr("可輸入 /cancel 放棄");
    lines << QStringLiteral("> ");
    return lines.join(QLatin1Char('\n'));
}
