#include "structs.h"
#include "engine.h"
#include "room.h"
#include "json.h"
#include "card-lifetime-manager.h"
#include "skill-instance-utils.h"
#include "protocol/protocol-message-utils.h"
#include <QMetaType>
#include <utility>

// Register metatypes for multi-instance skills so QVariant::toString() returns the base skill name.
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

bool isKnownCardMoveReason(int reason)
{
    switch (reason) {
    case CardMoveReason::S_REASON_UNKNOWN:
    case CardMoveReason::S_REASON_USE:
    case CardMoveReason::S_REASON_RESPONSE:
    case CardMoveReason::S_REASON_DISCARD:
    case CardMoveReason::S_REASON_RECAST:
    case CardMoveReason::S_REASON_PINDIAN:
    case CardMoveReason::S_REASON_DRAW:
    case CardMoveReason::S_REASON_GOTCARD:
    case CardMoveReason::S_REASON_SHOW:
    case CardMoveReason::S_REASON_TRANSFER:
    case CardMoveReason::S_REASON_PUT:
    case CardMoveReason::S_REASON_LETUSE:
    case CardMoveReason::S_REASON_RETRIAL:
    case CardMoveReason::S_REASON_RULEDISCARD:
    case CardMoveReason::S_REASON_THROW:
    case CardMoveReason::S_REASON_DISMANTLE:
    case CardMoveReason::S_REASON_GIVE:
    case CardMoveReason::S_REASON_EXTRACTION:
    case CardMoveReason::S_REASON_GOTBACK:
    case CardMoveReason::S_REASON_RECYCLE:
    case CardMoveReason::S_REASON_ROB:
    case CardMoveReason::S_REASON_PREVIEWGIVE:
    case CardMoveReason::S_REASON_EXCLUSIVE:
    case CardMoveReason::S_REASON_TURNOVER:
    case CardMoveReason::S_REASON_JUDGE:
    case CardMoveReason::S_REASON_PREVIEW:
    case CardMoveReason::S_REASON_DEMONSTRATE:
    case CardMoveReason::S_REASON_SWAP:
    case CardMoveReason::S_REASON_OVERRIDE:
    case CardMoveReason::S_REASON_EXCHANGE_FROM_PILE:
    case CardMoveReason::S_REASON_NATURAL_ENTER:
    case CardMoveReason::S_REASON_REMOVE_FROM_PILE:
    case CardMoveReason::S_REASON_JUDGEDONE:
    case CardMoveReason::S_REASON_CHANGE_EQUIP:
    case CardMoveReason::S_REASON_SHUFFLE:
    case CardMoveReason::S_REASON_PUT_END:
    //  Used as a reason by the game rule and a few skills; see structs.h.
    case CardMoveReason::S_MASK_BASIC_REASON:
        return true;
    default:
        return false;
    }
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
    origin_from = other.origin_from;
    origin_to = other.origin_to;
    origin_from_places = other.origin_from_places;
    origin_to_place = other.origin_to_place;
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
    m_equipmentSourcesBefore = other.m_equipmentSourcesBefore;
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
    if (arg.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = arg.toMap();
    CardsMoveStruct parsed;
    int parsedFromPlace = 0;
    int parsedToPlace = 0;
    QString fromPlayer;
    QString toPlayer;
    QString fromPile;
    QString toPile;
    if (!JsonUtils::tryParse(object.value(QStringLiteral("card_ids")), parsed.card_ids)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("from_place")), parsedFromPlace)
        || !ProtocolMessageUtils::tryParseInt(object.value(QStringLiteral("to_place")), parsedToPlace)
        || parsedFromPlace < Player::PlaceHand || parsedFromPlace > Player::PlaceWuGu
        || parsedToPlace < Player::PlaceHand || parsedToPlace > Player::PlaceWuGu
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("from_player")), fromPlayer)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("to_player")), toPlayer)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("from_pile")), fromPile)
        || !ProtocolMessageUtils::tryParseString(object.value(QStringLiteral("to_pile")), toPile)
        || !ProtocolMessageUtils::tryParseBool(object.value(QStringLiteral("open")), parsed.open)
        || !parsed.reason.tryParse(object.value(QStringLiteral("reason")))) {
        return false;
    }
    parsed.from_place = static_cast<Player::Place>(parsedFromPlace);
    parsed.to_place = static_cast<Player::Place>(parsedToPlace);
    parsed.from_player_name = fromPlayer;
    parsed.to_player_name = toPlayer;
    parsed.from_pile_name = fromPile;
    parsed.to_pile_name = toPile;
	if (!parsed.open){
        for (int i = 0; i < parsed.card_ids.length(); i++){
			if(parsed.from_place==Player::PlaceSpecial||parsed.from_place==Player::DrawPile){
				const int cardId = parsed.card_ids[i];
				const Card *card = cardId >= 0 && Sanguosha
					? Sanguosha->getCard(cardId) : nullptr;
				if(card && card->hasFlag("visible")) continue;
			}
			parsed.card_ids[i] = Card::S_UNKNOWN_CARD_ID;
		}
	}
    *this = parsed;
    return true;
}

