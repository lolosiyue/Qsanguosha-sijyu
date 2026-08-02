#include "photo-layout-fit.h"

#include <array>
#include <cmath>

namespace
{
using PhotoLayoutFit::LayoutTier;
using PhotoLayoutFit::RegionCounts;

bool fuzzyEqual(double left, double right)
{
    return std::abs(left - right) < 0.0001;
}
}

int main()
{
    const PhotoLayoutFit::LayoutDimensions small { 94.0, 108.0 };
    const PhotoLayoutFit::LayoutDimensions normal { 157.0, 181.0 };
    const PhotoLayoutFit::LayoutDimensions big { 235.0, 271.0 };

    RegionCounts fourPlayerCounts {};
    fourPlayerCounts[1] = 1;
    fourPlayerCounts[5] = 1;
    fourPlayerCounts[6] = 1;
    const PhotoLayoutFit::Result fourPlayer = PhotoLayoutFit::choose(
        940.0, 530.0, fourPlayerCounts, 32.0, 32.0, small, normal, big);
    if (fourPlayer.tier != LayoutTier::Big || !fuzzyEqual(fourPlayer.scale, 1.0))
        return 1;

    RegionCounts eightPlayerCounts {};
    eightPlayerCounts[1] = 3;
    eightPlayerCounts[5] = 2;
    eightPlayerCounts[6] = 2;
    const PhotoLayoutFit::Result eightPlayer = PhotoLayoutFit::choose(
        940.0, 530.0, eightPlayerCounts, 32.0, 32.0, small, normal, big);
    if (eightPlayer.tier != LayoutTier::Normal || !fuzzyEqual(eightPlayer.scale, 1.0))
        return 2;

    RegionCounts twentyPlayerCounts {};
    twentyPlayerCounts[3] = 5;
    twentyPlayerCounts[4] = 5;
    twentyPlayerCounts[7] = 9;
    const PhotoLayoutFit::Result hdpi1080 = PhotoLayoutFit::choose(
        940.0, 530.0, twentyPlayerCounts, 32.0, 32.0, small, normal, big);
    if (hdpi1080.tier != LayoutTier::Small || hdpi1080.scale >= 1.0
        || !PhotoLayoutFit::fits(940.0, 530.0, twentyPlayerCounts, 32.0, 32.0,
                                 small, hdpi1080.scale))
        return 3;

    const PhotoLayoutFit::Result ultrawide1440 = PhotoLayoutFit::choose(
        1450.0, 610.0, twentyPlayerCounts, 32.0, 32.0, small, normal, big);
    if (ultrawide1440.tier != LayoutTier::Small || ultrawide1440.scale >= 1.0
        || !PhotoLayoutFit::fits(1450.0, 610.0, twentyPlayerCounts, 32.0, 32.0,
                                 small, ultrawide1440.scale))
        return 4;

    return 0;
}
