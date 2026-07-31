import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: panel

    property alias quickJoinBtn: quickJoinBtn
    property alias joinGameBtn: joinGameBtn
    property alias startServerBtn: startServerBtn

    signal quickJoinClicked()
    signal joinGameClicked()
    signal startServerClicked()

    spacing: 16

    HomeMainButton {
        id: quickJoinBtn
        Layout.fillWidth: true

        primary: true
        text: qsTr("快速加入")
        iconSource: "qrc:/QSanguosha/Home/icons/quick-join.svg"

        onClicked: panel.quickJoinClicked()

        KeyNavigation.tab: joinGameBtn
        KeyNavigation.backtab: startServerBtn
    }

    HomeMainButton {
        id: joinGameBtn
        Layout.fillWidth: true

        text: qsTr("加入游戏")
        iconSource: "qrc:/QSanguosha/Home/icons/join-game.svg"

        onClicked: panel.joinGameClicked()

        KeyNavigation.tab: startServerBtn
        KeyNavigation.backtab: quickJoinBtn
    }

    HomeMainButton {
        id: startServerBtn
        Layout.fillWidth: true

        text: qsTr("启动服务器")
        iconSource: "qrc:/QSanguosha/Home/icons/server.svg"

        onClicked: panel.startServerClicked()

        KeyNavigation.tab: quickJoinBtn
        KeyNavigation.backtab: joinGameBtn
    }
}