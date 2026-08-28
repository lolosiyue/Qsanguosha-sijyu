import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

pragma ComponentBehavior: Bound

Item {
    id: root

    property var cardModel
    property var detail: ({})
    readonly property int cardId: detail && detail.cardId !== undefined ? detail.cardId : -1
    readonly property var firstVisibleAction: cardId >= 0 ? detailScroll : null
    readonly property var lastVisibleAction: effectAudio.visible ? effectAudio
                                             : (femaleAudio.visible ? femaleAudio
                                                                    : (maleAudio.visible ? maleAudio
                                                                                         : (cardId >= 0
                                                                                            ? effectText
                                                                                            : firstVisibleAction)))
    readonly property var visibleActions: {
        var actions = []
        if (cardId >= 0)
            actions.push(detailScroll)
        if (cardId >= 0)
            actions.push(effectText)
        if (maleAudio.visible)
            actions.push(maleAudio)
        if (femaleAudio.visible)
            actions.push(femaleAudio)
        if (effectAudio.visible)
            actions.push(effectAudio)
        return actions
    }

    function setScrollPosition(position) {
        detailScrollBar.position = Math.max(
                    0, Math.min(1 - detailScrollBar.size, position))
    }

    function revealItem(item) {
        if (!item || contentColumn.height <= 0)
            return
        var point = item.mapToItem(contentColumn, 0, 0)
        var top = Math.max(0, point.y - HomeTheme.cardSectionGap)
                    / contentColumn.height
        var bottom = Math.min(contentColumn.height,
                              point.y + item.height + HomeTheme.cardSectionGap)
                     / contentColumn.height
        if (top < detailScrollBar.position)
            root.setScrollPosition(top)
        else if (bottom > detailScrollBar.position + detailScrollBar.size)
            root.setScrollPosition(bottom - detailScrollBar.size)
    }

    onCardIdChanged: Qt.callLater(function() { root.setScrollPosition(0) })

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
        accentVisible: root.cardId >= 0
        accentColor: HomeTheme.cardAccent
    }

    ScrollView {
        id: detailScroll

        anchors.fill: parent
        anchors.margins: HomeTheme.cardPanelContentPadding
        clip: true
        contentWidth: availableWidth
        activeFocusOnTab: root.cardId >= 0
        Accessible.role: Accessible.Pane
        Accessible.name: homeController.qtTranslate("CardScene", "Card details")
        Accessible.focusable: root.cardId >= 0

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Up) {
                detailScrollBar.decrease()
            } else if (event.key === Qt.Key_Down) {
                detailScrollBar.increase()
            } else if (event.key === Qt.Key_PageUp) {
                root.setScrollPosition(detailScrollBar.position - detailScrollBar.size)
            } else if (event.key === Qt.Key_PageDown) {
                root.setScrollPosition(detailScrollBar.position + detailScrollBar.size)
            } else if (event.key === Qt.Key_Home) {
                root.setScrollPosition(0)
            } else if (event.key === Qt.Key_End) {
                root.setScrollPosition(1)
            } else {
                return
            }
            event.accepted = true
        }

        ScrollBar.vertical: HomeScrollBar {
            id: detailScrollBar
            policy: ScrollBar.AsNeeded
            stepSize: 0.12
        }

        ColumnLayout {
            id: contentColumn

            width: parent.width
            spacing: HomeTheme.cardSectionGap

            Text {
                visible: root.cardId < 0
                Layout.fillWidth: true
                Layout.topMargin: HomeTheme.cardDetailEmptyTopMargin
                horizontalAlignment: Text.AlignHCenter
                text: homeController.qtTranslate("CardScene", "Select a card to view its details")
                color: HomeTheme.cardTextMuted
                font.pixelSize: HomeTheme.cardBodyFontSize + 2
                wrapMode: Text.WordWrap
            }

            RowLayout {
                visible: root.cardId >= 0
                Layout.fillWidth: true
                spacing: HomeTheme.cardDetailHeroGap

                Rectangle {
                    Layout.preferredWidth: HomeTheme.cardDetailImageWidth
                    Layout.preferredHeight: HomeTheme.cardDetailImageHeight
                    radius: HomeTheme.cardControlRadius
                    color: HomeTheme.cardPanelInner
                    border.width: HomeTheme.cardBorderWidth
                    border.color: HomeTheme.cardPanelBorder

                    Image {
                        anchors.fill: parent
                        anchors.margins: HomeTheme.cardDetailImageInset
                        source: root.detail.imageUrl || ""
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: Math.ceil(width)
                        sourceSize.height: Math.ceil(height)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: HomeTheme.cardDetailMetaGap

                    Text {
                        Layout.fillWidth: true
                        text: root.detail.overviewDisplayName || ""
                        color: HomeTheme.cardTextPrimary
                        font.pixelSize: HomeTheme.cardDetailNameFontSize
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: homeController.qtTranslate("CardScene", "%1 physical cards · %2 variants")
                              .arg(root.detail.physicalCount || 0)
                              .arg(root.detail.variantCount || 0)
                        color: HomeTheme.cardInteractive
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.detail.typeDisplay || ""
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardDetailTypeFontSize
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.detail.kindDisplay || ""
                        color: HomeTheme.cardInteractive
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.detail.packageSummary || ""
                        color: HomeTheme.cardTextSecondary
                        font.pixelSize: HomeTheme.cardBodyFontSize
                        elide: Text.ElideRight
                    }
                }
            }

            Flow {
                visible: root.cardId >= 0 && root.detail.tagLabels
                         && root.detail.tagLabels.length > 0
                Layout.fillWidth: true
                spacing: HomeTheme.cardDetailMetaGap

                Repeater {
                    model: root.detail.tagLabels || []
                    Rectangle {
                        id: tagBadge
                        required property string modelData

                        width: flagText.implicitWidth + HomeTheme.cardBadgeHPadding * 2
                        height: HomeTheme.cardDetailFlagHeight
                        radius: HomeTheme.cardDetailFlagRadius
                        color: HomeTheme.cardInteractiveSoft
                        border.width: HomeTheme.cardBorderWidth
                        border.color: HomeTheme.cardInteractive
                        Text {
                            id: flagText
                            anchors.centerIn: parent
                            text: tagBadge.modelData
                            color: HomeTheme.cardTextPrimary
                            font.pixelSize: HomeTheme.cardMetaFontSize
                            font.bold: true
                        }
                    }
                }
            }

            Text {
                visible: root.cardId >= 0
                text: homeController.qtTranslate("CardScene", "Card effect")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardDetailSectionFontSize
                font.bold: true
            }

            Rectangle {
                visible: root.cardId >= 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(HomeTheme.cardControlHeight,
                                                 effectText.implicitHeight
                                                 + HomeTheme.cardDetailMetaGap * 2)
                radius: HomeTheme.cardControlRadius
                color: HomeTheme.cardTransparent
                border.width: effectText.activeFocus
                              ? (homeController.visualMode === "highcontrast"
                                 ? HomeTheme.cardHighContrastFocusBorderWidth
                                 : HomeTheme.cardFocusBorderWidth)
                              : 0
                border.color: HomeTheme.focusBorderHigh

                TextEdit {
                    id: effectText

                    anchors.fill: parent
                    anchors.margins: HomeTheme.cardDetailMetaGap
                    // Keep the rich description immutable while exposing normal
                    // mouse and Shift+arrow text selection to keyboard users.
                    readOnly: true
                    activeFocusOnTab: root.cardId >= 0
                    selectByMouse: true
                    selectByKeyboard: true
                    persistentSelection: true
                    cursorVisible: activeFocus
                    text: root.detail.description
                          || homeController.qtTranslate("CardScene", "No description available")
                    textFormat: TextEdit.RichText
                    color: HomeTheme.cardTextSecondary
                    selectedTextColor: HomeTheme.cardTextPrimary
                    selectionColor: HomeTheme.cardInteractiveSoft
                    font.pixelSize: HomeTheme.cardBodyFontSize
                    wrapMode: TextEdit.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: homeController.qtTranslate("CardScene", "Card effect")
                    Accessible.focusable: root.cardId >= 0
                    Keys.onTabPressed: function(event) {
                        var target = effectText.KeyNavigation.tab
                        if (target) {
                            target.forceActiveFocus(Qt.TabFocusReason)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    Keys.onBacktabPressed: function(event) {
                        var target = effectText.KeyNavigation.backtab
                        if (target) {
                            target.forceActiveFocus(Qt.BacktabFocusReason)
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                    onActiveFocusChanged: if (activeFocus) root.revealItem(effectText)
                    onLinkActivated: function(link) {}
                }
            }

            Text {
                visible: root.cardId >= 0
                text: homeController.qtTranslate("CardScene", "Audio preview")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardDetailSectionFontSize
                font.bold: true
            }

            RowLayout {
                visible: root.cardId >= 0
                Layout.fillWidth: true
                spacing: HomeTheme.cardDetailAudioGap

                BAToolButton {
                    id: maleAudio
                    Layout.fillWidth: true
                    Layout.preferredHeight: HomeTheme.cardControlHeight
                    text: homeController.qtTranslate("CardScene", "Male")
                    visible: !!root.detail.hasMaleAudio
                    onClicked: homeController.playCardAudio(root.cardId, "male")
                    onActiveFocusChanged: if (activeFocus) root.revealItem(maleAudio)
                }

                BAToolButton {
                    id: femaleAudio
                    Layout.fillWidth: true
                    Layout.preferredHeight: HomeTheme.cardControlHeight
                    text: homeController.qtTranslate("CardScene", "Female")
                    visible: !!root.detail.hasFemaleAudio
                    onClicked: homeController.playCardAudio(root.cardId, "female")
                    onActiveFocusChanged: if (activeFocus) root.revealItem(femaleAudio)
                }

                BAToolButton {
                    id: effectAudio
                    Layout.fillWidth: true
                    Layout.preferredHeight: HomeTheme.cardControlHeight
                    text: homeController.qtTranslate("CardScene", "Effect")
                    visible: !!root.detail.hasEffectAudio
                    onClicked: homeController.playCardAudio(root.cardId, "effect")
                    onActiveFocusChanged: if (activeFocus) root.revealItem(effectAudio)
                }
            }

            Rectangle {
                visible: root.cardId >= 0
                Layout.fillWidth: true
                Layout.preferredHeight: HomeTheme.cardDividerHeight
                color: HomeTheme.cardPanelBorder
            }

            Text {
                visible: root.cardId >= 0
                text: homeController.qtTranslate("CardScene", "Possible cards")
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardDetailSectionFontSize
                font.bold: true
            }

            CardVariantList {
                visible: root.cardId >= 0
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                variants: root.detail.variants || []
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: HomeTheme.cardPanelFocusInset
        radius: HomeTheme.cardPanelRadius
        color: HomeTheme.cardTransparent
        border.width: detailScroll.activeFocus
                      ? (homeController.visualMode === "highcontrast"
                         ? HomeTheme.cardHighContrastFocusBorderWidth
                         : HomeTheme.cardFocusBorderWidth)
                      : 0
        border.color: HomeTheme.focusBorderHigh
        visible: border.width > 0
    }
}
