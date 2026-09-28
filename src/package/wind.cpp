#include "settings.h"
//#include "settings.h"
//#include "standard.h"
//#include "skill.h"
#include "wind.h"
//#include "client.h"
#include "engine.h"
//#include "ai.h"
//#include "general.h"
#include "clientplayer.h"
#if !defined(QSAN_ENGINE_BUILD)
#include "../ui/package-dialogs.h"
#include "clientstruct.h"
#include "skin-bank.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QScrollArea>
#endif
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "maneuvering.h"
#include <QScopeGuard>
#include <memory>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

//#include "json.h"

#if !defined(QSAN_ENGINE_BUILD)
namespace {

static const int GuhuoDialogMaxHeight = 700;
static const int GuhuoDialogDefaultHeight = 560;
static const int GuhuoDialogLeftWidth = 340;
static const int GuhuoDialogRightWidth = 470;
static const int GuhuoDialogOuterMargin = 18;
static const int GuhuoDialogSectionSpacing = 14;
static const int GuhuoOptionHeight = 82;
static const int GuhuoOptionThumbWidth = 56;
static const int GuhuoOptionThumbHeight = 70;
static const int GuhuoDialogUiRevision = 3;

#ifdef Q_OS_WIN
struct GuhuoAccentPolicy {
    int accentState;
    int accentFlags;
    int gradientColor;
    int animationId;
};

struct GuhuoWindowCompositionAttribData {
    int attrib;
    PVOID data;
    SIZE_T size;
};

enum GuhuoAccentState {
    GuhuoAccentDisabled = 0,
    GuhuoAccentEnableGradient = 1,
    GuhuoAccentEnableTransparentGradient = 2,
    GuhuoAccentEnableBlurBehind = 3,
    GuhuoAccentEnableAcrylicBlurBehind = 4,
    GuhuoAccentEnableHostBackdrop = 5
};

enum GuhuoWindowCompositionAttrib {
    GuhuoWcaAccentPolicy = 19
};

typedef BOOL (WINAPI *SetWindowCompositionAttributePtr)(HWND, GuhuoWindowCompositionAttribData *);
#endif

QString getGuhuoCardTypeText(const Card *card)
{
    if (card == nullptr)
        return QString();
    if (card->isKindOf("DelayedTrick"))
        return Sanguosha->translate("delayed_trick");
    if (card->isKindOf("SingleTargetTrick"))
        return Sanguosha->translate("single_target_trick");
    if (card->isKindOf("TrickCard"))
        return Sanguosha->translate("multiple_target_trick");
    return Sanguosha->translate("basic");
}

QPixmap buildGuhuoOptionThumbnail(const QString &cardName)
{
    QString cacheKey = QString("GuhuoOptionThumbnail:%1:%2x%3")
        .arg(cardName)
        .arg(GuhuoOptionThumbWidth)
        .arg(GuhuoOptionThumbHeight);
    QPixmap cachedThumbnail;
    if (QPixmapCache::find(cacheKey, &cachedThumbnail))
        return cachedThumbnail;

    QPixmap pixmap = G_ROOM_SKIN.getCardMainPixmap(cardName, true);
    if (pixmap.isNull())
        return pixmap;

    QRect sourceRect = pixmap.rect();
    int horizontalMargin = qMax(4, sourceRect.width() / 7);
    int verticalMargin = qMax(4, sourceRect.height() / 10);
    sourceRect.adjust(horizontalMargin, verticalMargin, -horizontalMargin, -verticalMargin);

    QSize targetSize(GuhuoOptionThumbWidth, GuhuoOptionThumbHeight);
    qreal targetRatio = (qreal)targetSize.width() / (qreal)targetSize.height();
    int cropWidth = sourceRect.width();
    int cropHeight = sourceRect.height();
    if (cropHeight <= 0)
        cropHeight = 1;

    if ((qreal)cropWidth / (qreal)cropHeight > targetRatio)
        cropWidth = qMax(1, qRound(cropHeight * targetRatio));
    else
        cropHeight = qMax(1, qRound(cropWidth / targetRatio));

    QRect cropRect(sourceRect.x() + (sourceRect.width() - cropWidth) / 2,
        sourceRect.y() + (sourceRect.height() - cropHeight) / 2,
        cropWidth, cropHeight);
    QPixmap thumbnail = pixmap.copy(cropRect).scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!thumbnail.isNull())
        QPixmapCache::insert(cacheKey, thumbnail);
    return thumbnail;
}

void applyGuhuoDialogWindowBlur(QWidget *widget)
{
#ifdef Q_OS_WIN
    if (widget == nullptr)
        return;

    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (hwnd == nullptr)
        return;

    const DWORD immersiveDarkModeAttribute = 20;
    const DWORD systemBackdropTypeAttribute = 38;
    const DWORD transientWindowBackdrop = 3;

    BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, immersiveDarkModeAttribute, &useDarkMode, sizeof(useDarkMode));

    if (SUCCEEDED(DwmSetWindowAttribute(hwnd, systemBackdropTypeAttribute,
        &transientWindowBackdrop, sizeof(transientWindowBackdrop)))) {
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        SetWindowCompositionAttributePtr setWindowCompositionAttribute =
            reinterpret_cast<SetWindowCompositionAttributePtr>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
        if (setWindowCompositionAttribute != nullptr) {
            GuhuoAccentPolicy accentPolicy = {};
            accentPolicy.accentState = GuhuoAccentEnableBlurBehind;
            GuhuoWindowCompositionAttribData data = {
                GuhuoWcaAccentPolicy,
                &accentPolicy,
                sizeof(accentPolicy)
            };
            if (setWindowCompositionAttribute(hwnd, &data))
                return;
        }
    }

    DWM_BLURBEHIND blurBehind = {};
    blurBehind.dwFlags = DWM_BB_ENABLE;
    blurBehind.fEnable = TRUE;
    DwmEnableBlurBehindWindow(hwnd, &blurBehind);
#else
    Q_UNUSED(widget);
#endif
}

bool shouldRebuildGuhuoDialog(GuhuoDialog *dialog)
{
    if (dialog == nullptr)
        return true;

    if (dialog->property("guhuoUiRevision").toInt() != GuhuoDialogUiRevision)
        return true;

    foreach (QGroupBox *box, dialog->findChildren<QGroupBox *>()) {
        QString title = box->title();
        if (title == Sanguosha->translate("basic") || title == Sanguosha->translate("trick"))
            return true;
    }

    return false;
}

QScrollArea *createGuhuoSectionScrollArea(QWidget *widget, int width)
{
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidget(widget);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scrollArea->setFixedWidth(width);
    scrollArea->viewport()->setAutoFillBackground(false);
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    return scrollArea;
}

class GuhuoOptionButton : public QAbstractButton
{
public:
    explicit GuhuoOptionButton(const Card *card, QWidget *parent = nullptr)
        : QAbstractButton(parent),
          m_title(Sanguosha->translate(card->objectName())),
          m_typeText(getGuhuoCardTypeText(card)),
          m_thumbnail(buildGuhuoOptionThumbnail(card->objectName()))
    {
        setText(m_title);
        setObjectName(card->objectName());
        setToolTip(card->getDescription());
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(GuhuoOptionHeight);
    }

    QSize sizeHint() const
    {
        return QSize(280, GuhuoOptionHeight);
    }

    QSize minimumSizeHint() const
    {
        return QSize(220, GuhuoOptionHeight);
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QAbstractButton::enterEvent(event);
        update();
    }

    void leaveEvent(QEvent *event)
    {
        QAbstractButton::leaveEvent(event);
        update();
    }

    void focusInEvent(QFocusEvent *event)
    {
        QAbstractButton::focusInEvent(event);
        update();
    }

    void focusOutEvent(QFocusEvent *event)
    {
        QAbstractButton::focusOutEvent(event);
        update();
    }

    void changeEvent(QEvent *event)
    {
        QAbstractButton::changeEvent(event);
        if (event->type() == QEvent::EnabledChange)
            update();
    }

    void paintEvent(QPaintEvent *)
    {
        const bool hovered = isEnabled() && underMouse();
        const bool selected = isDown() || hasFocus();

        QColor borderColor = !isEnabled() ? QColor(92, 92, 92, 180)
            : selected ? QColor(231, 198, 112)
            : hovered ? QColor(152, 185, 212, 190)
            : QColor(112, 124, 138, 160);
        QColor panelColor = !isEnabled() ? QColor(26, 28, 32, 215)
            : selected ? QColor(44, 50, 58, 240)
            : hovered ? QColor(36, 42, 49, 236)
            : QColor(30, 35, 42, 228);
        QColor accentColor = !isEnabled() ? QColor(116, 116, 116, 120)
            : selected ? QColor(228, 185, 82, 220)
            : hovered ? QColor(118, 170, 202, 180)
            : QColor(84, 110, 128, 150);
        QColor titleColor = !isEnabled() ? QColor(188, 188, 188)
            : QColor(244, 240, 229);
        QColor detailColor = !isEnabled() ? QColor(146, 146, 146)
            : QColor(182, 188, 194);

        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);

        QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath outerPath;
        outerPath.addRoundedRect(outerRect, 10, 10);

        QLinearGradient panelGradient(0, outerRect.top(), 0, outerRect.bottom());
        panelGradient.setColorAt(0.0, panelColor.lighter(selected ? 118 : 110));
        panelGradient.setColorAt(1.0, panelColor.darker(116));
        painter.setPen(QPen(borderColor, selected ? 2.0 : 1.4));
        painter.setBrush(panelGradient);
        painter.drawPath(outerPath);

        QRectF innerRect = outerRect.adjusted(2.0, 2.0, -2.0, -2.0);
        QLinearGradient innerGradient(0, innerRect.top(), 0, innerRect.bottom());
        innerGradient.setColorAt(0.0, QColor(255, 255, 255, hovered ? 18 : 12));
        innerGradient.setColorAt(1.0, QColor(0, 0, 0, 36));
        painter.setPen(Qt::NoPen);
        painter.setBrush(innerGradient);
        painter.drawRoundedRect(innerRect, 9, 9);

        QRectF accentRect(innerRect.left() + 12, innerRect.bottom() - 8, innerRect.width() - 24, 4);
        painter.setBrush(accentColor);
        painter.drawRoundedRect(accentRect, 2, 2);

        QRect thumbRect(12, 6, GuhuoOptionThumbWidth, GuhuoOptionThumbHeight);
        QPainterPath thumbPath;
        thumbPath.addRoundedRect(QRectF(thumbRect), 8, 8);
        painter.save();
        painter.setClipPath(thumbPath);
        if (!m_thumbnail.isNull())
            painter.drawPixmap(thumbRect, m_thumbnail);
        else
            painter.fillRect(thumbRect, QColor(20, 24, 28, 220));
        painter.restore();
        painter.setPen(QPen(QColor(255, 255, 255, 36), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(thumbRect).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);

        QRect textRect(thumbRect.right() + 12, 14,
            width() - thumbRect.right() - 28, height() - 28);
        QRect titleRect = textRect;
        titleRect.setHeight(28);
        QRect typeRect = textRect;
        typeRect.setTop(titleRect.bottom() + 2);

        QFont titleFont = font();
        titleFont.setBold(true);
        titleFont.setPixelSize(15);
        painter.setFont(titleFont);
        painter.setPen(titleColor);
        QFontMetrics titleMetrics(titleFont);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
            titleMetrics.elidedText(m_title, Qt::ElideRight, titleRect.width()));

        QFont detailFont = font();
        detailFont.setPixelSize(11);
        painter.setFont(detailFont);
        painter.setPen(detailColor);
        QFontMetrics detailMetrics(detailFont);
        painter.drawText(typeRect, Qt::AlignLeft | Qt::AlignTop,
            detailMetrics.elidedText(m_typeText, Qt::ElideRight, typeRect.width()));

