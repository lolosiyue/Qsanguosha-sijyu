#ifndef ZOMBINE
#define ZOMBINE

#include "package.h"
#include "card.h"

class ZombinePackage : public Package
{
    Q_OBJECT

public:
    ZombinePackage();
};

class CeeCard : public SkillCard
{
    Q_OBJECT

public:
    Q_INVOKABLE CeeCard();
    void onEffect(CardEffectStruct &effect) const;
};

#endif