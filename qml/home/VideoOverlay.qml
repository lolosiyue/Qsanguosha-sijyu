import QtQuick
import QtMultimedia

Item {
    property url source: ""

    Video {
        anchors.fill: parent
        source: parent.source
        fillMode: VideoOutput.PreserveAspectCrop
        muted: true
        loops: MediaPlayer.Infinite
        opacity: 0.45
    }
}