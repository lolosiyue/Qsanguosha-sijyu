#include "engine-bootstrap.h"
#include "game-session-controller.h"
#include "room.h"

#include <QDebug>

struct GameSessionControllerTestAccess
{
    static bool transition(GameSessionController &controller,
                           GameSessionController::State next)
    {
        return controller.transitionTo(next);
    }

    static void finish(GameSessionController &controller)
    {
        if (controller.transitionTo(GameSessionController::State::Finished))
            controller.m_terminationCause = GameSessionController::TerminationCause::GameOver;
    }
};

namespace {

static bool expect(bool condition, const char *context)
{
    if (condition)
        return true;
    qCritical() << "game session controller test failed:" << context;
    return false;
}

static bool legalLifecycleReachesPlayingOnlyAfterGameReady()
{
    Room room(nullptr, QStringLiteral("02_1v1"));
    GameSessionController controller(room);

    if (!expect(controller.state() == GameSessionController::State::Waiting,
                "initial state is Waiting")
        || !expect(!controller.hasGameStarted(), "waiting is not started")
        || !expect(controller.requestStart(), "ready request enters Preparing")
        || !expect(controller.state() == GameSessionController::State::Preparing,
                   "state is Preparing")
        || !expect(!controller.requestStart(), "duplicate ready request is rejected")
        || !expect(GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Preparing),
                   "same-state transition is a no-op")
        || !expect(!GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Playing),
                   "Preparing cannot skip Initializing")
        || !expect(controller.state() == GameSessionController::State::Preparing,
                   "invalid transition keeps the current state")
        || !expect(GameSessionControllerTestAccess::transition(
                       controller, GameSessionController::State::Initializing),
                   "Preparing enters Initializing")
        || !expect(controller.hasGameStarted(), "Initializing keeps legacy started semantics")
        || !expect(!controller.isPlaying(), "Initializing is not Playing"))
        return false;

    controller.markGameReadyCompleted();
    return expect(controller.state() == GameSessionController::State::Playing,
                  "GameReady completion enters Playing")
        && expect(controller.isPlaying(), "Playing query becomes true");
}

static bool terminalStatesAreStickyAndKeepTheirCause()
{
    Room abortedRoom(nullptr, QStringLiteral("02_1v1"));
    GameSessionController aborted(abortedRoom);
    aborted.abort(GameSessionController::TerminationCause::Disconnected);
    if (!expect(aborted.state() == GameSessionController::State::Aborted,
                "abort enters Aborted")
        || !expect(aborted.terminationCause()
                       == GameSessionController::TerminationCause::Disconnected,
                   "abort records its cause")
        || !expect(aborted.isTerminal(), "Aborted is terminal")
        || !expect(!GameSessionControllerTestAccess::transition(
                       aborted, GameSessionController::State::Waiting),
                   "terminal state rejects later transitions"))
        return false;

    aborted.abort(GameSessionController::TerminationCause::Shutdown);
    if (!expect(aborted.terminationCause()
                    == GameSessionController::TerminationCause::Disconnected,
                "later abort does not overwrite the original cause"))
        return false;

    Room finishedRoom(nullptr, QStringLiteral("02_1v1"));
    GameSessionController finished(finishedRoom);
    GameSessionControllerTestAccess::finish(finished);
    return expect(finished.state() == GameSessionController::State::Finished,
                  "normal completion enters Finished")
        && expect(finished.terminationCause()
                      == GameSessionController::TerminationCause::GameOver,
                  "normal completion records GameOver")
        && expect(finished.isTerminal(), "Finished is terminal");
}

} // namespace

int runGameSessionControllerTests()
{
    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    return legalLifecycleReachesPlayingOnlyAfterGameReady()
            && terminalStatesAreStickyAndKeepTheirCause()
        ? 0 : 2;
}
