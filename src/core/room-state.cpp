#include "ai-card-query-scope.h"
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
    // Generic ingress may never be repaired by a later reset/current shape.
    m_aiRound56MapSourceAlive.store(false, std::memory_order_release);
    m_aiRound56MapSourceComplete.store(false, std::memory_order_release);
    clearCardsForSourceReset();
}

void RoomState::clearCardsForSourceReset()
{
    CardLifetimeManager &manager = globalCardLifetimeManager();
    foreach(Card *card, m_cards.values()) {
        manager.observeCard(card, true);
        delete card;
    }
    m_cards.clear();
    m_currentPlayer = nullptr;
    m_currentCardUsePattern.clear();
    m_flags.clear();
}

bool RoomState::aiRound56PhysicalMapSourceOrigin() const noexcept
{
    if (!m_aiRound56MapSourceAlive.load(std::memory_order_acquire)
        || !m_aiRound56MapSourceComplete.load(std::memory_order_acquire)
        || !hasAIRound56CardQueryOriginalSourceOrigin()
        || !Sanguosha || Sanguosha->currentRoomState() != this) return false;
    // Traverse the complete source-born public map, never an owner-selected
    // subset. Epochs only invalidate the already-issued factory provenance.
    for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) {
        int sourceIndex = -1;
        if (!it.value() || !it.value()->aiRound56PhysicalCloneIdentityOrigin(&sourceIndex)
            || sourceIndex != it.key()) return false;
    }
    return true;
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
    return AICardQueryScope::card(this, cardId, m_cards.value(cardId,nullptr));
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
    // This complete known factory transition may continue a healthy lineage;
    // a failed/unproved former map can never be healed by replacing its values.
    const bool continuousSource = aiRound56PhysicalMapSourceOrigin();
    if (!continuousSource) m_aiRound56MapSourceAlive.store(false, std::memory_order_release);
    m_aiRound56MapSourceComplete.store(false, std::memory_order_release);
    bool completeFactorySource = continuousSource;
    try {
        clearCardsForSourceReset();
        for (int i = 0; i < Sanguosha->getCardCount(); i++) {
            const Card *engineCard = Sanguosha->getEngineCard(i);
            if (!engineCard) {
                completeFactorySource = false;
                continue;
            }
            int sourceIndex = -1;
            const bool originalDefinition = engineCard->aiRound56OriginalPhysicalDefinitionIndex(&sourceIndex)
                && engineCard->aiRound56OriginalPhysicalDefinitionMaterialsOrigin() && sourceIndex == i
                && (engineCard->aiRound56OriginalRecursiveFieldOrigins() & 14u) == 14u;
            Card *clone = Card::Clone(engineCard);
            if (!clone) {
                completeFactorySource = false;
                continue;
            }
            const bool unchangedDefinition = originalDefinition
                && engineCard->aiRound56OriginalPhysicalDefinitionIndex(nullptr)
                && engineCard->aiRound56OriginalPhysicalDefinitionMaterialsOrigin()
                && (engineCard->aiRound56OriginalRecursiveFieldOrigins() & 14u) == 14u;
            WrappedCard *wrapped = new WrappedCard(WrappedCard::PhysicalCloneBirth{}, clone, i,
                                                    m_ownerThread, unchangedDefinition);
            if (!wrapped->aiRound56PhysicalCloneIdentityOrigin(nullptr)) completeFactorySource = false;
            if (m_identityChanged)
                QObject::connect(wrapped, &WrappedCard::identityChanged, wrapped,
                                 m_identityChanged, Qt::DirectConnection);
            m_cards[i] = wrapped;
        }
    } catch (...) {
        m_aiRound56MapSourceAlive.store(false, std::memory_order_release);
        throw;
    }
    if (!completeFactorySource || !hasAIRound56CardQueryOriginalSourceOrigin())
        m_aiRound56MapSourceAlive.store(false, std::memory_order_release);
    m_aiRound56MapSourceComplete.store(true, std::memory_order_release);
}
