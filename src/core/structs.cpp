#include "structs.h"
#include "engine.h"
#include "room.h"
#include "json.h"
#include <QMetaType>

// 註冊技能多實例相關 metatype——確保 QVariant::toString() 回傳基礎技能名
static struct SkillInstanceMetaRegistrar {
    SkillInstanceMetaRegistrar() {
        qRegisterMetaType<SkillInstanceKey>("SkillInstanceKey");
        qRegisterMetaType<SkillInstance>("SkillInstance");
        qRegisterMetaType<SkillChangeStruct>("SkillChangeStruct");
        QMetaType::registerConverter<SkillChangeStruct, QString>([](const SkillChangeStruct &scs) {
            return scs.skillName;
        });
    }
} _sir;

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
    : player_count(-1), is_scenario(false), is_mini_scene(false), shuffle_seats(true), lord_welfare(true)
{
}

GameModeStruct::GameModeStruct(const QString &mode_id, const QString &display_name,
                               int player_count, const QString &roles)
    : mode_id(mode_id), display_name(display_name), 
      player_count(player_count), roles(roles), 
      is_scenario(false), is_mini_scene(false), shuffle_seats(true), lord_welfare(true)
{
    is_mini_scene = mode_id.contains("_mini_");
}

bool GameModeStruct::isValid() const
{
    return !mode_id.isEmpty();
}

bool GameModeStruct::operator==(const GameModeStruct &other) const
{
    return mode_id == other.mode_id;
}

bool GameModeStruct::operator!=(const GameModeStruct &other) const
{
    return !(*this == other);
}

QString GameModeStruct::toString() const
{
    return QString("GameMode{id=%1, name=%2, players=%3, roles=%4, shuffle_seats=%5}")
            .arg(mode_id, display_name)
            .arg(player_count)
            .arg(roles)
            .arg(shuffle_seats ? "true" : "false");
}

ShownCardChangedStruct::ShownCardChangedStruct()
    : player(nullptr), shown(false), moveFromHand(false)
{
}

BrokenEquipChangedStruct::BrokenEquipChangedStruct()
    : player(nullptr), broken(false), moveFromEquip(false)
{
}

ChoiceData::ChoiceData()
    : player(nullptr), canceled(false)
{
}

QVariant ChoiceData::toVariant() const
{
    JsonArray arg;
    arg << (player ? player->objectName() : QString());
    arg << skill_name;
    arg << choices;
    arg << except_choices;
    arg << tip;
    arg << forced_answer;
    arg << canceled;
    return arg;
}

// SkillInstanceKey
QString SkillInstanceKey::toString() const
{
    if (instanceID <= 0)
        return skillName;
    return QString("%1#%2").arg(skillName).arg(instanceID);
}

// SkillChangeStruct
QVariant SkillChangeStruct::toVariant() const
{
    return QVariant::fromValue(*this);
}

bool SkillChangeStruct::tryParse(const QVariant &arg)
{
    if (!arg.isValid() || arg.isNull())
        return false;
    if (arg.canConvert<SkillChangeStruct>()) {
        *this = arg.value<SkillChangeStruct>();
        return true;
    }
    return false;
}

bool ChoiceData::tryParse(const QVariant &arg)
{
    JsonArray args = arg.value<JsonArray>();
    if (args.size() < 7) return false;

    QString playerName = args[0].toString();
    if (!playerName.isEmpty()) {
        Room *room = Sanguosha->currentRoom();
        if (room) {
            player = room->findPlayerByObjectName(playerName);
        }
    }
    skill_name = args[1].toString();
    choices = args[2].toString();
    except_choices = args[3].toString();
    tip = args[4].toString();
    forced_answer = args[5].toString();
    canceled = args[6].toBool();
    return true;
}
