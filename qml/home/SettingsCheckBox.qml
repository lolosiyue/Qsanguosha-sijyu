import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "."

// form is the hosting page: it provides compact, highContrast and reveal().
Basic.CheckBox {
    id: check
    required property var form
    property string hint: ""
    Layout.fillWidth: true
    implicitHeight: form.compact ? HomeTheme.compactTouch
                                 : Math.max(implicitContentHeight, implicitIndicatorHeight) + topPadding + bottomPadding
    padding: 4
    spacing: 10
    // Accessible toggle otherwise only changes checked, bypassing the draft.
    Accessible.onToggleAction: check.click()
    Accessible.onPressAction: check.click()
    onActiveFocusChanged: if (activeFocus) form.reveal(check)
    Keys.onShortcutOverride: function(event) {
        if (event.key === Qt.Key_Space)
            event.accepted = true
    }
    ToolTip.visible: hint !== "" && hovered
    ToolTip.text: hint
    ToolTip.delay: 500
    indicator: Rectangle {
        implicitWidth: HomeTheme.settingsCheckSize
        implicitHeight: HomeTheme.settingsCheckSize
        x: check.leftPadding
        y: parent ? (parent.height - height) / 2 : 0
        radius: HomeTheme.cardBadgeRadius
        color: check.checked ? HomeTheme.cardInteractive : HomeTheme.cardInputFill
        border.width: check.activeFocus
                      ? (check.form.highContrast ? HomeTheme.cardHighContrastFocusBorderWidth
                                                 : HomeTheme.cardSelectedBorderWidth)
                      : HomeTheme.cardBorderWidth
        border.color: check.activeFocus ? HomeTheme.focusBorderHigh
                    : check.checked ? HomeTheme.cardInteractive : HomeTheme.cardTileBorder
        opacity: check.enabled ? 1.0 : HomeTheme.cardControlDisabledOpacity
        Text {
            anchors.centerIn: parent
            visible: check.checked
            text: "✓"
            color: HomeTheme.cardTagMark
            font.pixelSize: HomeTheme.cardBodyFontSize
            font.bold: true
        }
    }
    contentItem: Text {
        leftPadding: check.indicator.width + check.spacing
        text: check.text
        color: check.enabled ? HomeTheme.cardTextPrimary : HomeTheme.cardControlDisabledText
        font.pixelSize: HomeTheme.settingsFontSize
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
