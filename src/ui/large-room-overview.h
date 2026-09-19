#ifndef LARGE_ROOM_OVERVIEW_H
#define LARGE_ROOM_OVERVIEW_H

#include <QGraphicsObject>
#include <memory>

class DesktopGamePresentation;
namespace RoomLayoutEngine { struct ResponsiveResult; }

// Native scene projection only. All target intents return to the existing draft.
class LargeRoomOverview final : public QGraphicsObject
{
public:
    explicit LargeRoomOverview(DesktopGamePresentation *presentation);
    ~LargeRoomOverview() override;
    void setLayout(const RoomLayoutEngine::ResponsiveResult &layout);
    QRectF boundingRect() const override;
    void paint(QPainter *, const QStyleOptionGraphicsItem *, QWidget *) override {}
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    struct Data;
    std::unique_ptr<Data> d;
};

#endif
