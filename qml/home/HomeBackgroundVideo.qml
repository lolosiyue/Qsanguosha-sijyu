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
            mipmap: false
            opacity: HomeTheme.backdropOpacity
            visible: !isVideo && backdropSource.toString() !== ""

            onStatusChanged: {
                if (status === Image.Error && backdropSource.toString() !== "")
                    backdropSource = homeController.randomBackdrop();
            }
        }

        Video {
            id: bgVideo
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

            // Qt 6 的 Video 不會自動播放，source 就緒後需呼叫 play() 才會有畫面
            Component.onCompleted: bgVideo.play()
            onSourceChanged: bgVideo.play()
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