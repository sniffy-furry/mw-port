// NFS MW port -- main application.
//
// Flow: MENU (rotating camera showcase, options react with camera moves)
//    -> LOADING (parses real game files with our validated C++ parsers)
//    -> WORLD (drive with placeholder arcade physics, or free-fly)
//
// Missing on purpose, for now: AI, real physics, real car model, audio, UI art.
// Placeholder car is a coloured box. Untextured geometry renders as flat grey.

#include "raylib.h"
#include "raymath.h"
#include "formats/chunk.h"
#include "formats/section_table.h"
#include "formats/carp.h"
#include "formats/solidlist.h"
#include "formats/scenery.h"
#include "render.h"

#include <vector>
#include <string>
#include <fstream>
#include <cmath>
#include <cstdio>

using namespace nfsmw;

// ---------------------------------------------------------------------
// Data directory: where the user drops L2RA.BUN / STREAML2RA.BUN.
// Desktop: ./data/ next to the executable.
// Android: the app's own external files dir -- copy the files there with
// any file manager after installing (Android > Android/data/<package>/files/).
// ---------------------------------------------------------------------
#if defined(__ANDROID__)
static const char* DATA_DIR = "/sdcard/Android/data/com.nfsmwport.app/files/";
#else
static const char* DATA_DIR = "./data/";
#endif

static std::vector<uint8_t> readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { ok = false; return {}; }
    size_t n = (size_t)f.tellg();
    f.seekg(0);
    std::vector<uint8_t> buf(n);
    f.read((char*)buf.data(), n);
    ok = true;
    return buf;
}

enum class AppState { MENU, LOADING, WORLD };

struct MenuOption {
    std::string label;
    Vector3 camPos, camTarget;
};

// ---------------------------------------------------------------------
// World data, populated during LOADING
// ---------------------------------------------------------------------
struct WorldData {
    RoadNetwork road;
    std::vector<GeomMesh> geomParts;
    std::vector<std::array<float,3>> scenery;
    Model roadModel{}, geomModel{}, sceneryUnitModel{};
    Vector3 spawn{};
    float groundY = 0;
    bool ready = false;
};

static WorldData g_world;
static std::vector<std::string> g_log;
static bool g_loadStarted = false;
static bool g_loadFailed = false;

static void logMsg(const std::string& s) {
    g_log.push_back(s);
    if (g_log.size() > 12) g_log.erase(g_log.begin());
}

