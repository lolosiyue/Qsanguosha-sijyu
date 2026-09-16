import QtQuick
import QtQuick.Window
import QtQuick.Controls
import "."

Item {
    id: shell
    property bool subPageOpen: false
    property bool pageLoading: false
    property alias pageHost: pages.contentItem
    property alias homeBtn: dock.homeBtn
    property alias generalsBtn: dock.generalsBtn
    property alias cardsBtn: dock.cardsBtn
    property alias replaysBtn: dock.replaysBtn
    property alias settingsBtn: dock.settingsBtn

    // Portrait rearranges the native home components, including their artwork and skin.
    HomePlayerInfo {
        id: heading
        z: 1
        width: Math.max(0, parent.width - logo.width - HomeTheme.compactGap)
        height: avatarSize
        avatarSize: 52
        compact: true
    }
    Image {
        id: logo
        z: 1
        anchors.right: parent.right
        width: Math.min(120, parent.width * 0.3)
        height: heading.height
        source: homeController.logoImage
        fillMode: Image.PreserveAspectFit
    }
    CharacterLayer {
        anchors.top: heading.bottom
        // The portrait is the scene, not a thumbnail above a stack of controls.
        anchors.bottom: navigation.bottom
        width: parent.width
        compact: true
        baseBottomMargin: -height * 0.06
        visible: !shell.subPageOpen
    }
    // Keep the same catalog instances and drafts alive across rotation.
    Flickable {
        id: pages
        anchors.top: heading.bottom
        anchors.topMargin: HomeTheme.compactGap
        anchors.bottom: navigation.top
        anchors.bottomMargin: HomeTheme.compactGap
        width: parent.width
        contentWidth: width
        contentHeight: height
        visible: shell.subPageOpen
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.horizontal: HomeScrollBar { }
    }
    Text {
        anchors.centerIn: pages
        visible: shell.subPageOpen && shell.pageLoading
        text: qsTr("載入中…")
        color: HomeTheme.btnSecondaryText
        font.pixelSize: HomeTheme.compactText
    }
    MainActionPanel {
        id: actions
        x: 8
        width: parent.width - 16
        height: implicitHeight
        anchors.bottom: navigation.top
        anchors.bottomMargin: 18
        visible: !shell.subPageOpen
        compact: true
        primaryOnLeft: Config.oneHandedness === 1
        onQuickJoinClicked: homeController.quickJoin()
        onJoinGameClicked: homeController.joinGame()
        onStartServerClicked: homeController.startServer()
    }
    HomeSideBar {
        id: tools
        x: Config.oneHandedness === 1 ? 8 : parent.width - width - 8
        width: 56
        anchors.top: heading.bottom
        anchors.topMargin: 20
        visible: !shell.subPageOpen
        compact: true
        portraitRail: true
        onSettingsClicked: homeController.openSettings()
        onAboutClicked: homeController.openAbout()
        onUpdateClicked: homeController.checkUpdates()
    }
    Flickable {
        id: navigation
        width: parent.width
        height: 108
        anchors.bottom: parent.bottom
        contentWidth: dock.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        HomeBottomBar {
            id: dock
            width: navigation.width
            y: 8
            height: navigation.height - 8
            compact: true
            onHomeClicked: homeController.openHome()
            onGeneralsClicked: homeController.openGenerals()
            onCardsClicked: homeController.openCards()
            onReplaysClicked: homeController.openReplays()
            onSettingsClicked: homeController.openSettings()
        }
        function reveal(item) {
            var point = item.mapToItem(navigation.contentItem, 0, 0)
            if (point.x < contentX) contentX = Math.max(0, point.x)
            else if (point.x + item.width > contentX + width)
                contentX = Math.min(contentWidth - width, point.x + item.width - width)
        }
        ScrollBar.horizontal: HomeScrollBar { }
    }
    Connections {
        target: homeController
        function onCurrentPageChanged() {
            dock.currentIndex = homeController.currentPage === "generals" ? 1
                                : homeController.currentPage === "cards" ? 2 : 0
        }
    }
    Connections {
        target: shell.Window.window
        function onActiveFocusItemChanged() {
            var item = shell.Window.window.activeFocusItem
            if (!item) return
            if ([dock.homeBtn, dock.generalsBtn, dock.cardsBtn, dock.replaysBtn,
                 dock.settingsBtn].indexOf(item) >= 0) navigation.reveal(item)
            if (!shell.subPageOpen) return
            var ancestor = item.parent
            while (ancestor && ancestor !== pages.contentItem) ancestor = ancestor.parent
            if (ancestor) {
                var point = item.mapToItem(pages.contentItem, 0, 0)
                if (point.x < pages.contentX) pages.contentX = Math.max(0, point.x)
                else if (point.x + item.width > pages.contentX + pages.width)
                    pages.contentX = Math.min(pages.contentWidth - pages.width,
                                              point.x + item.width - pages.width)
            }
        }
    }
}
