/******************************************************************************
 * spine-cpp Animation & Timeline implementations
 *****************************************************************************/

#include <spine/Animation.h>
#include <spine/Skeleton.h>
#include <spine/Bone.h>
#include <spine/Slot.h>
#include <spine/MeshAttachment.h>
#include <spine/EventData.h>
#include <spine/IkConstraint.h>
#include <spine/TransformConstraint.h>
#include <cmath>
#include <algorithm>

namespace spine {

// ─── Animation ──────────────────────────────────────────────────────────────

Animation::Animation(const String &name, Vector<Timeline *> &timelines, float duration)
    : _name(name), _timelines(timelines), _duration(duration)
{
}

Animation::~Animation() {
    for (size_t i = 0; i < _timelines.size(); ++i)
        delete _timelines[i];
}

void Animation::apply(Skeleton &skeleton, float lastTime, float time, bool loop,
                       Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    if (loop && _duration != 0) {
        time = std::fmod(time, _duration);
        if (lastTime > 0) lastTime = std::fmod(lastTime, _duration);
    }
    for (size_t i = 0; i < _timelines.size(); ++i) {
        _timelines[i]->apply(skeleton, lastTime, time, events, alpha, blend, direction);
    }
}

// ─── CurveTimeline ─────────────────────────────────────────────────────────

CurveTimeline::CurveTimeline(int frameCount) {
    _curves.setSize(frameCount * BEZIER_SIZE, 0.0f);
}

CurveTimeline::~CurveTimeline() {}

void CurveTimeline::setLinear(int frameIndex) {
    _curves[frameIndex * BEZIER_SIZE] = 0; // LINEAR
}

void CurveTimeline::setStepped(int frameIndex) {
    _curves[frameIndex * BEZIER_SIZE] = 1; // STEPPED
}

void CurveTimeline::setCurve(int frameIndex, float cx1, float cy1, float cx2, float cy2) {
    float tmpx = (-cx1 * 2 + cx2) * 0.03f;
    float tmpy = (-cy1 * 2 + cy2) * 0.03f;
    float dddfx = ((cx1 - cx2) * 3.0f + 1.0f) * 0.006f;
    float dddfy = ((cy1 - cy2) * 3.0f + 1.0f) * 0.006f;
    float ddfx = tmpx * 2 + dddfx;
    float ddfy = tmpy * 2 + dddfy;
    float dfx = cx1 * 0.3f + tmpx + dddfx * 0.16666666f;
    float dfy = cy1 * 0.3f + tmpy + dddfy * 0.16666666f;

    int i = frameIndex * BEZIER_SIZE;
    _curves[i++] = 2; // BEZIER

    float x = dfx, y = dfy;
    for (int n = i + BEZIER_SIZE - 2; i < n; i += 2) {
        _curves[i] = x;
        _curves[i + 1] = y;
        dfx += ddfx;
        dfy += ddfy;
        ddfx += dddfx;
        ddfy += dddfy;
        x += dfx;
        y += dfy;
    }
}

float CurveTimeline::getCurvePercent(int frameIndex, float percent) {
    int i = frameIndex * BEZIER_SIZE;
    float type = _curves[i];
    if (type == 0) return percent; // LINEAR
    if (type == 1) return 0;       // STEPPED
    i++;
    float x = 0;
    for (int start = i, n = i + BEZIER_SIZE - 2; i < n; i += 2) {
        x = _curves[i];
        if (x >= percent) {
            float prevX, prevY;
            if (i == start) {
                prevX = 0;
                prevY = 0;
            } else {
                prevX = _curves[i - 2];
                prevY = _curves[i - 1];
            }
            return prevY + (_curves[i + 1] - prevY) * (percent - prevX) / (x - prevX);
        }
    }
    float y = _curves[i - 1];
    return y + (1 - y) * (percent - x) / (1 - x);
}

// ─── RotateTimeline ─────────────────────────────────────────────────────────

RotateTimeline::RotateTimeline(int frameCount)
    : CurveTimeline(frameCount), _boneIndex(0)
{
    _frames.setSize(frameCount * ENTRIES, 0.0f);
}

void RotateTimeline::setFrame(int frameIndex, float time, float degrees) {
    frameIndex *= ENTRIES;
    _frames[frameIndex] = time;
    _frames[frameIndex + 1] = degrees;
}

void RotateTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                            Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Bone *bone = skeleton.getBones()[_boneIndex];
    if (!bone) return;

    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First)
            bone->setRotation(bone->getData()._rotation);
        return;
    }

    float r;
    int frameCount = static_cast<int>(_frames.size()) / ENTRIES;
    if (time >= _frames[(frameCount - 1) * ENTRIES]) {
        r = _frames[(frameCount - 1) * ENTRIES + 1];
    } else {
        // Binary search for frame
        int frame = 0;
        for (int i = ENTRIES; i < static_cast<int>(_frames.size()); i += ENTRIES) {
            if (_frames[i] > time) { frame = i / ENTRIES - 1; break; }
        }
        float frameTime = _frames[frame * ENTRIES];
        float percent = 1.0f - (time - _frames[(frame + 1) * ENTRIES]) /
                        (frameTime - _frames[(frame + 1) * ENTRIES]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));

        float prevR = _frames[frame * ENTRIES + 1];
        r = _frames[(frame + 1) * ENTRIES + 1];
        float diff = r - prevR;
        diff -= (16384 - static_cast<int>(16384.499999999996 - diff / 360.0)) * 360;
        r = prevR + diff * percent;
    }

    if (blend == MixBlend_Setup)
        bone->setRotation(bone->getData()._rotation + r * alpha);
    else if (blend == MixBlend_First || blend == MixBlend_Replace)
        bone->setRotation(bone->getRotation() + (bone->getData()._rotation + r - bone->getRotation()) * alpha);
    else
        bone->setRotation(bone->getRotation() + r * alpha);
}