QVariant CardsMoveStruct::toVariant() const
{
    return QVariantMap{
        {QStringLiteral("card_ids"), JsonUtils::toJsonArray(card_ids)},
        {QStringLiteral("from_place"), static_cast<int>(from_place)},
        {QStringLiteral("to_place"), static_cast<int>(to_place)},
        {QStringLiteral("from_player"), from_player_name},
        {QStringLiteral("to_player"), to_player_name},
        {QStringLiteral("from_pile"), from_pile_name},
        {QStringLiteral("to_pile"), to_pile_name},
        {QStringLiteral("reason"), reason.toVariant()},
        {QStringLiteral("open"), open}
    };
}

bool CardMoveReason::tryParse(const QVariant &arg)
{
    if (arg.userType() != QMetaType::QVariantMap)
        return false;
    const QVariantMap object = arg.toMap();
    int parsedReason = 0;
    QString playerId;
    QString skillName;
    QString eventName;
    QString targetId;
    if (!ProtocolMessageUtils::tryParseInt(
            object.value(QStringLiteral("reason")), parsedReason)
        || !isKnownCardMoveReason(parsedReason)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("player_id")), playerId)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("skill_name")), skillName)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("event_name")), eventName)
        || !ProtocolMessageUtils::tryParseString(
            object.value(QStringLiteral("target_id")), targetId)) {
        return false;
    }
    m_reason = parsedReason;
    m_playerId = playerId;
    m_skillName = skillName;
    m_eventName = eventName;
    m_targetId = targetId;
    return true;
}

QVariant CardMoveReason::toVariant() const
{
    return QVariantMap{
        {QStringLiteral("reason"), m_reason},
        {QStringLiteral("player_id"), m_playerId},
        {QStringLiteral("skill_name"), m_skillName},
        {QStringLiteral("event_name"), m_eventName},
        {QStringLiteral("target_id"), m_targetId}
    };
}

// GameModeStruct implementation
GameModeStruct::GameModeStruct()
    : player_count(-1), reward_policy(QStringLiteral("identity")), win_policy(QStringLiteral("identity")),
      is_scenario(false), is_mini_scene(false), shuffle_seats(true), lord_welfare(true)
{
}

GameModeStruct::GameModeStruct(const QString &mode_id, const QString &display_name,
                               int player_count, const QString &roles)
    : mode_id(mode_id), display_name(display_name),
      player_count(player_count), roles(roles),
      reward_policy(QStringLiteral("identity")), win_policy(QStringLiteral("identity")),
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
        opinion_counts << 1;
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

int YishiStruct::opinionOffset(int participant_index) const
{
    int offset = 0;
    for (int i = 0; i < participant_index && i < opinion_counts.length(); ++i)
        offset += qMax(0, opinion_counts.at(i));
    return offset;
}

void YishiStruct::removeParticipant(ServerPlayer *player)
{
    const int index = participantIndex(player);
    if (index < 0)
        return;

    const int card_offset = cardOffset(index);
    for (int i = 0; i < card_counts.at(index); ++i)
        card_ids.removeAt(card_offset);
    const int opinion_offset = opinionOffset(index);
    for (int i = 0; i < opinion_counts.at(index); ++i)
        opinions.removeAt(opinion_offset);
    card_counts.removeAt(index);
    opinion_counts.removeAt(index);
    participants.removeAt(index);
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
    const QList<int> selected = getCards(player);
    for (int i = 0; i < old_count && offset < card_ids.length(); ++i)
        card_ids.removeAt(offset);
    for (int i = 0; i < count; ++i)
        card_ids.insert(offset + i, i < selected.length() ? selected.at(i) : -1);
    card_counts[index] = count;
    QStringList values;
    for (int i = 0; i < count; ++i)
        values << "no_opinion";
    setOpinions(player, values);
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
    if (index < 0 || index >= opinion_counts.length())
        return QStringList();
    return opinions.mid(opinionOffset(index), qMax(0, opinion_counts.at(index)));
}

