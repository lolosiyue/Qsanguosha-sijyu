/******************************************************************************
 * spine-cpp SkeletonClipping implementation
 *****************************************************************************/

#include <spine/SkeletonClipping.h>

namespace spine {

SkeletonClipping::SkeletonClipping() : _clipAttachment(nullptr) {}

int SkeletonClipping::clipStart(Slot &slot, void *clip) {
    _clipAttachment = clip;
    return 0;
}

void SkeletonClipping::clipEnd(Slot &slot) {
    (void)slot;
    _clipAttachment = nullptr;
}

void SkeletonClipping::clipEnd() {
    _clipAttachment = nullptr;
}

void SkeletonClipping::clipTriangles(float *vertices, int verticesLength,
                                      unsigned short *triangles, int trianglesLength,
                                      float *uvs, int stride) {
    // Simplified: just pass through without clipping
    _clippedVertices.clear();
    _clippedTriangles.clear();
    _clippedUVs.clear();

    for (int i = 0; i < verticesLength; ++i)
        _clippedVertices.add(vertices[i]);
    for (int i = 0; i < trianglesLength; ++i)
        _clippedTriangles.add(triangles[i]);
    for (int i = 0; i < verticesLength; ++i)
        _clippedUVs.add(uvs[i]);
}

} // namespace spine
