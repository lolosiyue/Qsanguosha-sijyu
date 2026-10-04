#include "procedural-skin.h"
#include "engine.h"
#include "general.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>

namespace {
const QLatin1String kScheme("gen:");

struct Spec
{
    QString kind;
    QString arg;
    QHash<QString, QString> params;

    QString value(const QString &key, const QString &fallback = QString()) const
    {
        return params.value(key, fallback);
    }
    int number(const QString &key, int fallback) const
    {
        bool ok = false;
        const int n = params.value(key).toInt(&ok);
        return ok ? n : fallback;
    }
    QColor color(const QString &key, const QString &fallback = QStringLiteral("transparent")) const
    {
        return QColor(params.value(key, fallback));
    }
};

Spec parse(const QString &uri)
{
    Spec spec;
    const QString body = uri.mid(kScheme.size());
    const int query = body.indexOf('?');
    const QString path = query < 0 ? body : body.left(query);
    const int slash = path.indexOf('/');
    spec.kind = slash < 0 ? path : path.left(slash);
    spec.arg = slash < 0 ? QString() : path.mid(slash + 1);
    if (query >= 0) {
        for (const QString &pair : body.mid(query + 1).split('&', Qt::SkipEmptyParts))
            spec.params.insert(pair.section('=', 0, 0), pair.section('=', 1));
    }
    return spec;
}

QFont themeFont(int pixelSize, bool bold = true)
{
    // Qt falls back to a sans-serif CJK font where this family is missing.
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(qMax(6, pixelSize));
    font.setBold(bold);
    return font;
}

QString translated(const QString &name)
{
    const QString text = Sanguosha->translate(name);
    return text.isEmpty() ? name : text;
}

QColor kingdomColor(const QString &kingdom)
{
    const QColor color(Sanguosha->getKingdomColor(kingdom));
    return color.isValid() ? color : QColor(0x80, 0x80, 0x80);
}

// Draws text as large as maxPixel allows inside rect; vertical text stacks one character per line.
void drawFitted(QPainter &painter, const QRect &rect, const QString &text, const QColor &color,
    bool vertical, int maxPixel)
{
    if (text.isEmpty() || rect.isEmpty())
        return;
    painter.save();
    painter.setPen(color);
    if (vertical) {
        const int count = text.size();
        const int pixel = qMin(maxPixel, qMin(rect.width(), rect.height() / count));
        painter.setFont(themeFont(pixel));
        const int lineHeight = QFontMetrics(painter.font()).height();
        const int step = qMin(lineHeight, rect.height() / count);
        int y = rect.top() + (rect.height() - step * count) / 2;
        for (const QChar ch : text) {
            painter.drawText(QRect(rect.left(), y, rect.width(), step), Qt::AlignCenter, QString(ch));
            y += step;
        }
    } else {
        int pixel = qMin(maxPixel, rect.height());
        QFont font = themeFont(pixel);
        while (pixel > 6 && QFontMetrics(font).horizontalAdvance(text) > rect.width())
            font.setPixelSize(--pixel);
        painter.setFont(font);
        painter.drawText(rect, Qt::AlignCenter, text);
    }
    painter.restore();
}

void drawPanel(QPainter &painter, const QRectF &rect, const QColor &fill, const QColor &border,
    qreal borderWidth, qreal radius, bool ellipse = false)
{
    painter.save();
    painter.setPen(borderWidth > 0 && border.alpha() > 0 ? QPen(border, borderWidth) : QPen(Qt::NoPen));
    painter.setBrush(fill.alpha() > 0 ? QBrush(fill) : QBrush(Qt::NoBrush));
    const QRectF inner = rect.adjusted(borderWidth / 2, borderWidth / 2, -borderWidth / 2, -borderWidth / 2);
    if (ellipse)
        painter.drawEllipse(inner);
    else
        painter.drawRoundedRect(inner, radius, radius);
    painter.restore();
}

QColor stateColor(QColor base, const QString &state)
{
    if (state == QLatin1String("disabled"))
        return QColor(0x9e, 0x9e, 0x9e);
    if (state == QLatin1String("hover"))
        return base.lighter(120);
    if (state == QLatin1String("down"))
        return base.darker(130);
    return base;
}

QColor skillColor(const QString &type)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("proactive"), QColor(0xa0, 0x6a, 0x10)},
        {QStringLiteral("frequent"), QColor(0x3f, 0x76, 0x30)},
        {QStringLiteral("compulsory"), QColor(0x2f, 0x5a, 0x9a)},
        {QStringLiteral("awaken"), QColor(0x74, 0x3a, 0x9a)},
        {QStringLiteral("oneoff"), QColor(0x9a, 0x2f, 0x2f)},
        {QStringLiteral("attachedlord"), QColor(0xb0, 0x30, 0x60)},
        {QStringLiteral("change"), QColor(0x2a, 0x80, 0x80)},
        {QStringLiteral("changeF"), QColor(0x2a, 0x80, 0x80)},
    };
    return colors.value(type, QColor(0x60, 0x60, 0x60));
}

