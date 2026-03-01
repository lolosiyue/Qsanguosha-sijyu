/******************************************************************************
 * spine-cpp Skin implementation
 *****************************************************************************/

#include <spine/Skin.h>
#include <spine/Skeleton.h>

namespace spine {

Skin::~Skin() {
    // Note: Skin owns its attachments in the official runtime;
    // here we keep it simple – SkeletonData owns them.
}

void Skin::setAttachment(int slotIndex, const String &name, Attachment *attachment) {
    AttachmentKey key;
    key.slotIndex = slotIndex;
    key.name = name;
    _attachments[key] = attachment;
}

Attachment *Skin::getAttachment(int slotIndex, const String &name) {
    AttachmentKey key;
    key.slotIndex = slotIndex;
    key.name = name;
    auto it = _attachments.find(key);
    return it != _attachments.end() ? it->second : nullptr;
}

void Skin::attachAll(Skeleton &skeleton, Skin &oldSkin) {
    for (auto &pair : oldSkin._attachments) {
        int slotIndex = pair.first.slotIndex;
        Slot *slot = skeleton.getSlots()[slotIndex];
        if (slot->getAttachment() == pair.second) {
            Attachment *attachment = getAttachment(slotIndex, pair.first.name);
            if (attachment)
                slot->setAttachment(attachment);
        }
    }
}

} // namespace spine
