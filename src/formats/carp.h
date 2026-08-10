#pragma once
#include "chunk.h"
#include <vector>
#include <string>
#include <cstdint>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>

namespace nfsmw {

struct RoadNode {
    float x, h, z;
    std::vector<uint16_t> seglist; // neighbour segment indices
};

struct RoadNetwork {
    std::vector<RoadNode> nodes;
    std::vector<std::pair<uint16_t,uint16_t>> segs;
};

// The CARP tag directory's own "offset" fields are not reliable file
// positions -- each array is preceded by a small, tag-specific header
// whose size isn't recorded anywhere. True offsets are instead found by
// searching for structural invariants that only hold at the real position:
//   - node[k].nodeIndex == k for many consecutive k (stride 32)
//   - segment[s]'s two endpoint nodes are exactly the nodes whose own
//     seglists reference segment s (cross-checked against the node array)
inline RoadNetwork parseRoadNetwork(const ByteView& master) {
    std::vector<Chunk> found;
    findChunks(master, 0, master.size, {0x0003B800}, 0, 6, found);
    if (found.empty()) throw std::runtime_error("CARP (0x3B800) chunk not found");
    size_t blobOff = found[0].off;
    size_t blobEnd = blobOff + found[0].size;

    if (master.u8(blobOff + 8) != 'P') throw std::runtime_error("PRAC magic missing");

    struct DirEntry { uint32_t count, claimedOff, byteSize; };
    std::unordered_map<std::string, DirEntry> dir;
    size_t p = blobOff + 8;
    int consecInvalid = 0;
    while (p - (blobOff + 8) < 1024 && consecInvalid < 3) {
        uint8_t b0=master.u8(p), b1=master.u8(p+1), b2=master.u8(p+2), b3=master.u8(p+3);
        bool printable = b0>=0x20&&b0<=0x7A && b1>=0x20&&b1<=0x7A && b2>=0x20&&b2<=0x7A && b3>=0x20&&b3<=0x7A;
        if (printable) {
            std::string tag = {(char)b3,(char)b2,(char)b1,(char)b0};
            uint32_t v1 = master.u32(p+4), count = master.u32(p+8), claimedOff = master.u32(p+12);
            dir[tag] = {count, claimedOff, v1 >> 8};
            consecInvalid = 0;
        } else consecInvalid++;
        p += 16;
    }
    if (!dir.count("RNnd") || !dir.count("RNsg"))
        throw std::runtime_error("RNnd/RNsg missing from CARP directory");

    uint32_t numNodes = dir["RNnd"].count;
    uint32_t numSegs = dir["RNsg"].count;

    // --- locate the true node array ---
    size_t hint = blobOff + dir["RNnd"].claimedOff;
    size_t nodeBase = SIZE_MAX;
    size_t searchStart = hint > 512 ? hint - 512 : 0;
    for (size_t start = searchStart; start < hint + 512; start++) {
        int ok = 0;
        for (int k = 0; k < 40; k++) {
            size_t pos = start + (size_t)k * 32;
            if (pos + 32 > blobEnd) break;
            if (master.u16(pos + 0x0C) == (uint16_t)k) ok++; else break;
        }
        if (ok >= 40) { nodeBase = start; break; }
    }
    if (nodeBase == SIZE_MAX) throw std::runtime_error("node array not found via invariant");

    RoadNetwork net;
    net.nodes.reserve(numNodes);
    for (uint32_t k = 0; k < numNodes; k++) {
        size_t pn = nodeBase + (size_t)k * 32;
        RoadNode n;
        n.x = master.f32(pn); n.h = master.f32(pn+4); n.z = master.f32(pn+8);
        for (int s = 0; s < 7; s++) {
            uint16_t v = master.u16(pn + 0x12 + s*2);
            if (v != 0xAAAA) n.seglist.push_back(v);
        }
        net.nodes.push_back(std::move(n));
    }

    // --- locate the true segment array using node adjacency as ground truth ---
    std::unordered_map<uint16_t, std::unordered_set<uint16_t>> segToNodes;
    for (uint16_t k = 0; k < net.nodes.size(); k++)
        for (uint16_t s : net.nodes[k].seglist) segToNodes[s].insert(k);

    size_t segBase = SIZE_MAX;
    const int QUICK_N = 15;
    for (size_t start = 0; start + (size_t)22*QUICK_N <= blobEnd; start++) {
        bool ok = true;
        for (int s = 0; s < QUICK_N; s++) {
            size_t pos = start + (size_t)s * 22;
            uint16_t a = master.u16(pos), b = master.u16(pos+2);
            auto it = segToNodes.find((uint16_t)s);
            if (it == segToNodes.end() || !it->second.count(a) || !it->second.count(b) || a == b) { ok = false; break; }
        }
        if (ok) { segBase = start; break; }
    }
    if (segBase == SIZE_MAX) throw std::runtime_error("segment array not found via invariant");

    net.segs.reserve(numSegs);
    for (uint32_t s = 0; s < numSegs; s++) {
        size_t ps = segBase + (size_t)s * 22;
        net.segs.push_back({master.u16(ps), master.u16(ps+2)});
    }
    return net;
}

} // namespace nfsmw
