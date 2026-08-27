#include "local-response-ui-probe.h"

#include "carditem.h"
#include "cardcontainer.h"
#include "client.h"
#include "dashboard.h"
#include "engine.h"
#include "photo.h"
#include "playercardbox.h"
#include "qsanbutton.h"
#include "roomscene.h"
#include "skill-instance-utils.h"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QJsonArray>
#include <QMetaEnum>
#include <QTextDocument>

namespace {

QString statusName(Client::Status status)
{
    const int index = Client::staticMetaObject.indexOfEnumerator("Status");
    const QMetaEnum statusEnum = Client::staticMetaObject.enumerator(index);
    const char *key = statusEnum.valueToKey(status);
    return key ? QString::fromLatin1(key) : QString::number(status);
}

QString useReasonName(CardUseStruct::CardUseReason reason)
{
    switch (reason) {
    case CardUseStruct::CARD_USE_REASON_PLAY:
        return QStringLiteral("play");
    case CardUseStruct::CARD_USE_REASON_RESPONSE:
        return QStringLiteral("response");
    case CardUseStruct::CARD_USE_REASON_RESPONSE_USE:
        return QStringLiteral("response_use");
    case CardUseStruct::CARD_USE_REASON_UNKNOWN:
    default:
        return QStringLiteral("unknown");
    }
}

QJsonObject buttonSnapshot(const QSanButton *button)
{
    QJsonObject result;
    result.insert(QStringLiteral("visible"), button && button->isVisible());
    result.insert(QStringLiteral("enabled"), button && button->isEnabled());
    return result;
}

void collectCardItems(QGraphicsItem *parent, QList<CardItem *> *result)
{
    if (!parent)
        return;
    for (QGraphicsItem *child : parent->childItems()) {
        if (CardItem *card = dynamic_cast<CardItem *>(child))
            result->append(card);
        collectCardItems(child, result);
    }
}

bool isDescendantOf(const QGraphicsItem *item, const QGraphicsItem *parent)
{
    for (const QGraphicsItem *current = item; current; current = current->parentItem()) {
        if (current == parent)
            return true;
    }
    return false;
}

} // namespace

LocalResponseUiProbe::LocalResponseUiProbe(Client *client, RoomScene *scene,
    const QMap<QString, int> &aliasToId)
    : m_client(client), m_scene(scene), m_aliasToId(aliasToId)
{
    for (auto it = aliasToId.cbegin(); it != aliasToId.cend(); ++it)
        m_idToAlias.insert(it.value(), it.key());
}

