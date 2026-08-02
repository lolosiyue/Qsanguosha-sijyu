#include "wind.h"
#include "ol.h"
#include "mobile.h"
#include "mountain.h"

#include "card.h"
#include "clientplayer.h"
#include "engine.h"
#include "generaloverview.h"
#include "server-info.h"

#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QGroupBox>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QAbstractButton *makeDialogButton(const Card *card, QWidget *parent)
{
    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(card->objectName()), parent);
    button->setObjectName(card->objectName());
    button->setToolTip(card->getDescription());
    return button;
}

}

QHash<QString, GuhuoDialog *> GuhuoDialogs;

GuhuoDialog *GuhuoDialog::getInstance(const QString &object, bool left, bool right, bool play_only,
    bool slash_combined, bool delayed_tricks, bool update)
{
    if (update || GuhuoDialogs.value(object, nullptr) == nullptr) {
        delete GuhuoDialogs.take(object);
        GuhuoDialogs[object] = new GuhuoDialog(object, left, right, play_only, slash_combined, delayed_tricks);
    }
    return GuhuoDialogs.value(object);
}

GuhuoDialog::GuhuoDialog(const QString &object, bool left, bool right, bool play_only,
    bool slash_combined, bool delayed_tricks)
    : play_only(play_only), slash_combined(slash_combined), delayed_tricks(delayed_tricks)
{
    setObjectName(object);
    setWindowTitle(Sanguosha->translate(object));
    group = new QButtonGroup(this);
    group->setExclusive(false);

    QHBoxLayout *content = new QHBoxLayout;
    if (left)
        content->addWidget(createLeft());
    if (right)
        content->addWidget(createRight());
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(content);
    setLayout(layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectCard(QAbstractButton *)));
}

void GuhuoDialog::prepareOptions()
{
    clearChoice();
}

QStringList GuhuoDialog::getOptionNames() const
{
    return option_names;
}

const Card *GuhuoDialog::getOptionCard(const QString &option_name) const
{
    return map.value(option_name, nullptr);
}

bool GuhuoDialog::applyOption(const QString &option_name)
{
    const Card *card = getOptionCard(option_name);
    if (card == nullptr || Self == nullptr)
        return false;
    Self->setTag(objectName(), QVariant::fromValue(card));
    return true;
}

void GuhuoDialog::clearChoice() const
{
    if (Self != nullptr)
        Self->removeTag(objectName());
}

bool GuhuoDialog::shouldPopup() const
{
    return !play_only || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY;
}

bool GuhuoDialog::hasEnabledOptions() const
{
    foreach (const QString &name, option_names) {
        if (isButtonEnabled(name))
            return true;
    }
    return false;
}

bool GuhuoDialog::isButtonEnabled(const QString &button_name) const
{
    const Card *card = map.value(button_name, nullptr);
    if (card == nullptr || Self == nullptr)
        return false;
    if ((play_only || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY)
        && !card->isAvailable(Self))
        return false;
    return !Self->isLocked(card);
}

void GuhuoDialog::popup()
{
    prepareOptions();
    if (!shouldPopup() || !hasEnabledOptions()) {
        emit onButtonClick();
        return;
    }
    foreach (QAbstractButton *button, group->buttons())
        button->setEnabled(isButtonEnabled(button->objectName()));
    exec();
}

void GuhuoDialog::selectCard(QAbstractButton *button)
{
    if (button == nullptr || !applyOption(button->objectName()))
        return;
    emit onButtonClick();
    accept();
}

QGroupBox *GuhuoDialog::createLeft()
{
    QGroupBox *box = new QGroupBox(Sanguosha->translate("basic"));
    QVBoxLayout *layout = new QVBoxLayout(box);
    foreach (const BasicCard *engine_card, Sanguosha->findChildren<const BasicCard *>()) {
        if (engine_card->objectName().startsWith("_") || ServerInfo.BanPackages.contains(engine_card->getPackage())
            || map.contains(engine_card->objectName()))
            continue;
        if (slash_combined && engine_card->isKindOf("Slash") && engine_card->objectName() != "slash")
            continue;
        Card *card = Sanguosha->cloneCard(engine_card->objectName());
        if (card)
            layout->addWidget(createButton(card));
    }
    layout->addStretch();
    return box;
}

QGroupBox *GuhuoDialog::createRight()
{
    QGroupBox *box = new QGroupBox(Sanguosha->translate("trick"));
    QVBoxLayout *layout = new QVBoxLayout(box);
    foreach (const TrickCard *engine_card, Sanguosha->findChildren<const TrickCard *>()) {
        if (engine_card->objectName().startsWith("_") || ServerInfo.BanPackages.contains(engine_card->getPackage())
            || map.contains(engine_card->objectName()) || (!delayed_tricks && !engine_card->isNDTrick()))
            continue;
        Card *card = Sanguosha->cloneCard(engine_card->objectName());
        if (card)
            layout->addWidget(createButton(card));
    }
    layout->addStretch();
    return box;
}

QAbstractButton *GuhuoDialog::createButton(Card *card)
{
    card->setSkillName(objectName());
    card->setCanRecast(false);
    card->setParent(this);
    map.insert(card->objectName(), card);
    option_names << card->objectName();
    QAbstractButton *button = makeDialogButton(card, this);
    group->addButton(button);
    return button;
}

QHash<QString, JuguanDialog *> JuguanDialogs;

JuguanDialog *JuguanDialog::getInstance(const QString &object, const QString &card_names)
{
    if (JuguanDialogs.value(object, nullptr) == nullptr)
        JuguanDialogs[object] = new JuguanDialog(object, card_names);
    return JuguanDialogs.value(object);
}