        if (!isEnabled())
            painter.fillPath(outerPath, QColor(88, 88, 88, 110));
    }

private:
    QString m_title;
    QString m_typeText;
    QPixmap m_thumbnail;
};

}
#endif

class Guidao : public TriggerSkillV2
{
public:
    Guidao() : TriggerSkillV2("guidao") { events << AskForRetrial; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || !data.value<JudgeStruct *>()) return {};
        if (!player->isKongcheng()) return {{player, {objectName()}}};
        for (const Card *card : player->getEquips())
            if (card->isBlack()) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!judge || !judge->card || !judge->who) return false;
        const QString prompt = QStringList{"@guidao-card", judge->who->objectName(), objectName(),
            judge->reason, QString::number(judge->card->getEffectiveId())}.join(":");
        // Selection is cancellable; Room::retrial performs the atomic exchange later.
        const Card *card = room->askForCard(ctx.owner, ".|black", prompt, *ctx.original_data,
            Card::MethodNone, judge->who, true);
        if (!card || card->isVirtualCard() || !canRetrial(room, ctx.owner, card->getEffectiveId())) return false;
        ctx.extra_data = card->getEffectiveId();
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        bool ok = false;
        const int id = ctx.extra_data.toInt(&ok);
        JudgeStruct *judge = ctx.original_data->value<JudgeStruct *>();
        if (!ok || !judge || !canRetrial(room, ctx.owner, id)) return false;
        int index = qsanRandomBounded(2) + 1;
        if (Player::isNostalGeneral(ctx.owner, "zhangjiao")) index += 2;
        room->broadcastSkillInvoke(objectName(), index);
        room->retrial(Sanguosha->getCard(id), ctx.owner, judge, objectName(), true);
        return false;
    }
private:
    static bool canRetrial(Room *room, ServerPlayer *owner, int id)
    {
        if (!owner || !owner->isAlive() || id < 0 || room->getCardOwner(id) != owner) return false;
        const Card *card = Sanguosha->getCard(id);
        return card && card->isBlack() && !owner->isCardLimited(card, Card::MethodResponse)
            && (room->getCardPlace(id) == Player::PlaceHand || room->getCardPlace(id) == Player::PlaceEquip
                || owner->getHandPile().contains(id));
    }
};

class Leiji : public TriggerSkillV2
{
public:
    Leiji() : TriggerSkillV2("leiji") { events << CardResponded << CardUsed; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("Jink")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getAlivePlayers(), objectName(), "leiji-invoke", true, true);
        if (!target) return false;
        ctx.targets = {target}; return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge; judge.pattern = ".|black"; judge.good = false; judge.negative = true; judge.reason = objectName(); judge.who = target;
        room->judge(judge);
        if (!judge.isBad() || !target->isAlive()) return false;
        const int serial = room->getTag("LeijiNextReceipt").toInt() + 1; room->setTag("LeijiNextReceipt", serial);
        const QVariantMap receipt{{"serial", serial}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        QVariantList receipts = ctx.owner->getTag("LeijiReceipts").toList(); receipts << receipt; ctx.owner->setTag("LeijiReceipts", receipts);
        const auto cleanup = qScopeGuard([&] {
            QVariantList remaining = ctx.owner->getTag("LeijiReceipts").toList(); remaining.removeOne(receipt); ctx.owner->setTag("LeijiReceipts", remaining);
        });
        DamageStruct damage(objectName(), ctx.owner, target, getEffectiveAmount(ctx), DamageStruct::Thunder);
        damage.tips << "leiji_receipt:" + QString::number(serial);
        room->damage(damage);
        return false;
    }
};

class LeijiRecover : public TriggerSkillV2
{
public:
    LeijiRecover() : TriggerSkillV2("#leiji-recover") { events << DamageCaused; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.from != player || damage.chain || damage.reason != "leiji") return true;
        for (const QVariant &entry : player->getTag("LeijiReceipts").toList()) {
            const QVariantMap receipt = entry.toMap();
            const int serial = receipt.value("serial").toInt();
            if (serial <= 0 || !damage.tips.contains("leiji_receipt:" + QString::number(serial))) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = serial; ctx.owner = ctx.initiator = ctx.invoker = player; ctx.targets = {player};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.current_event = event; ctx.original_data = &data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *, const SkillContext &ctx) const override
    { return !ctx.activationRef.isValid() && ctx.owner && ctx.owner->isAlive() && ctx.owner->getTag("LeijiReceipts").toList().contains(ctx.extra_data); }
    bool effect(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        // Recovery belongs to this accepted damage, even after Leiji's source is retired.
        QVariantList receipts = ctx.owner->getTag("LeijiReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) ctx.targets.clear();
        ctx.owner->setTag("LeijiReceipts", receipts); return false;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { room->recover(target, RecoverStruct("leiji", target, getEffectiveAmount(ctx))); return false; }
};
HuangtianCard::HuangtianCard()
{
    setSkillName("huangtian_attach");
    will_throw = false;
    handling_method = Card::MethodNone;
	mute = true;
}

void HuangtianCard::onUse(Room *room, CardUseStruct &use) const
{
	QVariant data = QVariant::fromValue(use);
	room->getThread()->trigger(PreCardUsed, room, use.from, data);
	room->getThread()->trigger(CardUsed, room, use.from, data);
	use = data.value<CardUseStruct>();
	foreach (ServerPlayer *zhangjiao, use.to){
		if (zhangjiao->isWeidi()) {
			room->broadcastSkillInvoke("weidi",-1,zhangjiao);
			room->notifySkillInvoked(zhangjiao, "weidi");
		}else {
            int index = qsanRandomBounded(2) + 1;
            if (Player::isNostalGeneral(zhangjiao, "zhangjiao"))
                index += 2;
            else if (zhangjiao->isJieGeneral())
                index += 4;
            room->broadcastSkillInvoke("huangtian", index,zhangjiao);
		}
		use.from->skillInvoked("huangtian",0,zhangjiao);

        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, use.from->objectName(), zhangjiao->objectName(), "huangtian", "");
        room->obtainCard(zhangjiao, this, reason);
    }
	room->getThread()->trigger(CardFinished, room, use.from, data);
	use = data.value<CardUseStruct>();
}

bool HuangtianCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->hasLordSkill("huangtian");
}

class HuangtianViewAsSkill : public ViewAsSkillV2
{
public:
    HuangtianViewAsSkill() : ViewAsSkillV2("huangtian_attach", 1) { setPhaseName("Play"); attached_lord_skill = true; }
    LimitScope getLimitScope() const override { return Limit_Phase; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return "HuangtianCard"; }
    bool willThrowSelectedCards() const override { return false; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.initiator->getKingdom() == "qun" && request.reason == CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId()) && (card->isKindOf("Jink") || card->isKindOf("Lightning"));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        return canSelectCard(checked, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!request.initiator || !selected.isEmpty() || !candidate || !candidate->isAlive() || candidate == request.initiator || !candidate->hasLordSkill("huangtian")) return false;
        const SkillInstance *instance = request.initiator->findSkillInstance(request.activationRef.key.skillName, request.activationRef.key.instanceID);
        return instance && instance->parentRef.key.skillName == "huangtian" && instance->parentRef.ownerObjectName == candidate->objectName();
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (!ctx.use_card || ctx.use_card->subcardsLength() != 1) return ContinueEffects;
        const int id = ctx.use_card->getSubcards().first();
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand || Sanguosha->getCard(id)->hasFlag("using")) return ContinueEffects;
        room->broadcastSkillInvoke(target->isWeidi() ? "weidi" : "huangtian", -1, target);
        room->notifySkillInvoked(target, "huangtian");
        room->giveCard(ctx.initiator, target, QList<int>{id}, "huangtian");
        return ContinueEffects;
    }
};

class Huangtian : public TriggerSkillV2
{
public:
    Huangtian() : TriggerSkillV2("huangtian$")
    { events << GameStart << EventPhaseStart << EventAcquireSkill << EventLoseSkill << Death << GeneralShown << GeneralHidden; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const override
    {
        QList<SkillInstanceRef> parents;
        for (ServerPlayer *lord : room->getAlivePlayers())
            if (lord->hasLordSkill(objectName(), true))
                for (int id : lord->getSkillInstanceIds(objectName()))
                    parents << SkillInstanceRef(lord->objectName(), SkillInstanceKey(objectName(), id));
        for (ServerPlayer *donor : room->getAllPlayers(true)) {
            // Keep exact attached sources across extra phases so a fresh attachment cannot reset turn quota.
            for (const SkillInstance &instance : donor->getSkillInstances()) {
                if (instance.skillName != "huangtian_attach" || instance.source != SourceAttached || instance.parentRef.key.skillName != objectName()) continue;
                if (!donor->isAlive() || !parents.contains(instance.parentRef) || instance.parentRef.ownerObjectName == donor->objectName())
                    room->detachAttachedSkill(SkillInstanceRef(donor->objectName(), instance.key()));
            }
            if (!donor->isAlive()) continue;
            for (const SkillInstanceRef &parent : parents)
                if (parent.ownerObjectName != donor->objectName()) room->attachSkillToPlayer(donor, "huangtian_attach", parent);
        }
        return true;
    }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *, QVariant &) const override { return {}; }
};
ShensuCard::ShensuCard()
{
    setSkillName("shensu");
}

bool ShensuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(NoSuit, 0);
    slash->setSkillName("shensu");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void ShensuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
	if(subcards.isEmpty()){
		source->skip(Player::Judge, true);
		source->skip(Player::Draw, true);
	}else
		source->skip(Player::Play, true);
	Slash *slash = new Slash(Card::NoSuit, 0);
	slash->setSkillName("_shensu");
    room->useCard(CardUseStruct(slash, source, targets));
    slash->deleteLater();
}

class ShensuViewAsSkill : public ViewAsSkillV2
{
public:
    ShensuViewAsSkill() : ViewAsSkillV2("shensu") { response_pattern = "@@shensu"; }
    QString historyKey(const ActiveSkillRequest &) const override { return "ShensuCard"; }
    TargetMode targetMode() const override { return SelectTargets; }
    TargetEffectMode targetEffectMode() const override { return WholeTargetGroup; }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return false;
        if (request.pattern == "@@shensu1") {
            const ServerPlayer *sp = qobject_cast<const ServerPlayer *>(request.initiator);
            return sp && !sp->isSkipped(Player::Judge) && !sp->isSkipped(Player::Draw);
        }
        if (request.pattern == "@@shensu2") {
            const ServerPlayer *sp = qobject_cast<const ServerPlayer *>(request.initiator);
            return sp && !sp->isSkipped(Player::Play);
        }
        return false;
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        if (request.pattern != "@@shensu2" || !request.initiator || !card || !request.selectedCardIds.isEmpty()
            || !card->isKindOf("EquipCard") || card->hasFlag("using") || request.initiator->isJilei(card)) return false;
        const int id = card->getEffectiveId();
        return id >= 0 && (request.initiator->handCards().contains(id) || request.initiator->getEquipsId().contains(id));
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.pattern == "@@shensu1") return request.selectedCardIds.isEmpty();
        if (request.selectedCardIds.size() != 1) return false;
        ActiveSkillRequest selection = request; selection.selectedCardIds.clear();
        return request.selectedCardIds.first() >= 0 && canSelectCard(selection, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    {
        if (!request.initiator || !candidate || !candidate->isAlive()) return false;
        Slash slash(Card::NoSuit, 0); slash.setSkillName(objectName());
        return !request.initiator->isProhibited(candidate, &slash) && slash.targetFilter(selected, candidate, request.initiator);
    }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        Slash slash(Card::NoSuit, 0); slash.setSkillName(objectName());
        if (!request.initiator || !slash.targetsFeasible(selected, request.initiator)) return false;
        QList<const Player *> checked;
        for (const Player *target : selected) {
            if (!canSelectTarget(request, checked, target)) return false;
            checked << target;
        }
        return true;
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!canActivate(request) || !ViewAsSkillV2::pay(room, ctx, request)) return false;
        if (request.pattern == "@@shensu1") {
            ctx.invoker->skip(Player::Judge, true);
            ctx.invoker->skip(Player::Draw, true);
        } else ctx.invoker->skip(Player::Play, true);
        return true;
    }
    EffectFlow effectOnTargetGroup(SkillContext &ctx, const QList<ServerPlayer *> &targets) const override
    {
        const SkillContext acceptedEffect = ctx;
        ctx.extra_data = QStringList();
        for (ServerPlayer *target : targets) skillEffect(ctx, target);
        QList<ServerPlayer *> accepted;
        for (const QString &name : ctx.extra_data.toStringList()) {
            ServerPlayer *target = ctx.invoker->getRoom()->findPlayerByObjectName(name);
            if (target && target->isAlive()) accepted << target;
        }
        if (accepted.isEmpty() || !ctx.invoker->isAlive()) return FinishSkill;
        std::unique_ptr<Slash> slash(new Slash(Card::NoSuit, 0)); slash->setSkillName("_shensu");
        CardUseStruct use(slash.get(), ctx.invoker, accepted); use.setOwnedCard(slash.release());
        // The paid response owns this ordinary Slash even if phase-skip callbacks retire Shensu.
        ctx.invoker->getRoom()->useCardFromSkillEffect(use, acceptedEffect, true);
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        QStringList accepted = ctx.extra_data.toStringList();
        if (!accepted.contains(target->objectName())) accepted << target->objectName();
        ctx.extra_data = accepted;
        return ContinueEffects;
    }
};

