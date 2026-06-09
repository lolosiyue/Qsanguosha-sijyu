#include "rolecombobox.h"
#include "engine.h"
#include "clientstruct.h"

RoleComboBoxItem::RoleComboBoxItem(const QString &role, int number, QSize size)
    : m_role(role), m_number(number), m_size(size)
{
    setRole(role);
    this->setFlag(QGraphicsItem::ItemIgnoresParentOpacity);
}

QString RoleComboBoxItem::getRole() const
{
    return m_role;
}

void RoleComboBoxItem::setRole(const QString &role, bool isBigKingdom)
{
    m_role = role;
    QString file = "image/system/roles/%1.png";
    if (isBigKingdom)
        file = "image/system/roles/big/%1.png";
    
    if (m_number != 0 && role != "unknown")
        load(QString(file).arg(m_role).arg(m_number), m_size, false);
    else
        load(QString(file).arg(m_role), m_size, false);
}

void RoleComboBoxItem::mousePressEvent(QGraphicsSceneMouseEvent *)
{
    emit clicked();
}

RoleComboBox::RoleComboBox(QGraphicsItem *parent)
    : QGraphicsObject(parent)
{
    int index = Sanguosha->getRoleIndex();
    QSize size(S_ROLE_COMBO_BOX_WIDTH, S_ROLE_COMBO_BOX_HEIGHT);
    m_currentRole = new RoleComboBoxItem("unknown", index, size);
    m_currentRole->setParentItem(this);
    connect(m_currentRole, SIGNAL(clicked()), this, SLOT(expand()));
    
    createRoleItems();
}

void RoleComboBox::createRoleItems()
{
    foreach(RoleComboBoxItem *item, items)
        delete item;
    items.clear();
    
    int index = Sanguosha->getRoleIndex();
    QSize size(S_ROLE_COMBO_BOX_WIDTH, S_ROLE_COMBO_BOX_HEIGHT);
    
    QString currentMode = ServerInfo.GameMode;
    bool lordShown = !ServerInfo.EnableHegemony;
    
    if (currentMode.isEmpty()) {
        QStringList allRoles = Sanguosha->getAllRegisteredRoles();
        foreach(QString roleName, allRoles) {
            if (roleName == "lord" && lordShown) continue;
            items << new RoleComboBoxItem(roleName, index, size);
        }
    } else {
        QString roleAbbrs = Sanguosha->getRolesSingle(currentMode);
        
        for (int i = 0; i < roleAbbrs.length(); i++) {
            QString abbr = QString(roleAbbrs[i]);
            QString roleName = Sanguosha->getRoleByAbbreviation(abbr);
            if (!roleName.isEmpty()) {
                if (roleName == "lord" && lordShown) continue;
                items << new RoleComboBoxItem(roleName, index, size);
            }
        }
    }
    
    for (int i = 0; i < items.length(); i++) {
        RoleComboBoxItem *item = items.at(i);
        item->setPos(0, (i + 1) * (S_ROLE_COMBO_BOX_HEIGHT + S_ROLE_COMBO_BOX_GAP));
        item->setZValue(1.0);
    }
    foreach (RoleComboBoxItem *item, items) {
        item->setParentItem(this);
        item->hide();
        connect(item, SIGNAL(clicked()), this, SLOT(collapse()));
    }
}

void RoleComboBox::paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *)
{
}

QRectF RoleComboBox::boundingRect() const
{
    if (items.isEmpty())
        return QRect(0, 0, 0, 0);
    else
        return items[0]->boundingRect();
}

void RoleComboBox::collapse()
{
    disconnect(m_currentRole, SIGNAL(clicked()), this, SLOT(collapse()));
    connect(m_currentRole, SIGNAL(clicked()), this, SLOT(expand()));
    RoleComboBoxItem *clicked_item = qobject_cast<RoleComboBoxItem *>(sender());
    foreach(RoleComboBoxItem *item, items) item->hide();
    m_currentRole->setRole(clicked_item->getRole(), false);
}

void RoleComboBox::expand()
{
    foreach(RoleComboBoxItem *item, items)
        item->show();
    m_currentRole->setRole("unknown", false);
    connect(m_currentRole, SIGNAL(clicked()), this, SLOT(collapse()));
}

void RoleComboBox::toggle()
{
    Q_ASSERT(!_m_fixedRole.isEmpty());
    if (!isEnabled()) return;
    QString displayed = m_currentRole->getRole();
    if (displayed == "unknown")
        m_currentRole->setRole(_m_fixedRole, _m_isBigKingdom);
    else
        m_currentRole->setRole("unknown", false);
}

void RoleComboBox::fix(const QString &role, bool isBigKingdom)
{
    _m_isBigKingdom = isBigKingdom;
    
    if (role == "unknown" && !_m_fixedRole.isEmpty()) {
        disconnect(m_currentRole, SIGNAL(clicked()), this, SLOT(toggle()));
        connect(m_currentRole, SIGNAL(clicked()), this, SLOT(expand()));
        
        if (items.isEmpty()) {
            createRoleItems();
        }
        
        m_currentRole->setRole("unknown", false);
        _m_fixedRole.clear();
        return;
    }
    
    if (_m_fixedRole.isEmpty()) {
        disconnect(m_currentRole, SIGNAL(clicked()), this, SLOT(expand()));
        connect(m_currentRole, SIGNAL(clicked()), this, SLOT(toggle()));
    }
    m_currentRole->setRole(role, isBigKingdom);
    _m_fixedRole = role;
    foreach(RoleComboBoxItem *item, items)
        delete item;
    items.clear();
}