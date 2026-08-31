#ifndef CLIENT_GAME_STATE_REDUCER_H
#define CLIENT_GAME_STATE_REDUCER_H

#include <QString>
#include <QVariant>

class ClientGameState;

enum class ClientFlowDisposition
{
    StateMutation,
    PresentationEvent,
    SessionControl,
    ExplicitTextIrrelevant,
    Unclassified
};

struct ClientStateReduction
{
    bool success = false;
    ClientFlowDisposition disposition = ClientFlowDisposition::Unclassified;
    QString detail;
    QString eventText;
};

class ClientGameStateReducer
{
public:
    static ClientFlowDisposition classifyNotification(int command);
    static ClientStateReduction applyNotification(ClientGameState *state, int command,
                                                  const QVariant &payload);
};

#endif
