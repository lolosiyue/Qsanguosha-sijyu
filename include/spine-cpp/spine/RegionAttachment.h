#ifndef SPINE_REGION_ATTACHMENT_H
#define SPINE_REGION_ATTACHMENT_H

#include <spine/Attachment.h>
#include <spine/Atlas.h>

namespace spine {

class Bone;

class RegionAttachment : public Attachment {
public:
    static const int NUM_VERTICES = 8; // 4 vertices * 2 (x,y)
    static const int NUM_UVS = 8;

    RegionAttachment(const String &name);

    void updateOffset();
    void computeWorldVertices(Bone &bone, float *worldVertices, int offset, int stride);

    float getX() const { return _x; }
    void setX(float x) { _x = x; }
    float getY() const { return _y; }
    void setY(float y) { _y = y; }
    float getRotation() const { return _rotation; }
    void setRotation(float r) { _rotation = r; }
    float getScaleX() const { return _scaleX; }
    void setScaleX(float s) { _scaleX = s; }
    float getScaleY() const { return _scaleY; }
    void setScaleY(float s) { _scaleY = s; }
    float getWidth() const { return _width; }
    void setWidth(float w) { _width = w; }
    float getHeight() const { return _height; }
    void setHeight(float h) { _height = h; }

    Color &getColor() { return _color; }

    AtlasRegion *getRegion() { return _region; }
    void setRegion(AtlasRegion *region) { _region = region; }

    float *getUVs() { return _uvs; }
    float *getOffset() { return _offset; }

private:
    float _x, _y, _rotation, _scaleX, _scaleY, _width, _height;
    Color _color;
    AtlasRegion *_region;
    float _uvs[8];
    float _offset[8];
};

} // namespace spine

#endif // SPINE_REGION_ATTACHMENT_H
