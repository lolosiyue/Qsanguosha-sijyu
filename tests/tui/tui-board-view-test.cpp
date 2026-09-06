#include "client-game-state.h"
#include "tui-board-view.h"
#include "tui-screen.h"

#include "engine-bootstrap.h"
#include "card.h"
#include "engine.h"
#include "general.h"
#include "player.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QString>
#include <QVariantList>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

QString goldenPath(const QString &name)
{
    return QStringLiteral("%1/tui/golden/%2.txt").arg(QStringLiteral(QSAN_TEST_SOURCE_DIR), name);
}

void compareGolden(const QString &name, const QString &actual)
{
    if (qEnvironmentVariableIsSet("QSAN_TUI_GOLDEN_WRITE")) {
        QFile file(goldenPath(name));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
              "golden file opens for writing");
        file.write(actual.toUtf8());
        return;
    }
    QFile file(goldenPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        ++failures;
        std::printf("[FAIL] missing golden %s\n", qPrintable(name));
        return;
    }
    const QString expected = QString::fromUtf8(file.readAll());
    if (expected != actual) {
        ++failures;
        std::printf("[FAIL] golden %s differs\n--- expected ---\n%s\n--- actual ---\n%s\n",
            qPrintable(name), qPrintable(expected), qPrintable(actual));
    }
}

// Mirrors TuiApplicationController's production wiring
// (tui-application-controller.cpp:60-71) closely enough for a drawing test:
// real engine translation for names/kingdoms, no hints or targets since
// TuiBoardView never asks for them.
TuiResolvers testResolvers()
{
    TuiResolvers resolvers;
    resolvers.card = [](int cardId) {
        const Card *card = Sanguosha->getEngineCard(cardId);
        if (card == nullptr)
            return QStringLiteral("#%1").arg(cardId);
        QString name = Sanguosha->translate(card->objectName());
        if (name.isEmpty())
            name = card->objectName();
        QString suit;
        if (card->getSuit() != Card::NoSuit) {
            suit = Sanguosha->translate(card->getSuitString());
            if (suit.isEmpty())
                suit = card->getSuitString();
        }
        const QString number = card->getNumber() > 0 ? card->getNumberString() : QString();
        if (suit.isEmpty() && number.isEmpty())
            return name;
        return QStringLiteral("%1[%2%3]").arg(name, suit, number);
    };
    resolvers.name = [](const QString &name) {
        if (name.isEmpty() || Sanguosha == nullptr)
            return name;
        const QString translated = Sanguosha->translate(name);
        return translated.isEmpty() ? name : translated;
    };
    resolvers.player = [](const QString &objectName) { return objectName; };
    resolvers.kingdom = [](const QString &generalName) {
        if (Sanguosha == nullptr || generalName.isEmpty())
            return QString();
        const General *general = Sanguosha->getGeneral(generalName);
        return general == nullptr ? QString() : general->getKingdom();
    };
    return resolvers;
}

void addPlayer(ClientGameState *state, const QString &name, int seat, const QString &general,
              int hp, int maxHp, const QString &role)
{
    state->addPlayer(name);
    state->setPlayerValue(name, QStringLiteral("object_name"), name);
    if (seat > 0)
        state->setPlayerValue(name, QStringLiteral("seat"), seat);
    state->setPlayerValue(name, QStringLiteral("general"), general);
    state->setPlayerValue(name, QStringLiteral("hp"), hp);
    state->setPlayerValue(name, QStringLiteral("max_hp"), maxHp);
    state->setPlayerValue(name, QStringLiteral("role"), role);
    state->setPlayerAlive(name, true);
}

int firstCardOfClassWithSuit(const char *className)
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->isKindOf(className) && card->getSuit() != Card::NoSuit)
            return id;
    }
    return -1;
}