class Shensu : public TriggerSkillV2
{
public:
    Shensu() : TriggerSkillV2("shensu") { events << EventPhaseChanging; view_as_skill = new ShensuViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const Player::Phase phase = data.value<PhaseChangeStruct>().to;
        const bool first = phase == Player::Judge && !player->isSkipped(Player::Judge) && !player->isSkipped(Player::Draw);
        const bool second = phase == Player::Play && !player->isSkipped(Player::Play);
        return first || second ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        Room::AcceptedViewAsEffectScope source(room, ctx.owner, objectName(), ctx);
        if (!source.isValid()) return false;
        if (ctx.original_data->value<PhaseChangeStruct>().to == Player::Judge)
            room->askForUseCard(ctx.owner, "@@shensu1", "@shensu1", 1);
        else room->askForUseCard(ctx.owner, "@@shensu2", "@shensu2", 2, Card::MethodDiscard);
        return false;
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    { return qsanRandomBounded(2) + 1 + (player->hasSkill("baobian", true) ? 2 : 0); }
};

class ShensuTargetMod : public TargetModSkillV2
{
public:
    ShensuTargetMod() : TargetModSkillV2("#shensu-slash-ndl") { setBaseAmount(1000); }
    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == TargetModSkill::DistanceLimit && ctx.card && ctx.card->getSkillNames().contains("shensu")
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};
Jushou::Jushou() : TriggerSkillV2("jushou")
{
    events << EventPhaseStart;
}

int Jushou::getJushouDrawNum(ServerPlayer *) const
{
    return 1;
}

TriggerList Jushou::triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const
{
    return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Finish
        ? TriggerList{{player, {objectName()}}} : TriggerList();
}

bool Jushou::cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const
{
    if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
    ctx.targets = {ctx.owner};
    return true;
}

bool Jushou::effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const
{
    room->broadcastSkillInvoke(objectName());
    // Keep native variant draw rules while giving the actual beneficiary its target interception.
    target->drawCards(qMax(0, getJushouDrawNum(ctx.owner)) * getEffectiveAmount(ctx), objectName());
    if (target->isAlive()) target->turnOver();
    return false;
}
class Jiewei : public TriggerSkillV2
{
public:
    Jiewei() : TriggerSkillV2("jiewei") { events << TurnedOver; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName())
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!room->askForSkillInvoke(ctx.owner, objectName())) return false;
        ctx.targets = {ctx.owner};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "discard") {
            const QVariantMap selection = ctx.extra_data.toMap();
            ServerPlayer *actor = room->findPlayerByObjectName(selection.value("actor").toString(), true);
            if (!actor || !actor->isAlive()) return false;
            const int type = selection.value("type").toInt();
            QList<int> disabled;
            for (const Card *card : target->getCards("ej"))
                if (underlyingType(card) != type) disabled << card->getEffectiveId();
            if (!hasMaterial(actor, target, type)) return false;
            const int id = room->askForCardChosen(actor, target, "ej", objectName(), false, Card::MethodDiscard, disabled);
            const Card *card = id >= 0 ? Sanguosha->getCard(id) : nullptr;
            if (card && !card->hasFlag("using") && underlyingType(card) == type && room->getCardOwner(id) == target
                && (room->getCardPlace(id) == Player::PlaceEquip || room->getCardPlace(id) == Player::PlaceDelayedTrick)
                && actor->canDiscard(target, id)) room->throwCard(id, target, actor);
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        target->drawCards(getEffectiveAmount(ctx), objectName());
        if (!target->isAlive()) return false;
        // Retain the returned use payload while inspecting its virtual card after nested resolution.
        const CardUseStruct used = room->askForUseCardStruct(target,
            "TrickCard+^Nullification+^Suijiyingbian,EquipCard|.|.|hand", "@jiewei");
        if (!used.card || !target->isAlive()) return false;
        const int type = used.card->getTypeId();
        QList<ServerPlayer *> candidates;
        for (ServerPlayer *candidate : room->getAlivePlayers())
            if (hasMaterial(target, candidate, type)) candidates << candidate;
        if (candidates.isEmpty()) return false;
        ServerPlayer *discardTarget = room->askForPlayerChosen(target, candidates, objectName(), "@jiewei-discard", true);
        if (discardTarget) {
            SkillContext discard = ctx;
            discard.choice = "discard"; discard.targets = {discardTarget};
            discard.extra_data = QVariantMap{{"actor", target->objectName()}, {"type", type}};
            skillEffect(event, room, player, discard, discardTarget);
        }
        return false;
    }
private:
    static int underlyingType(const Card *card)
    {
        if (!card) return -1;
        const Card *actual = card->getTypeId() == Card::TypeSkill ? Sanguosha->getEngineCard(card->getEffectiveId()) : card;
        return actual ? int(actual->getTypeId()) : -1;
    }
    static bool hasMaterial(ServerPlayer *actor, ServerPlayer *target, int type)
    {
        if (type != Card::TypeTrick && type != Card::TypeEquip) return false;
        for (const Card *card : target->getCards("ej"))
            if (!card->hasFlag("using") && underlyingType(card) == type && actor->canDiscard(target, card->getEffectiveId())) return true;
        return false;
    }
};
class Liegong : public TriggerSkillV2
{
public:
    Liegong() : TriggerSkillV2("liegong") { events << TargetSpecified; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill(objectName()) && use.from == player
                && player->getPhase() == Player::Play && use.card && use.card->isKindOf("Slash")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        for (ServerPlayer *target : use.to) {
            const int hand = target->getHandcardNum();
            if ((ctx.owner->getHp() <= hand || ctx.owner->getAttackRange() >= hand)
                && ctx.owner->askForSkillInvoke(this, QVariant::fromValue(target))) ctx.targets << target;
        }
        return !ctx.targets.isEmpty();
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        if (!use.card || !use.from) return false;
        const int index = use.to.indexOf(target);
        const QString key = "Jink_" + use.card->toString();
        QVariantList jinks = use.from->getTag(key).toList();
        if (index < 0 || index >= jinks.size()) return false;
        room->broadcastSkillInvoke(objectName());
        LogMessage log;
        log.type = "#NoJink";
        log.from = target;
        room->sendLog(log);
        // Re-read the protocol-owned per-use requirements after each target hook.
        jinks[index] = 0;
        use.from->setTag(key, jinks);
        return false;
    }
};

class Kuanggu : public TriggerSkillV2
{
public:
    Kuanggu() : TriggerSkillV2("kuanggu") { frequency = Compulsory; events << Damage; }
    TriggerList triggerable(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || player != damage.from || !player->hasSkill(objectName()) || !player->isWounded()) return {};
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (eventId <= 0) return {};
        const QVariantMap range = room->getTag("KuangguDamageRanges").toMap().value(QString::number(eventId)).toMap();
        return range.value("from").toString() == player->objectName() && range.value("in_range").toBool()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        room->recover(target, RecoverStruct(ctx.owner, nullptr,
            ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx), objectName()));
        return false;
    }
};

class KuangguRecord : public TriggerSkillV2
{
public:
    KuangguRecord() : TriggerSkillV2("#kuanggu-record") { events << DamageDone << DamageComplete; global = true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        const DamageStruct damage = data.value<DamageStruct>();
        if (player != damage.to) return true;
        const qint64 eventId = room->historyParent(room->currentHistoryEventId(), "damage", true).value("id").toLongLong();
        if (eventId <= 0) return true;
        QVariantMap ranges = room->getTag("KuangguDamageRanges").toMap();
        const QString key = QString::number(eventId);
        if (event == DamageComplete) ranges.remove(key);
        else if (damage.from && damage.to) {
            // Bind the pre-dying distance to this damage episode; nested damage cannot overwrite it.
            ranges.insert(key, QVariantMap{{"from", damage.from->objectName()},
                {"in_range", damage.from->distanceTo(damage.to) <= 1}});
        }
        if (ranges.isEmpty()) room->removeTag("KuangguDamageRanges");
        else room->setTag("KuangguDamageRanges", ranges);
        return true;
    }
};
class Buqu : public TriggerSkillV2
{
public:
    Buqu() : TriggerSkillV2("buqu")
    {
        events << AskForPeaches;
        frequency = Compulsory;
    }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *zhoutai, QVariant &data) const override
    {
        TriggerList result;
        const DyingStruct dying = data.value<DyingStruct>();
        if (TriggerSkill::triggerable(zhoutai) && dying.who == zhoutai && zhoutai->getHp() <= 0)
            result[zhoutai] << objectName();
        return result;
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    { ctx.targets = {ctx.owner}; return true; }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *zhoutai) const override
    {
        // The wound is physical material; the recovery threshold uses this target effect amount.
        room->sendCompulsoryTriggerLog(zhoutai, this);
        int id = room->drawCard();
        if (id < 0) return false;
        zhoutai->addToPile("buqu", id);
        if (!zhoutai->isAlive() || !zhoutai->getPile("buqu").contains(id)) return false;
        int num = Sanguosha->getCard(id)->getNumber();
        foreach (int card_id, zhoutai->getPile("buqu")) {
            if (card_id!=id&&Sanguosha->getCard(card_id)->getNumber()==num) {
                QList<int> ids;
				ids << id << card_id;
				LogMessage log;
                log.type = "$NosBuquDuplicateItem";
                log.from = zhoutai;
                log.card_str = ListI2S(ids).join("+");
                room->sendLog(log);
				room->throwCard(id, objectName(), nullptr);
                return false;
            }
        }
        room->recover(zhoutai, RecoverStruct(zhoutai, nullptr, qMax(0, getEffectiveAmount(ctx) - zhoutai->getHp()), objectName()));
        return false;
    }
};

class BuquMaxCards : public MaxCardsSkillV2
{
public:
    BuquMaxCards() : MaxCardsSkillV2("#buqu")
    {
    }

    CorrectSkillResult getFixedValue(const CorrectSkillContext &ctx) const override
    {
        const int n = ctx.holder ? ctx.holder->getPile("buqu").length() : 0;
        return n > 0 ? CorrectSkillResult::useAmount(n * ctx.currentAmount)
                     : CorrectSkillResult::noEffect();
    }
};

