#include "chooseoptionsbox.h"
#include "button.h"
#include "client.h"
#include "clientstruct.h"
#include "engine.h"
#include "settings.h"
#include "timed-progressbar.h"

#include <QGraphicsProxyWidget>

ChooseOptionsBox::ChooseOptionsBox()
    : progressBar(NULL)
{
}

QRectF ChooseOptionsBox::boundingRect() const
{
    bool hasDisabled = !disabledOptions.isEmpty();

    int totalOpts = options.size() + (hasDisabled ? disabledOptions.size() : 0);
    int width = getButtonWidth() * (qMax(totalOpts, 1)) + outerBlankWidth * 2
                + (qMax(totalOpts, 1) - 1) * interval;

    int max = 0;
    foreach (const QString &str, options)
        max = qMax(max, str.split("+").length());
    if (hasDisabled) {
        foreach (const QString &str, disabledOptions)
            max = qMax(max, str.split("+").length());
    }

    int height = topBlankWidth + max * defaultButtonHeight + (max - 1) * interval + bottomBlankWidth;

    if (!tip.isEmpty())
        height += tipHeight;

    if (hasDisabled) {
        height += defaultButtonHeight + interval + 10;
    }

    if (ServerInfo.OperationTimeout != 0)
        height += 12;

    return QRectF(0, 0, width, height);
}

void ChooseOptionsBox::chooseOption(const QStringList &options)
{
    chooseOption(options, QStringList());
}

void ChooseOptionsBox::chooseOption(const QStringList &options, const QStringList &disabledOptions)
{
    this->options = options;
    this->disabledOptions = disabledOptions;

    QString titleText = Sanguosha->translate(skillName) + " " + tr("Please choose:");
    if (!tip.isEmpty()) {
        QString tipText = QString("%1:%2").arg(skillName).arg(tip);
        QString translated = Sanguosha->translate(tipText);
        if (translated == tipText)
            translated = Sanguosha->translate(tip);
        if (translated == tip)
            titleText = translated;
    }
    title = titleText;
    prepareGeometryChange();

    populateButtons();
    moveToCenter();
    show();
    setupProgressBar();
}

void ChooseOptionsBox::populateButtons()
{
    int buttonWidth = getButtonWidth();

    foreach (const QString &option, options) {
        foreach (const QString &choice, option.split("+")) {
            addButton(choice, true);
        }
    }

    foreach (const QString &option, disabledOptions) {
        foreach (const QString &choice, option.split("+")) {
            addButton(choice, false);
        }
    }

    // layout buttons
    const int startY = topBlankWidth + (!tip.isEmpty() ? tipHeight : 0);
    int btnIdx = 0;

    // enabled buttons first
    int btnMaxX = 0;
    for (int i = 0; i < buttons.size(); ++i) {
        Button *button = buttons.at(i);
        QPointF pos;
        pos.setX(outerBlankWidth + i * (buttonWidth + interval));
        pos.setY(startY + defaultButtonHeight / 2);
        button->setPos(pos);
        btnMaxX = i;
    }

    // disabled buttons below
    if (!disabledButtons.isEmpty()) {
        int dispY = startY + (buttons.isEmpty() ? 0 : defaultButtonHeight + interval);
        for (int i = 0; i < disabledButtons.size(); ++i) {
            Button *button = disabledButtons.at(i);
            QPointF pos;
            pos.setX(outerBlankWidth + i * (buttonWidth + interval));
            pos.setY(dispY + defaultButtonHeight / 2);
            button->setPos(pos);
        }
    }
}

