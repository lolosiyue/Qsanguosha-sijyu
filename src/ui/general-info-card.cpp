#include "general-info-card.h"
#include "engine.h"
#include "general.h"
#include "oracle_helper.h"
#include "player.h"
#include "server-info.h"
#include "settings.h"
#include "skill.h"

#include <QCoreApplication>
#include <QColor>
#include <QEvent>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QRegularExpression>
#include <QScreen>
#include <QWidget>

namespace
{
const char *const LazyGeneralProperty = "qsanGeneralInfoCard";

QString tr(const char *text)
{
    return QCoreApplication::translate("GeneralInfoCard", text);
}

GeneralInfoCard::Palette &mutablePalette()
{
    static GeneralInfoCard::Palette palette;
    return palette;
}

QHash<QString, QString> &generalCache()
{
    static QHash<QString, QString> cache;
    return cache;
}

struct CachedAvatarCards
{
    QString key;
    GeneralInfoCard::AvatarCards cards;
};

QHash<const Player *, CachedAvatarCards> &avatarCache()
{
    static QHash<const Player *, CachedAvatarCards> cache;
    return cache;
}

QString blend(const QString &color, const QString &with, qreal amount)
{
    const QColor a(color), b(with);
    if (!a.isValid() || !b.isValid()) return color;
    return QColor::fromRgbF(a.redF() * (1 - amount) + b.redF() * amount,
                            a.greenF() * (1 - amount) + b.greenF() * amount,
                            a.blueF() * (1 - amount) + b.blueF() * amount).name();
}

QString chip(const QString &text, const QString &color, const QString &textColor = QStringLiteral("#FFFFFF"))
{
    return QStringLiteral("<span style=\"background-color:%1; color:%2; font-size:8pt; font-weight:normal;\">"
                          "&nbsp;%3&nbsp;</span>").arg(color, textColor, text.toHtmlEscaped());
}

QString skillChips(const Skill *skill, const Player *owner, bool derived, bool greyed)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    // Greyed (invalid) skills keep one grey tone so no chip makes them look active.
    const auto colored = [greyed](const QString &color) { return greyed ? QStringLiteral("#BAB8BA") : color; };
    QStringList chips;
    if (skill) {
        switch (skill->getFrequency(owner)) {
        case Skill::Compulsory: chips << chip(tr("Compulsory"), colored(p.compulsory)); break;
        case Skill::Wake: chips << chip(tr("Wake"), colored(p.wake)); break;
        case Skill::Club: chips << chip(tr("Club"), colored(p.club)); break;
        default: break;
        }
        if (skill->isLimitedSkill()) chips << chip(tr("Limited"), colored(p.limited));
        if (skill->isShiMingSkill()) chips << chip(tr("Mission"), colored(p.mission));
        if (skill->isLordSkill()) chips << chip(tr("Lord"), colored(p.lord));
        if (skill->isChangeSkill()) chips << chip(tr("Switch"), colored(p.change));
        if (skill->isHideSkill()) chips << chip(tr("Hidden"), colored(p.hidden));
    }
    if (derived) chips << chip(tr("Derived"), colored(p.derived));
    return chips.join(QStringLiteral("&nbsp;"));
}

QString skillHeading(const QString &name, const QString &chips, const QString &color)
{
    QString style = QStringLiteral("font-size:11pt; font-weight:bold;");
    if (!color.isEmpty()) style += QStringLiteral(" color:%1;").arg(color);
    QString heading = QStringLiteral("<span style=\"%1\">%2</span>").arg(style, name);
    if (!chips.isEmpty()) heading += QStringLiteral("&nbsp;&nbsp;") + chips;
    return heading;
}

QString mutedParagraph(const QString &html, bool small = false)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    return QStringLiteral("<p style=\"margin-top:2px; margin-bottom:4px; color:%1;%2\">%3</p>")
        .arg(p.muted, small ? QStringLiteral(" font-size:8pt;") : QString(), html);
}

QString generalTitle(const QString &generalName)
{
    QString title = Sanguosha->translate("#" + generalName);
    if (title.startsWith("#") && generalName.contains("_"))
        title = Sanguosha->translate("#" + generalName.split("_").last());
    return title.startsWith("#") ? QString() : title;
}

QString generalInformation(const QString &generalName)
{
    QString info = Sanguosha->translate("information:" + generalName);
    if (info.contains("information:") && generalName.contains("_"))
        info = Sanguosha->translate("information:" + generalName.split("_").last());
    return info.contains("information:") ? QString() : info;
}

