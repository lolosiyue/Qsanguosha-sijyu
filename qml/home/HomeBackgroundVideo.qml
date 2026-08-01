import QtQuick
import QtMultimedia
import "."

Item {
    id: root

    property url backdropSource: {
        var cfg = homeController.backgroundImage;
        return cfg.toString() !== "" ? cfg : homeController.randomBackdrop();
    }

    property bool isVideo: {
        var str = String(backdropSource);
        return homeController.hasVideoSupport && /\.(mp4|webm|mkv)$/i.test(str);
    }

    Rectangle {
        anchors.fill: parent
        color: HomeTheme.windowBg

        Image {
            anchors.fill: parent
            source: isVideo ? "" : backdropSource
            fillMode: Image.PreserveAspectCrop
            mipmap: true
            opacity: HomeTheme.backdropOpacity
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
            opacity: HomeTheme.backdropOpacity
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
            GradientStop { position: 0.0; color: HomeTheme.gradientTop }
            GradientStop { position: 0.3; color: HomeTheme.gradientMidTop }
            GradientStop { position: 0.6; color: HomeTheme.gradientMidBot }
            GradientStop { position: 1.0; color: HomeTheme.gradientBottom }
        }
    }
}