#include "tui-interaction-view.h"
#include "tui-text.h"

#include <QHash>
#include <QtGlobal>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace {

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

int hiddenHandSlots(const InteractionRequest &request)
{
    if (request.type != InteractionType::ChooseCard)
        return 0;
    const auto *value = request.payloadAs<CardInteractionPayload>();
    return value != nullptr ? qMax(0, value->hiddenHandCount) : 0;
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

QString canonicalRole(const QString &token, const QStringList &roles)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty())
        return trimmed;
    if (roles.contains(trimmed))
        return trimmed;
    // Typed by the player, so both scripts are accepted whichever one the
    // interface itself is written in.
    static const QHash<QString, QString> aliases{
        {QStringLiteral("主公"), QStringLiteral("lord")},
        {QStringLiteral("地主"), QStringLiteral("lord")},
        {QStringLiteral("忠臣"), QStringLiteral("loyalist")},
        {QStringLiteral("反贼"), QStringLiteral("rebel")},
        {QStringLiteral("反賊"), QStringLiteral("rebel")},
        {QStringLiteral("农民"), QStringLiteral("rebel")},
        {QStringLiteral("農民"), QStringLiteral("rebel")},
        {QStringLiteral("内奸"), QStringLiteral("renegade")},
        {QStringLiteral("內奸"), QStringLiteral("renegade")}};
    const QString mapped = aliases.value(trimmed, trimmed.toLower());
    if (roles.isEmpty() || roles.contains(mapped))
        return mapped;
    return trimmed;
}

QString canonicalPlayer(const QString &token, const QStringList &players, QString *error)
{
    bool numeric = false;
    const int index = token.toInt(&numeric) - 1;
    if (numeric) {
        if (index >= 0 && index < players.size())
            return players.at(index);
        if (error != nullptr)
            *error = tuiText("tui_error_player_index_range").arg(token);
        return QString();
    }
    if (players.isEmpty() || players.contains(token))
        return token;
    if (error != nullptr)
        *error = tuiText("tui_error_player_unavailable").arg(token);
    return QString();
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
            *error = tuiText("tui_error_skill_card_syntax");
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
                *error = tuiText("tui_error_skill_instance");
            return false;
        }
        activation.truncate(hash);
    }
    if (activation.isEmpty() || cardPart->isEmpty()) {
        if (error != nullptr)
            *error = tuiText("tui_error_skill_expression");
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
                                       CardTextResolver cardTextResolver,
                                       SkillCardResolver skillCardResolver,
                                       SkillDeclarationResolver skillDeclarationResolver)
    : m_renderer(renderer), m_writer(std::move(writer)),
      m_cardTextResolver(std::move(cardTextResolver)),
      m_skillCardResolver(std::move(skillCardResolver)),
      m_skillDeclarationResolver(std::move(skillDeclarationResolver))
{
}

void TuiInteractionView::presentRequest(const InteractionRequest &request)
{
    if (request.type == InteractionType::ChooseRole)
        return;
    if (m_renderer != nullptr && m_writer)
        m_writer(m_renderer->renderInteraction(request));
}

QString TuiInteractionView::requestTitle(const InteractionRequest &request) const
{
    // The wire request id is bookkeeping between the client core and the
    // server; a player has no way to act on it, so name the prompt instead.
    if (m_renderer != nullptr)
        return m_renderer->interactionTitle(request);
    return tuiText("tui_interaction_default_title");
}

void TuiInteractionView::finishRequest(const InteractionRequest &request,
                                       const InteractionResponse &)
{
    if (request.type == InteractionType::ChooseRole)
        return;
    if (m_writer)
        m_writer(tuiText("tui_answer_accepted").arg(requestTitle(request)));
}

QString TuiInteractionView::cancelReasonText(InteractionCancelReason reason)
{
    switch (reason) {
    case InteractionCancelReason::Superseded:
        return tuiText("tui_cancel_superseded");
    case InteractionCancelReason::Expired:
        return tuiText("tui_cancel_timeout");
    case InteractionCancelReason::Abandoned:
        return tuiText("tui_cancel_abandoned");
    case InteractionCancelReason::Disconnected:
        return tuiText("tui_cancel_disconnected");
    }
    return tuiText("tui_cancel_default");
}

void TuiInteractionView::cancelRequest(const InteractionRequest &request,
                                       InteractionCancelReason reason)
{
    if (m_writer) {
        m_writer(tuiText("tui_cancel_notice")
            .arg(requestTitle(request), cancelReasonText(reason)));
    }
}