QString genderIcon(const General *general)
{
    QString gender;
    if (general->isMale()) gender = QStringLiteral("male");
    else if (general->isFemale()) gender = QStringLiteral("female");
    else if (general->isNeuter()) gender = QStringLiteral("neuter");
    else if (general->isSexless()) gender = QStringLiteral("sexless");
    return gender.isEmpty() ? QString()
        : QStringLiteral("<img src='image/gender/%1.png' height=16/>").arg(gender);
}

QString hpIcons(int hp, int maxHp, int armor)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    QString html;
    if (maxHp > 0) {
        hp = qBound(0, hp, maxHp);
        // Long rows of magatamas wrap badly; a count reads better past six.
        if (maxHp <= 6) {
            for (int i = 0; i < hp; i++) html += QStringLiteral("<img src='image/system/magatamas/5.png' height=12/>");
            for (int i = hp; i < maxHp; i++) html += QStringLiteral("<img src='image/system/magatamas/0.png' height=12/>");
        } else {
            html += QStringLiteral("<img src='image/system/magatamas/5.png' height=12/>");
        }
        html += QStringLiteral("<span style=\"color:%1; font-size:9pt;\">&nbsp;%2/%3</span>")
            .arg(p.headText).arg(hp).arg(maxHp);
    }
    if (armor > 0) {
        // Resolved once: this also runs on seat hovers.
        static const QString file = QFile::exists(QStringLiteral("image/mark/@HuJia.png"))
            ? QStringLiteral("image/mark/@HuJia.png") : QStringLiteral("image/mark/@default.png");
        html += QStringLiteral("&nbsp;<img src='%1' height=16/>").arg(file);
        if (armor > 1)
            html += QStringLiteral("<span style=\"color:%1; font-size:9pt;\">×%2</span>").arg(p.headText).arg(armor);
    }
    return html;
}

struct HeaderRow
{
    const General *general = nullptr;
    QString name;
    QString kingdom;
    QString role;
    QString roleColor;
    QString stats;
    bool focused = true;
};

QString headerRow(const HeaderRow &row)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    QString kingdomColor = Sanguosha->getKingdomColor(row.kingdom.split("+").first());
    if (!QColor(kingdomColor).isValid()) kingdomColor = QStringLiteral("#6B5B45");
    // Darkened kingdom colour keeps white text readable on light kingdoms;
    // the unfocused general of a pair fades towards the body colour.
    QString band = QColor(kingdomColor).darker(135).name();
    if (!row.focused) band = blend(band, QStringLiteral("#7F7F7F"), 0.45);

    QString left;
    // A light chip stays readable on every kingdom band.
    if (!row.role.isEmpty()) left += chip(row.role, p.headSubText, row.roleColor) + QStringLiteral("&nbsp;");
    foreach (const QString &kingdom, row.kingdom.split("+", Qt::SkipEmptyParts))
        left += QStringLiteral("<img src='image/kingdom/icon/%1.png' height=20/>").arg(kingdom);
    left += QStringLiteral("&nbsp;<span style=\"font-size:13pt; font-weight:bold; color:%1;\">%2</span>")
        .arg(p.headText, row.name.toHtmlEscaped());
    if (row.general) {
        const QString title = generalTitle(row.general->objectName());
        if (!title.isEmpty())
            left += QStringLiteral("&nbsp;&nbsp;<span style=\"font-size:9pt; color:%1;\">%2</span>")
                .arg(p.headSubText, title.toHtmlEscaped());
    }
    QString right = row.general ? genderIcon(row.general) : QString();
    if (!row.stats.isEmpty()) right += QStringLiteral("&nbsp;") + row.stats;

    return QStringLiteral("<tr><td bgcolor=\"%1\" style=\"padding-left:8px; padding-right:8px; padding-top:5px; padding-bottom:5px;\">"
                          "<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"><tr>"
                          "<td valign=\"middle\">%2</td><td align=\"right\" valign=\"middle\">%3</td>"
                          "</tr></table></td></tr>").arg(band, left, right);
}

// A character scan: this runs on every seat hover, where a regex pass costs more.
int plainLength(const QString &html)
{
    int length = 0;
    bool inTag = false;
    for (const QChar c : html) {
        if (c == QLatin1Char('<')) inTag = true;
        else if (c == QLatin1Char('>')) inTag = false;
        else if (!inTag) ++length;
    }
    return length;
}

// Fixed logical widths: short generals stay compact, long text wraps instead of
// stretching across the screen. Logical pixels follow the device-pixel ratio.
int cardWidth(const QString &bodyHtml)
{
    const int length = plainLength(bodyHtml);
    int width = 540;
    if (length < 120) width = 360;
    else if (length < 360) width = 420;
    else if (length < 800) width = 480;
    // Small (phone) screens: never wider than most of the screen.
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        width = qMin(width, screen->availableGeometry().width() * 9 / 10);
    return width;
}

