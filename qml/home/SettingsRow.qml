import QtQuick
import QtQuick.Layouts
import "."

// Label and control share a row; compact stacks them.
GridLayout {
    property bool compact: false
    Layout.fillWidth: true
    columns: compact ? 1 : 2
    columnSpacing: HomeTheme.cardPanelGap
    rowSpacing: HomeTheme.compactGap
}
