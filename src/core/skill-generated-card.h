#ifndef SKILL_GENERATED_CARD_H
#define SKILL_GENERATED_CARD_H
#include "structs.h"
#include <QString>
// Audited native producers returning an already-disclosed original card. This
// identifies a contract only; activation, source, live selection and product are
// still checked by the native skill on every admission.
inline bool isNotifiedPhysicalCardSkill(const QString &skill,
    CardUseStruct::CardUseReason reason, const QString &pattern)
{
    return reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "@@" + skill
        && (skill == "zuguangu" || skill == "zukaiji" || skill == "zugaobian"
            || skill == "zugaoshi" || skill == "kezuliuju" || skill == "kezuhuanjia"
            || skill == "keolzhuri" || skill == "keolsaoguslash");
}
inline bool isMarkedPhysicalCardSkill(const QString &skill,
    CardUseStruct::CardUseReason reason, const QString &pattern)
{
    return reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "@@" + skill
        && (skill == "kezumingjiemark" || skill == "kezulilunuse");
}
inline bool isAuditedPhysicalCardSkill(const QString &skill,
    CardUseStruct::CardUseReason reason, const QString &pattern)
{
    return isNotifiedPhysicalCardSkill(skill, reason, pattern) || isMarkedPhysicalCardSkill(skill, reason, pattern);
}
#endif
