#include "qsanbutton.h"
//#include "clientplayer.h"
//#include "skin-bank.h"
#include "engine.h"
#include "roomscene.h"
#include "skill-instance-utils.h"
#include <QMutexLocker>
#include <QtMath>

QSanButton::QSanButton(QGraphicsItem *parent) : QGraphicsObject(parent)
{
    _m_state = S_STATE_UP;
    _m_style = S_STYLE_PUSH;
    _m_mouseEntered = false;
    setSize(QSize(0, 0));
    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

QSanButton::QSanButton(const QString &groupName, const QString &buttonName, QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    _m_state = S_STATE_UP;
    _m_style = S_STYLE_PUSH;
    _m_groupName = groupName;
    _m_buttonName = buttonName;
    _m_mouseEntered = false;

    for (int i = 0; i < (int)S_NUM_BUTTON_STATES; i++)
        _m_bgPixmap[i] = G_ROOM_SKIN.getButtonPixmap(groupName, buttonName, (QSanButton::ButtonState)i);
    setSize(_m_bgPixmap[0].size());

    setAcceptHoverEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton);
}

void QSanButton::click()
{
    if (isEnabled())
        _onMouseClick(true);
}

bool QSanButton::isMouseInside() const
{
    QGraphicsScene *scenePtr = scene();
    if (nullptr == scenePtr) {
        return false;
    }

    QPoint cursorPos = QCursor::pos();
    foreach (QGraphicsView *view, scenePtr->views()) {
        QPointF pos = mapFromScene(view->mapToScene(view->mapFromGlobal(cursorPos)));
        if (_isMouseInside(pos)) {
            return true;
        }
    }

    return false;
}

QRectF QSanButton::boundingRect() const
{
    return QRectF(0, 0, _m_size.width(), _m_size.height()).adjusted(
        -_m_touchTargetPadding, -_m_touchTargetPadding,
        _m_touchTargetPadding, _m_touchTargetPadding);
}

void QSanButton::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (!m_actionText.isEmpty()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const bool disabled = _m_state == S_STATE_DISABLED;
        const bool pressed = _m_state == S_STATE_DOWN;
        const bool hover = _m_state == S_STATE_HOVER;
        const QRectF rect(1, 1, _m_size.width() - 2, _m_size.height() - 2);
        QLinearGradient fill(rect.topLeft(), rect.bottomLeft());
        fill.setColorAt(0, disabled ? QColor(48, 44, 52) : pressed ? QColor(55, 40, 66)
            : hover ? QColor(115, 85, 126) : QColor(85, 63, 99));
        fill.setColorAt(1, disabled ? QColor(35, 32, 39) : QColor(39, 29, 51));
        painter->setBrush(fill);
        painter->setPen(QPen(disabled ? QColor(99, 89, 102) : QColor(205, 181, 132), 1.5));
        painter->drawRoundedRect(rect, 6, 6);
        QFont font = painter->font();
        font.setFamily(QStringLiteral("KaiTi"));
        font.setPixelSize(20);
        font.setBold(true);
        painter->setFont(font);
        painter->setPen(disabled ? QColor(153, 144, 156) : QColor(255, 240, 209));
        painter->drawText(rect.translated(0, pressed ? 1 : 0), Qt::AlignCenter, m_actionText);
        painter->restore();
        return;
    }
    painter->drawPixmap(0, 0, _m_bgPixmap[(int)_m_state]);
}

void QSanButton::setActionText(const QString &text)
{
    if (m_actionText == text) return;
    m_actionText = text;
    setSize(_m_size);
    update();
}

void QSanButton::setSize(QSize newSize)
{
    prepareGeometryChange();
    _m_size = newSize;
    setTouchTargetMinimum(_m_touchTargetMinimum);
    if (!m_actionText.isEmpty()) {
        _m_mask = QRegion(QRect(QPoint(0, 0), newSize));
        return;
    }
    if (_m_size.width() == 0 || _m_size.height() == 0) {
        _m_mask = QRegion();
        return;
    }
    Q_ASSERT(!_m_bgPixmap[0].isNull());
    QPixmap pixmap = _m_bgPixmap[0];
    const QPixmap scaledMask = pixmap.mask().scaled(newSize);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    _m_mask = QRegion(QBitmap::fromPixmap(scaledMask));
#else
    _m_mask = QRegion(QBitmap(scaledMask));
#endif
}

