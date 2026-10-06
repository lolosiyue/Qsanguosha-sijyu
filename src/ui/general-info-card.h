#ifndef GENERAL_INFO_CARD_H
#define GENERAL_INFO_CARD_H

#include <QString>

class General;
class Player;
class QWidget;

// One look for every "hover a general" tooltip. Only presentation lives here:
// all text still comes from translate(), Skill::getDescription() and
// Player::getSkillDescription(), so skill wording never changes.
namespace GeneralInfoCard
{
// Self-contained colours: the card reads the same on every QSS tooltip theme.
// A theme pack can replace these (see the GH-general-hover-beautify report).
struct Palette
{
    QString background = QStringLiteral("#FBF7EE");
    QString backgroundImage;  // optional, drawn behind the body (theme-pack slot)
    QString ink = QStringLiteral("#2B2118");
    QString muted = QStringLiteral("#8A8072");
    QString skillName = QStringLiteral("#8B2A12");
    QString rule = QStringLiteral("#D9CCB4");
    QString headText = QStringLiteral("#FFFFFF");
    QString headSubText = QStringLiteral("#F1E6CF");
    QString compulsory = QStringLiteral("#2E5E9E");
    QString limited = QStringLiteral("#B03A2E");
    QString wake = QStringLiteral("#7D3C98");
    QString mission = QStringLiteral("#B9770E");
    QString lord = QStringLiteral("#CA6F1E");
    QString change = QStringLiteral("#117A65");
    QString hidden = QStringLiteral("#5D6D7E");
    QString club = QStringLiteral("#1F618D");
    QString derived = QStringLiteral("#01A5AF");
    QString head = QStringLiteral("#6E2C00");
    QString deputy = QStringLiteral("#4A5A6A");
};

const Palette &palette();
// Replacing the palette drops cached cards.
void setPalette(const Palette &palette);
void clearCache();

// Static card for one general (choose dialogs, piles, KOF boxes). Cached.
QString forGeneral(const General *general);
// Falls back to the escaped translated name when the general is unknown.
QString forGeneral(const QString &generalName);

enum class Focus
{
    Head,
    Deputy
};

// Live card for a seat: current HP/armour and Player::getSkillDescription(viewer).
// caption is plain text shown above the header (e.g. the overview seat label).
QString forPlayer(const Player *player, const Player *viewer, Focus focus = Focus::Head,
                  const QString &caption = QString());
struct AvatarCards
{
    QString head;
    QString deputy;  // empty unless the player has two generals
};
// Both avatar cards of one seat from one Player::getSkillDescription() text, so the
// head and deputy avatars share a single description and restyle pass.
AvatarCards forPlayerAvatars(const Player *player, const QString &skillDescription);

// Builds the card on the first tooltip request instead of when the widget is created.
void setLazyToolTip(QWidget *widget, const QString &generalName);
}

#endif // GENERAL_INFO_CARD_H