QString YishiStruct::getOpinionString(ServerPlayer *player) const
{
    return getOpinions(player).join("|");
}

void YishiStruct::setOpinions(ServerPlayer *player, const QStringList &values)
{
    const int index = participantIndex(player);
    if (index < 0 || index >= opinion_counts.length())
        return;

    // An opinion can exist without a revealed card, or count as several votes.
    const int offset = opinionOffset(index);
    for (int i = 0; i < opinion_counts.at(index); ++i)
        opinions.removeAt(offset);
    opinion_counts[index] = values.length();
    for (int i = 0; i < values.length(); ++i) {
        const QString value = values.at(i);
        opinions.insert(offset + i, value == "red" || value == "black" ? value : "no_opinion");
    }
}

void YishiStruct::setOpinion(ServerPlayer *player, const QString &opinion)
{
    QStringList values;
    const int count = qMax(1, getOpinions(player).length());
    for (int i = 0; i < count; ++i)
        values << opinion;
    setOpinions(player, values);
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
    if (participants.isEmpty())
        return false;
    foreach (int count, opinion_counts) {
        if (count <= 0)
            return false;
    }
    const int red = opinionCount("red");
    const int black = opinionCount("black");
    // Every participant must have a valid opinion; an abstention is not assent.
    return red + black > 0 && red + black == opinions.length() && (red == 0 || black == 0);
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

DamageStruct::DamageStruct()
	: from(nullptr), to(nullptr), card(nullptr), damage(1), nature(Normal), chain(false),
	transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const Card*card, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(card), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const QString &reason, ServerPlayer*from, ServerPlayer*to, int damage, DamageStruct::Nature nature)
	: from(from), to(to), card(nullptr), damage(damage), nature(nature),
	chain(false), transfer(false), by_user(true), reason(reason), prevented(false), ignore_hujia(false)
{
}

DamageStruct::DamageStruct(const DamageStruct &other) { *this = other; }
DamageStruct::DamageStruct(DamageStruct &&other) noexcept { *this = other; }
DamageStruct &DamageStruct::operator=(const DamageStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	from=other.from; to=other.to; card=other.card; damage=other.damage; nature=other.nature;
	chain=other.chain; transfer=other.transfer; by_user=other.by_user; reason=other.reason;
	transfer_reason=other.transfer_reason; prevented=other.prevented; tips=other.tips; ignore_hujia=other.ignore_hujia;
	globalCardLifetimeManager().retainEventPayload(this, {card});
	return *this;
}
DamageStruct &DamageStruct::operator=(DamageStruct &&other) noexcept { return operator=(static_cast<const DamageStruct &>(other)); }
DamageStruct::~DamageStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

QString DamageStruct::getReason() const
{
	if (reason.isEmpty()&&card)
		return card->objectName();
	return reason;
}

CardEffectStruct::CardEffectStruct()
	: card(nullptr), offset_card(nullptr), offset_num(1), from(nullptr), to(nullptr), multiple(false),
	nullified(false), no_respond(false), no_offset(false), extra_effect(0), skillExecutionID(0)
{
}

CardEffectStruct::CardEffectStruct(const CardEffectStruct &other) { *this = other; }
CardEffectStruct::CardEffectStruct(CardEffectStruct &&other) noexcept { *this = other; }
CardEffectStruct &CardEffectStruct::operator=(const CardEffectStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; offset_card=other.offset_card; offset_num=other.offset_num; from=other.from; to=other.to;
	multiple=other.multiple; nullified=other.nullified; no_respond=other.no_respond; no_offset=other.no_offset;
	extra_effect=other.extra_effect; skillExecutionID=other.skillExecutionID;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	globalCardLifetimeManager().retainEventPayload(this, {card, offset_card});
	return *this;
}
CardEffectStruct &CardEffectStruct::operator=(CardEffectStruct &&other) noexcept { return operator=(static_cast<const CardEffectStruct &>(other)); }
CardEffectStruct::~CardEffectStruct() { globalCardLifetimeManager().releaseEventPayload(this); }
void CardEffectStruct::setSkillUseContext(const CardUseStruct &use)
{
    skillExecutionID = use.skillExecutionID;
    physicalEquipSource = use.physicalEquipSource;
    m_acceptedSkillEffectCard = use.isAcceptedSkillEffectCard();
}


