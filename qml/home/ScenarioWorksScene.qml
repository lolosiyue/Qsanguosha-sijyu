pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

// The works page shares C++ ScenarioWorkLibrary with the legacy library dialog. The work editor,
// file pickers and refusal messages stay modal dialogs opened from this page.
Item {
    id: root
    objectName: "scenarioWorksScene"

    property bool compact: Config.responsiveUiEnabled && (width < 900 || height > width)
    // Portrait shows the list or the details pane, like the card catalog.
    property int compactPane: 0
    readonly property bool highContrast: homeController.visualMode === "highcontrast"
    readonly property var works: scenarioWorkLibrary.works
    property string selectedPath: ""
    property var work: ({})
    property int entryIndex: -1
    // A model swap resets the list's current index; ignore it while refresh() swaps the model.
    property bool syncing: false
    readonly property var entries: work.entries || []
    readonly property var entry: entryIndex >= 0 && entryIndex < entries.length ? entries[entryIndex] : null
    readonly property bool playable: work.compatibilityError === "" && work.progressError === ""
    readonly property var navigationEntry: !compact ? backButton
                                           : compactPane === 0 ? newSceneButton : compactBar.listButton
    readonly property var lastControl: compact && compactPane === 0 ? workList : playButton
    signal navigationEndpointChanged()

    focus: true
    Keys.onEscapePressed: {
        if (compact && compactPane !== 0) showCompactPane(0)
        else homeController.openHome()
    }
    onNavigationEntryChanged: navigationEndpointChanged()
    onLastControlChanged: navigationEndpointChanged()
    onWorksChanged: refresh()
    Component.onCompleted: refresh()

    function takeKeyboard() {
        if (compact && compactPane === 1) compactBar.detailButton.forceActiveFocus()
        else if (workList.count > 0) workList.forceActiveFocus()
        else newSceneButton.forceActiveFocus()
    }

    function showCompactPane(pane) {
        compactPane = pane
        Qt.callLater(function() {
            if (pane === 0) workList.forceActiveFocus()
            else (playButton.enabled ? playButton : compactBar.detailButton).forceActiveFocus()
        })
    }

    function indexOfPath(path) {
        for (var i = 0; i < works.length; ++i) {
            if (works[i].path === path)
                return i
        }
        return -1
    }

    function refresh() {
        var path = indexOfPath(selectedPath) >= 0 ? selectedPath : works.length > 0 ? works[0].path : ""
        syncing = true
        workList.model = works
        syncing = false
        select(path)
    }

    function select(path) {
        var entryId = path === selectedPath && entry ? entry.id : ""
        selectedPath = path
        workList.currentIndex = indexOfPath(path)
        work = path !== "" ? scenarioWorkLibrary.details(path) : ({})
        // Keep the chosen entry across a reload; otherwise prefer the continuation, then the first open entry.
        var index = -1
        for (var i = 0; i < entries.length && index < 0; ++i) {
            if (entries[i].id === entryId)
                index = i
        }
        for (i = 0; i < entries.length && index < 0; ++i) {
            if (entries[i].continuation)
                index = i
        }
        for (i = 0; i < entries.length && index < 0; ++i) {
            if (!entries[i].locked)
                index = i
        }
        selectEntry(index < 0 && entries.length > 0 ? 0 : index)
    }

    // Swapping the entry swaps the state model, so set the default index after it.
    function selectEntry(index) {
        entryIndex = index
        stateChoice.currentIndex = entry ? entry.defaultState : -1
    }

    // The library opens modal dialogs; let the click handler return first.
    function later(action) {
        Qt.callLater(action)
    }

    Connections {
        target: scenarioWorkLibrary
        function onWorkWritten(path) { root.select(path) }
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

    component SectionTitle: Text {
        Layout.fillWidth: true
        Layout.topMargin: HomeTheme.settingsRowGap
        Accessible.role: Accessible.Heading
        color: HomeTheme.cardAccent
        font.pixelSize: HomeTheme.cardDetailSectionFontSize
        font.bold: true
        wrapMode: Text.Wrap
    }

    component BodyText: Text {
        Layout.fillWidth: true
        visible: text !== ""
        color: HomeTheme.cardTextSecondary
        font.pixelSize: HomeTheme.cardBodyFontSize
        wrapMode: Text.Wrap
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
                columns: root.compact ? 3 : 5
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
                        text: qsTranslate("HomeScene", "Scenario Works")
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: root.compact ? HomeTheme.cardSectionTitleFontSize : HomeTheme.cardTitleFontSize
                        font.bold: true
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTranslate("ScenarioWorkLibraryDialog", "Work library")
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardCaptionFontSize
                    }
                }

                ActionButton {
                    id: newSceneButton
                    visible: !root.compact || root.compactPane === 0
                    text: qsTranslate("ScenarioWorkLibraryDialog", "New scene work")
                    onClicked: root.later(function() { scenarioWorkLibrary.newSceneWork() })
                }

                ActionButton {
                    visible: !root.compact || root.compactPane === 0
                    text: qsTranslate("ScenarioWorkLibraryDialog", "New stage work")
                    onClicked: root.later(function() { scenarioWorkLibrary.newStageWork() })
                }

                ActionButton {
                    visible: !root.compact || root.compactPane === 0
                    text: qsTranslate("ScenarioWorkLibraryDialog", "Import")
                    onClicked: root.later(function() { scenarioWorkLibrary.importWork() })
                }
            }
        }

        CatalogPaneBar {
            id: compactBar
            Layout.fillWidth: true
            visible: root.compact
            currentIndex: root.compactPane
            itemCount: root.works.length
            detailsEnabled: root.work.path !== undefined
            filterButton.visible: false
            onActivated: function(index) { root.showCompactPane(index) }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: root.compact ? 1 : 2
            columnSpacing: HomeTheme.cardPanelGap
            rowSpacing: HomeTheme.compactGap

            BASlantedPanel {
                visible: !root.compact || root.compactPane === 0
                Layout.fillWidth: root.compact
                Layout.fillHeight: true
                Layout.preferredWidth: root.compact ? -1 : Math.round(root.width * 0.34)
                slant: 0
                cornerRadius: HomeTheme.cardPanelRadius
                shadowBlur: 0
                shadowOffset: 0
                topColor: HomeTheme.cardPanelTop
                bottomColor: HomeTheme.cardPanelBottom
                borderColor: HomeTheme.cardPanelBorder
                shadowColor: HomeTheme.baDockShadow

                ListView {
                    id: workList
                    anchors.fill: parent
                    anchors.margins: root.compact ? HomeTheme.compactGap : HomeTheme.cardGridGap
                    clip: true
                    spacing: HomeTheme.cardTileTextGap
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    boundsBehavior: Flickable.StopAtBounds
                    highlightMoveDuration: 0
                    Accessible.role: Accessible.List
                    Accessible.name: qsTranslate("ScenarioWorkLibraryDialog", "Work library")
                    ScrollBar.vertical: HomeScrollBar { }
                    Keys.onReturnPressed: if (root.compact) root.showCompactPane(1)
                    Keys.onEnterPressed: if (root.compact) root.showCompactPane(1)
                    onCurrentIndexChanged: {
                        if (!root.syncing && currentIndex >= 0 && currentIndex < root.works.length
                                && root.works[currentIndex].path !== root.selectedPath)
                            root.select(root.works[currentIndex].path)
                    }

                    delegate: Rectangle {
                        id: workRow
                        required property var modelData
                        required property int index
                        readonly property bool current: modelData.path === root.selectedPath
                        width: ListView.view.width - HomeTheme.cardGridGap
                        height: workColumn.implicitHeight + HomeTheme.cardControlHPadding * 2
                        radius: HomeTheme.cardControlRadius
                        color: current ? HomeTheme.cardTileSelected
                             : workMouse.containsMouse ? HomeTheme.cardTileHover : HomeTheme.cardTileFill
                        border.width: current && workList.activeFocus
                                      ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                           : HomeTheme.cardFocusBorderWidth)
                                      : current ? HomeTheme.cardSelectedBorderWidth : HomeTheme.cardBorderWidth
                        border.color: current && workList.activeFocus ? HomeTheme.focusBorderHigh
                                    : current ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder
                        Accessible.role: Accessible.ListItem
                        Accessible.name: modelData.title

                        ColumnLayout {
                            id: workColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: HomeTheme.cardControlHPadding
                            anchors.rightMargin: HomeTheme.cardControlHPadding
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: HomeTheme.cardTileTextGap
                                Text {
                                    Layout.fillWidth: true
                                    text: workRow.modelData.title
                                    elide: Text.ElideRight
                                    color: HomeTheme.cardTextPrimary
                                    font.pixelSize: HomeTheme.cardTileTitleFontSize
                                    font.bold: true
                                }
                                Badge { text: workRow.modelData.kind }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(workRow.modelData.revision).slice(0, 12)
                                color: HomeTheme.cardTextMuted
                                font.pixelSize: HomeTheme.cardMetaFontSize
                            }
                        }

                        MouseArea {
                            id: workMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.select(workRow.modelData.path)
                                if (root.compact) root.showCompactPane(1)
                                else workList.forceActiveFocus()
                            }
                            onDoubleClicked: root.later(function() { scenarioWorkLibrary.editWork(root.selectedPath) })
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - HomeTheme.cardPanelContentPadding * 2
                    visible: workList.count === 0
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No works in the library.")
                    color: HomeTheme.cardTextMuted
                    font.pixelSize: HomeTheme.cardEmptyFontSize
                    wrapMode: Text.Wrap
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

                Text {
                    anchors.centerIn: parent
                    visible: root.selectedPath === "" || root.work.path === undefined
                    text: qsTr("Select a work to view its details.")
                    color: HomeTheme.cardTextMuted
                    font.pixelSize: HomeTheme.cardEmptyFontSize
                }

                Flickable {
                    id: detailView
                    anchors.fill: parent
                    anchors.margins: root.compact ? HomeTheme.compactMargin : HomeTheme.cardPanelContentPadding
                    visible: root.work.path !== undefined
                    contentWidth: width
                    contentHeight: detail.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: HomeScrollBar { }

                    ColumnLayout {
                        id: detail
                        width: detailView.width - HomeTheme.cardGridGap
                        spacing: HomeTheme.cardDetailMetaGap

                        Text {
                            Layout.fillWidth: true
                            text: root.work.title || ""
                            color: HomeTheme.cardTextPrimary
                            font.pixelSize: HomeTheme.cardDetailNameFontSize
                            font.bold: true
                            wrapMode: Text.Wrap
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: HomeTheme.cardTileTextGap
                            Badge { text: root.work.kind || "" }
                            Text {
                                height: HomeTheme.cardBadgeHeight
                                verticalAlignment: Text.AlignVCenter
                                text: qsTr("Author: %1").arg(root.work.author || "")
                                color: HomeTheme.cardTextSecondary
                                font.pixelSize: HomeTheme.cardBodyFontSize
                            }
                            Text {
                                height: HomeTheme.cardBadgeHeight
                                verticalAlignment: Text.AlignVCenter
                                text: String(root.work.revision || "").slice(0, 12)
                                color: HomeTheme.cardTextMuted
                                font.pixelSize: HomeTheme.cardMetaFontSize
                            }
                        }

                        BodyText {
                            text: root.work.intro || ""
                            color: HomeTheme.cardTextPrimary
                            font.pixelSize: HomeTheme.settingsFontSize
                        }

                        BodyText {
                            text: qsTranslate("ScenarioWorkLibraryDialog", "Rule: %1\nRequired extensions: %2")
                                  .arg(root.work.rule || "").arg(root.work.extensions || "")
                        }

                        SectionTitle {
                            visible: !!root.work.compatibilityError
                            text: qsTranslate("ScenarioWorkLibrary", "Incompatible work")
                            color: HomeTheme.cardWarning
                        }
                        BodyText {
                            text: root.work.compatibilityError || ""
                            color: HomeTheme.cardWarning
                            wrapMode: Text.WrapAnywhere
                        }

                        SectionTitle {
                            visible: !!root.work.progressError
                            text: qsTranslate("ScenarioWorkLibraryDialog", "Cannot read progress")
                            color: HomeTheme.cardDanger
                        }
                        BodyText {
                            text: root.work.progressError || ""
                            color: HomeTheme.cardDanger
                        }

                        SectionTitle { text: qsTranslate("ScenarioWorkEditorDialog", "Ordered stage entries") }

                        Repeater {
                            model: root.entries

                            AbstractButton {
                                id: entryTile
                                required property var modelData
                                required property int index
                                readonly property bool current: index === root.entryIndex
                                Layout.fillWidth: true
                                implicitHeight: entryColumn.implicitHeight + HomeTheme.cardVariantRowPadding * 2
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                Accessible.role: Accessible.RadioButton
                                Accessible.checked: current
                                Accessible.name: modelData.title
                                onClicked: root.selectEntry(index)
                                onDoubleClicked: if (root.playable && !modelData.locked) playButton.clicked()
                                onActiveFocusChanged: {
                                    if (!activeFocus)
                                        return
                                    var y = mapToItem(detailView.contentItem, 0, 0).y
                                    if (y < detailView.contentY)
                                        detailView.contentY = Math.max(0, y)
                                    else if (y + height > detailView.contentY + detailView.height)
                                        detailView.contentY = y + height - detailView.height
                                }
                                Keys.onShortcutOverride: function(event) {
                                    if (event.key === Qt.Key_Space)
                                        event.accepted = true
                                }

                                background: Rectangle {
                                    radius: HomeTheme.cardVariantRowRadius
                                    color: entryTile.current ? HomeTheme.cardTileSelected
                                         : entryTile.hovered ? HomeTheme.cardTileHover : HomeTheme.cardTileFill
                                    border.width: entryTile.activeFocus
                                                  ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                                       : HomeTheme.cardFocusBorderWidth)
                                                  : entryTile.current ? HomeTheme.cardSelectedBorderWidth
                                                                      : HomeTheme.cardBorderWidth
                                    border.color: entryTile.activeFocus ? HomeTheme.focusBorderHigh
                                                : entryTile.current ? HomeTheme.cardInteractive
                                                                    : HomeTheme.cardTileBorder
                                }

                                contentItem: Item {
                                    ColumnLayout {
                                        id: entryColumn
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: HomeTheme.cardVariantRowPadding
                                        anchors.rightMargin: HomeTheme.cardVariantRowPadding
                                        spacing: 4

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: HomeTheme.cardTileTextGap
                                            Text {
                                                Layout.fillWidth: true
                                                text: (entryTile.index + 1) + ". " + entryTile.modelData.title
                                                color: entryTile.modelData.locked ? HomeTheme.cardTextMuted
                                                                                  : HomeTheme.cardTextPrimary
                                                font.pixelSize: HomeTheme.cardDetailTypeFontSize
                                                font.bold: true
                                                wrapMode: Text.Wrap
                                            }
                                            Badge {
                                                visible: entryTile.modelData.continuation
                                                text: qsTr("Continuation")
                                                tone: HomeTheme.cardBadgePositive
                                            }
                                            Badge {
                                                visible: entryTile.modelData.locked
                                                text: qsTr("Locked")
                                                tone: HomeTheme.cardBadgeNeutral
                                            }
                                        }
                                        BodyText { text: entryTile.modelData.intro }
                                        BodyText {
                                            text: entryTile.modelData.sceneIntro
                                            color: HomeTheme.cardTextMuted
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: HomeTheme.settingsRowGap
                            visible: stateChoice.visible
                            text: qsTranslate("ScenarioWorkLibraryDialog", "Choose the initial state for this entry")
                            color: HomeTheme.cardTextSecondary
                            font.pixelSize: HomeTheme.cardBodyFontSize
                            wrapMode: Text.Wrap
                        }
                        CardComboBox {
                            id: stateChoice
                            Layout.fillWidth: root.compact
                            Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsChoiceWidth * 1.5
                            implicitHeight: root.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight
                            visible: root.entry !== null && !root.entry.locked && root.playable
                            model: root.entry ? root.entry.states : []
                            accessibleLabel: qsTranslate("ScenarioWorkLibraryDialog", "Choose the initial state for this entry")
                        }
                    }
                }
            }
        }

        GridLayout {
            visible: !root.compact || root.compactPane === 1
            Layout.fillWidth: true
            columns: root.compact ? 4 : 6
            columnSpacing: root.compact ? HomeTheme.compactGap : HomeTheme.cardPanelGap
            rowSpacing: HomeTheme.compactGap

            ActionButton {
                enabled: root.selectedPath !== ""
                text: qsTranslate("ScenarioWorkLibraryDialog", "Edit")
                onClicked: root.later(function() { scenarioWorkLibrary.editWork(root.selectedPath) })
            }
            ActionButton {
                enabled: root.selectedPath !== ""
                text: qsTranslate("ScenarioWorkLibraryDialog", "Duplicate")
                onClicked: root.later(function() { scenarioWorkLibrary.duplicateWork(root.selectedPath) })
            }
            ActionButton {
                enabled: root.selectedPath !== ""
                text: qsTranslate("ScenarioWorkLibraryDialog", "Export")
                onClicked: root.later(function() { scenarioWorkLibrary.exportWork(root.selectedPath) })
            }

            ActionButton {
                enabled: root.playable && root.work.canContinue === true
                text: qsTranslate("ScenarioWorkLibraryDialog", "Continue")
                onClicked: root.later(function() { scenarioWorkLibrary.continueWork(root.selectedPath) })
            }

            Item {
                visible: !root.compact
                Layout.fillWidth: true
            }

            HomeMainButton {
                id: playButton
                Layout.columnSpan: root.compact ? 4 : 1
                Layout.fillWidth: root.compact
                Layout.preferredWidth: root.compact ? -1 : HomeTheme.settingsFooterButtonWidth
                implicitHeight: HomeTheme.settingsFooterButtonHeight
                compact: true
                opacity: enabled ? 1 : 0.5
                enabled: root.playable && root.entry !== null && !root.entry.locked
                primary: true
                text: qsTranslate("ScenarioWorkLibraryDialog", "Play")
                iconSource: "qrc:/QSanguosha/Home/icons/quick-join.svg"
                onClicked: {
                    var path = root.selectedPath
                    var entryId = root.entry.id
                    var state = stateChoice.currentIndex
                    root.later(function() { scenarioWorkLibrary.playEntry(path, entryId, state) })
                }
            }
        }
    }
}
