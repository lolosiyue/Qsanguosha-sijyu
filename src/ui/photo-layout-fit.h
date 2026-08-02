#ifndef PHOTO_LAYOUT_FIT_H
#define PHOTO_LAYOUT_FIT_H

#include <array>

namespace PhotoLayoutFit
{
enum class LayoutTier
{
    Small,
    Normal,
    Big
};

struct LayoutDimensions
{
    double width;
    double height;
};

using RegionCounts = std::array<int, 8>;

struct Result
{
    LayoutTier tier;
    double scale;
};

inline bool fits(double tableWidth, double tableHeight, const RegionCounts &counts,
                 double horizontalGap, double verticalGap,
                 const LayoutDimensions &layout, double scale)
{
    if (tableWidth <= 0.0 || tableHeight <= 0.0 || scale <= 0.0)
        return false;

    const double photoWidth = layout.width * scale;
    const double photoHeight = layout.height * scale;
    const double hGap = horizontalGap * scale;
    const double vGap = verticalGap * scale;
    const double col1 = photoWidth + hGap;
    const double row1 = photoHeight + vGap;
    const std::array<double, 8> regionWidths {
        col1, tableWidth - col1 * 2.0, col1, col1,
        col1, col1, col1, tableWidth
    };
    const std::array<double, 8> regionHeights {
        row1, row1, row1, tableHeight - row1,
        tableHeight - row1, tableHeight, tableHeight, row1
    };

    for (int region = 0; region < 8; ++region) {
        const int count = counts[region];
        if (count == 0)
            continue;

        const bool vertical = region >= 3 && region <= 6;
        const double available = vertical ? regionHeights[region] : regionWidths[region];
        const double photoExtent = vertical ? photoHeight : photoWidth;
        const double gap = vertical ? vGap : hGap;
        const double required = count * photoExtent + (count - 1) * gap;
        if (available + 0.000001 < required)
            return false;
    }

    return true;
}

inline Result choose(double tableWidth, double tableHeight, const RegionCounts &counts,
                     double horizontalGap, double verticalGap,
                     const LayoutDimensions &smallLayout, const LayoutDimensions &normalLayout,
                     const LayoutDimensions &bigLayout)
{
    if (fits(tableWidth, tableHeight, counts, horizontalGap, verticalGap, bigLayout, 1.0))
        return { LayoutTier::Big, 1.0 };
    if (fits(tableWidth, tableHeight, counts, horizontalGap, verticalGap, normalLayout, 1.0))
        return { LayoutTier::Normal, 1.0 };
    if (fits(tableWidth, tableHeight, counts, horizontalGap, verticalGap, smallLayout, 1.0))
        return { LayoutTier::Small, 1.0 };

    double lower = 0.0;
    double upper = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double middle = (lower + upper) / 2.0;
        if (fits(tableWidth, tableHeight, counts, horizontalGap, verticalGap, smallLayout, middle))
            lower = middle;
        else
            upper = middle;
    }

    return { LayoutTier::Small, lower };
}
}

#endif
