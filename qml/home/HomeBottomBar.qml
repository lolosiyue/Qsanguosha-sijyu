import QtQuick
import "."

Item {
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

    implicitWidth: 1440
    implicitHeight: 136
    clip: false

    BASlantedPanel {
        id: dockPlate

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        height: 82

        slant: -0.17
        cornerRadius: 10
        shadowBlur: 12
        shadowOffset: 5
        borderWidth: 1

        topColor: HomeTheme.baDockTop
        bottomColor: HomeTheme.baDockBottom
        borderColor: HomeTheme.baDockBorder
        shadowColor: HomeTheme.baDockShadow

        accentVisible: true
        accentColor: HomeTheme.baSky
        accentHeight: 2
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4

        spacing: 28

        HomeNavButton {
            id: homeBtn

            width: 160
            height: parent.height

            text: qsTranslate("HomeScene", "Home")
            iconSource: "qrc:/QSanguosha/Home/icons/home.svg"
            characterSource: homeController.navButtonImage("home")

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

            width: 160
            height: parent.height

            text: qsTranslate("HomeScene", "Generals")
            iconSource: "qrc:/QSanguosha/Home/icons/generals.svg"
            characterSource: homeController.navButtonImage("generals")

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

            width: 160
            height: parent.height

            text: qsTranslate("HomeScene", "Cards")
            iconSource: "qrc:/QSanguosha/Home/icons/cards.svg"
            characterSource: homeController.navButtonImage("cards")

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

            width: 160
            height: parent.height

            text: qsTranslate("HomeScene", "Replays")
            iconSource: "qrc:/QSanguosha/Home/icons/replays.svg"
            characterSource: homeController.navButtonImage("replays")

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

            width: 160
            height: parent.height

            text: qsTranslate("HomeScene", "Settings")
            iconSource: "qrc:/QSanguosha/Home/icons/settings.svg"
            characterSource: homeController.navButtonImage("settings")

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