class Fenji : public TriggerSkillV2
{
public:
    Fenji() : TriggerSkillV2("fenji") { events << CardsMoveOneTime; m_baseAmount = 2; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        // CardsMoveOneTime is broadcast per observer: each observer admits only their own instances.
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getHp() > 0
            && move.from && move.from->isAlive() && move.from_places.contains(Player::PlaceHand)
            && ((move.reason.m_reason == CardMoveReason::S_REASON_DISMANTLE && move.reason.m_playerId != move.reason.m_targetId)
                || (move.to && move.to != move.from && move.to_place == Player::PlaceHand
                    && move.reason.m_reason != CardMoveReason::S_REASON_GIVE && move.reason.m_reason != CardMoveReason::S_REASON_SWAP))
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = static_cast<ServerPlayer *>(ctx.original_data->value<CardsMoveOneTimeStruct>().from);
        if (!target || !target->isAlive()) return false;
        const bool previous = target->hasFlag("FenjiMoveFrom");
        target->setFlags("FenjiMoveFrom");
        const auto restore = qScopeGuard([target, previous] { if (!previous) target->setFlags("-FenjiMoveFrom"); });
        if (!room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data)) return false;
        ctx.targets = {target};
        return true;
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        room->broadcastSkillInvoke(objectName());
        // This loss is part of resolution: the selected beneficiary still draws if the owner dies.
        room->loseHp(HpLostStruct(ctx.owner, 1, objectName(), ctx.owner));
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        target->drawCards(getEffectiveAmount(ctx), objectName());
        return false;
    }
};
class Hongyan : public FilterSkill
{
public:
    Hongyan() : FilterSkill("hongyan")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Spade;
    }

    const Card *viewAs(const Card *original) const
    {
        Card *new_card = Sanguosha->cloneCard(original->objectName(),Card::Heart,original->getNumber());
        new_card->setSkillName("hongyan");
        return new_card;
    }

	int getEffectIndex(const ServerPlayer *, const Card *) const
	{
		return -2;
	}
};

TianxiangCard::TianxiangCard()
{
    setSkillName("tianxiang");
}

void TianxiangCard::onEffect(CardEffectStruct &effect) const
{
    DamageStruct damage = effect.from->getTag("TianxiangDamage").value<DamageStruct>();
    damage.to = effect.to;
    damage.transfer = true;
    damage.transfer_reason = "tianxiang";
    effect.from->setTag("TransferDamage", QVariant::fromValue(damage));
}

class TianxiangViewAsSkill : public ViewAsSkillV2
{
public:
    TianxiangViewAsSkill() : ViewAsSkillV2("tianxiang", 1) { response_pattern = "@@tianxiang"; }
    TargetMode targetMode() const override { return SelectTargets; }
    QString historyKey(const ActiveSkillRequest &) const override { return "TianxiangCard"; }
    bool canActivate(const ActiveSkillRequest &request) const override
    { return request.initiator && request.pattern == "@@tianxiang" && request.reason != CardUseStruct::CARD_USE_REASON_PLAY; }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
            && !request.initiator->isJilei(card) && card->getSuit() == Card::Heart && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        return canSelectCard(checked, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected, const Player *candidate) const override
    { return selected.isEmpty() && candidate && candidate->isAlive() && candidate != request.initiator; }
    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    { return selected.size() == 1 && canSelectTarget(request, {}, selected.first()); }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        // The pending damage belongs to the original responder even when its effect actor changes.
        DamageStruct damage = ctx.initiator->getTag("TianxiangDamage").value<DamageStruct>();
        if (damage.to != ctx.initiator || damage.damage <= 0) return ContinueEffects;
        const int serial = room->getTag("TianxiangNextReceipt").toInt() + 1; room->setTag("TianxiangNextReceipt", serial);
        QVariantList receipts = target->getTag("TianxiangReceipts").toList();
        receipts << QVariantMap{{"serial", serial}, {"recipient", target->objectName()}, {"issuer", ctx.invoker->objectName()}, {"amount", getEffectiveAmount(ctx)},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID}};
        target->setTag("TianxiangReceipts", receipts);
        damage.to = target; damage.transfer = true; damage.transfer_reason = "tianxiang";
        damage.tips << "tianxiang_receipt:" + QString::number(serial);
        ctx.initiator->setTag("TransferDamage", QVariant::fromValue(damage));
        ctx.initiator->setTag("TianxiangTransferred", true);
        return ContinueEffects;
    }
};

class Tianxiang : public TriggerSkillV2
{
public:
    Tianxiang() : TriggerSkillV2("tianxiang") { events << DamageInflicted; view_as_skill = new TianxiangViewAsSkill; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->canDiscard(player, "h")
            && data.value<DamageStruct>().to == player ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        const QVariant previousDamage = ctx.owner->getTag("TianxiangDamage"), previousTransferred = ctx.owner->getTag("TianxiangTransferred");
        const auto restore = qScopeGuard([&] {
            ctx.owner->setTag("TianxiangDamage", previousDamage); ctx.owner->setTag("TianxiangTransferred", previousTransferred);
        });
        // These are stack-scoped prompt inputs and the native transfer output, not persistent source authority.
        ctx.owner->setTag("TianxiangDamage", *ctx.original_data); ctx.owner->setTag("TianxiangTransferred", false);
        Room::AcceptedViewAsEffectScope response(room, ctx.owner, objectName(), ctx);
        if (!response.isValid()) return false;
        room->askForUseCard(ctx.owner, "@@tianxiang", "@tianxiang-card", -1, Card::MethodDiscard);
        return ctx.owner->getTag("TianxiangTransferred").toBool();
    }
    int getEffectIndex(const ServerPlayer *player, const Card *) const override
    { return qsanRandomBounded(2) + 1 + (!player->hasInnateSkill(this) && player->hasSkill("luoyan") ? 2 : 0); }
};

class TianxiangDraw : public TriggerSkillV2
{
public:
    TianxiangDraw() : TriggerSkillV2("#tianxiang") { events << DamageComplete << EventSkillEffectFinished; global = true; frequency = Compulsory; }
    bool collectTriggerContexts(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data, QList<SkillContext> &contexts) const override
    {
        if (event != DamageComplete) return true;
        const DamageStruct damage = data.value<DamageStruct>();
        if (!player || !player->isAlive() || damage.to != player || !damage.transfer || damage.transfer_reason != "tianxiang") return true;
        for (const QVariant &entry : player->getTag("TianxiangReceipts").toList()) {
            const QVariantMap receipt = entry.toMap(); const int serial = receipt.value("serial").toInt();
            if (serial <= 0 || !damage.tips.contains("tianxiang_receipt:" + QString::number(serial))) continue;
            SkillContext ctx; ctx.skill_name = objectName(); ctx.instanceID = serial;
            ctx.owner = room->findPlayerByObjectName(receipt.value("issuer").toString(), true);
            if (!ctx.owner) continue;
            ctx.initiator = ctx.owner; ctx.invoker = player; ctx.targets = {player};
            ctx.sourceRef = SkillInstanceRef(receipt.value("source_owner").toString(), SkillInstanceKey(receipt.value("source_skill").toString(), receipt.value("source_id").toInt()));
            ctx.amount = receipt.value("amount").toInt(); ctx.extra_data = receipt; ctx.current_event = event; ctx.original_data = &data; contexts << ctx;
        }
        return true;
    }
    bool isSourceAvailable(Room *room, const SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        return !ctx.activationRef.isValid() && holder && holder->isAlive() && holder->getTag("TianxiangReceipts").toList().contains(ctx.extra_data);
    }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const override
    {
        if (event != EventSkillEffectFinished) return true;
        const SkillContext ctx = data.value<SkillContext>();
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (ctx.skill_name == objectName() && !ctx.activationRef.isValid() && holder) {
            QVariantList receipts = holder->getTag("TianxiangReceipts").toList(); receipts.removeOne(ctx.extra_data); holder->setTag("TianxiangReceipts", receipts);
        }
        return true;
    }
    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *holder = room->findPlayerByObjectName(ctx.extra_data.toMap().value("recipient").toString(), true);
        if (!holder) { ctx.targets.clear(); return false; }
        QVariantList receipts = holder->getTag("TianxiangReceipts").toList();
        if (!receipts.removeOne(ctx.extra_data)) ctx.targets.clear();
        holder->setTag("TianxiangReceipts", receipts); return false;
    }
    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    { target->drawCards(target->getLostHp() * getEffectiveAmount(ctx), "tianxiang"); return false; }
};
GuhuoCard::GuhuoCard()
{
    setSkillName("guhuo");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool GuhuoCard::guhuo(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();

    room->setTag("GuhuoType", user_string);

    ServerPlayer *questioned = nullptr;
    foreach (ServerPlayer *player, room->getOtherPlayers(yuji)) {
        QString choice = "noquestion+question";
        if (player->hasSkill("chanyuan")) {
            room->sendCompulsoryTriggerLog(player, "chanyuan", true, true);
            choice = "noquestion";
        }
        choice = room->askForChoice(player, "guhuo", choice, yuji->objectName()+":"+user_string);
        LogMessage log;
        log.type = "#GuhuoQuery";
        log.from = player;
        log.arg = choice;
        room->sendLog(log);
        if (choice == "question"){
            room->setEmotion(player, "question");
            questioned = player;
            break;
		}else
            room->setEmotion(player, "no-question");
    }

    LogMessage log;
    log.type = "$GuhuoResult";
    log.from = yuji;
    log.card_str = QString::number(subcards.first());
    room->sendLog(log);
    room->addPlayerMark(yuji, "guhuoUsed-Clear");
	foreach(ServerPlayer *player, room->getAlivePlayers())
		room->setEmotion(player, ".");

    bool success = false;
    if (!questioned) {
        success = true;
        CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "guhuo");
        CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
        room->moveCardsAtomic(move, true);
    } else {
        const Card *card = Sanguosha->getCard(subcards.first());
        if (user_string == "peach+analeptic")
            success = card->objectName() == yuji->getTag("GuhuoSaveSelf").toString();
        else if (user_string == "slash")
            success = card->objectName().contains("slash");
        else if (user_string == "normal_slash")
            success = card->objectName() == "slash";
        else
            success = card->match(user_string);

        if (success) {
            CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "guhuo");
            CardsMoveStruct move(subcards, yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
            room->moveCardsAtomic(move, true);
			room->acquireSkill(questioned, "chanyuan");
        } else {
            room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "guhuo"), true);
        }
    }
    yuji->removeTag("GuhuoSaveSelf");
    yuji->removeTag("GuhuoSlash");
    return success;
}

bool GuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("guhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFilter(targets, to_select, Self);
}

bool GuhuoCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("guhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetFixed();
}

bool GuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("guhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card && card->targetsFeasible(targets, Self);
}

