#ifndef LOCAL_RESPONSE_UI_PROBE_H
#define LOCAL_RESPONSE_UI_PROBE_H

#include <QJsonObject>
#include <QMap>

class CardItem;
class Client;
class QSanSkillButton;
class RoomScene;

class LocalResponseUiProbe
{
public:
    LocalResponseUiProbe(Client *client, RoomScene *scene,
        const QMap<QString, int> &aliasToId);

    QJsonObject snapshot() const;
    int openDialogCount() const;

    bool selectCard(const QString &alias, bool selected, QString *error);
    bool activateSkill(const QString &skillName, bool active, QString *error);
    bool selectPlayer(const QString &playerName, bool selected, QString *error);
    bool clickButton(const QString &name, QString *error);
    bool chooseOption(const QString &option, QString *error);

private:
    CardItem *findCard(const QString &alias) const;
    QSanSkillButton *findSkillButton(const QString &skillName) const;

    Client *m_client;
    RoomScene *m_scene;
    QMap<QString, int> m_aliasToId;
    QMap<int, QString> m_idToAlias;
};

#endif
