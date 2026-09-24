#include "aux-skills.h"
#include "engine.h"
#include "room.h"
#include "serverplayer.h"

DiscardSkill::DiscardSkill()
    : ViewAsSkill("discard"), card(new DummyCard),
    num(0), include_equip(false), is_discard(true), request_player(nullptr)
{
    card->setParent(this);
}

void DiscardSkill::setNum(int num)
{
    this->num = num;
}

void DiscardSkill::setMinNum(int minnum)
{
    this->minnum = minnum;
}

void DiscardSkill::setIncludeEquip(bool include_equip)
{
    this->include_equip = include_equip;
}

void DiscardSkill::setIsDiscard(bool is_discard)
{
    this->is_discard = is_discard;
}

void DiscardSkill::setPattern(const QString &pattern)
{
    this->pattern = pattern;
}

void DiscardSkill::setPlayer(const Player *player)
{
    request_player = player;
}

bool DiscardSkill::viewFilter(const QList<const Card *> &selected, const Card *card) const
{
    const Player *player = request_player;
    if (player == nullptr)
        return false;

    if (selected.length() >= num)
        return false;

    if (!include_equip && card->isEquipped())
        return false;

    if (!Sanguosha->matchExpPattern(pattern, player, card))
        return false;

    if (is_discard && player->isCardLimited(card, Card::MethodDiscard))
        return false;

    return true;
}

const Card *DiscardSkill::viewAs(const QList<const Card *> &cards) const
{
    if (cards.length() >= minnum) {
        card->clearSubcards();
        card->addSubcards(cards);
        return card;
    }
	return nullptr;
}

// -------------------------------------------

ResponseSkill::ResponseSkill()
    : OneCardViewAsSkill("response-skill"), request_player(nullptr)
{
    request = Card::MethodResponse;
}

void ResponseSkill::setPattern(const QString &pattern)
{
    this->pattern = Sanguosha->getPattern(pattern);
}

void ResponseSkill::setRequest(const Card::HandlingMethod request)
{
    this->request = request;
}

void ResponseSkill::setPlayer(const Player *player)
{
    request_player = player;
}

bool ResponseSkill::matchPattern(const Player *player, const Card *card) const
{
    if (player->isCardLimited(card, request))
        return false;

    return pattern && pattern->match(player, card);
}

bool ResponseSkill::viewFilter(const Card *card) const
{
    const Player *player = request_player;
    return player != nullptr && matchPattern(player, card);
}

const Card *ResponseSkill::viewAs(const Card *originalCard) const
{
    return originalCard;
}

// -------------------------------------------

ShowOrPindianSkill::ShowOrPindianSkill()
{
    setObjectName("showorpindian-skill");
    request = Card::MethodNone;
}

bool ShowOrPindianSkill::matchPattern(const Player *player, const Card *card) const
{
    return pattern && pattern->match(player, card);
}

// -------------------------------------------

class NosYijiCard : public DummyCard
{
public:
    NosYijiCard()
    {
        target_fixed = false;
    }

    void setPlayerNames(const QStringList &names)
    {
        set = names;
    }

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
    {
        return targets.isEmpty() && set.contains(to_select->objectName());
    }

private:
    QStringList set;
};

NosYijiViewAsSkill::NosYijiViewAsSkill()
    : ViewAsSkill("askforyiji")
{
    card = new NosYijiCard;
    card->setParent(this);
}

void NosYijiViewAsSkill::setPlayerNames(const QStringList &names, int max_num, const QString &card_str)
{
    card->setPlayerNames(names);
    this->max_num = max_num;
    ids = ListS2I(card_str.split("+"));
}

bool NosYijiViewAsSkill::viewFilter(const QList<const Card *> &selected, const Card *card) const
{
    return ids.contains(card->getId()) && selected.length() < max_num;
}

const Card *NosYijiViewAsSkill::viewAs(const QList<const Card *> &cards) const
{
    if (cards.isEmpty())
        return nullptr;

    card->clearSubcards();
    card->addSubcards(cards);
    return card;
}

// ------------------------------------------------

class ChoosePlayerCard : public DummyCard
{
public:
    ChoosePlayerCard()
    {
        target_fixed = false;
    }

    void setPlayerNames(const QStringList &names, int max, int min)
    {
        set = names;
        this->max = max;
        this->min = min;
    }

    bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
    {
        return targets.length() < max && set.contains(to_select->objectName());
    }

    bool targetsFeasible(const QList<const Player *> &targets, const Player *) const
    {
		return !targets.isEmpty()&&((min<0&&targets.length()==max)||(min>=0&&targets.length()>=min));
    }

private:
    QStringList set;
    int max;
    int min;
};

ChoosePlayerSkill::ChoosePlayerSkill()
    : ZeroCardViewAsSkill("choose_player")
{
    card = new ChoosePlayerCard;
    card->setParent(this);
}

void ChoosePlayerSkill::setPlayerNames(const QStringList &names, int max, int min)
{
    card->setPlayerNames(names, max, min);
}

const Card *ChoosePlayerSkill::viewAs() const
{
    return card;
}

// TransferSkill/TransferCard implementation

TransferCard::TransferCard()
{
    setObjectName("transfer");
    target_fixed = false;
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool TransferCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!Self || !to_select || !to_select->isAlive() || Self == to_select)
        return false;

    if (!targets.isEmpty())
        return false;

    return !Self->isProhibited(to_select, this)
        && (!to_select->hasShownOneGeneral()
            || (Self->hasShownOneGeneral() && !Self->isFriendWith(to_select)));
}

const Card *TransferCard::validate(CardUseStruct &use) const
{
    if (!use.from || use.from->isRemoved() || use.from->getPhase() != Player::Play || subcardsLength() != 1
        || use.to.size() != 1 || !targetFilter({}, use.to.first(), use.from)) return nullptr;
    TransferCard action;
    if (use.from->isCardLimited(&action, Card::MethodUse)) return nullptr;
    Room *room = use.from->getRoom();
    const int id = getSubcards().first();
    const Card *card = room->getCard(id);
    if (!card || !card->isTransferable() || room->getCardOwner(id) != use.from) return nullptr;
    // Transfer is a give action, not a discard or a use of the material card.
    if (room->getCardPlace(id) == Player::PlaceHand) return this;
    if (room->getCardPlace(id) != Player::PlaceEquip || use.from->isEquipsNullified(card)) return nullptr;
    return card->objectName() == "Breastplate" || card->objectName() == "jingfan" ? this : nullptr;
}

void TransferCard::onEffect(CardEffectStruct &effect) const
{
    if (!effect.from || !effect.to) return;
    const bool draw = effect.to->hasShownOneGeneral();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(),
                         effect.to->objectName(), "transfer", QString());
    effect.to->getRoom()->obtainCard(effect.to, this, reason, true);
    if (draw && effect.from->isAlive()) effect.from->drawCards(1, "transfer");
}

TransferSkill::TransferSkill() : ViewAsSkillV2("heg_transfer", 1)
{
    // This action comes from a card's transfer property, not a general skill.
    // CardActionButton is the UI entry; keep the V2 skill only for validation.
    attached_lord_skill = true;
    hide_skill = true;
    setProperty("IgnoreInvalidity", true);
}

bool TransferSkill::canActivate(const ActiveSkillRequest &request) const
{
    if (request.reason != CardUseStruct::CARD_USE_REASON_PLAY || !isAvailable(request.initiator, nullptr))
        return false;
    for (int id : request.initiator->handCards())
        if (isAvailable(request.initiator, Sanguosha->getCard(id))) return true;
    return false;
}

bool TransferSkill::canSelectCard(const ActiveSkillRequest &request, const Card *card) const
{
    return request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
        && isAvailable(request.initiator, card);
}

const Card *TransferSkill::createCard(const ActiveSkillRequest &request) const
{
    if (!cardSelectionFeasible(request)) return nullptr;
    // Preserve MethodNone: giving a card must not become a discard cost.
    TransferCard *card = new TransferCard;
    card->addSubcards(request.selectedCardIds);
    card->setSkillName(objectName());
    return card;
}

bool TransferSkill::isAvailable(const Player *player, const Card *card) const
{
    TransferCard action;
    return player && player->isAlive() && !player->isRemoved() && player->getPhase() == Player::Play
        && !player->isCardLimited(&action, Card::MethodUse)
        && player->hasSkill(objectName())
        && (!card || (card->isTransferable() && player->handCards().contains(card->getEffectiveId())));
}