SlashEffectStruct::SlashEffectStruct()
	: jink_num(1), slash(nullptr), jink(nullptr), from(nullptr), to(nullptr), drank(0), nature(DamageStruct::Normal), multiple(false), nullified(false),
	no_respond(false), no_offset(false)
{
}

SlashEffectStruct::SlashEffectStruct(const SlashEffectStruct &other) { *this = other; }
SlashEffectStruct::SlashEffectStruct(SlashEffectStruct &&other) noexcept { *this = other; }
SlashEffectStruct &SlashEffectStruct::operator=(const SlashEffectStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	jink_num=other.jink_num; slash=other.slash; jink=other.jink; from=other.from; to=other.to; drank=other.drank;
	nature=other.nature; multiple=other.multiple; nullified=other.nullified; no_respond=other.no_respond; no_offset=other.no_offset;
	globalCardLifetimeManager().retainEventPayload(this, {slash, jink});
	return *this;
}
SlashEffectStruct &SlashEffectStruct::operator=(SlashEffectStruct &&other) noexcept { return operator=(static_cast<const SlashEffectStruct &>(other)); }
SlashEffectStruct::~SlashEffectStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

DyingStruct::DyingStruct()
	: who(nullptr), damage(nullptr)
{
}

DeathStruct::DeathStruct()
	: who(nullptr), damage(nullptr)
{
}

RecoverStruct::RecoverStruct(ServerPlayer*who, const Card*card, int recover, const QString &reason)
	: recover(recover), who(who), card(card), reason(reason)
{
}

RecoverStruct::RecoverStruct(const QString &reason, ServerPlayer*who, int recover)
	: recover(recover), who(who), card(nullptr), reason(reason)
{
}

RecoverStruct::RecoverStruct(const RecoverStruct &other) { *this = other; }
RecoverStruct::RecoverStruct(RecoverStruct &&other) noexcept { *this = other; }
RecoverStruct &RecoverStruct::operator=(const RecoverStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	recover=other.recover; who=other.who; card=other.card; reason=other.reason;
	globalCardLifetimeManager().retainEventPayload(this, {card});
	return *this;
}
RecoverStruct &RecoverStruct::operator=(RecoverStruct &&other) noexcept { return operator=(static_cast<const RecoverStruct &>(other)); }
RecoverStruct::~RecoverStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

MarkStruct::MarkStruct()
	: who(nullptr), count(1), gain(-1)
{
}

DrawStruct::DrawStruct()
	: historyEventId(0), who(nullptr), num(1), top(true), visible(false)
{
}

HpLostStruct::HpLostStruct()
	: from(nullptr), to(nullptr), lose(1), ignore_hujia(true)
{
}

HpLostStruct::HpLostStruct(ServerPlayer*to, int lose, const QString &reason, ServerPlayer*from, bool ignore_hujia)
	: from(from), to(to), reason(reason), lose(lose), ignore_hujia(ignore_hujia)
{
}

MaxHpStruct::MaxHpStruct()
	: who(nullptr), change(0)
{
}

MaxHpStruct::MaxHpStruct(ServerPlayer*who, int change, const QString &reason)
	: who(who), change(change), reason(reason)
{
}

PindianStruct::PindianStruct()
	: from(nullptr), to(nullptr), from_card(nullptr), to_card(nullptr), success(false)
{
}

bool PindianStruct::isSuccess() const
{
	return success;
}

JudgeStruct::JudgeStruct()
	: who(nullptr), card(nullptr), pattern("."), good(true), time_consuming(false),
	negative(false), play_animation(true), throw_card(true), retrial_by_response(nullptr),
	_m_result(TRIAL_RESULT_UNKNOWN)
{
}

bool JudgeStruct::isEffected() const
{
	return negative ? isBad() : isGood();
}

void JudgeStruct::updateResult()
{
	if (good == Sanguosha->matchExpPattern(pattern,who,card))
		_m_result = TRIAL_RESULT_GOOD;
	else
		_m_result = TRIAL_RESULT_BAD;
}

bool JudgeStruct::isGood() const
{
	//Q_ASSERT(_m_result != TRIAL_RESULT_UNKNOWN);
	return _m_result == TRIAL_RESULT_GOOD;
}

