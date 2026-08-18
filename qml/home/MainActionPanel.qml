import QtQuick

Item {
    id: panel

    property alias quickJoinBtn: quickJoinBtn
    property alias joinGameBtn: joinGameBtn
    property alias startServerBtn: startServerBtn

    signal quickJoinClicked()
    signal joinGameClicked()
    signal startServerClicked()

    implicitWidth: 470
    implicitHeight: 264

    HomeMainButton {
        id: quickJoinBtn

        x: 0
        y: 0
        width: 470
        height: 80

        primary: true
        text: qsTr("快速加入")
        iconSource: "qrc:/QSanguosha/Home/icons/quick-join.svg"

        onClicked: panel.quickJoinClicked()

        KeyNavigation.tab: joinGameBtn
        KeyNavigation.backtab: startServerBtn
    }

    HomeMainButton {
        id: joinGameBtn

        x: 18
        y: 96
        width: 440
        height: 76

        text: qsTr("加入游戏")
        iconSource: "qrc:/QSanguosha/Home/icons/join-game.svg"

        onClicked: panel.joinGameClicked()

        KeyNavigation.tab: startServerBtn
        KeyNavigation.backtab: quickJoinBtn
    }

    HomeMainButton {
        id: startServerBtn

        x: 36
        y: 188
        width: 410
        height: 76

        text: qsTr("启动服务器")
        iconSource: "qrc:/QSanguosha/Home/icons/server.svg"

        onClicked: panel.startServerClicked()

        KeyNavigation.tab: quickJoinBtn
        KeyNavigation.backtab: joinGameBtn
    }
}
