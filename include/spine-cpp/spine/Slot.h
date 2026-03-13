#ifndef SPINE_SLOT_H
#define SPINE_SLOT_H

#include <spine/SlotData.h>
#include <spine/Bone.h>
#include <spine/Attachment.h>

namespace spine {

class Skeleton;

class Slot {
    friend class Skeleton;
public:
    Slot(SlotData &data, Bone &bone);

    SlotData &getData() { return _data; }
    Bone &getBone() { return _bone; }
    Skeleton &getSkeleton();
    Color &getColor() { return _color; }
    Color &getDarkColor() { return _darkColor; }
    bool hasDarkColor() const { return _data._hasDarkColor; }

    Attachment *getAttachment() { return _attachment; }
    void setAttachment(Attachment *attachment) { _attachment = attachment; }

    Vector<float> &getDeform() { return _deform; }

private:
    SlotData &_data;
    Bone &_bone;
    Color _color;
    Color _darkColor;
    Attachment *_attachment;
    Vector<float> _deform;
};

} // namespace spine

#endif // SPINE_SLOT_H
