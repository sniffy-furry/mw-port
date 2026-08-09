#pragma once
#include "chunk.h"
#include <vector>
#include <array>
#include <cmath>

namespace nfsmw {

// Positions are (worldX, worldZ-ground, worldHeight) as stored -- verified
// against the real road network (matching node positions in the same area).
inline std::vector<std::array<float,3>> parseSceneryInstances(const ByteView& sec, size_t start, size_t end) {
    std::vector<Chunk> chunks;
    findChunks(sec, start, end, {0x00034103}, 0, 10, chunks);

    std::vector<std::array<float,3>> positions;
    for (auto& c : chunks) {
        size_t p = c.off;
        while (p < c.off + c.size && sec.u8(p) == 0x11) p++;
        size_t n = (c.off + c.size > p) ? (c.off + c.size - p) / 64 : 0;
        for (size_t k = 0; k < n; k++) {
            size_t q = p + k * 64;
            float mnx=sec.f32(q), mny=sec.f32(q+4), mnz=sec.f32(q+8);
            float mxx=sec.f32(q+12), mxy=sec.f32(q+16), mxz=sec.f32(q+20);
            float x=sec.f32(q+0x20), y=sec.f32(q+0x24), z=sec.f32(q+0x28);
            bool degenerate = (x==0 && y==0 && z==0) && !(mnx==mxx && mny==mxy && mnz==mxz);
            if (degenerate) { x=(mnx+mxx)/2; y=(mny+mxy)/2; z=(mnz+mxz)/2; }
            if (std::fabs(x)<20000 && std::fabs(y)<20000 && std::fabs(z)<2000)
                positions.push_back({x,y,z});
        }
    }
    return positions;
}

} // namespace nfsmw
