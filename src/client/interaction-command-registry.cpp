#include "interaction-command-registry.h"

using namespace QSanProtocol;

const std::array<InteractionCommandDescriptor, 29> &InteractionCommandRegistry::descriptors()
{
    using Encoder = InteractionReplyEncoder;
    static const std::array<InteractionCommandDescriptor, 29> values = {{
        {S_COMMAND_CHOOSE_ROLE, InteractionType::ChooseRole, InteractionResponseShape::Assignment, &Encoder::assignment, "CHOOSE_ROLE", "assignment", "choose_role"},
        {S_COMMAND_CHOOSE_GENERAL, InteractionType::ChooseGeneral, InteractionResponseShape::Option, &Encoder::optionString, "CHOOSE_GENERAL", "option_string", "choose_general"},
        {S_COMMAND_CHOOSE_DIRECTION, InteractionType::ChooseDirection, InteractionResponseShape::Option, &Encoder::optionString, "CHOOSE_DIRECTION", "option_string", "choose_direction"},
        {S_COMMAND_EXCHANGE_CARD, InteractionType::ExchangeCard, InteractionResponseShape::Cards, &Encoder::discardCards, "EXCHANGE_CARD", "discard_cards", "exchange"},
        {S_COMMAND_ASK_PEACH, InteractionType::AskPeach, InteractionResponseShape::Cards, &Encoder::cardResponse, "ASK_PEACH", "card_response", "ask_peach"},
        {S_COMMAND_SKILL_GUANXING, InteractionType::SkillGuanxing, InteractionResponseShape::Rearrangement, &Encoder::rearrangement, "SKILL_GUANXING", "rearrangement", "guanxing"},
        {S_COMMAND_SKILL_GONGXIN, InteractionType::SkillGongxin, InteractionResponseShape::Cards, &Encoder::cardId, "SKILL_GONGXIN", "card_id", "gongxin"},
        {S_COMMAND_SKILL_YIJI, InteractionType::SkillYiji, InteractionResponseShape::Distribution, &Encoder::distribution, "SKILL_YIJI", "distribution", "yiji"},
        {S_COMMAND_PLAY_CARD, InteractionType::PlayCard, InteractionResponseShape::Cards, &Encoder::cardResponse, "PLAY_CARD", "card_response", "play_card"},
        {S_COMMAND_RESPONSE_CARD, InteractionType::ResponseCard, InteractionResponseShape::Cards, &Encoder::cardResponse, "RESPONSE_CARD", "card_response", "card_response"},
        {S_COMMAND_DISCARD_CARD, InteractionType::DiscardCard, InteractionResponseShape::Cards, &Encoder::discardCards, "DISCARD_CARD", "discard_cards", "discard"},
        {S_COMMAND_MULTIPLE_CHOICE, InteractionType::Choice, InteractionResponseShape::Option, &Encoder::optionString, "MULTIPLE_CHOICE", "option_string", "choice"},
        {S_COMMAND_CHOOSE_SUIT, InteractionType::ChooseSuit, InteractionResponseShape::Option, &Encoder::optionString, "CHOOSE_SUIT", "option_string", "choose_suit"},
        {S_COMMAND_CHOOSE_KINGDOM, InteractionType::ChooseKingdom, InteractionResponseShape::Option, &Encoder::optionString, "CHOOSE_KINGDOM", "option_string", "choose_kingdom"},
        {S_COMMAND_CHOOSE_PLAYER, InteractionType::ChoosePlayer, InteractionResponseShape::Players, &Encoder::playersJoined, "CHOOSE_PLAYER", "players_joined", "player_chosen"},
        {S_COMMAND_INVOKE_SKILL, InteractionType::SkillInvoke, InteractionResponseShape::Option, &Encoder::optionBool, "INVOKE_SKILL", "option_bool", "skill_invoke"},
        {S_COMMAND_TRIGGER_ORDER, InteractionType::TriggerOrder, InteractionResponseShape::Option, &Encoder::optionString, "TRIGGER_ORDER", "option_string", "trigger_order"},
        {S_COMMAND_NULLIFICATION, InteractionType::Nullification, InteractionResponseShape::Cards, &Encoder::cardResponse, "NULLIFICATION", "card_response", "nullification"},
        {S_COMMAND_SHOW_CARD, InteractionType::ShowCard, InteractionResponseShape::Cards, &Encoder::cardResponse, "SHOW_CARD", "card_response", "show_card"},
        {S_COMMAND_AMAZING_GRACE, InteractionType::AmazingGrace, InteractionResponseShape::Cards, &Encoder::amazingGraceCardId, "AMAZING_GRACE", "amazing_grace_card_id", "ask_for_ag"},
        {S_COMMAND_PINDIAN, InteractionType::Pindian, InteractionResponseShape::Cards, &Encoder::cardResponse, "PINDIAN", "card_response", "pindian"},
        {S_COMMAND_CHOOSE_CARD, InteractionType::ChooseCard, InteractionResponseShape::Cards, &Encoder::cardId, "CHOOSE_CARD", "card_id", "card_chosen"},
        {S_COMMAND_CHOOSE_ORDER, InteractionType::ChooseOrder, InteractionResponseShape::Option, &Encoder::optionInt, "CHOOSE_ORDER", "option_int", "choose_order"},
        {S_COMMAND_CHOOSE_ROLE_3V3, InteractionType::ChooseRole3v3, InteractionResponseShape::Option, &Encoder::optionString, "CHOOSE_ROLE_3V3", "option_string", "choose_role_3v3"},
        {S_COMMAND_SURRENDER, InteractionType::Surrender, InteractionResponseShape::Option, &Encoder::optionBool, "SURRENDER", "option_bool", "surrender"},
        {S_COMMAND_LUCK_CARD, InteractionType::LuckCard, InteractionResponseShape::Option, &Encoder::optionBool, "LUCK_CARD", "option_bool", "luck_card"},
        {S_COMMAND_ASK_GENERAL, InteractionType::AskGeneral, InteractionResponseShape::Option, &Encoder::optionString, "ASK_GENERAL", "option_string", "ask_general"},
        {S_COMMAND_ARRANGE_GENERAL, InteractionType::ArrangeGeneral, InteractionResponseShape::GeneralArrangement, &Encoder::generalArrangement, "ARRANGE_GENERAL", "general_arrangement", "arrange_general"},
        {S_COMMAND_QML_INTERACT, InteractionType::QmlInteract, InteractionResponseShape::Custom, &Encoder::custom, "QML_INTERACT", "custom", "qml_interact"}
    }};
    return values;
}

const InteractionCommandDescriptor *InteractionCommandRegistry::find(CommandType command)
{
    for (const InteractionCommandDescriptor &descriptor : descriptors()) {
        if (descriptor.command == command)
            return &descriptor;
    }
    return nullptr;
}

const InteractionCommandDescriptor *InteractionCommandRegistry::find(InteractionType type)
{
    for (const InteractionCommandDescriptor &descriptor : descriptors()) {
        if (descriptor.type == type)
            return &descriptor;
    }
    return nullptr;
}
