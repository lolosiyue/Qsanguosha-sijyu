#include "engine-bootstrap.h"
#include "card.h"
#include "engine.h"
#include "protocol.h"
#include "tui-card-text.h"
#include "tui-log-text.h"
#include "tui-renderer.h"

#include <QCoreApplication>
#include <QDebug>

#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) {
        ++failures;
        std::printf("[FAIL] %s\n", what);
    }
}

QString playerName(const QString &objectName)
{
    if (objectName == QLatin1String("sgs1"))
        return QStringLiteral("刘玄德");
    if (objectName == QLatin1String("sgs2"))
        return QStringLiteral("曹孟德");
    return objectName;
}

QVariantMap skillLog(const QString &type, const QString &from, const QStringList &tos,
                     const QString &cardString, const QStringList &arguments)
{
    return QVariantMap{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("log_type"), type},
        {QStringLiteral("from_player"), from},
        {QStringLiteral("to_players"), tos},
        {QStringLiteral("card_string"), cardString},
        {QStringLiteral("arguments"), arguments}};
}

bool hasNoPlaceholders(const QString &text)
{
    return !text.contains(QStringLiteral("%from")) && !text.contains(QStringLiteral("%to"))
        && !text.contains(QStringLiteral("%card")) && !text.contains(QStringLiteral("%arg"));
}

int firstConcreteCardId()
{
    for (int id = 0; id < Sanguosha->getCardCount(); ++id) {
        const Card *card = Sanguosha->getEngineCard(id);
        if (card != nullptr && card->getSuit() != Card::NoSuit && card->getNumber() > 0)
            return id;
    }
    return -1;
}

} // namespace

