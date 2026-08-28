import QtQuick
import "."

Item {
    id: root

    property url backdropSource: {
        var cfg = homeController.backgroundImage;
        return cfg.toString() !== "" ? cfg : homeController.randomBackdrop();
    }

    readonly property bool backdropIsVideoFile: /\.(mp4|webm|mkv)$/i.test(String(backdropSource))

    // 影片播放需要三個條件同時成立：使用者冇關掉、multimedia 後端真係載得到、
    // 而背景本身係影片檔。任何一個唔成立都行靜態圖片，唔會建立 Video component。
    property bool isVideo: homeController.videoBackgroundEnabled
                           && homeController.hasVideoSupport
                           && backdropIsVideoFile

    // 影片播唔到就換返一張靜態背景。呢個係 M2B-A 要求的 static fallback：
    // HomeScene 唔會因為影片失敗而載入唔到。
    function fallBackToStaticBackdrop(reason, message) {
        homeController.reportVideoStatus(reason, message);
        isVideo = false;
        var next = homeController.randomBackdrop();
        if (next.toString() !== "" && next !== backdropSource)
            backdropSource = next;
        // 保留原因，只額外標記「靜態背景已經頂上」。
        homeController.confirmVideoFallback();
    }

    Component.onCompleted: {
        if (!backdropIsVideoFile)
            homeController.reportVideoStatus("not_requested", "");
        else if (!homeController.videoBackgroundEnabled)
            fallBackToStaticBackdrop("disabled", "");
        else if (!homeController.hasVideoSupport)
            fallBackToStaticBackdrop("backend_unavailable", "");
        else if (!homeController.localFileExists(backdropSource))
            fallBackToStaticBackdrop("asset_missing", String(backdropSource));
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

                onVideoReady: homeController.reportVideoStatus("ok", "")
                onFailed: function(reason, message) {
                    root.fallBackToStaticBackdrop(reason, message);
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
