import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

pragma ComponentBehavior: Bound

Item {
    id: root

    property var cardModel
    property bool compact: false
    property string sortKey: "engine"
    property string typeKey: "all"
    property string suitKey: "all"
    property var selectedTagKeys: []
    property alias searchField: searchInput
    property alias kindControl: kindBox
    property alias packageControl: packageBox
    property alias resetButton: resetButton
    readonly property var firstTag: tagRepeater.count > 0 ? tagRepeater.itemAt(0) : null
    readonly property var lastTag: tagRepeater.count > 0 ? tagRepeater.itemAt(tagRepeater.count - 1) : null
    signal filtersChanged(var filters)
    signal navigationChanged()

    // Single-select facet with few options: one chip each, so a pick is one click instead of a popup.
    component FacetChips: Flow {
        id: facet
        property var options: []
        property string currentKey: "all"
        property string accessibleLabel: ""
        property Item tabTarget: null
        property Item backtabTarget: null
        readonly property Item firstChip: chipRepeater.count > 0 ? chipRepeater.itemAt(0) : null
        readonly property Item lastChip: chipRepeater.count > 0 ? chipRepeater.itemAt(chipRepeater.count - 1) : null
        signal picked(string key)

        spacing: HomeTheme.cardTagGap

        Repeater {
            id: chipRepeater
            model: facet.options

            Rectangle {
                id: chip
                required property int index
                required property var modelData
                readonly property bool checked: facet.currentKey === String(modelData.key)
                readonly property Item nextChip: index + 1 < chipRepeater.count ? chipRepeater.itemAt(index + 1) : facet.tabTarget
                readonly property Item previousChip: index > 0 ? chipRepeater.itemAt(index - 1) : facet.backtabTarget

                function pick() {
                    facet.picked(String(modelData.key))
                }

                width: chipLabel.implicitWidth + HomeTheme.cardTagHPadding * 2
                // These chips replaced 42 px combo boxes; keep a full touch target in portrait.
                height: root.compact ? HomeTheme.compactTouch : HomeTheme.cardTagHeight
                radius: height / 2
                color: checked ? HomeTheme.cardTagChecked
                               : (chipPointer.containsMouse ? HomeTheme.cardTagHover : HomeTheme.cardTagFill)
                border.width: activeFocus
                              ? (homeController.visualMode === "highcontrast"
                                 ? HomeTheme.cardHighContrastFocusBorderWidth
                                 : HomeTheme.cardFocusBorderWidth)
                              : (checked ? HomeTheme.cardSelectedBorderWidth : HomeTheme.cardBorderWidth)
                border.color: activeFocus ? HomeTheme.focusBorderHigh
                                          : (checked ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder)
                activeFocusOnTab: true
                Accessible.role: Accessible.RadioButton
                Accessible.name: facet.accessibleLabel + ": " + chipLabel.text
                Accessible.checked: checked
                KeyNavigation.tab: nextChip
                KeyNavigation.right: nextChip
                KeyNavigation.backtab: previousChip
                KeyNavigation.left: previousChip
                Keys.onReturnPressed: chip.pick()
                Keys.onEnterPressed: chip.pick()
                Keys.onSpacePressed: function(event) {
                    chip.pick()
                    event.accepted = true
                }
                onActiveFocusChanged: if (activeFocus) root.revealItem(chip)

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: String(chip.modelData.label)
                    color: chip.checked ? HomeTheme.cardBadgeText : HomeTheme.cardTextPrimary
                    font.pixelSize: HomeTheme.cardControlFontSize
                    font.bold: chip.checked
                }

                MouseArea {
                    id: chipPointer
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        chip.forceActiveFocus()
                        chip.pick()
                    }
                }
            }
        }
    }

    function applyNow() {
        filtersChanged({
            "query": searchInput.text,
            "type": root.typeKey,
            "kind": kindBox.currentValue || "all",
            "suit": root.suitKey,
            "package": packageBox.currentValue || "all",
            "tags": selectedTagKeys,
            "sort": root.sortKey
        })
    }

    function reset() {
        searchInput.clear()
        typeKey = "all"
        kindBox.currentIndex = 0
        suitKey = "all"
        packageBox.currentIndex = 0
        selectedTagKeys = []
        applyNow()
    }

    function toggleTag(tagKey, checked) {
        var next = selectedTagKeys.slice()
        var index = next.indexOf(tagKey)
        if (checked && index < 0)
            next.push(tagKey)
        else if (!checked && index >= 0)
            next.splice(index, 1)
        selectedTagKeys = next
        applyNow()
    }

    function revealItem(item) {
        if (!item)
            return
        var point = item.mapToItem(filterFlick.contentItem, 0, 0)
        var top = point.y - HomeTheme.cardSectionGap
        var bottom = point.y + item.height + HomeTheme.cardSectionGap
        if (top < filterFlick.contentY)
            filterFlick.contentY = Math.max(0, top)
        else if (bottom > filterFlick.contentY + filterFlick.height)
            filterFlick.contentY = Math.min(
                        Math.max(0, filterFlick.contentHeight - filterFlick.height),
                        bottom - filterFlick.height)
    }

    function applyLocalNavGraph() {
        var typeFirst = typeFacet.firstChip || kindBox
        var typeLast = typeFacet.lastChip || searchInput
        var suitFirst = suitFacet.firstChip || packageBox
        var suitLast = suitFacet.lastChip || kindBox
        searchInput.KeyNavigation.tab = typeFirst
        kindBox.backtabTarget = typeLast
        kindBox.tabTarget = suitFirst
        packageBox.backtabTarget = suitLast
        packageBox.tabTarget = firstTag || resetButton
        resetButton.KeyNavigation.backtab = lastTag || packageBox

        searchInput.KeyNavigation.down = typeFirst
        kindBox.KeyNavigation.up = typeFirst
        kindBox.leftTarget = typeLast
        kindBox.rightTarget = suitFirst
        packageBox.KeyNavigation.up = suitFirst
        packageBox.leftTarget = suitLast
        packageBox.rightTarget = firstTag || resetButton
        resetButton.KeyNavigation.up = lastTag || packageBox
        resetButton.KeyNavigation.left = lastTag || packageBox
    }

    onFirstTagChanged: Qt.callLater(applyLocalNavGraph)
    onLastTagChanged: Qt.callLater(applyLocalNavGraph)
    Component.onCompleted: Qt.callLater(applyLocalNavGraph)

    Timer {
        id: searchDebounce
        interval: 130
        repeat: false
        onTriggered: root.applyNow()
    }

    BASlantedPanel {
        anchors.fill: parent
        slant: 0
        cornerRadius: HomeTheme.cardPanelRadius
        shadowBlur: 0
        shadowOffset: 0
        topColor: HomeTheme.cardPanelTop
        bottomColor: HomeTheme.cardPanelBottom
        borderColor: HomeTheme.cardPanelBorder
        shadowColor: HomeTheme.baDockShadow
    }

    Flickable {
        id: filterFlick
        anchors.fill: parent
        anchors.margins: HomeTheme.cardPanelContentPadding
        contentHeight: form.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        ColumnLayout {
            id: form
            width: parent.width
            spacing: HomeTheme.cardSectionGap

            Text {
                text: homeController.qtTranslate("CardScene", "Filter cards")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardSectionTitleFontSize
                font.bold: true
            }

            TextField {
                id: searchInput
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                activeFocusOnTab: true
                placeholderText: homeController.qtTranslate("CardScene", "Name, effect, or package")
                color: HomeTheme.cardTextPrimary
                placeholderTextColor: HomeTheme.cardTextMuted
                selectByMouse: true
                Accessible.name: homeController.qtTranslate("CardScene", "Search cards")
                onTextChanged: searchDebounce.restart()
                onActiveFocusChanged: if (activeFocus) root.revealItem(searchInput)
                // Focus on open; disable the blinking cursor because each blink repaints the whole QQuickWidget.
                cursorDelegate: Rectangle {
                    width: 2
                    color: searchInput.color
                    visible: searchInput.cursorVisible
                }
                background: Rectangle {
                    radius: HomeTheme.cardControlRadius
                    color: HomeTheme.cardInputFill
                    border.width: searchInput.activeFocus
                                  ? (homeController.visualMode === "highcontrast"
                                     ? HomeTheme.cardHighContrastFocusBorderWidth
                                     : HomeTheme.cardSelectedBorderWidth)
                                  : HomeTheme.cardBorderWidth
                    border.color: searchInput.activeFocus ? HomeTheme.focusBorderHigh
                                                               : HomeTheme.cardPanelBorder
                }
            }

            Text { text: homeController.qtTranslate("CardScene", "Type"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            FacetChips {
                id: typeFacet
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                options: root.cardModel ? root.cardModel.typeOptions : []
                currentKey: root.typeKey
                accessibleLabel: homeController.qtTranslate("CardScene", "Type")
                tabTarget: kindBox
                backtabTarget: searchInput
                onPicked: function(key) {
                    root.typeKey = key
                    root.applyNow()
                }
                onFirstChipChanged: Qt.callLater(root.applyLocalNavGraph)
                onLastChipChanged: Qt.callLater(root.applyLocalNavGraph)
            }

            Text { text: homeController.qtTranslate("CardScene", "Kind"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: kindBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.kindOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Kind")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(kindBox)
            }

            Text { text: homeController.qtTranslate("CardScene", "Suit"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            FacetChips {
                id: suitFacet
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                options: root.cardModel ? root.cardModel.suitOptions : []
                currentKey: root.suitKey
                accessibleLabel: homeController.qtTranslate("CardScene", "Suit")
                tabTarget: packageBox
                backtabTarget: kindBox
                onPicked: function(key) {
                    root.suitKey = key
                    root.applyNow()
                }
                onFirstChipChanged: Qt.callLater(root.applyLocalNavGraph)
                onLastChipChanged: Qt.callLater(root.applyLocalNavGraph)
            }

            Text { text: homeController.qtTranslate("CardScene", "Package"); color: HomeTheme.cardTextSecondary; font.pixelSize: HomeTheme.cardCaptionFontSize }
            CardComboBox {
                id: packageBox
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardControlHeight
                model: root.cardModel ? root.cardModel.packageOptions : []
                textRole: "label"
                valueRole: "key"
                accessibleLabel: homeController.qtTranslate("CardScene", "Package")
                activeFocusOnTab: true
                onActivated: root.applyNow()
                onActiveFocusChanged: if (activeFocus) root.revealItem(packageBox)
            }

            Text {
                text: homeController.qtTranslate("CardScene", "Tags")
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardCaptionFontSize
            }

            Flow {
                id: tagFlow
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: HomeTheme.cardTagGap

                Repeater {
                    id: tagRepeater
                    model: root.cardModel ? root.cardModel.tagOptions : []
                    onItemAdded: Qt.callLater(root.navigationChanged)
                    onItemRemoved: Qt.callLater(root.navigationChanged)

                    CardTagChip {
                        required property int index
                        required property var modelData
                        tagKey: modelData.key
                        text: modelData.label
                        count: modelData.count
                        checked: root.selectedTagKeys.indexOf(tagKey) >= 0
                        KeyNavigation.left: index > 0 ? tagRepeater.itemAt(index - 1) : packageBox
                        KeyNavigation.right: index + 1 < tagRepeater.count
                                             ? tagRepeater.itemAt(index + 1) : resetButton
                        KeyNavigation.up: packageBox
                        KeyNavigation.down: resetButton
                        KeyNavigation.tab: index + 1 < tagRepeater.count
                                           ? tagRepeater.itemAt(index + 1) : resetButton
                        KeyNavigation.backtab: index > 0
                                               ? tagRepeater.itemAt(index - 1) : packageBox
                        onToggled: function(key, checkedValue) {
                            root.toggleTag(key, checkedValue)
                        }
                        onActiveFocusChanged: if (activeFocus) root.revealItem(this)
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardDividerHeight
                Layout.topMargin: HomeTheme.cardFilterDividerTopMargin
                color: HomeTheme.cardPanelBorder
            }

            Text {
                Layout.fillWidth: true
                text: homeController.qtTranslate("CardScene", "%1 card types found")
                      .arg(root.cardModel ? root.cardModel.filteredCount : 0)
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardBodyFontSize
            }

            BAToolButton {
                id: resetButton
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardFilterResetHeight
                text: homeController.qtTranslate("CardScene", "Reset filters")
                onClicked: root.reset()
                onActiveFocusChanged: if (activeFocus) root.revealItem(resetButton)
            }
        }
    }
}
