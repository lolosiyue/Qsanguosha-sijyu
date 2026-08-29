#include "interaction-descriptor-registry.h"

#include <QSet>

using namespace QSanProtocol;

namespace {

class RecordingInteractionPresenter final : public IClientInteractionPresenter
{
public:
#define QSAN_RECORD_PRESENTER(method) \
    void method(const InteractionRequest &request) override { record(request); }
    QSAN_RECORD_PRESENTER(presentGeneralChoice)
    QSAN_RECORD_PRESENTER(presentOptionChoice)
    QSAN_RECORD_PRESENTER(presentPlayerChoice)
    QSAN_RECORD_PRESENTER(presentSkillInvoke)
    QSAN_RECORD_PRESENTER(presentCardResponse)
    QSAN_RECORD_PRESENTER(presentRoleAssignment)
    QSAN_RECORD_PRESENTER(presentDirectionChoice)
    QSAN_RECORD_PRESENTER(presentCardExchange)
    QSAN_RECORD_PRESENTER(presentCardDiscard)
    QSAN_RECORD_PRESENTER(presentRespondingUse)
    QSAN_RECORD_PRESENTER(presentShowOrPindian)
    QSAN_RECORD_PRESENTER(presentPlayCard)
    QSAN_RECORD_PRESENTER(presentGuanxing)
    QSAN_RECORD_PRESENTER(presentGongxin)
    QSAN_RECORD_PRESENTER(presentYiji)
    QSAN_RECORD_PRESENTER(presentSuitChoice)
    QSAN_RECORD_PRESENTER(presentKingdomChoice)
    QSAN_RECORD_PRESENTER(presentTriggerOrder)
    QSAN_RECORD_PRESENTER(presentAmazingGrace)
    QSAN_RECORD_PRESENTER(presentChooseCard)
    QSAN_RECORD_PRESENTER(presentOrderChoice)
    QSAN_RECORD_PRESENTER(presentRole3v3)
    QSAN_RECORD_PRESENTER(presentBooleanPrompt)
    QSAN_RECORD_PRESENTER(presentDraftGeneral)
    QSAN_RECORD_PRESENTER(presentArrangeGeneral)
    QSAN_RECORD_PRESENTER(presentQmlInteraction)
#undef QSAN_RECORD_PRESENTER

    int invocationCount = 0;
    QSet<int> presentedTypes;

private:
    void record(const InteractionRequest &request)
    {
        ++invocationCount;
        presentedTypes.insert(static_cast<int>(request.type));
    }
};

} // namespace

QString interactionSupportName(InteractionSupport support)
{
    return support == InteractionSupport::CanonicalTyped
        ? QStringLiteral("canonical_typed") : QStringLiteral("legacy_adapter");
}

