#ifndef QSAN_SCENARIO_WORK_EXAMPLES_H
#define QSAN_SCENARIO_WORK_EXAMPLES_H

#include "scenario-work.h"

// Examples resolve cards against the active catalog, then pin that catalog like
// every authored work. They never ship somebody else's physical card numbers.
QList<ScenarioWork::WorkDefinition> scenarioWorkExamples(const QJsonObject &compatibility);

#endif
