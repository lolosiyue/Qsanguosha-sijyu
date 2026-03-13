#ifndef SPINE_SKELETON_H
#define SPINE_SKELETON_H

#include <spine/SkeletonData.h>
#include <spine/Bone.h>
#include <spine/Slot.h>
#include <spine/Skin.h>
#include <spine/Color.h>
#include <spine/IkConstraint.h>
#include <spine/TransformConstraint.h>

namespace spine {

class Skeleton {
public:
    Skeleton(SkeletonData *data);
    ~Skeleton();

    /// Advance poses to setup pose.
    void setToSetupPose();
    void setBonesToSetupPose();
    void setSlotsToSetupPose();

    /// Update world transforms for all bones.
    void updateWorldTransform();

    /// Find bone / slot by name.
    Bone *findBone(const String &name);
    Slot *findSlot(const String &name);

    /// Find constraints by index
    IkConstraint *findIkConstraint(const String &name);
    TransformConstraint *findTransformConstraint(const String &name);

    SkeletonData *getData() { return _data; }

    Vector<Bone *> &getBones() { return _bones; }
    Vector<Slot *> &getSlots() { return _slots; }
    Vector<Slot *> &getDrawOrder() { return _drawOrder; }
    Vector<IkConstraint *> &getIkConstraints() { return _ikConstraints; }
    Vector<TransformConstraint *> &getTransformConstraints() { return _transformConstraints; }

    Skin *getSkin() { return _skin; }
    void setSkin(const String &skinName);
    void setSkin(Skin *newSkin);

    Attachment *getAttachment(const String &slotName, const String &attachmentName);
    Attachment *getAttachment(int slotIndex, const String &attachmentName);

    Color &getColor() { return _color; }

    float getX() const { return _x; }
    void setX(float x) { _x = x; }
    float getY() const { return _y; }
    void setY(float y) { _y = y; }

    float getScaleX() const { return _scaleX; }
    void setScaleX(float s) { _scaleX = s; }
    float getScaleY() const { return _scaleY; }
    void setScaleY(float s) { _scaleY = s; }

    float getTime() const { return _time; }
    void setTime(float t) { _time = t; }

    void update(float deltaTime);

private:
    SkeletonData *_data;
    Vector<Bone *> _bones;
    Vector<Slot *> _slots;
    Vector<Slot *> _drawOrder;
    Vector<IkConstraint *> _ikConstraints;
    Vector<TransformConstraint *> _transformConstraints;
    Skin *_skin;
    Color _color;
    float _x, _y, _scaleX, _scaleY;
    float _time;

    /// Update cache: interleaved bone updates and constraint updates
    /// Entry type: 0=bone, 1=ik, 2=transform. Index into respective vector.
    struct UpdateEntry {
        int type; // 0=bone, 1=ik, 2=transform
        int index;
    };
    Vector<UpdateEntry> _updateCache;
    Vector<Bone *> _updateCacheReset;
    void buildUpdateCache();
    void sortBone(Bone *bone);
    void sortIkConstraint(IkConstraint *constraint);
    void sortTransformConstraint(TransformConstraint *constraint);
    void sortReset(Vector<Bone *> &children);
};

} // namespace spine

#endif // SPINE_SKELETON_H
