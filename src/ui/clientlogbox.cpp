#include "clientlogbox.h"
#include "settings.h"
#include "engine.h"
//#include "clientplayer.h"
//#include "client.h"
#include "roomscene.h"

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
        append(QString(tr("<font color='%1'>------------------------------</font>")).arg(Config.TextEditColor.name()));
        return;
    }

    QString log = Sanguosha->translate(type);

    if(card_str.isEmpty()){}
	else if (type.startsWith("$")) {
        QStringList log_name;
        foreach (QString str, card_str.split("+")) {
            const Card *card = Sanguosha->getEngineCard(str.toInt());
            if (type == "$JudgeResult" || type == "$PasteCard")
				card = Sanguosha->getCard(str.toInt());
            if (card) log_name << card->getLogName();
        }
        log.replace("%card", bold(log_name.join(", "), Qt::yellow));/*
        if (!arg5.isEmpty()) {
            arg5 = bold(Sanguosha->translate(arg5), Qt::yellow);
            log.replace("%arg5", arg5);
        }
        if (!arg4.isEmpty()) {
            arg4 = bold(Sanguosha->translate(arg4), Qt::yellow);
            log.replace("%arg4", arg4);
        }
        if (!arg3.isEmpty()) {
            arg3 = bold(Sanguosha->translate(arg3), Qt::yellow);
            log.replace("%arg3", arg3);
        }
        if (!arg2.isEmpty()) {
            arg2 = bold(Sanguosha->translate(arg2), Qt::yellow);
            log.replace("%arg2", arg2);
        }
        if (!arg.isEmpty()) {
            arg = bold(Sanguosha->translate(arg), Qt::yellow);
            log.replace("%arg", arg);
        }
        log = QString("<font color='%2'>%1</font>").arg(log).arg(Config.TextEditColor.name());
        append(log);
        return;*/
    }else if (type.startsWith("#UseCard") && from_general!="") {
        // do Indicator animation
        const Card *card = Card::Parse(card_str);
        if (card == nullptr) return;
        foreach(QString t, tos)
            RoomSceneInstance->showIndicator(from_general, t);
        QString card_name = bold(card->getLogName(), Qt::yellow);
        QString reason = tr("using");
        if (type.endsWith("_Resp")) reason = tr("playing");
        if (type.endsWith("_Recast")) reason = tr("recasting");
		QString skill_name = card->getSkillName();
        if (card->isVirtualCard()) {
            bool eff = (card->getTypeId()>0&&card->getSkillName(false)!=skill_name);
            QString meth = eff ? tr("carry out") : tr("use skill");
            QString suffix = eff ? tr("effect") : "";
            QStringList subcard_list;
            foreach (int id, card->getSubcards())
                subcard_list << bold(Sanguosha->getEngineCard(id)->getLogName(), Qt::yellow);
            skill_name = bold(Sanguosha->translate(skill_name), Qt::yellow);
            if (card->inherits("SkillCard") && !card->isKindOf("YanxiaoCard")) {
                if (subcard_list.isEmpty() || !card->willThrow()) log = tr("%from %2 [%1] %3").arg(skill_name).arg(meth).arg(suffix);
                else log = tr("%from %3 [%1] %4, and the cost is %2").arg(skill_name).arg(subcard_list.join(", ")).arg(meth).arg(suffix);
            } else {// || card->getSkillName().contains("guhuo"))
                if (subcard_list.isEmpty()) log = tr("%from %4 [%1] %5, %3 [%2]").arg(skill_name).arg(card_name).arg(reason).arg(meth).arg(suffix);
                else log = tr("%from %5 [%1] %6 %4 %2 as %3").arg(skill_name).arg(subcard_list.join(", ")).arg(card_name).arg(reason).arg(meth).arg(suffix);
            }
            //delete card;
        } else if (skill_name != "") {
            skill_name = bold(Sanguosha->translate(skill_name), Qt::yellow);
            QString subcard_str = bold(Sanguosha->getEngineCard(card->getId())->getLogName(), Qt::yellow);/*
            if (card->isKindOf("DelayedTrick"))
                log = tr("%from %5 [%1] %6 %4 %2 as %3").arg(skill_name).arg(subcard_str).arg(card_name).arg(reason).arg(tr("use skill")).arg("");
            else*/
                log = QString("%from 的 %2 因“%1”效果视为 %3 %4").arg(skill_name).arg(subcard_str).arg(card_name).arg(reason);
                //log = tr("Due to the effect of [%1], %from %4 %2 as %3").arg(skill_name).arg(subcard_str).arg(card_name).arg(reason);
        } else
            log = tr("%from %2 %1").arg(card_name).arg(reason);
        if (tos.length()>0) log.append(tr(", target is %to"));
    } else {
        const Card *card = Card::Parse(card_str);
        if (card)
			log.replace("%card", bold(card->getLogName(), Qt::yellow));
		else{
			QStringList log_name;
			foreach (QString str, card_str.split("+")) {
				card = Sanguosha->getCard(str.toInt());
				if (card) log_name << card->getLogName();
			}
			log.replace("%card", bold(log_name.join(", "), Qt::yellow));
		}
    }

    if (from_general!="")
		log.replace("%from", bold(ClientInstance->getPlayerName(from_general), Qt::green));
    if (tos.length()>0) {
        QStringList to_list;
        foreach(QString t, tos){
            if(t==from_general) to_list << "自己";
			else to_list << ClientInstance->getPlayerName(t);
		}
		log.replace("%to", bold(to_list.join(", "), Qt::red));
    }

    if (log.contains("%arg5"))
        log.replace("%arg5", bold(Sanguosha->translate(arg5), Qt::yellow));

    if (log.contains("%arg4"))
        log.replace("%arg4", bold(Sanguosha->translate(arg4), Qt::yellow));

    if (log.contains("%arg3"))
        log.replace("%arg3", bold(Sanguosha->translate(arg3), Qt::yellow));

    if (log.contains("%arg2"))
        log.replace("%arg2", bold(Sanguosha->translate(arg2), Qt::yellow));

    if (log.contains("%arg"))
        log.replace("%arg", bold(Sanguosha->translate(arg), Qt::yellow));

    log = append(QString("<font color='%2'>%1</font>").arg(log).arg(Config.TextEditColor.name()));
    if (type.contains("#Guhuo"))
        RoomSceneInstance->setGuhuoLog(log);
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
        append(QString("<font color='%1'>%2</font>").arg(Config.TextEditColor.name()).arg(err_string));
	}
}

QString ClientLogBox::append(const QString &text)
{
    QString to_append = QString("<p style=\"margin:3px 2px; line-height:120%;\">%1</p>").arg(text);
    QTextEdit::append(to_append);
    return to_append;
}

