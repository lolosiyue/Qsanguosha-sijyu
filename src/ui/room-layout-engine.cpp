#include "room-layout-engine.h"

#include "photo-layout-fit.h"
#include "seat-ring-table.h"

#include <QtGlobal>
#include <Qt>
#include <QtMath>
#include <cmath>

namespace RoomLayoutEngine
{
namespace
{
bool selectSeatRegions(const Input &input, const int *&regions, bool &pkMode)
{
    if (input.photoCount < 1 || input.photoCount > 19)
        return false;
    if (!input.gameStarted || input.mode == Mode::Regular) {
        regions = SeatRingTable::regularSeatRegions[input.photoCount - 1];
        pkMode = false;
        return true;
    }
    if (input.mode == Mode::Hulao || input.mode == Mode::Boss) {
        if (input.photoCount > 3 || input.selfSeat < 1 || input.selfSeat > 4)
            return false;
        regions = SeatRingTable::hulaoSeatRegions[input.selfSeat - 1];
        pkMode = true;
        return true;
    }
    if (input.mode == Mode::ThreeVThree) {
        if (input.photoCount > 5 || input.selfSeat < 1 || input.selfSeat > 6)
            return false;
        regions = SeatRingTable::threeVThreeSeatRegions[(input.selfSeat - 1) % 3];
        pkMode = true;
        return true;
    }
    return false;
}

QPointF disperse(const QRectF &region, bool horizontal, int align,
                 int count, double photoWidth, double photoHeight,
                 double horizontalGap, double verticalGap, int index)
{
    double stepX = 0.0;
    double stepY = 0.0;
    if (horizontal) {
        stepX = qMax(photoWidth + horizontalGap, region.width() / count);
    } else {
        const double minStepY = verticalGap + photoHeight;
        const double maxStepY = count > 1 ? region.height() / count : minStepY;
        stepY = qMax(minStepY, maxStepY);
    }

    double startX = 0.0;
    double startY = 0.0;
    const int alignLeft = Qt::AlignLeft;
    const int alignRight = Qt::AlignRight;
    const int alignHCenter = Qt::AlignHCenter;
    const int alignTop = Qt::AlignTop;
    const int alignBottom = Qt::AlignBottom;
    const int alignVCenter = Qt::AlignVCenter;
    const int horizontalMask = Qt::AlignHorizontal_Mask;
    const int verticalMask = Qt::AlignVertical_Mask;
    switch (align & verticalMask) {
    case alignTop: startY = region.top() + photoHeight / 2.0; break;
    case alignBottom: startY = region.bottom() - photoHeight / 2.0 - stepY * (count - 1); break;
    case alignVCenter: startY = region.center().y() - stepY * (count - 1) / 2.0; break;
    default: break;
    }
    switch (align & horizontalMask) {
    case alignLeft: startX = region.left() + photoWidth / 2.0; break;
    case alignRight: startX = region.right() - photoWidth / 2.0 - stepX * (count - 1); break;
    case alignHCenter: startX = region.center().x() - stepX * (count - 1) / 2.0; break;
    default: break;
    }
    return QPointF(startX + stepX * index, startY + stepY * index);
}
}

Result compute(const Input &input)
{
    Result result;
    if (input.clampScene && (input.viewport.width() <= 0.0 || input.viewport.height() <= 0.0))
        return result;

    result.valid = true;
    result.sceneRect = input.viewport;
    if (input.clampScene && input.viewport.width() > 0.0 && input.viewport.height() > 0.0) {
        const double width = input.viewport.width();
        const double height = input.viewport.height();
        const double minimumScale = qMax(input.minimumSceneSize.width() / width,
                                         input.minimumSceneSize.height() / height);
        double scale = qMax(minimumScale, 1.0);
        if (input.maximumSceneSize.isValid()) {
            const double maximumScale = qMin(input.maximumSceneSize.width() / width,
                                             input.maximumSceneSize.height() / height);
            scale = qMax(minimumScale, qMin(1.0, maximumScale));
        }
        result.sceneScale = scale;
        if (!qFuzzyCompare(scale, 1.0) || !qFuzzyIsNull(input.viewport.left())
            || !qFuzzyIsNull(input.viewport.top())) {
            result.sceneRect = QRectF(0.0, 0.0, width * scale, height * scale);
        }
    }

    QRectF display = result.sceneRect;
    const int scenePadding = input.skin.scenePadding;
    display.moveLeft(display.x() + scenePadding);
    display.moveTop(display.y() + scenePadding);
    display.setWidth(display.width() - scenePadding * 2);
    display.setHeight(display.height() - scenePadding * 2);
    result.displayRect = display;

    result.dashboardRect = QRectF(display.x(), display.height() - input.dashboardHeight,
                                  display.width(), input.dashboardHeight);
    const double infoWidth = display.width() * input.skin.infoPlaneWidthPercentage;
    result.infoRect = QRectF(display.right() - infoWidth,
                             display.top() + input.skin.roleBoxHeight,
                             infoWidth,
                             display.bottom() - input.avatarSceneHeight - input.skin.chatTextBoxHeight
                                 - (display.top() + input.skin.roleBoxHeight));
    result.roleBoxPosition = QPointF(result.infoRect.left(), display.top());
    result.logRect = QRectF(result.infoRect.topLeft(),
                            QSizeF(result.infoRect.width(), result.infoRect.height()
                                   * input.skin.logBoxHeightPercentage));
    result.logSizeWithChat = QSize(static_cast<int>(result.infoRect.width()),
        static_cast<int>(result.infoRect.height() * input.skin.logBoxHeightPercentage));
    result.logSizeWithoutChat = QSize(static_cast<int>(result.infoRect.width()),
        static_cast<int>(result.infoRect.height()
            * (input.skin.logBoxHeightPercentage + input.skin.chatBoxHeightPercentage)
            + input.skin.chatTextBoxHeight));
    result.chatBoxPosition = QPointF(result.infoRect.left(), result.infoRect.bottom()
                                     - result.infoRect.height() * input.skin.chatBoxHeightPercentage);
    result.chatRect = QRectF(result.chatBoxPosition,
                             QSizeF(result.infoRect.width(), result.infoRect.bottom()
                                    - result.chatBoxPosition.y()));
    result.chatEditPosition = QPointF(result.infoRect.left(), result.infoRect.bottom());
    result.chatEditSize = QSizeF(result.infoRect.width() - input.chatButtonSize.width(),
                                 input.skin.chatTextBoxHeight);
    result.chatButtonPosition = QPointF(result.infoRect.right() - input.chatButtonSize.width(),
        result.chatEditPosition.y() + (input.skin.chatTextBoxHeight - input.chatButtonSize.height()) / 2.0);

    int padding = input.skin.scenePadding;
    padding += input.skin.photoRoomPadding;
    result.selfBoxPosition = QPointF(result.infoRect.left() - padding - input.selfBoxSize.width(),
        result.sceneRect.height() - padding - input.selfBoxSize.height()
            - input.skin.dashboardNormalHeight - input.skin.dashboardFloatingAreaHeight);
    result.enemyBoxPosition = QPointF(padding * 2, padding * 2);
    result.backgroundSize = QSize(static_cast<int>(display.width()), static_cast<int>(display.height()));
    result.backgroundTableHeight = static_cast<int>(result.backgroundSize.height()
        - input.skin.photoDashboardPadding);
    result.timerPosition = QPointF(result.sceneRect.width() * 0.77, -1.0);

    const int tablePad = static_cast<int>(input.skin.scenePadding + input.skin.photoRoomPadding);
    // Preserve legacy integer truncation after QRectF-derived values enter int locals.
    result.tableWidth = static_cast<int>(result.infoRect.left() - tablePad * 2);
    result.tableHeight = static_cast<int>(result.sceneRect.height() - tablePad * 2 - input.dashboardHeight);
    if (input.gameStarted && (input.mode == Mode::Hulao || input.mode == Mode::ThreeVThree))
        result.tableHeight -= input.skin.photoVDistance;

    const int *seatRegions = 0;
    bool pkMode = false;
    if (!selectSeatRegions(input, seatRegions, pkMode))
        return result;

    PhotoLayoutFit::RegionCounts regionCounts = {{ 0, 0, 0, 0, 0, 0, 0, 0 }};
    for (int i = 0; i < input.photoCount; ++i)
        ++regionCounts[seatRegions[i]];

    if (input.smallPhotoSize.width() <= 0 || input.smallPhotoSize.height() <= 0
        || input.normalPhotoSize.width() <= 0 || input.normalPhotoSize.height() <= 0
        || input.bigPhotoSize.width() <= 0 || input.bigPhotoSize.height() <= 0)
        return result;
    result.seatsValid = true;

    const PhotoLayoutFit::LayoutDimensions smallDims = {
        static_cast<double>(input.smallPhotoSize.width()), static_cast<double>(input.smallPhotoSize.height()) };
    const PhotoLayoutFit::LayoutDimensions normal = {
        static_cast<double>(input.normalPhotoSize.width()), static_cast<double>(input.normalPhotoSize.height()) };
    const PhotoLayoutFit::LayoutDimensions big = {
        static_cast<double>(input.bigPhotoSize.width()), static_cast<double>(input.bigPhotoSize.height()) };
    const PhotoLayoutFit::Result fit = PhotoLayoutFit::choose(
        result.tableWidth, result.tableHeight, regionCounts, input.skin.photoHDistance,
        input.skin.photoVDistance, smallDims, normal, big);
    result.photoTier = fit.tier == PhotoLayoutFit::LayoutTier::Big ? PhotoTier::Big
        : fit.tier == PhotoLayoutFit::LayoutTier::Normal ? PhotoTier::Normal : PhotoTier::Small;
    result.photoScale = fit.scale;
    result.photoBaseSize = result.photoTier == PhotoTier::Big ? input.bigPhotoSize
        : result.photoTier == PhotoTier::Normal ? input.normalPhotoSize : input.smallPhotoSize;

    const double photoWidth = result.photoBaseSize.width() * result.photoScale;
    const double photoHeight = result.photoBaseSize.height() * result.photoScale;
    const double hGap = input.skin.photoHDistance * result.photoScale;
    const double vGap = input.skin.photoVDistance * result.photoScale;
    const double col1 = photoWidth + hGap;
    const double col2 = result.tableWidth - col1;
    const double row1 = photoHeight + vGap;
    const double row2 = result.tableHeight;
    const QRectF seatAreas[8] = {
        QRectF(col2, tablePad, col1, row1), QRectF(col1, tablePad, col2 - col1, row1),
        QRectF(tablePad, tablePad, col1, row1), QRectF(col2, row1, col1, row2 - row1),
        QRectF(tablePad, row1, col1, row2 - row1), QRectF(col2, tablePad, col1, row2),
        QRectF(tablePad, tablePad, col1, row2), QRectF(tablePad, tablePad, col1 + col2, row1)
    };
    const int regularAlignments[8] = {
        Qt::AlignRight | Qt::AlignTop, Qt::AlignHCenter | Qt::AlignTop,
        Qt::AlignLeft | Qt::AlignTop, Qt::AlignRight | Qt::AlignVCenter,
        Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignRight | Qt::AlignVCenter,
        Qt::AlignLeft | Qt::AlignVCenter, Qt::AlignHCenter | Qt::AlignTop
    };
    const int pkAlignments[8] = {
        Qt::AlignRight | Qt::AlignTop, Qt::AlignHCenter | Qt::AlignTop,
        Qt::AlignLeft | Qt::AlignTop, Qt::AlignRight | Qt::AlignBottom,
        Qt::AlignLeft | Qt::AlignBottom, Qt::AlignRight | Qt::AlignBottom,
        Qt::AlignLeft | Qt::AlignBottom, Qt::AlignHCenter | Qt::AlignTop
    };
    const bool horizontal[8] = { true, true, true, false, false, false, false, true };
    result.tableRect = QRectF(col1, row1, col2 - col1, row2 - row1);
    result.tableCenter = result.tableRect.center();
    QRect floating(0, 0, static_cast<int>(result.infoRect.left() - col1),
                   input.skin.dashboardFloatingAreaHeight);
    floating.moveBottomLeft(QPoint(static_cast<int>(result.tableRect.left()), 0));
    result.floatingArea = floating;
    result.discardPileSize = QSize(qMax(static_cast<int>(result.tableRect.width())
                                        - input.skin.discardPilePadding * 2,
                                        input.skin.discardPileMinWidth), input.skin.cardNormalHeight);

    QVector<int> ordered[8];
    for (int i = 0; i < input.photoCount; ++i) {
        const int region = seatRegions[i];
        if (region == 4 || region == 6)
            ordered[region].append(i);
        else
            ordered[region].prepend(i);
    }
    result.photos.resize(input.photoCount);
    for (int region = 0; region < 8; ++region) {
        const int count = ordered[region].size();
        for (int index = 0; index < count; ++index) {
            const int photoIndex = ordered[region][index];
            QRect photoFloating(0, 0, input.skin.photoHDistance, result.photoBaseSize.height());
            if (region == 0 || region == 3 || region == 5)
                photoFloating.moveRight(0);
            else
                photoFloating.moveLeft(result.photoBaseSize.width());
            PhotoPlacement &placement = result.photos[photoIndex];
            placement.region = region;
            placement.floatingArea = photoFloating;
            placement.position = disperse(seatAreas[region], horizontal[region],
                pkMode ? pkAlignments[region] : regularAlignments[region], count,
                photoWidth, photoHeight, hGap, vGap, index);
        }
    }
    return result;
}

namespace
{
bool validRect(const QRectF &rect)
{
    return rect.width() > 0.0 && rect.height() > 0.0
        && qIsFinite(rect.x()) && qIsFinite(rect.y())
        && qIsFinite(rect.width()) && qIsFinite(rect.height());
}

bool containsBounds(const QRectF &outer, const QRectF &inner)
{
    return inner.left() >= outer.left() && inner.top() >= outer.top()
        && inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

QRectF inset(const QRectF &rect, double amount)
{
    const double xInset = qMin(amount, rect.width() / 2.0);
    const double yInset = qMin(amount, rect.height() / 2.0);
    return rect.adjusted(xInset, yInset, -xInset, -yInset);
}

Profile chooseProfile(const ResponsiveInput &input, bool foldValid)
{
    if (foldValid)
        return input.fold.posture == FoldPosture::Book ? Profile::Book : Profile::Tabletop;

    const QRectF &basis = input.stableRect;
    const bool wasExpanded = input.hasPreviousProfile
        && input.previousProfile == Profile::ExpandedSplit;
    const double expandedWidth = wasExpanded ? 976.0 : 1000.0;
    const double expandedHeight = wasExpanded ? 576.0 : 600.0;
    if (basis.width() >= expandedWidth && basis.height() >= expandedHeight
        && basis.width() - 320.0 - input.gap >= (wasExpanded ? 616.0 : 640.0))
        return Profile::ExpandedSplit;

    const bool wasMedium = input.hasPreviousProfile
        && (input.previousProfile == Profile::Medium || wasExpanded);
    const double shortSide = qMin(basis.width(), basis.height());
    if (shortSide >= (wasMedium ? 576.0 : 600.0))
        return Profile::Medium;

    const bool wasPortrait = input.hasPreviousProfile
        && input.previousProfile == Profile::CompactPortrait;
    const bool wasLandscape = input.hasPreviousProfile
        && input.previousProfile == Profile::CompactLandscape;
    const double orientationHysteresis = 24.0;
    const bool portrait = wasPortrait ? basis.height() >= basis.width() - orientationHysteresis
        : wasLandscape ? basis.height() > basis.width() + orientationHysteresis
        : basis.height() > basis.width();
    return portrait ? Profile::CompactPortrait : Profile::CompactLandscape;
}

QRectF bottomZone(const QRectF &rect, double fraction, double maximumHeight)
{
    const double desired = rect.height() * fraction;
    const double minimum = qMin(rect.height(), 2.0 * 48.0);
    const double height = qMin(rect.height(), qMax(minimum, qMin(maximumHeight, desired)));
    return QRectF(rect.left(), rect.bottom() - height, rect.width(), height);
}

bool placeRing(ResponsiveResult &result, int photoCount, const QSize &photoSize, double gap)
{
    if (photoCount == 0) {
        result.seatsRect = QRectF();
        result.photos.clear();
        return true;
    }
    const QRectF &area = result.seatsRect;
    const double rx = qMax(0.0, area.width() / 2.0 - photoSize.width() / 2.0);
    const double ry = qMax(0.0, area.height() / 2.0 - photoSize.height() / 2.0);
    double centerLeft = area.left();
    double centerRight = area.right();
    double centerTop = area.top();
    double centerBottom = area.bottom();
    result.photos.reserve(photoCount);
    for (int i = 0; i < photoCount; ++i) {
        const double angle = 0.7853981633974483
            - (6.2831853071795865 * i / qMax(1, photoCount));
        ResponsivePhotoPlacement placement;
        placement.seat = i;
        placement.center = QPointF(area.center().x() + rx * std::cos(angle),
                                   area.center().y() + ry * std::sin(angle));
        const QRectF photoRect(placement.center.x() - photoSize.width() / 2.0,
                               placement.center.y() - photoSize.height() / 2.0,
                               photoSize.width(), photoSize.height());
        if (!area.contains(photoRect))
            return false;
        if (placement.center.x() >= area.center().x())
            centerRight = qMin(centerRight, photoRect.left() - gap);
        else
            centerLeft = qMax(centerLeft, photoRect.right() + gap);
        if (placement.center.y() >= area.center().y())
            centerBottom = qMin(centerBottom, photoRect.top() - gap);
        else
            centerTop = qMax(centerTop, photoRect.bottom() + gap);
        for (int previous = 0; previous < result.photos.size(); ++previous) {
            const QPointF &otherCenter = result.photos[previous].center;
            const double dx = std::abs(placement.center.x() - otherCenter.x());
            const double dy = std::abs(placement.center.y() - otherCenter.y());
            const bool separated = dx >= photoSize.width() + gap
                || dy >= photoSize.height() + gap;
            if (!separated)
                return false;
        }
        result.photos.append(placement);
    }
    // Reserve the center for the pile/table content, clear of every seat photograph.
    const QRectF center(centerLeft, centerTop, centerRight - centerLeft,
                        centerBottom - centerTop);
    if (!validRect(center))
        return false;
    result.tableRect = center;
    return true;
}

bool placeRibbon(ResponsiveResult &result, int photoCount, const QSize &photoSize,
                 double gap, const QRectF &area)
{
    result.seatPresentation = SeatPresentation::Ribbon;
    const double height = photoSize.height() + gap;
    if (area.width() <= 0.0 || area.height() < height) {
        result.seatsRect = QRectF();
        result.photos.clear();
        return true;
    }
    result.seatsRect = QRectF(area.left(), area.bottom() - height, area.width(), height);
    result.tableRect.setBottom(qMax(result.tableRect.top(), result.seatsRect.top() - gap));
    result.photos.reserve(photoCount);
    for (int i = 0; i < photoCount; ++i) {
        ResponsivePhotoPlacement placement;
        placement.seat = i;
        placement.center = QPointF(result.seatsRect.left() + photoSize.width() / 2.0
                                       + i * (photoSize.width() + gap),
                                   result.seatsRect.center().y());
        result.photos.append(placement);
    }
    return true;
}
}

ResponsiveResult computeResponsive(const ResponsiveInput &input)
{
    ResponsiveResult result;
    if (!validRect(input.stableRect) || !validRect(input.availableRect)
        || input.photoCount < 0 || input.photoCount > 19
        || input.smallPhotoSize.width() <= 0 || input.smallPhotoSize.height() <= 0
        || !qIsFinite(input.gap) || input.gap < 0.0
        || !qIsFinite(input.minimumTouchTarget) || input.minimumTouchTarget <= 0.0
        || !qIsFinite(input.headerHeight) || input.headerHeight < 0.0
        || !qIsFinite(input.interactionHeightFraction) || input.interactionHeightFraction <= 0.0)
        return result;

    // Accept platform fold snapshots only when they describe a real, contained hinge/crease.
    // A separating zero-width/height crease is valid; orientation comes from posture.
    bool foldValid = false;
    bool hasBoundedFoldFallback = false;
    QRectF firstPane;
    QRectF secondPane;
    QRectF foldFallbackPane;
    const bool hasFoldSignal = input.fold.separating
        || (input.fold.occluding && input.fold.bounds.width() > 0.0
            && input.fold.bounds.height() > 0.0);
    if (hasFoldSignal && input.fold.posture != FoldPosture::None
        && input.fold.bounds.width() >= 0.0 && input.fold.bounds.height() >= 0.0
        && containsBounds(input.stableRect, input.fold.bounds)) {
        const QRectF hinge = input.fold.bounds;
        if (input.fold.posture == FoldPosture::Book) {
            firstPane = QRectF(input.stableRect.left(), input.stableRect.top(),
                               hinge.left() - input.stableRect.left(), input.stableRect.height());
            secondPane = QRectF(hinge.right(), input.stableRect.top(),
                                input.stableRect.right() - hinge.right(), input.stableRect.height());
            foldValid = firstPane.width() >= 320.0 && secondPane.width() >= 320.0;
            if (!foldValid) {
                foldFallbackPane = firstPane.width() >= secondPane.width() ? firstPane : secondPane;
                hasBoundedFoldFallback = true;
            }
        } else if (input.fold.posture == FoldPosture::Tabletop) {
            firstPane = QRectF(input.stableRect.left(), input.stableRect.top(),
                               input.stableRect.width(), hinge.top() - input.stableRect.top());
            secondPane = QRectF(input.stableRect.left(), hinge.bottom(),
                                input.stableRect.width(), input.stableRect.bottom() - hinge.bottom());
            foldValid = firstPane.height() >= 240.0 && secondPane.height() >= 240.0;
            if (!foldValid) {
                foldFallbackPane = firstPane.height() >= secondPane.height() ? firstPane : secondPane;
                hasBoundedFoldFallback = true;
            }
        }
    }

    result.profile = chooseProfile(input, foldValid);
    result.valid = true;
    const double gap = input.gap;
    const double touch = qMax(48.0, input.minimumTouchTarget);
    QRectF available = input.availableRect;
    if (hasBoundedFoldFallback)
        available = available.intersected(foldFallbackPane);

    if (result.profile == Profile::ExpandedSplit) {
        const double inspectorWidth = qMax(320.0, input.inspectorMinimumSize.width());
        const double inspectorGap = gap;
        const bool canSplit = available.width() - inspectorWidth - inspectorGap >= 640.0;
        if (canSplit) {
            result.inspectorRect = QRectF(available.right() - inspectorWidth, available.top(),
                                          inspectorWidth, available.height());
            result.mainRect = QRectF(available.left(), available.top(),
                                    available.width() - inspectorWidth - inspectorGap,
                                    available.height());
        } else {
            result.mainRect = available;
            if (input.inspectorRequested || input.inspectorPinned)
                result.inspectorRect = QRectF(available.right() - qMin(inspectorWidth, available.width()),
                                              available.top(), qMin(inspectorWidth, available.width()),
                                              available.height());
        }
    } else if (result.profile == Profile::Book) {
        result.mainRect = available.intersected(QRectF(firstPane.left(), available.top(),
                                                        firstPane.width(), available.height()));
        result.inspectorRect = available.intersected(QRectF(secondPane.left(), available.top(),
                                                             secondPane.width(), available.height()));
    } else if (result.profile == Profile::Tabletop) {
        result.mainRect = available.intersected(QRectF(available.left(), firstPane.top(),
                                                        available.width(), firstPane.height()));
        result.interactionRect = available.intersected(QRectF(available.left(), secondPane.top(),
                                                               available.width(), secondPane.height()));
    } else {
        result.mainRect = available;
        if (input.inspectorRequested || input.inspectorPinned) {
            const double drawerWidth = qMin(qMax(320.0, input.inspectorMinimumSize.width()),
                                            available.width());
            result.inspectorRect = QRectF(available.right() - drawerWidth, available.top(),
                                          drawerWidth, available.height());
        }
    }
    const bool usesTabletopInteractionPane = result.profile == Profile::Tabletop
        && !result.interactionRect.isEmpty();
    if (!validRect(result.mainRect)) {
        result.valid = false;
        return result;
    }

    // Reserve the launcher inside the selected unoccluded pane. A view must not
    // guess this offset before fold fallback chooses a different pane.
    if (input.headerHeight > 0.0
        && result.mainRect.height() >= input.headerHeight + 2.0 * touch) {
        result.headerRect = QRectF(result.mainRect.left(), result.mainRect.top(),
                                   result.mainRect.width(), input.headerHeight);
        result.mainRect.setTop(result.headerRect.bottom());
    }
    result.tableRect = result.mainRect;
    if (result.interactionRect.isEmpty())
        result.interactionRect = bottomZone(result.mainRect, input.interactionHeightFraction, 400.0);
    else if (result.profile == Profile::Tabletop)
        result.interactionRect = inset(result.interactionRect, qMin(gap, result.interactionRect.height() / 4.0));

    if (!usesTabletopInteractionPane)
        result.tableRect.setBottom(qMax(result.tableRect.top(), result.interactionRect.top() - gap));

    // Visible log/chat panels occupy only the table band above interaction controls.
    if ((input.logVisible || input.chatVisible) && validRect(result.tableRect)) {
        const double panelWidth = qMin(result.tableRect.width() * 0.28, 320.0);
        const double panelHeight = result.tableRect.height();
        const QRectF panelColumn(result.tableRect.right() - panelWidth, result.tableRect.top(),
                                 panelWidth, panelHeight);
        result.tableRect.setRight(panelColumn.left() - gap);
        if (input.logVisible && input.chatVisible) {
            result.logRect = QRectF(panelColumn.left(), panelColumn.top(), panelColumn.width(),
                                    qMax(0.0, panelColumn.height() / 2.0 - gap / 2.0));
            result.chatRect = QRectF(panelColumn.left(), panelColumn.center().y() + gap / 2.0,
                                     panelColumn.width(),
                                     qMax(0.0, panelColumn.height() / 2.0 - gap / 2.0));
        } else if (input.logVisible) {
            result.logRect = panelColumn;
        } else {
            result.chatRect = panelColumn;
        }
    }

    const double actionHeight = qMin(touch, result.interactionRect.height());
    result.actionsRect = QRectF(result.interactionRect.left(),
                                result.interactionRect.bottom() - actionHeight,
                                result.interactionRect.width(), actionHeight);
    const double contentHeight = qMax(0.0, result.actionsRect.top() - result.interactionRect.top());
    const double handHeight = qMin(120.0, contentHeight * 0.6);
    result.handRect = QRectF(result.interactionRect.left(), result.interactionRect.top(),
                             result.interactionRect.width(), handHeight);
    result.promptRect = QRectF(result.handRect.left(), result.handRect.bottom(),
                               result.handRect.width(),
                               qMax(0.0, result.actionsRect.top() - result.handRect.bottom()));
    result.safeInteractionRect = inset(result.interactionRect,
                                       qMin(gap, qMin(result.interactionRect.width(), result.interactionRect.height()) / 4.0));
    result.seatsRect = result.tableRect;
    result.seatPresentation = SeatPresentation::Ring;
    const QRectF seatArea = result.tableRect;
    if (!placeRing(result, input.photoCount, input.smallPhotoSize, gap)) {
        result.photos.clear();
        result.tableRect = seatArea;
        if (!placeRibbon(result, input.photoCount, input.smallPhotoSize, gap, seatArea)) {
            result.valid = false;
            return result;
        }
    }
    result.tableCenter = validRect(result.tableRect) ? result.tableRect.center() : result.mainRect.center();

    // Handedness moves only the primary action target; hand and seat order stay stable.
    if (input.handedness != Handedness::None) {
        const double targetWidth = qMax(0.0, result.interactionRect.width() / 2.0 - gap / 2.0);
        result.safeInteractionRect.setWidth(targetWidth);
        if (input.handedness == Handedness::Left)
            result.safeInteractionRect.moveLeft(result.interactionRect.left() + gap);
        else
            result.safeInteractionRect.moveRight(result.interactionRect.right() - gap);
    }
    return result;
}
}
