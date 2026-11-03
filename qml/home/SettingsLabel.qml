import QtQuick
import QtQuick.Layouts
import "."

Text {
    property bool compact: false
    Layout.preferredWidth: compact ? -1 : HomeTheme.settingsLabelWidth
    Layout.fillWidth: compact
    color: enabled ? HomeTheme.cardTextSecondary : HomeTheme.cardControlDisabledText
    font.pixelSize: HomeTheme.settingsFontSize
    wrapMode: Text.Wrap
}