QString conceptFooter(const QStringList &sources)
{
    const QStringList concepts = oracleConcepts(sources);
    if (concepts.isEmpty()) return QString();
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    QString html = QStringLiteral("<hr/><p style=\"margin-top:2px; margin-bottom:0px; color:%1; font-size:8pt;\">相关概念：")
        .arg(p.muted);
    foreach (const QString &concept, concepts)
        html += QStringLiteral("<br/>· ") + concept;
    return html + QStringLiteral("</p>");
}

QString assemble(const QString &caption, const QStringList &headers, const QString &body)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    QString html = QStringLiteral("<table width=\"%1\" cellspacing=\"0\" cellpadding=\"0\" bgcolor=\"%2\"%3>")
        .arg(cardWidth(body)).arg(p.background)
        .arg(p.backgroundImage.isEmpty() ? QString() : QStringLiteral(" background=\"%1\"").arg(p.backgroundImage));
    if (!caption.isEmpty())
        html += QStringLiteral("<tr><td style=\"padding-left:8px; padding-right:8px; padding-top:3px; padding-bottom:3px; color:%1; font-size:9pt;\">%2</td></tr>")
            .arg(p.muted, caption.toHtmlEscaped().replace(QLatin1Char('\n'), QLatin1String("<br/>")));
    html += headers.join(QString());
    html += QStringLiteral("<tr><td style=\"padding-left:10px; padding-right:10px; padding-top:4px; padding-bottom:8px; color:%1;\">%2</td></tr></table>")
        .arg(p.ink, body);
    return html;
}

QString skillBlock(const Skill *skill, bool derived)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    const QString name = Sanguosha->translate(skill->objectName());
    QString html = QStringLiteral("<p style=\"margin-top:8px; margin-bottom:2px;\">%1</p>")
        .arg(skillHeading(name, skillChips(skill, nullptr, derived, false), derived ? p.derived : p.skillName));
    html += QStringLiteral("<p style=\"margin-top:0px; margin-bottom:0px;\">%1</p>")
        .arg(QString(skill->getDescription()).replace("\n", "<br/>"));
    const QString oracle = skill->getOracleText();
    if (!oracle.isEmpty()) html += mutedParagraph(QString(oracle).replace("\n", "<br/>"), true);
    return html;
}

// Same skill order and sources as General::getSkillDescription().
QString generalBody(const General *general)
{
    QString body;
    const QString oracle = general->getOracleText();
    if (!oracle.isEmpty()) body += mutedParagraph(QString(oracle).replace("\n", "<br/>"));
    const QString info = generalInformation(general->objectName());
    if (!info.isEmpty()) body += mutedParagraph(QString(info).replace("\n", "<br/>"));

    QStringList skillTexts;
    QStringList relateds = general->getRelatedSkillNames();
    foreach (const Skill *skill, general->getVisibleSkillList()) {
        body += skillBlock(skill, false);
        skillTexts << skill->getDescription();
        if (skill->getWakedSkills().isEmpty()) continue;
        foreach (const QString &waked, skill->getWakedSkills().split(",")) {
            if (!relateds.contains(waked)) relateds << waked;
        }
    }
    foreach (const QString &skillName, relateds) {
        if (general->hasSkill(skillName)) continue;
        const Skill *skill = Sanguosha->getSkill(skillName);
        if (skill && skill->isVisible()) {
            body += skillBlock(skill, true);
            skillTexts << skill->getDescription();
        }
    }
    return body + conceptFooter(QStringList() << oracle << skillTexts);
}

HeaderRow generalHeader(const General *general)
{
    HeaderRow row;
    row.general = general;
    row.name = Sanguosha->translate(general->objectName());
    row.kingdom = general->getKingdoms();
    row.stats = hpIcons(general->getStartHp(), general->getMaxHp(), general->getStartHujia());
    return row;
}

struct SkillIndex
{
    QString signature;
    QHash<QString, const Skill *> byName;
};

// Seat tooltips refresh on every avatar hover; translating every skill name each
// time dominated that cost, so the index is rebuilt only when the skill set changes.
// Keyed by pointer, but a reused address with a different skill set fails the
// signature check, and an equal skill set yields an equal index.
QHash<const Player *, SkillIndex> &skillIndexCache()
{
    static QHash<const Player *, SkillIndex> cache;
    return cache;
}

