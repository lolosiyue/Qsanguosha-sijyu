#include "../src/ui/room-layout-engine.h"

#include <cmath>
#include <iostream>

namespace
{
bool closeTo(double actual, double expected)
{
    return std::abs(actual - expected) < 0.0001;
}

int failed(int code, const char *stage)
{
    std::cerr << "RoomLayoutEngine fixture failed: " << stage << '\n';
    return code;
}

RoomLayoutEngine::Input standardInput()
{
    RoomLayoutEngine::Input input;
    input.viewport = QRectF(0.0, 0.0, 1280.0, 720.0);
    input.minimumSceneSize = QSize(800, 600);
    input.maximumSceneSize = QSize(1600, 900);
    input.skin.scenePadding = 20;
    input.skin.photoRoomPadding = 5.0;
    input.skin.photoDashboardPadding = 11.5;
    input.skin.roleBoxHeight = 40;
    input.skin.infoPlaneWidthPercentage = 0.2;
    input.skin.logBoxHeightPercentage = 0.4;
    input.skin.chatBoxHeightPercentage = 0.2;
    input.skin.chatTextBoxHeight = 40;
    input.skin.photoHDistance = 32;
    input.skin.photoVDistance = 32;
    input.skin.discardPilePadding = 12;
    input.skin.discardPileMinWidth = 120;
    input.skin.dashboardNormalHeight = 180;
    input.skin.dashboardFloatingAreaHeight = 70;
    input.skin.cardNormalHeight = 105;
    input.dashboardHeight = 200.0;
    input.avatarSceneHeight = 120.0;
    input.chatButtonSize = QSizeF(32.0, 24.0);
    input.selfBoxSize = QSizeF(100.0, 40.0);
    input.enemyBoxSize = QSizeF(90.0, 36.0);
    input.smallPhotoSize = QSize(94, 108);
    input.normalPhotoSize = QSize(157, 181);
    input.bigPhotoSize = QSize(235, 271);
    return input;
}
}

