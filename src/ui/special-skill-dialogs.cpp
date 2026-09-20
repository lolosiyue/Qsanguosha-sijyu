#include "special-skill-dialogs.h"

#include "skill-dialog-registry.h"

#include "card.h"
#include "client.h"
#include "client-core.h"
#include "clientplayer.h"
#include "engine.h"
#include "ol.h"
#include "olwenwu.h"
#include "sp.h"
#include "tenyear.h"
#include "wind.h"
#include "yczh2016.h"
#include "yjcm2015.h"

#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QVBoxLayout>

#if !defined(QSAN_ENGINE_BUILD)
namespace {

QDialog *createSpecialGuhuo(const SkillDialogInfo &info, QWidget *)
{
    if (info.type == QStringLiteral("youlong"))
        return YoulongDialog::getInstance(info.objectName);
    if (info.type == QStringLiteral("shefu"))
        return ShefuDialog::getInstance(info.objectName);
    if (info.type == QStringLiteral("caozhao"))
        return CaozhaoDialog::getInstance(info.objectName);
    if (info.type == QStringLiteral("taoluan"))
        return TaoluanDialog::getInstance(info.objectName);
    if (info.type == QStringLiteral("huomo"))
        return HuomoDialog::getInstance();
    return nullptr;
}

QDialog *createWeidi(const SkillDialogInfo &, QWidget *)
{
    return WeidiDialog::getInstance();
}

QDialog *createPingjian(const SkillDialogInfo &, QWidget *)
{
    return PingjianDialog::getInstance();
}

QDialog *createHuashen(const SkillDialogInfo &info, QWidget *)
{
    return new HuashenDialog(info.objectName);
}

}

void installSpecialSkillDialogFactories()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    SkillDialogRegistry::registerFactory(QStringLiteral("youlong"), createSpecialGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("shefu"), createSpecialGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("caozhao"), createSpecialGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("taoluan"), createSpecialGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("huomo"), createSpecialGuhuo);
    SkillDialogRegistry::registerFactory(QStringLiteral("weidi"), createWeidi);
    SkillDialogRegistry::registerFactory(QStringLiteral("pingjian"), createPingjian);
    SkillDialogRegistry::registerFactory(QStringLiteral("huashen"), createHuashen);
}
#endif

#if !defined(QSAN_ENGINE_BUILD)
CaozhaoDialog *CaozhaoDialog::getInstance(const QString &object)
{
	static QPointer<CaozhaoDialog> instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new CaozhaoDialog(object);

	return instance;
}

CaozhaoDialog::CaozhaoDialog(const QString &object)
	: GuhuoDialog(object)
{
}

#endif

#if !defined(QSAN_ENGINE_BUILD)
WeidiDialog *WeidiDialog::getInstance()
{
    static QPointer<WeidiDialog> instance;
    if (instance == nullptr)
        instance = new WeidiDialog();

    return instance;
}

WeidiDialog::WeidiDialog()
{
    setObjectName("weidi");
    setWindowTitle(Sanguosha->translate("weidi"));
    group = new QButtonGroup(this);

    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectSkill(QAbstractButton *)));
}

void WeidiDialog::popup()
{
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }

    const quint64 requestId = ClientInstance && ClientInstance->interactionCore()
        ? ClientInstance->interactionCore()->activeRequestId() : 0;
    declaration.reset(new SkillDeclarationSession("weidi", Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        QStringList(), requestId));
    declaration->clearChoice();
    const QList<SkillDeclarationCandidate> candidates = declaration->candidates();
    int count = 0;
    QString name;
    foreach (const SkillDeclarationCandidate &candidate, candidates) {
        QAbstractButton *button = createSkillButton(candidate.value);
        if (!button) continue;
        button->setEnabled(candidate.enabled);
        if (button->isEnabled()) {
            count++;
            name = candidate.value;
        }
        button_layout->addWidget(button);
    }

    if (count == 0) {
        emit onButtonClick();
        return;
    } else if (count == 1) {
        if (declaration->apply(name)) emit onButtonClick();
        return;
    }

    exec();
}

void WeidiDialog::selectSkill(QAbstractButton *button)
{
    if (button && declaration && declaration->apply(button->objectName())) {
        emit onButtonClick();
        accept();
    }
}

QAbstractButton *WeidiDialog::createSkillButton(const QString &skill_name)
{
    const Skill *skill = Sanguosha->getSkill(skill_name);
    if (!skill) return nullptr;

    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(skill_name));
    button->setObjectName(skill_name);
    button->setToolTip(skill->getDescription(Self));

    group->addButton(button);
    return button;
}
#endif

