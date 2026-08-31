#include "tui-interaction-view.h"

#include <QJsonArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace {

QString tr(const char *source)
{
    return QCoreApplication::translate("QSanguoshaTui", source);
}

QStringList splitTokens(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

QList<InteractionOption> requestOptions(const InteractionRequest &request)
{
    if (const auto *value = request.payloadAs<OptionInteractionPayload>())
        return value->options;
    if (const auto *value = request.payloadAs<ChooseOrderInteractionPayload>())
        return value->options;
    QList<InteractionOption> result;
    if (const auto *value = request.payloadAs<TriggerOrderInteractionPayload>()) {
        for (const TriggerOrderOption &option : value->options)
            result.append(InteractionOption(option.responseValue, option.skillName));
    }
    return result;
}

QList<int> requestCards(const InteractionRequest &request)
{
    if (const auto *value = request.payloadAs<CardInteractionPayload>()) {
        return value->selection.selectableCards.isEmpty()
            ? value->suggestedCards : value->selection.selectableCards;
    }
    if (const auto *value = request.payloadAs<GongxinInteractionPayload>())
        return value->selectableCards;
    if (const auto *value = request.payloadAs<AmazingGraceInteractionPayload>())
        return value->selection.selectableCards;
    if (const auto *value = request.payloadAs<YijiInteractionPayload>())
        return value->cardIds;
    if (const auto *value = request.payloadAs<RearrangeCardsInteractionPayload>())
        return value->cardIds;
    return {};
}

QStringList requestPlayers(const InteractionRequest &request)
{
    if (const auto *value = request.payloadAs<PlayerInteractionPayload>())
        return value->selection.selectablePlayers;
    if (const auto *value = request.payloadAs<CardInteractionPayload>())
        return value->optionalTargets;
    if (const auto *value = request.payloadAs<YijiInteractionPayload>())
        return value->targetPlayers;
    return {};
}

bool cardTextAllowed(const InteractionRequest &request)
{
    const auto *value = request.payloadAs<CardInteractionPayload>();
    return value != nullptr && value->cardTextAllowed;
}

bool parseActivationPrefix(QString *cardPart, QString *skillName,
                           int *skillInstanceId, QString *error)
{
    if (cardPart == nullptr || !cardPart->startsWith(
            QStringLiteral("skill "), Qt::CaseInsensitive)) {
        return true;
    }
    const qsizetype separator = cardPart->indexOf(QLatin1Char(':'));
    if (separator < 7) {
        if (error != nullptr)
            *error = tr("技能牌格式必須是 'skill <名稱>[#實例]: <牌>'");
        return false;
    }
    QString activation = cardPart->mid(6, separator - 6).trimmed();
    *cardPart = cardPart->mid(separator + 1).trimmed();
    const qsizetype hash = activation.lastIndexOf(QLatin1Char('#'));
    int instanceId = 0;
    if (hash > 0) {
        bool ok = false;
        instanceId = activation.mid(hash + 1).toInt(&ok);
        if (!ok || instanceId <= 0) {
            if (error != nullptr)
                *error = tr("啟用技能實例必須是正整數");
            return false;
        }
        activation.truncate(hash);
    }
    if (activation.isEmpty() || cardPart->isEmpty()) {
        if (error != nullptr)
            *error = tr("必須提供啟用技能與牌運算式");
        return false;
    }
    if (skillName != nullptr)
        *skillName = activation;
    if (skillInstanceId != nullptr)
        *skillInstanceId = instanceId;
    return true;
}

} // namespace

TuiInteractionView::TuiInteractionView(TuiRenderer *renderer, Writer writer,
                                       CardTextResolver cardTextResolver)
    : m_renderer(renderer), m_writer(std::move(writer)),
      m_cardTextResolver(std::move(cardTextResolver))
{
}

void TuiInteractionView::presentRequest(const InteractionRequest &request)
{
    if (m_renderer != nullptr && m_writer)
        m_writer(m_renderer->renderInteraction(request));
}

void TuiInteractionView::finishRequest(const InteractionRequest &request,
                                       const InteractionResponse &)
{
    if (m_writer)
        m_writer(tr("請求 %1 的作答已接受").arg(request.requestId));
}

void TuiInteractionView::cancelRequest(const InteractionRequest &request,
                                       InteractionCancelReason reason)
{
    if (m_writer) {
        m_writer(tr("請求 %1 已取消：%2")
            .arg(request.requestId).arg(interactionCancelReasonName(reason)));
    }
}