JuguanDialog::JuguanDialog(const QString &object, const QString &card_names)
    : cards(card_names)
{
    setObjectName(object);
    setWindowTitle(Sanguosha->translate(object));
    group = new QButtonGroup(this);
    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectCard(QAbstractButton *)));
}

void JuguanDialog::prepareOptions()
{
    clearChoice();
    clearButtons();
    if (!shouldPopup())
        return;
    QString names = cards;
    names.remove("!");
    names.remove("$");
    foreach (const QString &name, names.split(",", Qt::SkipEmptyParts)) {
        Card *card = Sanguosha->cloneCard(name);
        if (card && !map.contains(card->objectName()))
            button_layout->addWidget(createButton(card));
        else
            delete card;
    }
}

QStringList JuguanDialog::getOptionNames() const { return option_names; }
const Card *JuguanDialog::getOptionCard(const QString &name) const { return map.value(name, nullptr); }

bool JuguanDialog::applyOption(const QString &name)
{
    const Card *card = getOptionCard(name);
    if (card == nullptr || Self == nullptr)
        return false;
    Self->setTag(objectName(), QVariant::fromValue(card));
    return true;
}

void JuguanDialog::clearChoice() const
{
    if (Self != nullptr)
        Self->removeTag(objectName());
}

bool JuguanDialog::shouldPopup() const
{
    return !cards.isEmpty() && (cards.endsWith("!")
        || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY);
}

bool JuguanDialog::hasEnabledOptions() const
{
    foreach (const QString &name, option_names) {
        if (isButtonEnabled(name))
            return true;
    }
    return false;
}

void JuguanDialog::clearButtons()
{
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }
    qDeleteAll(map);
    map.clear();
    option_names.clear();
}

bool JuguanDialog::isButtonEnabled(const QString &name) const
{
    const Card *card = map.value(name, nullptr);
    return card != nullptr && Self != nullptr && !Self->isLocked(card)
        && (cards.startsWith("$") || Sanguosha->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY
            || card->isAvailable(Self));
}

void JuguanDialog::popup()
{
    prepareOptions();
    if (!shouldPopup() || !hasEnabledOptions()) {
        emit onButtonClick();
        return;
    }
    foreach (QAbstractButton *button, group->buttons())
        button->setEnabled(isButtonEnabled(button->objectName()));
    exec();
}

void JuguanDialog::selectCard(QAbstractButton *button)
{
    if (button == nullptr || !applyOption(button->objectName()))
        return;
    emit onButtonClick();
    accept();
}

QAbstractButton *JuguanDialog::createButton(Card *card)
{
    card->setParent(this);
    card->setSkillName(objectName());
    card->setCanRecast(false);
    QAbstractButton *button = makeDialogButton(card, this);
    map.insert(card->objectName(), card);
    option_names << card->objectName();
    group->addButton(button);
    return button;
}

QHash<QString, TiansuanDialog *> TiansuanDialogs;

TiansuanDialog *TiansuanDialog::getInstance(const QString &name, const QString &choices)
{
    if (TiansuanDialogs.value(name, nullptr) == nullptr)
        TiansuanDialogs[name] = new TiansuanDialog(name, choices);
    return TiansuanDialogs.value(name);
}

TiansuanDialog::TiansuanDialog(const QString &name, const QString &choices)
    : tiansuan_choices(choices)
{
    setObjectName(name);
    setWindowTitle(Sanguosha->translate(name));
    group = new QButtonGroup(this);
    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectChoice(QAbstractButton *)));
}

bool TiansuanDialog::MarkJudge(const QString &choice)
{
    const QString mark = objectName() + "_tiansuan_remove_" + choice;
    foreach (const QString &mark_name, Self->getMarkNames()) {
        if (mark_name.startsWith(mark) && Self->getMark(mark_name) > 0)
            return false;
    }
    return true;
}

void TiansuanDialog::popup()
{
    Self->removeTag(objectName());
    QStringList choices = tiansuan_choices.split(",", Qt::SkipEmptyParts);
    foreach (const QString &choice, choices) {
        if (!MarkJudge(choice))
            continue;
        QAbstractButton *button = createChoiceButton(choice);
        button_layout->addWidget(button);
    }
    if (!group->buttons().isEmpty())
        exec();
}

void TiansuanDialog::selectChoice(QAbstractButton *button)
{
    Self->setTag(objectName(), button->objectName());
    emit onButtonClick();
    accept();
}

QAbstractButton *TiansuanDialog::createChoiceButton(const QString &choice)
{
    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(choice), this);
    button->setObjectName(choice);
    group->addButton(button);
    return button;
}

HuashenDialog::HuashenDialog(const QString &propertyName)
    : GeneralOverview(), m_propertyName(propertyName)
{
    setPreviewMode(true);
}

void HuashenDialog::popup()
{
    if (Self == nullptr || m_propertyName.isEmpty())
        return;
    QString skillName = m_propertyName;
    if (skillName.endsWith("_general", Qt::CaseInsensitive))
        skillName.chop(8);
    const QVariant value = Self->property(m_propertyName.toLatin1().constData());
    QStringList names = value.toString().split("+", Qt::SkipEmptyParts);
    QList<const General *> generals;
    foreach (const QString &name, names) {
        const General *general = Sanguosha->getGeneral(name);
        if (general != nullptr)
            generals << general;
    }
    fillGenerals(generals);
    setWindowTitle(Sanguosha->translate(skillName));
    show();
}
