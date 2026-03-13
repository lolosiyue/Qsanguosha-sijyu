#ifndef SPINE_ANIMATION_H
#define SPINE_ANIMATION_H

#include <spine/SpineString.h>
#include <spine/Vector.h>

namespace spine {

class Skeleton;
class Event;
class MeshAttachment;

enum MixBlend {
    MixBlend_Setup,
    MixBlend_First,
    MixBlend_Replace,
    MixBlend_Add
};

enum MixDirection {
    MixDirection_In,
    MixDirection_Out
};

/// Base class for timeline entries.
class Timeline {
public:
    virtual ~Timeline() {}
    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) = 0;
    virtual int getPropertyId() = 0;
};

/// An animation composed of timelines.
class Animation {
public:
    Animation(const String &name, Vector<Timeline *> &timelines, float duration);
    ~Animation();

    const String &getName() const { return _name; }
    float getDuration() const { return _duration; }
    void setDuration(float d) { _duration = d; }

    Vector<Timeline *> &getTimelines() { return _timelines; }

    void apply(Skeleton &skeleton, float lastTime, float time, bool loop,
               Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction);

private:
    String _name;
    Vector<Timeline *> _timelines;
    float _duration;
};

// ─── Concrete timeline types ──────────────────────────────────────────

class CurveTimeline : public Timeline {
public:
    CurveTimeline(int frameCount);
    virtual ~CurveTimeline();

    void setLinear(int frameIndex);
    void setStepped(int frameIndex);
    void setCurve(int frameIndex, float cx1, float cy1, float cx2, float cy2);
    float getCurvePercent(int frameIndex, float percent);

protected:
    Vector<float> _curves;

private:
    static const int BEZIER_SIZE = 18;
};

class RotateTimeline : public CurveTimeline {
public:
    static const int ENTRIES = 2;

    RotateTimeline(int frameCount);
    void setFrame(int frameIndex, float time, float degrees);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _boneIndex;
    Vector<float> _frames;
};

class TranslateTimeline : public CurveTimeline {
public:
    static const int ENTRIES = 3;

    TranslateTimeline(int frameCount);
    void setFrame(int frameIndex, float time, float x, float y);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _boneIndex;
    Vector<float> _frames;
};

class ScaleTimeline : public TranslateTimeline {
public:
    ScaleTimeline(int frameCount);
    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;
};

class ShearTimeline : public TranslateTimeline {
public:
    ShearTimeline(int frameCount);
    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;
};

class ColorTimeline : public CurveTimeline {
public:
    static const int ENTRIES = 5;

    ColorTimeline(int frameCount);
    void setFrame(int frameIndex, float time, float r, float g, float b, float a);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _slotIndex;
    Vector<float> _frames;
};

class AttachmentTimeline : public Timeline {
public:
    AttachmentTimeline(int frameCount);
    void setFrame(int frameIndex, float time, const String &attachmentName);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _slotIndex;
    Vector<float> _frames;
    Vector<String> _attachmentNames;
};

class DeformTimeline : public CurveTimeline {
public:
    DeformTimeline(int frameCount);
    void setFrame(int frameIndex, float time, Vector<float> &vertices);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _slotIndex;
    MeshAttachment *_attachment;
    Vector<float> _frames;
    Vector<Vector<float>> _frameVertices;
};

class DrawOrderTimeline : public Timeline {
public:
    DrawOrderTimeline(int frameCount);
    void setFrame(int frameIndex, float time, Vector<int> &drawOrder);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    Vector<float> _frames;
    Vector<Vector<int>> _drawOrders;
};

/// IK constraint timeline — animates mix, softness, bendDirection, compress, stretch
class IkConstraintTimeline : public CurveTimeline {
public:
    static const int ENTRIES = 6; // time, mix, softness, bendDir, compress, stretch

    IkConstraintTimeline(int frameCount);
    void setFrame(int frameIndex, float time, float mix, float softness,
                  int bendDirection, bool compress, bool stretch);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _ikConstraintIndex;
    Vector<float> _frames;
};

/// Transform constraint timeline — animates rotateMix, translateMix, scaleMix, shearMix
class TransformConstraintTimeline : public CurveTimeline {
public:
    static const int ENTRIES = 5; // time, rotateMix, translateMix, scaleMix, shearMix

    TransformConstraintTimeline(int frameCount);
    void setFrame(int frameIndex, float time, float rotateMix, float translateMix,
                  float scaleMix, float shearMix);

    virtual void apply(Skeleton &skeleton, float lastTime, float time, Vector<Event *> *events,
                       float alpha, MixBlend blend, MixDirection direction) override;
    virtual int getPropertyId() override;

    int _transformConstraintIndex;
    Vector<float> _frames;
};

} // namespace spine

#endif // SPINE_ANIMATION_H
