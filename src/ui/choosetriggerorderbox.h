#ifndef CHOOSETRIGGERORDERBOX_H
#define CHOOSETRIGGERORDERBOX_H

#include <QGraphicsObject>
#include <QList>
#include <QString>

#include "graphicsbox.h"

class Button;
class QGraphicsProxyWidget;
class QSanCommandProgressBar;

class Skill;
class ClientPlayer;

struct ClientSkillContext
{
    const Skill *skill;
    ClientPlayer *owner;
    ClientPlayer *invoker;
    ClientPlayer *preferredTarget;
    int preferredTargetSeat;
    int trigger_count;
    int multiplier;
    int instanceID;

    ClientSkillContext();

    bool operator==(const ClientSkillContext &arg2) const;
    bool operator==(const QVariantMap &arg2) const;
    bool tryParse(const QVariantMap &map);
    bool tryParse(const QString &str);
    QString toString() const;
};

bool operator==(const QVariantMap &arg1, const ClientSkillContext &arg2);

class TriggerOptionButton : public QGraphicsObject
{
    Q_OBJECT
    friend class ChooseTriggerOrderBox;

public:
    static QFont defaultFont();

signals:
    void clicked();
    void hovered(bool entering);

public slots:
    void needDisabled(bool disabled);

protected:
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QRectF boundingRect() const override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

    static QString displayedTextOf(const ClientSkillContext &detail, int times, int mull);

private:
    TriggerOptionButton(QGraphicsObject *parent, const QVariantMap &skillDetail, int width);
    TriggerOptionButton(QGraphicsObject *parent, const ClientSkillContext &skillDetail, int width);

    bool isPreferentialSkillOf(const TriggerOptionButton *other) const;

    void construct();
    void setSelected(bool selected);

    ClientSkillContext detail;
    int times;
    int mull;
    bool selected;

    int width;
};

class ChooseTriggerOrderBox : public GraphicsBox
{
    Q_OBJECT

public:
    struct KeyboardOption {
        QString id;
        QString label;
        bool enabled;
        bool selected;
    };

    ChooseTriggerOrderBox();

    QRectF boundingRect() const override;
    void chooseOption(const QVariantList &options, bool optional);
    void clear();
    bool handleChooseKey(int key);

    QList<KeyboardOption> keyboardOptions() const;
    bool selectChoice(const QString &choice, bool selected);
    QString selectedChoice() const;
    bool submitChoice(const QString &choice);
    bool canCancelChoice() const;
    bool hasActiveChoice() const { return m_active; }

signals:
    void draftChanged(const QString &choice);

public slots:
    void reply();

private:
    QList<TriggerOptionButton *> optionButtons;
    static const int top_dark_bar;
    static const int m_topBlankWidth;
    static const int bottom_blank_width;
    static const int interval;
    static const int m_leftBlankWidth;

    QVariantList options;
    bool optional;
    bool m_active;
    QString m_selectedChoice;
    int m_minimumWidth;

    Button *cancel;
    QGraphicsProxyWidget *progress_bar_item;
    QSanCommandProgressBar *progressBar;

    void storeMinimumWidth();
};

#endif
