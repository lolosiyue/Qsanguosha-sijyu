import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Window
import QSanguosha.HomeFx 1.0
import "."

Item {
    id: root

    focus: true

    Keys.onPressed: function(event) {
        if (event.accepted)
            return
        if (HomeTheme.tvMode && (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown)) {
            root.cycleTvTab(event.key === Qt.Key_PageDown ? 1 : -1)
            event.accepted = true
            return
        }
        if (HomeTheme.tvMode && root.subPageOpen
                && (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace)) {
            if (!root.isEditableTextFocused()) {
                homeController.openHome()
                event.accepted = true
            }
            return
        }
        if (root.subPageOpen)
            return
        switch (event.key) {
        case Qt.Key_1:
            homeController.quickJoin();
            event.accepted = true;
            break;
        case Qt.Key_2:
            homeController.joinGame();
            event.accepted = true;
            break;
        case Qt.Key_3:
            homeController.startServer();
            event.accepted = true;
            break;
        }
    }

    Shortcut {
        enabled: HomeTheme.tvMode
        autoRepeat: false
        sequence: "PgUp"
        onActivated: root.cycleTvTab(-1)
    }
    Shortcut {
        enabled: HomeTheme.tvMode
        autoRepeat: false
        sequence: "PgDown"
        onActivated: root.cycleTvTab(1)
    }

    property string visualMode: homeController ? homeController.visualMode : "normal"
    property real uiScale: 1.0
    readonly property bool compact: Config.responsiveUiEnabled
                                    && (width < 900 || height > width)
    readonly property var navBar: compact ? compactShell : bottomBar
    onCompactChanged: Qt.callLater(restoreHomeKeyboard)
    readonly property bool generalsOpen: homeController.currentPage === "generals"
    readonly property bool cardsOpen: homeController.currentPage === "cards"
    readonly property bool settingsOpen: homeController.currentPage === "settings"
    readonly property bool subPageOpen: generalsOpen || cardsOpen || settingsOpen
    property bool generalsMounted: false
    property bool cardsMounted: false
    property bool settingsMounted: false
    property bool tvKeepTabFocus: false
    property double tvTabCycleAt: 0
    readonly property bool generalPageBusy: {
        if (!generalsOpen)
            return false
        return generalPage.status !== Loader.Ready || generalPage.item === null
    }
    readonly property bool cardPageBusy: {
        if (!cardsOpen)
            return false
        return cardPage.status !== Loader.Ready || cardPage.item === null
    }
    readonly property bool cardsReadyForSmoke: cardsOpen && cardPage.status === Loader.Ready
                                                && cardPage.item !== null
                                                && cardPage.item.readyForSmoke
    readonly property int cardsModelCount: cardPage.item ? cardPage.item.modelCount : 0
    readonly property int cardsDetailCardId: cardPage.item ? cardPage.item.detailCardId : -1

    onGeneralsOpenChanged: {
        if (generalsOpen)
            generalsMounted = true
    }

    onCardsOpenChanged: {
        if (cardsOpen)
            cardsMounted = true
    }

    // The settings page and legacy dialog share SettingsSession: edits start on entry and revert if left unsaved.
    onSettingsOpenChanged: {
        if (settingsOpen) {
            settingsMounted = true
            settingsSession.begin()
        } else {
            settingsSession.revert()
        }
    }

    // Popups and tooltips use the window Overlay, so apply the same filter outside contentHost.
    // Hide the overlay layer when it has no popups to avoid an extra composition pass.
    readonly property Item popupOverlay: Overlay.overlay
    onPopupOverlayChanged: attachPopupOverlayEffect()
    function attachPopupOverlayEffect() {
        if (!popupOverlay)
            return
        popupOverlay.layer.effect = overlayVisualEffect
        popupOverlay.layer.enabled = Qt.binding(function() { return root.visualMode !== "normal" })
    }
    Component {
        id: overlayVisualEffect
        MultiEffect {
            autoPaddingEnabled: false
            saturation: root.visualMode === "grayscale" ? -1.0 : 0.0
            contrast: root.visualMode === "highcontrast" ? 0.35 : 0.0
        }
    }

    Item {
        id: contentHost
        anchors.fill: parent
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.topMargin: root.SafeArea.margins.top
        anchors.rightMargin: root.SafeArea.margins.right
        anchors.bottomMargin: root.SafeArea.margins.bottom
        clip: true
        // Apply the grayscale/high-contrast filter offscreen only when needed; Qt 6 saturation -1.0 removes color.
        layer.enabled: root.visualMode !== "normal"
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            saturation: root.visualMode === "grayscale" ? -1.0 : 0.0
            contrast: root.visualMode === "highcontrast" ? 0.35 : 0.0
        }

        HomeBackground {
            id: backgroundLayer
            anchors.fill: parent
        }

        // 1920x1080 design canvas fitted into the window. UIScale shrinks the canvas so
        // the layout reflows at the larger size; 1280x720 is the smallest canvas it fits.
        Item {
            id: uiCanvas
            visible: !root.compact

            anchors.centerIn: parent

            readonly property real zoom: Math.min(root.uiScale, 1.5)
            width: 1920 / zoom
            height: 1080 / zoom

            scale: Math.min(contentHost.width / width, contentHost.height / height)

            // Character artwork fills the height, enlarged and lowered behind the bottom dock.
            CharacterLayer {
                id: characterLayer

                visible: !root.subPageOpen

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.horizontalCenterOffset: -parent.width * 0.15

                width: Math.min(parent.width * 0.5, 1300)
            }

            // Only the bottom dock is constrained to the centered safe area.
            Item {
                id: safeArea

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter

                width: Math.min(parent.width, 1760)

                HomeBottomBar {
                    id: bottomBar

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 12

                    width: Math.min(1440, parent.width)
                    height: 136
                    opacity: 0

                    transform: Translate {
                        id: bottomEnter
                        y: 180
                    }

                    onHomeClicked: homeController.openHome()
                    onGeneralsClicked: homeController.openGenerals()
                    onCardsClicked: homeController.openCards()
                    onReplaysClicked: homeController.openReplays()
                    onSettingsClicked: homeController.openSettings()
                }
            }

            // Top-left player info: avatar and name, matching Quick Join.
            HomePlayerInfo {
                id: playerInfo

                visible: !root.subPageOpen

                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 32
                anchors.topMargin: 24
                opacity: 0

                transform: Translate {
                    id: playerEnter
                    x: -180
                }
            }

            // Place the logo above the three main buttons on the right.
            Image {
                id: logo

                visible: !root.subPageOpen

                anchors.right: actionPanel.right
                anchors.bottom: actionPanel.top
                anchors.rightMargin: 4
                anchors.bottomMargin: 20

                width: Math.min(parent.width * 0.12, 220)
                height: width * 0.58

                source: homeController.logoImage
                fillMode: Image.PreserveAspectFit
                mipmap: false
                opacity: 0

                transform: Translate {
                    id: logoEnter
                    y: -20
                }

                // Builds without the logo artwork show the game title instead.
                Text {
                    anchors.fill: parent
                    visible: logo.status !== Image.Ready
                    text: qsTranslate("MainWindow", "Sanguosha")
                    color: "#ffffff"
                    style: Text.Outline
                    styleColor: "#1565c0"
                    font.bold: true
                    font.pixelSize: 40
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 12
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            MainActionPanel {
                id: actionPanel

                visible: !root.subPageOpen

                anchors.right: sideBar.left
                anchors.rightMargin: 28
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -25

                width: Math.max(implicitWidth, Math.min(520, parent.width * 0.3))
                opacity: 0

                transform: Translate {
                    id: actionEnter
                    x: 250
                }

                onQuickJoinClicked: homeController.quickJoin()
                onJoinGameClicked: homeController.joinGame()
                onStartServerClicked: homeController.startServer()
                onScenarioWorksClicked: homeController.openScenarioWorks()
            }

            HomeSideBar {
                id: sideBar

                visible: !root.subPageOpen

                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                opacity: 0

                transform: Translate {
                    id: sideEnter
                    x: 150
                }

                onSettingsClicked: homeController.openSettings()
                onAboutClicked: homeController.openAbout()
                onUpdateClicked: homeController.checkUpdates()
            }

            Loader {
                id: generalPage
                parent: root.compact ? compactShell.pageHost : uiCanvas

                anchors.fill: parent
                // Sheared panel edges overhang their box; the compact host clips at its edges.
                anchors.leftMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.rightMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.bottomMargin: root.compact ? 0 : 148
                z: 40
                asynchronous: true
                active: root.generalsMounted
                source: "GeneralScene.qml"
                visible: root.generalsOpen && !root.generalPageBusy
                onStatusChanged: {
                    if (status === Loader.Ready && generalPage.item) {
                        if (root.generalsOpen) {
                            root.applyGeneralsNavGraph()
                            if (!root.tvKeepTabFocus)
                                generalPage.item.takeKeyboard()
                        }
                    }
                }
            }

            Binding {
                target: generalPage.item
                property: "compact"
                value: root.compact
                when: generalPage.item !== null
            }

            Connections {
                target: generalPage.item
                ignoreUnknownSignals: true
                function onNavigationEndpointChanged() {
                    if (root.generalsOpen) Qt.callLater(root.applyGeneralsNavGraph)
                }
            }

            // Show the panel skeleton during Loader compilation, then reveal GeneralScene and load portraits across frames.
            Item {
                id: generalPageSkeleton
                anchors.fill: generalPage
                z: 41
                visible: root.generalsOpen && root.generalPageBusy

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: HomeTheme.generalPageHMargin
                    anchors.rightMargin: HomeTheme.generalPageHMargin
                    anchors.topMargin: HomeTheme.generalPageTopMargin
                    anchors.bottomMargin: HomeTheme.generalPageBottomMargin
                    spacing: HomeTheme.generalPanelGap

                    BASlantedPanel {
                        width: parent.width
                        height: HomeTheme.generalHeaderHeight
                        slant: -0.08
                        cornerRadius: 10
                        shadowBlur: 0
                        shadowOffset: 0
                        topColor: HomeTheme.baDockTop
                        bottomColor: HomeTheme.baDockBottom
                        borderColor: HomeTheme.baDockBorder
                        shadowColor: HomeTheme.baDockShadow

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 28
                            anchors.rightMargin: 28
                            anchors.bottomMargin: 8
                            spacing: 18

                            Text {
                                text: homeController.qtTranslate("GeneralOverview", "General Overview")
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 28
                                font.bold: true
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.maximumWidth: 480
                                Layout.preferredHeight: 48
                                radius: 24
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Rectangle {
                                Layout.preferredWidth: 220
                                Layout.preferredHeight: 48
                                radius: 24
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Rectangle {
                                Layout.preferredWidth: 140
                                Layout.preferredHeight: 48
                                radius: 8
                                color: HomeTheme.btnSecondary
                                border.color: HomeTheme.btnSecondaryBorder
                            }

                            Text {
                                text: "0"
                                color: HomeTheme.btnSecondaryText
                                font.pixelSize: 22
                                font.bold: true
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: parent.height - HomeTheme.generalHeaderHeight - HomeTheme.generalPanelGap
                        spacing: HomeTheme.generalPanelGap

                        BASlantedPanel {
                            width: HomeTheme.generalPackageNavWidth
                            height: parent.height
                            slant: 0
                            cornerRadius: 10
                            shadowBlur: 0
                            shadowOffset: 0
                            topColor: HomeTheme.baDockTop
                            bottomColor: HomeTheme.baDockBottom
                            borderColor: HomeTheme.baDockBorder
                            shadowColor: HomeTheme.baDockShadow

                            Column {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 4
                                Repeater {
                                    model: 8
                                    SkeletonBlock {
                                        width: Math.round((HomeTheme.generalPackageNavWidth - 30) * 0.45)
                                        height: 44
                                        radius: 8
                                    }
                                }
                            }
                        }

                        BASlantedPanel {
                            id: skListPanel
                            width: Math.round((parent.width - HomeTheme.generalPanelGap) * HomeTheme.generalListShare)
                            height: parent.height
                            slant: 0
                            cornerRadius: 10
                            shadowBlur: 0
                            shadowOffset: 0
                            topColor: HomeTheme.baDockTop
                            bottomColor: HomeTheme.baDockBottom
                            borderColor: HomeTheme.baDockBorder
                            shadowColor: HomeTheme.baDockShadow

                            Item {
                                id: skGrid
                                anchors.fill: parent
                                anchors.margins: HomeTheme.generalGridMargin
                                // Read the saved column count before revealing the page to prevent the grid from shifting.
                                readonly property int savedCols: homeController.generalGridColumns()
                                readonly property bool tableMode: HomeTheme.resolvedGridColumns(width, savedCols)
                                                                  >= HomeTheme.generalGridMaxColumns
                                property int cellW: HomeTheme.generalCellWidth(width, savedCols)
                                property int cellH: HomeTheme.generalCellHeight(width, savedCols)
                                property int cols: HomeTheme.generalCellColumns(width, savedCols)

                                Column {
                                    visible: skGrid.tableMode
                                    anchors.fill: parent
                                    spacing: 4
                                    Repeater {
                                        model: skGrid.tableMode ? 12 : 0
                                        SkeletonBlock {
                                            width: skGrid.width
                                            height: HomeTheme.generalTableRowHeight
                                            radius: 4
                                        }
                                    }
                                }

                                Grid {
                                    visible: !skGrid.tableMode
                                    anchors.fill: parent
                                    columns: skGrid.cols

                                    Repeater {
                                        model: skGrid.tableMode ? 0 : skGrid.cols * 3
                                        Item {
                                            width: skGrid.cellW
                                            height: skGrid.cellH
                                            SkeletonBlock {
                                                anchors.fill: parent
                                                anchors.margins: HomeTheme.generalCellInset
                                                radius: 8
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        BASlantedPanel {
                            width: parent.width - HomeTheme.generalPackageNavWidth - skListPanel.width
                                   - HomeTheme.generalPanelGap * 2
                            height: parent.height
                            slant: 0
                            cornerRadius: 10
                            shadowBlur: 0
                            shadowOffset: 0
                            topColor: HomeTheme.baDockTop
                            bottomColor: HomeTheme.baDockBottom
                            borderColor: HomeTheme.baDockBorder
                            clip: true

                            Row {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                SkeletonBlock {
                                    width: Math.round(parent.width * 0.33)
                                    height: Math.min(parent.height - 28, Math.round(parent.width * 0.33 * 1.45))
                                    radius: 10
                                }

                                Column {
                                    width: parent.width - parent.children[0].width - 12
                                    spacing: 8

                                    SkeletonBlock { width: 140; height: 16 }
                                    SkeletonBlock { width: 220; height: 32 }
                                    Row {
                                        spacing: 10
                                        Repeater {
                                            model: 5
                                            SkeletonBlock { width: 22; height: 22; radius: 4 }
                                        }
                                    }
                                    Row {
                                        spacing: 8
                                        Repeater {
                                            model: 3
                                            SkeletonBlock { width: 88; height: 24; radius: 4 }
                                        }
                                    }
                                    Row {
                                        spacing: 8
                                        SkeletonBlock { width: 88; height: 36; radius: 8 }
                                        SkeletonBlock { width: 88; height: 36; radius: 8 }
                                    }
                                    SkeletonBlock { width: parent.width; height: 14 }
                                    SkeletonBlock { width: parent.width * 0.88; height: 14 }
                                    SkeletonBlock { width: parent.width * 0.62; height: 14 }
                                }
                            }
                        }
                    }
                }
            }

            Loader {
                id: cardPage
                parent: root.compact ? compactShell.pageHost : uiCanvas

                anchors.fill: parent
                // Sheared panel edges overhang their box; the compact host clips at its edges.
                anchors.leftMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.rightMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.bottomMargin: root.compact ? 0 : 148
                z: 40
                asynchronous: true
                active: root.cardsMounted
                source: "CardScene.qml"
                visible: root.cardsOpen && !root.cardPageBusy
                onStatusChanged: {
                    if (status === Loader.Ready && cardPage.item) {
                        if (root.cardsOpen) {
                            root.applyCardsNavGraph()
                            if (!root.tvKeepTabFocus)
                                cardPage.item.takeKeyboard()
                        }
                    }
                }
            }

            Binding {
                target: cardPage.item
                property: "compact"
                value: root.compact
                when: cardPage.item !== null
            }

            Connections {
                target: cardPage.item
                ignoreUnknownSignals: true
                function onNavigationEndpointChanged() {
                    if (root.cardsOpen)
                        Qt.callLater(root.applyCardsNavGraph)
                }
            }

            Loader {
                id: settingsPage
                parent: root.compact ? compactShell.pageHost : uiCanvas

                anchors.fill: parent
                // Sheared panel edges overhang their box; the compact host clips at its edges.
                anchors.leftMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.rightMargin: root.compact ? HomeTheme.compactGap : 0
                anchors.bottomMargin: root.compact ? 0 : 148
                z: 40
                active: root.settingsMounted
                source: "SettingsScene.qml"
                visible: root.settingsOpen
                onLoaded: {
                    if (root.settingsOpen) {
                        root.applySettingsNavGraph()
                        if (!root.tvKeepTabFocus)
                            settingsPage.item.takeKeyboard()
                    }
                }
            }

            Binding {
                target: settingsPage.item
                property: "compact"
                value: root.compact
                when: settingsPage.item !== null
            }

            Connections {
                target: settingsPage.item
                ignoreUnknownSignals: true
                function onNavigationEndpointChanged() {
                    if (root.settingsOpen)
                        Qt.callLater(root.applySettingsNavGraph)
                }
            }

            Item {
                anchors.fill: cardPage
                z: 41
                visible: root.cardsOpen && root.cardPageBusy

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: HomeTheme.cardPageHMargin
                    anchors.rightMargin: HomeTheme.cardPageHMargin
                    anchors.topMargin: HomeTheme.cardPageTopMargin
                    anchors.bottomMargin: HomeTheme.cardPageBottomMargin
                    spacing: HomeTheme.cardPanelGap

                    BASlantedPanel {
                        width: parent.width
                        height: HomeTheme.cardHeaderHeight
                        slant: -0.05
                        cornerRadius: HomeTheme.cardPanelRadius
                        shadowBlur: 0
                        shadowOffset: 0
                        topColor: HomeTheme.cardPanelTop
                        bottomColor: HomeTheme.cardPanelBottom
                        borderColor: HomeTheme.cardPanelBorder

                        Row {
                            anchors.fill: parent
                            anchors.margins: HomeTheme.cardSkeletonHeaderPadding
                            spacing: HomeTheme.cardPanelGap
                            SkeletonBlock {
                                width: HomeTheme.cardHeaderButtonWidth
                                height: HomeTheme.cardActionButtonExtent
                                radius: HomeTheme.cardControlRadius
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: HomeTheme.cardDetailMetaGap
                                SkeletonBlock {
                                    width: HomeTheme.cardSkeletonTitleWidth
                                    height: HomeTheme.cardSkeletonTitleHeight
                                }
                                SkeletonBlock {
                                    width: HomeTheme.cardSkeletonSubtitleWidth
                                    height: HomeTheme.cardSkeletonSubtitleHeight
                                }
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        height: parent.height - HomeTheme.cardHeaderHeight - HomeTheme.cardPanelGap
                        spacing: HomeTheme.cardPanelGap

                        SkeletonBlock {
                            width: HomeTheme.cardFilterWidth
                            height: parent.height
                            radius: HomeTheme.cardPanelRadius
                        }

                        Item {
                            width: parent.width - HomeTheme.cardFilterWidth - HomeTheme.cardDetailWidth
                                   - HomeTheme.cardPanelGap * 2
                            height: parent.height

                            Grid {
                                id: cardSkeletonGrid
                                anchors.fill: parent
                                anchors.margins: HomeTheme.cardGridGap
                                columns: 4
                                rows: 3
                                property real tileWidth: width / columns
                                property real tileHeight: height / rows
                                Repeater {
                                    model: 12
                                    Item {
                                        width: cardSkeletonGrid.tileWidth
                                        height: cardSkeletonGrid.tileHeight
                                        SkeletonBlock {
                                            anchors.fill: parent
                                            anchors.margins: HomeTheme.cardGridGap / 2
                                            radius: HomeTheme.cardPanelRadius
                                        }
                                    }
                                }
                            }
                        }

                        SkeletonBlock {
                            width: HomeTheme.cardDetailWidth
                            height: parent.height
                            radius: HomeTheme.cardPanelRadius
                        }
                    }
                }
            }
        }
    }

    HomeCompactShell {
        id: compactShell
        parent: contentHost
        // UIScale lays the shell out narrower and enlarges it; 320 is the narrowest width it fits.
        readonly property real availableWidth: contentHost.width - HomeTheme.compactMargin * 2
        readonly property real zoom: Math.min(root.uiScale, Math.max(1.0, availableWidth / 320))
        x: HomeTheme.compactMargin
        y: HomeTheme.compactMargin
        width: availableWidth / zoom
        height: (contentHost.height - HomeTheme.compactMargin) / zoom
        scale: zoom
        transformOrigin: Item.TopLeft
        visible: root.compact
        subPageOpen: root.subPageOpen
        pageLoading: root.generalPageBusy || root.cardPageBusy
    }
    // This entry is outside the scaled landscape canvas and remains reachable on every home page.
    HomeLayoutControls {
        id: layoutControls
        parent: contentHost
        // Portrait reaches these options from the Settings page, leaving the dock at the bottom.
        visible: !root.compact
        safeInsets: root.SafeArea.margins
        anchors.bottom: parent.bottom
        anchors.bottomMargin: HomeTheme.compactMargin
        x: Config.oneHandedness === 1 ? HomeTheme.compactMargin
           : parent.width - width - HomeTheme.compactMargin
        width: Math.min(implicitWidth, parent.width - HomeTheme.compactMargin * 2)
        z: 150
    }

    HomePointerFx {
        anchors.fill: parent
        z: 200
        enabled: false
    }

    // Keyboard navigation links between the controls on each panel.
    function applyHomeNavGraph() {
        if (root.compact) return
        actionPanel.quickJoinBtn.KeyNavigation.right = sideBar.settingsBtn
        actionPanel.joinGameBtn.KeyNavigation.right = sideBar.aboutBtn
        actionPanel.startServerBtn.KeyNavigation.right = sideBar.updateBtn
        sideBar.settingsBtn.KeyNavigation.left = actionPanel.quickJoinBtn
        sideBar.aboutBtn.KeyNavigation.left = actionPanel.joinGameBtn
        sideBar.updateBtn.KeyNavigation.left = actionPanel.startServerBtn

        actionPanel.quickJoinBtn.KeyNavigation.down = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.down = actionPanel.startServerBtn
        actionPanel.startServerBtn.KeyNavigation.down = actionPanel.worksBtn.visible ? actionPanel.worksBtn : bottomBar.replaysBtn
        actionPanel.worksBtn.KeyNavigation.up = actionPanel.startServerBtn
        actionPanel.worksBtn.KeyNavigation.down = bottomBar.replaysBtn
        actionPanel.startServerBtn.KeyNavigation.up = actionPanel.joinGameBtn
        actionPanel.joinGameBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        actionPanel.quickJoinBtn.KeyNavigation.up = bottomBar.homeBtn

        sideBar.settingsBtn.KeyNavigation.down = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.down = sideBar.updateBtn
        sideBar.updateBtn.KeyNavigation.down = sideBar.settingsBtn
        sideBar.updateBtn.KeyNavigation.up = sideBar.aboutBtn
        sideBar.aboutBtn.KeyNavigation.up = sideBar.settingsBtn
        sideBar.settingsBtn.KeyNavigation.up = sideBar.updateBtn

        bottomBar.homeBtn.KeyNavigation.right = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.right = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.right = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.right = bottomBar.settingsBtn
        bottomBar.settingsBtn.KeyNavigation.right = bottomBar.homeBtn
        bottomBar.settingsBtn.KeyNavigation.left = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.left = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.left = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.left = bottomBar.homeBtn
        bottomBar.homeBtn.KeyNavigation.left = bottomBar.settingsBtn

        bottomBar.homeBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.generalsBtn.KeyNavigation.up = actionPanel.joinGameBtn
        bottomBar.cardsBtn.KeyNavigation.up = actionPanel.startServerBtn
        bottomBar.replaysBtn.KeyNavigation.up = actionPanel.quickJoinBtn
        bottomBar.settingsBtn.KeyNavigation.up = actionPanel.joinGameBtn

        bottomBar.homeBtn.KeyNavigation.tab = bottomBar.generalsBtn
        bottomBar.generalsBtn.KeyNavigation.tab = bottomBar.cardsBtn
        bottomBar.cardsBtn.KeyNavigation.tab = bottomBar.replaysBtn
        bottomBar.replaysBtn.KeyNavigation.tab = bottomBar.settingsBtn
        bottomBar.settingsBtn.KeyNavigation.tab = bottomBar.homeBtn
        bottomBar.homeBtn.KeyNavigation.backtab = bottomBar.settingsBtn
        bottomBar.generalsBtn.KeyNavigation.backtab = bottomBar.homeBtn
        bottomBar.cardsBtn.KeyNavigation.backtab = bottomBar.generalsBtn
        bottomBar.replaysBtn.KeyNavigation.backtab = bottomBar.cardsBtn
        bottomBar.settingsBtn.KeyNavigation.backtab = bottomBar.replaysBtn
    }

    function applyGeneralsNavGraph() {
        var g = generalPage.item
        if (!g)
            return
        if (root.compact) {
            applyCompactCatalogNav(g.navigationEntry, g.navigationExit)
            return
        }
        g.banBtn.KeyNavigation.tab = root.navBar.generalsBtn
        g.searchField.KeyNavigation.backtab = root.navBar.settingsBtn
        root.navBar.homeBtn.KeyNavigation.up = g.searchField
        root.navBar.generalsBtn.KeyNavigation.up = g.searchField
        root.navBar.cardsBtn.KeyNavigation.up = g.searchField
        root.navBar.replaysBtn.KeyNavigation.up = g.searchField
        root.navBar.settingsBtn.KeyNavigation.up = g.searchField
        root.navBar.settingsBtn.KeyNavigation.tab = g.searchField
        root.navBar.homeBtn.KeyNavigation.backtab = g.banBtn
    }

    function applyCardsNavGraph() {
        var c = cardPage.item
        if (!c)
            return
        if (root.compact) {
            applyCompactCatalogNav(c.navigationEntry, c.lastControl)
            return
        }
        c.backButton.KeyNavigation.tab = c.sortControl
        c.backButton.KeyNavigation.backtab = root.navBar.settingsBtn
        c.sortControl.KeyNavigation.tab = c.themeButton
        c.sortControl.KeyNavigation.backtab = c.backButton
        c.themeButton.KeyNavigation.tab = c.reloadButton
        c.themeButton.KeyNavigation.backtab = c.sortControl
        c.reloadButton.KeyNavigation.tab = c.searchField
        c.reloadButton.KeyNavigation.backtab = c.themeButton
        c.searchField.KeyNavigation.backtab = c.reloadButton
        c.lastControl.KeyNavigation.tab = root.navBar.cardsBtn
        c.lastControl.KeyNavigation.down = root.navBar.cardsBtn
        root.navBar.homeBtn.KeyNavigation.up = c.searchField
        root.navBar.generalsBtn.KeyNavigation.up = c.searchField
        root.navBar.cardsBtn.KeyNavigation.up = c.lastControl
        root.navBar.replaysBtn.KeyNavigation.up = c.searchField
        root.navBar.settingsBtn.KeyNavigation.up = c.searchField
        root.navBar.settingsBtn.KeyNavigation.tab = c.backButton
        root.navBar.cardsBtn.KeyNavigation.backtab = c.lastControl
    }

    function applySettingsNavGraph() {
        var s = settingsPage.item
        if (s)
            applyCompactCatalogNav(s.navigationEntry, s.lastControl)
    }

    function restoreHomeKeyboard() {
        if (root.compact) {
            if (root.generalsOpen) applyGeneralsNavGraph()
            else if (root.cardsOpen) applyCardsNavGraph()
            else if (root.settingsOpen) applySettingsNavGraph()
            if (HomeTheme.tvMode && compactShell.quickJoinBtn && !root.subPageOpen)
                compactShell.quickJoinBtn.forceActiveFocus()
            else
                root.navBar.homeBtn.forceActiveFocus()
            return
        }
        applyHomeNavGraph()
        if (actionPanel.visible)
            actionPanel.quickJoinBtn.forceActiveFocus()
        else
            root.navBar.homeBtn.forceActiveFocus()
    }

    function isEditableTextFocused() {
        var win = Window.window
        var item = win ? win.activeFocusItem : null
        while (item) {
            if (item instanceof TextInput || item instanceof TextEdit)
                return true
            item = item.parent
        }
        return false
    }

    function currentTvTabIndex() {
        var idx = root.navBar.currentIndex
        if (idx >= 0 && idx <= 4)
            return idx
        if (root.generalsOpen)
            return 1
        if (root.cardsOpen)
            return 2
        if (root.settingsOpen)
            return 4
        return 0
    }

    function cycleTvTab(delta) {
        var now = Date.now()
        if (now - root.tvTabCycleAt < 80)
            return
        root.tvTabCycleAt = now
        var next = (currentTvTabIndex() + delta + 5) % 5
        var bar = root.navBar
        var buttons = [bar.homeBtn, bar.generalsBtn, bar.cardsBtn, bar.replaysBtn, bar.settingsBtn]
        var btn = buttons[next]
        root.tvKeepTabFocus = true
        bar.currentIndex = next
        switch (next) {
        case 1:
            homeController.openGenerals()
            break
        case 2:
            homeController.openCards()
            break
        case 3:
            homeController.openReplays()
            break
        case 4:
            homeController.openSettings()
            break
        default:
            homeController.openHome()
            break
        }
        Qt.callLater(function() {
            if (btn)
                btn.forceActiveFocus()
            root.tvKeepTabFocus = false
        })
    }

    function applyCompactCatalogNav(entry, endpoint) {
        if (!entry || !endpoint) return
        entry.KeyNavigation.backtab = root.navBar.settingsBtn
        endpoint.KeyNavigation.tab = root.navBar.homeBtn
        root.navBar.homeBtn.KeyNavigation.backtab = endpoint
        root.navBar.settingsBtn.KeyNavigation.tab = entry
        var buttons = [root.navBar.homeBtn, root.navBar.generalsBtn, root.navBar.cardsBtn,
                       root.navBar.replaysBtn, root.navBar.settingsBtn]
        for (var i = 0; i < buttons.length; ++i) buttons[i].KeyNavigation.up = entry
    }

    // Load after the home page settles; the 800 ms delay avoids competing with entrance animation and image decoding.
    Item {
        id: generalArtPrefetch
        x: -4000
        y: -4000
        width: 1
        height: 1
        opacity: 0
        enabled: false
        z: -1

        readonly property int gridInnerWidth: {
            var colW = uiCanvas.width - HomeTheme.generalPageHMargin * 2
            var listW = Math.round((colW - HomeTheme.generalPanelGap)
                                   * HomeTheme.generalListShare)
            return Math.max(1, listW - HomeTheme.generalGridMargin * 2)
        }
        readonly property int cols: {
            var saved = homeController.generalGridColumns()
            var v = saved > 0 ? saved : HomeTheme.generalGridMinColumns
            return Math.max(HomeTheme.generalGridMinColumns,
                            Math.min(v, HomeTheme.generalGridMaxColumns - 1))
        }
        readonly property int cellW: HomeTheme.generalCellWidth(gridInnerWidth, cols)
        readonly property int cellH: HomeTheme.generalCellHeight(gridInnerWidth, cols)
        readonly property int artW: Math.max(1, cellW - HomeTheme.generalCellInset * 2)
        readonly property int artH: Math.max(1, cellH - HomeTheme.generalCellInset * 2)
        property int mounted: 0
        property int target: 0

        Repeater {
            model: generalArtPrefetch.mounted
            Image {
                width: generalArtPrefetch.artW
                height: generalArtPrefetch.artH
                asynchronous: true
                cache: true
                sourceSize.width: Math.ceil(width)
                sourceSize.height: Math.ceil(height)
                source: homeController.prefetchArtUrl(index)
            }
        }
    }

    Timer {
        id: generalPrefetchStart
        interval: 800
        repeat: false
        onTriggered: root.startGeneralPrefetch()
    }

    Timer {
        id: generalPrefetchTick
        interval: 32
        repeat: true
        onTriggered: {
            if (generalArtPrefetch.mounted >= generalArtPrefetch.target) {
                stop()
                return
            }
            generalArtPrefetch.mounted += 1
        }
    }

    function startGeneralPrefetch() {
        homeController.warmGeneralCatalog()
        var n = homeController.generalModel ? homeController.generalModel.count : 0
        generalArtPrefetch.target = Math.min(16, n)
        if (generalArtPrefetch.target > 0) {
            generalArtPrefetch.mounted = 1
            if (generalArtPrefetch.target > 1)
                generalPrefetchTick.start()
        }
        generalsMounted = true
    }

    Component.onCompleted: {
        // A reload can land while the settings page is current; resume its edit.
        if (settingsOpen) {
            settingsMounted = true
            settingsSession.begin()
        }
        applyHomeNavGraph()
        attachPopupOverlayEffect()
        if (HomeTheme.tvMode)
            Qt.callLater(root.restoreHomeKeyboard)
        else
            actionPanel.quickJoinBtn.forceActiveFocus()
        enterAnim.start()
        generalPrefetchStart.start()
    }

    Connections {
        target: homeController
        function onCurrentPageChanged() {
            if (!root.tvKeepTabFocus) {
                bottomBar.currentIndex = root.generalsOpen ? 1 : root.cardsOpen ? 2
                                         : root.settingsOpen ? 4 : 0
            }
            if (root.generalsOpen) {
                if (generalPage.item) {
                    root.applyGeneralsNavGraph()
                    if (!root.tvKeepTabFocus)
                        generalPage.item.takeKeyboard()
                }
            } else if (root.cardsOpen) {
                if (cardPage.item) {
                    root.applyCardsNavGraph()
                    if (!root.tvKeepTabFocus)
                        cardPage.item.takeKeyboard()
                }
            } else if (root.settingsOpen) {
                if (settingsPage.item) {
                    root.applySettingsNavGraph()
                    if (!root.tvKeepTabFocus)
                        settingsPage.item.takeKeyboard()
                }
            } else if (!root.tvKeepTabFocus) {
                Qt.callLater(root.restoreHomeKeyboard)
            }
        }
    }

    ParallelAnimation {
        id: enterAnim

        NumberAnimation {
            target: bottomEnter
            property: "y"
            to: 0
            duration: 320
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: bottomBar
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: actionEnter
            property: "x"
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: actionPanel
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: sideEnter
            property: "x"
            to: 0
            duration: 260
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: sideBar
            property: "opacity"
            to: 1
            duration: 180
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: playerEnter
            property: "x"
            to: 0
            duration: 280
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: playerInfo
            property: "opacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: logoEnter
            property: "y"
            to: 0
            duration: 300
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: logo
            property: "opacity"
            to: 1
            duration: 220
            easing.type: Easing.OutCubic
        }
    }
}