// ---------------------------------------------------------------------
// Loading (runs synchronously when entering LOADING state -- fine for a
// first version; a real build would thread this to keep the menu smooth)
// ---------------------------------------------------------------------
static void loadWorld() {
    bool ok1, ok2;
    auto masterData = readFile(std::string(DATA_DIR) + "L2RA.BUN", ok1);
    auto streamData = readFile(std::string(DATA_DIR) + "STREAML2RA.BUN", ok2);
    if (!ok1) { logMsg("EROARE: nu găsesc L2RA.BUN în " + std::string(DATA_DIR)); g_loadFailed = true; return; }
    if (!ok2) { logMsg("EROARE: nu găsesc STREAML2RA.BUN în " + std::string(DATA_DIR)); g_loadFailed = true; return; }

    ByteView master(masterData.data(), masterData.size());
    ByteView stream(streamData.data(), streamData.size());

    logMsg("Parsez tabelul de sectiuni...");
    std::vector<SectionEntry> sections;
    try { sections = parseSectionTable(master); }
    catch (std::exception& e) { logMsg(std::string("EROARE: ") + e.what()); g_loadFailed = true; return; }
    logMsg("OK: " + std::to_string(sections.size()) + " sectiuni");

    logMsg("Parsez reteaua de drum...");
    try { g_world.road = parseRoadNetwork(master); }
    catch (std::exception& e) { logMsg(std::string("EROARE: ") + e.what()); g_loadFailed = true; return; }
    logMsg("OK: " + std::to_string(g_world.road.nodes.size()) + " noduri, " +
           std::to_string(g_world.road.segs.size()) + " segmente");

    logMsg("Extrag geometrie + scenery...");
    const size_t TRI_CAP = 130000;
    size_t triTotal = 0;
    int used = 0;
    for (auto& s : sections) {
        if (s.sizeMem == 0) continue;
        if (triTotal > TRI_CAP) break;
        size_t secStart = s.streamOff, secEnd = s.streamOff + s.sizeMem;
        if (secEnd > stream.size) continue;

        std::vector<Chunk> objs;
        findGeometryObjects(stream, secStart, secEnd, 0, 6, objs);
        if (objs.empty()) continue;
        used++;
        for (auto& o : objs) {
            GeomMesh m = parseGeometryObject(stream, o.off, o.size);
            if (m.verts.empty() || m.tris.empty()) continue;
            float minx=1e18f,maxx=-1e18f,miny=1e18f,maxy=-1e18f,minz=1e18f,maxz=-1e18f;
            for (auto& v : m.verts) {
                minx=fminf(minx,v[0]); maxx=fmaxf(maxx,v[0]);
                miny=fminf(miny,v[1]); maxy=fmaxf(maxy,v[1]);
                minz=fminf(minz,v[2]); maxz=fmaxf(maxz,v[2]);
            }
            float span = fmaxf(maxx-minx, fmaxf(maxy-miny, maxz-minz));
            if (span < 5) continue;
            float maxabs = fmaxf(fmaxf(fabsf(minx),fabsf(maxx)), fmaxf(fmaxf(fabsf(miny),fabsf(maxy)), fmaxf(fabsf(minz),fabsf(maxz))));
            if (maxabs > 15000) continue;
            triTotal += m.tris.size();
            g_world.geomParts.push_back(std::move(m));
        }
        auto sc = parseSceneryInstances(stream, secStart, secEnd);
        g_world.scenery.insert(g_world.scenery.end(), sc.begin(), sc.end());
    }
    logMsg("OK: geometrie din " + std::to_string(used) + " sectiuni, " +
           std::to_string(triTotal) + " triunghiuri");
    logMsg("OK: " + std::to_string(g_world.scenery.size()) + " obiecte scenery");

    // build raylib meshes/models
    Mesh roadMesh = buildRoadMesh(g_world.road, 3.0f);
    g_world.roadModel = LoadModelFromMesh(roadMesh);
    g_world.roadModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{58,61,64,255};

    Mesh geomMesh = buildGeometryMesh(g_world.geomParts);
    g_world.geomModel = LoadModelFromMesh(geomMesh);
    g_world.geomModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{184,168,136,255};

    Mesh unitCyl = GenMeshCylinder(1.1f, 0.6f, 6);
    g_world.sceneryUnitModel = LoadModelFromMesh(unitCyl);
    g_world.sceneryUnitModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = Color{214,162,60,255};

    float minH = 1e18f;
    for (auto& n : g_world.road.nodes) minH = fminf(minH, n.h);
    g_world.groundY = minH - 1.5f;
    auto& n0 = g_world.road.nodes[0];
    g_world.spawn = {n0.x, n0.h, n0.z};

    g_world.ready = true;
    logMsg("GATA.");
}

// ---------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------
static int g_menuSel = 0;
static Camera3D g_menuCam;
static Vector3 g_menuCamPos, g_menuCamTarget; // current lerped values
static float g_menuT = 0;

static std::vector<MenuOption> makeMenuOptions() {
    return {
        {"PORNESTE",   {6.0f, 2.2f, 6.0f},  {0,0.6f,0}},
        {"ZBOR LIBER", {0.2f, 6.0f, 0.1f},  {0,0.5f,0}},
        {"IESIRE",     {-5.0f, 1.2f, 4.0f}, {0,0.4f,0}},
    };
}

// ---------------------------------------------------------------------
// Placeholder car / fly-camera state (world driving)
// ---------------------------------------------------------------------
struct CarState {
    float speed=0, heading=0, x=0, z=0;
    float maxSpeed=46, maxReverse=-9, accel=14, brakeDecel=26, dragDecel=6, steerRate=2.0f;
};
static CarState g_car;
static bool g_flyMode = false;
static Vector3 g_flyPos; static float g_flyYaw = PI, g_flyPitch = -0.4f;

