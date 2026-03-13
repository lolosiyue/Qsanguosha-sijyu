#ifndef SPINE_SKELETON_CLIPPING_H
#define SPINE_SKELETON_CLIPPING_H

#include <spine/Vector.h>

namespace spine {

class Slot;

/// Utility to clip mesh vertices against clipping attachments.
class SkeletonClipping {
public:
    SkeletonClipping();

    int clipStart(Slot &slot, void *clip);
    void clipEnd(Slot &slot);
    void clipEnd();

    bool isClipping() const { return _clipAttachment != nullptr; }

    void clipTriangles(float *vertices, int verticesLength,
                       unsigned short *triangles, int trianglesLength,
                       float *uvs, int stride);

    Vector<float> &getClippedVertices() { return _clippedVertices; }
    Vector<unsigned short> &getClippedTriangles() { return _clippedTriangles; }
    Vector<float> &getClippedUVs() { return _clippedUVs; }

private:
    void *_clipAttachment;
    Vector<float> _clippedVertices;
    Vector<unsigned short> _clippedTriangles;
    Vector<float> _clippedUVs;
    Vector<float> _scratch;
};

} // namespace spine

#endif // SPINE_SKELETON_CLIPPING_H
