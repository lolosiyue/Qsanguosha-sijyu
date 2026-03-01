/******************************************************************************
 * spine-cpp Bone implementation
 *****************************************************************************/

#include <spine/Bone.h>
#include <spine/Skeleton.h>
#include <cmath>

namespace spine {

static const float DEG_RAD = 3.14159265358979323846f / 180.0f;
static const float RAD_DEG = 180.0f / 3.14159265358979323846f;

Bone::Bone(BoneData &data, Skeleton &skeleton, Bone *parent)
    : _data(data), _skeleton(skeleton), _parent(parent),
      _x(0), _y(0), _rotation(0), _scaleX(1), _scaleY(1), _shearX(0), _shearY(0),
      _ax(0), _ay(0), _arotation(0), _ascaleX(1), _ascaleY(1), _ashearX(0), _ashearY(0),
      _a(1), _b(0), _worldX(0),
      _c(0), _d(1), _worldY(0),
      _sorted(false), _active(true)
{
    setToSetupPose();
}

void Bone::update() {
    updateWorldTransform();
}

void Bone::setToSetupPose() {
    _x = _data._x;
    _y = _data._y;
    _rotation = _data._rotation;
    _scaleX = _data._scaleX;
    _scaleY = _data._scaleY;
    _shearX = _data._shearX;
    _shearY = _data._shearY;
}

void Bone::updateWorldTransform() {
    updateWorldTransformWith(_x, _y, _rotation, _scaleX, _scaleY, _shearX, _shearY);
}

void Bone::updateWorldTransformWith(float x, float y, float rotation,
                                     float scaleX, float scaleY,
                                     float shearX, float shearY) {
    _ax = x; _ay = y;
    _arotation = rotation;
    _ascaleX = scaleX; _ascaleY = scaleY;
    _ashearX = shearX; _ashearY = shearY;

    Bone *parent = _parent;
    if (!parent) {
        // Root bone
        float rotationY = rotation + 90 + shearY;
        float la = std::cos((rotation + shearX) * DEG_RAD) * scaleX;
        float lb = std::cos(rotationY * DEG_RAD) * scaleY;
        float lc = std::sin((rotation + shearX) * DEG_RAD) * scaleX;
        float ld = std::sin(rotationY * DEG_RAD) * scaleY;
        float skX = _skeleton.getScaleX();
        float skY = _skeleton.getScaleY();
        _a = la * skX;  _b = lb * skX;
        _c = lc * skY;  _d = ld * skY;
        _worldX = x * skX + _skeleton.getX();
        _worldY = y * skY + _skeleton.getY();
        return;
    }

    float pa = parent->_a, pb = parent->_b, pc = parent->_c, pd = parent->_d;
    _worldX = pa * x + pb * y + parent->_worldX;
    _worldY = pc * x + pd * y + parent->_worldY;

    switch (_data._transformMode) {
    case TransformMode_Normal: {
        float rotationY = rotation + 90 + shearY;
        float la = std::cos((rotation + shearX) * DEG_RAD) * scaleX;
        float lb = std::cos(rotationY * DEG_RAD) * scaleY;
        float lc = std::sin((rotation + shearX) * DEG_RAD) * scaleX;
        float ld = std::sin(rotationY * DEG_RAD) * scaleY;
        _a = pa * la + pb * lc;
        _b = pa * lb + pb * ld;
        _c = pc * la + pd * lc;
        _d = pc * lb + pd * ld;
        return; // skip flip
    }

    case TransformMode_OnlyTranslation: {
        float rotationY = rotation + 90 + shearY;
        _a = std::cos((rotation + shearX) * DEG_RAD) * scaleX;
        _b = std::cos(rotationY * DEG_RAD) * scaleY;
        _c = std::sin((rotation + shearX) * DEG_RAD) * scaleX;
        _d = std::sin(rotationY * DEG_RAD) * scaleY;
        break; // apply flip
    }

    case TransformMode_NoRotationOrReflection: {
        float s = pa * pa + pc * pc;
        float prx = 0;
        if (s > 0.0001f) {
            s = std::abs(pa * pd - pb * pc) / s;
            pb = pc * s;
            pd = pa * s;
            prx = std::atan2(pc, pa) * RAD_DEG;
        } else {
            pa = 0; pc = 0;
            prx = 90 - std::atan2(pd, pb) * RAD_DEG;
        }
        float rx = rotation + shearX - prx;
        float ry = rotation + shearY - prx + 90;
        float la = std::cos(rx * DEG_RAD) * scaleX;
        float lb = std::cos(ry * DEG_RAD) * scaleY;
        float lc = std::sin(rx * DEG_RAD) * scaleX;
        float ld = std::sin(ry * DEG_RAD) * scaleY;
        _a = pa * la - pb * lc;
        _b = pa * lb - pb * ld;
        _c = pc * la + pd * lc;
        _d = pc * lb + pd * ld;
        break; // apply flip
    }

    case TransformMode_NoScale:
    case TransformMode_NoScaleOrReflection: {
        float cosV = std::cos(rotation * DEG_RAD);
        float sinV = std::sin(rotation * DEG_RAD);
        float za = pa * cosV + pb * sinV;
        float zc = pc * cosV + pd * sinV;
        float s = std::sqrt(za * za + zc * zc);
        if (s > 0.00001f) s = 1.0f / s;
        za *= s; zc *= s;
        s = std::sqrt(za * za + zc * zc);
        float r = 1.5707963267948966f + std::atan2(zc, za); // PI/2
        float zb = std::cos(r) * s;
        float zd = std::sin(r) * s;
        float la = std::cos(shearX * DEG_RAD) * scaleX;
        float lb = std::cos((90 + shearY) * DEG_RAD) * scaleY;
        float lc = std::sin(shearX * DEG_RAD) * scaleX;
        float ld = std::sin((90 + shearY) * DEG_RAD) * scaleY;
        bool flipNeg = (_data._transformMode != TransformMode_NoScaleOrReflection)
                           ? (pa * pd - pb * pc < 0)
                           : (_skeleton.getScaleX() < 0) != (_skeleton.getScaleY() < 0);
        if (flipNeg) { zb = -zb; zd = -zd; }
        _a = za * la + zb * lc;
        _b = za * lb + zb * ld;
        _c = zc * la + zd * lc;
        _d = zc * lb + zd * ld;
        return; // skip flip
    }
    }

    // Flip fixup (reached by OnlyTranslation and NoRotationOrReflection)
    if (_skeleton.getScaleX() < 0) { _a = -_a; _b = -_b; }
    if (_skeleton.getScaleY() < 0) { _c = -_c; _d = -_d; }
}

float Bone::worldToLocalRotation(float worldRotation) {
    float sine = std::sin(worldRotation * DEG_RAD);
    float cosine = std::cos(worldRotation * DEG_RAD);
    return std::atan2(_a * sine - _c * cosine, _d * cosine - _b * sine) * RAD_DEG + _rotation - _shearX;
}

float Bone::localToWorldRotation(float localRotation) {
    localRotation -= _rotation - _shearX;
    float sine = std::sin(localRotation * DEG_RAD);
    float cosine = std::cos(localRotation * DEG_RAD);
    return std::atan2(cosine * _c + sine * _d, cosine * _a + sine * _b) * RAD_DEG;
}

} // namespace spine
