#include "tui-application-controller.h"

#include "core/client-game-state.h"
#include "core/game-view-state.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <cstdio>
#include <utility>

static int failures = 0;

static void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

struct TuiPresentationTestAccess
{
    static ClientGameState *state(TuiApplicationController &controller)
    {
        return controller.m_core.state();
    }

    static void replaceRenderer(TuiApplicationController &controller,
                                int *nameCalls)
    {
        TuiRenderer::Resolvers resolvers;
        resolvers.name = [nameCalls](const QString &name) {
            ++*nameCalls;
            return name;
        };
        controller.m_renderer = TuiRenderer(false, std::move(resolvers));
    }

    static void emitStateChanged(TuiApplicationController &controller)
    {
        emit controller.m_session.stateChanged();
    }

    static void status(TuiApplicationController &controller)
    {
        controller.handleCommand({TuiCommandType::Status});
    }

    static const GameViewState &view(const TuiApplicationController &controller)
    {
        return controller.m_gameViewState;
    }

    static quint64 latestEventSequence(const TuiApplicationController &controller)
    {
        const QList<GamePresentationEvent> events = controller.m_eventStream.events();
        return events.isEmpty() ? 0 : events.last().sequence;
    }
};

namespace {

void seedState(ClientGameState *state)
{
    state->reset();
    state->setSelfName(QStringLiteral("self"));
    state->setPlayerNames({QStringLiteral("self"), QStringLiteral("other")});
    state->setPlayerValue(QStringLiteral("self"), QStringLiteral("seat"), 1);
    state->setPlayerValue(QStringLiteral("self"), QStringLiteral("general"),
                         QStringLiteral("caocao"));
    state->setPlayerValue(QStringLiteral("self"), QStringLiteral("hp"), 4);
    state->setPlayerValue(QStringLiteral("self"), QStringLiteral("max_hp"), 4);
    state->setPlayerMark(QStringLiteral("self"), QStringLiteral("@token"), 1);
    state->setPlayerValue(QStringLiteral("other"), QStringLiteral("seat"), 2);
    state->setPlayerValue(QStringLiteral("other"), QStringLiteral("general"),
                         QStringLiteral("liubei"));
    state->setPlayerValue(QStringLiteral("other"), QStringLiteral("hp"), 3);
    state->setPlayerValue(QStringLiteral("other"), QStringLiteral("max_hp"), 4);
    state->setGameValue(QStringLiteral("current_player"), QStringLiteral("self"));
    state->setGameValue(QStringLiteral("current_phase"), QStringLiteral("play"));
    state->appendPresentationEvent(9001, QStringLiteral("first event"));
}

const GameViewPlayer *findPlayer(const GameViewState &view, const QString &name)
{
    for (const GameViewPlayer &player : view.players) {
        if (player.name == name)
            return &player;
    }
    return nullptr;
}

void testClassicRefreshIsOnDemand()
{
    TuiApplicationOptions options;
    options.ansiEnabled = false;
    TuiApplicationController controller(options);
    int nameCalls = 0;
    TuiPresentationTestAccess::replaceRenderer(controller, &nameCalls);
    seedState(TuiPresentationTestAccess::state(controller));

    for (int i = 0; i < 300; ++i)
        TuiPresentationTestAccess::emitStateChanged(controller);

    check(nameCalls == 0,
          "classic state notification burst does not rebuild the shared projection");
    check(TuiPresentationTestAccess::view(controller).players.isEmpty(),
          "classic burst leaves the cached projection untouched");

    TuiPresentationTestAccess::status(controller);
    const GameViewState &firstView = TuiPresentationTestAccess::view(controller);
    const GameViewPlayer *firstSelf = findPlayer(firstView, QStringLiteral("self"));
    check(nameCalls > 0, "status performs the deferred projection");
    check(firstSelf != nullptr && firstSelf->hp == 4,
          "status exposes the newest player hp");
    check(firstSelf != nullptr && firstSelf->marks.value(QStringLiteral("@token")) == 1,
          "status exposes the newest player mark");
    check(TuiPresentationTestAccess::latestEventSequence(controller) == 1,
          "status imports the latest presentation event sequence");

    TuiPresentationTestAccess::state(controller)->setPlayerValue(
        QStringLiteral("self"), QStringLiteral("hp"), 2);
    TuiPresentationTestAccess::state(controller)->setPlayerMark(
        QStringLiteral("self"), QStringLiteral("@token"), 7);
    TuiPresentationTestAccess::state(controller)->appendPresentationEvent(
        9002, QStringLiteral("second event"));
    for (int i = 0; i < 250; ++i)
        TuiPresentationTestAccess::emitStateChanged(controller);

    const GameViewPlayer *staleSelf = findPlayer(
        TuiPresentationTestAccess::view(controller), QStringLiteral("self"));
    check(staleSelf != nullptr && staleSelf->hp == 4,
          "a later classic burst does not silently rebuild the cached projection");
    check(TuiPresentationTestAccess::latestEventSequence(controller) == 1,
          "a later classic burst does not advance the event projection");

    TuiPresentationTestAccess::status(controller);
    const GameViewPlayer *latestSelf = findPlayer(
        TuiPresentationTestAccess::view(controller), QStringLiteral("self"));
    check(latestSelf != nullptr && latestSelf->hp == 2,
          "a later status sees the newest player hp");
    check(latestSelf != nullptr && latestSelf->marks.value(QStringLiteral("@token")) == 7,
          "a later status sees the newest player mark");
    check(TuiPresentationTestAccess::latestEventSequence(controller) == 2,
          "a later status sees the newest presentation event sequence");
}

void testBoardRefreshRemainsLive()
{
    TuiApplicationOptions options;
    options.boardMode = true;
    TuiApplicationController controller(options);
    int nameCalls = 0;
    TuiPresentationTestAccess::replaceRenderer(controller, &nameCalls);
    seedState(TuiPresentationTestAccess::state(controller));

    TuiPresentationTestAccess::emitStateChanged(controller);
    const GameViewPlayer *self = findPlayer(
        TuiPresentationTestAccess::view(controller), QStringLiteral("self"));
    check(nameCalls > 0, "board state notification builds the live projection");
    check(self != nullptr && self->hp == 4,
          "board state notification exposes the current player hp");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testClassicRefreshIsOnDemand();
    testBoardRefreshRemainsLive();
    if (failures != 0)
        std::printf("tui-presentation-refresh-test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
