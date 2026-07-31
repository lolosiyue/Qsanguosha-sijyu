import QtQuick
import QtMultimedia

Item {
    id: root

    property url backdropSource: {
        var cfg = homeController.backgroundImage;
        return cfg.toString() !== "" ? cfg : homeController.randomBackdrop();
    }

    property bool isVideo: {
        var str = String(backdropSource);
        return /\.(mp4|webm|mkv)$/i.test(str);
    }

    Rectangle {
        anchors.fill: parent
        color: "#0A0E27"

        Image {
            anchors.fill: parent
            source: isVideo ? "" : backdropSource
            fillMode: Image.PreserveAspectCrop
            mipmap: true
            opacity: 0.45
            visible: !isVideo

            onStatusChanged: {
                if (status === Image.Error)
                    backdropSource = homeController.randomBackdrop();
            }
        }

        Video {
            anchors.fill: parent
            source: isVideo ? backdropSource : ""
            fillMode: VideoOutput.PreserveAspectCrop
            opacity: 0.45
            visible: isVideo
            muted: true
            loops: MediaPlayer.Infinite

            onErrorOccurred: {
                backdropSource = homeController.randomBackdrop();
            }
        }
    }

    Rectangle {
        anchors.fill: parent

        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00FFFFFF" }
            GradientStop { position: 0.3; color: "#10FFFFFF" }
            GradientStop { position: 0.6; color: "#05FFFFFF" }
            GradientStop { position: 1.0; color: "#000A0E27" }
        }
    }
}