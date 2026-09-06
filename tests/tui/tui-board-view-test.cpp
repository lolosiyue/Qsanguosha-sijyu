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
#include <QRegularExpression>
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

// Up to `count` distinct card ids of `className` that carry a suit, in
// ascending id order -- used where a fixture needs two non-colliding cards of
// the same kind (e.g. one weapon for an opponent, a different one for self).
QList<int> cardsOfClassWithSuit(const char *className, int count)
{
    QList<int> ids;
    for (int id = 0; id < Sanguosha->getCardCount() && ids.size() < count; ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->isKindOf(className) && card->getSuit() != Card::NoSuit)
            ids.append(id);
    }
    return ids;
}

// Up to `count` distinct suited card ids of any class -- used to overflow a
// hand pane where the specific card kind does not matter.
QList<int> suitedCardIds(int count)
{
    QList<int> ids;
    for (int id = 0; id < Sanguosha->getCardCount() && ids.size() < count; ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->getSuit() != Card::NoSuit)
            ids.append(id);
    }
    return ids;
}

void testBoardWaiting()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("server_name"), QStringLiteral("测试服务器")},
        {QStringLiteral("game_mode"), QStringLiteral("05p")},
        {QStringLiteral("player_count"), 5}});
    state.setSelfName(QStringLiteral("sgs1"));
    // Before GAME_START nobody has a seat yet -- that absence is exactly what
    // isGameStarted() in tui-board-view.cpp keys off of. But sgs1 and sgs2
    // carry real hp/max_hp/general values, i.e. exactly the data that would
    // produce a full seat ring (hearts, a general name, a kingdom) if the
    // ring were ever drawn before GAME_START -- a vacuous "no seats" check
    // that only looked for a string ("体力") the drawing code never emits
    // would not catch that regression; this fixture makes it demonstrable.
    // sgs3 is left without a general so the readiness proxy still shows a
    // "未就绪" player alongside the "已就绪" ones.
    state.addPlayer(QStringLiteral("sgs1"));
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("object_name"), QStringLiteral("sgs1"));
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("general"), QStringLiteral("zhaoyun"));
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("hp"), 3);
    state.setPlayerValue(QStringLiteral("sgs1"), QStringLiteral("max_hp"), 4);
    state.addPlayer(QStringLiteral("sgs2"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("object_name"), QStringLiteral("sgs2"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("general"), QStringLiteral("caocao"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("hp"), 4);
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("max_hp"), 4);
    state.addPlayer(QStringLiteral("sgs3"));
    state.setPlayerValue(QStringLiteral("sgs3"), QStringLiteral("object_name"), QStringLiteral("sgs3"));
    state.setPlayerValue(QStringLiteral("sgs3"), QStringLiteral("hp"), 2);
    state.setPlayerValue(QStringLiteral("sgs3"), QStringLiteral("max_hp"), 3);
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2"), QStringLiteral("sgs3")});
    // A current_player also being set (with nobody seated yet) means the ▶
    // check below is a real check, not one that would trivially pass because
    // nothing in the fixture could ever produce that marker either way.
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("sgs2"));

    TuiScreen screen;
    screen.resize(24, 80);
    TuiBoardView view(testResolvers());
    view.render(&screen, state, TuiBoardViewState{});

    const QString text = screen.toPlainText();
    check(!text.contains(QChar(0x2665)) && !text.contains(QChar(0x2661)),
          "no hp hearts are drawn before GAME_START, even though players have real hp/max_hp");
    check(!text.contains(QString::fromUtf8("(我)")),
          "no self marker is drawn before GAME_START");
    check(!text.contains(QStringLiteral("▶")),
          "no current-player marker is drawn before GAME_START");
    check(!text.contains(QRegularExpression(QStringLiteral("\\[\\d+\\]"))),
          "no seat-index bracket is drawn before GAME_START");
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
    // sgs2's max_hp is 12 (> 8) to exercise hpText()'s numeric fallback
    // ("♥ 9/12") instead of the hearts glyph run, and it is also drawn
    // face-down to exercise the "翻面" status marker.
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("caocao"), 9, 12,
              QStringLiteral("rebel"));
    state.setPlayerValue(QStringLiteral("sgs2"), QStringLiteral("faceup"), false);
    // sgs3 is also chained, to exercise the "连环" status marker alongside
    // its existing count-only equipment/judge format.
    addPlayer(&state, QStringLiteral("sgs3"), 3, QStringLiteral("zhangfei"), 4, 4,
              QStringLiteral("loyalist"));
    state.setPlayerValue(QStringLiteral("sgs3"), QStringLiteral("chained"), true);
    addPlayer(&state, QStringLiteral("sgs4"), 4, QStringLiteral("diaochan"), 0, 3,
              QStringLiteral("rebel"));
    state.setPlayerAlive(QStringLiteral("sgs4"), false);
    // sgs5 is dying (Global_Dying), to exercise the "濒死" status marker.
    addPlayer(&state, QStringLiteral("sgs5"), 5, QStringLiteral("sunquan"), 3, 4,
              QStringLiteral("renegade"));
    state.setPlayerValue(QStringLiteral("sgs5"), QStringLiteral("flags"),
                         QStringList{QStringLiteral("Global_Dying")});
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
    // count-only opponent format; sgs1 (self) carries a *different* equip and
    // judge card, exercising the wide self-cell format that spells the
    // names out in brackets instead of just counting them.
    const QList<int> weaponCards = cardsOfClassWithSuit("Weapon", 2);
    const QList<int> judgeCards = cardsOfClassWithSuit("DelayedTrick", 2);
    check(weaponCards.size() == 2 && judgeCards.size() == 2,
          "the engine has at least two weapons and two delayed tricks");
    state.setCardValue(weaponCards.at(0), QStringLiteral("owner"), QStringLiteral("sgs3"));
    state.setCardValue(weaponCards.at(0), QStringLiteral("place"), Player::PlaceEquip);
    state.setCardValue(judgeCards.at(0), QStringLiteral("owner"), QStringLiteral("sgs3"));
    state.setCardValue(judgeCards.at(0), QStringLiteral("place"), Player::PlaceDelayedTrick);
    state.setCardValue(weaponCards.at(1), QStringLiteral("owner"), QStringLiteral("sgs1"));
    state.setCardValue(weaponCards.at(1), QStringLiteral("place"), Player::PlaceEquip);
    state.setCardValue(judgeCards.at(1), QStringLiteral("owner"), QStringLiteral("sgs1"));
    state.setCardValue(judgeCards.at(1), QStringLiteral("place"), Player::PlaceDelayedTrick);

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
    check(text.contains(QStringLiteral("♥ 9/12")),
          "a max hp above 8 falls back to the numeric hp display");
    check(text.contains(QString::fromUtf8("翻面")), "a face-down player is marked");
    check(text.contains(QString::fromUtf8("连环")), "a chained player is marked");
    check(text.contains(QString::fromUtf8("濒死")), "a dying player is marked");
    check(text.contains(QString::fromUtf8("装【")),
          "self spells out equipment names in brackets instead of just a count");
    check(text.contains(QString::fromUtf8("判【")),
          "self spells out judge names in brackets instead of just a count");
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
    // sgs2 sits on page 1 and is never rendered here -- picking them as the
    // current player would leave the ▶-on-a-non-self-cell path untested on
    // this page. sgs9 is one of the seats page 2 actually shows.
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("sgs9"));
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
    check(text.contains(QStringLiteral("▶")),
          "the current player is marked even when they are not self and not on page 1");
    compareGolden(QStringLiteral("board-09p-page2"), text);
}