const Card *GuhuoCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *yuji = card_use.from;
    Room *room = yuji->getRoom();

    QString to_guhuo = user_string;
    if ((user_string.contains("slash") || (user_string.contains("Slash")))
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
		static QList<const Slash *>cards = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, cards) {
            QString name = slash->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            guhuo_list << name;
        }

        if (guhuo_list.isEmpty()) guhuo_list << "slash";
        to_guhuo = room->askForChoice(yuji, "guhuo_slash", guhuo_list.join("+"));
        yuji->setTag("GuhuoSlash", QVariant(to_guhuo));
    }
    room->broadcastSkillInvoke("guhuo");

    LogMessage log;
    log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
    log.from = yuji;
    log.to = card_use.to;
    log.arg = to_guhuo;
    log.arg2 = "guhuo";

    room->sendLog(log);

    if (guhuo(card_use.from)) {
        Card *card = Sanguosha->getCard(subcards.first());
		Card *use_card;
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			use_card = card;
		else{
			use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
		}
        foreach (ServerPlayer *to, card_use.to) {
            const Skill *skill = room->isProhibited(card_use.from, to, use_card);
            if (skill) {
				log.type = "#SkillAvoid";
				log.from = to;
                if (skill->isVisible()) {
                    log.arg = skill->objectName();
                    log.arg2 = use_card->objectName();
                    room->sendLog(log);

                    room->broadcastSkillInvoke(skill->objectName());
                    room->notifySkillInvoked(to, skill->objectName());
                } else {
                    skill = Sanguosha->getMainSkill(skill->objectName());
                    if (skill && skill->isVisible()) {
						log.arg = skill->objectName();
						log.arg2 = objectName();
                        if (to->hasSkill(skill)) {
                            room->sendLog(log);

                            room->broadcastSkillInvoke(skill->objectName());
                            room->notifySkillInvoked(to, skill->objectName());
                        } else if (yuji->hasSkill(skill)) {
                            log.type = "#SkillAvoidFrom";
                            log.from = yuji;
                            log.to.clear();
                            log.to << to;
                            room->sendLog(log);

                            room->broadcastSkillInvoke(skill->objectName());
                            room->notifySkillInvoked(yuji, skill->objectName());
                        }
                    }
                }
                card_use.to.removeOne(to);
            }
        }
        return use_card;
    }
	return nullptr;
}

const Card *GuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    room->broadcastSkillInvoke("guhuo");

    QString to_guhuo;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
		static QList<const Peach *>Peachs = Sanguosha->findChildren<const Peach *>();
        foreach (const Peach *peach, Peachs) {
            QString name = peach->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
            guhuo_list << name;
            break;
        }
		static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
        foreach (const Analeptic *ana, anas) {
            QString name = ana->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
            guhuo_list << name;
            break;
        }

        if (guhuo_list.isEmpty())
            guhuo_list << "peach";
        to_guhuo = room->askForChoice(yuji, "guhuo_saveself", guhuo_list.join("+"));
        yuji->setTag("GuhuoSaveSelf", QVariant(to_guhuo));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList guhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (guhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            guhuo_list << name;
        }

        if (guhuo_list.isEmpty())
            guhuo_list << "slash";
        to_guhuo = room->askForChoice(yuji, "guhuo_slash", guhuo_list.join("+"));
        yuji->setTag("GuhuoSlash", QVariant(to_guhuo));
    } else
        to_guhuo = user_string;

    LogMessage log;
    log.type = "#GuhuoNoTarget";
    log.from = yuji;
    log.arg = to_guhuo;
    log.arg2 = "guhuo";
    room->sendLog(log);

    if (guhuo(yuji)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_guhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_guhuo = card->objectName();
		} else if (to_guhuo == "normal_slash")
			to_guhuo = "slash";
		if (to_guhuo.startsWith(card->objectName()))
			return card;
		else{
			if (to_guhuo == "slash") {
				if (card->isKindOf("Slash"))
					to_guhuo = card->objectName();
			} else if (to_guhuo == "normal_slash")
				to_guhuo = "slash";
			Card *use_card = Sanguosha->cloneCard(to_guhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

class Guhuo : public ViewAsSkillV2
{
public:
    Guhuo(const QString &name = "guhuo") : ViewAsSkillV2(name, 1) { response_or_use = true; }
    LimitScope getLimitScope() const override { return objectName() == "guhuo" ? Limit_Turn : Limit_None; }
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName()); }
    bool canActivate(const ActiveSkillRequest &request) const override
    {
        if (!request.initiator || request.initiator->isKongcheng()) return false;
        if (request.reason == CardUseStruct::CARD_USE_REASON_PLAY) return true;
        return !request.pattern.startsWith(".") && !request.pattern.startsWith("@")
            && (objectName() != "guhuo" || request.initiator->hasTurn()) && !usableNames(request).isEmpty();
    }
    bool canSelectCard(const ActiveSkillRequest &request, const Card *card) const override
    {
        return request.initiator && request.selectedCardIds.isEmpty() && card && !card->hasFlag("using")
            && request.initiator->handCards().contains(card->getEffectiveId());
    }
    bool cardSelectionFeasible(const ActiveSkillRequest &request) const override
    {
        if (request.selectedCardIds.size() != 1 || request.selectedCardIds.first() < 0) return false;
        ActiveSkillRequest checked = request; checked.selectedCardIds.clear();
        return canSelectCard(checked, Sanguosha->getCard(request.selectedCardIds.first()));
    }
    bool pay(Room *room, SkillContext &ctx, const ActiveSkillRequest &request) const override
    {
        if (!ctx.initiator || ctx.executionID <= 0 || !cardSelectionFeasible(request) || !ctx.use_card) return false;
        const int id = request.selectedCardIds.first();
        const Card *material = Sanguosha->getCard(id);
        if (room->getCardOwner(id) != ctx.initiator || room->getCardPlace(id) != Player::PlaceHand) return false;
        const QVariantMap receipt{{"execution", ctx.executionID}, {"skill", objectName()}, {"id", id},
            {"activation_owner", ctx.activationRef.ownerObjectName}, {"activation_skill", ctx.activationRef.key.skillName}, {"activation_id", ctx.activationRef.key.instanceID},
            {"source_owner", ctx.sourceRef.ownerObjectName}, {"source_skill", ctx.sourceRef.key.skillName}, {"source_id", ctx.sourceRef.key.instanceID},
            {"name", material->objectName()}, {"suit", int(material->getSuit())}, {"number", material->getNumber()}, {"slash", material->isKindOf("Slash")},
            {"real", ctx.use_card->objectName() == "slash" ? material->isKindOf("Slash") : material->objectName() == ctx.use_card->objectName()}};
        QVariantList receipts = ctx.initiator->getTag("GuhuoPayments").toList(); receipts << receipt; ctx.initiator->setTag("GuhuoPayments", receipts);
        ctx.extra_data = receipt;
        // The actual material is paid face down before any gameplay challenge. Finished owns abort cleanup.
        room->moveCardsAtomic(CardsMoveStruct(QList<int>{id}, ctx.initiator, nullptr, Player::PlaceHand, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_USE, ctx.initiator->objectName(), objectName(), QString())), false);
        return room->getCardPlace(id) == Player::PlaceTable && !room->getCardOwner(id);
    }
    EffectFlow effect(SkillContext &ctx) const override
    {
        Room *room = ctx.invoker->getRoom();
        QVariantMap receipt = ctx.extra_data.toMap();
        if (ctx.bypass_cost && receipt.isEmpty() && ctx.use_card && ctx.use_card->subcardsLength() == 1) {
            const int materialId = ctx.use_card->getSubcards().first();
            if (materialId < 0 || room->getCardOwner(materialId) != ctx.initiator || room->getCardPlace(materialId) != Player::PlaceHand) return FinishSkill;
            const Card *material = Sanguosha->getCard(materialId);
            receipt = QVariantMap{{"id", materialId}, {"name", material->objectName()}, {"suit", int(material->getSuit())}, {"number", material->getNumber()},
                {"slash", material->isKindOf("Slash")}, {"waived", true},
                {"real", ctx.use_card->objectName() == "slash" ? material->isKindOf("Slash") : material->objectName() == ctx.use_card->objectName()}};
        }
        const int id = receipt.value("id", -1).toInt();
        const auto materialAvailable = [&] {
            return id >= 0 && (receipt.value("waived").toBool()
                ? room->getCardOwner(id) == ctx.initiator && room->getCardPlace(id) == Player::PlaceHand
                : !room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable);
        };
        if (!materialAvailable()) return FinishSkill;
        const bool nostalgia = objectName() == "nosguhuo";
        const QString declaration = ctx.use_card->objectName(), tag = nostalgia ? "NosGuhuoType" : "GuhuoType";
        const QVariant previous = room->getTag(tag);
        room->setTag(tag, declaration);
        const auto restore = qScopeGuard([&] {
            room->setTag(tag, previous);
            for (ServerPlayer *other : room->getAlivePlayers()) room->setEmotion(other, ".");
        });
        room->broadcastSkillInvoke(objectName());
        LogMessage log; log.type = ctx.targets.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo"; log.from = ctx.invoker; log.to = ctx.targets;
        log.arg = declaration; log.arg2 = objectName(); room->sendLog(log);
        QList<ServerPlayer *> questioned;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.invoker)) {
            QString choices = "noquestion+question";
            if ((nostalgia && other->getHp() <= 0) || (!nostalgia && other->hasSkill("chanyuan"))) choices = "noquestion";
            const QString choice = room->askForChoice(other, objectName(), choices, ctx.invoker->objectName() + ":" + declaration);
            LogMessage query; query.type = "#GuhuoQuery"; query.from = other; query.arg = choice; room->sendLog(query);
            room->setEmotion(other, choice == "question" ? "question" : "no-question");
            if (choice == "question" && choices.contains("+question")) { questioned << other; if (!nostalgia) break; }
        }
        log = LogMessage(); log.type = "$GuhuoResult"; log.from = ctx.invoker; log.card_str = QString::number(id); room->sendLog(log);
        const bool real = receipt.value("real").toBool();
        const bool success = questioned.isEmpty() || (real && (!nostalgia || receipt.value("suit").toInt() == Card::Heart));
        for (ServerPlayer *other : questioned) {
            if (!nostalgia && !real) break;
            SkillContext outcome = ctx; outcome.choice = nostalgia ? (real ? "lose_hp" : "draw") : "chanyuan"; outcome.targets = {other};
            skillEffect(outcome, other);
        }
        if (!success || !ctx.invoker->isAlive() || !materialAvailable()) return FinishSkill;
        QString name = declaration;
        if (name == "slash" && receipt.value("slash").toBool()) name = receipt.value("name").toString();
        Card *card = Sanguosha->cloneCard(name, Card::Suit(receipt.value("suit").toInt()), receipt.value("number").toInt());
        if (!card) return FinishSkill;
        card->addSubcard(id); card->setSkillName(objectName()); card->setCanRecast(false); card->deleteLater();
        ctx.updated_card = card;
        return ContinueEffects;
    }
    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        Room *room = ctx.invoker->getRoom();
        if (ctx.choice == "chanyuan") room->acquireSkillFromEffect(target, "chanyuan", ctx);
        else if (ctx.choice == "lose_hp") room->loseHp(HpLostStruct(target, getEffectiveAmount(ctx), objectName(), ctx.invoker));
        else if (ctx.choice == "draw") target->drawCards(getEffectiveAmount(ctx), objectName());
        return ContinueEffects;
    }
protected:
    bool allowDeclaration(const Player *, const QString &name) const override
    {
        std::unique_ptr<Card> card(Sanguosha->cloneCard(name));
        return card && (card->isKindOf("BasicCard") || card->isNDTrick());
    }
    Card *buildCard(const ActiveSkillRequest &request, const QString &name) const override
    {
        Card *card = ViewAsSkillV2::buildCard(request, name);
        if (card) card->setCanRecast(false);
        return card;
    }
};

