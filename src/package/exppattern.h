#ifndef _EXPPATTERN_H
#define _EXPPATTERN_H

#include "package.h"

class ExpPattern : public CardPattern
{
public:
    ExpPattern(const QString &exp);
    virtual bool match(const Player *player, const Card *card) const;
private:
    // 表達式在建構時拆好；match() 在選目標時每張牌、每個技能都會跑，不能每次重切字串。
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
        QList<QList<Term>> names; // ',' 分隔的「或」，每組內 '+' 分隔的「且」
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