int RotateTimeline::getPropertyId() {
    return (0 << 24) + _boneIndex;
}

// ─── TranslateTimeline ──────────────────────────────────────────────────────

TranslateTimeline::TranslateTimeline(int frameCount)
    : CurveTimeline(frameCount), _boneIndex(0)
{
    _frames.setSize(frameCount * ENTRIES, 0.0f);
}

void TranslateTimeline::setFrame(int frameIndex, float time, float x, float y) {
    frameIndex *= ENTRIES;
    _frames[frameIndex] = time;
    _frames[frameIndex + 1] = x;
    _frames[frameIndex + 2] = y;
}

void TranslateTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                               Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Bone *bone = skeleton.getBones()[_boneIndex];
    if (!bone) return;

    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First) {
            bone->setX(bone->getData()._x);
            bone->setY(bone->getData()._y);
        }
        return;
    }

    float x, y;
    int frameCount = static_cast<int>(_frames.size()) / ENTRIES;
    if (time >= _frames[(frameCount - 1) * ENTRIES]) {
        x = _frames[(frameCount - 1) * ENTRIES + 1];
        y = _frames[(frameCount - 1) * ENTRIES + 2];
    } else {
        int frame = 0;
        for (int i = ENTRIES; i < static_cast<int>(_frames.size()); i += ENTRIES) {
            if (_frames[i] > time) { frame = i / ENTRIES - 1; break; }
        }
        float frameTime = _frames[frame * ENTRIES];
        float percent = 1.0f - (time - _frames[(frame + 1) * ENTRIES]) /
                        (frameTime - _frames[(frame + 1) * ENTRIES]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));

        x = _frames[frame * ENTRIES + 1];
        y = _frames[frame * ENTRIES + 2];
        x += (_frames[(frame + 1) * ENTRIES + 1] - x) * percent;
        y += (_frames[(frame + 1) * ENTRIES + 2] - y) * percent;
    }

    if (blend == MixBlend_Setup) {
        bone->setX(bone->getData()._x + x * alpha);
        bone->setY(bone->getData()._y + y * alpha);
    } else {
        bone->setX(bone->getX() + (bone->getData()._x + x - bone->getX()) * alpha);
        bone->setY(bone->getY() + (bone->getData()._y + y - bone->getY()) * alpha);
    }
}

int TranslateTimeline::getPropertyId() {
    return (1 << 24) + _boneIndex;
}

// ─── ScaleTimeline ──────────────────────────────────────────────────────────

