#ifndef AI_STRATEGY_ROUND56_FACTORY_SOURCE_H
#define AI_STRATEGY_ROUND56_FACTORY_SOURCE_H
#include <QString>
#include <QStringList>
#include <QVector>
class Room;
class Engine;
class RoomDefinitionRegistry;
// Native-only source facts; no userdata, prototype pointer or dispatch token.
struct AIRound56FactorySourceRow {
    QString publicPath, exactType, source;
    // True only for the closed, exact LuaSkillCard::clone constructor/copy
    // source recipe. It does NOT certify its effects or product lifetime.
    bool luaSkillCloneRecipe = false;
};
struct AIRound56EquipmentFactorySourceRow {
    QString publicName, exactType, reason;
    bool recipe = false, roomLocal = false, nullResult = false;
};
struct AIRound56FactorySourceReport {
    bool enumerationComplete = false;
    bool globalMapOrigin = false, roomMapOrigin = false;
    bool equipmentRecipeFamilyComplete = false;
    // Fixed public ZunweiCard native metaobject alternatives, no product custody.
    bool zunweiSkillRecipeComplete = false;
    bool globalHorseCallbackSourceComplete = false;
    int globalHorseOriginalNullCallbacks = 0;
    QVector<AIRound56EquipmentFactorySourceRow> equipmentRows;
    bool luaSkillCloneSourceFamilyComplete = false;
    QVector<AIRound56FactorySourceRow> rows;
    QString firstUncoveredLuaSkillCloneSource;
    QStringList remainingEffectObligations;
};
AIRound56FactorySourceReport inspectAIRound56PublicFactorySources(Room &);
// Strict complete public global+runtime LuaSkillCard prototype family check.
// No selected name/card/owner parameter. Unknown RTTI/null/invalid frame rejects.
// Success proves source dispatch/constructor/copy recipe membership ONLY;
// every effect obligation in the report is still required before any execution.
// This API cannot authorize Activate, allocation, preservation or a writer.
bool proveAIRound56LuaSkillCloneSourceFamily(Room &, QString *reason = nullptr);
// Internal cross-TU enumerator; never executes Parse, clone, metaObject or callback.
void appendAIRound56LuaSkillFactorySources(const RoomDefinitionRegistry &,
    AIRound56FactorySourceReport &);
// History check only. Unknown global registration attempts retire permanently.
bool hasAIRound56GlobalFactoryMapOrigin(const Engine *) noexcept;
#endif