bool JudgeStruct::isBad() const
{
	return !isGood();
}

bool JudgeStruct::isGood(const Card*card) const
{
	//Q_ASSERT(card);
	return good == Sanguosha->matchExpPattern(pattern,who,card);
}

PhaseChangeStruct::PhaseChangeStruct()
	: from(Player::NotActive), to(Player::NotActive)
{
}

CardUseStruct::CardUseStruct()
	: card(nullptr), from(nullptr), m_isOwnerUse(true), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(nullptr), who(nullptr), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, QList<ServerPlayer*> to, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), to(to), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(whocard), who(who), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard});
}

CardUseStruct::CardUseStruct(const Card*card, ServerPlayer*from, ServerPlayer*target, bool isOwnerUse, const Card*whocard, ServerPlayer*who)
	: card(card), from(from), m_isOwnerUse(isOwnerUse), m_addHistory(true), m_isHandcard(false), m_validateTargets(false), whocard(whocard), who(who), extra_use(0), bypass_cost(false), skipSkillEffect(false), hasSkillActivationRequest(false), skillExecutionID(0)
{
	if (target) this->to << target;
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard});
}

CardUseStruct::CardUseStruct(const CardUseStruct &other) { *this = other; }
CardUseStruct::CardUseStruct(CardUseStruct &&other) noexcept { *this = std::move(other); }
CardUseStruct &CardUseStruct::operator=(const CardUseStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; from=other.from; to=other.to; m_isOwnerUse=other.m_isOwnerUse; m_addHistory=other.m_addHistory;
	m_isHandcard=other.m_isHandcard; m_validateTargets=other.m_validateTargets; nullified_list=other.nullified_list;
	whocard=other.whocard; who=other.who; no_respond_list=other.no_respond_list; no_offset_list=other.no_offset_list;
	extra_use=other.extra_use; bypass_cost=other.bypass_cost; skipSkillEffect=other.skipSkillEffect; cardFinished=other.cardFinished;
	hasSkillActivationRequest=other.hasSkillActivationRequest; sourceRef=other.sourceRef; activationRef=other.activationRef;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	skillExecutionID=other.skillExecutionID; m_ownedCard=other.m_ownedCard;
	targetModReveal=other.targetModReveal;
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard, m_ownedCard.data()});
	return *this;
}
CardUseStruct &CardUseStruct::operator=(CardUseStruct &&other) noexcept
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	card=other.card; from=other.from; to=std::move(other.to); m_isOwnerUse=other.m_isOwnerUse; m_addHistory=other.m_addHistory;
	m_isHandcard=other.m_isHandcard; m_validateTargets=other.m_validateTargets; nullified_list=std::move(other.nullified_list);
	whocard=other.whocard; who=other.who; no_respond_list=std::move(other.no_respond_list); no_offset_list=std::move(other.no_offset_list);
	extra_use=other.extra_use; bypass_cost=other.bypass_cost; skipSkillEffect=other.skipSkillEffect; cardFinished=other.cardFinished;
	hasSkillActivationRequest=other.hasSkillActivationRequest; sourceRef=other.sourceRef; activationRef=other.activationRef;
    m_acceptedSkillEffectCard=other.m_acceptedSkillEffectCard;
    physicalEquipSource=other.physicalEquipSource;
	skillExecutionID=other.skillExecutionID; m_ownedCard=std::move(other.m_ownedCard);
	targetModReveal=std::move(other.targetModReveal);
	globalCardLifetimeManager().retainEventPayload(this, {card, whocard, m_ownedCard.data()});
	globalCardLifetimeManager().releaseEventPayload(&other);
	other.card = nullptr;
	other.whocard = nullptr;
    other.m_acceptedSkillEffectCard = false;
	return *this;
}
CardUseStruct::~CardUseStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

void CardUseStruct::replaceCard(const Card *newCard, Card *ownedCard)
{
	Q_ASSERT(!ownedCard || ownedCard == newCard);
	CardLifetimeManager &manager = globalCardLifetimeManager();
	CardLifetimeLease replacementLease(manager, manager.liveToken(newCard));
	manager.releaseEventPayload(this);
	card = nullptr;
	if (m_ownedCard.data() != ownedCard) {
		m_ownedCard.clear();
		if (ownedCard)
			m_ownedCard.reset(ownedCard);
	}
	card = newCard;
	manager.retainEventPayload(this, {card, whocard, m_ownedCard.data()});
}