QJsonObject LocalResponseUiProbe::snapshot() const
{
    QJsonObject root;
    QJsonObject client;
    client.insert(QStringLiteral("status"), statusName(m_client->getStatus()));
    client.insert(QStringLiteral("pattern"), Sanguosha->getCurrentCardUsePattern());
    client.insert(QStringLiteral("use_reason"), useReasonName(Sanguosha->getCurrentCardUseReason()));
    client.insert(QStringLiteral("prompt_html"), m_client->getPromptDoc()->toHtml());
    client.insert(QStringLiteral("prompt_plain_text"), m_client->getPromptDoc()->toPlainText());
    root.insert(QStringLiteral("client"), client);

    QJsonObject buttons;
    buttons.insert(QStringLiteral("ok"), buttonSnapshot(m_scene->ok_button));
    buttons.insert(QStringLiteral("cancel"), buttonSnapshot(m_scene->cancel_button));
    buttons.insert(QStringLiteral("discard"), buttonSnapshot(m_scene->discard_button));
    root.insert(QStringLiteral("buttons"), buttons);

    QJsonArray cards;
    auto appendCard = [this, &cards](CardItem *item, const QString &place) {
        if (!item || !item->getCard())
            return;
        const int id = item->getCard()->getEffectiveId();
        QJsonObject card;
        card.insert(QStringLiteral("alias"), m_idToAlias.value(id));
        card.insert(QStringLiteral("card_id"), id);
        card.insert(QStringLiteral("object_name"), item->getCard()->objectName());
        card.insert(QStringLiteral("place"), place);
        card.insert(QStringLiteral("enabled"), item->isEnabled());
        card.insert(QStringLiteral("selected"), item->isSelected() || item->isMarked());
        card.insert(QStringLiteral("visible"), item->isVisible());
        cards.append(card);
    };
    for (CardItem *item : m_scene->dashboard->getHandCards())
        appendCard(item, QStringLiteral("hand"));
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i)
        appendCard(m_scene->dashboard->_m_equipCards[i], QStringLiteral("equip"));
    root.insert(QStringLiteral("cards"), cards);

    QJsonArray surfaceCards;
    auto appendSurfaceCard = [this, &surfaceCards](CardItem *item, const QString &surface) {
        if (!item || !item->getCard())
            return;
        const int id = item->getCard()->getEffectiveId();
        QJsonObject card;
        card.insert(QStringLiteral("alias"), m_idToAlias.value(id));
        card.insert(QStringLiteral("card_id"), id);
        card.insert(QStringLiteral("object_name"), item->getCard()->objectName());
        card.insert(QStringLiteral("surface"), surface);
        card.insert(QStringLiteral("enabled"), item->isEnabled());
        card.insert(QStringLiteral("selected"), item->isSelected() || item->isMarked());
        card.insert(QStringLiteral("visible"), item->isVisible());
        surfaceCards.append(card);
    };
    QList<CardItem *> playerBoxCards;
    collectCardItems(m_scene->m_playerCardBox, &playerBoxCards);
    for (CardItem *item : playerBoxCards)
        appendSurfaceCard(item, QStringLiteral("player_card_box"));
    for (CardItem *item : m_scene->card_container->getItems())
        appendSurfaceCard(item, QStringLiteral("card_container"));
    QList<CardItem *> guanxingCards;
    collectCardItems(m_scene->m_guanxingBox, &guanxingCards);
    for (CardItem *item : guanxingCards)
        appendSurfaceCard(item, QStringLiteral("guanxing"));
    root.insert(QStringLiteral("surface_cards"), surfaceCards);

    QJsonObject surfaces;
    surfaces.insert(QStringLiteral("player_card_box"), QJsonObject {
        { QStringLiteral("open"), m_scene->m_playerCardBox && m_scene->m_playerCardBox->isVisible() }
    });
    surfaces.insert(QStringLiteral("card_container"), QJsonObject {
        { QStringLiteral("open"), m_scene->card_container->isVisible() }
    });
    surfaces.insert(QStringLiteral("guanxing"), QJsonObject {
        { QStringLiteral("open"), m_scene->m_guanxingBox->isVisible() }
    });
    root.insert(QStringLiteral("surfaces"), surfaces);

    QJsonArray players;
    for (auto it = m_scene->name2photo.cbegin(); it != m_scene->name2photo.cend(); ++it) {
        Photo *photo = it.value();
        QJsonObject player;
        player.insert(QStringLiteral("object_name"), it.key());
        player.insert(QStringLiteral("enabled"), photo->isEnabled());
        player.insert(QStringLiteral("selected"), photo->isSelected());
        player.insert(QStringLiteral("visible"), photo->isVisible());
        if (const ClientPlayer *model = photo->getPlayer()) {
            player.insert(QStringLiteral("hp"), model->getHp());
            player.insert(QStringLiteral("max_hp"), model->getMaxHp());
        }
        players.append(player);
    }
    root.insert(QStringLiteral("players"), players);

    QJsonArray skills;
    for (QSanSkillButton *button : m_scene->m_skillButtons) {
        QString baseName;
        const int instanceId = SkillInstanceUtils::parseName(button->objectName(), baseName);
        QJsonObject skill;
        skill.insert(QStringLiteral("object_name"), baseName);
        skill.insert(QStringLiteral("instance_id"), instanceId);
        skill.insert(QStringLiteral("text"), button->_m_displayName.isEmpty()
            ? Sanguosha->translate(baseName) : button->_m_displayName);
        skill.insert(QStringLiteral("tooltip"), button->toolTip());
        skill.insert(QStringLiteral("visible"), button->isVisible());
        skill.insert(QStringLiteral("enabled"), button->isEnabled());
        skill.insert(QStringLiteral("selected"), button->isDown());
        skills.append(skill);
    }
    root.insert(QStringLiteral("skills"), skills);

    QJsonObject dialog;
    QDialog *choiceDialog = m_scene->m_choiceDialog;
    dialog.insert(QStringLiteral("open"), choiceDialog && choiceDialog->isVisible());
    dialog.insert(QStringLiteral("class_name"), choiceDialog
        ? QString::fromLatin1(choiceDialog->metaObject()->className()) : QString());
    dialog.insert(QStringLiteral("title"), choiceDialog ? choiceDialog->windowTitle() : QString());
    QJsonArray options;
    QJsonArray enabledOptions;
    QJsonArray disabledOptions;
    if (choiceDialog) {
        const QList<QAbstractButton *> optionButtons = choiceDialog->findChildren<QAbstractButton *>();
        for (QAbstractButton *button : optionButtons) {
            options.append(button->objectName());
            if (button->isEnabled())
                enabledOptions.append(button->objectName());
            else
                disabledOptions.append(button->objectName());
        }
    }
    dialog.insert(QStringLiteral("options"), options);
    dialog.insert(QStringLiteral("enabled_options"), enabledOptions);
    dialog.insert(QStringLiteral("disabled_options"), disabledOptions);
    root.insert(QStringLiteral("dialog"), dialog);
    root.insert(QStringLiteral("open_dialog_count"), openDialogCount());
    return root;
}

