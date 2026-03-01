/******************************************************************************
 * spine-cpp TransformConstraint implementation
 * Ported from spine-ts/spine.js TransformConstraint
 *****************************************************************************/

#include <spine/TransformConstraint.h>
#include <spine/Bone.h>
#include <spine/Skeleton.h>
#include <cmath>

namespace spine {

static const float PI_F = 3.14159265358979323846f;
static const float DEG_RAD_F = PI_F / 180.0f;
static const float RAD_DEG_F = 180.0f / PI_F;

TransformConstraint::TransformConstraint(TransformConstraintData &data, Skeleton &skeleton)
    : _data(data), _target(nullptr),
      _rotateMix(data._rotateMix), _translateMix(data._translateMix),
      _scaleMix(data._scaleMix), _shearMix(data._shearMix),
      _active(true), _skeleton(skeleton)
{
    for (size_t i = 0; i < data._bones.size(); ++i) {
        Bone *b = skeleton.findBone(data._bones[i]->getName());
        if (b) _bones.add(b);
    }
    if (data._target)
        _target = skeleton.findBone(data._target->getName());
}

void TransformConstraint::update() {
    if (!_target || _bones.size() == 0) return;
    if (_rotateMix == 0 && _translateMix == 0 && _scaleMix == 0 && _shearMix == 0) return;

    if (_data._local) {
        if (_data._relative)
            applyRelativeLocal();
        else
            applyAbsoluteLocal();
    } else {
        if (_data._relative)
            applyRelativeWorld();
        else
            applyAbsoluteWorld();
    }
}

void TransformConstraint::applyAbsoluteWorld() {
    float rotateMix = _rotateMix, translateMix = _translateMix;
    float scaleMix = _scaleMix, shearMix = _shearMix;
    Bone &target = *_target;

    float ta = target.getA(), tb = target.getB(), tc = target.getC(), td = target.getD();
    float degRadReflect = (ta * td - tb * tc > 0) ? DEG_RAD_F : -DEG_RAD_F;
    float offsetRotation = _data._offsetRotation * degRadReflect;
    float offsetShearY = _data._offsetShearY * degRadReflect;

    for (size_t i = 0; i < _bones.size(); ++i) {
        Bone &bone = *_bones[i];

        if (rotateMix != 0) {
            float a = bone.getA(), b = bone.getB(), c = bone.getC(), d = bone.getD();
            float r = std::atan2(tc, ta) - std::atan2(c, a) + offsetRotation;
            if (r > PI_F) r -= PI_F * 2;
            else if (r < -PI_F) r += PI_F * 2;
            r *= rotateMix;
            float cosR = std::cos(r), sinR = std::sin(r);
            bone._a = cosR * a - sinR * c;
            bone._b = cosR * b - sinR * d;
            bone._c = sinR * a + cosR * c;
            bone._d = sinR * b + cosR * d;
        }

        if (translateMix != 0) {
            float tempX = 0, tempY = 0;
            target.localToWorld(_data._offsetX, _data._offsetY, tempX, tempY);
            bone._worldX += (tempX - bone.getWorldX()) * translateMix;
            bone._worldY += (tempY - bone.getWorldY()) * translateMix;
        }

        if (scaleMix > 0) {
            float s = std::sqrt(bone.getA() * bone.getA() + bone.getC() * bone.getC());
            float ts = std::sqrt(ta * ta + tc * tc);
            if (s > 0.00001f) s = (s + (ts - s + _data._offsetScaleX) * scaleMix) / s;
            bone._a *= s;
            bone._c *= s;

            s = std::sqrt(bone.getB() * bone.getB() + bone.getD() * bone.getD());
            ts = std::sqrt(tb * tb + td * td);
            if (s > 0.00001f) s = (s + (ts - s + _data._offsetScaleY) * scaleMix) / s;
            bone._b *= s;
            bone._d *= s;
        }

        if (shearMix > 0) {
            float b2 = bone.getB(), d2 = bone.getD();
            float by = std::atan2(d2, b2);
            float r = std::atan2(td, tb) - std::atan2(tc, ta)
                      - (by - std::atan2(bone.getC(), bone.getA()));
            if (r > PI_F) r -= PI_F * 2;
            else if (r < -PI_F) r += PI_F * 2;
            r = by + (r + offsetShearY) * shearMix;
            float s = std::sqrt(b2 * b2 + d2 * d2);
            bone._b = std::cos(r) * s;
            bone._d = std::sin(r) * s;
        }

        // Mark modified - but in our simple impl, this is sufficient
    }
}

void TransformConstraint::applyRelativeWorld() {
    float rotateMix = _rotateMix, translateMix = _translateMix;
    float scaleMix = _scaleMix, shearMix = _shearMix;
    Bone &target = *_target;

    float ta = target.getA(), tb = target.getB(), tc = target.getC(), td = target.getD();
    float degRadReflect = (ta * td - tb * tc > 0) ? DEG_RAD_F : -DEG_RAD_F;
    float offsetRotation = _data._offsetRotation * degRadReflect;
    float offsetShearY = _data._offsetShearY * degRadReflect;

    for (size_t i = 0; i < _bones.size(); ++i) {
        Bone &bone = *_bones[i];

        if (rotateMix != 0) {
            float a = bone.getA(), b = bone.getB(), c = bone.getC(), d = bone.getD();
            float r = std::atan2(tc, ta) + offsetRotation;
            if (r > PI_F) r -= PI_F * 2;
            else if (r < -PI_F) r += PI_F * 2;
            r *= rotateMix;
            float cosR = std::cos(r), sinR = std::sin(r);
            bone._a = cosR * a - sinR * c;
            bone._b = cosR * b - sinR * d;
            bone._c = sinR * a + cosR * c;
            bone._d = sinR * b + cosR * d;
        }

        if (translateMix != 0) {
            float tempX = 0, tempY = 0;
            target.localToWorld(_data._offsetX, _data._offsetY, tempX, tempY);
            bone._worldX += tempX * translateMix;
            bone._worldY += tempY * translateMix;
        }

        if (scaleMix > 0) {
            float s = (std::sqrt(ta * ta + tc * tc) - 1 + _data._offsetScaleX) * scaleMix + 1;
            bone._a *= s;
            bone._c *= s;
            s = (std::sqrt(tb * tb + td * td) - 1 + _data._offsetScaleY) * scaleMix + 1;
            bone._b *= s;
            bone._d *= s;
        }

        if (shearMix > 0) {
            float r = std::atan2(td, tb) - std::atan2(tc, ta);
            if (r > PI_F) r -= PI_F * 2;
            else if (r < -PI_F) r += PI_F * 2;
            float b2 = bone.getB(), d2 = bone.getD();
            r = std::atan2(d2, b2) + (r - PI_F / 2 + offsetShearY) * shearMix;
            float s = std::sqrt(b2 * b2 + d2 * d2);
            bone._b = std::cos(r) * s;
            bone._d = std::sin(r) * s;
        }
    }
}

void TransformConstraint::applyAbsoluteLocal() {
    float rotateMix = _rotateMix, translateMix = _translateMix;
    float scaleMix = _scaleMix, shearMix = _shearMix;
    Bone &target = *_target;

    for (size_t i = 0; i < _bones.size(); ++i) {
        Bone &bone = *_bones[i];

        float rotation = bone._arotation;
        if (rotateMix != 0) {
            float r = target._arotation - rotation + _data._offsetRotation;
            r -= (16384 - (int)(16384.499999999996 - r / 360)) * 360;
            rotation += r * rotateMix;
        }

        float x = bone._ax, y = bone._ay;
        if (translateMix != 0) {
            x += (target._ax - x + _data._offsetX) * translateMix;
            y += (target._ay - y + _data._offsetY) * translateMix;
        }

        float scaleX = bone._ascaleX, scaleY = bone._ascaleY;
        if (scaleMix > 0) {
            if (scaleX > 0.00001f)
                scaleX = (scaleX + (target._ascaleX - scaleX + _data._offsetScaleX) * scaleMix) / scaleX;
            if (scaleY > 0.00001f)
                scaleY = (scaleY + (target._ascaleY - scaleY + _data._offsetScaleY) * scaleMix) / scaleY;
        }

        float shearY = bone._ashearY;
        if (shearMix > 0) {
            float r = target._ashearY - shearY + _data._offsetShearY;
            r -= (16384 - (int)(16384.499999999996 - r / 360)) * 360;
            shearY += r * shearMix;
        }

        bone.updateWorldTransformWith(x, y, rotation, scaleX, scaleY, bone._ashearX, shearY);
    }
}

void TransformConstraint::applyRelativeLocal() {
    float rotateMix = _rotateMix, translateMix = _translateMix;
    float scaleMix = _scaleMix, shearMix = _shearMix;
    Bone &target = *_target;

    for (size_t i = 0; i < _bones.size(); ++i) {
        Bone &bone = *_bones[i];

        float rotation = bone._arotation;
        if (rotateMix != 0)
            rotation += (target._arotation + _data._offsetRotation) * rotateMix;

        float x = bone._ax, y = bone._ay;
        if (translateMix != 0) {
            x += (target._ax + _data._offsetX) * translateMix;
            y += (target._ay + _data._offsetY) * translateMix;
        }

        float scaleX = bone._ascaleX, scaleY = bone._ascaleY;
        if (scaleMix > 0) {
            scaleX *= ((target._ascaleX - 1 + _data._offsetScaleX) * scaleMix) + 1;
            scaleY *= ((target._ascaleY - 1 + _data._offsetScaleY) * scaleMix) + 1;
        }

        float shearY = bone._ashearY;
        if (shearMix > 0)
            shearY += (target._ashearY + _data._offsetShearY) * shearMix;

        bone.updateWorldTransformWith(x, y, rotation, scaleX, scaleY, bone._ashearX, shearY);
    }
}

} // namespace spine