bool CardUseStruct::isValid(const QString &pattern) const
{
	Q_UNUSED(pattern)
	return card != nullptr;
	/*if (card == nullptr) return false;
	if (!card->getSkillName().isEmpty()) {
	bool validSkill = false;
	QString skillName = card->getSkillName();
	QSet<const Skill*> skills = from->getVisibleSkills();
	for (int i = 0; i < 4; i++) {
	const EquipCard*equip = from->getEquip(i);
	if (equip == nullptr) continue;
	const Skill*skill = Sanguosha->getSkill(equip);
	if (skill)
	skills.insert(skill);
	}
	foreach (const Skill*skill, skills) {
	if (skill->objectName() != skillName) continue;
	const ViewAsSkill*vsSkill = ViewAsSkill::parseViewAsSkill(skill);
	if (vsSkill) {
	if (!vsSkill->isAvailable(from, m_reason, pattern))
	return false;
	else {
	validSkill = true;
	break;
	}
	} else if (skill->getFrequency() == Skill::Wake) {
	bool valid = (from->getMark(skill->objectName()) > 0);
	if (!valid)
	return false;
	else
	validSkill = true;
	} else
	return false;
	}
	if (!validSkill) return false;
	}
	if (card->targetFixed())
	return true;
	else {
	QList<const Player*> targets;
	foreach (const ServerPlayer*player, to)
	targets.push_back(player);
	return card->targetsFeasible(targets, from);
	}*/
}

bool CardUseStruct::tryParse(const QVariant &usage, Room*room)
{
    m_acceptedSkillEffectCard = false;
    sourceRef = SkillInstanceRef(); activationRef = SkillInstanceRef(); physicalEquipSource = PhysicalEquipSource(); skillExecutionID = 0;
	JsonArray use = usage.value<JsonArray>();
	replaceCard(nullptr);
	to.clear();
	m_validateTargets = true;
	hasSkillActivationRequest = false;
	if (use.length()>1&&JsonUtils::isString(use[0])&&use[1].canConvert<JsonArray>()){
		foreach(const QVariant &target, use[1].value<JsonArray>()) {
			if (!JsonUtils::isString(target)) return false;
			ServerPlayer *player = room->findChild<ServerPlayer*>(target.toString());
			if (!player) return false;
			to << player;
		}
		const Card *parsedCard = Card::Parse(use[0].toString());
		if (!parsedCard) return false;
		SkillInstanceUtils::SkillActivationRequest request;
		if (!SkillInstanceUtils::decodeActivationRequest(use, parsedCard->getSkillName(false), request)) return false;
		hasSkillActivationRequest = request.supplied;
		if (request.instanceID > 0)
			const_cast<Card *>(parsedCard)->setActivationSkill(request.skillName, request.instanceID);
		CardLifetimeManager &manager = globalCardLifetimeManager();
		const auto token = manager.liveToken(parsedCard);
		replaceCard(parsedCard, token && manager.state(token) == CardLifetimeState::PendingDelete
			? const_cast<Card *>(parsedCard) : nullptr);
		return true;
	}
	return false;
}

void CardUseStruct::parse(const QString &str, Room*room)
{
    m_acceptedSkillEffectCard = false;
    sourceRef = SkillInstanceRef(); activationRef = SkillInstanceRef(); physicalEquipSource = PhysicalEquipSource(); skillExecutionID = 0;
	replaceCard(nullptr);
	to.clear();
	m_validateTargets = true;
	const Card *parsedCard = Card::Parse(str);
	CardLifetimeManager &manager = globalCardLifetimeManager();
	const auto token = manager.liveToken(parsedCard);
	replaceCard(parsedCard, token && manager.state(token) == CardLifetimeState::PendingDelete
		? const_cast<Card *>(parsedCard) : nullptr);
	if (str.contains("->sgs")){
		foreach(QString target_name, str.split("->").last().split("+"))
			to << room->findChild<ServerPlayer*>(target_name);
	}
}

void CardUseStruct::clientReply()
{
	if (from){
		const QVariant &client = from->getRoom()->getTag("AiResult");
		if(client.canConvert<JsonArray>()) tryParse(client, from->getRoom());
		else parse(client.toString(), from->getRoom());
	}
}

