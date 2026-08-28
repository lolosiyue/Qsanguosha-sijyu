#include "table-pile.h"
//#include "skin-bank.h"
#include "pixmapanimation.h"
#include "structs.h"
#include "carditem.h"
#include "effects/effects-policy.h"

QList<CardItem *> TablePile::removeCardItems(const QList<int> &card_ids, Player::Place)
{
	QList<CardItem *> result = _createCards(card_ids);
	_disperseCards(result, m_cardsDisplayRegion, Qt::AlignCenter, false, true);
	foreach (CardItem *card, m_visibleCards) {
		for (int i = 0; i < result.size(); i++) {
			if (result[i]->getId() == card->getId()) {
				result[i]->setPos(card->pos());
				break;
			}
		}
	}
	return result;
}

QRectF TablePile::boundingRect() const
{
	return m_cardsDisplayRegion;
}

void TablePile::setSize(double width, double height)
{
	m_cardsDisplayRegion = QRect(0, 0, width, height);
	m_numCardsVisible = width / G_COMMON_LAYOUT.m_cardNormalHeight + 1;
	resetTransform();
	setTransform(QTransform::fromTranslate(-width / 2, -height / 2), true);
}

void TablePile::timerEvent(QTimerEvent *)
{
	m_currentTime++;
	QList<CardItem *> oldCards;
	_m_mutex_pileCards.lock();
	foreach (CardItem *toRemove, m_visibleCards) {
		if (m_currentTime - toRemove->m_uiHelper.tablePileClearTimeStamp > S_CLEARANCE_DELAY_BUCKETS) {
			oldCards.append(toRemove);
			m_visibleCards.removeOne(toRemove);
		} else if (m_currentTime > toRemove->m_uiHelper.tablePileClearTimeStamp)
			toRemove->setEnabled(false); // @todo: this is a dirty trick. Use another property in the future
	}
	_fadeOutCardsLocked(oldCards);
	_m_mutex_pileCards.unlock();

	adjustCards();
}

void TablePile::_markClearance(CardItem *item)
{
	if (item->m_uiHelper.tablePileClearTimeStamp > m_currentTime)
		item->m_uiHelper.tablePileClearTimeStamp = m_currentTime;
}

void TablePile::clear(bool delayRequest)
{
	m_convertedCards.clear();
	if (m_visibleCards.isEmpty()) return;
	_m_mutex_pileCards.lock();
	// check again since we just gain the lock.

	if (delayRequest) {
		foreach(CardItem *toRemove, m_visibleCards)
			_markClearance(toRemove);
	} else {
		_fadeOutCardsLocked(m_visibleCards);
		m_visibleCards.clear();
	}

	_m_mutex_pileCards.unlock();
}

void TablePile::setConvertedSubcardName(const QList<int> &cardIds, const QString &cardObjectName,
	const QString &name)
{
	foreach (int cardId, cardIds) {
		m_convertedCards.insert(cardId, qMakePair(cardObjectName, name));
	}
}