QString TuiInteractionView::rejectionText(const InteractionValidation &validation)
{
    switch (validation.rejection) {
    case InteractionRejection::None:
        return tuiText("tui_reject_default");
    case InteractionRejection::NoActiveRequest:
        return tuiText("tui_reject_no_request");
    case InteractionRejection::RequestIdMismatch:
        return tuiText("tui_reject_wrong_request");
    case InteractionRejection::AlreadyCompleted:
        return tuiText("tui_reject_already_answered");
    case InteractionRejection::RequestCancelled:
        return tuiText("tui_reject_cancelled");
    case InteractionRejection::RequestExpired:
        return tuiText("tui_reject_timeout");
    case InteractionRejection::KindMismatch:
        return tuiText("tui_reject_shape");
    case InteractionRejection::UnknownOption:
        return tuiText("tui_reject_option_missing");
    case InteractionRejection::DisabledOption:
        return tuiText("tui_reject_option_disabled");
    case InteractionRejection::UnknownPlayer:
        return tuiText("tui_reject_player_missing");
    case InteractionRejection::DuplicatePlayer:
        return tuiText("tui_reject_player_duplicate");
    case InteractionRejection::UnknownCard:
        return tuiText("tui_reject_card_missing");
    case InteractionRejection::DisabledCard:
        return tuiText("tui_reject_card_disabled");
    case InteractionRejection::DuplicateCard:
        return tuiText("tui_reject_card_duplicate");
    case InteractionRejection::UnknownGeneral:
        return tuiText("tui_reject_general_missing");
    case InteractionRejection::DuplicateGeneral:
        return tuiText("tui_reject_general_duplicate");
    case InteractionRejection::SelectionCountOutOfRange:
        return tuiText("tui_reject_count");
    case InteractionRejection::NotCancelable:
        return tuiText("tui_reject_not_cancellable");
    }
    return tuiText("tui_reject_default");
}

void TuiInteractionView::rejectResponse(const InteractionRequest &request,
    const InteractionResponse &, const InteractionValidation &validation)
{
    if (!m_writer)
        return;
    const QString detail = TuiRenderer::sanitize(validation.detail, 512);
    m_writer(detail.isEmpty()
        ? tuiText("tui_reject_notice").arg(rejectionText(validation))
        : tuiText("tui_reject_notice_detail").arg(rejectionText(validation), detail));
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
                *error = tuiText("tui_error_index_range").arg(token);
            return {};
        }
        for (int index = first; index <= last; ++index) {
            if (seen.contains(index)) {
                if (error != nullptr)
                    *error = tuiText("tui_error_index_duplicate").arg(index);
                return {};
            }
            seen.insert(index);
            result.append(index - 1);
        }
    }
    if (result.isEmpty() && error != nullptr)
        *error = tuiText("tui_error_selection_empty");
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
                *error = tuiText("tui_error_player_invalid").arg(token);
            return {};
        }
        result.append(value);
    }
    if (result.isEmpty() && error != nullptr)
        *error = tuiText("tui_error_player_empty");
    return result;
}