int main(void) {
    const int screenW = 900, screenH = 1600; // portrait-ish, phone-friendly
    InitWindow(screenW, screenH, "NFS MW Port -- loader");
    SetTargetFPS(60);

    AppState state = AppState::MENU;
    auto options = makeMenuOptions();
    g_menuCamPos = options[0].camPos;
    g_menuCamTarget = options[0].camTarget;

    Camera3D worldCam = {0};
    worldCam.up = {0,1,0};
    worldCam.fovy = 60;
    worldCam.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // ---------------- MENU ----------------
        if (state == AppState::MENU) {
            bool changed = false;
            if (IsKeyPressed(KEY_DOWN)) { g_menuSel = (g_menuSel+1)%(int)options.size(); changed=true; }
            if (IsKeyPressed(KEY_UP))   { g_menuSel = (g_menuSel-1+(int)options.size())%(int)options.size(); changed=true; }

            // touch/click on an option label selects it; tapping the
            // already-selected option (or Enter/Space) confirms
            bool confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
            for (int i=0;i<(int)options.size();i++){
                Rectangle r = { 40, (float)(screenH-360+i*100), (float)screenW-80, 84 };
                bool tapped = false;
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(GetMousePosition(), r)) tapped = true;
                if (GetTouchPointCount() > 0 && CheckCollisionPointRec(GetTouchPosition(0), r)) {
                    if (IsGestureDetected(GESTURE_TAP)) tapped = true;
                }
                if (tapped) {
                    if (g_menuSel == i) confirm = true;
                    else { g_menuSel = i; changed = true; }
                }
            }

            g_menuT += dt * 2.0f;
            if (g_menuT > 1) g_menuT = 1;
            if (changed) g_menuT = 0;
            Vector3 fromPos = g_menuCamPos, fromTgt = g_menuCamTarget;
            Vector3 toPos = options[g_menuSel].camPos, toTgt = options[g_menuSel].camTarget;
            float tt = 1 - powf(1-g_menuT, 3); // ease-out
            g_menuCamPos = Vector3Lerp(fromPos, toPos, changed ? 0 : tt);
            g_menuCamTarget = Vector3Lerp(fromTgt, toTgt, changed ? 0 : tt);
            if (!changed) {
                // slow continuous orbit around whichever target we're settled near
                float angle = GetTime() * 0.15f;
                float radius = Vector3Distance({toPos.x,0,toPos.z}, {toTgt.x,0,toTgt.z});
                g_menuCamPos.x = toTgt.x + cosf(angle)*radius;
                g_menuCamPos.z = toTgt.z + sinf(angle)*radius;
            }

            if (confirm) {
                if (g_menuSel == 0) { g_flyMode = false; state = AppState::LOADING; }
                else if (g_menuSel == 1) { g_flyMode = true; state = AppState::LOADING; }
                else if (g_menuSel == 2) break;
            }
        }

        // ---------------- LOADING ----------------
        if (state == AppState::LOADING && !g_loadStarted) {
            g_loadStarted = true;
            loadWorld();
            if (!g_loadFailed) {
                g_car.x = g_world.spawn.x; g_car.z = g_world.spawn.z;
                g_flyPos = {g_world.spawn.x, g_world.spawn.y+40, g_world.spawn.z+60};
                state = AppState::WORLD;
            }
        }

        // ---------------- WORLD ----------------
        if (state == AppState::WORLD && g_world.ready) {
            if (g_flyMode) {
                float spd = 45;
                if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) { g_flyPos.x += sinf(g_flyYaw)*spd*dt; g_flyPos.z += cosf(g_flyYaw)*spd*dt; }
                if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) { g_flyPos.x -= sinf(g_flyYaw)*spd*dt; g_flyPos.z -= cosf(g_flyYaw)*spd*dt; }
                if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) g_flyYaw += 1.6f*dt;
                if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) g_flyYaw -= 1.6f*dt;
                if (IsKeyDown(KEY_SPACE)) g_flyPos.y += spd*dt;
                if (IsKeyDown(KEY_LEFT_SHIFT)) g_flyPos.y -= spd*dt;
                if (GetTouchPointCount() > 0) {
                    Vector2 d = GetTouchPosition(0);
                    static Vector2 last = d; static bool have=false;
                    if (have) { g_flyYaw -= (d.x-last.x)*0.005f; g_flyPitch -= (d.y-last.y)*0.005f; }
                    last = d; have = true;
                }
                Vector3 dir = { sinf(g_flyYaw)*cosf(g_flyPitch), sinf(g_flyPitch), cosf(g_flyYaw)*cosf(g_flyPitch) };
                worldCam.position = g_flyPos;
                worldCam.target = Vector3Add(g_flyPos, dir);
            } else {
                bool gas = IsKeyDown(KEY_UP)||IsKeyDown(KEY_W);
                bool brake = IsKeyDown(KEY_DOWN)||IsKeyDown(KEY_S);
                bool left = IsKeyDown(KEY_LEFT)||IsKeyDown(KEY_A);
                bool right = IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D);
                if (gas) g_car.speed += g_car.accel*dt;
                else if (brake) g_car.speed -= g_car.brakeDecel*dt*(g_car.speed>0?1:0.5f);
                else {
                    float drag = g_car.dragDecel*dt;
                    if (g_car.speed>0) g_car.speed = fmaxf(0, g_car.speed-drag);
                    else if (g_car.speed<0) g_car.speed = fminf(0, g_car.speed+drag);
                }
                g_car.speed = fmaxf(g_car.maxReverse, fminf(g_car.maxSpeed, g_car.speed));
                float speedFactor = fminf(1, fabsf(g_car.speed)/6) * (1 - fminf(0.55f, fabsf(g_car.speed)/g_car.maxSpeed*0.55f));
                float dirSign = g_car.speed>=0 ? 1.f : -1.f;
                float steer=0; if(left) steer+=1; if(right) steer-=1;
                g_car.heading += steer*g_car.steerRate*speedFactor*dirSign*dt;
                g_car.x += sinf(g_car.heading)*g_car.speed*dt;
                g_car.z += cosf(g_car.heading)*g_car.speed*dt;
                Vector3 camPos = { g_car.x - sinf(g_car.heading)*8.5f, g_world.groundY+3.6f, g_car.z - cosf(g_car.heading)*8.5f };
                worldCam.position = Vector3Lerp(worldCam.position, camPos, 0.14f);
                worldCam.target = { g_car.x, g_world.groundY+1.0f, g_car.z };
            }
        }

        // ================= DRAW =================
        BeginDrawing();
        ClearBackground(Color{20,22,26,255});

        if (state == AppState::MENU) {
            g_menuCam = {0};
            g_menuCam.position = g_menuCamPos;
            g_menuCam.target = g_menuCamTarget;
            g_menuCam.up = {0,1,0};
            g_menuCam.fovy = 45;
            g_menuCam.projection = CAMERA_PERSPECTIVE;

            ClearBackground(Color{25,28,33,255});
            BeginMode3D(g_menuCam);
            DrawCube({0,0.55f,0}, 1.9f,0.5f,4.2f, Color{217,79,61,255});
            DrawCube({0,0.95f,-0.1f}, 1.5f,0.45f,2.0f, Color{30,34,38,255});
            DrawGrid(20, 1.0f);
            EndMode3D();

            DrawText("NFS MW -- PORT", 40, 60, 34, RAYWHITE);
            DrawText("geometrie + drum + scenery reale. AI: lipsa. fizica: placeholder.", 40, 104, 14, Fade(RAYWHITE,0.6f));
            for (int i=0;i<(int)options.size();i++){
                Rectangle r = { 40, (float)(screenH-360+i*100), (float)screenW-80, 84 };
                bool sel = (i==g_menuSel);
                DrawRectangleRounded(r, 0.25f, 8, sel?Fade(SKYBLUE,0.35f):Fade(RAYWHITE,0.08f));
                DrawText(options[i].label.c_str(), (int)r.x+24, (int)r.y+26, 28, RAYWHITE);
            }
        }
        else if (state == AppState::LOADING) {
            DrawText("SE INCARCA...", 40, 80, 30, RAYWHITE);
            int y = 140;
            for (auto& l : g_log) { DrawText(l.c_str(), 40, y, 16, Fade(RAYWHITE,0.85f)); y += 24; }
            if (g_loadFailed) DrawText("Verifica fisierele din folderul de date.", 40, y+20, 16, Color{255,140,128,255});
        }
        else if (state == AppState::WORLD) {
            BeginMode3D(worldCam);
            DrawModel(g_world.roadModel, {0,0,0}, 1.0f, WHITE);
            DrawModel(g_world.geomModel, {0,0,0}, 1.0f, WHITE);
            for (auto& s : g_world.scenery)
                DrawModel(g_world.sceneryUnitModel, {s[0], s[2]+0.3f, s[1]}, 1.0f, WHITE);
            if (!g_flyMode) {
                DrawCube({g_car.x, g_world.groundY+0.55f+0.15f, g_car.z}, 1.9f,0.5f,4.2f, Color{217,79,61,255});
            }
            EndMode3D();

            DrawText(g_flyMode ? "ZBOR LIBER" : TextFormat("%.0f km/h", fabsf(g_car.speed)*3.6f), 30, 30, 26, RAYWHITE);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