const std::array<ClientInteractionDescriptor, 29> &InteractionDescriptorRegistry::descriptors()
{
    using Adapter = LegacyV1InteractionReplyAdapter;
    static const std::array<ClientInteractionDescriptor, 29> values = {{
        { S_COMMAND_CHOOSE_ROLE, InteractionType::ChooseRole, &Client::askForAssign, &IClientInteractionPresenter::presentRoleAssignment, InteractionResponseShape::Assignment, &Adapter::assignment, InteractionSupport::CanonicalTyped, "CHOOSE_ROLE", "askForAssign", "presentRoleAssignment", "assignment", "choose_role" },
        { S_COMMAND_CHOOSE_GENERAL, InteractionType::ChooseGeneral, &Client::askForGeneral, &IClientInteractionPresenter::presentGeneralChoice, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "CHOOSE_GENERAL", "askForGeneral", "presentGeneralChoice", "option_string", "choose_general" },
        { S_COMMAND_CHOOSE_DIRECTION, InteractionType::ChooseDirection, &Client::askForDirection, &IClientInteractionPresenter::presentDirectionChoice, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "CHOOSE_DIRECTION", "askForDirection", "presentDirectionChoice", "option_string", "choose_direction" },
        { S_COMMAND_EXCHANGE_CARD, InteractionType::ExchangeCard, &Client::askForExchange, &IClientInteractionPresenter::presentCardExchange, InteractionResponseShape::Cards, &Adapter::discardCards, InteractionSupport::CanonicalTyped, "EXCHANGE_CARD", "askForExchange", "presentCardExchange", "discard_cards", "exchange" },
        { S_COMMAND_ASK_PEACH, InteractionType::AskPeach, &Client::askForSinglePeach, &IClientInteractionPresenter::presentRespondingUse, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "ASK_PEACH", "askForSinglePeach", "presentRespondingUse", "card_response", "ask_peach" },
        { S_COMMAND_SKILL_GUANXING, InteractionType::SkillGuanxing, &Client::askForGuanxing, &IClientInteractionPresenter::presentGuanxing, InteractionResponseShape::Rearrangement, &Adapter::rearrangement, InteractionSupport::CanonicalTyped, "SKILL_GUANXING", "askForGuanxing", "presentGuanxing", "rearrangement", "guanxing" },
        { S_COMMAND_SKILL_GONGXIN, InteractionType::SkillGongxin, &Client::askForGongxin, &IClientInteractionPresenter::presentGongxin, InteractionResponseShape::Cards, &Adapter::cardId, InteractionSupport::CanonicalTyped, "SKILL_GONGXIN", "askForGongxin", "presentGongxin", "card_id", "gongxin" },
        { S_COMMAND_SKILL_YIJI, InteractionType::SkillYiji, &Client::askForYiji, &IClientInteractionPresenter::presentYiji, InteractionResponseShape::Distribution, &Adapter::distribution, InteractionSupport::CanonicalTyped, "SKILL_YIJI", "askForYiji", "presentYiji", "distribution", "yiji" },
        { S_COMMAND_PLAY_CARD, InteractionType::PlayCard, &Client::activate, &IClientInteractionPresenter::presentPlayCard, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "PLAY_CARD", "activate", "presentPlayCard", "card_response", "play_card" },
        { S_COMMAND_RESPONSE_CARD, InteractionType::ResponseCard, &Client::askForCardOrUseCard, &IClientInteractionPresenter::presentCardResponse, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "RESPONSE_CARD", "askForCardOrUseCard", "presentCardResponse", "card_response", "card_response" },
        { S_COMMAND_DISCARD_CARD, InteractionType::DiscardCard, &Client::askForDiscard, &IClientInteractionPresenter::presentCardDiscard, InteractionResponseShape::Cards, &Adapter::discardCards, InteractionSupport::CanonicalTyped, "DISCARD_CARD", "askForDiscard", "presentCardDiscard", "discard_cards", "discard" },
        { S_COMMAND_MULTIPLE_CHOICE, InteractionType::Choice, &Client::askForChoice, &IClientInteractionPresenter::presentOptionChoice, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "MULTIPLE_CHOICE", "askForChoice", "presentOptionChoice", "option_string", "choice" },
        { S_COMMAND_CHOOSE_SUIT, InteractionType::ChooseSuit, &Client::askForSuit, &IClientInteractionPresenter::presentSuitChoice, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "CHOOSE_SUIT", "askForSuit", "presentSuitChoice", "option_string", "choose_suit" },
        { S_COMMAND_CHOOSE_KINGDOM, InteractionType::ChooseKingdom, &Client::askForKingdom, &IClientInteractionPresenter::presentKingdomChoice, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "CHOOSE_KINGDOM", "askForKingdom", "presentKingdomChoice", "option_string", "choose_kingdom" },
        { S_COMMAND_CHOOSE_PLAYER, InteractionType::ChoosePlayer, &Client::askForPlayerChosen, &IClientInteractionPresenter::presentPlayerChoice, InteractionResponseShape::Players, &Adapter::playersJoined, InteractionSupport::CanonicalTyped, "CHOOSE_PLAYER", "askForPlayerChosen", "presentPlayerChoice", "players_joined", "player_chosen" },
        { S_COMMAND_INVOKE_SKILL, InteractionType::SkillInvoke, &Client::askForSkillInvoke, &IClientInteractionPresenter::presentSkillInvoke, InteractionResponseShape::Option, &Adapter::optionBool, InteractionSupport::CanonicalTyped, "INVOKE_SKILL", "askForSkillInvoke", "presentSkillInvoke", "option_bool", "skill_invoke" },
        { S_COMMAND_TRIGGER_ORDER, InteractionType::TriggerOrder, &Client::askForTriggerOrder, &IClientInteractionPresenter::presentTriggerOrder, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "TRIGGER_ORDER", "askForTriggerOrder", "presentTriggerOrder", "option_string", "trigger_order" },
        { S_COMMAND_NULLIFICATION, InteractionType::Nullification, &Client::askForNullification, &IClientInteractionPresenter::presentRespondingUse, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "NULLIFICATION", "askForNullification", "presentRespondingUse", "card_response", "nullification" },
        { S_COMMAND_SHOW_CARD, InteractionType::ShowCard, &Client::askForCardShow, &IClientInteractionPresenter::presentShowOrPindian, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "SHOW_CARD", "askForCardShow", "presentShowOrPindian", "card_response", "show_card" },
        { S_COMMAND_AMAZING_GRACE, InteractionType::AmazingGrace, &Client::askForAG, &IClientInteractionPresenter::presentAmazingGrace, InteractionResponseShape::Cards, &Adapter::amazingGraceCardId, InteractionSupport::CanonicalTyped, "AMAZING_GRACE", "askForAG", "presentAmazingGrace", "amazing_grace_card_id", "ask_for_ag" },
        { S_COMMAND_PINDIAN, InteractionType::Pindian, &Client::askForPindian, &IClientInteractionPresenter::presentShowOrPindian, InteractionResponseShape::Cards, &Adapter::cardResponse, InteractionSupport::CanonicalTyped, "PINDIAN", "askForPindian", "presentShowOrPindian", "card_response", "pindian" },
        { S_COMMAND_CHOOSE_CARD, InteractionType::ChooseCard, &Client::askForCardChosen, &IClientInteractionPresenter::presentChooseCard, InteractionResponseShape::Cards, &Adapter::cardId, InteractionSupport::CanonicalTyped, "CHOOSE_CARD", "askForCardChosen", "presentChooseCard", "card_id", "card_chosen" },
        { S_COMMAND_CHOOSE_ORDER, InteractionType::ChooseOrder, &Client::askForOrder, &IClientInteractionPresenter::presentOrderChoice, InteractionResponseShape::Option, &Adapter::optionInt, InteractionSupport::CanonicalTyped, "CHOOSE_ORDER", "askForOrder", "presentOrderChoice", "option_int", "choose_order" },
        { S_COMMAND_CHOOSE_ROLE_3V3, InteractionType::ChooseRole3v3, &Client::askForRole3v3, &IClientInteractionPresenter::presentRole3v3, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "CHOOSE_ROLE_3V3", "askForRole3v3", "presentRole3v3", "option_string", "choose_role_3v3" },
        { S_COMMAND_SURRENDER, InteractionType::Surrender, &Client::askForSurrender, &IClientInteractionPresenter::presentBooleanPrompt, InteractionResponseShape::Option, &Adapter::optionBool, InteractionSupport::CanonicalTyped, "SURRENDER", "askForSurrender", "presentBooleanPrompt", "option_bool", "surrender" },
        { S_COMMAND_LUCK_CARD, InteractionType::LuckCard, &Client::askForLuckCard, &IClientInteractionPresenter::presentBooleanPrompt, InteractionResponseShape::Option, &Adapter::optionBool, InteractionSupport::CanonicalTyped, "LUCK_CARD", "askForLuckCard", "presentBooleanPrompt", "option_bool", "luck_card" },
        { S_COMMAND_ASK_GENERAL, InteractionType::AskGeneral, &Client::askForGeneral3v3, &IClientInteractionPresenter::presentDraftGeneral, InteractionResponseShape::Option, &Adapter::optionString, InteractionSupport::CanonicalTyped, "ASK_GENERAL", "askForGeneral3v3", "presentDraftGeneral", "option_string", "ask_general" },
        { S_COMMAND_ARRANGE_GENERAL, InteractionType::ArrangeGeneral, &Client::startArrange, &IClientInteractionPresenter::presentArrangeGeneral, InteractionResponseShape::GeneralArrangement, &Adapter::generalArrangement, InteractionSupport::CanonicalTyped, "ARRANGE_GENERAL", "startArrange", "presentArrangeGeneral", "general_arrangement", "arrange_general" },
        { S_COMMAND_QML_INTERACT, InteractionType::QmlInteract, &Client::askForQml, &IClientInteractionPresenter::presentQmlInteraction, InteractionResponseShape::Custom, &Adapter::custom, InteractionSupport::LegacyAdapter, "QML_INTERACT", "askForQml", "presentQmlInteraction", "custom", "qml_interact" }
    }};
    return values;
}