bool LocalResponseUiProbe::surfaceCardsSettled() const
{
    QList<CardItem *> items;
    collectCardItems(m_scene->m_playerCardBox, &items);
    items.append(m_scene->card_container->getItems());
    collectCardItems(m_scene->m_guanxingBox, &items);
    for (CardItem *item : items) {
        QAbstractAnimation *animation = item ? item->getCurrentAnimation(false) : nullptr;
        if (animation && animation->state() != QAbstractAnimation::Stopped)
            return false;
    }
    return true;
}

int LocalResponseUiProbe::openDialogCount() const
{
    int count = 0;
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        if (qobject_cast<QDialog *>(widget) && widget->isVisible())
            ++count;
    }
    return count;
}

CardItem *LocalResponseUiProbe::findCard(const QString &alias) const
{
    if (!m_aliasToId.contains(alias))
        return nullptr;
    const int id = m_aliasToId.value(alias);
    for (CardItem *item : m_scene->dashboard->getHandCards()) {
        if (item->getCard() && item->getCard()->getEffectiveId() == id)
            return item;
    }
    for (int i = 0; i < S_EQUIP_AREA_LENGTH; ++i) {
        CardItem *item = m_scene->dashboard->_m_equipCards[i];
        if (item && item->getCard() && item->getCard()->getEffectiveId() == id)
            return item;
    }
    return nullptr;
}

CardItem *LocalResponseUiProbe::findSurfaceCard(const QString &alias) const
{
    if (!m_aliasToId.contains(alias))
        return nullptr;
    const int id = m_aliasToId.value(alias);
    QList<CardItem *> items;
    collectCardItems(m_scene->m_playerCardBox, &items);
    items.append(m_scene->card_container->getItems());
    collectCardItems(m_scene->m_guanxingBox, &items);
    for (CardItem *item : items) {
        if (item && item->getCard() && item->getCard()->getEffectiveId() == id)
            return item;
    }
    return nullptr;
}

QSanSkillButton *LocalResponseUiProbe::findSkillButton(const QString &skillName) const
{
    for (QSanSkillButton *button : m_scene->m_skillButtons) {
        QString baseName;
        SkillInstanceUtils::parseName(button->objectName(), baseName);
        if (button->objectName() == skillName || baseName == skillName)
            return button;
    }
    return nullptr;
}

bool LocalResponseUiProbe::selectCard(const QString &alias, bool selected, QString *error)
{
    CardItem *item = findCard(alias);
    if (!item) {
        *error = QStringLiteral("card '%1' is not present").arg(alias);
        return false;
    }
    if (!item->isVisible() || !item->isEnabled()) {
        *error = QStringLiteral("card '%1' is not enabled and visible").arg(alias);
        return false;
    }
    const bool current = item->isSelected() || item->isMarked();
    if (current != selected)
        item->clickItem();
    return true;
}

