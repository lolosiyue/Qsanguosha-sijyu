#include "choosetriggerorderbox.h"
#include "skin-bank.h"
#include "timed-progressbar.h"
#include "button.h"
#include "client.h"
#include "clientstruct.h"
#include "clientplayer.h"
#include "engine.h"
#include "effects/effects-policy.h"
#include "settings.h"

#include <QDebug>
#include <QGraphicsProxyWidget>
#include <QGraphicsSceneMouseEvent>
#include <QPen>
#include <QPropertyAnimation>

static qreal initialOpacity = 0.8;
static int optionButtonHeight = 40;
static QFont optionButtonFont;

const int ChooseTriggerOrderBox::top_dark_bar = 27;
const int ChooseTriggerOrderBox::m_topBlankWidth = 42;
const int ChooseTriggerOrderBox::bottom_blank_width = 25;
const int ChooseTriggerOrderBox::interval = 15;
const int ChooseTriggerOrderBox::m_leftBlankWidth = 37;

ClientSkillContext::ClientSkillContext()
    : skill(nullptr)
    , owner(nullptr)
    , invoker(nullptr)
    , preferredTarget(nullptr)
    , preferredTargetSeat(-1)
    , trigger_count(0)
    , multiplier(1)
    , instanceID(0)
{
}

bool ClientSkillContext::operator==(const ClientSkillContext &arg2) const
{
    return skill == arg2.skill && owner == arg2.owner && invoker == arg2.invoker
        && preferredTarget == arg2.preferredTarget && preferredTargetSeat == arg2.preferredTargetSeat
        && instanceID == arg2.instanceID;
}

bool ClientSkillContext::operator==(const QVariantMap &arg2) const
{
    ClientSkillContext arg2str;
    arg2str.tryParse(arg2);
    return (*this) == arg2str;
}

bool operator==(const QVariantMap &arg1, const ClientSkillContext &arg2)
{
    ClientSkillContext arg1str;
    arg1str.tryParse(arg1);
    return arg1str == arg2;
}

bool ClientSkillContext::tryParse(const QVariantMap &map)
{
    *this = ClientSkillContext();

    if (map.contains("skill")) {
        QString skillName = map.value("skill").toString();
        skill = Sanguosha->getSkill(skillName);
    }
    if (skill == nullptr) {
        return false;
    }

    if (map.contains("invoker")) {
        QString invokerName = map.value("invoker").toString();
        invoker = ClientInstance->getPlayer(invokerName);
    }
    if (invoker == nullptr) {
        return false;
    }

    if (map.contains("owner"))
        owner = ClientInstance->getPlayer(map.value("owner").toString());
    if (owner == nullptr)
        owner = invoker;

    if (map.contains("preferredtarget"))
        preferredTarget = ClientInstance->getPlayer(map.value("preferredtarget").toString());

    if (map.contains("preferredtargetseat"))
        preferredTargetSeat = map.value("preferredtargetseat").toInt();

    if (map.contains("trigger_count"))
        trigger_count = map.value("trigger_count").toInt();

    if (map.contains("multiplier"))
        multiplier = map.value("multiplier").toInt();

    if (map.contains("instanceID"))
        instanceID = map.value("instanceID").toInt();

    return true;
}

bool ClientSkillContext::tryParse(const QString &str)
{
    QStringList l = str.split(":");
    skill = Sanguosha->getSkill(l.first());
    if (skill == nullptr)
        return false;
    invoker = ClientInstance->getPlayer(l.value(2));
    if (invoker == nullptr)
        return false;
    owner = ClientInstance->getPlayer(l.value(1));
    if (owner == nullptr)
        owner = invoker;

    if (l.length() > 3) {
        preferredTarget = ClientInstance->getPlayer(l.value(3));
        preferredTargetSeat = l.value(4).toInt();
    }

    return true;
}

