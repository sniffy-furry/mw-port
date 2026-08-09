#pragma once
// Generic EAGL chunk container reading.
//
// Reverse-engineered format: every chunk is [u32 id][u32 size][payload].
// Bit 31 of id marks a container (payload is itself a sequence of chunks).
// Containers are sometimes preceded, inside their own payload, by a run of
// 0x11 alignment/padding bytes before the real content begins -- callers
// must detect and skip that run themselves (see stripAlignPad below).
//
// Verified against real retail NFS Most Wanted (2005) track files:
// L2RA.BUN (master) and STREAML2RA.BUN (streamed section data).

#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>

namespace nfsmw {

// A read-only view over a byte buffer, with the same little-endian
// accessors used throughout every parser in this project.
struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;

    ByteView() = default;
    ByteView(const uint8_t* d, size_t s) : data(d), size(s) {}

    uint8_t u8(size_t off) const {
        if (off >= size) throw std::out_of_range("u8 out of range");
        return data[off];
    }
    uint16_t u16(size_t off) const {
        if (off + 2 > size) throw std::out_of_range("u16 out of range");
        uint16_t v; std::memcpy(&v, data + off, 2); return v; // assumes LE host
    }
    uint32_t u32(size_t off) const {
        if (off + 4 > size) throw std::out_of_range("u32 out of range");
        uint32_t v; std::memcpy(&v, data + off, 4); return v;
    }
    float f32(size_t off) const {
        if (off + 4 > size) throw std::out_of_range("f32 out of range");
        float v; std::memcpy(&v, data + off, 4); return v;
    }
    ByteView slice(size_t off, size_t len) const {
        if (off + len > size) throw std::out_of_range("slice out of range");
        return ByteView(data + off, len);
    }
};

struct Chunk {
    uint32_t id;
    size_t off;   // absolute offset of payload start, within the ByteView passed to iterateChunks
    size_t size;  // payload size
};

// Strict back-to-back [id][size][payload] walk. No gap-guessing -- this
// matches how the file format actually chains sibling chunks once you are
// already positioned at the start of a container's payload.
inline std::vector<Chunk> iterateChunks(const ByteView& buf, size_t start, size_t end) {
    std::vector<Chunk> out;
    size_t off = start;
    while (off + 8 <= end) {
        uint32_t cid = buf.u32(off);
        uint32_t csize = buf.u32(off + 4);
        size_t pl = off + 8;
        if (pl + csize > end) break;
        out.push_back({cid, pl, csize});
        off = pl + csize;
        if (off % 4) off += 4 - (off % 4);
    }
    return out;
}

// Count leading 0x11 bytes at [off, off+size) -- unconditional, no minimum.
inline size_t stripAlignPad(const ByteView& buf, size_t off, size_t size) {
    size_t pad = 0;
    while (pad < size && buf.u8(off + pad) == 0x11) pad++;
    return pad;
}

inline bool isContainerId(uint32_t cid) {
    return cid == 0 || (cid & 0x80000000u) != 0 ||
           (cid >= 0x33000000 && cid <= 0x34000000) ||
           (cid >= 0x00130000 && cid <= 0x00140000);
}

// Recursive search for chunks of specific ids anywhere below [start,end),
// automatically descending through sentinel-padded containers (a real,
// >=8-byte run of 0x11 before a container's child chunk-tree begins).
inline void findChunks(const ByteView& buf, size_t start, size_t end,
                        const std::vector<uint32_t>& targetIds,
                        int depth, int maxDepth,
                        std::vector<Chunk>& results) {
    size_t off = start;
    while (off + 8 <= end) {
        uint32_t cid = buf.u32(off);
        uint32_t csize = buf.u32(off + 4);
        size_t pl = off + 8;
        if (pl + csize > end) return;
        for (uint32_t t : targetIds) if (t == cid) results.push_back({cid, pl, csize});
        if (isContainerId(cid) && depth < maxDepth && csize > 0 && cid != 0) {
            size_t pp = pl, run = 0;
            while (pp < pl + csize && buf.u8(pp) == 0x11) { run++; pp++; }
            if (run >= 8) findChunks(buf, pp, pl + csize, targetIds, depth + 1, maxDepth, results);
            else          findChunks(buf, pl, pl + csize, targetIds, depth + 1, maxDepth, results);
        }
        off = pl + csize;
        if (off % 4) off += 4 - (off % 4);
    }
}

} // namespace nfsmw