using namespace QSanProtocol;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    QString error;
    if (!EngineBootstrap::initialize(false, &error)) {
        qCritical() << "engine initialization failed:" << error;
        return 1;
    }

    const QString trigger = tuiSkillLogText(
        skillLog(QStringLiteral("#TriggerSkill"), QStringLiteral("sgs1"), {}, QString(),
                 {QStringLiteral("bahu"), QString(), QString(), QString(), QString()}),
        playerName);
    check(trigger.contains(QStringLiteral("刘玄德")),
          "skill log names the acting player, not its object name");
    check(trigger.contains(Sanguosha->translate(QStringLiteral("bahu"))),
          "skill log translates the skill argument");
    check(hasNoPlaceholders(trigger), "skill log leaves no unfilled placeholder");
    check(!trigger.startsWith(QStringLiteral("#TriggerSkill")),
          "skill log renders the template, not the raw log key");

    const int cardId = firstConcreteCardId();
    const QString discard = tuiSkillLogText(
        skillLog(QStringLiteral("$DiscardCard"), QStringLiteral("sgs1"), {},
                 QString::number(cardId), {QString(), QString(), QString(), QString(), QString()}),
        playerName);
    check(discard.contains(tuiCardDisplayText(cardId)),
          "card log shows the same card identity as the hand view");
    check(hasNoPlaceholders(discard), "card log leaves no unfilled placeholder");

    const QString damage = tuiSkillLogText(
        skillLog(QStringLiteral("#Damage"), QStringLiteral("sgs1"),
                 {QStringLiteral("sgs2")}, QString(),
                 {QStringLiteral("1"), QStringLiteral("fire"), QString(), QString(), QString()}),
        playerName);
    check(damage.contains(QStringLiteral("刘玄德")) && damage.contains(QStringLiteral("曹孟德")),
          "damage log names both sides");
    check(hasNoPlaceholders(damage), "damage log leaves no unfilled placeholder");

    const QString unknown = tuiSkillLogText(
        skillLog(QStringLiteral("#NoSuchLogTypeHere"), QStringLiteral("sgs1"), {}, QString(),
                 {QString(), QString(), QString(), QString(), QString()}),
        playerName);
    check(unknown.contains(QStringLiteral("#NoSuchLogTypeHere")),
          "an unknown log type degrades to its key instead of vanishing");

    for (const QString &text : {trigger, discard, damage, unknown}) {
        check(!text.contains(QLatin1Char('<')) && !text.contains(QChar(0x1b)),
              "log text is plain: no markup and no escape sequences");
    }

    // The battle log already carries #enterDying and #AcquireSkill, so the game
    // event channel must stay quiet about them or every line appears twice.
    const QString separator = tuiSkillLogText(
        skillLog(QStringLiteral("$AppendSeparator"), QString(), {}, QString(),
                 {QString(), QString(), QString(), QString(), QString()}),
        playerName);
    check(!separator.isEmpty() && !separator.contains(QStringLiteral("$AppendSeparator")),
          "the turn separator renders as a rule, not as its raw key");

    const QVariantMap dying{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), int(QSanProtocol::S_GAME_EVENT_PLAYER_DYING)},
        {QStringLiteral("player_name"), QStringLiteral("sgs2")}};
    check(tuiGameEventText(dying, playerName).isEmpty(),
          "dying is left to the battle log, not repeated as a game event");

    const QVariantMap acquire{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), int(QSanProtocol::S_GAME_EVENT_ACQUIRE_SKILL)},
        {QStringLiteral("player_name"), QStringLiteral("sgs1")},
        {QStringLiteral("skill_name"), QStringLiteral("bahu")}};
    check(tuiGameEventText(acquire, playerName).isEmpty(),
          "gaining a skill is left to the battle log");

    // Leaving dying state has no log template anywhere, so it only reaches the
    // player through this channel.
    const QVariantMap quitDying{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), int(QSanProtocol::S_GAME_EVENT_PLAYER_QUITDYING)},
        {QStringLiteral("player_name"), QStringLiteral("sgs2")}};
    check(tuiGameEventText(quitDying, playerName).contains(QStringLiteral("曹孟德")),
          "leaving dying state is announced by name");

    const QVariantMap changeHero{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), int(QSanProtocol::S_GAME_EVENT_CHANGE_HERO)},
        {QStringLiteral("player_name"), QStringLiteral("sgs1")},
        {QStringLiteral("general_name"), QStringLiteral("caocao")},
        {QStringLiteral("secondary"), false},
        {QStringLiteral("send_log"), true}};
    check(tuiGameEventText(changeHero, playerName).isEmpty(),
          "a hero change the server already logged is not repeated");
    QVariantMap silentChangeHero = changeHero;
    silentChangeHero[QStringLiteral("send_log")] = false;
    check(tuiGameEventText(silentChangeHero, playerName)
              .contains(Sanguosha->translate(QStringLiteral("caocao"))),
          "a hero change the server did not log is announced here");

    const QVariantMap chat{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("speaker"), QStringLiteral("sgs1")},
        {QStringLiteral("text"),
         QStringLiteral("<font color=#EEB422>已加入游戏</font>")}};
    const QString chatText = tuiPresentationEventText(
        QSanProtocol::S_COMMAND_SPEAK, QStringLiteral("sgs1: raw"), chat, playerName);
    check(!chatText.contains(QLatin1Char('<')) && chatText.contains(QStringLiteral("已加入游戏")),
          "chat strips server markup and keeps the message");
    check(chatText.contains(QStringLiteral("刘玄德")),
          "chat names the speaker, not their object name");

    const QVariantMap emotion{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("player_name"), QStringLiteral("sgs3")},
        {QStringLiteral("emotion"), QStringLiteral("thunder_slash")}};
    check(tuiPresentationEventText(QSanProtocol::S_COMMAND_SET_EMOTION,
              QStringLiteral("sgs3 emotion thunder_slash"), emotion, playerName).isEmpty(),
          "emotion stays out of the transcript");

    const QString useCard = tuiSkillLogText(
        skillLog(QStringLiteral("#UseCard"), QStringLiteral("sgs1"),
                 {QStringLiteral("sgs2")}, QString::number(cardId),
                 {QString(), QString(), QString(), QString(), QString()}),
        playerName);
    check(!useCard.contains(QStringLiteral("#UseCard")),
          "use-card log renders a sentence, not the raw log key");
    check(useCard.contains(QStringLiteral("刘玄德")) && useCard.contains(QStringLiteral("曹孟德")),
          "use-card log names the user and the target");
    check(hasNoPlaceholders(useCard), "use-card log leaves no unfilled placeholder");

    // lang writes several templates for the desktop log box; #AskForPeaches asks
    // for a <b><font>桃</font></b>.
    const QString peaches = tuiSkillLogText(
        skillLog(QStringLiteral("#AskForPeaches"), QStringLiteral("sgs1"),
                 {QStringLiteral("sgs2")}, QString(),
                 {QStringLiteral("2"), QString(), QString(), QString(), QString()}),
        playerName);
    check(!peaches.contains(QLatin1Char('<')) && !peaches.contains(QLatin1Char('>')),
          "a log template written with markup renders as plain text");
    check(peaches.contains(Sanguosha->translate(QStringLiteral("peach"))),
          "stripping the markup keeps the word it wrapped");

    check(TuiRenderer::plainText(QStringLiteral("闪[方块7]<br>使用同名的牌可获得 <b>1</b> 枚G币"))
              == QStringLiteral("闪[方块7] 使用同名的牌可获得 1 枚G币"),
          "a line break survives as a separator instead of gluing two sentences");
    check(TuiRenderer::plainText(QStringLiteral("a<br/>b<br />c")) == QStringLiteral("a b c"),
          "every spelling of the break tag is recognised");
    check(TuiRenderer::plainText(QStringLiteral("3 < 4")) == QStringLiteral("3 < 4"),
          "an unterminated tag is treated as the text a player typed");
    check(TuiRenderer::plainText(QStringLiteral("&lt;b&gt; &amp; &nbsp;x"))
              == QStringLiteral("<b> & x"),
          "escaped characters are unescaped once, not re-read as tags");

    // lang has no template for the "played in response" variant, so the log box
    // synthesises the sentence and so must the transcript.
    const QString respond = tuiSkillLogText(
        skillLog(QStringLiteral("#UseCard_Resp"), QStringLiteral("sgs1"), {},
                 QString::number(cardId),
                 {QString(), QString(), QString(), QString(), QString()}),
        playerName);
    check(!respond.contains(QStringLiteral("#UseCard_Resp"))
              && respond.contains(QStringLiteral("刘玄德"))
              && respond.contains(tuiCardDisplayText(cardId)),
          "a played-in-response log renders a sentence, not the raw log key");
    check(respond != useCard,
          "playing a card in response is not narrated as using it");

    const QVariantMap bgm{{QStringLiteral("schema_version"), 1},
        {QStringLiteral("event"), int(QSanProtocol::S_GAME_EVENT_CHANGE_BGM)},
        {QStringLiteral("path"), QStringLiteral("audio/system/test.ogg")},
        {QStringLiteral("stop_current"), true}};
    check(tuiGameEventText(bgm, playerName).isEmpty(),
          "a presentation-only game event stays out of the transcript");

    std::printf("[AUTOTEST] TUI_LOG_TEXT_RESULT status=%s trigger=%s discard=%s damage=%s\n",
        failures == 0 ? "PASS" : "FAIL", trigger.toUtf8().constData(),
        discard.toUtf8().constData(), damage.toUtf8().constData());
    return failures == 0 ? 0 : 1;
}
