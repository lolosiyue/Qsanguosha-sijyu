#include "structs.h"
#include "engine.h"
#include "room.h"
#include "json.h"
#include "card-lifetime-manager.h"
#include <QMetaType>

// 註冊技能多實例相關 metatype——確保 QVariant::toString() 回傳基礎技能名
static struct SkillInstanceMetaRegistrar {
    SkillInstanceMetaRegistrar() {
        qRegisterMetaType<SkillInstanceKey>("SkillInstanceKey");
        qRegisterMetaType<SkillInstance>("SkillInstance");
        qRegisterMetaType<SkillChangeStruct>("SkillChangeStruct");
        qRegisterMetaType<SkillAmountChangeStruct>("SkillAmountChangeStruct");
        QMetaType::registerConverter<SkillChangeStruct, QString>([](const SkillChangeStruct &scs) {
            return scs.skillName;
        });
    }
} _sir;

namespace {
void releaseOwnedCard(Card *card)
{
    if (card)
        card->deleteLater();
}
}

void CardUseStruct::OwnedCardPtr::reset(Card *card)
{
    if (!card) {
        m_ptr.clear();
        return;
    }
    m_ptr = QSharedPointer<Card>(card, &releaseOwnedCard);
}

CardMoveReason::CardMoveReason(const CardMoveReason &other) { *this = other; }
CardMoveReason::CardMoveReason(CardMoveReason &&other) noexcept { *this = other; }
CardMoveReason &CardMoveReason::operator=(const CardMoveReason &other)
{
    if (this == &other) return *this;
    const QVariant previousExtraData = m_extraData;
    globalCardLifetimeManager().releaseEventPayload(this);
    m_reason=other.m_reason; m_playerId=other.m_playerId; m_targetId=other.m_targetId;
    m_skillName=other.m_skillName; m_eventName=other.m_eventName; m_extraData=other.m_extraData;
    m_useStruct=other.m_useStruct;
    QByteArray error;
    if (!globalCardLifetimeManager().retainVariantPayload(this, m_extraData, &error)) {
        m_extraData = previousExtraData;
        globalCardLifetimeManager().retainVariantPayload(this, previousExtraData, nullptr);
    }
    return *this;
}
CardMoveReason &CardMoveReason::operator=(CardMoveReason &&other) noexcept { return operator=(static_cast<const CardMoveReason &>(other)); }
CardMoveReason::~CardMoveReason() { globalCardLifetimeManager().releaseEventPayload(this); }

CardsMoveOneTimeStruct::CardsMoveOneTimeStruct(const CardsMoveOneTimeStruct &other)
{
    *this = other;
}

CardsMoveOneTimeStruct::CardsMoveOneTimeStruct(CardsMoveOneTimeStruct &&other) noexcept
{
    *this = other;
}

CardsMoveOneTimeStruct &CardsMoveOneTimeStruct::operator=(const CardsMoveOneTimeStruct &other)
{
    if (this == &other) return *this;
    globalCardLifetimeManager().releaseEventPayload(this);
    card_ids = other.card_ids;
    from_places = other.from_places;
    to_place = other.to_place;
    reason = other.reason;
    from = other.from;
    to = other.to;
    from_pile_names = other.from_pile_names;
    to_pile_name = other.to_pile_name;
    open = other.open;
    is_last_handcard = other.is_last_handcard;
    last_hand_suits = other.last_hand_suits;
    shown_ids = other.shown_ids;
    broken_ids = other.broken_ids;
    return *this;
}

CardsMoveOneTimeStruct &CardsMoveOneTimeStruct::operator=(CardsMoveOneTimeStruct &&other) noexcept
{
    return operator=(static_cast<const CardsMoveOneTimeStruct &>(other));
}

CardsMoveOneTimeStruct::~CardsMoveOneTimeStruct()
{
    globalCardLifetimeManager().releaseEventPayload(this);
}

CardsMoveStruct::CardsMoveStruct(const CardsMoveStruct &other)
{
    *this = other;
}

CardsMoveStruct::CardsMoveStruct(CardsMoveStruct &&other) noexcept
{
    *this = other;
}

CardsMoveStruct &CardsMoveStruct::operator=(const CardsMoveStruct &other)
{
    if (this == &other) return *this;
    globalCardLifetimeManager().releaseEventPayload(this);
    card_ids = other.card_ids;
    to_place = other.to_place;
    from_place = other.from_place;
    from_player_name = other.from_player_name;
    to_player_name = other.to_player_name;
    from_pile_name = other.from_pile_name;
    to_pile_name = other.to_pile_name;
    from = other.from;
    to = other.to;
    reason = other.reason;
    open = other.open;
    is_last_handcard = other.is_last_handcard;
    last_hand_suits = other.last_hand_suits;
    return *this;
}

CardsMoveStruct &CardsMoveStruct::operator=(CardsMoveStruct &&other) noexcept
{
    return operator=(static_cast<const CardsMoveStruct &>(other));
}