const ClientInteractionDescriptor *InteractionDescriptorRegistry::find(CommandType command)
{
    for (const ClientInteractionDescriptor &descriptor : descriptors()) {
        if (descriptor.command == command)
            return &descriptor;
    }
    return nullptr;
}

const ClientInteractionDescriptor *InteractionDescriptorRegistry::find(InteractionType type)
{
    for (const ClientInteractionDescriptor &descriptor : descriptors()) {
        if (descriptor.type == type)
            return &descriptor;
    }
    return nullptr;
}

QJsonArray InteractionDescriptorRegistry::inventory()
{
    QJsonArray result;
    for (const ClientInteractionDescriptor &descriptor : descriptors()) {
        QJsonObject entry;
        entry.insert(QStringLiteral("command"), QString::fromLatin1(descriptor.commandName));
        entry.insert(QStringLiteral("command_value"), static_cast<int>(descriptor.command));
        entry.insert(QStringLiteral("type"), interactionTypeName(descriptor.type));
        entry.insert(QStringLiteral("builder"), QString::fromLatin1(descriptor.builderName));
        entry.insert(QStringLiteral("validator"),
            interactionResponseShapeName(descriptor.responseShape));
        entry.insert(QStringLiteral("desktop_presenter"),
            QString::fromLatin1(descriptor.presenterName));
        entry.insert(QStringLiteral("reply_encoder"),
            QString::fromLatin1(descriptor.encoderName));
        entry.insert(QStringLiteral("test"), QString::fromLatin1(descriptor.testName));
        entry.insert(QStringLiteral("support"), interactionSupportName(descriptor.support));
        entry.insert(QStringLiteral("has_builder"), descriptor.builder != nullptr);
        entry.insert(QStringLiteral("has_presenter"), descriptor.presenter != nullptr);
        entry.insert(QStringLiteral("has_validator"),
            descriptor.responseShape != InteractionResponseShape::None);
        entry.insert(QStringLiteral("has_reply_encoder"), descriptor.replyEncoder != nullptr);
        result.append(entry);
    }
    return result;
}