void testBoardHandOverflow()
{
    ClientGameState state;
    state.setSetup(QVariantMap{
        {QStringLiteral("game_mode"), QStringLiteral("02p")},
        {QStringLiteral("player_count"), 2}});
    state.setSelfName(QStringLiteral("sgs1"));
    addPlayer(&state, QStringLiteral("sgs1"), 1, QStringLiteral("zhaoyun"), 4, 4,
              QStringLiteral("lord"));
    addPlayer(&state, QStringLiteral("sgs2"), 2, QStringLiteral("caocao"), 4, 4,
              QStringLiteral("rebel"));
    state.setPlayerNames({QStringLiteral("sgs1"), QStringLiteral("sgs2")});
    state.setGameValue(QStringLiteral("status"), QStringLiteral("active"));
    state.setGameValue(QStringLiteral("round"), 1);
    state.setGameValue(QStringLiteral("current_player"), QStringLiteral("sgs1"));
    state.setGameValue(QStringLiteral("draw_pile_count"), 30);
    state.setGameValue(QStringLiteral("discard_pile"), QVariantList{});

    // Enough suited cards (of whatever kind) to spill past layoutHandLines()'s
    // 5-line cap at an ordinary 80-column width, exercising the "…(+N)"
    // overflow marker (spec §3.2).
    const QList<int> handCards = suitedCardIds(40);
    check(handCards.size() >= 30,
          "the engine has enough suited cards to overflow a 5-line hand pane");
    for (int id : handCards) {
        state.setCardValue(id, QStringLiteral("owner"), QStringLiteral("sgs1"));
        state.setCardValue(id, QStringLiteral("place"), Player::PlaceHand);
    }

    TuiScreen screen;
    screen.resize(24, 80);
    TuiBoardView view(testResolvers());
    TuiBoardViewState viewState;
    viewState.promptLine = QStringLiteral("出牌阶段");
    view.render(&screen, state, viewState);

    const QString text = screen.toPlainText();
    check(text.contains(QStringLiteral("…(+")),
          "an overflowing hand names how many cards are off-screen");
    compareGolden(QStringLiteral("board-hand-overflow"), text);
}

void testBoardTooSmall()
{
    // A state barely needs to exist here: render() bails out on geometry
    // before it ever looks at seats, so an empty (but self-named) state is
    // enough to exercise the usable == false path (tui-board-view.cpp
    // around line 536-538).
    ClientGameState state;
    state.setSelfName(QStringLiteral("sgs1"));

    TuiScreen screen;
    screen.resize(14, 52); // rows, cols -- below the 60x18 floor (spec §3.5)
    TuiBoardView view(testResolvers());
    view.render(&screen, state, TuiBoardViewState{});

    const QString text = screen.toPlainText();
    check(text.contains(QStringLiteral("60×18")),
          "the too-small message names the required size");
    check(text.contains(QStringLiteral("52×14")),
          "the too-small message names the actual (too small) size");
    compareGolden(QStringLiteral("board-too-small"), text);
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
    testBoardHandOverflow();
    testBoardTooSmall();

    std::printf("[AUTOTEST] TUI_BOARD_VIEW_RESULT status=%s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
