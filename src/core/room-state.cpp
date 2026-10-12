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
    m_aiIdMapSourceAlive.store(false, std::memory_order_release);
    m_aiIdMapSourceComplete.store(false, std::memory_order_release);
    // Generic ingress may never be repaired by a later reset/current shape.
    m_aiMapSourceAlive.store(false, std::memory_order_release);
    m_aiMapSourceComplete.store(false, std::memory_order_release);
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

bool RoomState::aiPhysicalMapSourceOrigin() const noexcept
{
    if (!m_aiMapSourceAlive.load(std::memory_order_acquire)
        || !m_aiMapSourceComplete.load(std::memory_order_acquire)
        || !hasAICardQueryOriginalSourceOrigin()
        || !Sanguosha || Sanguosha->currentRoomState() != this) return false;
    // Traverse the complete source-born public map, never an owner-selected
    // subset. Epochs only invalidate the already-issued factory provenance.
    for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) {
        int sourceIndex = -1;
        if (!it.value() || !it.value()->aiPhysicalCloneIdentityOrigin(&sourceIndex)
            || sourceIndex != it.key()) return false;
    }
    return true;
}

bool RoomState::aiPhysicalIdMapSourceOrigin() const noexcept
{
    if (!m_aiIdMapSourceAlive.load(std::memory_order_acquire)
        || !m_aiIdMapSourceComplete.load(std::memory_order_acquire)
        || !hasAICardQueryOriginalSourceOrigin()
        || !Sanguosha || Sanguosha->currentRoomState() != this) return false;
    for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) {
        int sourceIndex = -1;
        if (!it.value() || !it.value()->aiPhysicalCloneIdOrigin(&sourceIndex)
            || sourceIndex != it.key()) return false;
    }
    return true;
}

quint64 RoomState::aiPhysicalIdMapGeneration() const noexcept
{
    return m_aiIdMapGeneration.load(std::memory_order_acquire);
}

bool RoomState::aiPhysicalIdSourceContains(const Card *card) const noexcept
{
    if (!card || !m_aiIdMapSourceAlive.load(std::memory_order_acquire)
        || !m_aiIdMapSourceComplete.load(std::memory_order_acquire)
        || !hasAICardQueryOriginalSourceOrigin()
        || !Sanguosha || Sanguosha->currentRoomState() != this) return false;
    // Actual writer checks only the member it introduces. Consumers also check
    // the entire map source; no selected hand scan or sampled card ID grants it.
    for (auto it = m_cards.cbegin(); it != m_cards.cend(); ++it) {
        if (it.value() != card) continue;
        int sourceIndex = -1;
        return it.value()->aiPhysicalCloneIdOrigin(&sourceIndex) && sourceIndex == it.key();
    }
    return false;
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
    const bool continuousIdSource = aiPhysicalIdMapSourceOrigin();
    if (!continuousIdSource) m_aiIdMapSourceAlive.store(false, std::memory_order_release);
    m_aiIdMapSourceComplete.store(false, std::memory_order_release);
    bool completeIdFactorySource = continuousIdSource;
    if (m_aiIdMapGeneration.fetch_add(1, std::memory_order_acq_rel) == ~quint64(0)) {
        completeIdFactorySource = false;
        m_aiIdMapSourceAlive.store(false, std::memory_order_release);
    }
    const bool continuousSource = aiPhysicalMapSourceOrigin();
    if (!continuousSource) m_aiMapSourceAlive.store(false, std::memory_order_release);
    m_aiMapSourceComplete.store(false, std::memory_order_release);
    bool completeFactorySource = continuousSource;
    try {
        clearCardsForSourceReset();
        for (int i = 0; i < Sanguosha->getCardCount(); i++) {
            const Card *engineCard = Sanguosha->getEngineCard(i);
            if (!engineCard) {
                completeFactorySource = false;
                completeIdFactorySource = false;
                continue;
            }
            int sourceIndex = -1;
            const bool originalDefinition = engineCard->aiOriginalPhysicalDefinitionIndex(&sourceIndex)
                && engineCard->aiOriginalPhysicalDefinitionMaterialsOrigin() && sourceIndex == i
                && (engineCard->aiOriginalRecursiveFieldOrigins() & 14u) == 14u;
            const bool originalDefinitionId = engineCard->aiPhysicalDefinitionIdIndex(&sourceIndex)
                && sourceIndex == i;
            Card *clone = Card::Clone(engineCard);
            if (!clone) {
                completeFactorySource = false;
                completeIdFactorySource = false;
                continue;
            }
            const bool unchangedDefinition = originalDefinition
                && engineCard->aiOriginalPhysicalDefinitionIndex(nullptr)
                && engineCard->aiOriginalPhysicalDefinitionMaterialsOrigin()
                && (engineCard->aiOriginalRecursiveFieldOrigins() & 14u) == 14u;
            WrappedCard *wrapped = new WrappedCard(WrappedCard::PhysicalCloneBirth{}, clone, i,
                                                    m_ownerThread, unchangedDefinition,
                                                    originalDefinitionId && engineCard->aiPhysicalDefinitionIdIndex(nullptr));
            if (!wrapped->aiPhysicalCloneIdOrigin(nullptr)) completeIdFactorySource = false;
            if (!wrapped->aiPhysicalCloneIdentityOrigin(nullptr)) completeFactorySource = false;
            if (m_identityChanged)
                QObject::connect(wrapped, &WrappedCard::identityChanged, wrapped,
                                 m_identityChanged, Qt::DirectConnection);
            m_cards[i] = wrapped;
        }
    } catch (...) {
        m_aiIdMapSourceAlive.store(false, std::memory_order_release);
        m_aiMapSourceAlive.store(false, std::memory_order_release);
        throw;
    }
    if (!completeFactorySource || !hasAICardQueryOriginalSourceOrigin())
        m_aiMapSourceAlive.store(false, std::memory_order_release);
    m_aiMapSourceComplete.store(true, std::memory_order_release);
    if (!completeIdFactorySource || !hasAICardQueryOriginalSourceOrigin())
        m_aiIdMapSourceAlive.store(false, std::memory_order_release);
    m_aiIdMapSourceComplete.store(true, std::memory_order_release);
}