QColor roleColor(const QString &role)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("lord"), QColor(0xc8, 0x96, 0x10)},
        {QStringLiteral("loyalist"), QColor(0xc6, 0x28, 0x28)},
        {QStringLiteral("rebel"), QColor(0x2e, 0x7d, 0x32)},
        {QStringLiteral("renegade"), QColor(0x15, 0x65, 0xc0)},
        {QStringLiteral("careerist"), QColor(0x6a, 0x1b, 0x9a)},
        {QStringLiteral("unknown"), QColor(0x75, 0x75, 0x75)},
    };
    // Hegemony death marks use kingdom names.
    return colors.value(role, kingdomColor(role));
}

QSize defaultSize(const Spec &spec, const JsonObject &buttons)
{
    if (spec.kind == QLatin1String("role"))
        return spec.arg.startsWith(QLatin1String("small-")) ? QSize(24, 30) : QSize(26, 26);
    if (spec.kind == QLatin1String("button")) {
        const QVariantList entry = buttons.value(spec.arg).toList();
        if (entry.size() >= 3)
            return QSize(entry.at(1).toInt(), entry.at(2).toInt());
        return QSize(60, 26);
    }
    if (spec.kind == QLatin1String("skill")) {
        // The skill width argument is 1 (wide), 2 (medium) or 3 (narrow).
        const int code = spec.number(QStringLiteral("w"), 1);
        return QSize(code == 3 ? 45 : code == 2 ? 67 : 134, 26);
    }
    if (spec.kind == QLatin1String("card"))
        return QSize(200, 280);
    return QSize(40, 40);
}
}