void QSanButton::setRect(QRect rect)
{
    setSize(rect.size());
    setPos(rect.topLeft());
}

void QSanButton::setTouchTargetMinimum(qreal minimumSize)
{
    _m_touchTargetMinimum = qMax<qreal>(0.0, minimumSize);
    qreal sx = 1.0, sy = 1.0;
    if (scene() && !scene()->views().isEmpty()) {
        // Include parent/dashboard scaling, not only fitInView's transform.
        const QTransform transform = deviceTransform(scene()->views().first()->viewportTransform());
        sx = qMax<qreal>(0.01, qSqrt(transform.m11() * transform.m11() + transform.m12() * transform.m12()));
        sy = qMax<qreal>(0.01, qSqrt(transform.m21() * transform.m21() + transform.m22() * transform.m22()));
    }
    const qreal padding = qMax<qreal>(0.0, qMax(
        (_m_touchTargetMinimum / sx - _m_size.width()) / 2.0,
        (_m_touchTargetMinimum / sy - _m_size.height()) / 2.0));
    if (qFuzzyCompare(_m_touchTargetPadding, padding))
        return;
    prepareGeometryChange();
    _m_touchTargetPadding = padding;
    update();
}

void QSanButton::setStyle(ButtonStyle style)
{
    _m_style = style;
}

void QSanButton::setEnabled(bool enabled)
{
    bool changed = (enabled != isEnabled());
    if (!changed) return;
    if (enabled) {
        setState(S_STATE_UP);
        _m_mouseEntered = false;
    }
    QGraphicsObject::setEnabled(enabled);
    if (!enabled) setState(S_STATE_DISABLED);
    update();
    emit enable_changed();
}

void QSanButton::setState(QSanButton::ButtonState state)
{
    if (this->_m_state != state) {
        this->_m_state = state;
        update();
    }
}

bool QSanButton::insideButton(QPointF pos) const
{
    return _isMouseInside(pos);
}

void QSanButton::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    if (_m_state == S_STATE_DISABLED) return;
    QPointF point = event->pos();
    if (_m_mouseEntered || !insideButton(point)) return; // fake event;

    Q_ASSERT(_m_state != S_STATE_HOVER);
    _m_mouseEntered = true;
    if (_m_state == S_STATE_UP)
        setState(S_STATE_HOVER);
}

void QSanButton::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    if (_m_state == S_STATE_DISABLED) return;
    if (!_m_mouseEntered) return;

    Q_ASSERT(_m_state != S_STATE_DISABLED);
    if (_m_state == S_STATE_HOVER)
        setState(S_STATE_UP);
    _m_mouseEntered = false;
}

void QSanButton::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    QPointF point = event->pos();
    if (insideButton(point)) {
        if (!_m_mouseEntered) hoverEnterEvent(event);
    } else {
        if (_m_mouseEntered) hoverLeaveEvent(event);
    }
}

void QSanButton::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    QPointF point = event->pos();
    if (!insideButton(point)) return;

    Q_ASSERT(_m_state != S_STATE_DISABLED);
    if (_m_style == S_STYLE_TOGGLE) return;
    setState(S_STATE_DOWN);
}

void QSanButton::_onMouseClick(bool inside)
{
    if (_m_style == S_STYLE_PUSH)
        setState(S_STATE_UP);
    else if (_m_style == S_STYLE_TOGGLE) {
        if (_m_state == S_STATE_HOVER)
            _m_state = S_STATE_UP; // temporarily set, do not use setState!

        if (_m_state == S_STATE_DOWN && inside)
            setState(S_STATE_UP);
        else if (_m_state == S_STATE_UP && inside)
            setState(S_STATE_DOWN);
    }
    update();

    if (inside) emit clicked();
    else emit clicked_mouse_outside();
}

void QSanButton::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_ASSERT(_m_state != S_STATE_DISABLED);
    QPointF point = event->pos();
    bool inside = insideButton(point);
    _onMouseClick(inside);
}

