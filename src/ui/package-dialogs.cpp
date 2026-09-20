#include "wind.h"
#include "ol.h"
#include "mobile.h"
#include "mountain.h"

#include "card.h"
#include "client.h"
#include "client-core.h"
#include "clientplayer.h"
#include "engine.h"
#include "generaloverview.h"
#include "server-info.h"

#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QGroupBox>
#include <QHBoxLayout>
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

SkillDialogInfo actualDialogInfo(const QString &skillName, const SkillDialogInfo &fallback)
{
    if (Sanguosha == nullptr)
        return fallback;
    if (const Skill *skill = Sanguosha->getSkill(skillName)) {
        const SkillDialogInfo info = skill->getDialogInfo();
        if (info.isValid())
            return info;
    }
    if (const ViewAsSkill *viewAs = Sanguosha->getViewAsSkill(skillName)) {
        const SkillDialogInfo info = viewAs->getDialogInfo();
        if (info.isValid())
            return info;
    }
    return fallback;
}

quint64 currentDeclarationRequestId()
{
    return ClientInstance != nullptr && ClientInstance->interactionCore() != nullptr
        ? ClientInstance->interactionCore()->activeRequestId() : 0;
}

}

QHash<QString, QPointer<GuhuoDialog>> GuhuoDialogs;

GuhuoDialog *GuhuoDialog::getInstance(const QString &object, bool left, bool right, bool play_only,
    bool slash_combined, bool delayed_tricks, bool update)
{
    if (update || GuhuoDialogs.value(object, nullptr) == nullptr) {
        delete GuhuoDialogs.take(object).data();
        GuhuoDialogs[object] = new GuhuoDialog(object, left, right, play_only, slash_combined, delayed_tricks);
    }
    return GuhuoDialogs.value(object);
}

GuhuoDialog::GuhuoDialog(const QString &object, bool left, bool right, bool play_only,
    bool slash_combined, bool delayed_tricks)
    : play_only(play_only), slash_combined(slash_combined), delayed_tricks(delayed_tricks),
      show_left(left), show_right(right)
{
    setObjectName(object);
    setWindowTitle(Sanguosha->translate(object));
    group = new QButtonGroup(this);
    group->setExclusive(false);

    content_layout = new QHBoxLayout;
    QVBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(content_layout);
    setLayout(layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectCard(QAbstractButton *)));
    prepareOptions();
}

void GuhuoDialog::prepareOptions()
{
    clearButtons();
    delete left_box;
    left_box = nullptr;
    delete right_box;
    right_box = nullptr;
    declaration = std::make_unique<SkillDeclarationSession>(
        actualDialogInfo(objectName(), SkillDialogInfo::guhuo(
            objectName(), show_left, show_right, play_only, slash_combined, delayed_tricks)),
        Self, Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        ServerInfo.BanPackages, currentDeclarationRequestId());
    if (declaration->active()) {
        if (show_left) {
            left_box = createLeft();
            content_layout->addWidget(left_box);
        }
        if (show_right) {
            right_box = createRight();
            content_layout->addWidget(right_box);
        }
    }
    clearChoice();
}

void GuhuoDialog::clearButtons()
{
    if (group != nullptr) {
        for (QAbstractButton *button : group->buttons()) {
            group->removeButton(button);
            delete button;
        }
    }
    map.clear();
    option_names.clear();
}

QStringList GuhuoDialog::getOptionNames() const
{
    QStringList names;
    for (const SkillDeclarationCandidate &candidate : declaration->candidates())
        names << candidate.value;
    return names;
}

const Card *GuhuoDialog::getOptionCard(const QString &option_name) const
{
    return map.value(option_name, nullptr);
}

bool GuhuoDialog::applyOption(const QString &option_name)
{
    return declaration->apply(option_name);
}

void GuhuoDialog::clearChoice() const
{
    if (declaration)
        declaration->clearChoice();
}

bool GuhuoDialog::shouldPopup() const
{
    return declaration->active();
}

bool GuhuoDialog::hasEnabledOptions() const
{
    for (const SkillDeclarationCandidate &candidate : declaration->candidates()) {
        if (declaration->validate(candidate.value).accepted)
            return true;
    }
    return false;
}

bool GuhuoDialog::isButtonEnabled(const QString &button_name) const
{
    return declaration->validate(button_name).accepted;
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
    for (const SkillDeclarationCandidate &candidate : declaration->candidates()) {
        Card *card = const_cast<Card *>(declaration->cloneCard(candidate.value));
        if (card != nullptr && card->isKindOf("BasicCard"))
            layout->addWidget(createButton(card));
    }
    layout->addStretch();
    return box;
}