QString ClientSkillContext::toString() const
{
    QStringList l;
    QString skillName = skill->objectName();
    if (instanceID > 0)
        skillName += "#" + QString::number(instanceID);
    l << skillName;
    l << owner->objectName();
    l << invoker->objectName();
    if (preferredTarget != nullptr) {
        l << preferredTarget->objectName();
        l << QString::number(preferredTargetSeat);
    }
    return l.join(":");
}

TriggerOptionButton::TriggerOptionButton(QGraphicsObject *parent, const QVariantMap &skillDetail, int width)
    : QGraphicsObject(parent)
    , times(1)
    , mull(0)
    , selected(false)
    , width(width)
{
    detail.tryParse(skillDetail);
    construct();
}

TriggerOptionButton::TriggerOptionButton(QGraphicsObject *parent, const ClientSkillContext &skillDetail, int width)
    : QGraphicsObject(parent)
    , detail(skillDetail)
    , times(1)
    , mull(0)
    , selected(false)
    , width(width)
{
    construct();
}

void TriggerOptionButton::construct()
{
    setToolTip(detail.skill->getDescription());

    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setOpacity(initialOpacity);
    if (optionButtonFont.pixelSize() <= 0) {
        optionButtonFont = UiConfig.SmallFont;
        optionButtonFont.setPixelSize(UiConfig.TinyFont.pixelSize());
    }
}

QFont TriggerOptionButton::defaultFont()
{
    QFont font = UiConfig.SmallFont;
    font.setPixelSize(UiConfig.TinyFont.pixelSize());
    return font;
}

void TriggerOptionButton::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->save();
    painter->setBrush(Qt::black);
    painter->setPen(selected ? QPen(QColor(255, 215, 0), 3)
                             : QPen(QColor(Sanguosha->getKingdomColor(Self->getGeneral()->getKingdom()))));
    QRectF rect = boundingRect();
    painter->drawRoundedRect(rect, 5, 5);
    painter->restore();

    QString generalName;
    if (detail.preferredTarget != nullptr)
        generalName = detail.preferredTarget->getGeneralName();
    else
        generalName = detail.owner->getGeneralName();

    QPixmap pixmap = G_ROOM_SKIN.getGeneralPixmap(generalName, QSanRoomSkin::S_GENERAL_ICON_SIZE_TINY);

    pixmap = pixmap.scaledToHeight(optionButtonHeight, Qt::SmoothTransformation);
    QRect pixmapRect(QPoint(0, (rect.height() - pixmap.height()) / 2), pixmap.size());
    painter->setBrush(pixmap);
    painter->drawRoundedRect(pixmapRect, 5, 5);

    QRect textArea(optionButtonHeight, 0, width - optionButtonHeight, optionButtonHeight);
    painter->setFont(optionButtonFont);
    painter->setPen(Qt::white);
    painter->drawText(textArea, Qt::AlignCenter, displayedTextOf(detail, times, mull));
}

QRectF TriggerOptionButton::boundingRect() const
{
    return QRectF(0, 0, width, optionButtonHeight);
}

void TriggerOptionButton::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    event->accept();
}

void TriggerOptionButton::mouseReleaseEvent(QGraphicsSceneMouseEvent *)
{
    emit clicked();
}

void TriggerOptionButton::hoverEnterEvent(QGraphicsSceneHoverEvent *)
{
    QPropertyAnimation *animation = new QPropertyAnimation(this, "opacity");
    animation->setEndValue(1.0);
    animation->setDuration(G_EFFECTS.scaledDuration(100));
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    emit hovered(true);
}

void TriggerOptionButton::hoverLeaveEvent(QGraphicsSceneHoverEvent *)
{
    QPropertyAnimation *animation = new QPropertyAnimation(this, "opacity");
    animation->setEndValue(initialOpacity);
    animation->setDuration(G_EFFECTS.scaledDuration(100));
    animation->start(QAbstractAnimation::DeleteWhenStopped);
    emit hovered(false);
}