bool QSanButton::isDown()
{
    return (_m_state == S_STATE_DOWN);
}

void QSanButton::redraw()
{
    for (int i = 0; i < (int)S_NUM_BUTTON_STATES; ++i)
        _m_bgPixmap[i] = G_ROOM_SKIN.getButtonPixmap(_m_groupName, _m_buttonName, (QSanButton::ButtonState)i);

    setSize(_m_bgPixmap[0].size());
}

QSanSkillButton::QSanSkillButton(QGraphicsItem *parent)
    : QSanButton(parent)
{
    _m_groupName = QSanRoomSkin::S_SKIN_KEY_BUTTON_SKILL;
    _m_emitActivateSignal = false;
    _m_emitDeactivateSignal = false;
    _m_canEnable = true;
    _m_canDisable = true;
    _m_skill = nullptr;
    _m_viewAsSkill = nullptr;
    _m_preshowEnabled = false;
    _m_savedStyle = _m_style;
    _m_savedState = _m_state;
    _m_savedEmitActivateSignal = false;
    _m_savedEmitDeactivateSignal = false;
    _m_savedCanEnable = true;
    _m_savedCanDisable = true;
    connect(this, SIGNAL(clicked()), this, SLOT(onMouseClick()));
    _m_skill = nullptr;
}

void QSanSkillButton::_setSkillType(SkillType type)
{
    _m_skillType = type;
}

void QSanSkillButton::onMouseClick()
{
	if (_m_skill == nullptr) return;
	if (_m_preshowEnabled) {
		const bool requestedState = isDown();
		// Wait for the owner-only server notification before changing the display.
		setState(requestedState ? S_STATE_UP : S_STATE_DOWN);
		emit skill_preshow_toggled(_m_preshowSkillName, requestedState);
		return;
	}
    if ((_m_style == S_STYLE_TOGGLE && isDown() && _m_emitActivateSignal) || _m_style == S_STYLE_PUSH) {
        emit skill_activated();
        emit skill_activated(_m_skill);
    } else if (!isDown() && _m_emitDeactivateSignal) {
        emit skill_deactivated();
        emit skill_deactivated(_m_skill);
    }
}

void QSanSkillButton::setPreshowEnabled(const QString &skillName, bool enabled, bool preshowed)
{
	const bool appearanceChanged = enabled != _m_preshowEnabled;
	if (enabled && !_m_preshowEnabled) {
		_m_savedStyle = _m_style;
		_m_savedState = _m_state;
		_m_savedEmitActivateSignal = _m_emitActivateSignal;
		_m_savedEmitDeactivateSignal = _m_emitDeactivateSignal;
		_m_savedCanEnable = _m_canEnable;
		_m_savedCanDisable = _m_canDisable;
	}
	if (!enabled && _m_preshowEnabled) {
		_m_preshowEnabled = false;
		_m_preshowSkillName.clear();
		_m_style = _m_savedStyle;
		_m_emitActivateSignal = _m_savedEmitActivateSignal;
		_m_emitDeactivateSignal = _m_savedEmitDeactivateSignal;
		_m_canEnable = _m_savedCanEnable;
		_m_canDisable = _m_savedCanDisable;
		setState(_m_savedState);
		QSanButton::setEnabled(_m_savedState != S_STATE_DISABLED);
		_repaint();
		update();
		return;
	}
	_m_preshowEnabled = enabled;
	_m_preshowSkillName = enabled ? skillName : QString();
	if (enabled) {
		// Keep the normal toggle button usable even when the skill itself is inactive.
		_m_style = S_STYLE_TOGGLE;
		_m_emitActivateSignal = false;
		_m_emitDeactivateSignal = false;
		QSanButton::setEnabled(true);
		setPreshowState(preshowed);
		if (appearanceChanged) {
			_repaint();
			update();
		}
	} else {
		// Ordinary skill buttons retain the configuration established by setSkill().
	}
}