class GuhuoPaymentCleanup : public TriggerSkillV2
{
public:
    GuhuoPaymentCleanup() : TriggerSkillV2("#guhuo-payment") { events << EventSkillEffectFinished; global = true; }
    bool recordEvent(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const override
    {
        const SkillContext finished = data.value<SkillContext>();
        ServerPlayer *payer = finished.initiator;
        if (!payer || finished.executionID <= 0 || !finished.activationRef.isValid()) return true;
        const QVariantMap completion = finished.interceptor_data.value("native_response_completion");
        const CardResponseStruct response = finished.original_data ? finished.original_data->value<CardResponseStruct>() : CardResponseStruct();
        const bool provision = completion.value("completed").toBool() && completion.value("is_provision").toBool()
            && !completion.value("nullified").toBool() && !response.nullified && response.m_card
            && response.skillExecutionID == finished.executionID && response.activationRef == finished.activationRef;
        QVariantList keep; QList<int> remaining;
        for (const QVariant &entry : payer->getTag("GuhuoPayments").toList()) {
            const QVariantMap receipt = entry.toMap();
            const bool match = receipt.value("execution").toLongLong() == finished.executionID
                && receipt.value("activation_owner").toString() == finished.activationRef.ownerObjectName
                && receipt.value("activation_skill").toString() == finished.activationRef.key.skillName
                && receipt.value("activation_id").toInt() == finished.activationRef.key.instanceID;
            if (!match) { keep << entry; continue; }
            const int id = receipt.value("id", -1).toInt();
            // Only the final, completed provision response can take custody of this exact paid material.
            const bool handedOff = provision && (response.m_card->isVirtualCard()
                ? response.m_card->getSubcards().contains(id) : response.m_card->getEffectiveId() == id);
            if (!handedOff && id >= 0 && !room->getCardOwner(id) && room->getCardPlace(id) == Player::PlaceTable) remaining << id;
        }
        payer->setTag("GuhuoPayments", keep);
        if (!remaining.isEmpty()) { DummyCard cards(remaining); room->throwCard(&cards, nullptr); }
        return true;
    }
};
class Chanyuan : public TriggerSkillV2
{
public:
    Chanyuan() : TriggerSkillV2("chanyuan")
    {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory; global = true;
    }
    int getPriority(TriggerEvent) const override { return 5; }
    bool usesEventPriority() const override { return true; }
    bool recordEvent(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player) return true;
        const bool changed = event == EventAcquireSkill || event == EventLoseSkill;
        if (changed && data.value<SkillChangeStruct>().skillName != objectName()) return true;
        if (!changed && !player->hasSkill(objectName(), true)) return true;
        // This is presentation refresh only; ChanyuanInvalidity reads the actual skill state.
        room->setPlayerMark(player, "@chanyuan", player->hasSkill(objectName(), true) ? 1 : 0);
        for (ServerPlayer *other : room->getOtherPlayers(player)) room->filterCards(other, other->getCards("he"), true);
        JsonArray args; args << QSanProtocol::S_GAME_EVENT_UPDATE_SKILL;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, args);
        return true;
    }
};
class ChanyuanInvalidity : public InvaliditySkill
{
public:
    ChanyuanInvalidity() : InvaliditySkill("#chanyuan-inv")
    {
    }

    bool isSkillValid(const Player *player, const Skill *skill) const
    {
        return player->getHp() != 1 || skill->objectName() == "chanyuan" || !player->hasSkill("chanyuan");
    }
};

class TenyearLeiji : public TriggerSkillV2
{
public:
    TenyearLeiji() : TriggerSkillV2("tenyearleiji") { events << CardUsed << CardResponded; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return player && player->isAlive() && player->hasSkill(objectName()) && card && card->isKindOf("Jink")
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, room->getOtherPlayers(ctx.owner),
            objectName(), "@tenyearleiji", true, true);
        if (!target) return false;
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx, ServerPlayer *target) const override
    {
        if (ctx.choice == "recover") {
            room->recover(target, RecoverStruct(ctx.owner, nullptr, getEffectiveAmount(ctx), objectName()));
            return false;
        }
        room->broadcastSkillInvoke(objectName());
        JudgeStruct judge;
        judge.who = target; judge.pattern = ".|black"; judge.good = false; judge.reason = objectName();
        room->judge(judge);
        if (!judge.card) return false;
        const Card::Suit suit = judge.card->getSuit();
        if (suit != Card::Spade && suit != Card::Club) return false;
        if (suit == Card::Club && ctx.owner->isAlive()) {
            // Recovery has a different recipient from the judgement/damage effect.
            SkillContext recovery = ctx;
            recovery.choice = "recover";
            recovery.targets = {ctx.owner};
            skillEffect(event, room, player, recovery, ctx.owner);
        }
        if (target->isAlive()) room->damage(DamageStruct(objectName(), ctx.owner, target,
            (suit == Card::Spade ? 2 : 1) * getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};
class NosLeiji : public TriggerSkillV2
{
public:
    NosLeiji() : TriggerSkillV2("nosleiji") { events << CardUsed << CardResponded; m_baseAmount = 2; }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName())) return {};
        const Card *card = event == CardUsed ? data.value<CardUseStruct>().card : data.value<CardResponseStruct>().m_card;
        return card && card->isKindOf("Jink") ? TriggerList{{player, QStringList(objectName())}} : TriggerList();
    }
    bool cost(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner) return false;
        ServerPlayer *target = room->askForPlayerChosen(ctx.owner, Config.EnableHegemony ? room->getOtherPlayers(ctx.owner) : room->getAlivePlayers(), objectName(),
                                                       "leiji-invoke", true);
        if (!target) return false;
        // Chosen targets belong to this execution; nested Jink responses cannot overwrite them.
        ctx.targets = {target};
        return true;
    }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        ServerPlayer *source = ctx.invoker;
        if (!source || !target || !target->isAlive()) return false;
        room->broadcastSkillInvoke(objectName(), source);
        JudgeStruct judge;
        judge.pattern = ".|spade";
        judge.good = false;
        judge.negative = true;
        judge.reason = objectName();
        judge.who = target;
        room->judge(judge);
        if (judge.isBad()) room->damage(DamageStruct(objectName(), source, target, getEffectiveAmount(ctx), DamageStruct::Thunder));
        return false;
    }
};

class NosJushou : public Jushou
{
public:
    NosJushou() : Jushou()
    {
        setObjectName("nosjushou");
    }

    int getJushouDrawNum(ServerPlayer *) const
    {
        return 3;
    }
};

class NosBuquRemove : public TriggerSkillV2
{
public:
    NosBuquRemove() : TriggerSkillV2("#nosbuqu-remove") { events << HpRecover; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    { return player && player->isAlive() && player->hasSkill(objectName()) && !player->getPile("nosbuqu").isEmpty() ? TriggerList{{player, {objectName()}}} : TriggerList(); }
    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override { ctx.targets = {ctx.owner}; return true; }
    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int retain = qMax(0, getEffectiveAmount(ctx) - target->getHp());
        QList<int> available = target->getPile("nosbuqu"), selected;
        const int remove = qMax(0, int(available.size()) - retain);
        if (retain == 0) selected = available;
        else for (int i = 0; i < remove && !available.isEmpty(); ++i) {
            room->fillAG(available, target);
            auto clear = qScopeGuard([&] { room->clearAG(target); });
            const int id = room->askForAG(target, available, false, "nosbuqu");
            room->clearAG(target); clear.dismiss();
            if (!available.removeOne(id) || !target->getPile("nosbuqu").contains(id)) break;
            selected << id;
        }
        QList<int> current;
        for (int id : selected) if (target->getPile("nosbuqu").contains(id)) current << id;
        if (!current.isEmpty()) {
            LogMessage log; log.type = "$NosBuquRemove"; log.from = target; log.card_str = ListI2S(current).join("+"); room->sendLog(log);
            room->throwCard(current, "nosbuqu", nullptr);
        }
        return false;
    }
};

class NosBuqu : public TriggerSkillV2
{
public:
    NosBuqu() : TriggerSkillV2("nosbuqu") { events << HpChanged << AskForPeachesDone; }
    bool usesEventPriority() const override { return true; }
    int getPriority(TriggerEvent event) const override { return event == HpChanged ? 1 : TriggerSkillV2::getPriority(event); }
    TriggerList triggerable(TriggerEvent event, Room *, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->isAlive() || !player->hasSkill(objectName()) || player->getHp() > 0) return {};
        if (event == HpChanged && data.canConvert<RecoverStruct>()) return {};
        if (event == AskForPeachesDone && data.value<DyingStruct>().who != player) return {};
        return {{player, {objectName()}}};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = {ctx.owner};
        return event == AskForPeachesDone || room->askForSkillInvoke(ctx.owner, objectName(), *ctx.original_data);
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        const int need = qMax(0, getEffectiveAmount(ctx) - target->getHp());
        if (event == HpChanged) {
            room->broadcastSkillInvoke(objectName(), -1, target);
            const int add = qMax(0, need - int(target->getPile("nosbuqu").size()));
            if (add > 0) target->addToPile("nosbuqu", room->getNCards(add));
        }
        const QList<int> cards = target->getPile("nosbuqu");
        if (cards.size() < need) return false;
        QMap<int, QList<int>> numbers;
        for (int id : cards) numbers[Sanguosha->getCard(id)->getNumber()] << id;
        QList<int> duplicates;
        for (const QList<int> &group : numbers) if (group.size() > 1) duplicates << group;
        if (!duplicates.isEmpty()) {
            if (event == HpChanged) {
                LogMessage log; log.type = "$NosBuquDuplicateItem"; log.from = target; log.card_str = ListI2S(duplicates).join("+"); room->sendLog(log);
            }
            return false;
        }
        // Survival requires the complete physical pile, not an empty-pile success after declining activation.
        if (event == AskForPeachesDone) room->setPlayerFlag(target, "-Global_Dying");
        return true;
    }
};
class NosBuquClear : public DetachEffectSkill
{
public:
    NosBuquClear() : DetachEffectSkill("nosbuqu")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        if (player->getHp() <= 0)
            room->enterDying(player, nullptr);
    }
};

NosGuhuoCard::NosGuhuoCard()
{
    setSkillName("nosguhuo");
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool NosGuhuoCard::nosguhuo(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    QList<ServerPlayer *> questioned;

    room->setTag("NosGuhuoType", user_string);

    foreach (ServerPlayer *player, room->getOtherPlayers(yuji)) {
        QString choice = "question+noquestion";
        if (player->getHp() <= 0) {
            LogMessage log;
            log.type = "#GuhuoCannotQuestion";
            log.from = player;
            log.arg = QString::number(player->getHp());
            room->sendLog(log);
			choice = "noquestion";
        }

        choice = room->askForChoice(player, "nosguhuo", choice, yuji->objectName()+":"+user_string);
        if (choice == "question") {
            room->setEmotion(player, "question");
            questioned << player;
        } else
            room->setEmotion(player, "no-question");

        LogMessage log;
        log.type = "#GuhuoQuery";
        log.from = player;
        log.arg = choice;
        room->sendLog(log);
    }

    LogMessage log;
    log.type = "$GuhuoResult";
    log.from = yuji;
    log.card_str = QString::number(subcards.first());
    room->sendLog(log);
	foreach(ServerPlayer *player, room->getAlivePlayers())
		room->setEmotion(player, ".");

    bool success = questioned.isEmpty();
    if (!success) {
        const Card *card = Sanguosha->getCard(subcards.first());
        bool real = card->match(user_string);
        if (user_string == "peach+analeptic")
            real = card->objectName() == yuji->getTag("NosGuhuoSaveSelf").toString();
        else if (user_string == "slash")
            real = card->objectName().contains("slash");
        else if (user_string == "normal_slash")
            real = card->objectName() == "slash";

        success = real && card->getSuit() == Card::Heart;
        if (!success) {
            room->moveCardTo(this, yuji, nullptr, Player::DiscardPile,
                CardMoveReason(CardMoveReason::S_REASON_PUT, yuji->objectName(), "", "nosguhuo"), true);
        }
        foreach (ServerPlayer *player, questioned) {
			if (real) room->loseHp(HpLostStruct(player, 1, "nosguhuo", yuji));
            else player->drawCards(1, "nosguhuo");
        }
    }
    if (success) {
        CardMoveReason reason(CardMoveReason::S_REASON_USE, yuji->objectName(), "", "nosguhuo");
        CardsMoveStruct move(getSubcards(), yuji, nullptr, Player::PlaceUnknown, Player::PlaceTable, reason);
        room->moveCardsAtomic(move, true);
    }
    yuji->removeTag("NosGuhuoSaveSelf");
    yuji->removeTag("NosGuhuoSlash");
    return success;
}

bool NosGuhuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFilter(targets, to_select, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("nosguhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFilter(targets, to_select, Self);
}

bool NosGuhuoCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("nosguhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetFixed();
}

bool NosGuhuoCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        Card *card = Sanguosha->cloneCard(user_string.split("+").first());
        if (card){
			card->setCanRecast(false);
			card->deleteLater();
		}
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    // The server has no engine Self or dialog tag; fall back to the declared card.
    const Card *_card = Self ? Self->getTag("nosguhuo").value<const Card *>() : nullptr;
    Card *card = _card ? Sanguosha->cloneCard(_card) : Sanguosha->cloneCard(user_string.split("+").first());
    if (card == nullptr)
        return false;
    card->setCanRecast(false);
    card->deleteLater();
    return card->targetsFeasible(targets, Self);
}

const Card *NosGuhuoCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *yuji = card_use.from;
    Room *room = yuji->getRoom();

    QString to_nosguhuo = user_string;
    if ((user_string.contains("slash") || (user_string.contains("Slash")))
        && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList nosguhuo_list;
		static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            nosguhuo_list << name;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "slash";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_slash", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSlash", QVariant(to_nosguhuo));
    }
    room->broadcastSkillInvoke("nosguhuo");

    LogMessage log;
    log.type = card_use.to.isEmpty() ? "#GuhuoNoTarget" : "#Guhuo";
    log.from = yuji;
    log.to = card_use.to;
    log.arg = to_nosguhuo;
    log.arg2 = "nosguhuo";

    room->sendLog(log);

    if (nosguhuo(card_use.from)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_nosguhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_nosguhuo = card->objectName();
		} else if (to_nosguhuo == "normal_slash")
			to_nosguhuo = "slash";
		if (to_nosguhuo.startsWith(card->objectName()))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_nosguhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