void TablePile::_fadeOutCardsLocked(const QList<CardItem *> &cards)
{
	if (cards.isEmpty()) return;

	if (!G_EFFECTS.animationsEnabled()) {
		// 最終狀態:張牌透明兼且要拆走。deleteLater() 本來就係排隊嘅,
		// 所以呢度唔會喺 caller 手上炸咗 m_visibleCards 入面嘅 item。
		G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
		foreach (CardItem *toRemove, cards) {
			toRemove->setZValue(0.0);
			toRemove->setHomeOpacity(0.0);
			toRemove->setOpacity(0.0);
			toRemove->goBack(false);
			toRemove->setVisible(false);
			toRemove->deleteLater();
		}
		return;
	}

	QParallelAnimationGroup *group = new QParallelAnimationGroup;
	foreach (CardItem *toRemove, cards) {
		toRemove->setZValue(0.0);
		toRemove->setHomeOpacity(0.0);
		toRemove->setHomePos(QPointF(toRemove->homePos().x(), toRemove->homePos().y()));
		// duration 由 getGoBackAnimation() 自己 scale,唔可以喺呢度再 scale 一次。
		group->addAnimation(toRemove->getGoBackAnimation(true, false, 1000));
		toRemove->deleteLater();
	}
	G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
	group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TablePile::showJudgeResult(int cardId, bool takeEffect)
{
	CardItem *judgeCard = nullptr;
	_m_mutex_pileCards.lock();
	foreach (CardItem *toRemove, m_visibleCards) {
		if(toRemove->getId() == cardId){
			m_visibleCards.removeOne(toRemove);
			judgeCard = toRemove;
			break;
		}
	}
	if (judgeCard == nullptr)
		judgeCard = _createCard(cardId);
	_fadeOutCardsLocked(m_visibleCards);
	m_visibleCards.clear();
	m_visibleCards.append(judgeCard);
	_m_mutex_pileCards.unlock();
	if (G_EFFECTS.animationsEnabled())
		PixmapAnimation::GetPixmapAnimation(judgeCard, takeEffect ? "judgegood" : "judgebad");
	else
		G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
	adjustCards();
}

bool TablePile::_addCardItems(QList<CardItem *> &card_items, const CardsMoveStruct &moveInfo)
{
	// Apply the conversion name to each physical source card as it enters the table.
	foreach (CardItem *card_item, card_items) {
		const int cardId = card_item->getId();
		if (m_convertedCards.contains(cardId)) {
			const QPair<QString, QString> convertedCard = m_convertedCards.take(cardId);
			card_item->setConvertedCardName(convertedCard.first, convertedCard.second);
		}
	}
	if (card_items.isEmpty())
		return false;/*
	else if (moveInfo.from_place == Player::PlaceDelayedTrick
		&& moveInfo.reason.m_reason == CardMoveReason::S_REASON_NATURAL_ENTER) {
		foreach(CardItem *item, card_items)
			item->deleteLater();
		card_items.clear();
		return false;
	}*/

	QPointF rightMostPos = m_cardsDisplayRegion.center();
	if (m_visibleCards.length() > 0) {
		rightMostPos = m_visibleCards.last()->homePos();
		rightMostPos += QPointF(G_COMMON_LAYOUT.m_cardNormalWidth, 0);
	}

	_m_mutex_pileCards.lock();
	m_visibleCards.append(card_items);

	for (int i = 0; i < m_visibleCards.size() - qMax(m_numCardsVisible, card_items.length() + 1); i++)
		_markClearance(m_visibleCards[i]);

	foreach (CardItem *card_item, card_items) {
		card_item->setHomeOpacity(1.0);
		card_item->showFootnote();
		if (moveInfo.from_place == Player::DrawPile
			|| moveInfo.from_place == Player::PlaceJudge
			|| moveInfo.from_place == Player::PlaceTable) {
			card_item->setOpacity(0.0);
			card_item->setPos(rightMostPos);
			rightMostPos += QPointF(G_COMMON_LAYOUT.m_cardNormalWidth, 0);
		}
		else if (card_item->hasVirtualCardVisual()) {
			card_item->setOpacity(0.0);
			QPointF fadeInPos = QPointF(m_cardsDisplayRegion.right() + G_COMMON_LAYOUT.m_cardNormalWidth, m_cardsDisplayRegion.center().y());
			card_item->setPos(fadeInPos);
		}/*else if(moveInfo.from_place == Player::PlaceDelayedTrick// && moveInfo.from->getPhase()==Player::Judge
			&& moveInfo.reason.m_reason == CardMoveReason::S_MASK_BASIC_REASON){
			PixmapAnimation::GetPixmapAnimation(card_item, "question");
		}*/
		card_item->m_uiHelper.tablePileClearTimeStamp = INT_MAX;
	}
	_m_mutex_pileCards.unlock();

	adjustCards();
	return false;
}

void TablePile::adjustCards()
{
	if (m_visibleCards.isEmpty()) return;
	_disperseCards(m_visibleCards, m_cardsDisplayRegion, Qt::AlignCenter, true, true);

	if (!G_EFFECTS.animationsEnabled()) {
		// 牌堆嘅牌一定要到達 home 位同 home 透明度,唔係就會攤喺原位。
		G_EFFECTS.note(VisualEffectsPolicy::AnimationsSkipped);
		foreach(CardItem *card_item, m_visibleCards){
			card_item->goBack(false);
			card_item->setOpacity(card_item->getHomeOpacity());
		}
		updateContainer();
		return;
	}

	QParallelAnimationGroup *animation = new QParallelAnimationGroup;
	foreach(CardItem *card_item, m_visibleCards){
		animation->addAnimation(card_item->getGoBackAnimation(true));
	}
	connect(animation, SIGNAL(finished()), this, SLOT(onAnimationFinished()));
	G_EFFECTS.note(VisualEffectsPolicy::AnimationsStarted);
	animation->start();
}