void QSanSkillButton::setEnabled(bool enabled)
{
	if (_m_preshowEnabled) {
		// Skill refreshes disable ordinary activation buttons; pre-show remains a
		// separate owner-authorized action while the skill is hidden.
		Q_UNUSED(enabled);
		QSanButton::setEnabled(true);
		return;
	}
	if (!_m_canEnable && enabled) return;
	if (!_m_canDisable && !enabled) return;
	QSanButton::setEnabled(enabled);
}

void QSanSkillButton::setPreshowState(bool preshowed)
{
	if (!_m_preshowEnabled)
		return;
	setState(preshowed ? S_STATE_DOWN : S_STATE_UP);
}

void QSanSkillButton::setSkill(const Skill *skill)
{
	Q_ASSERT(skill != nullptr);
    if(!skill) return;
    _m_skill = skill;
    // This is a nasty trick because the server side decides to choose a nasty design
    // such that sometimes the actual viewas skill is nested inside a trigger skill.
    // Since the trigger skill is not relevant, we flatten it before we create the button.
    _m_viewAsSkill = ViewAsSkill::parseViewAsSkill(skill);

    if (skill->inherits("AnytimeSkill")) {
        setState(QSanButton::S_STATE_UP);
        _setSkillType(QSanInvokeSkillButton::S_SKILL_ANYTIME);
        setStyle(QSanButton::S_STYLE_PUSH);
        _m_emitDeactivateSignal = false;
        _m_emitActivateSignal = true;
        _m_canDisable = true;
        _m_canEnable = true;
    } else if (skill->inherits("BattleArraySkill")) {
        setStyle(QSanButton::S_STYLE_TOGGLE);
        setState(QSanButton::S_STATE_DISABLED);
        _setSkillType(QSanInvokeSkillButton::S_SKILL_ARRAY);
        _m_emitActivateSignal = true;
        _m_emitDeactivateSignal = true;
    } else {
    Skill::Frequency freq = skill->getFrequency(Self);
    if (freq == Skill::Frequent
	|| (freq == Skill::NotFrequent && skill->inherits("TriggerSkill")
		&& _m_viewAsSkill == nullptr && !Self->hasEquipSkill(skill->objectName()))) {
        setStyle(QSanButton::S_STYLE_TOGGLE);
        setState(freq == Skill::Frequent ? QSanButton::S_STATE_DOWN : QSanButton::S_STATE_UP);
        if (skill->isChangeSkill()) {
            //setState(QSanButton::S_STATE_HOVER);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_CHANGE);
        } else if (skill->isLimitedSkill()) {
            _setSkillType(QSanInvokeSkillButton::S_SKILL_ONEOFF_SPELL);
        } else {
            _setSkillType(QSanInvokeSkillButton::S_SKILL_FREQUENT);
        }
        _m_emitDeactivateSignal = false;
        _m_emitActivateSignal = false;
        _m_canDisable = false;
        _m_canEnable = true;
    } else if (freq == Skill::Limited || freq == Skill::NotFrequent) {
        if (skill->isChangeSkill()) {
            //setState(QSanButton::S_STATE_HOVER);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_CHANGE);
        } else {
            setState(QSanButton::S_STATE_DISABLED);
            if (skill->isAttachedLordSkill())
                _setSkillType(QSanInvokeSkillButton::S_SKILL_ATTACHEDLORD);
            //else if (freq == Skill::Limited)
            else if (skill->isLimitedSkill())
                _setSkillType(QSanInvokeSkillButton::S_SKILL_ONEOFF_SPELL);
            else
                _setSkillType(QSanInvokeSkillButton::S_SKILL_PROACTIVE);
        }
        setStyle(QSanButton::S_STYLE_TOGGLE);
        _m_emitDeactivateSignal = true;
        _m_emitActivateSignal = true;
        _m_canDisable = true;
        _m_canEnable = true;
    } else if (freq == Skill::Wake) {
        if (skill->isChangeSkill()) {
            //setState(QSanButton::S_STATE_HOVER);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_CHANGE);
        } else {
            setState(QSanButton::S_STATE_DISABLED);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_AWAKEN);
        }
        setStyle(QSanButton::S_STYLE_PUSH);
        _m_emitDeactivateSignal = false;
        _m_emitActivateSignal = false;
        _m_canEnable = true;
        _m_canDisable = true;
    /*} else if (freq == Skill::Change) {
         setStyle(QSanButton::S_STYLE_TOGGLE);
         setState(QSanButton::S_STATE_HOVER);
         _setSkillType(QSanInvokeSkillButton::S_SKILL_CHANGE);
         _m_emitDeactivateSignal = true;
         _m_emitActivateSignal = true;
         _m_canDisable = true;
         _m_canEnable = true;*/
    } else if (freq == Skill::Compulsory || freq == Skill::NotCompulsory) { // we have to set it in such way for WeiDi
        if (skill->isChangeSkill()) {
            //setState(QSanButton::S_STATE_HOVER);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_CHANGE);
        } else if (skill->isLimitedSkill()) {
            _setSkillType(QSanInvokeSkillButton::S_SKILL_ONEOFF_SPELL);
            setStyle(QSanButton::S_STYLE_TOGGLE);
        } else {
            setState(QSanButton::S_STATE_UP);
            _setSkillType(QSanInvokeSkillButton::S_SKILL_COMPULSORY);
        }
        setStyle(QSanButton::S_STYLE_PUSH);
        _m_emitDeactivateSignal = false;
        _m_emitActivateSignal = false;
        _m_canDisable = true;
        _m_canEnable = true;
    } else
		Q_ASSERT(false);
    }

    {
        LuaLocker locker;
        setToolTip(skill->getDescription(Self));
    }

    Q_ASSERT((int)_m_skillType < QSanInvokeSkillButton::S_NUM_SKILL_TYPES && _m_state <= 3);
    _repaint();
}

