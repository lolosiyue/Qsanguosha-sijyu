import QtQuick
import QtQuick.Layouts
import "."

// form is the hosting page: it provides compact and reveal().
BAToolButton {
    id: formButton
    required property var form
    implicitHeight: form.compact ? HomeTheme.compactTouch : HomeTheme.cardControlHeight + 4
    Layout.fillWidth: form.compact
    onActiveFocusChanged: if (activeFocus) form.reveal(formButton)
}