ScaleTimeline::ScaleTimeline(int frameCount) : TranslateTimeline(frameCount) {}

void ScaleTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                           Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Bone *bone = skeleton.getBones()[_boneIndex];
    if (!bone) return;

    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First) {
            bone->setScaleX(bone->getData()._scaleX);
            bone->setScaleY(bone->getData()._scaleY);
        }
        return;
    }

    float x, y;
    int frameCount_val = static_cast<int>(_frames.size()) / ENTRIES;
    if (time >= _frames[(frameCount_val - 1) * ENTRIES]) {
        x = _frames[(frameCount_val - 1) * ENTRIES + 1];
        y = _frames[(frameCount_val - 1) * ENTRIES + 2];
    } else {
        int frame = 0;
        for (int i = ENTRIES; i < static_cast<int>(_frames.size()); i += ENTRIES) {
            if (_frames[i] > time) { frame = i / ENTRIES - 1; break; }
        }
        float ft = _frames[frame * ENTRIES];
        float percent = 1.0f - (time - _frames[(frame + 1) * ENTRIES]) /
                        (ft - _frames[(frame + 1) * ENTRIES]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));
        x = _frames[frame * ENTRIES + 1];
        y = _frames[frame * ENTRIES + 2];
        x += (_frames[(frame + 1) * ENTRIES + 1] - x) * percent;
        y += (_frames[(frame + 1) * ENTRIES + 2] - y) * percent;
    }

    if (blend == MixBlend_Setup) {
        bone->setScaleX(bone->getData()._scaleX * (1 - alpha) + x * alpha);
        bone->setScaleY(bone->getData()._scaleY * (1 - alpha) + y * alpha);
    } else {
        bone->setScaleX(bone->getScaleX() * (1 - alpha) + x * alpha);
        bone->setScaleY(bone->getScaleY() * (1 - alpha) + y * alpha);
    }
}

int ScaleTimeline::getPropertyId() {
    return (2 << 24) + _boneIndex;
}

// ─── ShearTimeline ───────────────────────────────────────────────────────────

ShearTimeline::ShearTimeline(int frameCount) : TranslateTimeline(frameCount) {}

void ShearTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                           Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Bone *bone = skeleton.getBones()[_boneIndex];
    if (!bone) return;

    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First) {
            bone->setShearX(bone->getData()._shearX);
            bone->setShearY(bone->getData()._shearY);
        }
        return;
    }

    float x, y;
    int frameCount_val = static_cast<int>(_frames.size()) / ENTRIES;
    if (time >= _frames[(frameCount_val - 1) * ENTRIES]) {
        x = _frames[(frameCount_val - 1) * ENTRIES + 1];
        y = _frames[(frameCount_val - 1) * ENTRIES + 2];
    } else {
        int frame = 0;
        for (int i = ENTRIES; i < static_cast<int>(_frames.size()); i += ENTRIES) {
            if (_frames[i] > time) { frame = i / ENTRIES - 1; break; }
        }
        float ft = _frames[frame * ENTRIES];
        float percent = 1.0f - (time - _frames[(frame + 1) * ENTRIES]) /
                        (ft - _frames[(frame + 1) * ENTRIES]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));
        x = _frames[frame * ENTRIES + 1];
        y = _frames[frame * ENTRIES + 2];
        x += (_frames[(frame + 1) * ENTRIES + 1] - x) * percent;
        y += (_frames[(frame + 1) * ENTRIES + 2] - y) * percent;
    }

    if (blend == MixBlend_Setup) {
        bone->setShearX(bone->getData()._shearX + x * alpha);
        bone->setShearY(bone->getData()._shearY + y * alpha);
    } else {
        bone->setShearX(bone->getShearX() + (bone->getData()._shearX + x - bone->getShearX()) * alpha);
        bone->setShearY(bone->getShearY() + (bone->getData()._shearY + y - bone->getShearY()) * alpha);
    }
}

int ShearTimeline::getPropertyId() {
    return (9 << 24) + _boneIndex;
}

// ─── ColorTimeline ──────────────────────────────────────────────────────────

ColorTimeline::ColorTimeline(int frameCount)
    : CurveTimeline(frameCount), _slotIndex(0)
{
    _frames.setSize(frameCount * ENTRIES, 0.0f);
}

