#ifndef _CARD_ITEM_H
#define _CARD_ITEM_H

#include "qsan-selectable-item.h"
#include "qsanbutton.h"
#include "settings.h"

class FilterSkill;
class General;
class Card;
class CardActionButton;

class CardItem : public QSanSelectableItem
{
    Q_OBJECT

public:
    CardItem(const Card *card);
    CardItem(const QString &general_name);
    ~CardItem();

    virtual QRectF boundingRect() const;
    virtual void setEnabled(bool enabled);

    const Card *getCard() const;
    void setCard(const Card *card);
    void refreshTooltip();
    inline int getId() const
    {
        return m_cardId;
    }

    void setHomePos(QPointF home_pos);
    QPointF homePos() const;
    QAbstractAnimation *getGoBackAnimation(bool doFadeEffect, bool smoothTransition = false,
        int duration = Config.S_MOVE_CARD_ANIMATION_DURATION);
    void goBack(bool playAnimation, bool doFade = true);
    inline QAbstractAnimation *getCurrentAnimation(bool)
    {
        return m_currentAnimation;
    }
    inline void setHomeOpacity(double opacity)
    {
        m_opacityAtHome = opacity;
    }
    inline double getHomeOpacity()
    {
        return m_opacityAtHome;
    }

    void showAvatar(const QString &general_name);
    void hideAvatar();
    void setAutoBack(bool auto_back);
    void changeGeneral(const QString &general_name);
    void setFootnote(const QString &desc);
    void setYingbiannote(const QString &desc);

    inline bool isSelected() const
    {
        return m_isSelected;
    }
    inline void setSelected(bool selected)
    {
        m_isSelected = selected;
    }
    bool isEquipped() const;

    void setFrozen(bool is_frozen);

    inline void showFootnote()
    {
        _m_showFootnote = true;
    }
    inline void hideFootnote()
    {
        _m_showFootnote = false;
    }

    static CardItem *FindItem(const QList<CardItem *> &items, int card_id);

    struct UiHelper
    {
        int tablePileClearTimeStamp;
    } m_uiHelper;

    void clickItem()
    {
        emit clicked();
    }

    void addActionButton(CardActionButton *button);
    void removeActionButton(const QString &buttonId);
    QList<CardActionButton *> getActionButtons() const;
    void updateActionButtonsLayout();
    void clearActionButtons();

private slots:
    void currentAnimationDestroyed();

protected:
    void _initialize();
    QAbstractAnimation *m_currentAnimation;
    QImage _m_footnoteImage;
    QImage _m_yingbiannoteImage;
    bool _m_showFootnote;
    QMutex m_animationMutex;
    double m_opacityAtHome;
    bool m_isSelected;
    bool _m_isUnknownGeneral;
    static const int _S_CLICK_JITTER_TOLERANCE;
    static const int _S_MOVE_JITTER_TOLERANCE;
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *event);
    virtual void mouseReleaseEvent(QGraphicsSceneMouseEvent *event);
    virtual void mouseMoveEvent(QGraphicsSceneMouseEvent *event);
    virtual void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event);
    virtual void hoverEnterEvent(QGraphicsSceneHoverEvent *);
    virtual void hoverLeaveEvent(QGraphicsSceneHoverEvent *);
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

private:
    int m_cardId;
    bool m_hasVirtualCardVisual;
    Card::Suit m_virtualCardSuit;
    int m_virtualCardNumber;
    bool m_virtualCardBlack;
    QString _m_avatarName;
    QPointF home_pos;
    QPointF _m_lastMousePressScenePos;
    bool auto_back, frozen;
    bool m_isShiny;
    QList<CardActionButton *> m_actionButtons;

signals:
    void toggle_discards();
    void clicked();
    void double_clicked();
    void thrown();
    void released();
    void enter_hover();
    void leave_hover();
    void movement_animation_finished();
    void actionButtonClicked(const QString &buttonId, int cardId);
};

class CardActionButton : public QSanButton
{
    Q_OBJECT

public:
    enum ActionMode
    {
        S_MODE_DIRECT,
        S_MODE_SELECT_CARD,
        S_MODE_SELECT_PLAYER
    };

    CardActionButton(CardItem *parent);
    ~CardActionButton();

    void setButtonId(const QString &id);
    QString getButtonId() const;

    void setIconName(const QString &iconName);
    QString getIconName() const;

    void setTooltip(const QString &tooltip);
    QString getTooltip() const;

    void setActionMode(ActionMode mode);
    ActionMode getActionMode() const;

    void setLuaCallback(int callbackRef);
    int getLuaCallback() const;

    void setCallbackKey(const QString &key);
    QString getCallbackKey() const;

    virtual QRectF boundingRect() const;

protected:
    virtual void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);
    virtual void mousePressEvent(QGraphicsSceneMouseEvent *event);

private:
    QString m_buttonId;
    QString m_iconName;
    QString m_tooltip;
    QString m_callbackKey;
    ActionMode m_actionMode;
    int m_luaCallback;
    CardItem *m_cardItem;
    int _m_width;
    int _m_height;
};

#endif