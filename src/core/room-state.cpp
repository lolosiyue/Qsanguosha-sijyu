#include "room-state.h"
#include "card-lifetime-manager.h"
#include "engine.h"
#include "wrapped-card.h"

RoomState::~RoomState()
{
    clear();
}

void RoomState::clear()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    foreach(Card *card, m_cards.values()) {
        manager.observeCard(card);
        delete card;
    }
    m_cards.clear();
    m_currentPlayer = nullptr;
    m_currentCardUsePattern.clear();
    m_flags.clear();
}

Card *RoomState::getCard(int cardId) const
{
	/*if (m_cards.contains(cardId)){
		const Player*owner = Sanguosha->getCardOwner(cardId);
		if(owner&&Sanguosha->getCardPlace(cardId)!=Player::PlaceSpecial){
			const Card*card = nullptr;
			foreach (const Skill *skill, owner->getSkillList(true, false)) {
				if (skill->inherits("FilterSkill")){
					const FilterSkill *fs = qobject_cast<const FilterSkill *>(skill);
					if (fs->viewFilter(m_cards[cardId])&&owner->hasSkill(skill->objectName())){
						if(card) delete card;
						card = fs->viewAs(m_cards[cardId]);
					}
				}
			}
			if(card){
				if(card->getSkillName(false)!=m_cards[cardId]->getSkillName(false))
					m_cards[cardId]->takeOver((Card*)card);
			}else if(m_cards[cardId]->isModified())
				resetCard(cardId);
		}
	}*/
    return m_cards.value(cardId,nullptr);
}

void RoomState::resetCard(int cardId) const
{
    WrappedCard *wrapped = m_cards.value(cardId, nullptr);
    const Card *engineCard = Sanguosha->getEngineCard(cardId);
    if (!wrapped || !engineCard)
        return;
    Card *newCard = Card::Clone(engineCard);
    if (newCard){/*
		newCard->tag = m_cards[cardId]->tag;
		newCard->setFlags(m_cards[cardId]->getFlags());*/
		m_cards[cardId]->copyEverythingFrom(newCard);
		m_cards[cardId]->setModified(false);/*
		newCard->clearFlags();
		newCard->tag.clear();*/
	}
}

// Reset all cards, generals' states of the room instance
void RoomState::reset()
{
    clear();
    for (int i = 0; i < Sanguosha->getCardCount(); i++) {
        const Card *engineCard = Sanguosha->getEngineCard(i);
        if (!engineCard)
            continue;
        Card *clone = Card::Clone(engineCard);
        if (!clone)
            continue;
        WrappedCard *wrapped = new WrappedCard(nullptr);
        wrapped->setAdoptionOwnerThread(m_ownerThread);
        wrapped->copyEverythingFrom(clone);
        m_cards[i] = wrapped;
    }
}

