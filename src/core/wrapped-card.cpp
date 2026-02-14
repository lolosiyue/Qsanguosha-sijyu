#include "wrapped-card.h"

WrappedCard::WrappedCard(Card *card)
    : Card(card->getSuit(), card->getNumber()), m_card(nullptr), m_isModified(false)
{
    m_id = card->getId();
    copyEverythingFrom(card);
}

WrappedCard::~WrappedCard()
{
    //Q_ASSERT(m_card != nullptr);
    delete m_card;
}

void WrappedCard::takeOver(Card *card)
{
    /*Q_ASSERT(getId() >= 0);
    Q_ASSERT(card != this);
    Q_ASSERT(m_card != card);
    if (m_card != nullptr) {
        m_isModified = true;
        delete m_card;
    }
    m_card = card;
    m_card->setId(getId());
    setObjectName(card->objectName());
    setSuit(card->getSuit());
    setNumber(card->getNumber());
    m_skillName = card->getSkillName(false);*/
	if(card != this && m_card != card){
		if(m_card){
			m_isModified = true;
			m_card->deleteLater();
			card->tag = m_card->tag;
			card->setFlags(m_card->getFlags());
		}
		m_card = card;
		m_card->setId(getId());
		setSuit(card->getSuit());
		setNumber(card->getNumber());
		setObjectName(card->objectName());
		m_skillName = card->getSkillName(false);
	}
}

void WrappedCard::copyEverythingFrom(Card *card)
{
    /*Q_ASSERT(card->getId() >= 0);
    Q_ASSERT(card != this);
    Q_ASSERT(m_card != card);
    if (m_card != nullptr) {
        m_isModified = true;
        m_card->deleteLater();
    }
    m_card = card;
    Card::setId(card->getId());
    Card::setSuit(card->getSuit());
    Card::setNumber(card->getNumber());
    m_skillName = card->getSkillName(false);
    setObjectName(card->objectName());
    flags = card->getFlags();*/
	if(card->getId() >= 0 && card != this && m_card != card){
		if (m_card) {
			m_isModified = true;
			m_card->deleteLater();
			card->tag = m_card->tag;
		}
		m_card = card;
		Card::setId(card->getId());
		Card::setSuit(card->getSuit());
		Card::setNumber(card->getNumber());
		m_skillName = card->getSkillName(false);
		setObjectName(card->objectName());
		flags = card->getFlags();
	}
}

void WrappedCard::setFlags(const QString &flag) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setFlags(flag);
    m_card->setFlags(flag);
}

void WrappedCard::setTag(const QString &key, const QVariant &data) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setTag(key, data);
    m_card->setTag(key, data);
}

void WrappedCard::removeTag(const QString &key) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::removeTag(key);
    m_card->removeTag(key);
}

void WrappedCard::setMark(const QString &mark, int value) const
{
    //Q_ASSERT(m_card != nullptr);
    //m_isModified = true;
    Card::setMark(mark, value);
    m_card->setMark(mark, value);
}