int runRoomLayoutEngineTests()
{
    using namespace RoomLayoutEngine;

    Input input = standardInput();
    input.photoCount = 2;
    Result result = compute(input);
    if (!result.valid || !result.seatsValid || !closeTo(result.sceneScale, 1.0)
        || result.sceneRect != QRectF(0.0, 0.0, 1280.0, 720.0)
        || result.displayRect != QRectF(20.0, 20.0, 1240.0, 680.0)
        || result.dashboardRect != QRectF(20.0, 480.0, 1240.0, 200.0)
        || result.infoRect != QRectF(1012.0, 60.0, 248.0, 480.0)
        || result.logSizeWithChat != QSize(248, 192)
        || result.logSizeWithoutChat != QSize(248, 328)
        || result.chatBoxPosition != QPointF(1012.0, 444.0)
        || result.chatEditPosition != QPointF(1012.0, 540.0)
        || result.chatEditSize != QSizeF(216.0, 40.0)
        || result.chatButtonPosition != QPointF(1228.0, 548.0)
        || result.selfBoxPosition != QPointF(887.0, 405.0)
        || result.enemyBoxPosition != QPointF(50.0, 50.0)
        || result.backgroundSize != QSize(1240, 680) || result.backgroundTableHeight != 668
        || result.timerPosition != QPointF(1280.0 * 0.77, -1.0)
        || result.tableWidth != 962 || result.tableHeight != 470
        || result.photoTier != PhotoTier::Big || result.photoBaseSize != QSize(235, 271)
        || result.photos.size() != 2 || result.photos[0].region != 5 || result.photos[1].region != 6
        || result.photos[0].position != QPointF(844.5, 260.0)
        || result.photos[1].position != QPointF(142.5, 260.0))
        return failed(1, "legacy shell and regular seats");

    // Pin the regular table rows used by representative odd player counts, including the
    // last defined row. Positions are checked in a separate fixture below.
    const int counts[] = { 1, 3, 7, 9, 19 };
    const int firstRegions[][5] = {
        { 1 }, { 5, 1, 6 }, { 5, 5, 1, 1, 1 }, { 3, 3, 7, 7, 7 }, { 3, 3, 3, 3, 3 }
    };
    for (int fixture = 0; fixture < 5; ++fixture) {
        input = standardInput();
        input.photoCount = counts[fixture];
        result = compute(input);
        if (!result.seatsValid || result.photos.size() != counts[fixture])
            return failed(2, "regular seat table count");
        for (int i = 0; i < 5 && i < counts[fixture]; ++i) {
            if (result.photos[i].region != firstRegions[fixture][i])
                return failed(3, "regular seat table ordering");
        }
    }

    // The 1-based Hulao seat table changes both ordering and the started-game table height.
    input.viewport.setHeight(1000.0);
    input.mode = Mode::Hulao;
    input.gameStarted = true;
    input.photoCount = 3;
    input.selfSeat = 2;
    result = compute(input);
    if (!result.valid || result.tableHeight != 618 || result.photos.size() != 3
        || result.photos[0].region != 3 || result.photos[1].region != 3
        || result.photos[2].region != 1)
        return failed(4, "Hulao mapping");

    // Boss uses the Hulao seat mapping but legacy updateTable does not shorten its table.
    input.mode = Mode::Boss;
    result = compute(input);
    if (result.tableHeight != 650)
        return failed(5, "Boss table height");

    input.mode = Mode::ThreeVThree;
    input.photoCount = 5;
    input.selfSeat = 3;
    result = compute(input);
    if (!result.valid || result.tableHeight != 618 || result.photos.size() != 5
        || result.photos[0].region != 3 || result.photos[1].region != 3
        || result.photos[2].region != 1 || result.photos[3].region != 1
        || result.photos[4].region != 1)
        return failed(6, "3v3 mapping");

    input.smallPhotoSize = QSize(40, 40);
    input.normalPhotoSize = QSize(60, 60);
    input.bigPhotoSize = QSize(80, 80);
    result = compute(input);
    if (!result.seatsValid || result.photoTier != PhotoTier::Big
        || result.photos[1].position != QPointF(819.0, 325.0)
        || result.photos[0].position != QPointF(819.0, 578.0)
        || !closeTo(result.photos[4].position.x(), 217.8333333333)
        || !closeTo(result.photos[3].position.x(), 429.5)
        || !closeTo(result.photos[2].position.x(), 641.1666666667))
        return failed(7, "3v3 reverse order and positions");

    // Out-of-range counts/seats and zero sizes return shell geometry without indexing tables.
    input.photoCount = 20;
    result = compute(input);
    if (!result.valid || result.seatsValid || !result.photos.isEmpty())
        return failed(8, "count bounds");
    input.photoCount = 2;
    input.selfSeat = 0;
    result = compute(input);
    if (!result.valid || result.seatsValid || !result.photos.isEmpty())
        return failed(9, "invalid 1-based seat");
    input.selfSeat = 1;
    input.smallPhotoSize = QSize();
    result = compute(input);
    if (!result.valid || result.seatsValid || !result.photos.isEmpty())
        return failed(10, "invalid photo dimensions");

    input = standardInput();
    input.photoCount = 2;
    input.normalPhotoSize = QSize(400, 400);
    input.bigPhotoSize = QSize(500, 500);
    result = compute(input);
    if (!result.seatsValid || result.photoTier != PhotoTier::Normal)
        return failed(11, "normal photo tier");
    input.normalPhotoSize = QSize(500, 500);
    input.bigPhotoSize = QSize(600, 600);
    result = compute(input);
    if (!result.seatsValid || result.photoTier != PhotoTier::Small)
        return failed(12, "small photo tier");

    input = standardInput();
    input.photoCount = 2;
    input.skin.photoVDistance = 0;
    input.smallPhotoSize = QSize(300, 500);
    input.normalPhotoSize = QSize(400, 600);
    input.bigPhotoSize = QSize(500, 700);
    result = compute(input);
    if (!result.seatsValid || result.photoTier != PhotoTier::Small
        || result.floatingArea.width() != 699)
        return failed(13, "fractional photo scale and floating width");

    input = standardInput();
    input.photoCount = 2;
    input.skin.photoRoomPadding = 5.75;
    input.viewport = QRectF(0.0, 0.0, 1281.25, 720.5);
    result = compute(input);
    if (!result.valid || result.tableWidth != 963 || result.backgroundSize != QSize(1241, 680)
        || result.backgroundTableHeight != 668 || result.selfBoxPosition != QPointF(888.0, 405.5))
        return failed(14, "fractional viewport and padding truncation");

    input = standardInput();
    input.photoCount = 2;
    input.viewport = QRectF(0.0, 0.0, 640.0, 360.0);
    result = compute(input);
    if (!result.valid || !closeTo(result.sceneScale, 5.0 / 3.0)
        || !closeTo(result.sceneRect.width(), 640.0 * 5.0 / 3.0)
        || !closeTo(result.sceneRect.height(), 600.0))
        return failed(15, "minimum scene clamp");

    input = standardInput();
    input.viewport.setHeight(1000.0);
    input.mode = Mode::Hulao;
    input.gameStarted = true;
    input.photoCount = 3;
    input.selfSeat = 2;
    input.clampScene = false;
    result = compute(input);
    if (!result.valid || result.tableHeight != 718
        || result.sceneRect != QRectF(0.0, 0.0, 1280.0, 1000.0))
        return failed(16, "table-only path skips scene clamp");

    input = standardInput();
    input.photoCount = 2;
    input.viewport = QRectF(15.0, 10.0, 0.0, 720.0);
    result = compute(input);
    if (result.valid)
        return failed(17, "empty viewport");
    input.photoCount = 0;
    input.viewport = QRectF(15.0, 10.0, 1280.0, 720.0);
    result = compute(input);
    if (!result.valid || result.seatsValid || result.sceneRect != QRectF(0.0, 0.0, 1280.0, 720.0)
        || !result.photos.isEmpty())
        return failed(18, "zero photo count and origin reset");

    ResponsiveInput responsive;
    responsive.stableRect = QRectF(0.0, 0.0, 844.0, 390.0);
    responsive.availableRect = responsive.stableRect;
    responsive.photoCount = 3;
    responsive.smallPhotoSize = QSize(94, 108);
    ResponsiveResult responsiveResult = computeResponsive(responsive);
    if (!responsiveResult.valid || responsiveResult.profile != Profile::CompactLandscape
        || responsiveResult.seatPresentation != SeatPresentation::Ribbon
        || responsiveResult.photos.size() != 3
        || responsiveResult.tableRect.bottom() >= responsiveResult.interactionRect.top())
        return failed(19, "compact landscape geometry");

    responsive.stableRect = QRectF(0.0, 0.0, 390.0, 844.0);
    responsive.availableRect = responsive.stableRect;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::CompactPortrait
        || responsiveResult.seatPresentation != SeatPresentation::Ring
        || responsiveResult.photos[0].center.x() <= responsiveResult.seatsRect.center().x()
        || responsiveResult.photos[0].center.y() <= responsiveResult.seatsRect.center().y())
        return failed(20, "compact portrait ring");
    for (int i = 0; i < responsiveResult.photos.size(); ++i) {
        const QPointF &center = responsiveResult.photos[i].center;
        const QRectF photoRect(center.x() - responsive.smallPhotoSize.width() / 2.0,
                               center.y() - responsive.smallPhotoSize.height() / 2.0,
                               responsive.smallPhotoSize.width(), responsive.smallPhotoSize.height());
        if (!responsiveResult.seatsRect.contains(photoRect)
            || responsiveResult.tableRect.intersects(photoRect))
            return failed(20, "ring bounds and central pile clearance");
        for (int j = 0; j < i; ++j) {
            const QPointF &other = responsiveResult.photos[j].center;
            const double dx = std::abs(center.x() - other.x());
            const double dy = std::abs(center.y() - other.y());
            if (dx < responsive.smallPhotoSize.width() + responsive.gap
                && dy < responsive.smallPhotoSize.height() + responsive.gap)
                return failed(20, "ring photo separation");
        }
    }

    responsive.stableRect = QRectF(0.0, 0.0, 200.0, 400.0);
    responsive.availableRect = responsive.stableRect;
    responsiveResult = computeResponsive(responsive);
    if (!responsiveResult.valid || responsiveResult.seatPresentation != SeatPresentation::Ribbon
        || responsiveResult.photos.size() != 3
        || responsiveResult.seatsRect.height() < responsive.smallPhotoSize.height()
        || responsiveResult.tableRect.bottom() >= responsiveResult.seatsRect.top())
        return failed(21, "small viewport ribbon fallback");

    responsive.stableRect = QRectF(0.0, 0.0, 768.0, 1024.0);
    responsive.availableRect = responsive.stableRect;
    responsive.logVisible = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Medium || responsiveResult.logRect.isEmpty()
        || responsiveResult.logRect.intersects(responsiveResult.interactionRect))
        return failed(22, "medium profile");
    responsive.chatVisible = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.logRect.isEmpty() || responsiveResult.chatRect.isEmpty()
        || responsiveResult.logRect.intersects(responsiveResult.chatRect)
        || responsiveResult.chatRect.intersects(responsiveResult.interactionRect))
        return failed(22, "log and chat stay above interaction controls");
    responsive.chatVisible = false;

    responsive.stableRect = QRectF(0.0, 0.0, 1920.0, 1080.0);
    responsive.availableRect = responsive.stableRect;
    responsive.inspectorPinned = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::ExpandedSplit
        || responsiveResult.inspectorRect.width() < 320.0
        || responsiveResult.mainRect.width() < 640.0)
        return failed(23, "expanded split minimum panes");

    // Keyboard/IME shrinks only available geometry; profile selection uses stable bounds.
    responsive.availableRect.setHeight(620.0);
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::ExpandedSplit
        || responsiveResult.mainRect.height() != 620.0)
        return failed(24, "keyboard independent profile");

    responsive.stableRect = QRectF(0.0, 0.0, 980.0, 580.0);
    responsive.availableRect = responsive.stableRect;
    responsive.hasPreviousProfile = true;
    responsive.previousProfile = Profile::ExpandedSplit;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::ExpandedSplit)
        return failed(25, "expanded profile hysteresis");
    responsive.stableRect = QRectF(0.0, 0.0, 950.0, 560.0);
    responsive.availableRect = responsive.stableRect;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile == Profile::ExpandedSplit)
        return failed(26, "expanded hysteresis exit");

    // A separating crease may have zero width; both book panes must remain usable.
    responsive.stableRect = QRectF(0.0, 0.0, 800.0, 900.0);
    responsive.availableRect = responsive.stableRect;
    responsive.hasPreviousProfile = false;
    responsive.fold.posture = FoldPosture::Book;
    responsive.fold.bounds = QRectF(400.0, 0.0, 0.0, 900.0);
    responsive.fold.separating = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Book || responsiveResult.mainRect.width() != 400.0
        || responsiveResult.inspectorRect.width() != 400.0)
        return failed(27, "book crease split");

    responsive.fold.posture = FoldPosture::Tabletop;
    responsive.fold.bounds = QRectF(0.0, 440.0, 800.0, 20.0);
    responsive.fold.separating = false;
    responsive.fold.occluding = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Tabletop
        || responsiveResult.tableRect.bottom() > 440.0
        || responsiveResult.interactionRect.top() < 460.0)
        return failed(28, "tabletop hinge exclusion");

    // The keyboard can hide the lower posture pane; controls then fit inside the upper pane.
    responsive.availableRect = QRectF(0.0, 0.0, 800.0, 200.0);
    responsiveResult = computeResponsive(responsive);
    if (!responsiveResult.valid || responsiveResult.profile != Profile::Tabletop
        || responsiveResult.interactionRect.bottom() > 200.0
        || responsiveResult.tableRect.bottom() >= responsiveResult.interactionRect.top()
        || responsiveResult.tableRect.intersects(responsive.fold.bounds))
        return failed(28, "tabletop keyboard fallback stays above hinge");
    responsive.availableRect = responsive.stableRect;

    responsive.fold.bounds = QRectF(790.0, 0.0, 20.0, 900.0);
    responsive.fold.posture = FoldPosture::Book;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Medium)
        return failed(29, "out of bounds fold fallback");

    // A real but too-narrow Book split falls back entirely into its larger pane.
    responsive.fold.bounds = QRectF(100.0, 0.0, 20.0, 900.0);
    responsive.fold.occluding = true;
    responsive.fold.separating = false;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Medium || responsiveResult.mainRect.left() < 120.0
        || responsiveResult.tableRect.left() < 120.0
        || responsiveResult.interactionRect.left() < 120.0
        || (responsiveResult.inspectorRect.isValid() && responsiveResult.inspectorRect.left() < 120.0))
        return failed(29, "small Book pane fallback avoids hinge");

    responsive.fold.posture = FoldPosture::Book;
    responsive.fold.bounds = QRectF(100.0, 0.0, 0.0, 900.0);
    responsive.fold.separating = true;
    responsive.fold.occluding = false;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Medium)
        return failed(30, "fold tiny pane fallback");

    responsive.fold.posture = FoldPosture::Book;
    responsive.fold.bounds = QRectF(400.0, 0.0, 0.0, 900.0);
    responsive.fold.separating = false;
    responsive.fold.occluding = true;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.profile != Profile::Medium)
        return failed(35, "zero size occluding fold is invalid");

    responsive.fold = FoldInfo();
    responsive.stableRect = QRectF(0.0, 0.0, 844.0, 390.0);
    responsive.availableRect = responsive.stableRect;
    const QRectF actionBounds = computeResponsive(responsive).safeInteractionRect;
    responsive.handedness = Handedness::Left;
    responsiveResult = computeResponsive(responsive);
    if (!closeTo(responsiveResult.safeInteractionRect.left(), actionBounds.left())
        || responsiveResult.safeInteractionRect.width() >= actionBounds.width())
        return failed(31, "left handed interaction alignment");

    responsive.handedness = Handedness::None;
    responsive.photoCount = 20;
    responsiveResult = computeResponsive(responsive);
    if (responsiveResult.valid)
        return failed(32, "responsive player count bounds");

    responsive.photoCount = 3;
    responsive.stableRect = QRectF(0.0, 0.0, 390.0, 844.0);
    responsive.availableRect = QRectF(0.0, 0.0, 390.0, 200.0);
    responsiveResult = computeResponsive(responsive);
    if (!responsiveResult.valid || responsiveResult.profile != Profile::CompactPortrait
        || responsiveResult.actionsRect.height() < 48.0
        || !responsiveResult.seatsRect.isEmpty() || !responsiveResult.photos.isEmpty())
        return failed(33, "keyboard shrink preserves actions and hides unfit seats");

    responsive.availableRect.setHeight(100.0);
    responsiveResult = computeResponsive(responsive);
    if (!responsiveResult.valid || responsiveResult.actionsRect.height() < 48.0
        || !responsiveResult.tableRect.isEmpty()
        || responsiveResult.tableCenter != responsiveResult.mainRect.center())
        return failed(34, "zero height table center fallback");

    // The launcher follows a safe fallback pane, rather than floating over seats/hinge.
    ResponsiveInput headerInput;
    headerInput.stableRect = headerInput.availableRect = QRectF(0, 0, 800, 900);
    headerInput.smallPhotoSize = QSize(94, 108);
    headerInput.photoCount = 3;
    headerInput.headerHeight = 56;
    headerInput.fold = {FoldPosture::Book, QRectF(100, 0, 20, 900), false, true};
    const auto headerResult = computeResponsive(headerInput);
    if (!headerResult.valid || headerResult.headerRect.left() < 120
        || headerResult.headerRect.height() != 56
        || headerResult.headerRect.intersects(headerResult.seatsRect)
        || headerResult.headerRect.intersects(headerResult.interactionRect))
        return failed(38, "launcher reserved in unoccluded pane");

    return 0;
}