void ColorTimeline::setFrame(int frameIndex, float time, float r, float g, float b, float a) {
    frameIndex *= ENTRIES;
    _frames[frameIndex] = time;
    _frames[frameIndex + 1] = r;
    _frames[frameIndex + 2] = g;
    _frames[frameIndex + 3] = b;
    _frames[frameIndex + 4] = a;
}

void ColorTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                           Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Slot *slot = skeleton.getSlots()[_slotIndex];
    if (!slot) return;

    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First)
            slot->getColor().set(slot->getData()._color);
        return;
    }

    float r, g, b, a;
    int fc = static_cast<int>(_frames.size()) / ENTRIES;
    if (time >= _frames[(fc - 1) * ENTRIES]) {
        int i = (fc - 1) * ENTRIES;
        r = _frames[i + 1]; g = _frames[i + 2]; b = _frames[i + 3]; a = _frames[i + 4];
    } else {
        int frame = 0;
        for (int i = ENTRIES; i < static_cast<int>(_frames.size()); i += ENTRIES) {
            if (_frames[i] > time) { frame = i / ENTRIES - 1; break; }
        }
        float ft = _frames[frame * ENTRIES];
        float percent = 1.0f - (time - _frames[(frame + 1) * ENTRIES]) /
                        (ft - _frames[(frame + 1) * ENTRIES]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));
        int i = frame * ENTRIES;
        r = _frames[i + 1] + (_frames[i + ENTRIES + 1] - _frames[i + 1]) * percent;
        g = _frames[i + 2] + (_frames[i + ENTRIES + 2] - _frames[i + 2]) * percent;
        b = _frames[i + 3] + (_frames[i + ENTRIES + 3] - _frames[i + 3]) * percent;
        a = _frames[i + 4] + (_frames[i + ENTRIES + 4] - _frames[i + 4]) * percent;
    }

    Color &color = slot->getColor();
    if (alpha == 1) {
        color.set(r, g, b, a);
    } else {
        if (blend == MixBlend_Setup) color.set(slot->getData()._color);
        color.r += (r - color.r) * alpha;
        color.g += (g - color.g) * alpha;
        color.b += (b - color.b) * alpha;
        color.a += (a - color.a) * alpha;
    }
}

int ColorTimeline::getPropertyId() {
    return (3 << 24) + _slotIndex;
}

// ─── AttachmentTimeline ─────────────────────────────────────────────────────

AttachmentTimeline::AttachmentTimeline(int frameCount) : _slotIndex(0) {
    _frames.setSize(frameCount, 0.0f);
    _attachmentNames.setSize(frameCount);
}

void AttachmentTimeline::setFrame(int frameIndex, float time, const String &attachmentName) {
    _frames[frameIndex] = time;
    _attachmentNames[frameIndex] = attachmentName;
}

void AttachmentTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                                Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)alpha; (void)direction;

    Slot *slot = skeleton.getSlots()[_slotIndex];
    if (!slot) return;

    if (blend == MixBlend_Setup || blend == MixBlend_First) {
        // Find the frame
        int frameIndex = 0;
        if (time >= _frames[_frames.size() - 1])
            frameIndex = static_cast<int>(_frames.size()) - 1;
        else {
            for (size_t i = 0; i < _frames.size(); ++i) {
                if (_frames[i] > time) { frameIndex = static_cast<int>(i) - 1; break; }
            }
        }
        if (frameIndex < 0) frameIndex = 0;

        const String &name = _attachmentNames[frameIndex];
        if (name.isEmpty())
            slot->setAttachment(nullptr);
        else
            slot->setAttachment(skeleton.getAttachment(_slotIndex, name));
    }
}

int AttachmentTimeline::getPropertyId() {
    return (4 << 24) + _slotIndex;
}

// ─── DeformTimeline ─────────────────────────────────────────────────────────

DeformTimeline::DeformTimeline(int frameCount)
    : CurveTimeline(frameCount), _slotIndex(0), _attachment(nullptr)
{
    _frames.setSize(frameCount, 0.0f);
    _frameVertices.setSize(frameCount);
}