void CardUseStruct::changeCard(Card*newcard)
{
	globalCardLifetimeManager().releaseEventPayload(this);
	const bool replacingOwnedCard = !m_ownedCard.isNull() && m_ownedCard.data() == card;
	QVariantMap tag = newcard->tag;
	for (auto it = card->tag.cbegin(); it != card->tag.cend(); ++it)
		tag.insert(it.key(), it.value());
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+card->getFlags());
	if (activationRef.isValid())
		newcard->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
	if (sourceRef.isValid())
		newcard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
	if (!replacingOwnedCard)
		newcard->change_cards << card;
	replaceCard(newcard);
	if (from) {
		Room *room = from->getRoom();
		const QVariantMap use = room->historyParent(room->currentHistoryEventId(), "use_card", true);
		if (room->historyRecordingEnabled() && use.value("data").toMap().value("from").toString() == from->objectName()) {
			// Conversions such as Fan keep the same use event. Its resolved card
			// changes, while the accepted-use fact remains an immutable snapshot.
			room->resolutionHistory().updateEvent(use.value("id").toLongLong(),
				{{"card", room->historyCardSnapshot(newcard)}});
		}
	}
}

void CardUseStruct::setOwnedCard(Card *ownedCard)
{
	replaceCard(ownedCard, ownedCard);
}

CardResponseStruct::CardResponseStruct()
	: m_card(nullptr), m_who(nullptr), m_isUse(false), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
}

CardResponseStruct::CardResponseStruct(const Card *card, bool isUse)
	: m_card(card), m_who(nullptr), m_isUse(isUse), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
	globalCardLifetimeManager().retainEventPayload(this, {m_card});
}

CardResponseStruct::CardResponseStruct(const Card *card, ServerPlayer *who, bool isUse)
	: m_card(card), m_who(who), m_isUse(isUse), m_isHandcard(false), m_isRetrial(false),
	  m_toCard(nullptr), skillExecutionID(0), nullified(false)
{
	globalCardLifetimeManager().retainEventPayload(this, {m_card});
}

CardResponseStruct::CardResponseStruct(const CardResponseStruct &other) { *this = other; }
CardResponseStruct::CardResponseStruct(CardResponseStruct &&other) noexcept { *this = std::move(other); }
CardResponseStruct &CardResponseStruct::operator=(const CardResponseStruct &other)
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	m_card=other.m_card; m_who=other.m_who; m_isUse=other.m_isUse; m_isHandcard=other.m_isHandcard;
	m_isRetrial=other.m_isRetrial; m_toCard=other.m_toCard; sourceRef=other.sourceRef; physicalEquipSource=other.physicalEquipSource;
	activationRef=other.activationRef; skillExecutionID=other.skillExecutionID; nullified=other.nullified;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
	return *this;
}
CardResponseStruct &CardResponseStruct::operator=(CardResponseStruct &&other) noexcept
{
	if (this == &other) return *this;
	globalCardLifetimeManager().releaseEventPayload(this);
	m_card=other.m_card; m_who=other.m_who; m_isUse=other.m_isUse; m_isHandcard=other.m_isHandcard;
	m_isRetrial=other.m_isRetrial; m_toCard=other.m_toCard; sourceRef=other.sourceRef; physicalEquipSource=other.physicalEquipSource;
	activationRef=other.activationRef; skillExecutionID=other.skillExecutionID; nullified=other.nullified;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
	globalCardLifetimeManager().releaseEventPayload(&other);
	other.m_card = nullptr;
	other.m_toCard = nullptr;
	return *this;
}
CardResponseStruct::~CardResponseStruct() { globalCardLifetimeManager().releaseEventPayload(this); }

void CardResponseStruct::changeCard(Card*newcard)
{
	globalCardLifetimeManager().releaseEventPayload(this);
	QVariantMap tag = newcard->tag;
	for (auto it = m_card->tag.cbegin(); it != m_card->tag.cend(); ++it)
		tag.insert(it.key(), it.value());
	newcard->tag = tag;
	newcard->setFlags(newcard->getFlags()+m_card->getFlags());
	if (activationRef.isValid())
		newcard->setActivationSkill(activationRef.key.skillName, activationRef.key.instanceID);
	if (sourceRef.isValid())
		newcard->setSourceSkill(sourceRef.key.skillName, sourceRef.key.instanceID);
	newcard->change_cards << m_card;
	m_card = newcard;
	globalCardLifetimeManager().retainEventPayload(this, {m_card, m_toCard});
}