void QSanSkillButton::setDisplayName(const QString &name)
{
    if (_m_displayName == name) return;
    _m_displayName = name;
    _repaint();
}

void QSanInvokeSkillButton::_repaint()
{
    // Compulsory skins have identical up/down images. Preshow is an owner
    // toggle, so use the existing toggle skin until ordinary activation returns.
    const SkillType displayType = _m_preshowEnabled ? S_SKILL_FREQUENT : _m_skillType;
    for (int i = 0; i < (int)S_NUM_BUTTON_STATES; i++) {
        _m_bgPixmap[i] = G_ROOM_SKIN.getSkillButtonPixmap((ButtonState)i, displayType, _m_enumWidth);
        Q_ASSERT(!_m_bgPixmap[i].isNull());
        const IQSanComponentSkin::QSanShadowTextFont &font = G_DASHBOARD_LAYOUT.getSkillTextFont((ButtonState)i, displayType, _m_enumWidth);
        QPainter painter(&_m_bgPixmap[i]);
        QString skillName = _m_displayName.isEmpty()
            ? Sanguosha->translate(_m_skill->objectName()) : _m_displayName;
        if (_m_enumWidth != S_WIDTH_WIDE) skillName = skillName.left(4);
        font.paintText(&painter,
            (ButtonState)i == S_STATE_DOWN ? G_DASHBOARD_LAYOUT.m_skillTextAreaDown[_m_enumWidth] :
            G_DASHBOARD_LAYOUT.m_skillTextArea[_m_enumWidth],
            Qt::AlignCenter, skillName);

    }
    setSize(_m_bgPixmap[0].size());
}

