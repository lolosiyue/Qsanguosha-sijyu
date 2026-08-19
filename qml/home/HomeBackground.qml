import QtQuick
import "."

Item {
    id: root

    property url backdropSource: {
        var cfg = homeController.backgroundImage;
        return cfg.toString() !== "" ? cfg : homeController.randomBackdrop();
    }

    property bool isVideo: {
        var str = String(backdropSource);
        // 僅在 multimedia 後端實際可用時才以影片播放；
        // 否則即使副檔名是 mp4/webm/mkv，也走圖片路徑（底下 Image 會自動回退隨機背景）。
        return homeController.hasVideoSupport && /\.(mp4|webm|mkv)$/i.test(str);
    }

    Rectangle {
        anchors.fill: parent
        color: HomeTheme.windowBg

        Image {
            id: backdropImage
            anchors.fill: parent
            source: isVideo ? "" : backdropSource
            fillMode: Image.PreserveAspectCrop
            mipmap: false
            asynchronous: true
            cache: true
            opacity: HomeTheme.backdropOpacity
            visible: !isVideo && status === Image.Ready

            onStatusChanged: {
                if (status === Image.Error && backdropSource.toString() !== "") {
                    var next = homeController.randomBackdrop()
                    if (next.toString() !== "" && next !== backdropSource)
                        backdropSource = next
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: isVideo
            sourceComponent: VideoOverlay {
                source: backdropSource

                onErrorOccurred: {
                    var next = homeController.randomBackdrop()
                    if (next.toString() !== "")
                        backdropSource = next
                }
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