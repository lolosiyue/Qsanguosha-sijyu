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

