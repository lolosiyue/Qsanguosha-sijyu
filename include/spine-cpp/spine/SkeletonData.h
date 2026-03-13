#ifndef SPINE_SKELETON_DATA_H
#define SPINE_SKELETON_DATA_H

#include <spine/SpineString.h>
#include <spine/Vector.h>
#include <spine/BoneData.h>
#include <spine/SlotData.h>
#include <spine/Color.h>
#include <spine/IkConstraintData.h>
#include <spine/TransformConstraintData.h>

namespace spine {

class Animation;
class Skin;
class EventData;

class SkeletonData {
public:
    SkeletonData();
    ~SkeletonData();

    const String &getName() const { return _name; }
    void setName(const String &name) { _name = name; }

    Vector<BoneData *> &getBones() { return _bones; }
    Vector<SlotData *> &getSlots() { return _slots; }
    Vector<Animation *> &getAnimations() { return _animations; }
    Vector<IkConstraintData *> &getIkConstraints() { return _ikConstraints; }
    Vector<TransformConstraintData *> &getTransformConstraints() { return _transformConstraints; }

    BoneData *findBone(const String &name);
    SlotData *findSlot(const String &name);
    Animation *findAnimation(const String &name);
    IkConstraintData *findIkConstraint(const String &name);
    TransformConstraintData *findTransformConstraint(const String &name);

    Skin *getDefaultSkin() { return _defaultSkin; }
    void setDefaultSkin(Skin *skin) { _defaultSkin = skin; }

    float getWidth() const { return _width; }
    void setWidth(float w) { _width = w; }
    float getHeight() const { return _height; }
    void setHeight(float h) { _height = h; }

    float getX() const { return _x; }
    void setX(float x) { _x = x; }
    float getY() const { return _y; }
    void setY(float y) { _y = y; }

    String _version;
    String _hash;
    float _fps;
    String _imagesPath;

private:
    String _name;
    Vector<BoneData *> _bones;
    Vector<SlotData *> _slots;
    Vector<Animation *> _animations;
    Vector<Skin *> _skins;
    Skin *_defaultSkin;
    Vector<EventData *> _events;
    Vector<IkConstraintData *> _ikConstraints;
    Vector<TransformConstraintData *> _transformConstraints;

    float _x, _y, _width, _height;
};

} // namespace spine

#endif // SPINE_SKELETON_DATA_H