const QHash<QString, const Skill *> &skillIndex(const Player *player, const QList<const Skill *> &skills)
{
    QString signature = player->getGeneralName() + QLatin1Char('|') + player->getGeneral2Name();
    foreach (const Skill *skill, skills) signature += QLatin1Char('|') + skill->objectName();
    QHash<const Player *, SkillIndex> &cache = skillIndexCache();
    auto cached = cache.find(player);
    if (cached != cache.end() && cached->signature == signature) return cached->byName;
    if (cache.size() > 128) cache.clear();  // players of finished rooms

    SkillIndex index;
    index.signature = signature;
    const auto remember = [&index](const QString &skillName) {
        const Skill *skill = Sanguosha->getSkill(skillName);
        if (skill) index.byName.insert(Sanguosha->translate(skillName), skill);
    };
    foreach (const Skill *skill, skills) {
        index.byName.insert(Sanguosha->translate(skill->objectName()), skill);
        if (!skill->getWakedSkills().isEmpty())
            foreach (const QString &waked, skill->getWakedSkills().split(",")) remember(waked);
    }
    foreach (const General *general, QList<const General *>() << player->getGeneral() << player->getGeneral2()) {
        if (!general) continue;
        foreach (const QString &related, general->getRelatedSkillNames()) remember(related);
        foreach (const Skill *skill, general->getVisibleSkillList())
            if (!index.byName.contains(Sanguosha->translate(skill->objectName())))
                index.byName.insert(Sanguosha->translate(skill->objectName()), skill);
    }
    return cache.insert(player, index)->byName;
}

// Re-wraps the "<b>name</b>：" headings of Player::getSkillDescription(). Text and
// order stay as produced there; grey (invalid) and teal (related) spans keep their colour.
QString restyleSkillHeadings(const QString &description, const Player *player)
{
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();
    const QHash<QString, const Skill *> &skillsByName = skillIndex(player, player->getSkillList(false, false));
    const General *head = player->getGeneral();
    const General *deputy = player->getGeneral2();

    static const QRegularExpression heading(
        QStringLiteral("(<font color=\"#(?:bab8ba|01A5AF)\">)?<b>([^<#][^<]*)</b>："));
    QString result;
    int last = 0;
    QRegularExpressionMatchIterator it = heading.globalMatch(description);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += description.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();
        const QString context = match.captured(1);
        const QString name = match.captured(2);
        const bool greyed = context.contains(QLatin1String("bab8ba"), Qt::CaseInsensitive);
        const bool derived = context.contains(QLatin1String("01A5AF"), Qt::CaseInsensitive);
        const Skill *skill = skillsByName.value(name);
        QString chips = skillChips(skill, player, derived, greyed);
        // Dual generals: say which general a skill belongs to when only one has it.
        if (skill && head && deputy) {
            const bool headOwns = head->hasSkill(skill->objectName());
            const bool deputyOwns = deputy->hasSkill(skill->objectName());
            if (headOwns != deputyOwns) {
                const QString owner = chip(headOwns ? tr("Head general") : tr("Deputy general"),
                                           greyed ? QStringLiteral("#BAB8BA") : (headOwns ? p.head : p.deputy));
                chips = chips.isEmpty() ? owner : chips + QStringLiteral("&nbsp;") + owner;
            }
        }
        // Inside a grey or teal span an explicit colour would override that state.
        result += context + skillHeading(name, chips, context.isEmpty() ? p.skillName : QString())
            + QStringLiteral("<br/>");
    }
    result += description.mid(last);

    static const QRegularExpression instanceTag(QStringLiteral("<b>#(\\d+)</b>"));
    result.replace(instanceTag, QStringLiteral("<span style=\"color:%1; font-weight:bold;\">#\\1</span>").arg(p.muted));
    return result;
}