namespace ProceduralSkin
{
bool isUri(const QString &fileName)
{
    return fileName.startsWith(kScheme);
}

QPixmap render(const QString &uri, const JsonObject &buttons)
{
    const Spec spec = parse(uri);
    QSize size = defaultSize(spec, buttons);
    if (spec.kind != QLatin1String("button") && spec.kind != QLatin1String("skill"))
        size = QSize(spec.number(QStringLiteral("w"), size.width()), spec.number(QStringLiteral("h"), size.height()));
    if (size.isEmpty())
        return QPixmap(1, 1);

    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRect rect(QPoint(0, 0), size);
    const int w = size.width(), h = size.height();
    const QString &kind = spec.kind;
    const QString &arg = spec.arg;

    if (kind == QLatin1String("fill")) {
        drawPanel(painter, rect, spec.color(QStringLiteral("c")), spec.color(QStringLiteral("b")),
            spec.number(QStringLiteral("bw"), 1), spec.number(QStringLiteral("r"), 0),
            spec.value(QStringLiteral("shape")) == QLatin1String("dot"));
    } else if (kind == QLatin1String("label")) {
        drawPanel(painter, rect, spec.color(QStringLiteral("c")), spec.color(QStringLiteral("b")),
            spec.number(QStringLiteral("bw"), 1), spec.number(QStringLiteral("r"), 0));
        const QString text = spec.value(QStringLiteral("tr")) == QLatin1String("1") ? translated(arg) : arg;
        drawFitted(painter, rect.adjusted(2, 2, -2, -2), text, spec.color(QStringLiteral("fg"), QStringLiteral("#000000")),
            spec.value(QStringLiteral("v")) == QLatin1String("1"), spec.number(QStringLiteral("px"), h));
    } else if (kind == QLatin1String("general")) {
        const General *general = Sanguosha->getGeneral(arg);
        const QColor base = kingdomColor(general ? general->getKingdom() : QString());
        drawPanel(painter, rect, base.darker(140), base, qMax(1, w / 40), 0);
        QString name = translated(arg);
        // Tiny avatars only have room for the leading characters.
        if (w <= 48 && h <= 48)
            name = name.left(2);
        const bool vertical = h > w * 1.25;
        drawFitted(painter, rect.adjusted(w / 10, h / 12, -w / 10, -h / 12), name, Qt::white, vertical,
            vertical ? w * 2 / 3 : h / 2);
    } else if (kind == QLatin1String("card")) {
        const General *general = Sanguosha->getGeneral(arg);
        const QColor border = general ? kingdomColor(general->getKingdom()) : QColor(0x8b, 0x2a, 0x1a);
        drawPanel(painter, rect, QColor(0xff, 0xf8, 0xe1), border, qMax(2, w / 40), w / 25);
        // Leave the top-left column free for the suit and number overlays.
        const QRect textRect(w * 28 / 100, h * 8 / 100, w * 64 / 100, h * 84 / 100);
        drawFitted(painter, textRect, translated(arg), QColor(0x20, 0x20, 0x20), true, w / 3);
    } else if (kind == QLatin1String("suit")) {
        static const QHash<QString, QString> glyphs = {
            {QStringLiteral("spade"), QStringLiteral("♠")}, {QStringLiteral("heart"), QStringLiteral("♥")},
            {QStringLiteral("club"), QStringLiteral("♣")}, {QStringLiteral("diamond"), QStringLiteral("♦")},
        };
        const bool red = arg == QLatin1String("heart") || arg == QLatin1String("diamond")
            || arg == QLatin1String("no_suit_red");
        drawFitted(painter, rect, glyphs.value(arg), red ? spec.color(QStringLiteral("red"), QStringLiteral("#c61010"))
            : spec.color(QStringLiteral("black"), QStringLiteral("#101010")), false, h);
    } else if (kind == QLatin1String("number")) {
        static const QStringList points = {QString(), QStringLiteral("A"), QStringLiteral("2"), QStringLiteral("3"),
            QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
            QStringLiteral("9"), QStringLiteral("10"), QStringLiteral("J"), QStringLiteral("Q"), QStringLiteral("K")};
        const int point = arg.toInt();
        // The suit area starts below the top of the number area, so keep the number in the upper part.
        drawFitted(painter, QRect(0, 0, w, h * 3 / 5), point > 0 && point < points.size() ? points.at(point) : arg,
            spec.color(QStringLiteral("c"), QStringLiteral("#101010")), false, h);
    } else if (kind == QLatin1String("kingdom")) {
        const QColor base = kingdomColor(arg);
        if (spec.value(QStringLiteral("m")) == QLatin1String("icon")) {
            drawPanel(painter, rect, base, base.darker(150), 1, 0, true);
            drawFitted(painter, rect.adjusted(3, 3, -3, -3), translated(arg).left(1), Qt::white, false, h);
        } else {
            drawPanel(painter, rect, base, base.darker(150), 1, qMin(w, h) / 4);
        }
    } else if (kind == QLatin1String("role")) {
        // Role art file names look like "rebel", "rebel-1" or "small-rebel".
        QString role = arg.startsWith(QLatin1String("small-")) ? arg.mid(6) : arg;
        role = role.section('-', 0, 0);
        const QColor color = roleColor(role);
        const bool unknown = role == QLatin1String("unknown");
        if (spec.value(QStringLiteral("m")) == QLatin1String("death")) {
            const QString text = unknown ? spec.value(QStringLiteral("unknown"), QStringLiteral("?")) : translated(role);
            drawPanel(painter, rect.adjusted(w / 12, h / 6, -w / 12, -h / 6), QColor(0xff, 0xff, 0xff, 0xb0), color, 4, 8);
            drawFitted(painter, rect.adjusted(w / 6, h / 4, -w / 6, -h / 4), text, color, false, h / 2);
        } else {
            drawPanel(painter, rect, color, color.darker(150), 1, qMin(w, h) / 4);
            drawFitted(painter, rect.adjusted(2, 2, -2, -2), unknown ? QStringLiteral("?") : translated(role).left(1),
                Qt::white, false, h);
        }
    } else if (kind == QLatin1String("equip")) {
        drawPanel(painter, rect, QColor(0xf3, 0xe6, 0xc4, 0xe6), QColor(0x6b, 0x4a, 0x2a), 1, 2);
        drawFitted(painter, rect.adjusted(3, 1, -3, -1), translated(arg), QColor(0x20, 0x20, 0x20), false, h);
    } else if (kind == QLatin1String("judge")) {
        drawPanel(painter, rect, QColor(0x4a, 0x2a, 0x10), QColor(0xe0, 0xc0, 0x80), 1, 0, true);
        drawFitted(painter, rect.adjusted(3, 3, -3, -3), translated(arg).left(1), QColor(0xff, 0xe8, 0xb0), false, h);
    } else if (kind == QLatin1String("button")) {
        const QVariantList entry = buttons.value(arg).toList();
        const QString text = entry.isEmpty() ? translated(arg) : entry.at(0).toString();
        const QString state = spec.value(QStringLiteral("state"));
        drawPanel(painter, rect, stateColor(QColor(0x7a, 0x3b, 0x1e), state), QColor(0x3a, 0x1a, 0x0a), 1, 3);
        const bool vertical = h > w * 1.4;
        drawFitted(painter, rect.adjusted(3, 3, -3, -3), text,
            state == QLatin1String("disabled") ? QColor(0xe0, 0xe0, 0xe0) : QColor(0xff, 0xf3, 0xd6),
            vertical, vertical ? w : h * 2 / 5);
    } else if (kind == QLatin1String("skill")) {
        // QSanInvokeSkillButton paints the skill name over this background.
        const QColor base = skillColor(arg);
        drawPanel(painter, rect, stateColor(base, spec.value(QStringLiteral("state"))), base.darker(160), 1, 3);
    }
    painter.end();
    return pixmap;
}
}