void DeformTimeline::setFrame(int frameIndex, float time, Vector<float> &vertices) {
    _frames[frameIndex] = time;
    _frameVertices[frameIndex] = vertices;
}

void DeformTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                            Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;
    Slot *slot = skeleton.getSlots()[_slotIndex];
    if (!slot || slot->getAttachment() != _attachment) return;

    Vector<float> &deform = slot->getDeform();
    if (deform.isEmpty()) {
        blend = MixBlend_Setup;
    }

    int fc = static_cast<int>(_frames.size());
    if (time < _frames[0]) {
        if (blend == MixBlend_Setup || blend == MixBlend_First) deform.clear();
        return;
    }

    Vector<float> *vertices;
    if (time >= _frames[fc - 1]) {
        vertices = &_frameVertices[fc - 1];
    } else {
        int frame = 0;
        for (int i = 1; i < fc; ++i) {
            if (_frames[i] > time) { frame = i - 1; break; }
        }
        float ft = _frames[frame];
        float percent = 1.0f - (time - _frames[frame + 1]) / (ft - _frames[frame + 1]);
        percent = getCurvePercent(frame, std::max(0.0f, std::min(1.0f, percent)));

        // Interpolate
        vertices = &_frameVertices[frame];
        Vector<float> &next = _frameVertices[frame + 1];
        size_t count = vertices->size();
        deform.setSize(count);
        for (size_t i = 0; i < count; ++i) {
            float prev = (*vertices)[i];
            deform[i] = prev + (next[i] - prev) * percent;
        }
        return;
    }

    size_t count = vertices->size();
    deform.setSize(count);
    if (alpha == 1) {
        for (size_t i = 0; i < count; ++i)
            deform[i] = (*vertices)[i];
    } else {
        for (size_t i = 0; i < count; ++i)
            deform[i] += ((*vertices)[i] - deform[i]) * alpha;
    }
}

int DeformTimeline::getPropertyId() {
    return (5 << 24) + _slotIndex;
}

// ─── DrawOrderTimeline ──────────────────────────────────────────────────────

DrawOrderTimeline::DrawOrderTimeline(int frameCount) {
    _frames.setSize(frameCount, 0.0f);
    _drawOrders.setSize(frameCount);
}

void DrawOrderTimeline::setFrame(int frameIndex, float time, Vector<int> &drawOrder) {
    _frames[frameIndex] = time;
    _drawOrders[frameIndex] = drawOrder;
}

void DrawOrderTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                               Vector<Event *> *events, float alpha, MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)alpha; (void)direction;

    Vector<Slot *> &drawOrder = skeleton.getDrawOrder();
    Vector<Slot *> &slotsRef = skeleton.getSlots();

    if (blend == MixBlend_Setup || blend == MixBlend_First) {
        int fc = static_cast<int>(_frames.size());
        int frameIndex;
        if (time >= _frames[fc - 1])
            frameIndex = fc - 1;
        else {
            frameIndex = 0;
            for (int i = 1; i < fc; ++i) {
                if (_frames[i] > time) { frameIndex = i - 1; break; }
            }
        }

        Vector<int> &order = _drawOrders[frameIndex];
        if (order.isEmpty()) {
            for (size_t i = 0; i < slotsRef.size(); ++i)
                drawOrder[i] = slotsRef[i];
        } else {
            for (size_t i = 0; i < order.size(); ++i)
                drawOrder[i] = slotsRef[order[i]];
        }
    }
}

int DrawOrderTimeline::getPropertyId() {
    return (6 << 24);
}

// ─── IkConstraintTimeline ──────────────────────────────────────────────────

IkConstraintTimeline::IkConstraintTimeline(int frameCount)
    : CurveTimeline(frameCount), _ikConstraintIndex(0)
{
    _frames.setSize(frameCount * ENTRIES);
}

void IkConstraintTimeline::setFrame(int frameIndex, float time, float mix, float softness,
                                     int bendDirection, bool compress, bool stretch) {
    int i = frameIndex * ENTRIES;
    _frames[i]     = time;
    _frames[i + 1] = mix;
    _frames[i + 2] = softness;
    _frames[i + 3] = (float)bendDirection;
    _frames[i + 4] = compress ? 1.0f : 0.0f;
    _frames[i + 5] = stretch ? 1.0f : 0.0f;
}

void IkConstraintTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                                  Vector<Event *> *events, float alpha,
                                  MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;

    Vector<IkConstraint *> &constraints = skeleton.getIkConstraints();
    if (_ikConstraintIndex >= (int)constraints.size()) return;
    IkConstraint *constraint = constraints[_ikConstraintIndex];

    int fc = (int)_frames.size();
    if (fc == 0) return;

    if (time < _frames[0]) {
        // Before first frame
        switch (blend) {
        case MixBlend_Setup:
            constraint->_mix = constraint->_data._mix;
            constraint->_softness = constraint->_data._softness;
            constraint->_bendDirection = constraint->_data._bendDirection;
            constraint->_compress = constraint->_data._compress;
            constraint->_stretch = constraint->_data._stretch;
            return;
        case MixBlend_First:
            constraint->_mix += (constraint->_data._mix - constraint->_mix) * alpha;
            constraint->_softness += (constraint->_data._softness - constraint->_softness) * alpha;
            constraint->_bendDirection = constraint->_data._bendDirection;
            constraint->_compress = constraint->_data._compress;
            constraint->_stretch = constraint->_data._stretch;
            return;
        default:
            return;
        }
    }

    float mix, softness;
    int bendDir;
    bool compress, stretch;

    if (time >= _frames[fc - ENTRIES]) {
        // After last frame
        if (blend == MixBlend_Setup) {
            mix = constraint->_data._mix + (_frames[fc + 1 - ENTRIES] - constraint->_data._mix) * alpha;
            softness = constraint->_data._softness + (_frames[fc + 2 - ENTRIES] - constraint->_data._softness) * alpha;
        } else {
            mix = constraint->_mix + (_frames[fc + 1 - ENTRIES] - constraint->_mix) * alpha;
            softness = constraint->_softness + (_frames[fc + 2 - ENTRIES] - constraint->_softness) * alpha;
        }
        if (direction == MixDirection_Out) {
            bendDir = constraint->_data._bendDirection;
            compress = constraint->_data._compress;
            stretch = constraint->_data._stretch;
        } else {
            bendDir = (int)_frames[fc + 3 - ENTRIES];
            compress = _frames[fc + 4 - ENTRIES] != 0;
            stretch = _frames[fc + 5 - ENTRIES] != 0;
        }
    } else {
        // Interpolate
        int frame = 0;
        for (int i = ENTRIES; i < fc; i += ENTRIES) {
            if (_frames[i] > time) { frame = i - ENTRIES; break; }
        }

        float frameTime = _frames[frame];
        float percent = getCurvePercent(frame / ENTRIES,
                                         1 - (time - frameTime) / (_frames[frame + ENTRIES] - frameTime));

        float frameMix = _frames[frame + 1];
        float frameSoftness = _frames[frame + 2];
        if (blend == MixBlend_Setup) {
            mix = constraint->_data._mix + (frameMix + (_frames[frame + ENTRIES + 1] - frameMix) * percent - constraint->_data._mix) * alpha;
            softness = constraint->_data._softness + (frameSoftness + (_frames[frame + ENTRIES + 2] - frameSoftness) * percent - constraint->_data._softness) * alpha;
        } else {
            mix = constraint->_mix + (frameMix + (_frames[frame + ENTRIES + 1] - frameMix) * percent - constraint->_mix) * alpha;
            softness = constraint->_softness + (frameSoftness + (_frames[frame + ENTRIES + 2] - frameSoftness) * percent - constraint->_softness) * alpha;
        }
        if (direction == MixDirection_Out) {
            bendDir = constraint->_data._bendDirection;
            compress = constraint->_data._compress;
            stretch = constraint->_data._stretch;
        } else {
            bendDir = (int)_frames[frame + 3];
            compress = _frames[frame + 4] != 0;
            stretch = _frames[frame + 5] != 0;
        }
    }

    constraint->_mix = mix;
    constraint->_softness = softness;
    constraint->_bendDirection = bendDir;
    constraint->_compress = compress;
    constraint->_stretch = stretch;
}

int IkConstraintTimeline::getPropertyId() {
    return (7 << 24) + _ikConstraintIndex;
}

