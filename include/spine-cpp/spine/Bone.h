#ifndef SPINE_BONE_H
#define SPINE_BONE_H

#include <spine/BoneData.h>
#include <spine/Vector.h>
#include <cmath>

namespace spine {

class Skeleton;

class Bone {
    friend class Skeleton;
    friend class IkConstraint;
    friend class TransformConstraint;
public:
    Bone(BoneData &data, Skeleton &skeleton, Bone *parent);

    void update();
    void updateWorldTransform();
    void updateWorldTransformWith(float x, float y, float rotation,
                                   float scaleX, float scaleY,
                                   float shearX, float shearY);

    float getWorldX() const { return _worldX; }
    float getWorldY() const { return _worldY; }
    float getA() const { return _a; }
    float getB() const { return _b; }
    float getC() const { return _c; }
    float getD() const { return _d; }

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
    float getShearX() const { return _shearX; }
    void setShearX(float s) { _shearX = s; }
    float getShearY() const { return _shearY; }
    void setShearY(float s) { _shearY = s; }

    BoneData &getData() { return _data; }
    Bone *getParent() { return _parent; }
    Skeleton &getSkeleton() { return _skeleton; }
    Vector<Bone *> &getChildren() { return _children; }

    void setToSetupPose();

    float worldToLocalRotation(float worldRotation);
    float localToWorldRotation(float localRotation);

    void localToWorld(float localX, float localY, float &outX, float &outY) {
        outX = localX * _a + localY * _b + _worldX;
        outY = localX * _c + localY * _d + _worldY;
    }

private:
    BoneData &_data;
    Skeleton &_skeleton;
    Bone *_parent;
    Vector<Bone *> _children;

    float _x, _y, _rotation, _scaleX, _scaleY, _shearX, _shearY;
    float _ax, _ay, _arotation, _ascaleX, _ascaleY, _ashearX, _ashearY;
    float _a, _b, _worldX;
    float _c, _d, _worldY;

    bool _sorted;
    bool _active;
};

} // namespace spine

#endif // SPINE_BONE_H