QString TriggerOptionButton::displayedTextOf(const ClientSkillContext &detail, int times, int mull)
{
    QString skillName = detail.skill->objectName();
    QString text = Sanguosha->translate(skillName);
    if (detail.instanceID > 0)
        text += " #" + QString::number(detail.instanceID);
    if (detail.preferredTarget != nullptr) {
        QString targetName = detail.preferredTarget->getGeneralName();
        text = TriggerOptionButton::tr("%1 (use upon %2)").arg(text).arg(Sanguosha->translate(targetName));
    }
    if (detail.owner != detail.invoker)
        text = TriggerOptionButton::tr("%1 (of %2's)").arg(text).arg(Sanguosha->translate(detail.owner->getGeneralName()));

    int remaining = detail.multiplier - detail.trigger_count;
    if (remaining > 1)
        text += QString(" * %1").arg(remaining);

    return text;
}

bool TriggerOptionButton::isPreferentialSkillOf(const TriggerOptionButton *other) const
{
    return detail.skill == other->detail.skill && detail.preferredTargetSeat < other->detail.preferredTargetSeat;
}

void TriggerOptionButton::needDisabled(bool disabled)
{
    if (disabled) {
        QPropertyAnimation *animation = new QPropertyAnimation(this, "opacity");
        animation->setEndValue(0.2);
        animation->setDuration(G_EFFECTS.scaledDuration(100));
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        QPropertyAnimation *animation = new QPropertyAnimation(this, "opacity");
        animation->setEndValue(initialOpacity);
        animation->setDuration(G_EFFECTS.scaledDuration(100));
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void TriggerOptionButton::setSelected(bool selected)
{
    if (this->selected == selected)
        return;
    this->selected = selected;
    update();
}

ChooseTriggerOrderBox::ChooseTriggerOrderBox()
    : optional(true)
    , m_active(false)
    , m_minimumWidth(0)
    , cancel(new Button(tr("cancel"), 0.6))
    , progressBar(nullptr)
{
    cancel->hide();
    cancel->setParentItem(this);
    cancel->setObjectName("cancel");
    connect(cancel, &Button::clicked, this, &ChooseTriggerOrderBox::reply);
}

void ChooseTriggerOrderBox::storeMinimumWidth()
{
    int width = 0;
    static QFontMetrics fontMetrics(TriggerOptionButton::defaultFont());
    foreach (const QVariant &option, options) {
        ClientSkillContext skillDetail;
        skillDetail.tryParse(option.toMap());

        const int w = fontMetrics.horizontalAdvance(TriggerOptionButton::displayedTextOf(skillDetail, 1, 2));
        if (w > width)
            width = w;
    }

    QFont titleFont = UiConfig.SmallFont;
    titleFont.setBold(true);
    QFontMetrics titleFontMetrics(titleFont);
    int titleWidth = titleFontMetrics.horizontalAdvance(title) + 40;

    m_minimumWidth = qMax(width + optionButtonHeight + 20, titleWidth);
}

QRectF ChooseTriggerOrderBox::boundingRect() const
{
    int width = m_minimumWidth + (m_leftBlankWidth * 2);

    int height = m_topBlankWidth + (optionButtons.size() * optionButtonHeight) + ((optionButtons.size() - 1) * interval) + bottom_blank_width;

    if (ServerInfo.OperationTimeout != 0)
        height += 12;

    if (optional)
        height += cancel->boundingRect().height() + interval;

    return QRectF(0, 0, width, height);
}

void ChooseTriggerOrderBox::chooseOption(const QVariantList &options, bool optional)
{
    clear();
    this->options = options;
    this->optional = optional;
    title = tr("Please Select Trigger Order");

    storeMinimumWidth();
    foreach (const QVariant &option, options) {
        QVariantMap map = option.toMap();
        ClientSkillContext detail;
        detail.tryParse(map);

        bool duplicate = false;
        foreach (TriggerOptionButton *otherButton, optionButtons) {
            if (otherButton->detail == detail) {
                ++otherButton->times;
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        TriggerOptionButton *button = new TriggerOptionButton(this, detail, m_minimumWidth);
        button->setObjectName(detail.toString());
        foreach (TriggerOptionButton *otherButton, optionButtons) {
            if (otherButton->isPreferentialSkillOf(button))
                connect(button, &TriggerOptionButton::hovered, otherButton, &TriggerOptionButton::needDisabled);
            if (button->isPreferentialSkillOf(otherButton))
                connect(otherButton, &TriggerOptionButton::hovered, button, &TriggerOptionButton::needDisabled);
        }
        optionButtons << button;
    }

    foreach (TriggerOptionButton *button, optionButtons) {
        button->mull = button->times;
    }

    prepareGeometryChange();
    moveToCenter();
    m_active = true;
    show();

    int y = m_topBlankWidth;
    foreach (TriggerOptionButton *button, optionButtons) {
        QPointF pos;
        pos.setX(m_leftBlankWidth);
        pos.setY(y);

        button->setPos(pos);
        connect(button, &TriggerOptionButton::clicked, this, &ChooseTriggerOrderBox::reply);
        y += button->boundingRect().height() + interval;
    }

    if (optional) {
        cancel->setPos((boundingRect().width() - cancel->boundingRect().width()) / 2, y + interval);
        cancel->show();
    }

    if (ServerInfo.OperationTimeout != 0) {
        if (progressBar == nullptr) {
            progressBar = new QSanCommandProgressBar;
            progressBar->setMaximumWidth(boundingRect().width() - 16);
            progressBar->setMaximumHeight(12);
            progressBar->setTimerEnabled(true);
            progress_bar_item = new QGraphicsProxyWidget(this);
            progress_bar_item->setWidget(progressBar);
            progress_bar_item->setPos(boundingRect().center().x() - (progress_bar_item->boundingRect().width() / 2), boundingRect().height() - 20);
            connect(progressBar, &QSanCommandProgressBar::timedOut, this, &ChooseTriggerOrderBox::reply);
        }
        progressBar->setCountdown(QSanProtocol::S_COMMAND_TRIGGER_ORDER);
        progressBar->show();
    }
}

void ChooseTriggerOrderBox::clear()
{
    m_active = false;
    if (!m_selectedChoice.isEmpty()) {
        m_selectedChoice.clear();
        emit draftChanged(m_selectedChoice);
    }

    if (progressBar != nullptr) {
        disconnect(progressBar, &QSanCommandProgressBar::timedOut, this, &ChooseTriggerOrderBox::reply);
        progressBar->hide();
        progressBar->deleteLater();
        progressBar = nullptr;
    }

    foreach (TriggerOptionButton *button, optionButtons) {
        disconnect(button, nullptr, this, nullptr);
        button->hide();
        button->deleteLater();
    }

    optionButtons.clear();
    options.clear();

    cancel->hide();

    disappear();
}

void ChooseTriggerOrderBox::reply()
{
    if (!m_active)
        return;

    QObject *source = sender();
    for (TriggerOptionButton *button : optionButtons) {
        if (button == source) {
            selectChoice(button->objectName(), true);
            submitChoice(button->objectName());
            return;
        }
    }

    if (source == cancel) {
        submitChoice("cancel");
        return;
    }

    // Ignore delayed callbacks from a progress bar that belonged to a cleared request.
    if (source != nullptr && source != progressBar)
        return;

    // Timeout keeps the established fallback: first option when mandatory, cancel when optional.
    if (optional) {
        submitChoice("cancel");
    } else if (!options.isEmpty()) {
        ClientSkillContext detail;
        detail.tryParse(options.first().toMap());
        submitChoice(detail.toString());
    } else {
        // Preserve the legacy timeout fallback for a malformed mandatory empty request.
        m_active = false;
        ClientInstance->onPlayerChooseTriggerOrder("cancel");
        clear();
    }
}

QList<ChooseTriggerOrderBox::KeyboardOption> ChooseTriggerOrderBox::keyboardOptions() const
{
    QList<KeyboardOption> result;
    if (!m_active)
        return result;
    result.reserve(optionButtons.size());
    for (const TriggerOptionButton *button : optionButtons) {
        KeyboardOption option;
        option.id = button->objectName();
        option.label = TriggerOptionButton::displayedTextOf(button->detail, button->times, button->mull);
        // needDisabled() only dims the hover presentation; it does not remove eligibility.
        option.enabled = true;
        option.selected = option.id == m_selectedChoice;
        result.append(option);
    }
    return result;
}

bool ChooseTriggerOrderBox::selectChoice(const QString &choice, bool selected)
{
    if (!m_active || !isVisible())
        return false;

    bool found = false;
    for (const TriggerOptionButton *button : optionButtons) {
        if (button->objectName() == choice) {
            found = true;
            break;
        }
    }
    if (!found)
        return false;

    const QString nextChoice = selected ? choice : (m_selectedChoice == choice ? QString() : m_selectedChoice);
    if (nextChoice != m_selectedChoice) {
        m_selectedChoice = nextChoice;
        for (TriggerOptionButton *button : optionButtons)
            button->setSelected(button->objectName() == m_selectedChoice);
        emit draftChanged(m_selectedChoice);
    }
    return true;
}

QString ChooseTriggerOrderBox::selectedChoice() const
{
    return m_selectedChoice;
}

bool ChooseTriggerOrderBox::submitChoice(const QString &choice)
{
    if (!m_active || !isVisible())
        return false;

    bool valid = choice == "cancel" ? canCancelChoice() : false;
    if (!valid) {
        for (const TriggerOptionButton *button : optionButtons) {
            if (button->objectName() == choice) {
                valid = true;
                break;
            }
        }
    }
    if (!valid)
        return false;

    m_active = false;
    // The Client entry point represents cancellation as an empty choice. The
    // literal "cancel" is a UI identifier, not one of the request's skill IDs.
    ClientInstance->onPlayerChooseTriggerOrder(choice == "cancel" ? QString() : choice);
    if (!m_active) clear(); // A synchronous replacement owns a new active draft.
    return true;
}

bool ChooseTriggerOrderBox::canCancelChoice() const
{
    return m_active && isVisible() && optional;
}

bool ChooseTriggerOrderBox::handleChooseKey(int key)
{
    const bool forward = key == Qt::Key_Right || key == Qt::Key_Down || key == Qt::Key_Tab;
    const bool backward = key == Qt::Key_Left || key == Qt::Key_Up || key == Qt::Key_Backtab;
    const bool confirm = key == Qt::Key_Return || key == Qt::Key_Enter;
    if (!m_active || !isVisible()
        || (!forward && !backward && !confirm && key != Qt::Key_Space && key != Qt::Key_Escape)) return false;
    if (key == Qt::Key_Escape) {
        if (canCancelChoice()) submitChoice("cancel");
        return true;
    }
    QList<TriggerOptionButton *> candidates;
    int current = -1;
    for (TriggerOptionButton *button : optionButtons) {
        if (!button->isVisible() || !button->isEnabled()) continue;
        if (button->objectName() == m_selectedChoice) current = candidates.size();
        candidates << button;
    }
    if (candidates.isEmpty()) return true;
    if (current < 0) current = backward ? candidates.size() - 1 : 0;
    else if (forward || backward)
        current = (current + (forward ? 1 : candidates.size() - 1)) % candidates.size();
    const QString choice = candidates.at(current)->objectName();
    // Reuse the native selected border and the shared request draft.
    selectChoice(choice, true);
    if (confirm) submitChoice(choice);
    return true;
}
