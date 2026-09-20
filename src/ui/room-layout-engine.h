#ifndef ROOM_LAYOUT_ENGINE_H
#define ROOM_LAYOUT_ENGINE_H

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QVector>

namespace RoomLayoutEngine
{
enum class Mode
{
    Regular,
    Hulao,
    Boss,
    ThreeVThree
};

enum class PhotoTier
{
    Small,
    Normal,
    Big
};

enum class Profile
{
    LegacyLandscape,
    CompactPortrait,
    CompactLandscape,
    Medium,
    ExpandedSplit,
    Book,
    Tabletop,
    LargeRoom
};

enum class Handedness
{
    None,
    Left,
    Right
};

enum class FoldPosture
{
    None,
    Book,
    Tabletop
};

enum class SeatPresentation
{
    Ring,
    Ribbon
};

struct FoldInfo
{
    FoldPosture posture = FoldPosture::None;
    QRectF bounds;
    bool separating = false;
    bool occluding = false;
};

// Responsive geometry uses Qt logical coordinates. stableRect selects a profile;
// availableRect may be shorter because of the keyboard and only sizes content zones.
struct ResponsiveInput
{
    QRectF stableRect;
    QRectF availableRect;
    Profile previousProfile = Profile::LegacyLandscape;
    bool hasPreviousProfile = false;
    FoldInfo fold;
    Handedness handedness = Handedness::None;
    int photoCount = 0; // Other players; placement order is the input index order.
    bool largeRoom = false; // Desktop only; never indexes legacy seat tables.
    int selfSeat = 0;
    QSize smallPhotoSize;
    QSizeF inspectorMinimumSize = QSizeF(320.0, 0.0);
    double gap = 16.0;
    double minimumTouchTarget = 48.0;
    double headerHeight = 0.0;
    double promptHeight = 0.0; // Space above the native hand reserved for its live prompt.
    double interactionHeightFraction = 0.38;
    double minimumInteractionHeight = 0.0;
    int firstVisibleSeat = 0;
    bool inspectorRequested = false;
    bool inspectorPinned = false;
    bool logVisible = false;
    bool chatVisible = false;
};

struct ResponsivePhotoPlacement
{
    int seat = 0;
    QPointF center;
    bool visible = true;
};

struct ResponsiveResult
{
    bool valid = false;
    Profile profile = Profile::LegacyLandscape;
    SeatPresentation seatPresentation = SeatPresentation::Ring;
    QRectF headerRect;
    QRectF mainRect;
    QRectF tableRect;
    QPointF tableCenter;
    QRectF interactionRect;
    QRectF inspectorRect;
    QRectF seatsRect;
    QRectF resolutionRect;
    QRectF actionsRect;
    QRectF handRect;
    QRectF promptRect;
    QRectF logRect;
    QRectF chatRect;
    bool logAlwaysVisible = false; // Landscape large rooms retain the native right-hand log.
    bool nativeChrome = false; // Use the skin's original log/chat, roles and background.
    QRectF safeInteractionRect;
    QVector<ResponsivePhotoPlacement> photos;
    int firstVisibleSeat = 0;
    int visibleSeatCount = 0;
};

// Native skin frames are rearranged, retaining their original children and input.
struct DashboardGeometry
{
    QRectF handRect;
    QRectF handRowRect;
    QRectF skillRect;
    QPointF equipmentPosition;
    QPointF avatarPosition;
    QRectF confirmRect;
    QRectF cancelRect;
    QRectF finishRect;
    QRectF trustRect;
    double footerScale = 1.0;
    double equipmentScale = 1.0;
    double height = 0.0;
};

DashboardGeometry computeDashboard(const QSizeF &available, double handHeight,
    const QSizeF &equipment, const QSizeF &avatar,
    Handedness handedness, double skillHeight = 48.0, double cardLift = 0.0);

struct SkinMetrics
{
    int scenePadding = 0;
    double photoRoomPadding = 0.0;
    double photoDashboardPadding = 0.0;
    int roleBoxHeight = 0;
    double infoPlaneWidthPercentage = 0.0;
    double logBoxHeightPercentage = 0.0;
    double chatBoxHeightPercentage = 0.0;
    int chatTextBoxHeight = 0;
    int photoHDistance = 0;
    int photoVDistance = 0;
    int discardPilePadding = 0;
    int discardPileMinWidth = 0;
    int dashboardNormalHeight = 0;
    int dashboardFloatingAreaHeight = 0;
    int cardNormalHeight = 0;
};

struct Input
{
    QRectF viewport;
    QSize minimumSceneSize;
    QSize maximumSceneSize;
    SkinMetrics skin;
    double dashboardHeight = 0.0;
    double avatarSceneHeight = 0.0;
    QSizeF chatButtonSize;
    QSizeF selfBoxSize;
    QSizeF enemyBoxSize;
    int photoCount = 0;
    int selfSeat = 0;
    Mode mode = Mode::Regular;
    bool gameStarted = false;
    bool clampScene = true;
    QSize smallPhotoSize;
    QSize normalPhotoSize;
    QSize bigPhotoSize;
};

struct PhotoPlacement
{
    int region = -1;
    QPointF position;
    QRect floatingArea;
};

struct Result
{
    bool valid = false;
    bool seatsValid = false;
    QRectF sceneRect;
    double sceneScale = 1.0;
    QRectF displayRect;
    QRectF dashboardRect;
    QRectF infoRect;
    QPointF roleBoxPosition;
    QRectF logRect;
    QSize logSizeWithChat;
    QSize logSizeWithoutChat;
    QPointF chatBoxPosition;
    QRectF chatRect;
    QPointF chatEditPosition;
    QSizeF chatEditSize;
    QPointF chatButtonPosition;
    QPointF selfBoxPosition;
    QPointF enemyBoxPosition;
    QSize backgroundSize;
    int backgroundTableHeight = 0;
    int tableWidth = 0;
    int tableHeight = 0;
    QRectF tableRect;
    QPointF tableCenter;
    QRect floatingArea;
    QSize discardPileSize;
    QPointF timerPosition;
    PhotoTier photoTier = PhotoTier::Small;
    QSize photoBaseSize;
    double photoScale = 1.0;
    QVector<PhotoPlacement> photos;
};

Result compute(const Input &input);
ResponsiveResult computeResponsive(const ResponsiveInput &input);
ResponsiveResult computeLargeRoom(const ResponsiveInput &input, const Result &frame);
}

#endif
