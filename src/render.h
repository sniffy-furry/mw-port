#pragma once
#include "raylib.h"
#include "formats/carp.h"
#include "formats/solidlist.h"
#include <vector>
#include <array>
#include <cmath>

namespace nfsmw {

// Builds a flat, non-indexed raylib Mesh (each triangle's 3 vertices are
// stored directly, no shared-index buffer) from parsed geometry. This
// sidesteps raylib's 16-bit index limit, which real combined world
// geometry can exceed; the memory cost of duplicated vertices is fine for
// static opaque scenery at this scale.
inline Mesh buildGeometryMesh(const std::vector<GeomMesh>& parts) {
    size_t triCount = 0;
    for (auto& m : parts) triCount += m.tris.size();

    Mesh mesh = {0};
    mesh.triangleCount = (int)triCount;
    mesh.vertexCount = (int)triCount * 3;
    mesh.vertices = (float*)MemAlloc(sizeof(float) * 3 * mesh.vertexCount);
    mesh.normals  = (float*)MemAlloc(sizeof(float) * 3 * mesh.vertexCount);

    size_t vi = 0;
    for (auto& m : parts) {
        for (auto& tri : m.tris) {
            std::array<float,3> pts[3];
            for (int k = 0; k < 3; k++) {
                auto& src = m.verts[tri[k]];
                // file is Z-up; raylib/OpenGL convention here is Y-up
                pts[k] = {src[0], src[2], src[1]};
            }
            // flat face normal
            float ux = pts[1][0]-pts[0][0], uy = pts[1][1]-pts[0][1], uz = pts[1][2]-pts[0][2];
            float vx = pts[2][0]-pts[0][0], vy = pts[2][1]-pts[0][1], vz = pts[2][2]-pts[0][2];
            float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
            float len = sqrtf(nx*nx+ny*ny+nz*nz);
            if (len > 1e-6f) { nx/=len; ny/=len; nz/=len; }
            for (int k = 0; k < 3; k++) {
                mesh.vertices[vi*3+0] = pts[k][0];
                mesh.vertices[vi*3+1] = pts[k][1];
                mesh.vertices[vi*3+2] = pts[k][2];
                mesh.normals[vi*3+0] = nx;
                mesh.normals[vi*3+1] = ny;
                mesh.normals[vi*3+2] = nz;
                vi++;
            }
        }
    }
    UploadMesh(&mesh, false);
    return mesh;
}

// Road ribbon: a flat quad (2 triangles, non-indexed) per segment,
// following real node height so it follows the terrain.
inline Mesh buildRoadMesh(const RoadNetwork& road, float halfWidth) {
    size_t n = road.segs.size();
    Mesh mesh = {0};
    mesh.triangleCount = (int)n * 2;
    mesh.vertexCount = (int)n * 6;
    mesh.vertices = (float*)MemAlloc(sizeof(float) * 3 * mesh.vertexCount);
    mesh.normals  = (float*)MemAlloc(sizeof(float) * 3 * mesh.vertexCount);

    size_t vi = 0;
    for (auto& seg : road.segs) {
        auto& a = road.nodes[seg.first];
        auto& b = road.nodes[seg.second];
        float dx = b.x - a.x, dz = b.z - a.z;
        float len = sqrtf(dx*dx+dz*dz);
        if (len < 1e-4f) continue;
        dx /= len; dz /= len;
        float px = -dz * halfWidth, pz = dx * halfWidth;
        float ax0=a.x+px, az0=a.z+pz, ax1=a.x-px, az1=a.z-pz;
        float bx0=b.x-px, bz0=b.z-pz, bx1=b.x+px, bz1=b.z+pz;
        float pts[6][3] = {
            {ax0, a.h+0.06f, az0}, {ax1, a.h+0.06f, az1}, {bx0, b.h+0.06f, bz0},
            {ax0, a.h+0.06f, az0}, {bx0, b.h+0.06f, bz0}, {bx1, b.h+0.06f, bz1},
        };
        for (int k = 0; k < 6; k++) {
            mesh.vertices[vi*3+0]=pts[k][0]; mesh.vertices[vi*3+1]=pts[k][1]; mesh.vertices[vi*3+2]=pts[k][2];
            mesh.normals[vi*3+0]=0; mesh.normals[vi*3+1]=1; mesh.normals[vi*3+2]=0;
            vi++;
        }
    }
    // trim unused tail if some segments were skipped (near-zero length)
    mesh.vertexCount = (int)vi;
    mesh.triangleCount = (int)vi / 3;
    UploadMesh(&mesh, false);
    return mesh;
}

} // namespace nfsmw