void ChooseOptionsBox::addButton(const QString &choice, bool enabled)
{
    int buttonWidth = getButtonWidth();
    Button *button = new Button(translate(choice), QSizeF(buttonWidth, defaultButtonHeight));
    button->setObjectName(choice);
    button->setParentItem(this);

    // skill tooltip
    QString text = QString("%1:%2").arg(skillName).arg(choice);
    QString translated = Sanguosha->translate(text);
    const Skill *skill = Sanguosha->getSkill(choice);
    if (skill) {
        button->setToolTip(skill->getDescription(Self));
    } else {
        QString original_tooltip = QString(":%1").arg(text);
        QString tooltip = Sanguosha->translate(original_tooltip);
        if (tooltip == original_tooltip) {
            original_tooltip = QString(":%1").arg(choice);
            tooltip = Sanguosha->translate(original_tooltip);
        }
        if (tooltip != original_tooltip)
            button->setToolTip(tooltip);
    }

    if (enabled) {
        connect(button, &Button::clicked, this, &ChooseOptionsBox::reply);
        buttons << button;
    } else {
        button->setOpacity(0.4);
        disabledButtons << button;
    }
}

void ChooseOptionsBox::setupProgressBar()
{
    if (ServerInfo.OperationTimeout != 0) {
        if (!progressBar) {
            progressBar = new QSanCommandProgressBar();
            progressBar->setMaximumWidth(boundingRect().width() - 16);
            progressBar->setMaximumHeight(12);
            progressBar->setTimerEnabled(true);
            progressBarItem = new QGraphicsProxyWidget(this);
            progressBarItem->setWidget(progressBar);
            progressBarItem->setPos(boundingRect().center().x() - progressBarItem->boundingRect().width() / 2,
                                    boundingRect().height() - 20);
            connect(progressBar, &QSanCommandProgressBar::timedOut, this, &ChooseOptionsBox::reply);
        }
        progressBar->setCountdown(QSanProtocol::S_COMMAND_MULTIPLE_CHOICE);
        progressBar->show();
    }
}

void ChooseOptionsBox::paintLayout(QPainter *painter)
{
    if (tip.isEmpty()) return;

    painter->save();
    painter->setFont(Config.SmallFont);
    painter->setPen(Qt::white);

    QString tipText = QString("%1:%2").arg(skillName).arg(tip);
    QString translated = Sanguosha->translate(tipText);
    if (translated == tipText)
        translated = Sanguosha->translate(tip);
    if (translated == tip)
        return;

    QRectF rect = boundingRect();
    QRectF tipRect(outerBlankWidth, topBlankWidth, rect.width() - outerBlankWidth * 2, tipHeight);
    painter->drawText(tipRect, Qt::AlignLeft | Qt::AlignVCenter, translated);
    painter->restore();
}

void ChooseOptionsBox::reply()
{
    QString choice = sender()->objectName();
    if (choice.isEmpty())
        choice = options.first();
    ClientInstance->onPlayerMakeChoice(choice);
}

int ChooseOptionsBox::getButtonWidth() const
{
    if (options.isEmpty() && disabledOptions.isEmpty())
        return minButtonWidth;

    QFontMetrics fontMetrics(Config.UIFont);
    int biggest = 0;

    QStringList allOptions = options + disabledOptions;
    foreach (const QString &section, allOptions) {
        foreach (const QString &choice, section.split("+")) {
            int width = fontMetrics.width(translate(choice));
            if (width > biggest)
                biggest = width;
        }
    }

    biggest += 20;
    return qMax(biggest, minButtonWidth);
}

QString ChooseOptionsBox::translate(const QString &option) const
{
    QString title = QString("%1:%2").arg(skillName).arg(option);
    QString translated = Sanguosha->translate(title);
    if (translated == title)
        translated = Sanguosha->translate(option);
    return translated;
}

void ChooseOptionsBox::clear()
{
    if (progressBar != NULL) {
        progressBar->hide();
        progressBar->deleteLater();
        progressBar = NULL;
    }

    foreach (Button *button, buttons)
        button->deleteLater();
    buttons.clear();

    foreach (Button *button, disabledButtons)
        button->deleteLater();
    disabledButtons.clear();

    options.clear();
    disabledOptions.clear();
    tip.clear();

    disappear();
}
