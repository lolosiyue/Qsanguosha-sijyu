#include "ai-strategy-round21-readonly.h"
#include "ai.h"

int aiRound21ReadonlyPrototype(const QString &skill, const QString &prototype)
{
    // b6025 ai/happy2v2-ai.lua:5,18 and ai/assassins-ai.lua:269,292-323.
    // Native metaobjects: happy2v2.cpp:522; assassins.cpp:1127.
    // Bit 1 permits an ordered material wire, never a user-string declaration.
    if ((skill == QLatin1String("kuiji") && prototype == QLatin1String("KuijiCard"))
        || (skill == QLatin1String("mixin") && prototype == QLatin1String("MixinCard"))) return 1;
    // Empty source constructors only: assassins-ai.lua:95 and :372. Native
    // V2 factories select no materials (assassins.cpp:356-371,799-811).
    if ((skill == QLatin1String("mizhao") && prototype == QLatin1String("MizhaoCard"))
        || (skill == QLatin1String("duyi") && prototype == QLatin1String("DuyiCard"))) return 0;
    return -1;
}

bool aiRound21FenyueEvaluation(const AIRequest &request, const QString &skill,
                              const QString &name, const QJsonArray &costs)
{
    // ai/yongjian-ai.lua:620-637, dummyCard() default NoSuit (6), number 0;
    // extensions/yongjian.lua:1719 supplies this exact public choice. Source
    // sets raw attribution _zd_fenyue and never selects payment material here.
    return skill == QLatin1String("_zd_fenyue") && name == QLatin1String("slash")
        && costs.isEmpty() && request.kind == AIRequest::Choice
        && request.choiceOptions.reason == QLatin1String("zd_fenyue")
        && request.choiceOptions.choices.contains(QStringLiteral("zd_fenyue2"));
}
