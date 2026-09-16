import QtQuick

Item {
    id: panel

    property alias quickJoinBtn: quickJoinBtn
    property alias joinGameBtn: joinGameBtn
    property alias startServerBtn: startServerBtn
    property bool compact: false
    property bool primaryOnLeft: false
    readonly property real tileWidth: width * 0.44
    readonly property real primaryX: primaryOnLeft ? 0 : width - tileWidth
    readonly property real secondaryX: primaryOnLeft ? width - tileWidth : 0

    signal quickJoinClicked()
    signal joinGameClicked()
    signal startServerClicked()

    implicitWidth: 470
    implicitHeight: compact ? 184 : 264

    HomeMainButton {
        id: quickJoinBtn

        x: panel.compact ? panel.primaryX : 0
        y: panel.compact ? 96 : 0
        width: panel.compact ? panel.tileWidth : 470
        height: panel.compact ? 88 : 80
        compact: panel.compact
        tile: panel.compact

        primary: true
        leadingText: homeController.currentGameModeName
        text: qsTranslate("HomeScene", "Quick Join")
        iconSource: "qrc:/QSanguosha/Home/icons/quick-join.svg"

        onClicked: panel.quickJoinClicked()

        KeyNavigation.tab: joinGameBtn
        KeyNavigation.backtab: startServerBtn
    }

    HomeMainButton {
        id: joinGameBtn

        x: panel.compact ? panel.secondaryX : 18
        y: panel.compact ? 0 : 96
        width: panel.compact ? panel.tileWidth : 440
        height: panel.compact ? 84 : 76
        compact: panel.compact
        tile: panel.compact

        text: qsTranslate("HomeScene", "Join Game")
        iconSource: "qrc:/QSanguosha/Home/icons/join-game.svg"

        onClicked: panel.joinGameClicked()

        KeyNavigation.tab: startServerBtn
        KeyNavigation.backtab: quickJoinBtn
    }

    HomeMainButton {
        id: startServerBtn

        x: panel.compact ? panel.secondaryX : 36
        y: panel.compact ? 100 : 188
        width: panel.compact ? panel.tileWidth : 410
        height: panel.compact ? 84 : 76
        compact: panel.compact
        tile: panel.compact

        text: qsTranslate("HomeScene", "Start Server")
        iconSource: "qrc:/QSanguosha/Home/icons/server.svg"

        onClicked: panel.startServerClicked()

        KeyNavigation.tab: quickJoinBtn
        KeyNavigation.backtab: joinGameBtn
    }
}