QString TuiInteractionView::rejectionText(const InteractionValidation &validation)
{
    switch (validation.rejection) {
    case InteractionRejection::None:
        return tr("作答無效");
    case InteractionRejection::NoActiveRequest:
        return tr("目前沒有等待作答的互動");
    case InteractionRejection::RequestIdMismatch:
        return tr("這個作答不屬於目前的互動");
    case InteractionRejection::AlreadyCompleted:
        return tr("這個互動已經作答過了");
    case InteractionRejection::RequestCancelled:
        return tr("這個互動已被取消");
    case InteractionRejection::RequestExpired:
        return tr("這個互動已逾時");
    case InteractionRejection::KindMismatch:
        return tr("作答的形式與這個互動不符");
    case InteractionRejection::UnknownOption:
        return tr("沒有這個選項");
    case InteractionRejection::DisabledOption:
        return tr("這個選項目前不可選");
    case InteractionRejection::UnknownPlayer:
        return tr("沒有這名玩家");
    case InteractionRejection::DuplicatePlayer:
        return tr("同一名玩家不可重複選擇");
    case InteractionRejection::UnknownCard:
        return tr("沒有這張牌");
    case InteractionRejection::DisabledCard:
        return tr("這張牌目前不可選");
    case InteractionRejection::DuplicateCard:
        return tr("同一張牌不可重複選擇");
    case InteractionRejection::UnknownGeneral:
        return tr("沒有這名武將");
    case InteractionRejection::DuplicateGeneral:
        return tr("同一名武將不可重複選擇");
    case InteractionRejection::SelectionCountOutOfRange:
        return tr("選擇的數量不符要求");
    case InteractionRejection::NotCancelable:
        return tr("這個互動不可取消");
    }
    return tr("作答無效");
}

void TuiInteractionView::rejectResponse(const InteractionRequest &request,
    const InteractionResponse &, const InteractionValidation &validation)
{
    if (!m_writer)
        return;
    const QString detail = TuiRenderer::sanitize(validation.detail, 512);
    m_writer(detail.isEmpty()
        ? tr("作答無效：%1").arg(rejectionText(validation))
        : tr("作答無效：%1（%2）").arg(rejectionText(validation), detail));
    // The request is still open, so show it again rather than leaving the
    // player staring at an error with no prompt.
    presentRequest(request);
}

QList<int> TuiInteractionView::parseIndexes(const QString &text, int size,
                                            QString *error) const
{
    QList<int> result;
    QSet<int> seen;
    for (const QString &token : splitTokens(text)) {
        const qsizetype separator = token.indexOf(QLatin1Char('-'), 1);
        bool firstOk = false;
        bool lastOk = false;
        const int first = (separator > 0 ? token.left(separator) : token).toInt(&firstOk);
        const int last = separator > 0 ? token.mid(separator + 1).toInt(&lastOk) : first;
        if (separator <= 0)
            lastOk = firstOk;
        if (!firstOk || !lastOk || first < 1 || last < first || last > size) {
            if (error != nullptr)
                *error = tr("選擇索引 '%1' 超出範圍").arg(token);
            return {};
        }
        for (int index = first; index <= last; ++index) {
            if (seen.contains(index)) {
                if (error != nullptr)
                    *error = tr("選擇索引 %1 重複").arg(index);
                return {};
            }
            seen.insert(index);
            result.append(index - 1);
        }
    }
    if (result.isEmpty() && error != nullptr)
        *error = tr("選擇不可為空");
    return result;
}

QStringList TuiInteractionView::parseNames(const QString &text,
                                           const QStringList &values,
                                           QString *error) const
{
    QStringList result;
    for (const QString &token : splitTokens(text)) {
        bool numeric = false;
        const int index = token.toInt(&numeric) - 1;
        const QString value = numeric && index >= 0 && index < values.size()
            ? values.at(index) : token;
        if (!values.contains(value) || result.contains(value)) {
            if (error != nullptr)
                *error = tr("玩家 '%1' 不可用或重複").arg(token);
            return {};
        }
        result.append(value);
    }
    if (result.isEmpty() && error != nullptr)
        *error = tr("玩家選擇不可為空");
    return result;
}

