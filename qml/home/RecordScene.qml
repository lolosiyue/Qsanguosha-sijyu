pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

// The replays page lists the replay files in the record directory and runs the replay tools on them.
Item {
    id: root
    objectName: "recordScene"

    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    // Portrait shows one pane at a time: 0 is the list, 1 the selected replay.
    property int compactPane: 0
    readonly property bool highContrast: homeController.visualMode === "highcontrast"
    property var replays: []
    property string query: ""
    property string selectedPath: ""
    property var detail: null
    property bool detailLoading: false
    // Analysis reads the whole replay, so each file is parsed at most once per listing.
    property var detailCache: ({})
    property string statusText: ""
    property bool statusFailed: false
    readonly property var shownReplays: {
        var key = query.trim().toLowerCase()
        if (key === "")
            return replays
        return replays.filter(function(item) { return item.name.toLowerCase().indexOf(key) >= 0 })
    }
    readonly property var selected: {
        for (var i = 0; i < shownReplays.length; ++i) {
            if (shownReplays[i].path === selectedPath)
                return shownReplays[i]
        }
        return null
    }
    property alias searchField: searchInput
    readonly property var navigationEntry: compact ? refreshButton : backButton
    readonly property var lastControl: compact && compactPane === 0 ? replayList : deleteButton
    signal navigationEndpointChanged()

    focus: true
    Keys.onEscapePressed: {
        if (compact && compactPane !== 0) showCompactPane(0)
        else homeController.openHome()
    }
    onNavigationEntryChanged: navigationEndpointChanged()
    onLastControlChanged: {
        // HomeScene points the endpoint's Tab at the dock; a former endpoint rejoins the normal chain.
        if (lastControl !== replayList) replayList.KeyNavigation.tab = null
        if (lastControl !== deleteButton) deleteButton.KeyNavigation.tab = null
        navigationEndpointChanged()
    }
    onSelectedChanged: if (selected === null && compactPane === 1) compactPane = 0
    onVisibleChanged: if (visible) reload()
    onShownReplaysChanged: Qt.callLater(syncSelection)
    Component.onCompleted: reload()

    function takeKeyboard() {
        if (compact && compactPane === 1) playButton.forceActiveFocus()
        else if (replayList.count > 0) replayList.forceActiveFocus()
        else searchInput.forceActiveFocus()
    }

    function showCompactPane(pane) {
        compactPane = pane
        Qt.callLater(takeKeyboard)
    }

    // Portrait opens the details pane for the replay the user picked.
    function open(path) {
        select(path)
        if (compact && selected !== null) showCompactPane(1)
        else replayList.forceActiveFocus()
    }

    function indexOfPath(path) {
        for (var i = 0; i < shownReplays.length; ++i) {
            if (shownReplays[i].path === path)
                return i
        }
        return -1
    }

    function reload(preferredPath) {
        replays = homeController.replayFiles()
        detailCache = ({})
        var path = preferredPath !== undefined ? preferredPath : selectedPath
        select(indexOfPath(path) >= 0 ? path : shownReplays.length > 0 ? shownReplays[0].path : "")
    }

    function syncSelection() {
        var index = indexOfPath(selectedPath)
        if (index < 0)
            select(shownReplays.length > 0 ? shownReplays[0].path : "")
        else
            replayList.currentIndex = index
    }

    function select(path) {
        if (path !== selectedPath)
            statusText = ""
        selectedPath = path
        replayList.currentIndex = indexOfPath(path)
        detail = path !== "" && detailCache[path] !== undefined ? detailCache[path] : null
        detailLoading = path !== "" && detail === null
        if (detailLoading)
            detailTimer.restart()
    }

    function report(text, failed) {
        statusText = text
        statusFailed = failed
    }

    function play() {
        if (selected)
            homeController.playReplay(selected.path)
    }

    function convert() {
        if (!selected)
            return
        var target = selected.format === "TXT" ? "PNG" : "TXT"
        var output = homeController.convertReplay(selected.path)
        if (output === "") {
            report(qsTr("Conversion failed: a %1 replay with this name already exists, or the file cannot be read.").arg(target), true)
            return
        }
        reload(output)
        report(qsTr("Converted to a %1 replay.").arg(target), false)
    }

    function rename(name) {
        if (!selected)
            return true
        if (name.trim() === selected.name)
            return true
        var output = homeController.renameReplay(selected.path, name)
        if (output === "")
            return false
        reload(output)
        report(qsTr("Renamed."), false)
        return true
    }

    function remove() {
        if (!selected)
            return
        var index = indexOfPath(selected.path)
        var next = index + 1 < shownReplays.length ? shownReplays[index + 1].path
                 : index > 0 ? shownReplays[index - 1].path : ""
        var name = selected.name
        if (!homeController.deleteReplay(selected.path)) {
            report(qsTr("Delete failed: the file may be in use."), true)
            return
        }
        reload(next)
        report(qsTr("Deleted %1.").arg(name), false)
    }

    Timer {
        id: detailTimer
        // Let the selection paint before the synchronous analysis runs.
        interval: 150
        onTriggered: {
            var path = root.selectedPath
            if (path === "")
                return
            var result = homeController.replayDetails(path)
            root.detailCache[path] = result
            if (path === root.selectedPath) {
                root.detail = result
                root.detailLoading = false
            }
        }
    }

    component Badge: Rectangle {
        property alias text: badgeLabel.text
        property color tone: HomeTheme.cardBadgeInfo
        implicitWidth: badgeLabel.implicitWidth + HomeTheme.cardBadgeHPadding * 2
        implicitHeight: HomeTheme.cardBadgeHeight
        radius: HomeTheme.cardBadgeRadius
        color: tone
        Text {
            id: badgeLabel
            anchors.centerIn: parent
            color: HomeTheme.cardBadgeText
            font.pixelSize: HomeTheme.cardMetaFontSize
            font.bold: true
        }
    }

    component GroupTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: HomeTheme.settingsRowGap
        Accessible.role: Accessible.Heading
        color: HomeTheme.cardAccent
        font.pixelSize: HomeTheme.cardDetailSectionFontSize
        font.bold: true
        wrapMode: Text.Wrap
    }

    component InfoRow: RowLayout {
        property alias label: infoLabel.text
        property alias value: infoValue.text
        Layout.fillWidth: true
        spacing: HomeTheme.cardPanelGap
        Text {
            id: infoLabel
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 72
            color: HomeTheme.cardTextMuted
            font.pixelSize: HomeTheme.cardBodyFontSize
        }
        Text {
            id: infoValue
            Layout.fillWidth: true
            color: HomeTheme.cardTextPrimary
            font.pixelSize: HomeTheme.cardBodyFontSize
            wrapMode: Text.Wrap
        }
    }

    component ActionButton: BAToolButton {
        Layout.fillWidth: root.compact
        Layout.preferredHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardActionButtonExtent
        opacity: enabled ? 1 : 0.5
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.rightMargin: root.compact ? 0 : HomeTheme.cardPageHMargin
        anchors.topMargin: root.compact ? 0 : HomeTheme.cardPageTopMargin
        anchors.bottomMargin: HomeTheme.cardPageBottomMargin
        spacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap

        BASlantedPanel {
            id: headerPanel
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? headerGrid.implicitHeight + HomeTheme.compactMargin * 2
                                                 : HomeTheme.cardHeaderHeight
            slant: -0.05
            cornerRadius: HomeTheme.cardPanelRadius
            shadowBlur: 0
            shadowOffset: 0
            topColor: HomeTheme.cardPanelTop
            bottomColor: HomeTheme.cardPanelBottom
            borderColor: HomeTheme.cardPanelBorder
            shadowColor: HomeTheme.baDockShadow
            accentVisible: true
            accentColor: HomeTheme.cardAccent

            GridLayout {
                id: headerGrid
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                anchors.rightMargin: root.compact ? HomeTheme.compactMargin : HomeTheme.cardHeaderPadding
                columns: root.compact ? 3 : 6
                columnSpacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap
                rowSpacing: HomeTheme.compactGap

                BAToolButton {
                    id: backButton
                    visible: !root.compact
                    Layout.preferredWidth: HomeTheme.cardHeaderButtonWidth
                    Layout.preferredHeight: HomeTheme.cardActionButtonExtent
                    text: homeController.qtTranslate("CardScene", "Back")
                    onClicked: homeController.openHome()
                }

                ColumnLayout {
                    Layout.columnSpan: root.compact ? 3 : 1
                    Layout.fillWidth: true
                    spacing: HomeTheme.cardHeaderTitleGap
                    Text {
                        Layout.fillWidth: true
                        text: qsTranslate("HomeScene", "Replays")
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: root.compact ? HomeTheme.cardSectionTitleFontSize : HomeTheme.cardTitleFontSize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: homeController.recordFolder()
                        elide: Text.ElideMiddle
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardCaptionFontSize
                    }
                }

                Rectangle {
                    visible: !root.compact
                    Layout.preferredWidth: countText.implicitWidth + HomeTheme.cardCountBadgeHPadding * 2
                    Layout.preferredHeight: HomeTheme.cardCountBadgeHeight
                    radius: HomeTheme.cardCountBadgeHeight / 2
                    color: HomeTheme.cardInteractiveSoft
                    border.width: HomeTheme.cardBorderWidth
                    border.color: HomeTheme.cardInteractive
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: root.shownReplays.length === root.replays.length
                              ? qsTr("%1 replays").arg(root.replays.length)
                              : qsTr("%1 / %2 replays").arg(root.shownReplays.length).arg(root.replays.length)
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        font.bold: true
                    }
                }

                ActionButton {
                    id: refreshButton
                    text: qsTr("Refresh")
                    onClicked: root.reload()
                }

                ActionButton {
                    // Android has no file manager to hand the folder to.
                    visible: Qt.platform.os !== "android"
                    text: qsTr("Open folder")
                    onClicked: homeController.openRecordFolder()
                }

                ActionButton {
                    text: qsTr("Open other replay")
                    onClicked: homeController.browseReplay()
                }
            }
        }

        CatalogPaneBar {
            id: compactBar
            Layout.fillWidth: true
            visible: root.compact
            currentIndex: root.compactPane
            itemCount: root.shownReplays.length
            detailsEnabled: root.selected !== null
            // The search field sits above the list, so this page has no filter pane.
            filterButton.visible: false
            onActivated: function(index) { root.showCompactPane(index) }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: HomeTheme.cardPanelGap

            BASlantedPanel {
                visible: !root.compact || root.compactPane === 0
                Layout.fillWidth: root.compact
                Layout.fillHeight: true
                Layout.preferredWidth: root.compact ? -1 : Math.round(root.width * 0.36)
                slant: 0
                cornerRadius: HomeTheme.cardPanelRadius
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.cardPanelTop
                bottomColor: HomeTheme.cardPanelBottom
                borderColor: HomeTheme.cardPanelBorder
                shadowColor: HomeTheme.baDockShadow

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.compact ? HomeTheme.compactGap : HomeTheme.cardGridGap
                    spacing: HomeTheme.cardGridGap

                    TextField {
                        id: searchInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
                        activeFocusOnTab: true
                        placeholderText: qsTr("Search replay names")
                        color: HomeTheme.cardTextPrimary
                        placeholderTextColor: HomeTheme.cardTextMuted
                        selectByMouse: true
                        Accessible.name: qsTr("Search replay names")
                        onTextChanged: root.query = text
                        Keys.onDownPressed: replayList.forceActiveFocus()
                        // A blinking cursor repaints the whole QQuickWidget on every blink.
                        cursorDelegate: Rectangle {
                            width: 2
                            color: searchInput.color
                            visible: searchInput.cursorVisible
                        }
                        background: Rectangle {
                            radius: HomeTheme.cardControlRadius
                            color: HomeTheme.cardInputFill
                            border.width: searchInput.activeFocus
                                          ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                               : HomeTheme.cardSelectedBorderWidth)
                                          : HomeTheme.cardBorderWidth
                            border.color: searchInput.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.cardPanelBorder
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: replayList
                            anchors.fill: parent
                            clip: true
                            spacing: HomeTheme.cardTileTextGap
                            activeFocusOnTab: true
                            keyNavigationEnabled: true
                            boundsBehavior: Flickable.StopAtBounds
                            highlightMoveDuration: 0
                            model: root.shownReplays
                            Accessible.role: Accessible.List
                            Accessible.name: qsTranslate("HomeScene", "Replays")
                            ScrollBar.vertical: HomeScrollBar { }
                            onCurrentIndexChanged: {
                                if (currentIndex >= 0 && currentIndex < root.shownReplays.length
                                        && root.shownReplays[currentIndex].path !== root.selectedPath)
                                    root.select(root.shownReplays[currentIndex].path)
                            }
                            Keys.onReturnPressed: root.play()
                            Keys.onEnterPressed: root.play()
                            Keys.onDeletePressed: if (root.selected) deleteDialog.open()

                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                readonly property bool current: modelData.path === root.selectedPath
                                width: ListView.view.width - HomeTheme.cardGridGap
                                height: rowColumn.implicitHeight + HomeTheme.cardControlHPadding * 2
                                radius: HomeTheme.cardControlRadius
                                color: current ? HomeTheme.cardTileSelected
                                     : rowMouse.containsMouse ? HomeTheme.cardTileHover : HomeTheme.cardTileFill
                                border.width: current && replayList.activeFocus
                                              ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                                   : HomeTheme.cardFocusBorderWidth)
                                              : current ? HomeTheme.cardSelectedBorderWidth : HomeTheme.cardBorderWidth
                                border.color: current && replayList.activeFocus ? HomeTheme.focusBorderHigh
                                            : current ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.name

                                ColumnLayout {
                                    id: rowColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: HomeTheme.cardControlHPadding
                                    anchors.rightMargin: HomeTheme.cardControlHPadding
                                    spacing: 4

                                    Text {
                                        Layout.fillWidth: true
                                        text: row.modelData.name
                                        elide: Text.ElideRight
                                        color: HomeTheme.cardTextPrimary
                                        font.pixelSize: HomeTheme.cardTileTitleFontSize
                                        font.bold: true
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: HomeTheme.cardTileTextGap
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.modified + " · " + row.modelData.size
                                            elide: Text.ElideRight
                                            color: HomeTheme.cardTextSecondary
                                            font.pixelSize: HomeTheme.cardCaptionFontSize
                                        }
                                        Badge {
                                            visible: row.modelData.takeover
                                            text: qsTr("Takeover")
                                            tone: HomeTheme.cardBadgeBasic
                                        }
                                        Badge {
                                            text: row.modelData.format
                                            tone: row.modelData.format === "TXT" ? HomeTheme.cardBadgeEquip
                                                                                  : HomeTheme.cardBadgeSkill
                                        }
                                    }
                                }

                                MouseArea {
                                    id: rowMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: root.open(row.modelData.path)
                                    onDoubleClicked: root.play()
                                }
                            }
                        }

                        Text {
                            anchors.fill: parent
                            anchors.margins: HomeTheme.cardPanelContentPadding
                            visible: replayList.count === 0
                            text: root.replays.length === 0
                                  ? qsTr("The record folder has no replays yet.") + "\n"
                                    + qsTr("Turn on auto-save under Settings → Game → Replays to save finished games here, or use Open other replay to play a file from elsewhere.")
                                  : qsTr("No replays match the search.")
                            color: HomeTheme.cardTextMuted
                            font.pixelSize: HomeTheme.cardEmptyFontSize
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            BASlantedPanel {
                visible: !root.compact || root.compactPane === 1
                Layout.fillWidth: true
                Layout.fillHeight: true
                slant: 0
                cornerRadius: HomeTheme.cardPanelRadius
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.cardPanelTop
                bottomColor: HomeTheme.cardPanelBottom
                borderColor: HomeTheme.cardPanelBorder
                shadowColor: HomeTheme.baDockShadow

                // The actions stay below the scrolling statistics, reachable without scrolling in portrait.
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: root.compact ? HomeTheme.compactMargin : HomeTheme.cardPanelContentPadding
                    spacing: HomeTheme.cardSectionGap

                    Flickable {
                        id: detailView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: width
                        contentHeight: detailColumn.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        // The statistics hold no controls; a long chat log scrolls from the keyboard here.
                        activeFocusOnTab: contentHeight > height
                        Accessible.role: Accessible.Pane
                        Accessible.name: qsTr("Replay details")
                        Accessible.focusable: activeFocusOnTab
                        ScrollBar.vertical: HomeScrollBar { }
                        Keys.onPressed: function(event) {
                            var target = event.key === Qt.Key_Down ? contentY + 48
                                       : event.key === Qt.Key_Up ? contentY - 48
                                       : event.key === Qt.Key_PageDown ? contentY + height
                                       : event.key === Qt.Key_PageUp ? contentY - height
                                       : event.key === Qt.Key_Home ? 0
                                       : event.key === Qt.Key_End ? contentHeight : NaN
                            if (isNaN(target))
                                return
                            contentY = Math.max(0, Math.min(contentHeight - height, target))
                            event.accepted = true
                        }

                        ColumnLayout {
                            id: detailColumn
                            width: detailView.width - HomeTheme.cardGridGap
                            spacing: HomeTheme.cardSectionGap

                            Text {
                                Layout.fillWidth: true
                                text: root.selected ? root.selected.name : qsTr("No replay selected")
                                color: HomeTheme.cardTextPrimary
                                // A long autosave name stays on one line in portrait.
                                font.pixelSize: root.compact ? HomeTheme.cardTileTitleFontSize : HomeTheme.cardDetailNameFontSize
                                font.bold: true
                                wrapMode: Text.WrapAnywhere
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.selected
                                      ? [root.selected.modified, root.selected.size, root.selected.format].join(" · ")
                                      : qsTr("Select a replay from the list to play it, convert its format or view the game statistics.")
                                color: HomeTheme.cardTextSecondary
                                font.pixelSize: HomeTheme.cardBodyFontSize
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: root.selected !== null && root.selected.takeover
                                text: qsTr("This replay has takeover snapshots: during playback you can take over the game from a snapshot.")
                                color: HomeTheme.cardPositive
                                font.pixelSize: HomeTheme.cardCaptionFontSize
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.topMargin: HomeTheme.settingsRowGap
                                visible: root.selected !== null && (root.detailLoading || !root.detail || !root.detail.valid)
                                text: root.detailLoading ? qsTr("Analyzing replay...")
                                                         : qsTr("Cannot read the game information of this replay. The file may be damaged or from an incompatible version.")
                                color: HomeTheme.cardTextMuted
                                font.pixelSize: HomeTheme.cardBodyFontSize
                                wrapMode: Text.Wrap
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.selected !== null && !root.detailLoading
                                         && root.detail !== null && root.detail.valid === true
                                spacing: HomeTheme.cardSectionGap

                                GroupTitle { text: qsTr("Game information") }
                                InfoRow {
                                    label: qsTr("Mode")
                                    value: root.detail && root.detail.mode ? root.detail.mode : "-"
                                }
                                InfoRow {
                                    label: qsTr("Options")
                                    value: root.detail && root.detail.options ? root.detail.options : "-"
                                }
                                InfoRow {
                                    label: qsTr("Packages")
                                    value: root.detail && root.detail.packages ? root.detail.packages : "-"
                                }

                                GroupTitle { text: qsTr("Players") }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: root.compact ? 1 : 2
                                    columnSpacing: HomeTheme.cardGridGap
                                    rowSpacing: HomeTheme.cardGridGap

                                    Repeater {
                                        model: root.detail && root.detail.players ? root.detail.players : []
                                        Rectangle {
                                            id: playerCard
                                            required property var modelData
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            implicitHeight: playerColumn.implicitHeight + HomeTheme.cardVariantRowPadding * 2
                                            radius: HomeTheme.cardVariantRowRadius
                                            color: HomeTheme.cardVariantFill
                                            border.width: HomeTheme.cardBorderWidth
                                            border.color: playerCard.modelData.winner ? HomeTheme.cardPositive : HomeTheme.cardTileBorder

                                            ColumnLayout {
                                                id: playerColumn
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.margins: HomeTheme.cardVariantRowPadding
                                                spacing: HomeTheme.cardVariantRowGap

                                                RowLayout {
                                                    Layout.fillWidth: true
                                                    spacing: HomeTheme.cardTileTextGap
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: playerCard.modelData.screenName
                                                        elide: Text.ElideRight
                                                        color: HomeTheme.cardTextPrimary
                                                        font.pixelSize: HomeTheme.cardBodyFontSize
                                                        font.bold: true
                                                    }
                                                    Badge {
                                                        text: playerCard.modelData.winner ? qsTr("Win") : qsTr("Lose")
                                                        tone: playerCard.modelData.winner ? HomeTheme.cardBadgePositive
                                                                                          : HomeTheme.cardBadgeNeutral
                                                    }
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: [playerCard.modelData.generals, playerCard.modelData.role,
                                                           playerCard.modelData.alive ? qsTr("Alive") : qsTr("Dead")].join(" · ")
                                                    color: HomeTheme.cardTextSecondary
                                                    font.pixelSize: HomeTheme.cardCaptionFontSize
                                                    wrapMode: Text.Wrap
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: qsTr("Turns %1 · Kills %2 · Damage dealt %3 · Damage taken %4 · Recovered %5")
                                                          .arg(playerCard.modelData.turns).arg(playerCard.modelData.kill)
                                                          .arg(playerCard.modelData.damage).arg(playerCard.modelData.damaged)
                                                          .arg(playerCard.modelData.recover)
                                                    color: HomeTheme.cardTextSecondary
                                                    font.pixelSize: HomeTheme.cardCaptionFontSize
                                                    wrapMode: Text.Wrap
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: text !== ""
                                                    text: playerCard.modelData.designation
                                                    color: HomeTheme.cardAccent
                                                    font.pixelSize: HomeTheme.cardCaptionFontSize
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }
                                }

                                GroupTitle {
                                    visible: chatText.text !== ""
                                    text: qsTr("Chat log")
                                }
                                Text {
                                    id: chatText
                                    Layout.fillWidth: true
                                    visible: text !== ""
                                    text: root.detail && root.detail.chat ? root.detail.chat : ""
                                    textFormat: Text.PlainText
                                    color: HomeTheme.cardTextPrimary
                                    font.pixelSize: HomeTheme.cardBodyFontSize
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.statusText !== ""
                        text: root.statusText
                        color: root.statusFailed ? HomeTheme.cardDanger : HomeTheme.cardPositive
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: root.compact ? 3 : 5
                        columnSpacing: HomeTheme.compactGap
                        rowSpacing: HomeTheme.compactGap

                        HomeMainButton {
                            id: playButton
                            Layout.columnSpan: root.compact ? 3 : 1
                            Layout.fillWidth: root.compact
                            Layout.preferredWidth: root.compact ? -1 : 200
                            implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.settingsFooterButtonHeight
                            compact: true
                            primary: true
                            enabled: root.selected !== null
                            opacity: enabled ? 1 : 0.5
                            text: qsTr("Play")
                            iconSource: "qrc:/QSanguosha/Home/icons/replays.svg"
                            onClicked: root.play()
                        }

                        ActionButton {
                            enabled: root.selected !== null
                            text: qsTr("Convert to %1").arg(root.selected && root.selected.format === "PNG" ? "TXT" : "PNG")
                            onClicked: root.convert()
                        }

                        ActionButton {
                            enabled: root.selected !== null
                            text: qsTr("Rename")
                            onClicked: renameDialog.open()
                        }

                        ActionButton {
                            id: deleteButton
                            enabled: root.selected !== null
                            text: qsTr("Delete")
                            onClicked: deleteDialog.open()
                        }

                        Item { visible: !root.compact; Layout.fillWidth: true }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: HomeTheme.cardPanelFocusInset
                    radius: HomeTheme.cardPanelRadius
                    color: HomeTheme.cardTransparent
                    border.width: root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                    : HomeTheme.cardFocusBorderWidth
                    border.color: HomeTheme.focusBorderHigh
                    visible: detailView.activeFocus
                }
            }
        }
    }

    Popup {
        id: renameDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - HomeTheme.compactMargin * 2 : 520, 520)
        modal: true
        focus: true
        padding: HomeTheme.cardPanelContentPadding
        onOpened: {
            renameError.visible = false
            renameInput.text = root.selected ? root.selected.name : ""
            renameInput.selectAll()
            renameInput.forceActiveFocus()
        }
        onClosed: replayList.forceActiveFocus()
        function accept() {
            if (root.rename(renameInput.text)) close()
            else renameError.visible = true
        }
        background: BASlantedPanel {
            slant: -0.03
            cornerRadius: 10
            topColor: HomeTheme.cardPanelTop
            bottomColor: HomeTheme.cardPanelBottom
            borderColor: HomeTheme.cardPanelBorder
            shadowColor: HomeTheme.baDockShadow
            accentVisible: true
            accentColor: HomeTheme.cardAccent
        }
        contentItem: ColumnLayout {
            spacing: HomeTheme.cardSectionGap
            Text {
                Layout.fillWidth: true
                text: qsTr("Rename replay")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardSectionTitleFontSize
                font.bold: true
            }
            TextField {
                id: renameInput
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                color: HomeTheme.cardTextPrimary
                selectByMouse: true
                Accessible.name: qsTr("New name")
                onAccepted: renameDialog.accept()
                background: Rectangle {
                    radius: HomeTheme.cardControlRadius
                    color: HomeTheme.cardInputFill
                    border.width: renameInput.activeFocus ? HomeTheme.cardSelectedBorderWidth : HomeTheme.cardBorderWidth
                    border.color: renameInput.activeFocus ? HomeTheme.focusBorderHigh : HomeTheme.cardPanelBorder
                }
            }
            Text {
                id: renameError
                Layout.fillWidth: true
                visible: false
                text: qsTr("Cannot rename: the name is empty, contains one of %1, or matches an existing replay.")
                      .arg("\\ / : * ? \" < > |")
                color: HomeTheme.cardDanger
                font.pixelSize: HomeTheme.cardCaptionFontSize
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: HomeTheme.compactGap
                Item { Layout.fillWidth: true }
                BAToolButton {
                    text: qsTr("Cancel")
                    onClicked: renameDialog.close()
                }
                BAToolButton {
                    text: qsTr("OK")
                    onClicked: renameDialog.accept()
                }
            }
        }
    }

    Popup {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - HomeTheme.compactMargin * 2 : 520, 520)
        modal: true
        focus: true
        padding: HomeTheme.cardPanelContentPadding
        onOpened: cancelDeleteButton.forceActiveFocus()
        onClosed: replayList.forceActiveFocus()
        background: BASlantedPanel {
            slant: -0.03
            cornerRadius: 10
            topColor: HomeTheme.cardPanelTop
            bottomColor: HomeTheme.cardPanelBottom
            borderColor: HomeTheme.cardPanelBorder
            shadowColor: HomeTheme.baDockShadow
            accentVisible: true
            accentColor: HomeTheme.cardDanger
        }
        contentItem: ColumnLayout {
            spacing: HomeTheme.cardSectionGap
            Text {
                Layout.fillWidth: true
                text: qsTr("Delete replay")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardSectionTitleFontSize
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: root.selected
                      ? (root.selected.format === "TXT"
                         ? qsTr("Move %1 and its takeover snapshots, if any, to the recycle bin. Without a recycle bin they are deleted permanently.").arg(root.selected.name)
                         : qsTr("Move %1 to the recycle bin. Without a recycle bin it is deleted permanently.").arg(root.selected.name))
                      : ""
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardBodyFontSize
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: HomeTheme.compactGap
                Item { Layout.fillWidth: true }
                BAToolButton {
                    id: cancelDeleteButton
                    text: qsTr("Cancel")
                    onClicked: deleteDialog.close()
                }
                BAToolButton {
                    text: qsTr("Delete")
                    onClicked: {
                        deleteDialog.close()
                        root.remove()
                    }
                }
            }
        }
    }
}
