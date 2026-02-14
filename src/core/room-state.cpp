#include "room-state.h"
#include "engine.h"
#include "wrapped-card.h"

RoomState::~RoomState()
{
    foreach(Card *card, m_cards.values())
        delete card;
    m_cards.clear();
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
    Card *newCard = Card::Clone(Sanguosha->getEngineCard(cardId));
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
    foreach(WrappedCard *card, m_cards.values())
        delete card;
    m_cards.clear();
    for (int i = 0; i < Sanguosha->getCardCount(); i++)
        m_cards[i] = new WrappedCard(Card::Clone(Sanguosha->getEngineCard(i)));
}

