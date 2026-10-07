import QtQuick
import QtQuick.Effects

// Root of QmlTableLayer. Extension elements are created from C++ into `content`.
Item {
    id: root
    // FitView::applyVisualMode keeps these in step with the table's grayscale / high-contrast filter.
    property real saturation: 0
    property real contrast: 0

    Item {
        id: content
        objectName: "content"
        anchors.fill: parent
        // The software scene graph has no shader effects; elements then stay in colour.
        layer.enabled: (root.saturation !== 0 || root.contrast !== 0)
            && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            saturation: root.saturation
            contrast: root.contrast
        }
    }
}