void QSanInvokeSkillButton::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->drawPixmap(0, 0, _m_bgPixmap[(int)_m_state]);
    if (_m_skillType == S_SKILL_ATTACHEDLORD) {
        int nline = _m_skill->objectName().indexOf("-");
        if (nline == -1)
            nline = _m_skill->objectName().indexOf("_");
        QString engskillname = _m_skill->objectName().left(nline);
        QString generalName = "";

        foreach (const Player* p, Self->getSiblings()) {
            const General* general = p->getGeneral();
            if (general->hasSkill(engskillname)) {
                generalName = general->objectName();
                break;
            } else {
                if (general->hasSkill("weidi") && Self->isLord() && Self->hasSkill(engskillname)) {
                    generalName = general->objectName();
                    break;
                }
                if (general->hasSkill("weiwudi_guixin") && p->hasSkill(engskillname)) {
                    generalName = general->objectName();
                    break;
                }

            }
            if (p->getGeneral2()) {
                const General* general2 = p->getGeneral2();
                if (general2->hasSkill(engskillname)) {
                    generalName = general2->objectName();
                    break;
                } else {
                    if (general2->hasSkill("weidi") && Self->isLord() && Self->hasSkill(engskillname)) {
                        generalName = general2->objectName();
                        break;
                    }
                    if (general2->hasSkill("weiwudi_guixin") && p->hasSkill(engskillname)) {
                        generalName = general2->objectName();
                        break;
                    }
                }
            }
        }
        if (generalName == "")
            return;
        QString path = G_ROOM_SKIN.getButtonPixmapPath(G_ROOM_SKIN.S_SKIN_KEY_BUTTON_SKILL, getSkillTypeString(_m_skillType), _m_state);
        int n = path.lastIndexOf("/");
        path = path.left(n + 1) + generalName + ".png";
        QPixmap pixmap = G_ROOM_SKIN.getPixmapFromFileName(path);
        if (pixmap.isNull())
            return;
        int h = pixmap.height() - _m_bgPixmap[(int)_m_state].height();
        painter->drawPixmap(0, -h, pixmap.width(), pixmap.height(), pixmap);
    }
}

QSanSkillButton *QSanInvokeSkillDock::addSkillButtonByName(const QString &skillName)
{
    //Q_ASSERT(getSkillButtonByName(skillName) == nullptr);
    QSanInvokeSkillButton *button = new QSanInvokeSkillButton(this);
    button->setObjectName(skillName);
    QString baseName = SkillInstanceUtils::baseName(skillName);
    button->setSkill(Sanguosha->getSkill(baseName));
    connect(button, SIGNAL(skill_activated(const Skill *)), this, SIGNAL(skill_activated(const Skill *)));
    connect(button, SIGNAL(skill_deactivated(const Skill *)), this, SIGNAL(skill_deactivated(const Skill *)));
    int instanceId = SkillInstanceUtils::parseName(skillName, baseName);
    int insertAt = _m_buttons.length();
    for (int i = 0; i < _m_buttons.length(); ++i) {
        QString otherBase;
        int otherId = SkillInstanceUtils::parseName(_m_buttons.at(i)->objectName(), otherBase);
        if (otherBase == baseName && otherId > instanceId) {
            insertAt = i;
            break;
        }
    }
    _m_buttons.insert(insertAt, button);
    update();
    return button;
}

int QSanInvokeSkillDock::width() const
{
    return _m_width;
}

int QSanInvokeSkillDock::height() const
{
    int visibleRegularButtons = 0;
    foreach (QSanInvokeSkillButton *button, _m_buttons) {
        if (button == nullptr)
            continue;

        const Skill *skill = button->getSkill();
        if (skill == nullptr)
            continue;

        if (Self != nullptr && !skill->shouldBeVisible(Self))
            continue;

        if (!skill->isAttachedLordSkill())
            visibleRegularButtons++;
    }

    if (visibleRegularButtons == 0)
        return 0;

    int rows = (visibleRegularButtons - 1) / 2 + 1;
    return rows * G_DASHBOARD_LAYOUT.m_skillButtonsSize[0].height();
}

void QSanInvokeSkillDock::setWidth(int width)
{
    if (_m_width == width)
        return;

    prepareGeometryChange();
    _m_width = width;
}

