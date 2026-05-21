#include "aux-skills.h"
#include "clientplayer.h"
#include "engine.h"
#include "roomscene.h"
#include "room.h"

static const Player *GetCurrentRequestPlayer()
{
    if (RoomSceneInstance != nullptr) {
        const ClientPlayer *dashboard_player = RoomSceneInstance->getDashboardPlayer();
        if (dashboard_player != nullptr)
            return dashboard_player;
    }

    return Self;
}

DiscardSkill::DiscardSkill()
    : ViewAsSkill("discard"), card(new DummyCard),
    num(0), include_equip(false), is_discard(true)
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

bool DiscardSkill::viewFilter(const QList<const Card *> &selected, const Card *card) const
{
    const Player *player = GetCurrentRequestPlayer();
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
    : OneCardViewAsSkill("response-skill")
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

bool ResponseSkill::matchPattern(const Player *player, const Card *card) const
{
    if (player->isCardLimited(card, request))
        return false;

    return pattern && pattern->match(player, card);
}

bool ResponseSkill::viewFilter(const Card *card) const
{
    const Player *player = GetCurrentRequestPlayer();
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
}

bool TransferCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!Self || Self == to_select)
        return false;

    if (!targets.isEmpty())
        return false;

    return Self->canDiscard(to_select, "he");
}

void TransferCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    CardUseStruct new_use = card_use;
    new_use.card = this;
    room->useCard(new_use);
}

TransferSkill::TransferSkill()
    : OneCardViewAsSkill("transfer"), m_toSelect(-1)
{
}

void TransferSkill::setToSelect(int cardId)
{
    m_toSelect = cardId;
}

bool TransferSkill::viewFilter(const Card *to_select) const
{
    if (m_toSelect < 0)
        return false;

    return to_select->getId() == m_toSelect;
}

const Card *TransferSkill::viewAs(const Card *originalCard) const
{
    if (!originalCard || originalCard->getId() != m_toSelect)
        return nullptr;

    TransferCard *card = new TransferCard;
    card->addSubcard(originalCard);
    return card;
}

bool TransferSkill::isAvailable(const Player *player, const Card *card) const
{
    if (!player)
        return false;

    if (player->getPhase() != Player::Play)
        return false;

    if (card && player->isCardLimited(card, Card::MethodUse))
        return false;

    return true;
}