const Card *NosGuhuoCard::validateInResponse(ServerPlayer *yuji) const
{
    Room *room = yuji->getRoom();
    room->broadcastSkillInvoke("nosguhuo");

    QString to_nosguhuo;
    if (user_string == "peach+analeptic") {
        QStringList nosguhuo_list;
        static QList<const Peach *> peachs = Sanguosha->findChildren<const Peach *>();
        foreach (const Peach *peach, peachs) {
            QString name = peach->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(peach->getPackage())) continue;
            nosguhuo_list << name;
            break;
        }
        static QList<const Analeptic *> anas = Sanguosha->findChildren<const Analeptic *>();
        foreach (const Analeptic *ana, anas) {
            QString name = ana->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(ana->getPackage())) continue;
            nosguhuo_list << name;
            break;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "peach";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_saveself", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSaveSelf", QVariant(to_nosguhuo));
    } else if (user_string.contains("slash") || user_string.contains("Slash")) {
        QStringList nosguhuo_list;
        static QList<const Slash *> slashs = Sanguosha->findChildren<const Slash *>();
        foreach (const Slash *slash, slashs) {
            QString name = slash->objectName();
            if (nosguhuo_list.contains(name) || ServerInfo.BanPackages.contains(slash->getPackage())) continue;
            nosguhuo_list << name;
        }

        if (nosguhuo_list.isEmpty())
            nosguhuo_list << "slash";
        to_nosguhuo = room->askForChoice(yuji, "nosguhuo_slash", nosguhuo_list.join("+"));
        yuji->setTag("NosGuhuoSlash", QVariant(to_nosguhuo));
    } else
        to_nosguhuo = user_string;

    LogMessage log;
    log.type = "#GuhuoNoTarget";
    log.from = yuji;
    log.arg = to_nosguhuo;
    log.arg2 = "nosguhuo";
    room->sendLog(log);

    if (nosguhuo(yuji)) {
        Card *card = Sanguosha->getCard(subcards.first());
		if (to_nosguhuo == "slash") {
			if (card->isKindOf("Slash"))
				to_nosguhuo = card->objectName();
		} else if (to_nosguhuo == "normal_slash")
			to_nosguhuo = "slash";
		if (to_nosguhuo.startsWith(card->objectName()))
			return card;
		else{
			Card *use_card = Sanguosha->cloneCard(to_nosguhuo, card->getSuit(), card->getNumber());
			use_card->setSkillName("guhuo");
			use_card->addSubcard(subcards.first());
			use_card->deleteLater();
			return use_card;
		}
    }
	return nullptr;
}

class NosGuhuo : public Guhuo
{
public:
    NosGuhuo() : Guhuo("nosguhuo") {}
    SkillDialogInfo getDialogInfo() const override { return SkillDialogInfo::guhuo(objectName(), true, true, true, false, false, false); }
};
class Wushen : public FilterSkill
{
public:
    Wushen() : FilterSkill("wushen")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Heart
		&&Sanguosha->getCardPlace(to_select->getId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class WushenTargetMod : public TargetModSkillV2
{
public:
    WushenTargetMod() : TargetModSkillV2("#wushen-target")
    {
        setBaseAmount(1000);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        return ctx.modType == DistanceLimit && ctx.card && ctx.card->getSuit() == Card::Heart
            ? CorrectSkillResult::useAmount(ctx.currentAmount) : CorrectSkillResult::noEffect();
    }
};

class Wuhun : public TriggerSkillV2
{
public:
    Wuhun() : TriggerSkillV2("wuhun") { events << DamageDone << Death; frequency = Compulsory; }
    TriggerList triggerable(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const override
    {
        if (!player || !player->hasSkill(objectName())) return {};
        if (event == DamageDone) {
            const DamageStruct damage = data.value<DamageStruct>();
            return damage.to == player && damage.from && damage.from != player && damage.from->isAlive()
                ? TriggerList{{player, {objectName()}}} : TriggerList();
        }
        if (data.value<DeathStruct>().who != player) return {};
        for (ServerPlayer *other : room->getOtherPlayers(player))
            if (other->getMark("&nightmare+#" + player->objectName()) > 0) return {{player, {objectName()}}};
        return {};
    }
    bool cost(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        if (event == DamageDone) { ctx.targets = {ctx.original_data->value<DamageStruct>().from}; return true; }
        int maximum = 0; QList<ServerPlayer *> foes;
        for (ServerPlayer *other : room->getOtherPlayers(ctx.owner)) {
            const int mark = other->getMark("&nightmare+#" + ctx.owner->objectName());
            if (mark > maximum) { maximum = mark; foes.clear(); }
            if (mark == maximum && mark > 0) foes << other;
        }
        if (foes.isEmpty()) return false;
        ServerPlayer *foe = room->askForPlayerChosen(ctx.owner, foes, objectName(), "@wuhun-revenge");
        if (!foe) return false;
        ctx.targets = {foe}; ctx.extra_data = maximum; ctx.manual_effect = true; return true;
    }
    bool effect(TriggerEvent event, Room *room, ServerPlayer *player, SkillContext &ctx) const override
    {
        if (event != Death) return false;
        const QString mark = "&nightmare+#" + ctx.owner->objectName();
        // Revenge consumes the shared nightmare resource even if its recipient intercepts it.
        const auto cleanup = qScopeGuard([&] { for (ServerPlayer *other : room->getAllPlayers()) other->loseAllMarks(mark); });
        if (!ctx.targets.isEmpty()) skillEffect(event, room, player, ctx, ctx.targets.first());
        return false;
    }
    bool effectTarget(TriggerEvent event, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->sendCompulsoryTriggerLog(ctx.owner, objectName());
        if (event == DamageDone) {
            room->broadcastSkillInvoke(objectName(), 1);
            target->gainMark("&nightmare+#" + ctx.owner->objectName(), ctx.original_data->value<DamageStruct>().damage * getEffectiveAmount(ctx));
            return false;
        }
        JudgeStruct judge; judge.pattern = "Peach,GodSalvation"; judge.good = true; judge.negative = true; judge.reason = objectName(); judge.who = target;
        room->judge(judge);
        room->broadcastSkillInvoke(objectName(), judge.isBad() ? 2 : 3);
        if (judge.isBad() && target->isAlive()) {
            room->doSuperLightbox(ctx.owner, objectName());
            LogMessage log; log.type = "#WuhunRevenge"; log.from = ctx.owner; log.to << target;
            log.arg = QString::number(ctx.extra_data.toInt()); log.arg2 = objectName(); room->sendLog(log);
            room->killPlayer(target);
        }
        return false;
    }
};
static bool CompareBySuit(int card1, int card2)
{
    const Card *c1 = Sanguosha->getCard(card1);
    const Card *c2 = Sanguosha->getCard(card2);

    int a = static_cast<int>(c1->getSuit());
    int b = static_cast<int>(c2->getSuit());

    return a < b;
}

class OLWushen : public FilterSkill
{
public:
    OLWushen() : FilterSkill("olwushen")
    {
    }

    bool viewFilter(const Card *to_select) const
    {
        return to_select->getSuit() == Card::Heart
		&& Sanguosha->getCardPlace(to_select->getId()) == Player::PlaceHand;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
        slash->setSkillName(objectName());/*
        WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
        card->takeOver(slash);*/
        return slash;
    }
};

class OLWushenTargetMod : public TargetModSkillV2
{
public:
    OLWushenTargetMod() : TargetModSkillV2("#olwushen-target")
    {
        setBaseAmount(1000);
    }

    CorrectSkillResult getCorrection(const CorrectSkillContext &ctx) const override
    {
        if (!ctx.card || ctx.card->getSuit() != Card::Heart) return CorrectSkillResult::noEffect();
        // Unlimited use is a distinct result, not an arbitrary large quota.
        if (ctx.modType == Residue) return CorrectSkillResult::unlimitedResidue();
        return ctx.modType == DistanceLimit ? CorrectSkillResult::useAmount(ctx.currentAmount)
                                           : CorrectSkillResult::noEffect();
    }
};

class OLWushenSlash : public TriggerSkillV2
{
public:
    OLWushenSlash() : TriggerSkillV2("#olwushen-slash")
    { events << TargetSpecified; frequency = Compulsory; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const override
    {
        const CardUseStruct use = data.value<CardUseStruct>();
        return player && player->isAlive() && player->hasSkill("olwushen") && use.from == player
            && use.card && use.card->isKindOf("Slash") && use.card->getSuit() == Card::Heart && !use.to.isEmpty()
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        ctx.targets = ctx.original_data->value<CardUseStruct>().to;
        return !ctx.targets.isEmpty();
    }

    bool effect(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx) const override
    {
        LogMessage log; log.type = "#OLwushenSlash"; log.from = ctx.owner; log.arg = "olwushen";
        room->sendLog(log);
        return false;
    }

    bool effectTarget(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        CardUseStruct use = ctx.original_data->value<CardUseStruct>();
        // Per-recipient entries allow a target interceptor to cancel only its own no-response effect.
        if (use.to.contains(target) && !use.no_respond_list.contains(target->objectName()))
            use.no_respond_list << target->objectName();
        *ctx.original_data = QVariant::fromValue(use);
        return false;
    }
};
class Shelie : public TriggerSkillV2
{
public:
    Shelie() : TriggerSkillV2("shelie") { events << EventPhaseStart; m_baseAmount = 5; }

    TriggerList triggerable(TriggerEvent, Room *, ServerPlayer *player, QVariant &) const override
    {
        return player && player->isAlive() && player->hasSkill(objectName()) && player->getPhase() == Player::Draw
            ? TriggerList{{player, {objectName()}}} : TriggerList();
    }

    bool cost(TriggerEvent, Room *, ServerPlayer *, SkillContext &ctx) const override
    {
        if (!ctx.owner->askForSkillInvoke(this)) return false;
        ctx.targets = {ctx.owner};
        return true;
    }

    bool effectTarget(TriggerEvent, Room *room, ServerPlayer *, SkillContext &ctx, ServerPlayer *target) const override
    {
        room->broadcastSkillInvoke(objectName());
        QList<int> ids = room->getNCards(qMax(0, getEffectiveAmount(ctx)), false);
        std::sort(ids.begin(), ids.end(), CompareBySuit);
        if (ids.isEmpty()) return true;
        room->moveCardsAtomic(CardsMoveStruct(ids, nullptr, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_TURNOVER, ctx.owner->objectName(), objectName(), "")), true);
        QList<int> candidates;
        for (int id : ids)
            if (room->getCardPlace(id) == Player::PlaceTable) candidates << id;
        room->fillAG(candidates);
        auto clear = qScopeGuard([room] { room->clearAG(); });
        QList<int> obtained;
        while (!candidates.isEmpty() && target->isAlive()) {
            const int selected = room->askForAG(target, candidates, false, objectName());
            if (!candidates.contains(selected)) break;
            candidates.removeOne(selected);
            if (room->getCardPlace(selected) != Player::PlaceTable) continue;
            obtained << selected;
            const Card::Suit suit = Sanguosha->getCard(selected)->getSuit();
            room->takeAG(target, selected, false);
            const QList<int> remaining = candidates;
            for (int id : remaining) {
                if (Sanguosha->getCard(id)->getSuit() != suit) continue;
                candidates.removeOne(id);
                room->takeAG(nullptr, id, false);
            }
        }
        room->getThread()->delay();
        room->clearAG();
        clear.dismiss();
        // AG callbacks can move cards. Only currently revealed material belongs to this resolution.
        for (int i = obtained.size() - 1; i >= 0; --i)
            if (room->getCardPlace(obtained.at(i)) != Player::PlaceTable) obtained.removeAt(i);
        if (target->isAlive() && !obtained.isEmpty()) {
            DummyCard cards(obtained);
            room->obtainCard(target, &cards);
        }
        QList<int> discarded;
        for (int id : ids)
            if (room->getCardPlace(id) == Player::PlaceTable) discarded << id;
        if (!discarded.isEmpty()) {
            DummyCard cards(discarded);
            room->throwCard(&cards, CardMoveReason(CardMoveReason::S_REASON_NATURAL_ENTER,
                ctx.owner->objectName(), objectName(), ""), nullptr);
        }
        return true;
    }
};
GongxinCard::GongxinCard()
{
    setSkillName("gongxin");
}

bool GongxinCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self;
}

void GongxinCard::onEffect(CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (!effect.to->isKongcheng()) {
        QList<int> ids;
        foreach (const Card *card, effect.to->getHandcards()) {
            if (card->getSuit() == Card::Heart)
                ids << card->getEffectiveId();
        }

        int card_id = room->doGongxin(effect.from, effect.to, ids);
        if (card_id == -1) return;

        QString result = room->askForChoice(effect.from, "gongxin", "discard+put");
        effect.from->removeTag("gongxin");
        if (result == "discard") {
            CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE, effect.from->objectName(), "", "gongxin", "");
            room->throwCard(Sanguosha->getCard(card_id), reason, effect.to, effect.from);
        } else {
            effect.from->setFlags("Global_GongxinOperator");
            CardMoveReason reason(CardMoveReason::S_REASON_PUT, effect.from->objectName(), "", "gongxin", "");
            room->moveCardTo(Sanguosha->getCard(card_id), effect.to, nullptr, Player::DrawPile, reason, true);
            effect.from->setFlags("-Global_GongxinOperator");
        }
    }
}

class Gongxin : public ViewAsSkillV2
{
public:
    Gongxin() : ViewAsSkillV2("gongxin")
    { setPhaseName("Play");
    }

