#ifndef GAME_VIEW_H
#define GAME_VIEW_H

#include <QGraphicsView>
#include <QMargins>

class FitView final : public QGraphicsView
{
public:
    explicit FitView(QGraphicsScene *scene = nullptr, QWidget *parent = nullptr);
    void setSafeAreaMargins(const QMargins &margins);

    void setUiScale(qreal scale);
    void refit();
    void setBackgroundBrush(bool centerAsOrigin);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void fitCurrentScene(const QSize &viewportSize);

    qreal m_uiScale = 1.0;
};

#endif