// ─── TransformConstraintTimeline ───────────────────────────────────────────

TransformConstraintTimeline::TransformConstraintTimeline(int frameCount)
    : CurveTimeline(frameCount), _transformConstraintIndex(0)
{
    _frames.setSize(frameCount * ENTRIES);
}

void TransformConstraintTimeline::setFrame(int frameIndex, float time, float rotateMix,
                                            float translateMix, float scaleMix, float shearMix) {
    int i = frameIndex * ENTRIES;
    _frames[i]     = time;
    _frames[i + 1] = rotateMix;
    _frames[i + 2] = translateMix;
    _frames[i + 3] = scaleMix;
    _frames[i + 4] = shearMix;
}

void TransformConstraintTimeline::apply(Skeleton &skeleton, float lastTime, float time,
                                         Vector<Event *> *events, float alpha,
                                         MixBlend blend, MixDirection direction) {
    (void)lastTime; (void)events; (void)direction;

    Vector<TransformConstraint *> &constraints = skeleton.getTransformConstraints();
    if (_transformConstraintIndex >= (int)constraints.size()) return;
    TransformConstraint *constraint = constraints[_transformConstraintIndex];

    int fc = (int)_frames.size();
    if (fc == 0) return;

    if (time < _frames[0]) {
        TransformConstraintData &setup = constraint->_data;
        switch (blend) {
        case MixBlend_Setup:
            constraint->_rotateMix = setup._rotateMix;
            constraint->_translateMix = setup._translateMix;
            constraint->_scaleMix = setup._scaleMix;
            constraint->_shearMix = setup._shearMix;
            return;
        case MixBlend_First:
            constraint->_rotateMix += (setup._rotateMix - constraint->_rotateMix) * alpha;
            constraint->_translateMix += (setup._translateMix - constraint->_translateMix) * alpha;
            constraint->_scaleMix += (setup._scaleMix - constraint->_scaleMix) * alpha;
            constraint->_shearMix += (setup._shearMix - constraint->_shearMix) * alpha;
            return;
        default:
            return;
        }
    }

    float rotate, translate, scale, shear;
    if (time >= _frames[fc - ENTRIES]) {
        int i = fc - ENTRIES;
        rotate    = _frames[i + 1];
        translate = _frames[i + 2];
        scale     = _frames[i + 3];
        shear     = _frames[i + 4];
    } else {
        int frame = 0;
        for (int i = ENTRIES; i < fc; i += ENTRIES) {
            if (_frames[i] > time) { frame = i - ENTRIES; break; }
        }

        float frameTime = _frames[frame];
        float percent = getCurvePercent(frame / ENTRIES,
                                         1 - (time - frameTime) / (_frames[frame + ENTRIES] - frameTime));

        rotate    = _frames[frame + 1];
        translate = _frames[frame + 2];
        scale     = _frames[frame + 3];
        shear     = _frames[frame + 4];

        rotate    += (_frames[frame + ENTRIES + 1] - rotate) * percent;
        translate += (_frames[frame + ENTRIES + 2] - translate) * percent;
        scale     += (_frames[frame + ENTRIES + 3] - scale) * percent;
        shear     += (_frames[frame + ENTRIES + 4] - shear) * percent;
    }

    if (blend == MixBlend_Setup) {
        TransformConstraintData &setup = constraint->_data;
        constraint->_rotateMix = setup._rotateMix + (rotate - setup._rotateMix) * alpha;
        constraint->_translateMix = setup._translateMix + (translate - setup._translateMix) * alpha;
        constraint->_scaleMix = setup._scaleMix + (scale - setup._scaleMix) * alpha;
        constraint->_shearMix = setup._shearMix + (shear - setup._shearMix) * alpha;
    } else {
        constraint->_rotateMix += (rotate - constraint->_rotateMix) * alpha;
        constraint->_translateMix += (translate - constraint->_translateMix) * alpha;
        constraint->_scaleMix += (scale - constraint->_scaleMix) * alpha;
        constraint->_shearMix += (shear - constraint->_shearMix) * alpha;
    }
}

int TransformConstraintTimeline::getPropertyId() {
    return (8 << 24) + _transformConstraintIndex;
}

} // namespace spine
