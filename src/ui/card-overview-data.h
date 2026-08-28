#pragma once

#include <QList>
#include <QString>
#include <QStringList>

class Card;

namespace CardOverviewData {

struct Classification
{
    QString typeKey;
    QString kindKey;
};

struct CardGroup
{
    const Card *representative = nullptr;
    QList<const Card *> cards;
};

struct PhysicalVariantGroup
{
    QString suitKey;
    int number = 0;
    QString numberDisplay;
    QString packageKey;
    QList<int> cardIds;
};

QList<const Card *> collectCards();
QList<CardGroup> groupCardsByObjectName(const QList<const Card *> &cards);
QList<PhysicalVariantGroup> groupPhysicalVariants(const QList<const Card *> &cards);
bool matchesAnyTag(const QStringList &availableTags, const QStringList &selectedTags);
QString overviewName(const Card *card);
Classification classify(const Card *card);

}