bool LocalResponseUiProbe::activateSkill(const QString &skillName, bool active, QString *error)
{
    QSanSkillButton *button = findSkillButton(skillName);
    if (!button) {
        *error = QStringLiteral("skill button '%1' is not present").arg(skillName);
        return false;
    }
    if (!button->isVisible() || !button->isEnabled()) {
        *error = QStringLiteral("skill button '%1' is not enabled and visible").arg(skillName);
        return false;
    }
    if (button->isDown() != active)
        button->click();
    return true;
}

bool LocalResponseUiProbe::selectPlayer(const QString &playerName, bool selected, QString *error)
{
    Photo *photo = m_scene->name2photo.value(playerName, nullptr);
    if (!photo) {
        *error = QStringLiteral("player '%1' is not present").arg(playerName);
        return false;
    }
    if (!photo->isVisible() || !photo->isEnabled()) {
        *error = QStringLiteral("player '%1' is not enabled and visible").arg(playerName);
        return false;
    }
    if (photo->isSelected() != selected)
        photo->setSelected(selected);
    return true;
}

bool LocalResponseUiProbe::clickButton(const QString &name, QString *error)
{
    QSanButton *button = nullptr;
    if (name == QStringLiteral("ok"))
        button = m_scene->ok_button;
    else if (name == QStringLiteral("cancel"))
        button = m_scene->cancel_button;
    else if (name == QStringLiteral("discard"))
        button = m_scene->discard_button;
    if (!button || !button->isVisible() || !button->isEnabled()) {
        *error = QStringLiteral("button '%1' is not enabled and visible").arg(name);
        return false;
    }
    button->click();
    return true;
}

bool LocalResponseUiProbe::chooseOption(const QString &option, QString *error)
{
    QDialog *dialog = m_scene->m_choiceDialog;
    if (!dialog || !dialog->isVisible()) {
        *error = QStringLiteral("choice dialog is not open");
        return false;
    }
    const QList<QAbstractButton *> buttons = dialog->findChildren<QAbstractButton *>();
    for (QAbstractButton *button : buttons) {
        if (button->objectName() != option)
            continue;
        if (!button->isVisible() || !button->isEnabled()) {
            *error = QStringLiteral("option '%1' is not enabled and visible").arg(option);
            return false;
        }
        button->click();
        return true;
    }
    *error = QStringLiteral("option '%1' is not present").arg(option);
    return false;
}

bool LocalResponseUiProbe::chooseSurfaceCard(const QString &alias, QString *error)
{
    CardItem *item = findSurfaceCard(alias);
    if (!item || !item->isVisible() || !item->isEnabled()) {
        *error = QStringLiteral("surface card '%1' is not enabled and visible").arg(alias);
        return false;
    }

    const Client::Status status = static_cast<Client::Status>(
        m_client->getStatus() & Client::ClientStatusBasicMask);
    if (status == Client::ExecDialog
        && isDescendantOf(item, m_scene->m_playerCardBox)) {
        item->clickItem();
        return true;
    }
    if ((status == Client::AskForAG || status == Client::AskForGongxin)
        && isDescendantOf(item, m_scene->card_container)) {
        item->double_clicked();
        return true;
    }

    *error = QStringLiteral("surface card '%1' does not match client status").arg(alias);
    return false;
}

bool LocalResponseUiProbe::toggleGuanxingCard(const QString &alias, QString *error)
{
    CardItem *item = findSurfaceCard(alias);
    if (!item || !item->isVisible() || !item->isEnabled()
        || !isDescendantOf(item, m_scene->m_guanxingBox)
        || (m_client->getStatus() & Client::ClientStatusBasicMask) != Client::AskForGuanxing) {
        *error = QStringLiteral("guanxing card '%1' is not enabled and visible").arg(alias);
        return false;
    }
    item->clickItem();
    return true;
}