bool TuiInteractionView::parseAnswer(const InteractionRequest &request,
    const QString &line, InteractionResponse *response, QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (response == nullptr) {
        if (error != nullptr)
            *error = tr("互動回覆輸出不可為 null");
        return false;
    }
    const QString text = line.trimmed();
    if (text.compare(QStringLiteral("cancel"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0
        || text == QLatin1String("/cancel")) {
        *response = InteractionResponse::makeCancel(request.requestId);
        response->command = request.command;
        return true;
    }

    switch (request.responseSchema) {
    case InteractionResponseShape::Option: {
        const QList<InteractionOption> values = requestOptions(request);
        bool numeric = false;
        const int index = text.toInt(&numeric) - 1;
        QString value = numeric && index >= 0 && index < values.size()
            ? values.at(index).value : text;
        if (value.compare(QStringLiteral("y"), Qt::CaseInsensitive) == 0)
            value = QStringLiteral("yes");
        else if (value.compare(QStringLiteral("n"), Qt::CaseInsensitive) == 0)
            value = QStringLiteral("no");
        *response = InteractionResponse::makeOption(request.requestId, value);
        break;
    }
    case InteractionResponseShape::Players: {
        const QStringList selected = parseNames(text, requestPlayers(request), error);
        if (selected.isEmpty())
            return false;
        *response = InteractionResponse::makePlayers(request.requestId, selected);
        break;
    }
    case InteractionResponseShape::Cards: {
        const QStringList halves = text.split(QStringLiteral("->"));
        QString cardPart = halves.first().trimmed();
        const QString targetPart = halves.size() > 1 ? halves.mid(1).join(QStringLiteral("->")).trimmed()
                                                    : QString();
        QString activationSkillName;
        int activationSkillInstanceId = 0;
        if (!parseActivationPrefix(&cardPart, &activationSkillName,
                                   &activationSkillInstanceId, error)) {
            return false;
        }
        if (cardPart.startsWith(QStringLiteral("card "), Qt::CaseInsensitive))
            cardPart = cardPart.mid(5).trimmed();
        InteractionResponse value = InteractionResponse::makeCards(request.requestId, {});
        auto *answer = std::get_if<InteractionResponse::CardSelectionData>(&value.payload);
        const QList<int> candidates = requestCards(request);
        const QStringList cardTokens = splitTokens(cardPart);
        bool indexSyntax = !cardTokens.isEmpty() && !candidates.isEmpty();
        static const QRegularExpression indexToken(QStringLiteral("^[0-9]+(?:-[0-9]+)?$"));
        for (const QString &token : cardTokens)
            indexSyntax = indexSyntax && indexToken.match(token).hasMatch();
        if (indexSyntax) {
            const QList<int> indexes = parseIndexes(cardPart, candidates.size(), error);
            if (indexes.isEmpty())
                return false;
            for (int index : indexes)
                answer->cardIds.append(candidates.at(index));
        }

        if (!answer->cardIds.isEmpty()) {
            if (answer->cardIds.size() == 1 && m_cardTextResolver
                && (request.type == InteractionType::PlayCard
                    || request.type == InteractionType::ResponseCard
                    || request.type == InteractionType::AskPeach
                    || request.type == InteractionType::Nullification
                    || request.type == InteractionType::ShowCard
                    || request.type == InteractionType::Pindian)) {
                answer->cardText = m_cardTextResolver(answer->cardIds.first());
            }
        } else if (!indexSyntax) {
            bool numericOnly = !cardTokens.isEmpty();
            for (const QString &token : cardTokens) {
                bool ok = false;
                token.toInt(&ok);
                numericOnly = numericOnly && ok;
            }
            if (numericOnly && candidates.isEmpty()) {
                if (error != nullptr)
                    *error = tr("數字牌索引需要已授權候選清單");
                return false;
            }
            if (!cardTextAllowed(request) && !cardPart.isEmpty()) {
                if (error != nullptr)
                    *error = tr("此請求不允許語意牌字串");
                return false;
            }
            answer->cardText = cardPart;
        }
        if (const auto *cardRequest = request.payloadAs<CardInteractionPayload>())
            answer->targets = cardRequest->fixedTargets;
        if (!targetPart.isEmpty()) {
            const QStringList candidates = requestPlayers(request);
            QStringList targets;
            if (candidates.isEmpty()) {
                targets = splitTokens(targetPart);
                QSet<QString> unique(targets.begin(), targets.end());
                if (targets.isEmpty() || unique.size() != targets.size()) {
                    if (error != nullptr)
                        *error = tr("目標選擇為空或重複");
                    return false;
                }
            } else {
                targets = parseNames(targetPart, candidates, error);
                if (targets.isEmpty())
                    return false;
            }
            for (const QString &target : targets) {
                if (answer->targets.contains(target)) {
                    if (error != nullptr)
                        *error = tr("目標 '%1' 重複").arg(target);
                    return false;
                }
                answer->targets.append(target);
            }
        }
        answer->activationSkillName = activationSkillName;
        answer->activationSkillInstanceId = activationSkillInstanceId;
        *response = std::move(value);
        break;
    }
    case InteractionResponseShape::Assignment: {
        QStringList names;
        QStringList values;
        for (const QString &token : splitTokens(text)) {
            const qsizetype equals = token.indexOf(QLatin1Char('='));
            if (equals <= 0 || equals == token.size() - 1) {
                if (error != nullptr)
                    *error = tr("身分分配必須使用 player=role");
                return false;
            }
            names.append(token.left(equals));
            values.append(token.mid(equals + 1));
        }
        *response = InteractionResponse::makeAssignment(request.requestId, names, values);
        break;
    }
    case InteractionResponseShape::Rearrangement: {
        const QStringList halves = text.split(QLatin1Char('|'));
        if (halves.size() != 2) {
            if (error != nullptr)
                *error = tr("排列必須使用 '<頂部> | <底部>'");
            return false;
        }
        const QList<int> cards = requestCards(request);
        QList<int> first;
        QList<int> second;
        if (!halves.first().trimmed().isEmpty()) {
            for (int index : parseIndexes(halves.first(), cards.size(), error))
                first.append(cards.at(index));
            if (first.isEmpty())
                return false;
        }
        if (!halves.last().trimmed().isEmpty()) {
            for (int index : parseIndexes(halves.last(), cards.size(), error))
                second.append(cards.at(index));
            if (second.isEmpty())
                return false;
        }
        *response = InteractionResponse::makeRearrangement(request.requestId, first, second);
        break;
    }
    case InteractionResponseShape::Distribution: {
        const QStringList halves = text.split(QStringLiteral("->"));
        if (halves.size() != 2) {
            if (error != nullptr)
                *error = tr("分配必須使用 'cards <索引> -> <玩家>'");
            return false;
        }
        QString selection = halves.first().trimmed();
        if (selection.startsWith(QStringLiteral("cards "), Qt::CaseInsensitive))
            selection = selection.mid(6).trimmed();
        const QList<int> cards = requestCards(request);
        QList<int> selected;
        for (int index : parseIndexes(selection, cards.size(), error))
            selected.append(cards.at(index));
        const QStringList players = parseNames(halves.last(), requestPlayers(request), error);
        if (selected.isEmpty() || players.size() != 1)
            return false;
        *response = InteractionResponse::makeDistribution(request.requestId,
            selected, players.first());
        break;
    }
    case InteractionResponseShape::GeneralArrangement: {
        const auto *arrange = request.payloadAs<ArrangeGeneralsInteractionPayload>();
        if (arrange == nullptr)
            return false;
        QStringList selected;
        for (const QString &token : splitTokens(text)) {
            bool numeric = false;
            const int index = token.toInt(&numeric) - 1;
            selected.append(numeric && index >= 0 && index < arrange->generalNames.size()
                ? arrange->generalNames.at(index) : token);
        }
        *response = InteractionResponse::makeGeneralArrangement(request.requestId, selected);
        break;
    }
    case InteractionResponseShape::Custom: {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || document.isNull()) {
            if (error != nullptr)
                *error = tr("自訂作答必須是有效 JSON");
            return false;
        }
        const auto *custom = request.payloadAs<CustomInteractionPayload>();
        const QVariant value = document.isArray() ? QVariant(document.array().toVariantList())
                                                  : QVariant(document.object().toVariantMap());
        *response = InteractionResponse::makeCustom(request.requestId,
            custom != nullptr ? custom->schemaVersion : 1,
            custom != nullptr ? custom->typeName : QString(), value);
        break;
    }
    case InteractionResponseShape::None:
        if (error != nullptr)
            *error = tr("互動沒有回覆 schema");
        return false;
    }
    response->command = request.command;
    return true;
}
