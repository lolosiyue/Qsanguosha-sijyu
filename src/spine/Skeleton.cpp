/******************************************************************************
 * spine-cpp Skeleton implementation
 *****************************************************************************/

#include <spine/Skeleton.h>
#include <spine/Animation.h>
#include <algorithm>

namespace spine {

Skeleton::Skeleton(SkeletonData *data)
    : _data(data), _skin(nullptr), _x(0), _y(0), _scaleX(1), _scaleY(1), _time(0)
{
    // Create bones & build children lists
    Vector<BoneData *> &bd = data->getBones();
    _bones.setSize(bd.size());
    for (size_t i = 0; i < bd.size(); ++i) {
        Bone *parent = nullptr;
        if (bd[i]->getParent()) {
            // Find parent bone in our list
            for (size_t j = 0; j < i; ++j) {
                if (&_bones[j]->getData() == bd[i]->getParent()) {
                    parent = _bones[j];
                    break;
                }
            }
        }
        _bones[i] = new Bone(*bd[i], *this, parent);
        if (parent)
            parent->_children.add(_bones[i]);
    }

    // Create slots
    Vector<SlotData *> &sd = data->getSlots();
    _slots.setSize(sd.size());
    _drawOrder.setSize(sd.size());
    for (size_t i = 0; i < sd.size(); ++i) {
        Bone *bone = nullptr;
        for (size_t j = 0; j < _bones.size(); ++j) {
            if (&_bones[j]->getData() == &sd[i]->getBoneData()) {
                bone = _bones[j];
                break;
            }
        }
        _slots[i] = new Slot(*sd[i], *bone);
        _drawOrder[i] = _slots[i];
    }

    // Create IK constraints
    Vector<IkConstraintData *> &ikData = data->getIkConstraints();
    for (size_t i = 0; i < ikData.size(); ++i) {
        _ikConstraints.add(new IkConstraint(*ikData[i], *this));
    }

    // Create Transform constraints
    Vector<TransformConstraintData *> &xfData = data->getTransformConstraints();
    for (size_t i = 0; i < xfData.size(); ++i) {
        _transformConstraints.add(new TransformConstraint(*xfData[i], *this));
    }

    // Build update cache
    buildUpdateCache();

    // Apply default skin
    if (data->getDefaultSkin())
        setSkin(data->getDefaultSkin());
}

Skeleton::~Skeleton() {
    for (size_t i = 0; i < _bones.size(); ++i) delete _bones[i];
    for (size_t i = 0; i < _slots.size(); ++i) delete _slots[i];
    for (size_t i = 0; i < _ikConstraints.size(); ++i) delete _ikConstraints[i];
    for (size_t i = 0; i < _transformConstraints.size(); ++i) delete _transformConstraints[i];
}

void Skeleton::buildUpdateCache() {
    _updateCache.clear();
    _updateCacheReset.clear();

    // Reset all bones' sorted flag
    for (size_t i = 0; i < _bones.size(); ++i)
        _bones[i]->_sorted = false;

    // Collect all constraints with their orders
    struct ConstraintRef {
        int order;
        int type; // 1=ik, 2=transform
        int index;
    };

    Vector<ConstraintRef> constraints;
    for (size_t i = 0; i < _ikConstraints.size(); ++i) {
        ConstraintRef r;
        r.order = _ikConstraints[i]->getOrder();
        r.type = 1;
        r.index = (int)i;
        constraints.add(r);
    }
    for (size_t i = 0; i < _transformConstraints.size(); ++i) {
        ConstraintRef r;
        r.order = _transformConstraints[i]->getOrder();
        r.type = 2;
        r.index = (int)i;
        constraints.add(r);
    }

    // Sort constraints by order (insertion sort, stable)
    for (size_t i = 1; i < constraints.size(); ++i) {
        ConstraintRef key = constraints[i];
        int j = (int)i - 1;
        while (j >= 0 && constraints[j].order > key.order) {
            constraints[j + 1] = constraints[j];
            --j;
        }
        constraints[j + 1] = key;
    }

    // Process each constraint in order, interleaving bones
    for (size_t i = 0; i < constraints.size(); ++i) {
        switch (constraints[i].type) {
        case 1: sortIkConstraint(_ikConstraints[constraints[i].index]); break;
        case 2: sortTransformConstraint(_transformConstraints[constraints[i].index]); break;
        }
    }

    // Sort any remaining unsorted bones
    for (size_t i = 0; i < _bones.size(); ++i)
        sortBone(_bones[i]);
}

void Skeleton::sortBone(Bone *bone) {
    if (bone->_sorted) return;
    if (bone->_parent)
        sortBone(bone->_parent);
    bone->_sorted = true;
    UpdateEntry e;
    e.type = 0;
    e.index = bone->_data.getIndex();
    _updateCache.add(e);
}

void Skeleton::sortIkConstraint(IkConstraint *constraint) {
    // Sort target bone
    sortBone(constraint->_target);

    // Sort constrained bones' parents up to the target
    Vector<Bone *> &constrained = constraint->_bones;
    Bone *parent = constrained[0];
    sortBone(parent);

    // Push the IK constraint into cache
    UpdateEntry ce;
    ce.type = 1;
    // Find the index of this constraint in _ikConstraints
    for (size_t i = 0; i < _ikConstraints.size(); ++i) {
        if (_ikConstraints[i] == constraint) {
            ce.index = (int)i;
            break;
        }
    }
    _updateCache.add(ce);

    // sortReset the parent's children so they get re-sorted after this constraint
    sortReset(parent->_children);

    // Mark the last constrained bone as sorted (it's been handled by the constraint)
    constrained[constrained.size() - 1]->_sorted = true;
}

void Skeleton::sortTransformConstraint(TransformConstraint *constraint) {
    // Sort the target bone first
    sortBone(constraint->_target);

    Vector<Bone *> &constrained = constraint->_bones;
    if (constraint->_data._local) {
        // Local mode: sort each constrained bone's parent, and add constrained bone to updateCacheReset
        for (size_t i = 0; i < constrained.size(); ++i) {
            Bone *child = constrained[i];
            sortBone(child->_parent);
            // Check if already in cache — if not, add to updateCacheReset
            bool inCache = false;
            for (size_t j = 0; j < _updateCache.size(); ++j) {
                if (_updateCache[j].type == 0 && _updateCache[j].index == child->_data.getIndex()) {
                    inCache = true;
                    break;
                }
            }
            if (!inCache) _updateCacheReset.add(child);
        }
    } else {
        // Non-local mode: sort each constrained bone
        for (size_t i = 0; i < constrained.size(); ++i) {
            sortBone(constrained[i]);
        }
    }

    // Push the transform constraint into cache
    UpdateEntry ce;
    ce.type = 2;
    for (size_t i = 0; i < _transformConstraints.size(); ++i) {
        if (_transformConstraints[i] == constraint) {
            ce.index = (int)i;
            break;
        }
    }
    _updateCache.add(ce);

    // sortReset children of all constrained bones
    for (size_t i = 0; i < constrained.size(); ++i) {
        sortReset(constrained[i]->_children);
    }

    // Mark all constrained bones as sorted
    for (size_t i = 0; i < constrained.size(); ++i) {
        constrained[i]->_sorted = true;
    }
}

void Skeleton::sortReset(Vector<Bone *> &children) {
    for (size_t i = 0; i < children.size(); ++i) {
        Bone *child = children[i];
        if (child->_sorted) sortReset(child->_children);
        child->_sorted = false;
    }
}

void Skeleton::setToSetupPose() {
    setBonesToSetupPose();
    setSlotsToSetupPose();
}

void Skeleton::setBonesToSetupPose() {
    for (size_t i = 0; i < _bones.size(); ++i) {
        Bone *bone = _bones[i];
        bone->_x = bone->_data._x;
        bone->_y = bone->_data._y;
        bone->_rotation = bone->_data._rotation;
        bone->_scaleX = bone->_data._scaleX;
        bone->_scaleY = bone->_data._scaleY;
        bone->_shearX = bone->_data._shearX;
        bone->_shearY = bone->_data._shearY;
    }
}

void Skeleton::setSlotsToSetupPose() {
    for (size_t i = 0; i < _slots.size(); ++i) {
        Slot *slot = _slots[i];
        _drawOrder[i] = slot;
        slot->getColor().set(slot->getData()._color);
        if (!slot->getData()._attachmentName.isEmpty()) {
            slot->setAttachment(getAttachment(static_cast<int>(i), slot->getData()._attachmentName));
        } else {
            slot->setAttachment(nullptr);
        }
    }
}

void Skeleton::updateWorldTransform() {
    // Reset applied values for bones in updateCacheReset
    // (these are bones whose local values need to be copied to applied values
    //  before the cache runs, used by local transform constraints)
    for (size_t i = 0; i < _updateCacheReset.size(); ++i) {
        Bone *b = _updateCacheReset[i];
        b->_ax = b->_x;
        b->_ay = b->_y;
        b->_arotation = b->_rotation;
        b->_ascaleX = b->_scaleX;
        b->_ascaleY = b->_scaleY;
        b->_ashearX = b->_shearX;
        b->_ashearY = b->_shearY;
    }
    // Run the interleaved update cache
    for (size_t i = 0; i < _updateCache.size(); ++i) {
        const UpdateEntry &e = _updateCache[i];
        switch (e.type) {
        case 0: _bones[e.index]->updateWorldTransform(); break;
        case 1: _ikConstraints[e.index]->update(); break;
        case 2: _transformConstraints[e.index]->update(); break;
        }
    }
}

Bone *Skeleton::findBone(const String &name) {
    for (size_t i = 0; i < _bones.size(); ++i)
        if (_bones[i]->getData().getName() == name) return _bones[i];
    return nullptr;
}

Slot *Skeleton::findSlot(const String &name) {
    for (size_t i = 0; i < _slots.size(); ++i)
        if (_slots[i]->getData().getName() == name) return _slots[i];
    return nullptr;
}

IkConstraint *Skeleton::findIkConstraint(const String &name) {
    for (size_t i = 0; i < _ikConstraints.size(); ++i)
        if (_ikConstraints[i]->getData()._name == name) return _ikConstraints[i];
    return nullptr;
}

TransformConstraint *Skeleton::findTransformConstraint(const String &name) {
    for (size_t i = 0; i < _transformConstraints.size(); ++i)
        if (_transformConstraints[i]->getData()._name == name) return _transformConstraints[i];
    return nullptr;
}

void Skeleton::setSkin(const String &skinName) {
    Skin *skin = _data->getDefaultSkin();
    // Could search skins vector by name if needed
    setSkin(skin);
}

void Skeleton::setSkin(Skin *newSkin) {
    if (newSkin) {
        if (_skin)
            newSkin->attachAll(*this, *_skin);
        else {
            // Apply default attachments from the new skin
            for (size_t i = 0; i < _slots.size(); ++i) {
                Slot *slot = _slots[i];
                const String &name = slot->getData()._attachmentName;
                if (!name.isEmpty()) {
                    Attachment *attachment = newSkin->getAttachment(static_cast<int>(i), name);
                    if (attachment)
                        slot->setAttachment(attachment);
                }
            }
        }
    }
    _skin = newSkin;
}

Attachment *Skeleton::getAttachment(const String &slotName, const String &attachmentName) {
    for (size_t i = 0; i < _slots.size(); ++i) {
        if (_slots[i]->getData().getName() == slotName)
            return getAttachment(static_cast<int>(i), attachmentName);
    }
    return nullptr;
}

Attachment *Skeleton::getAttachment(int slotIndex, const String &attachmentName) {
    if (attachmentName.isEmpty()) return nullptr;
    if (_skin) {
        Attachment *attachment = _skin->getAttachment(slotIndex, attachmentName);
        if (attachment) return attachment;
    }
    if (_data->getDefaultSkin() && _data->getDefaultSkin() != _skin) {
        Attachment *attachment = _data->getDefaultSkin()->getAttachment(slotIndex, attachmentName);
        if (attachment) return attachment;
    }
    return nullptr;
}

void Skeleton::update(float deltaTime) {
    _time += deltaTime;
}

} // namespace spine
