#pragma once
#include "chunk.h"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

namespace nfsmw {

struct GeomGroup {
    uint32_t fvf, vcount, tcount, firstIdx, icount;
};

struct GeomVB { size_t off, size, pad; };

struct GeomSlot {
    size_t vbOff, stride, byteOffset, vertsBefore;
    bool valid = false;
};

struct GeomMesh {
    std::vector<std::array<float,3>> verts;
    std::vector<std::array<uint32_t,3>> tris;
};

inline int strideForFVF(uint32_t fvf) {
    if (fvf & 0x00200000u) return 60;
    if (fvf & 0x00004000u) return 36;
    return 0;
}

inline GeomGroup readGroup(const ByteView& b, size_t off) {
    GeomGroup g;
    g.fvf = b.u32(off+56);
    g.vcount = b.u32(off+60);
    g.tcount = b.u32(off+64);
    g.firstIdx = b.u32(off+68);
    g.icount = b.u32(off+92);
    if (!g.icount) g.icount = g.tcount * 3;
    return g;
}

// Returns {groupCount, stride, byteOffset} or groupCount==0 if no fit found.
struct FitResult { int groupCount = 0; int stride = 0; size_t byteOffset = 0; };

inline FitResult fitGroupsToVB(const std::vector<GeomGroup>& groups, size_t gi,
                                size_t vbSize, size_t vbPad, bool requireAll) {
    static const int candidates_base[] = {36,60,24,48,72,28,32,40,44,52,56,64,80,96};
    int hint = strideForFVF(groups[gi].fvf);
    std::vector<int> candidates;
    if (hint) candidates.push_back(hint);
    for (int s : candidates_base) if (s != hint) candidates.push_back(s);

    size_t remaining = groups.size() - gi;
    FitResult best;
    bool haveBest = false;
    auto leadDist = [&](size_t lead) { return lead > vbPad ? lead - vbPad : vbPad - lead; };

    for (int s : candidates) {
        size_t total = 0;
        for (size_t k = 0; k < remaining; k++) {
            total += groups[gi+k].vcount;
            size_t need = total * (size_t)s;
            if (need > vbSize) break;
            size_t lead = vbSize - need;
            if (lead > vbPad) continue;
            if (requireAll) {
                if (k + 1 != remaining) continue;
                if (!haveBest || leadDist(lead) < leadDist(best.byteOffset)) {
                    best = {(int)(k+1), s, lead};
                    haveBest = true;
                }
            } else {
                return {(int)(k+1), s, lead};
            }
        }
    }
    return haveBest ? best : FitResult{};
}

inline GeomMesh parseGeometryObject(const ByteView& sec, size_t objOff, size_t objSize) {
    GeomMesh mesh;
    size_t payloadStart = objOff + 8;
    auto top = iterateChunks(sec, payloadStart, payloadStart + objSize);

    const Chunk* header = nullptr;
    const Chunk* meshChild = nullptr;
    for (auto& c : top) {
        if (c.id == 0x00134011) header = &c;
        else if (c.id == 0x80134100) meshChild = &c;
    }
    if (!header || !meshChild) return mesh;

    size_t pad = stripAlignPad(sec, header->off, header->size);
    if (header->size < pad + 160) return mesh;

    auto mchildren = iterateChunks(sec, meshChild->off, meshChild->off + meshChild->size);
    uint32_t hdrNumGroups = 0;
    const Chunk* groupChunk = nullptr;
    std::vector<GeomVB> vbs;
    size_t indexOff = 0, indexSize = 0;
    bool haveIndex = false;

    for (auto& c : mchildren) {
        if (c.id == 0x00134900) {
            size_t pad2 = stripAlignPad(sec, c.off, c.size);
            if (c.size >= pad2 + 20) hdrNumGroups = sec.u32(c.off + pad2 + 16);
        } else if (c.id == 0x00134B02) {
            groupChunk = &c;
        } else if (c.id == 0x00134B01) {
            size_t p2 = stripAlignPad(sec, c.off, c.size);
            vbs.push_back({c.off, c.size, p2});
        } else if (c.id == 0x00134B03) {
            size_t p2 = stripAlignPad(sec, c.off, c.size);
            indexOff = c.off + p2; indexSize = c.size - p2; haveIndex = true;
        }
    }
    if (!groupChunk || vbs.empty() || !haveIndex) return mesh;

    size_t recsBytes = (size_t)hdrNumGroups * 104;
    size_t lead;
    if (hdrNumGroups && groupChunk->size >= recsBytes && (groupChunk->size - recsBytes) < 104)
        lead = groupChunk->size - recsBytes;
    else
        lead = stripAlignPad(sec, groupChunk->off, groupChunk->size);

    std::vector<GeomGroup> groups;
    size_t gp = groupChunk->off + lead;
    size_t gEnd = groupChunk->off + groupChunk->size;
    while (gp + 104 <= gEnd) { groups.push_back(readGroup(sec, gp)); gp += 104; }
    if (groups.empty()) return mesh;

    size_t totalIndices = indexSize / 2;
    std::vector<GeomSlot> slots(groups.size());
    std::vector<bool> vbUsed(vbs.size(), false);
    size_t gi = 0;
    while (gi < groups.size()) {
        bool assigned = false;
        for (size_t vbi = 0; vbi < vbs.size(); vbi++) {
            if (vbUsed[vbi]) continue;
            bool requireAll = vbs.size() == 1;
            FitResult fit = fitGroupsToVB(groups, gi, vbs[vbi].size, vbs[vbi].pad, requireAll);
            if (fit.groupCount > 0) {
                size_t cb = fit.byteOffset, cv = 0;
                for (int k = 0; k < fit.groupCount; k++) {
                    auto& g = groups[gi+k];
                    slots[gi+k] = {vbs[vbi].off, (size_t)fit.stride, cb, cv, true};
                    cb += (size_t)g.vcount * fit.stride;
                    cv += g.vcount;
                }
                vbUsed[vbi] = true;
                gi += fit.groupCount;
                assigned = true;
                break;
            }
        }
        if (!assigned) gi++;
    }

    for (size_t i = 0; i < groups.size(); i++) {
        if (!slots[i].valid) continue;
        auto& g = groups[i];
        auto& slot = slots[i];
        size_t vbEnd = 0;
        for (auto& vb : vbs) if (vb.off == slot.vbOff) vbEnd = vb.off + vb.size;

        uint32_t baseIdx = (uint32_t)mesh.verts.size();
        for (uint32_t k = 0; k < g.vcount; k++) {
            size_t vp = slot.vbOff + slot.byteOffset + (size_t)k * slot.stride;
            if (vp + 12 > vbEnd) { mesh.verts.push_back({0,0,0}); continue; }
            float x = sec.f32(vp), y = sec.f32(vp+4), z = sec.f32(vp+8);
            bool ok = std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
            mesh.verts.push_back(ok ? std::array<float,3>{x,y,z} : std::array<float,3>{0,0,0});
        }

        uint32_t first = g.firstIdx, icount = g.icount;
        if (first + icount > totalIndices) icount = (first < totalIndices) ? (uint32_t)(totalIndices - first) : 0;
        std::vector<uint16_t> idxs(icount);
        for (uint32_t k = 0; k < icount; k++) idxs[k] = sec.u16(indexOff + (size_t)(first+k)*2);
        if (idxs.empty()) continue;

        uint16_t maxIdx = *std::max_element(idxs.begin(), idxs.end());
        uint16_t minIdx = *std::min_element(idxs.begin(), idxs.end());
        if (maxIdx >= g.vcount && slot.vertsBefore > 0 &&
            minIdx >= slot.vertsBefore && maxIdx < slot.vertsBefore + g.vcount) {
            for (auto& v : idxs) v = (uint16_t)(v - slot.vertsBefore);
            maxIdx = (uint16_t)(maxIdx - slot.vertsBefore);
        }
        if (maxIdx >= g.vcount) continue;

        for (size_t t = 0; t + 2 < idxs.size(); t += 3)
            mesh.tris.push_back({baseIdx+idxs[t], baseIdx+idxs[t+1], baseIdx+idxs[t+2]});
    }
    return mesh;
}

// Finds every 0x80134010 (GeometryObject) chunk within [start,end).
inline void findGeometryObjects(const ByteView& sec, size_t start, size_t end,
                                 int depth, int maxDepth, std::vector<Chunk>& out) {
    size_t off = start;
    while (off + 8 <= end) {
        uint32_t cid = sec.u32(off), size = sec.u32(off+4);
        size_t pl = off + 8;
        if (pl + size > end) return;
        if (cid == 0x80134010) out.push_back({cid, off, size}); // note: off, not pl -- object header starts here
        if (isContainerId(cid) && depth < maxDepth && size > 0 && cid != 0) {
            size_t pp = pl, run = 0;
            while (pp < end && sec.u8(pp) == 0x11) { run++; pp++; }
            if (run >= 8) findGeometryObjects(sec, pp, pl+size, depth+1, maxDepth, out);
            else          findGeometryObjects(sec, pl, pl+size, depth+1, maxDepth, out);
        }
        off = pl + size;
        if (off % 4) off += 4 - (off % 4);
    }
}

} // namespace nfsmw