#if !defined(QSAN_ENGINE_BUILD)
HuomoDialog::HuomoDialog() : GuhuoDialog("huomo", true, false)
{
}

HuomoDialog *HuomoDialog::getInstance()
{
	static QPointer<HuomoDialog> instance;
    if (instance == nullptr || instance->objectName() != "huomo")
        instance = new HuomoDialog;

    return instance;
}

#endif

#if !defined(QSAN_ENGINE_BUILD)
TaoluanDialog *TaoluanDialog::getInstance(const QString &object)
{
	static QPointer<TaoluanDialog> instance;
    if (instance == nullptr || instance->objectName() != object)
        instance = new TaoluanDialog(object);

    return instance;
}

TaoluanDialog::TaoluanDialog(const QString &object)
    : GuhuoDialog(object)
{
}

#endif

#if !defined(QSAN_ENGINE_BUILD)
PingjianDialog*PingjianDialog::getInstance()
{
	static QPointer<PingjianDialog> instance;
	if(instance==nullptr)
		instance = new PingjianDialog();
	return instance;
}

PingjianDialog::PingjianDialog()
{
	setObjectName("pingjian");
	setWindowTitle(Sanguosha->translate("pingjian"));
	group = new QButtonGroup(this);
	button_layout = new QVBoxLayout;
	setLayout(button_layout);
	connect(group,SIGNAL(buttonClicked(QAbstractButton*)),this,SLOT(selectSkill(QAbstractButton*)));
}

void PingjianDialog::popup()
{
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }

    const quint64 requestId = ClientInstance && ClientInstance->interactionCore()
        ? ClientInstance->interactionCore()->activeRequestId() : 0;
    declaration.reset(new SkillDeclarationSession("pingjian", Self,
        Sanguosha->getCurrentCardUseReason(), Sanguosha->getCurrentCardUsePattern(),
        QStringList(), requestId));
    declaration->clearChoice();
    const QList<SkillDeclarationCandidate> candidates = declaration->candidates();
    foreach (const SkillDeclarationCandidate &candidate, candidates) {
        QAbstractButton *button = createSkillButton(candidate.value);
        if (!button) continue;
        button->setEnabled(candidate.enabled);
        button_layout->addWidget(button);
    }
    if (!candidates.isEmpty())
        exec();
}

void PingjianDialog::selectSkill(QAbstractButton*button)
{
	if (button && declaration && declaration->apply(button->objectName())) {
		emit onButtonClick();
		accept();
	}
}

QAbstractButton*PingjianDialog::createSkillButton(const QString&skill_name)
{
	const Skill*skill = Sanguosha->getSkill(skill_name);
	if(!skill){
		if(!Sanguosha->getGeneral(skill_name))return nullptr;
	}
	QCommandLinkButton*button = new QCommandLinkButton(Sanguosha->translate(skill_name));
	button->setObjectName(skill_name);
	if(skill){
		LuaLocker locker;
		button->setToolTip(skill->getDescription());
	}
	group->addButton(button);
	return button;
}

#endif

#if !defined(QSAN_ENGINE_BUILD)
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
    QStringList names;
    // Existing extensions expose this preview as a string, QVariantList or
    // QStringList. Moving the widget must retain all three representations.
    if (value.userType() == QMetaType::QString) {
        names = value.toString().split("+", Qt::SkipEmptyParts);
    } else if (value.userType() == QMetaType::QVariantList) {
        for (const QVariant &entry : value.toList()) {
            if (!entry.toString().isEmpty())
                names << entry.toString();
        }
    } else if (value.canConvert<QStringList>()) {
        names = value.toStringList();
    } else {
        names = value.toString().split("+", Qt::SkipEmptyParts);
    }
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
#endif

#if !defined(QSAN_ENGINE_BUILD)
YoulongDialog*YoulongDialog::getInstance(const QString &object)
{
	static QPointer<YoulongDialog> instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new YoulongDialog(object);

	return instance;
}

YoulongDialog::YoulongDialog(const QString &object)
	: GuhuoDialog(object)
{
}

#endif

#if !defined(QSAN_ENGINE_BUILD)
ShefuDialog*ShefuDialog::getInstance(const QString &object)
{
	static QPointer<ShefuDialog> instance;
	if (instance == nullptr || instance->objectName() != object)
		instance = new ShefuDialog(object);
	return instance;
}

ShefuDialog::ShefuDialog(const QString &object)
	: GuhuoDialog(object, true, true, false, true, true)
{
}

#endif
