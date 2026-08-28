import QtQuick
import "."

Item {
    id: root

    required property int cardId
    required property string overviewDisplayName
    required property string baseDisplayName
    required property string typeKey
    required property string typeDisplay
    required property string packageSummary
    required property int physicalCount
    required property int variantCount
    required property var tagLabels
    required property url imageUrl
    property bool selected: false
    property bool keyboardFocus: false
    property bool highContrast: homeController.visualMode === "highcontrast"
    signal activated(int cardId)

    activeFocusOnTab: false
    focus: false
    Accessible.role: Accessible.ListItem
    Accessible.name: overviewDisplayName
    Accessible.focusable: true
    Accessible.selected: root.selected
    Accessible.description: typeDisplay + ", " + packageSummary + ", "
                            + homeController.qtTranslate("CardScene", "%1 cards · %2 variants")
                              .arg(physicalCount).arg(variantCount)

    Keys.onReturnPressed: root.activated(root.cardId)
    Keys.onEnterPressed: root.activated(root.cardId)
    Keys.onSpacePressed: root.activated(root.cardId)

    scale: pointer.pressed ? 0.98 : 1.0
    Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }

    Rectangle {
        anchors.fill: parent
        radius: HomeTheme.cardPanelRadius
        color: root.selected ? HomeTheme.cardTileSelected
                             : (pointer.containsMouse ? HomeTheme.cardTileHover : HomeTheme.cardTileFill)
        border.width: root.activeFocus || root.keyboardFocus
                      ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                           : HomeTheme.cardFocusBorderWidth)
                      : (root.selected ? HomeTheme.cardSelectedBorderWidth
                                       : HomeTheme.cardBorderWidth)
        border.color: root.activeFocus || root.keyboardFocus ? HomeTheme.focusBorderHigh
                                      : (root.selected ? HomeTheme.cardAccent : HomeTheme.cardTileBorder)
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: HomeTheme.cardTileAccentWidth
        radius: HomeTheme.cardTileAccentRadius
        color: HomeTheme.cardAccent
        opacity: root.selected ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: HomeTheme.cardTileMotionDuration
                easing.type: Easing.OutCubic
            }
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: HomeTheme.cardGridGap
        spacing: HomeTheme.cardSectionGap

        Item {
            width: Math.min(HomeTheme.cardTileImageMaxWidth, parent.width * 0.38)
            height: parent.height

            Rectangle {
                anchors.fill: parent
                radius: HomeTheme.cardControlRadius
                color: HomeTheme.cardPanelInner
            }

            Image {
                anchors.fill: parent
                anchors.margins: HomeTheme.cardTileImageInset
                source: root.imageUrl
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: Math.ceil(width)
                sourceSize.height: Math.ceil(height)
            }
        }

        Column {
            width: parent.width - parent.children[0].width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: HomeTheme.cardTileTextGap

            Text {
                width: parent.width
                text: root.overviewDisplayName
                color: HomeTheme.cardTextPrimary
                font.pixelSize: HomeTheme.cardTileTitleFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            Rectangle {
                width: typeText.implicitWidth + HomeTheme.cardBadgeHPadding * 2
                height: HomeTheme.cardBadgeHeight
                radius: HomeTheme.cardBadgeRadius
                color: root.typeKey === "BasicCard" ? HomeTheme.cardBadgeBasic
                     : root.typeKey === "TrickCard" ? HomeTheme.cardBadgeTrick
                     : root.typeKey === "EquipCard" ? HomeTheme.cardBadgeEquip
                                                     : HomeTheme.cardBadgeSkill
                Text {
                    id: typeText
                    anchors.centerIn: parent
                    text: root.typeDisplay
                    color: HomeTheme.cardBadgeText
                    font.pixelSize: HomeTheme.cardMetaFontSize
                    font.bold: true
                }
            }

            Text {
                width: parent.width
                text: homeController.qtTranslate("CardScene", "%1 cards · %2 variants")
                      .arg(root.physicalCount).arg(root.variantCount)
                color: HomeTheme.cardInteractive
                font.pixelSize: HomeTheme.cardCaptionFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: root.packageSummary
                color: HomeTheme.cardTextSecondary
                font.pixelSize: HomeTheme.cardCaptionFontSize
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.tagLabels && root.tagLabels.length > 0
                text: root.tagLabels ? root.tagLabels.join(" · ") : ""
                color: HomeTheme.cardTextMuted
                font.pixelSize: HomeTheme.cardMetaFontSize
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.activated(root.cardId)
        }
    }
}
