import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root

    property alias homeBtn: homeBtn
    property alias generalsBtn: generalsBtn
    property alias cardsBtn: cardsBtn
    property alias replaysBtn: replaysBtn
    property alias settingsBtn: settingsBtn

    property int currentIndex: 0

    signal homeClicked()
    signal generalsClicked()
    signal cardsClicked()
    signal replaysClicked()
    signal settingsClicked()

    implicitHeight: 116

    color: "#E6101428"
    radius: 22

    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: "#F0101428"
        }

        GradientStop {
            position: 1.0
            color: "#D0080D22"
        }
    }

    // 上方微亮邊線
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 22
        anchors.rightMargin: 22

        height: 1
        color: "#45A9CFFF"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        anchors.topMargin: 6
        anchors.bottomMargin: 5

        spacing: 4

        HomeNavButton {
            id: homeBtn

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: qsTr("首頁")
            iconSource: "qrc:/QSanguosha/Home/nav/home.png"

            active: root.currentIndex === 0

            onClicked: {
                root.currentIndex = 0
                root.homeClicked()
            }

            KeyNavigation.tab: generalsBtn
            KeyNavigation.backtab: settingsBtn
        }

        HomeNavButton {
            id: generalsBtn

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: qsTr("武將")
            iconSource: "qrc:/QSanguosha/Home/nav/generals.png"

            active: root.currentIndex === 1

            onClicked: {
                root.currentIndex = 1
                root.generalsClicked()
            }

            KeyNavigation.tab: cardsBtn
            KeyNavigation.backtab: homeBtn
        }

        HomeNavButton {
            id: cardsBtn

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: qsTr("卡牌")
            iconSource: "qrc:/QSanguosha/Home/nav/cards.png"

            active: root.currentIndex === 2

            onClicked: {
                root.currentIndex = 2
                root.cardsClicked()
            }

            KeyNavigation.tab: replaysBtn
            KeyNavigation.backtab: generalsBtn
        }

        HomeNavButton {
            id: replaysBtn

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: qsTr("錄像")
            iconSource: "qrc:/QSanguosha/Home/nav/replays.png"

            active: root.currentIndex === 3

            onClicked: {
                root.currentIndex = 3
                root.replaysClicked()
            }

            KeyNavigation.tab: settingsBtn
            KeyNavigation.backtab: cardsBtn
        }

        HomeNavButton {
            id: settingsBtn

            Layout.fillWidth: true
            Layout.fillHeight: true

            text: qsTr("設定")
            iconSource: "qrc:/QSanguosha/Home/nav/settings.png"

            active: root.currentIndex === 4

            onClicked: {
                root.currentIndex = 4
                root.settingsClicked()
            }

            KeyNavigation.tab: homeBtn
            KeyNavigation.backtab: replaysBtn
        }
    }
}
