#ifndef _EXPPATTERN_H
#define _EXPPATTERN_H

#include "package.h"

class ExpPattern : public CardPattern
{
public:
    ExpPattern(const QString &exp);
    virtual bool match(const Player *player, const Card *card) const;
private:
    // Parse the expression at construction; match() runs for every card and skill during target selection.
    struct Term {
        QString text;
        QByteArray className;
        bool negated = false;
    };
    struct NumberTerm {
        QString text;
        bool negated = false;
        bool isRange = false;
        int from = 1, to = 13;
        bool isInt = false;
        int value = 0;
    };
    struct Alternative {
        int factorCount = 0;
        bool anyName = false, anySuit = false, anyNumber = false, anyPlace = false;
        QList<QList<Term>> names; // Commas separate alternatives; plus signs within a group mean all conditions must match.
        QList<Term> suits;
        QList<NumberTerm> numbers;
        QList<Term> places;
    };

    QString exp;
	bool available;
    QList<Alternative> alternatives;
    bool matchOne(const Player *player, const Card *card, const Alternative &alternative) const;
};

#endif