CardsMoveStruct::~CardsMoveStruct()
{
    globalCardLifetimeManager().releaseEventPayload(this);
}

SkillAmountChangeStruct::SkillAmountChangeStruct()
    : source(nullptr), oldAmount(0), newAmount(0), canceled(false), resetToBase(false)
{
}

QVariant SkillAmountChangeStruct::toVariant() const
{
    return QVariant::fromValue(*this);
}

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

YishiStruct::YishiStruct()
    : initiator(nullptr), result("no_result"), started(false)
{
}

YishiStruct::YishiStruct(ServerPlayer *initiator, const QList<ServerPlayer *> &participants, const QString &reason)
    : initiator(initiator), participants(participants), reason(reason), result("no_result"), started(false)
{
    foreach (ServerPlayer *participant, participants) {
        Q_UNUSED(participant);
        card_counts << 1;
        card_ids << -1;
        opinions << "no_opinion";
    }
}

int YishiStruct::participantIndex(ServerPlayer *player) const
{
    return participants.indexOf(player);
}

int YishiStruct::cardOffset(int participant_index) const
{
    int offset = 0;
    for (int i = 0; i < participant_index && i < card_counts.length(); ++i)
        offset += qMax(0, card_counts.at(i));
    return offset;
}

bool YishiStruct::containsParticipant(ServerPlayer *player) const
{
    return participantIndex(player) >= 0;
}

int YishiStruct::getCardCount(ServerPlayer *player) const
{
    const int index = participantIndex(player);
    return index >= 0 && index < card_counts.length() ? card_counts.at(index) : 0;
}

void YishiStruct::setCardCount(ServerPlayer *player, int count)
{
    const int index = participantIndex(player);
    if (index < 0 || index >= card_counts.length())
        return;

    count = qMax(0, count);
    const int old_count = qMax(0, card_counts.at(index));
    const int offset = cardOffset(index);
    for (int i = 0; i < old_count && offset < card_ids.length(); ++i)
        card_ids.removeAt(offset);
    for (int i = 0; i < old_count && offset < opinions.length(); ++i)
        opinions.removeAt(offset);
    for (int i = 0; i < count; ++i) {
        card_ids.insert(offset + i, -1);
        opinions.insert(offset + i, "no_opinion");
    }
    card_counts[index] = count;
}

QList<int> YishiStruct::getCards(ServerPlayer *player) const
{
    const int index = participantIndex(player);
    if (index < 0 || index >= card_counts.length())
        return QList<int>();
    return card_ids.mid(cardOffset(index), qMax(0, card_counts.at(index)));
}

void YishiStruct::setCards(ServerPlayer *player, const QList<int> &ids)
{
    const int index = participantIndex(player);
    if (index < 0)
        return;
    setCardCount(player, ids.length());
    const int offset = cardOffset(index);
    for (int i = 0; i < ids.length(); ++i)
        card_ids[offset + i] = ids.at(i);
}

QStringList YishiStruct::getOpinions(ServerPlayer *player) const
{
    const int index = participantIndex(player);
    if (index < 0 || index >= card_counts.length())
        return QStringList();
    return opinions.mid(cardOffset(index), qMax(0, card_counts.at(index)));
}

QString YishiStruct::getOpinionString(ServerPlayer *player) const
{
    return getOpinions(player).join("|");
}

void YishiStruct::setOpinions(ServerPlayer *player, const QStringList &values)
{
    const int index = participantIndex(player);
    if (index < 0 || index >= card_counts.length())
        return;

    const int count = qMax(0, card_counts.at(index));
    const int offset = cardOffset(index);
    for (int i = 0; i < count; ++i) {
        const QString value = values.isEmpty() ? "no_opinion"
            : values.at(qMin(i, values.length() - 1));
        opinions[offset + i] = value == "red" || value == "black" ? value : "no_opinion";
    }
}

void YishiStruct::setOpinion(ServerPlayer *player, const QString &opinion)
{
    setOpinions(player, QStringList() << opinion);
}

bool YishiStruct::hasOpinion(ServerPlayer *player, const QString &opinion) const
{
    return opinion == "red" || opinion == "black"
        ? getOpinions(player).contains(opinion) : false;
}

bool YishiStruct::sharesOpinion(ServerPlayer *first, ServerPlayer *second) const
{
    const QStringList first_opinions = getOpinions(first);
    const QStringList second_opinions = getOpinions(second);
    foreach (const QString &opinion, first_opinions) {
        if ((opinion == "red" || opinion == "black") && second_opinions.contains(opinion))
            return true;
    }
    return false;
}

bool YishiStruct::allOpinionsSame() const
{
    const int red = opinionCount("red");
    const int black = opinionCount("black");
    return red + black > 0 && (red == 0 || black == 0);
}

int YishiStruct::opinionCount(const QString &opinion) const
{
    return opinions.count(opinion);
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