    LimitScope getLimitScope() const override { return Limit_Phase; }
    QString historyKey(const ActiveSkillRequest &) const override { return "GongxinCard"; }
    TargetMode targetMode() const override { return SelectTargets; }

    bool canActivate(const ActiveSkillRequest &request) const override
    {
        return request.initiator && request.reason == CardUseStruct::CARD_USE_REASON_PLAY;
    }

    bool canSelectTarget(const ActiveSkillRequest &request, const QList<const Player *> &selected,
                         const Player *candidate) const override
    {
        return candidate && candidate->isAlive() && candidate != request.initiator && selected.isEmpty();
    }

    bool targetsFeasible(const ActiveSkillRequest &request, const QList<const Player *> &selected) const override
    {
        return selected.size() == 1 && canSelectTarget(request, {}, selected.first());
    }

    EffectFlow effectOnTarget(SkillContext &ctx, ServerPlayer *target) const override
    {
        if (target->isKongcheng()) return ContinueEffects;
        Room *room = ctx.invoker->getRoom();
        QList<int> hearts;
        for (const Card *card : target->getHandcards())
            if (card->getSuit() == Card::Heart) hearts << card->getEffectiveId();
        const QVariant priorChoice = ctx.invoker->getTag("gongxin");
        const auto clearChoice = qScopeGuard([&] {
            if (priorChoice.isValid()) ctx.invoker->setTag("gongxin", priorChoice);
            else ctx.invoker->removeTag("gongxin");
        });
        const int id = room->doGongxin(ctx.invoker, target, hearts);
        if (id < 0 || !hearts.contains(id) || !target->handCards().contains(id)) return ContinueEffects;
        const QString choice = room->askForChoice(ctx.invoker, objectName(), "discard+put");
        if (!target->handCards().contains(id)) return ContinueEffects;
        if (choice == "discard") {
            const CardMoveReason reason(CardMoveReason::S_REASON_DISMANTLE,
                ctx.invoker->objectName(), "", objectName(), "");
            room->throwCard(Sanguosha->getCard(id), reason, target, ctx.invoker);
        } else {
            // This UI projection is scoped even when a nested move breaks the turn.
            const bool prior = ctx.invoker->hasFlag("Global_GongxinOperator");
            ctx.invoker->setFlags("Global_GongxinOperator");
            const auto clearFlag = qScopeGuard([&] {
                if (!prior) ctx.invoker->setFlags("-Global_GongxinOperator");
            });
            const CardMoveReason reason(CardMoveReason::S_REASON_PUT,
                ctx.invoker->objectName(), "", objectName(), "");
            room->moveCardTo(Sanguosha->getCard(id), target, nullptr, Player::DrawPile, reason, true);
        }
        return ContinueEffects;
    }

    int getEffectIndex(const ServerPlayer *player, const Card *) const
    {
        int index = qsanRandomBounded(2) + 1;
        if (!player->hasInnateSkill(this))
            index += 2;
        return index;
    }
};

WindPackage::WindPackage()
    :Package("wind")
{
    General *xiahouyuan = new General(this, "xiahouyuan", "wei"); // WEI 008
    xiahouyuan->addSkill(new Shensu);
    xiahouyuan->addSkill(new ShensuTargetMod);
    related_skills.insert("shensu", "#shensu-slash-ndl");

    General *caoren = new General(this, "caoren", "wei"); // WEI 011
    caoren->addSkill(new Jushou);
    caoren->addSkill(new Jiewei);

    General *huangzhong = new General(this, "huangzhong", "shu"); // SHU 008
    huangzhong->addSkill(new Liegong);

    General *weiyan = new General(this, "weiyan", "shu"); // SHU 009
    weiyan->addSkill(new Kuanggu);
    weiyan->addSkill(new KuangguRecord);
    related_skills.insert("kuanggu", "#kuanggu-record");

    General *xiaoqiao = new General(this, "xiaoqiao", "wu", 3, false); // WU 011
    xiaoqiao->addSkill(new Tianxiang);
    xiaoqiao->addSkill(new TianxiangDraw);
    xiaoqiao->addSkill(new Hongyan);
    related_skills.insert("tianxiang", "#tianxiang");

    General *zhoutai = new General(this, "zhoutai", "wu"); // WU 013
    zhoutai->addSkill(new Buqu);
    zhoutai->addSkill(new BuquMaxCards);
    zhoutai->addSkill(new Fenji);
    related_skills.insert("buqu", "#buqu");

    General *zhangjiao = new General(this, "zhangjiao$", "qun", 3); // QUN 010
    zhangjiao->addSkill(new Leiji);
    zhangjiao->addSkill(new LeijiRecover);
    related_skills.insert("leiji", "#leiji-recover");
    zhangjiao->addSkill(new Guidao);
    zhangjiao->addSkill(new Huangtian);

    General *yuji = new General(this, "yuji", "qun", 3); // QUN 011
    yuji->addSkill(new Guhuo);
    yuji->addSkill(new GuhuoPaymentCleanup);
    related_skills.insert("guhuo", "#guhuo-payment");
    related_skills.insert("nosguhuo", "#guhuo-payment");
    yuji->addRelateSkill("chanyuan");

    General *shenguanyu = new General(this, "shenguanyu", "god", 5); // LE 001
    shenguanyu->addSkill(new Wushen);
    shenguanyu->addSkill(new WushenTargetMod);
    shenguanyu->addSkill(new Wuhun);
    related_skills.insert("wushen", "#wushen-target");

    General *shenlvmeng = new General(this, "shenlvmeng", "god", 3); // LE 002
    shenlvmeng->addSkill(new Shelie);
    shenlvmeng->addSkill(new Gongxin);
    addMetaObject<GongxinCard>();


    addMetaObject<ShensuCard>();
    addMetaObject<TianxiangCard>();
    addMetaObject<HuangtianCard>();
    addMetaObject<GuhuoCard>();

    skills << new HuangtianViewAsSkill << new Chanyuan << new ChanyuanInvalidity;
    related_skills.insert("chanyuan", "#chanyuan-inv");
}
ADD_PACKAGE(Wind)

NostalgiaWindPackage::NostalgiaWindPackage()
    : Package("nostal_wind")
{
    General *noscaoren = new General(this, "nos_caoren", "wei");
    noscaoren->addSkill(new NosJushou);

    General *nos_zhoutai = new General(this, "nos_zhoutai", "wu");
    nos_zhoutai->addSkill(new NosBuqu);
    nos_zhoutai->addSkill(new NosBuquRemove);
    nos_zhoutai->addSkill(new NosBuquClear);
    related_skills.insert("nosbuqu", "#nosbuqu-remove");
    related_skills.insert("nosbuqu", "#nosbuqu-clear");

    General *nos_zhangjiao = new General(this, "nos_zhangjiao$", "qun", 3);
    nos_zhangjiao->addSkill(new NosLeiji);
    nos_zhangjiao->addSkill("guidao");
    nos_zhangjiao->addSkill("huangtian");

    General *nos_yuji = new General(this, "nos_yuji", "qun", 3);
    nos_yuji->addSkill(new NosGuhuo);

    addMetaObject<NosGuhuoCard>();
}
ADD_PACKAGE(NostalgiaWind)

void MigrateToOLStWind(Package *pkg)
{
    General *ol_shenguanyu = new General(pkg, "ol_shenguanyu", "god", 5);
    ol_shenguanyu->addSkill(new OLWushen);
    ol_shenguanyu->addSkill(new OLWushenTargetMod);
    ol_shenguanyu->addSkill(new OLWushenSlash);
    ol_shenguanyu->addSkill("wuhun");
    pkg->insertRelatedSkills("olwushen", "#olwushen-target");
    pkg->insertRelatedSkills("olwushen", "#olwushen-slash");
}

void MigrateToTenyearStWind(Package *pkg)
{
    General *tenyear_zhangjiao = new General(pkg, "tenyear_zhangjiao$", "qun", 3);
    tenyear_zhangjiao->addSkill(new TenyearLeiji);
    tenyear_zhangjiao->addSkill("guidao");
    tenyear_zhangjiao->addSkill("huangtian");
}