bool TuiInteractionView::parseAnswer(const InteractionRequest &request,
    const QString &line, InteractionResponse *response, QString *error) const
{
    if (error != nullptr)
        error->clear();
    if (response == nullptr) {
        if (error != nullptr)
            *error = tuiText("tui_error_null_response");
        return false;
    }
    const QString text = line.trimmed();
    if (text.compare(QStringLiteral("cancel"), Qt::CaseInsensitive) == 0
        || text.compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0
        || text == QLatin1String("/cancel")
        || text.compare(QStringLiteral("pass"), Qt::CaseInsensitive) == 0
        || text == QStringLiteral("过")
        || text == QStringLiteral("過")
        || text == QStringLiteral("不出")) {
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
        const QList<SkillActivationCandidate> skills
            = request.payloadAs<CardInteractionPayload>() != nullptr
            ? request.payloadAs<CardInteractionPayload>()->skillCandidates
            : QList<SkillActivationCandidate>{};
        const int hiddenSlots = hiddenHandSlots(request);
        const int indexCount = hiddenSlots + candidates.size() + skills.size();
        QStringList cardTokens = splitTokens(cardPart);
        // "5=slash" is the declaration a dialog skill asks for, written on the
        // skill's own menu number. It comes out before the numbers are parsed.
        QString declaration;
        int declarationIndex = -1;
        bool declarationRepeated = false;
        static const QRegularExpression declaredToken(QStringLiteral("^([0-9]+)=(\\S+)$"));
        for (QString &token : cardTokens) {
            const QRegularExpressionMatch declared = declaredToken.match(token);
            if (!declared.hasMatch())
                continue;
            declarationRepeated = declarationRepeated || declarationIndex >= 0;
            declarationIndex = declared.captured(1).toInt() - 1;
            declaration = declared.captured(2);
            token = declared.captured(1);
        }
        if (declarationRepeated) {
            if (error != nullptr)
                *error = tuiText("tui_error_declaration_repeated");
            return false;
        }
        if (!declaration.isEmpty())
            cardPart = cardTokens.join(QLatin1Char(' '));
        bool indexSyntax = !cardTokens.isEmpty() && indexCount > 0;
        static const QRegularExpression indexToken(QStringLiteral("^[0-9]+(?:-[0-9]+)?$"));
        for (const QString &token : cardTokens)
            indexSyntax = indexSyntax && indexToken.match(token).hasMatch();
        if (!declaration.isEmpty() && !indexSyntax) {
            if (error != nullptr)
                *error = tuiText("tui_error_declaration_misplaced");
            return false;
        }
        if (indexSyntax) {
            const QList<int> indexes = parseIndexes(cardPart, indexCount, error);
            if (indexes.isEmpty())
                return false;
            int skillIndex = -1;
            int skillMenuIndex = -1;
            QList<int> selectedCardIndexes;
            for (int index : indexes) {
                if (index < hiddenSlots) {
                    answer->cardIds.append(-1);
                    continue;
                }
                const int cardIndex = index - hiddenSlots;
                if (cardIndex < candidates.size()) {
                    selectedCardIndexes.append(cardIndex);
                    continue;
                }
                if (skillIndex >= 0) {
                    if (error != nullptr)
                        *error = tuiText("tui_error_skill_repeated");
                    return false;
                }
                skillIndex = cardIndex - candidates.size();
                skillMenuIndex = index;
            }
            if (declarationIndex >= 0 && declarationIndex != skillMenuIndex) {
                if (error != nullptr)
                    *error = tuiText("tui_error_declaration_misplaced");
                return false;
            }
            if (skillIndex >= 0) {
                const SkillActivationCandidate &skill = skills.at(skillIndex);
                // Runs even with nothing declared: a skill that asks for a
                // declaration says so here, and one that does not has its last
                // answer cleared before this card is built from it.
                if (m_skillDeclarationResolver
                    && !m_skillDeclarationResolver(skill.skillName, declaration, error)) {
                    return false;
                }
                QList<int> subcardIds;
                for (int cardIndex : selectedCardIndexes)
                    subcardIds.append(candidates.at(cardIndex));
                if (!m_skillCardResolver) {
                    if (error != nullptr)
                        *error = tuiText("tui_error_skill_card_build");
                    return false;
                }
                const QString cardText = m_skillCardResolver(skill.skillName,
                    skill.instanceId, subcardIds, error);
                if (cardText.isEmpty())
                    return false;
                answer->cardText = cardText;
                answer->subcardIds = subcardIds;
                answer->activationSkillName = skill.skillName;
                answer->activationSkillInstanceId = skill.instanceId;
            } else {
                for (int cardIndex : selectedCardIndexes)
                    answer->cardIds.append(candidates.at(cardIndex));
            }
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
                    *error = tuiText("tui_error_card_index_unauthorized");
                return false;
            }
            if (!cardTextAllowed(request) && !cardPart.isEmpty()) {
                if (error != nullptr)
                    *error = tuiText("tui_error_card_text_forbidden");
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
                        *error = tuiText("tui_error_targets_invalid");
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
                        *error = tuiText("tui_error_target_duplicate").arg(target);
                    return false;
                }
                answer->targets.append(target);
            }
        }
        if (!activationSkillName.isEmpty()) {
            answer->activationSkillName = activationSkillName;
            answer->activationSkillInstanceId = activationSkillInstanceId;
        }
        *response = std::move(value);
        break;
    }
    case InteractionResponseShape::Assignment: {
        const auto *assignment = request.payloadAs<RoleAssignmentInteractionPayload>();
        const QStringList players = assignment != nullptr ? assignment->playerNames
                                                          : QStringList();
        const QStringList roles = assignment != nullptr ? assignment->roles
                                                        : QStringList();
        QStringList names;
        QStringList values;
        for (const QString &token : splitTokens(text)) {
            const qsizetype equals = token.indexOf(QLatin1Char('='));
            if (equals <= 0 || equals == token.size() - 1) {
                if (error != nullptr)
                    *error = tuiText("tui_error_role_syntax");
                return false;
            }
            const QString player = canonicalPlayer(token.left(equals), players, error);
            if (player.isEmpty())
                return false;
            const QString role = canonicalRole(token.mid(equals + 1), roles);
            if (names.contains(player)) {
                if (error != nullptr)
                    *error = tuiText("tui_error_role_player_duplicate").arg(token.left(equals));
                return false;
            }
            names.append(player);
            values.append(role);
        }
        if (names.isEmpty()) {
            if (error != nullptr)
                *error = tuiText("tui_error_role_incomplete");
            return false;
        }
        *response = InteractionResponse::makeAssignment(request.requestId, names, values);
        break;
    }
    case InteractionResponseShape::Rearrangement: {
        const QStringList halves = text.split(QLatin1Char('|'));
        if (halves.size() != 2) {
            if (error != nullptr)
                *error = tuiText("tui_error_rearrange_syntax");
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
                *error = tuiText("tui_error_distribution_syntax");
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
                *error = tuiText("tui_error_custom_json");
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
            *error = tuiText("tui_error_no_schema");
        return false;
    }
    response->command = request.command;
    return true;
}
