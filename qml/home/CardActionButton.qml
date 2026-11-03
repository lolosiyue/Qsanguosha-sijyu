import QtQuick
import QtQuick.Layouts
import "."

BAToolButton {
    property bool compact: false
    Layout.fillWidth: compact
    Layout.preferredHeight: compact ? HomeTheme.compactTouch : HomeTheme.cardActionButtonExtent
    opacity: enabled ? 1 : 0.5
}