void testBoardWaiting()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("server_name"), QStringLiteral("测试服务器")},
        {QStringLiteral("game_mode"), QStringLiteral("05p")},
        {QStringLiteral("player_count"), 5}});
    state.setSelfName(QStringLiteral("sgs1"));
    // Before GAME_START nobody has a seat or a general yet -- that absence is
    // exactly what isGameStarted() in tui-board-view.cpp keys off of.
    state.addPlayer(QStringLiteral("sgs1"));
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("object_name"), QStringLiteral("sgs1"));
    state.addPlayer(QStringLiteral("sgs2"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("object_name"), QStringLiteral("sgs2"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("general"), QStringLiteral("caocao"));
    state.addPlayer(QStringLiteral("sgs3"));
    state.setPlayerValue(QStringLiteral("sgs3"), QStringLiteral("object_name"), QStringLiteral("sgs3"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2"), QStringLiteral("sgs3")});

    TuiScreen screen;
    screen.resize(24, 80);
    TuiBoardView view(testResolvers());
    view.render(&screen, state, TuiBoardViewState{});

    const QString text = screen.toPlainText();
    check(!text.contains(QString::fromUtf8("体力")), "no seats are drawn before GAME_START");
    check(!text.contains(QChar(0x2665)) && !text.contains(QChar(0x2661)),
          "no hp hearts are drawn before GAME_START either");
    check(text.contains(QStringLiteral("3/5")), "the waiting room shows join progress");
    check(text.contains(QStringLiteral("已就绪")) && text.contains(QStringLiteral("未就绪")),
          "the waiting room shows each joined player's ready state");
    compareGolden(QStringLiteral("board-waiting"), text);
}

void testBoard05pPlay()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("05p")},
        {QStringLiteral("player_count"), 5}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("zhaoyun"), 3, 4,
              QStringLiteral("lord"));
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("caocao"), 4, 4,
              QStringLiteral("rebel"));
    addPlayer(&state, QStringLiteral("sgs3"), 3, QStringLiteral("zhangfei"), 4, 4,
              QStringLiteral("loyalist"));
    addPlayer(&state, QStringLiteral("sgs4"), 4, QStringLiteral("diaochan"), 0, 3,
              QStringLiteral("rebel"));
    state.setPlayerAlive(QStringLiteral("sgs4"), false);
    addPlayer(&state, QStringLiteral("sgs5"), 5, QStringLiteral("sunquan"), 3, 4,
              QStringLiteral("renegade"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2"), QStringLiteral("sgs3"),
                          QStringLiteral("sgs4"), QStringLiteral("sgs5")});
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 3);
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("sgs1"));
    state.setGameValue(QStringLiteral("draw_pile_count"), 42);
    QVariantList discards;
    for (int i = 0; i < 17; ++i)
        discards << i;
    state.setGameValue(QStringLiteral("discard_pile"), discards);

    // sgs3 (张飞) carries one equip and one judge card, exercising line 2's
    // count-only opponent format; sgs1 (self) carries the same shapes so the
    // golden also shows the wide self-cell format spelling names out.
    const int equipCard = firstCardOfClassWithSuit("Weapon");
    const int judgeCard = firstCardOfClassWithSuit("DelayedTrick");
    check(equipCard >= 0 && judgeCard >= 0, "the engine has at least one weapon and one delayed trick");
    state.setCardValue(equipCard, QStringLiteral("owner"), QStringLiteral("sgs3"));
    state.setCardValue(equipCard, QStringLiteral("place"), Player::PlaceEquip);
    state.setCardValue(judgeCard, QStringLiteral("owner"), QStringLiteral("sgs3"));
    state.setCardValue(judgeCard, QStringLiteral("place"), Player::PlaceDelayedTrick);

    // Five hand cards for self, each a distinct suited card so tuiCardDisplayText
    // (well, its stand-in resolvers.card here) has a suit and a rank to show.
    QList<int> handCards;
    for (const char *kind : {"Slash", "Jink", "Peach", "Nullification", "Snatch"}) {
        const int id = firstCardOfClassWithSuit(kind);
        if (id >= 0 && !handCards.contains(id))
            handCards.append(id);
    }
    check(handCards.size() == 5, "the fixture found five distinct hand cards");
    for (int id : handCards) {
        state.setCardValue(id, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(id, QStringLiteral("place"), Player::PlaceHand);
    }

    // Wider than the 09p/page2 fixture's 80 columns on purpose: at 80 columns
    // a 5-player room only fits 2 opponent-grid columns (spec §3.6's own
    // degradation ladder), which pages fine but does not exercise the full
    // three-region ring this golden is meant to show at a glance. 100 columns
    // is still a very ordinary terminal width and gets cellCols to 3.
    TuiScreen screen;
    screen.resize(24, 100);
    TuiBoardView view(testResolvers());
    TuiBoardViewState viewState;
    viewState.promptLine = QStringLiteral("出牌阶段");
    viewState.inputLine = QStringLiteral("1 -> p2");
    viewState.inputCursorColumn = 7;
    viewState.logLines = {QStringLiteral("时语 对 孙权"), QStringLiteral("  使用【杀】"),
                          QStringLiteral("孙权 打出【闪】"), QStringLiteral("张飞 摸了 2 张牌")};
    view.render(&screen, state, viewState);

    const QString text = screen.toPlainText();
    check(text.contains(QString::fromUtf8("▶")), "the current player is marked");
    check(!text.contains(QStringLiteral("‹")), "a single page shows no page indicator");
    check(text.contains(QString::fromUtf8("✖阵亡")), "the dead player is marked");
    check(text.contains(QString::fromUtf8("(我)")), "self is marked in its own cell");
    check(text.contains(QStringLiteral("[5]")), "all five hand cards are indexed");
    compareGolden(QStringLiteral("board-05p-play"), text);
}

void testBoard09pPage2()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("09p")},
        {QStringLiteral("player_count"), 9}});
    state.setSelfName(QStringLiteral("sgs1"));
    const QStringList generals{QStringLiteral("zhaoyun"), QStringLiteral("caocao"),
        QStringLiteral("zhangfei"), QStringLiteral("diaochan"), QStringLiteral("sunquan"),
        QStringLiteral("guanyu"), QStringLiteral("zhouyu"), QStringLiteral("simayi"),
        QStringLiteral("lvbu")};
    QStringList names;
    for (int i = 1; i <= 9; ++i) {
        const QString name = QStringLiteral("sgs%1").arg(i);
        names << name;
        addPlayer(&state, name, i, generals.at(i - 1), 4, 4, QStringLiteral("loyalist"));
    }
    state.setPlayerNames(names);
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 1);
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("sgs2"));
    state.setGameValue(QStringLiteral("draw_pile_count"), 60);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList{});

    TuiScreen screen;
    screen.resize(24, 80);
    TuiBoardView view(testResolvers());
    TuiBoardViewState viewState;
    viewState.page = 1;
    viewState.promptLine = QStringLiteral("等待其他玩家");
    view.render(&screen, state, viewState);

    const QString text = screen.toPlainText();
    check(text.contains(QStringLiteral("‹2/")), "a paged board shows which page is on screen");
    check(text.contains(QString::fromUtf8("(我)")), "self stays on screen on every page");
    compareGolden(QStringLiteral("board-09p-page2"), text);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical("engine initialization failed: %s", qPrintable(error));
        return 1;
    }

    testBoardWaiting();
    testBoard05pPlay();
    testBoard09pPage2();

    std::printf("[AUTOTEST] TUI_BOARD_VIEW_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
