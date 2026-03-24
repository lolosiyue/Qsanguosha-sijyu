#include "structs.h"
#include "engine.h"
#include "json.h"

bool CardsMoveStruct::tryParse(const QVariant &arg)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() < 8) return false;
	JsonUtils::tryParse(args[0], card_ids);
    from_place = (Player::Place)args[1].toInt();
    to_place = (Player::Place)args[2].toInt();
    from_player_name = args[3].toString();
    to_player_name = args[4].toString();
    from_pile_name = args[5].toString();
    to_pile_name = args[6].toString();
    reason.tryParse(args[7]);
	open = args[8].toBool();
	if (!open){
        for (int i = 0; i < card_ids.length(); i++){
			if(from_place==Player::PlaceSpecial||from_place==Player::DrawPile){
				if(Sanguosha->getCard(card_ids[i])->hasFlag("visible")) continue;
			}
			card_ids[i] = Card::S_UNKNOWN_CARD_ID;
		}
	}
    return true;
}

QVariant CardsMoveStruct::toVariant() const
{
    JsonArray arg;
	arg << JsonUtils::toJsonArray(card_ids);
    arg << from_place;
    arg << to_place;
    arg << from_player_name;
    arg << to_player_name;
    arg << from_pile_name;
    arg << to_pile_name;
    arg << reason.toVariant();
    arg << open;
    return arg;
}

bool CardMoveReason::tryParse(const QVariant &arg)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() < 5) return false;

    m_reason = args[0].toInt();
    m_playerId = args[1].toString();
    m_skillName = args[2].toString();
    m_eventName = args[3].toString();
    m_targetId = args[4].toString();
    return true;
}

QVariant CardMoveReason::toVariant() const
{
    JsonArray result;
    result << m_reason;
    result << m_playerId;
    result << m_skillName;
    result << m_eventName;
    result << m_targetId;
    return result;
}

// GameModeStruct implementation
GameModeStruct::GameModeStruct()
    : player_count(-1), is_scenario(false), is_mini_scene(false), shuffle_roles(true), lord_welfare(true)
{
}

GameModeStruct::GameModeStruct(const QString &mode_id, const QString &display_name,
                               int player_count, const QString &roles)
    : mode_id(mode_id), display_name(display_name), 
      player_count(player_count), roles(roles), 
      is_scenario(false), is_mini_scene(false), shuffle_roles(true), lord_welfare(true)
{
    is_mini_scene = mode_id.contains("_mini_");
    is_scenario = !mode_id.isEmpty() && !mode_id.contains(QRegExp("^(0[2-9]|1[0-6])p[dz]*$")) && !is_mini_scene;

    if (display_name.isEmpty() && player_count == -1 && !mode_id.isEmpty()) {
        if (is_scenario || mode_id == "custom_scenario") {
            if (mode_id == "custom_scenario") {
                this->display_name = "Custom Scenario";
                this->player_count = 0;
            } else {
                this->display_name = mode_id;
                this->player_count = 0;
            }
        }
    }
}

bool GameModeStruct::isValid() const
{
    return !mode_id.isEmpty();
}

bool GameModeStruct::operator==(const GameModeStruct &other) const
{
    return mode_id == other.mode_id && rule_mode == other.rule_mode;
}

bool GameModeStruct::operator!=(const GameModeStruct &other) const
{
    return !(*this == other);
}

QString GameModeStruct::toString() const
{
    return QString("GameMode{id=%1, name=%2, players=%3, roles=%4, rule=%5, shuffle_roles=%6}")
            .arg(mode_id, display_name)
            .arg(player_count)
            .arg(roles, rule_mode)
            .arg(shuffle_roles ? "true" : "false");
}
