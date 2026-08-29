#include "ui-rng.h"

#include <QRandomGenerator>

namespace UiRng {

int bounded(int upperExclusive)
{
    return upperExclusive > 0
        ? QRandomGenerator::global()->bounded(upperExclusive)
        : 0;
}

double generateDouble()
{
    return QRandomGenerator::global()->generateDouble();
}

}