QJsonObject InteractionDescriptorRegistry::inventoryDocument()
{
    int canonicalCount = 0;
    int legacyCount = 0;
    int missingBuilders = 0;
    int missingPresenters = 0;
    int missingValidators = 0;
    int missingEncoders = 0;
    RecordingInteractionPresenter recorder;
    for (const ClientInteractionDescriptor &descriptor : descriptors()) {
        descriptor.support == InteractionSupport::CanonicalTyped
            ? ++canonicalCount : ++legacyCount;
        if (descriptor.builder == nullptr)
            ++missingBuilders;
        if (descriptor.presenter == nullptr)
            ++missingPresenters;
        if (descriptor.responseShape == InteractionResponseShape::None)
            ++missingValidators;
        if (descriptor.replyEncoder == nullptr)
            ++missingEncoders;
        if (descriptor.presenter != nullptr) {
            InteractionRequest request;
            request.type = descriptor.type;
            (recorder.*(descriptor.presenter))(request);
        }
    }

    QJsonObject document;
    document.insert(QStringLiteral("schema_version"), 2);
    document.insert(QStringLiteral("total_commands"),
        static_cast<int>(descriptors().size()));
    document.insert(QStringLiteral("canonical_typed"), canonicalCount);
    document.insert(QStringLiteral("legacy_adapter"), legacyCount);
    document.insert(QStringLiteral("missing_builder"), missingBuilders);
    document.insert(QStringLiteral("missing_presenter"), missingPresenters);
    document.insert(QStringLiteral("missing_validator"), missingValidators);
    document.insert(QStringLiteral("missing_reply_encoder"), missingEncoders);
    document.insert(QStringLiteral("presenter_invocations"), recorder.invocationCount);
    document.insert(QStringLiteral("presented_types"), recorder.presentedTypes.size());
    document.insert(QStringLiteral("commands"), inventory());
    return document;
}