QString playerCard(const Player *player, const QString &description, const QString &restyled,
                   GeneralInfoCard::Focus focus, const QString &caption)
{
    using GeneralInfoCard::Focus;
    const General *head = player->getGeneral();
    const General *deputy = player->getGeneral2();
    const bool dual = head && deputy;
    if (!dual) focus = Focus::Head;
    const GeneralInfoCard::Palette &p = GeneralInfoCard::palette();

    QStringList headers;
    HeaderRow headRow;
    headRow.general = head;
    headRow.name = head ? Sanguosha->translate(head->objectName()) : Sanguosha->translate(player->getGeneralName());
    headRow.kingdom = player->getKingdom();
    headRow.stats = hpIcons(player->getHp(), player->getMaxHp(), player->getHujia());
    if (dual) {
        headRow.role = tr("Head general");
        headRow.roleColor = p.head;
        headRow.focused = focus == Focus::Head;
    }
    if (!headRow.name.isEmpty()) headers << headerRow(headRow);
    if (dual) {
        HeaderRow deputyRow;
        deputyRow.general = deputy;
        deputyRow.name = Sanguosha->translate(deputy->objectName());
        deputyRow.kingdom = player->getKingdom();
        deputyRow.role = tr("Deputy general");
        deputyRow.roleColor = p.deputy;
        deputyRow.focused = focus == Focus::Deputy;
        headers << headerRow(deputyRow);
    }

    const General *focused = focus == Focus::Deputy ? deputy : head;
    const QString oracle = focused ? focused->getOracleText() : QString();
    QString body;
    if (!oracle.isEmpty()) body += mutedParagraph(QString(oracle).replace("\n", "<br/>"));
    body += QStringLiteral("<p style=\"margin-top:6px; margin-bottom:0px;\">%1</p>").arg(restyled);
    body += conceptFooter(QStringList() << oracle << description);
    return assemble(caption, headers, body);
}

class LazyToolTipFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ToolTip) {
            auto *widget = qobject_cast<QWidget *>(watched);
            if (widget && widget->toolTip().isEmpty())
                widget->setToolTip(GeneralInfoCard::forGeneral(widget->property(LazyGeneralProperty).toString()));
        }
        return QObject::eventFilter(watched, event);
    }
};
}

namespace GeneralInfoCard
{
const Palette &palette()
{
    return mutablePalette();
}

void setPalette(const Palette &palette)
{
    mutablePalette() = palette;
    clearCache();
}

void clearCache()
{
    generalCache().clear();
    skillIndexCache().clear();
    avatarCache().clear();
}

QString forGeneral(const General *general)
{
    if (!general) return QString();
    // Skill::getDescription() switches to the "_p" text during a game.
    const QString key = general->objectName() + (ServerInfo.DuringGame ? QStringLiteral("|g") : QString())
        + (Config.value("EnableOracleConcepts", true).toBool() ? QStringLiteral("|c") : QString());
    QHash<QString, QString> &cache = generalCache();
    const auto cached = cache.constFind(key);
    if (cached != cache.constEnd()) return cached.value();
    const QString card = assemble(QString(), QStringList() << headerRow(generalHeader(general)), generalBody(general));
    cache.insert(key, card);
    return card;
}

QString forGeneral(const QString &generalName)
{
    if (generalName.isEmpty()) return QString();
    const General *general = Sanguosha->getGeneral(generalName);
    return general ? forGeneral(general) : Sanguosha->translate(generalName).toHtmlEscaped();
}

QString forPlayer(const Player *player, const Player *viewer, Focus focus, const QString &caption)
{
    if (!player) return QString();
    const QString description = player->getSkillDescription(viewer);
    return playerCard(player, description, restyleSkillHeadings(description, player), focus, caption);
}

AvatarCards forPlayerAvatars(const Player *player, const QString &description)
{
    AvatarCards cards;
    if (!player) return cards;
    // Hover re-reads the description; when nothing shown on the card changed, reuse it.
    QString key = description;
    key += QStringLiteral("\x1f%1/%2/%3/%4/%5/%6").arg(player->getHp()).arg(player->getMaxHp())
        .arg(player->getHujia()).arg(player->getKingdom(), player->getGeneralName(), player->getGeneral2Name());
    foreach (const Skill *skill, player->getSkillList(false, false))
        key += QLatin1Char(',') + QString::number(int(skill->getFrequency(player)));
    if (Config.value("EnableOracleConcepts", true).toBool()) key += QStringLiteral(",c");
    QHash<const Player *, CachedAvatarCards> &cache = avatarCache();
    const auto cached = cache.constFind(player);
    if (cached != cache.constEnd() && cached->key == key) return cached->cards;

    // The restyled body is shared; only the header focus and general oracle differ.
    const QString restyled = restyleSkillHeadings(description, player);
    cards.head = playerCard(player, description, restyled, Focus::Head, QString());
    if (player->getGeneral() && player->getGeneral2())
        cards.deputy = playerCard(player, description, restyled, Focus::Deputy, QString());
    if (cache.size() > 128) cache.clear();  // players of finished rooms
    cache.insert(player, CachedAvatarCards{key, cards});
    return cards;
}

void setLazyToolTip(QWidget *widget, const QString &generalName)
{
    if (!widget) return;
    static LazyToolTipFilter *filter = new LazyToolTipFilter(QCoreApplication::instance());
    widget->setProperty(LazyGeneralProperty, generalName);
    widget->setToolTip(QString());
    widget->installEventFilter(filter);
}
}
