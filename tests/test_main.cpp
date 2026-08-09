#include "../src/formats/chunk.h"
#include "../src/formats/section_table.h"
#include "../src/formats/carp.h"
#include "../src/formats/solidlist.h"
#include "../src/formats/scenery.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <fstream>

using namespace nfsmw;

static std::vector<uint8_t> readFile(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(1); }
    size_t n = (size_t)f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(n);
    f.read((char*)buf.data(), n);
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s master.BUN stream.BUN\n", argv[0]); return 1; }

    auto masterData = readFile(argv[1]);
    auto streamData = readFile(argv[2]);
    ByteView master(masterData.data(), masterData.size());
    ByteView stream(streamData.data(), streamData.size());

    std::printf("--- section table ---\n");
    auto sections = parseSectionTable(master);
    std::printf("sections: %zu  first: id=%u off=%u sizeMem=%u\n",
        sections.size(), sections[0].id, sections[0].streamOff, sections[0].sizeMem);

    std::printf("--- road network ---\n");
    auto road = parseRoadNetwork(master);
    std::printf("nodes: %zu  segs: %zu\n", road.nodes.size(), road.segs.size());
    std::printf("node0: x=%.3f h=%.3f z=%.3f\n", road.nodes[0].x, road.nodes[0].h, road.nodes[0].z);
    size_t degSum = 0;
    for (auto& n : road.nodes) degSum += n.seglist.size();
    std::printf("sum(degree)=%zu  expect=%zu  %s\n", degSum, road.segs.size()*2,
        degSum == road.segs.size()*2 ? "OK" : "MISMATCH");

    std::printf("--- geometry test on known section id=3355717 ---\n");
    const SectionEntry* testSec = nullptr;
    for (auto& s : sections) if (s.id == 3355717) testSec = &s;
    if (!testSec) { std::fprintf(stderr, "test section not found\n"); return 1; }
    size_t secStart = testSec->streamOff, secEnd = testSec->streamOff + testSec->sizeMem;

    std::vector<Chunk> objs;
    findGeometryObjects(stream, secStart, secEnd, 0, 6, objs);
    std::printf("geometry objects found: %zu\n", objs.size());

    size_t okCount = 0, vertTotal = 0, triTotal = 0;
    for (auto& o : objs) {
        GeomMesh m = parseGeometryObject(stream, o.off, o.size);
        if (!m.verts.empty() && !m.tris.empty()) { okCount++; vertTotal += m.verts.size(); triTotal += m.tris.size(); }
    }
    std::printf("objects with valid geometry: %zu / %zu\n", okCount, objs.size());
    std::printf("total verts: %zu  total tris: %zu\n", vertTotal, triTotal);

    std::printf("--- scenery test on same section ---\n");
    auto scenery = parseSceneryInstances(stream, secStart, secEnd);
    std::printf("scenery instances: %zu\n", scenery.size());

    std::printf("\nALL TESTS RAN\n");
    return 0;
}