QGroupBox *GuhuoDialog::createRight()
{
    QGroupBox *box = new QGroupBox(Sanguosha->translate("trick"));
    QVBoxLayout *layout = new QVBoxLayout(box);
    for (const SkillDeclarationCandidate &candidate : declaration->candidates()) {
        Card *card = const_cast<Card *>(declaration->cloneCard(candidate.value));
        if (card != nullptr && card->isKindOf("TrickCard"))
            layout->addWidget(createButton(card));
    }
    layout->addStretch();
    return box;
}

QAbstractButton *GuhuoDialog::createButton(Card *card)
{
    card->setSkillName(objectName());
    card->setCanRecast(false);
    map.insert(card->objectName(), card);
    option_names << card->objectName();
    QAbstractButton *button = makeDialogButton(card, this);
    group->addButton(button);
    return button;
}

QHash<QString, QPointer<JuguanDialog>> JuguanDialogs;

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
    declaration = std::make_unique<SkillDeclarationSession>(
        SkillDialogInfo::juguan(object, card_names), Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        ServerInfo.BanPackages, currentDeclarationRequestId());
    group = new QButtonGroup(this);
    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectCard(QAbstractButton *)));
}

void JuguanDialog::prepareOptions()
{
    declaration = std::make_unique<SkillDeclarationSession>(
        SkillDialogInfo::juguan(objectName(), cards), Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        ServerInfo.BanPackages, currentDeclarationRequestId());
    clearChoice();
    clearButtons();
    if (!shouldPopup())
        return;
    for (const SkillDeclarationCandidate &candidate : declaration->candidates()) {
        Card *card = const_cast<Card *>(declaration->cloneCard(candidate.value));
        if (card != nullptr && !map.contains(card->objectName()))
            button_layout->addWidget(createButton(card));
    }
}

QStringList JuguanDialog::getOptionNames() const
{
    QStringList names;
    for (const SkillDeclarationCandidate &candidate : declaration->candidates())
        names << candidate.value;
    return names;
}
const Card *JuguanDialog::getOptionCard(const QString &name) const { return map.value(name, nullptr); }

bool JuguanDialog::applyOption(const QString &name)
{
    return declaration->apply(name);
}

void JuguanDialog::clearChoice() const
{
    if (declaration)
        declaration->clearChoice();
}

bool JuguanDialog::shouldPopup() const
{
    return declaration->active();
}

bool JuguanDialog::hasEnabledOptions() const
{
    for (const SkillDeclarationCandidate &candidate : declaration->candidates()) {
        if (declaration->validate(candidate.value).accepted)
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
    map.clear();
    option_names.clear();
}

bool JuguanDialog::isButtonEnabled(const QString &name) const
{
    return declaration->validate(name).accepted;
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
    card->setSkillName(objectName());
    card->setCanRecast(false);
    QAbstractButton *button = makeDialogButton(card, this);
    map.insert(card->objectName(), card);
    option_names << card->objectName();
    group->addButton(button);
    return button;
}

QHash<QString, QPointer<TiansuanDialog>> TiansuanDialogs;

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
    declaration = std::make_unique<SkillDeclarationSession>(
        SkillDialogInfo::tiansuan(name, choices), Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        ServerInfo.BanPackages, currentDeclarationRequestId());
    group = new QButtonGroup(this);
    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectChoice(QAbstractButton *)));
}

void TiansuanDialog::prepareOptions()
{
    declaration = std::make_unique<SkillDeclarationSession>(
        SkillDialogInfo::tiansuan(objectName(), tiansuan_choices), Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        ServerInfo.BanPackages, currentDeclarationRequestId());
    declaration->clearChoice();
    // This singleton is reused across requests. Rebuild its widgets from the
    // same stable choices the table and keyboard presenter consume.
    for (QAbstractButton *button : group->buttons()) {
        group->removeButton(button);
        button_layout->removeWidget(button);
        delete button;
    }
    for (const QString &choice : getOptionNames()) {
        QAbstractButton *button = createChoiceButton(choice);
        button->setEnabled(isButtonEnabled(choice));
        button_layout->addWidget(button);
    }
}

QStringList TiansuanDialog::getOptionNames() const
{
    QStringList choices;
    for (const SkillDeclarationCandidate &candidate : declaration->candidates())
        choices << candidate.value;
    return choices;
}

bool TiansuanDialog::isButtonEnabled(const QString &choice) const
{
    return getOptionNames().contains(choice) && declaration->validate(choice).accepted;
}

bool TiansuanDialog::applyOption(const QString &choice)
{
    return declaration->apply(choice);
}

void TiansuanDialog::popup()
{
    prepareOptions();
    for (QAbstractButton *button : group->buttons()) {
        if (button->isEnabled()) {
            button->setFocus();
            exec();
            return;
        }
    }
}

void TiansuanDialog::selectChoice(QAbstractButton *button)
{
    if (button == nullptr || !applyOption(button->objectName())) return;
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
