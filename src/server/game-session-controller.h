#ifndef _GAME_SESSION_CONTROLLER_H
#define _GAME_SESSION_CONTROLLER_H

#include <QList>
#include <QString>
#include <QStringList>

class Room;
class ServerPlayer;

class GameSessionController
{
public:
    enum class State
    {
        Waiting,
        Preparing,
        Initializing,
        Playing,
        Finished,
        Aborted
    };

    enum class PreparationPhase
    {
        None,
        AssigningRoles,
        ChoosingGenerals,
        ModeDrafting
    };

    enum class TerminationCause
    {
        None,
        GameOver,
        Surrender,
        InitializationFailure,
        Disconnected,
        Shutdown
    };

    explicit GameSessionController(Room &room);

    State state() const;
    PreparationPhase preparationPhase() const;
    TerminationCause terminationCause() const;
    bool requestStart();
    bool hasGameStarted() const;
    bool isPlaying() const;
    bool isTerminal() const;
    void abort(TerminationCause cause);
    void markGameReadyCompleted();

    void run();
    void prepareForStart();
    void assignRoles();
    void chooseGenerals(QList<ServerPlayer *> players = QList<ServerPlayer *>());
    void startGame();
    void gameOver(const QString &winner,
                  TerminationCause cause = TerminationCause::GameOver);
    bool makeSurrender(ServerPlayer *initiator);

private:
    friend struct GameSessionControllerTestAccess;
    friend struct PlayerDecisionServiceTestAccess;

    bool transitionTo(State next);
    void assignGeneralsForPlayers(const QList<ServerPlayer *> &toAssign);
    void assignGeneralsForPlayersOfJianGeDefenseMode(
        const QList<ServerPlayer *> &toAssign);
    void chooseGeneralsOfJianGeDefenseMode();
    QStringList triggerPreSelectionSkills(ServerPlayer *player, QStringList generals,
                                          const QString &reason);
    void triggerGeneralNotChosen(ServerPlayer *player, const QStringList &generals,
                                 const QString &chosen, const QString &reason);
    void setupChooseGeneralRequestArgs(ServerPlayer *player);

    Room &m_room;
    State m_state;
    PreparationPhase m_preparationPhase;
    TerminationCause m_terminationCause;
};

#endif
