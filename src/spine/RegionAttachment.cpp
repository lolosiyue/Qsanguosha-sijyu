/******************************************************************************
 * spine-cpp RegionAttachment implementation
 *****************************************************************************/

#include <spine/RegionAttachment.h>
#include <spine/Bone.h>
#include <cmath>

namespace spine {

RegionAttachment::RegionAttachment(const String &name)
    : Attachment(name, AttachmentType_Region),
      _x(0), _y(0), _rotation(0), _scaleX(1), _scaleY(1), _width(0), _height(0),
      _region(nullptr)
{
    std::memset(_uvs, 0, sizeof(_uvs));
    std::memset(_offset, 0, sizeof(_offset));
}

void RegionAttachment::updateOffset() {
    float regionScaleX = _width / _region->originalWidth * _scaleX;
    float regionScaleY = _height / _region->originalHeight * _scaleY;
    float localX = -_width / 2.0f * _scaleX + _region->offsetX * regionScaleX;
    float localY = -_height / 2.0f * _scaleY + _region->offsetY * regionScaleY;
    float localX2 = localX + _region->width * regionScaleX;
    float localY2 = localY + _region->height * regionScaleY;

    float rad = _rotation * 3.14159265358979323846f / 180.0f;
    float cosR = std::cos(rad);
    float sinR = std::sin(rad);
    float localXCos = localX * cosR + _x;
    float localXSin = localX * sinR;
    float localYCos = localY * cosR + _y;
    float localYSin = localY * sinR;
    float localX2Cos = localX2 * cosR + _x;
    float localX2Sin = localX2 * sinR;
    float localY2Cos = localY2 * cosR + _y;
    float localY2Sin = localY2 * sinR;

    // 4 vertices: BL, UL, UR, BR
    _offset[0] = localXCos - localYSin;   // BL x
    _offset[1] = localYCos + localXSin;   // BL y
    _offset[2] = localXCos - localY2Sin;  // UL x
    _offset[3] = localY2Cos + localXSin;  // UL y
    _offset[4] = localX2Cos - localY2Sin; // UR x
    _offset[5] = localY2Cos + localX2Sin; // UR y
    _offset[6] = localX2Cos - localYSin;  // BR x
    _offset[7] = localYCos + localX2Sin;  // BR y

    // UVs
    if (_region) {
        if (_region->rotate) {
            _uvs[0] = _region->u;   _uvs[1] = _region->v2;
            _uvs[2] = _region->u;   _uvs[3] = _region->v;
            _uvs[4] = _region->u2;  _uvs[5] = _region->v;
            _uvs[6] = _region->u2;  _uvs[7] = _region->v2;
        } else {
            _uvs[0] = _region->u;   _uvs[1] = _region->v2;
            _uvs[2] = _region->u;   _uvs[3] = _region->v;
            _uvs[4] = _region->u2;  _uvs[5] = _region->v;
            _uvs[6] = _region->u2;  _uvs[7] = _region->v2;
        }
    }
}

void RegionAttachment::computeWorldVertices(Bone &bone, float *worldVertices, int offset, int stride) {
    float a = bone.getA(), b = bone.getB(), x = bone.getWorldX();
    float c = bone.getC(), d = bone.getD(), y = bone.getWorldY();

    for (int i = 0; i < 4; ++i) {
        float ox = _offset[i * 2];
        float oy = _offset[i * 2 + 1];
        worldVertices[offset]     = ox * a + oy * b + x;
        worldVertices[offset + 1] = ox * c + oy * d + y;
        offset += stride;
    }
}

} // namespace spine
