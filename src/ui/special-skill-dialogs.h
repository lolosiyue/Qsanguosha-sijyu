#ifndef QSAN_SPECIAL_SKILL_DIALOGS_H
#define QSAN_SPECIAL_SKILL_DIALOGS_H

#if !defined(QSAN_ENGINE_BUILD)

#include "package-dialogs.h"

#include <memory>
#include <QPointer>

void installSpecialSkillDialogFactories();

class ShefuDialog : public GuhuoDialog
{
    Q_OBJECT
public:
    static ShefuDialog *getInstance(const QString &object);
protected:
    explicit ShefuDialog(const QString &object);
};

class YoulongDialog : public GuhuoDialog
{
    Q_OBJECT
public:
    static YoulongDialog *getInstance(const QString &object);
protected:
    explicit YoulongDialog(const QString &object);
};

class CaozhaoDialog : public GuhuoDialog
{
    Q_OBJECT
public:
    static CaozhaoDialog *getInstance(const QString &object);
protected:
    explicit CaozhaoDialog(const QString &object);
};

class WeidiDialog : public QDialog
{
    Q_OBJECT
public:
    static WeidiDialog *getInstance();
public slots:
    void popup();
    void selectSkill(QAbstractButton *button);
private:
    explicit WeidiDialog();
    QAbstractButton *createSkillButton(const QString &skill_name);
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    std::unique_ptr<SkillDeclarationSession> declaration;
signals:
    void onButtonClick();
};

class PingjianDialog : public QDialog
{
    Q_OBJECT
public:
    static PingjianDialog *getInstance();
public slots:
    void popup();
    void selectSkill(QAbstractButton *button);
private:
    explicit PingjianDialog();
    QAbstractButton *createSkillButton(const QString &skill_name);
    QButtonGroup *group;
    QVBoxLayout *button_layout;
    std::unique_ptr<SkillDeclarationSession> declaration;
signals:
    void onButtonClick();
};

class HuashenDialog : public GeneralOverview
{
    Q_OBJECT
public:
    explicit HuashenDialog(const QString &propertyName = "Huashens");
public slots:
    void popup();
private:
    QString m_propertyName;
};

class TaoluanDialog : public GuhuoDialog
{
    Q_OBJECT
public:
    static TaoluanDialog *getInstance(const QString &object);
protected:
    explicit TaoluanDialog(const QString &object);
};

class HuomoDialog : public GuhuoDialog
{
    Q_OBJECT
public:
    static HuomoDialog *getInstance();
protected:
    explicit HuomoDialog();
};

#endif
#endif
