#pragma once
#include "chunk.h"
#include <vector>
#include <cstdint>

namespace nfsmw {

struct SectionEntry {
    uint32_t id;
    uint32_t streamOff;
    uint32_t sizeMem;
};

// Reads the 92-byte-per-entry streaming section table from a track master
// .BUN file (chunk id 0x00034110). Each entry tells you where to find that
// section's data inside the paired STREAM<track>.BUN file.
inline std::vector<SectionEntry> parseSectionTable(const ByteView& master) {
    std::vector<Chunk> found;
    findChunks(master, 0, master.size, {0x00034110}, 0, 6, found);
    if (found.empty()) throw std::runtime_error("section table (0x34110) not found");
    const Chunk& c = found[0];
    size_t n = c.size / 92;
    std::vector<SectionEntry> sections;
    sections.reserve(n);
    for (size_t i = 0; i < n; i++) {
        size_t e = c.off + i * 92;
        sections.push_back({
            master.u32(e + 0x00),
            master.u32(e + 0x14),
            master.u32(e + 0x18),
        });
    }
    return sections;
}

} // namespace nfsmw
