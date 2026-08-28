import QtQuick
import "."

Item {
    id: root

    required property string tagKey
    required property string text
    required property int count
    property bool checked: false
    property bool highContrast: homeController.visualMode === "highcontrast"
    signal toggled(string tagKey, bool checked)

    implicitWidth: content.implicitWidth + HomeTheme.cardTagHPadding * 2
    implicitHeight: HomeTheme.cardTagHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.text + ", " + root.count
    Accessible.checked: root.checked

    function toggle() {
        if (enabled)
            toggled(tagKey, !checked)
    }

    Keys.onReturnPressed: root.toggle()
    Keys.onEnterPressed: root.toggle()
    Keys.onSpacePressed: function(event) {
        root.toggle()
        event.accepted = true
    }

    scale: pointer.pressed ? 0.96 : 1.0
    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }

    Rectangle {
        anchors.fill: parent
        radius: HomeTheme.cardTagRadius
        color: root.checked ? HomeTheme.cardTagChecked
                            : (pointer.containsMouse ? HomeTheme.cardTagHover : HomeTheme.cardTagFill)
        border.width: root.activeFocus
                      ? (root.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                           : HomeTheme.cardFocusBorderWidth)
                      : (root.checked ? HomeTheme.cardSelectedBorderWidth
                                      : HomeTheme.cardBorderWidth)
        border.color: root.activeFocus ? HomeTheme.focusBorderHigh
                                      : (root.checked ? HomeTheme.cardInteractive
                                                      : HomeTheme.cardTileBorder)
    }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: HomeTheme.cardTagCountGap

        Rectangle {
            width: HomeTheme.cardTagCheckSize
            height: HomeTheme.cardTagCheckSize
            anchors.verticalCenter: parent.verticalCenter
            radius: HomeTheme.cardBadgeRadius
            color: root.checked ? HomeTheme.cardInteractive : HomeTheme.cardTransparent
            border.width: HomeTheme.cardBorderWidth
            border.color: root.checked ? HomeTheme.cardTagMark : HomeTheme.cardTextMuted

            Text {
                anchors.centerIn: parent
                visible: root.checked
                text: "✓"
                color: HomeTheme.cardTagMark
                font.pixelSize: HomeTheme.cardMetaFontSize
                font.bold: true
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.checked ? HomeTheme.cardBadgeText : HomeTheme.cardTextPrimary
            font.pixelSize: HomeTheme.cardControlFontSize
            font.bold: root.checked
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.count
            color: root.checked ? HomeTheme.cardBadgeText : HomeTheme.cardTextMuted
            font.pixelSize: HomeTheme.cardMetaFontSize
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.toggle()
        }
    }
}
