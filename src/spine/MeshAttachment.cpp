/******************************************************************************
 * spine-cpp MeshAttachment implementation
 *****************************************************************************/

#include <spine/MeshAttachment.h>
#include <spine/Slot.h>
#include <spine/Bone.h>
#include <spine/Skeleton.h>

namespace spine {

MeshAttachment::MeshAttachment(const String &name)
    : Attachment(name, AttachmentType_Mesh),
    _region(nullptr), _worldVerticesLength(0), _hullLength(0), _weighted(false)
{
}

void MeshAttachment::updateUVs() {
    if (!_region) return;
    float u = _region->u, v = _region->v;
    float width = _region->u2 - u;
    float height = _region->v2 - v;

    int n = (int)_regionUVs.size();
    _uvs.setSize(n);
    if (_region->rotate) {
        for (int i = 0; i < n; i += 2) {
            _uvs[i] = u + _regionUVs[i + 1] * width;
            _uvs[i + 1] = v + height - _regionUVs[i] * height;
        }
    } else {
        for (int i = 0; i < n; i += 2) {
            _uvs[i] = u + _regionUVs[i] * width;
            _uvs[i + 1] = v + _regionUVs[i + 1] * height;
        }
    }
}

void MeshAttachment::computeWorldVertices(Slot &slot, int start, int count,
                                           float *worldVertices, int offset, int stride) {
    count = offset + (count >> 1) * stride;

    Vector<float> &deform = slot.getDeform();
    Vector<float> &vertices = _vertices;

    if (!_weighted) {
        Bone &bone = slot.getBone();

        float a = bone.getA(), b = bone.getB(), wx = bone.getWorldX();
        float c = bone.getC(), d = bone.getD(), wy = bone.getWorldY();

        int v = start;
        bool hasDeform = deform.size() > 0;
        for (int w = offset; w < count; w += stride) {
            float vx = vertices[v];
            float vy = vertices[v + 1];
            if (hasDeform && v < (int)deform.size() - 1) {
                vx += deform[v];
                vy += deform[v + 1];
            }
            worldVertices[w] = vx * a + vy * b + wx;
            worldVertices[w + 1] = vx * c + vy * d + wy;
            v += 2;
        }
        return;
    }

    Vector<int> &bones = _bones;
    Vector<Bone *> &skeletonBones = slot.getSkeleton().getBones();

    int w = offset;
    int v = 0;
    int b = 0;
    int f = 0;

    int skip = start >> 1;
    for (int i = 0; i < skip; ++i) {
        int n = bones[v];
        v += n + 1;
        b += n * 3;
    }

    bool hasDeform = deform.size() > 0;
    for (; w < count; w += stride) {
        float wx = 0.0f, wy = 0.0f;
        int n = bones[v++];
        int end = v + n;
        for (; v < end; ++v, b += 3) {
            int boneIndex = bones[v];
            if (boneIndex < 0 || boneIndex >= (int)skeletonBones.size())
                continue;
            Bone *bone = skeletonBones[boneIndex];
            float vx = vertices[b];
            float vy = vertices[b + 1];
            float weight = vertices[b + 2];
            if (hasDeform && f + 1 < (int)deform.size()) {
                vx += deform[f];
                vy += deform[f + 1];
            }
            f += 2;
            wx += (vx * bone->getA() + vy * bone->getB() + bone->getWorldX()) * weight;
            wy += (vx * bone->getC() + vy * bone->getD() + bone->getWorldY()) * weight;
        }
        worldVertices[w] = wx;
        worldVertices[w + 1] = wy;
    }
}

} // namespace spine
