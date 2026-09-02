#include "clientlogbox.h"
#include "settings.h"
#include "engine.h"
#include "client-log-formatter.h"
#include "roomscene.h"
#include "client.h"

ClientLogBox::ClientLogBox(QWidget *parent)
    : QTextEdit(parent)
{
    setReadOnly(true);

    QScrollBar *bar = verticalScrollBar();
    QFile file("qss/scroll.qss");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        bar->setStyleSheet(stream.readAll());
    }
}

void ClientLogBox::appendLog(const QString &type, const QString &from_general, const QStringList &tos,
    QString card_str, QString arg, QString arg2, QString arg3, QString arg4, QString arg5)
{
    if (Self->hasFlag("marshalling")) return;

    if (type == "$AppendSeparator") {
        append(QString(tr("<font color='%1'>------------------------------</font>")).arg(UiConfig.TextEditColor.name()));
        return;
    }

    ClientLogFormatRequest request;
    request.type = type;
    request.from = from_general;
    request.tos = tos;
    request.cardString = card_str;
    request.arg = arg;
    request.arg2 = arg2;
    request.arg3 = arg3;
    request.arg4 = arg4;
    request.arg5 = arg5;

    ClientLogFormatStyle style;
    style.phrases.usingText = tr("using");
    style.phrases.playingText = tr("playing");
    style.phrases.recastingText = tr("recasting");
    style.phrases.useSkillText = tr("use skill");
    style.phrases.carryOutText = tr("carry out");
    style.phrases.effectText = tr("effect");
    style.phrases.skillNoCost = tr("%from %2 [%1] %3");
    style.phrases.skillCost = tr("%from %3 [%1] %4, and the cost is %2");
    style.phrases.asNoSub = tr("%from %4 [%1] %5, %3 [%2]");
    style.phrases.asSub = tr("%from %5 [%1] %6 %4 %2 as %3");
    style.phrases.filterAs = QStringLiteral("%from 的 %2 因“%1”效果视为 %3 %4");
    style.phrases.plain = tr("%from %2 %1");
    style.phrases.targetSuffix = tr(", target is %to");
    style.phrases.selfName = QStringLiteral("自己");
    style.translate = [](const QString &key) { return Sanguosha->translate(key); };
    style.cardLogName = [](const Card *card) { return card->getLogName(); };
    style.playerName = [](const QString &name) { return ClientInstance->getPlayerName(name); };
    style.wrapFrom = [this](const QString &text) { return bold(text, Qt::green); };
    style.wrapTo = [this](const QString &text) { return bold(text, Qt::red); };
    style.wrapArg = [this](const QString &text) { return bold(text, Qt::yellow); };
    style.wrapCard = [this](const QString &text) { return bold(text, Qt::yellow); };
    style.onUseCardTargets = [](const QString &from, const QStringList &targets) {
        foreach (const QString &to, targets)
            RoomSceneInstance->showIndicator(from, to);
    };

    const QString log = formatClientLog(request, style);
    if (log.isEmpty())
        return;

    const QString html = append(QString("<font color='%2'>%1</font>").arg(log).arg(UiConfig.TextEditColor.name()));
    if (type.contains("#Guhuo"))
        RoomSceneInstance->setGuhuoLog(html);
}

QString ClientLogBox::bold(const QString &str, QColor color) const
{
    return QString("<font color='%1'><b>%2</b></font>").arg(color.name()).arg(str);
}

void ClientLogBox::appendLog(const QStringList &log_str)
{
	if(log_str.length()>8&&(log_str.first().startsWith("$")||log_str.first().startsWith("#")))
		appendLog(log_str[0], log_str[1], log_str[2].isEmpty()?QStringList():log_str[2].split("+"),
		log_str[3], log_str[4], log_str[5], log_str[6], log_str[7], log_str[8]);
	else{
		QString err_string = tr("Log string is not well formatted: %1").arg(log_str.join(","));
        append(QString("<font color='%1'>%2</font>").arg(UiConfig.TextEditColor.name()).arg(err_string));
	}
}

QString ClientLogBox::append(const QString &text)
{
    QString to_append = QString("<p style=\"margin:3px 2px; line-height:120%;\">%1</p>").arg(text);
    QTextEdit::append(to_append);
    return to_append;
}