void QSanInvokeSkillDock::update()
{
    prepareGeometryChange();

    if (!_m_buttons.isEmpty()) {
        QList<QSanInvokeSkillButton *> regular_buttons, lordskill_buttons/*, all_buttons*/;
        foreach (QSanInvokeSkillButton *btn, _m_buttons) {
            if (btn->getSkill()->shouldBeVisible(Self)) {
                btn->setVisible(true);
            } else {
                btn->setVisible(false);
                continue;
            }
            if (btn->getSkill()->isAttachedLordSkill())
                lordskill_buttons << btn;
            else
                regular_buttons << btn;
        }
        //all_buttons = regular_buttons + lordskill_buttons;

        int numButtons = regular_buttons.length();
        int lordskillNum = lordskill_buttons.length();
        //Q_ASSERT(lordskillNum <= 6); // HuangTian, ZhiBa and XianSi
        int rows = (numButtons == 0) ? 0 : (numButtons - 1) / 2 + 1;
        int rowH = G_DASHBOARD_LAYOUT.m_skillButtonsSize[0].height();
        int *btnNum = new int[rows + lordskillNum + 2 + 1]; // we allocate one more row in case we need it.
        int remainingBtns = numButtons;
        for (int i = 0; i < rows; i++) {
            btnNum[i] = qMin(2, remainingBtns);
            remainingBtns -= 2;
        }/*
		if (lordskillNum > 3) {
			int half = lordskillNum / 2;
			btnNum[rows] = half;
			btnNum[rows + 1] = lordskillNum - half;
		} else if (lordskillNum > 0) {
			btnNum[rows] = lordskillNum;
		}*/
        if (lordskillNum > 0) {
            for (int k = 0; k < lordskillNum; k++) {
                btnNum[rows + k] = 2;
            }
        }/*
        if (rows >= 2) {// If the buttons in rows are 3, 1, then balance them to 2, 2
            if (btnNum[rows - 1] == 1 && btnNum[rows - 2] == 3) {
				btnNum[rows - 1] = 2;
				btnNum[rows - 2] = 2;
            }
		} else if (rows == 1 && btnNum[0] == 3 && lordskillNum == 0) {
            btnNum[0] = 2;
            btnNum[1] = 1;
            rows = 2;
		}*/

        int m = 0;/*
		int x_ls = 0;
		if (lordskillNum > 0) x_ls++;
		if (lordskillNum > 3) x_ls++;
		for (int i = 0; i < rows + x_ls; i++) {
			int rowTop = (RoomSceneInstance->m_skillButtonSank) ? (-rowH - 2 * (rows + x_ls - i - 1)) :
				((-rows - x_ls + i) * rowH);
			int btnWidth = _m_width / btnNum[i];
			for (int j = 0; j < btnNum[i]; j++) {
				QSanInvokeSkillButton *button = all_buttons[m++];
				button->setButtonWidth((QSanInvokeSkillButton::SkillButtonWidth)(btnNum[i] - 1));
				button->setPos(btnWidth * j, rowTop);
			}
		}*/
        for (int i = 0; i < rows; i++) {
            int rowTop = (RoomSceneInstance->m_skillButtonSank) ? (-rowH - 2 * (rows - i - 1)) : ((-rows + i) * rowH);
            int pixWidth = G_DASHBOARD_LAYOUT.m_skillButtonsSize[btnNum[i] - 1].width();
            int rowLeft = qMax(0, (_m_width - pixWidth * btnNum[i]) / 2);
            for (int j = 0; j < btnNum[i]; j++) {
                QSanInvokeSkillButton *button = regular_buttons[m++];
                button->setButtonWidth((QSanInvokeSkillButton::SkillButtonWidth)(btnNum[i] - 1));
                button->setPos(rowLeft + pixWidth * j, rowTop);
            }
        }
        int m1 = 0;
        int btnWidth1 = _m_width / 2;
        int rowTop1 = G_DASHBOARD_LAYOUT.m_confirmButtonArea.top() - 2.6*G_DASHBOARD_LAYOUT.m_confirmButtonArea.height();
        int rowLeft1 = G_DASHBOARD_LAYOUT.m_confirmButtonArea.left() - 2.4*G_DASHBOARD_LAYOUT.m_confirmButtonArea.width();
        for (int i = 0; i < lordskillNum; i++) {
            QSanInvokeSkillButton *button = lordskill_buttons[m1++];
            button->setButtonWidth((QSanInvokeSkillButton::SkillButtonWidth)(1));
            button->setPos(rowLeft1 - btnWidth1 * i, rowTop1);
        }
        delete[] btnNum;
    }
    QGraphicsObject::update();
}

QSanInvokeSkillButton *QSanInvokeSkillDock::getSkillButtonByName(const QString &skillName) const
{
    foreach (QSanInvokeSkillButton *button, _m_buttons) {
        if (button->objectName() == skillName)
            return button;
    }
    return nullptr;
}

