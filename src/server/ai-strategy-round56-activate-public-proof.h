#ifndef AI_STRATEGY_ROUND56_ACTIVATE_PUBLIC_PROOF_H
#define AI_STRATEGY_ROUND56_ACTIVATE_PUBLIC_PROOF_H
#include <QString>
#include <QVector>
#include <QtGlobal>
class Room;
struct lua_State;

// These are diagnostic/source obligations, NOT execution capabilities. In
// particular GetterClosureProved does not certify its native descendants.
enum class AIRound56ActivateSourceStatus {
    Uncovered, GetterClosureProved, ScalarMethodBodyProved, NoAuthoredCallback,
    CataloguedType, ReadOnlySelectionClosureProved, FactorySourceRecipeClassified
};
struct AIRound56ActivateSourceRow {
    QString publicPath;
    QString exactType;
    QString role;
    QString source;
    QString detail;
    int nargs = -1;
    quintptr definitionIdentity = 0;
    quintptr closureIdentity = 0;
    AIRound56ActivateSourceStatus status = AIRound56ActivateSourceStatus::Uncovered;
};
struct AIRound56ActivatePublicSourceReport {
    // True means the public enumeration finished in one live definition frame.
    // It NEVER means the request, source, callback or allocation union is proved.
    bool enumerationComplete = false;
    quint64 generation = 0;
    quint64 definitionVersion = 0;
    QVector<AIRound56ActivateSourceRow> rows;
    QString firstUncoveredSource() const;
};

// Enumerates every public getSkillNames definition, Trigger-owned view-as
// component, source-ref Amount definition, Pindian definition and engine card.
// No player/owner selection, applicability test, callback execution or factory
// invocation occurs. Unknown exact types/roles/sources remain Uncovered.
// Caller must hold the ordinary runtime binding on its owner thread.
// No scope/token, dispatcher member set, writer exception, preservation or
// allocation capability is created, even if every currently implemented row
// happens to pass. The remaining global obligations are explicit rows.
AIRound56ActivatePublicSourceReport inspectAIRound56ActivatePublicSources(
    Room &, lua_State *);
#endif
