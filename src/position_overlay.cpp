// Position-only adaptation of GD Access's world.cpp coordinate reader.
// Original copyright (c) 2026 Austin Hicks. See THIRD_PARTY_NOTICES.md.
// This is an altered, independent prototype, not the GD Access mod.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#include <tlhelp32.h>
#include <detours.h>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <vector>
#include <memory>
#include <array>
#include "guide_operations.h"
#include "area_connections.h"


namespace {
HMODULE module;
SRWLOCK lock = SRWLOCK_INIT;
constexpr unsigned VisibleQuestRows = 5;
enum class DetailKind { Task, Description, Objective };
struct DetailRow {
    wchar_t text[1024];
    unsigned taskIndex;
    unsigned taskUid;
    unsigned objectiveUid;
    int objectiveState;
    int taskState;
    DetailKind kind;
};
struct QuestRow {
    unsigned id; wchar_t name[256];
    std::shared_ptr<std::vector<DetailRow>> details;
    bool detailsValid;
};
constexpr unsigned DetailPageRows = 5;
// Six shortcut rows at up to 20 pixels each, plus bottom padding.
constexpr int PanelFooterHeight = 132;
constexpr int PanelBaseHeight = 570 + PanelFooterHeight;
constexpr int PanelMinimumHeight = PanelBaseHeight + 24;
unsigned detailPage = 0; // Window-thread diagnostic page, not navigation selection.
size_t detailAnchor = 0;
bool taskFocusInitialized = false;
std::vector<unsigned> focusedTasks;
struct QuestSnapshot {
    std::vector<QuestRow> rows; unsigned count, total;
    ULONGLONG time; bool valid;
};
enum class TargetState { Unavailable, NotDetected, Live, Dead };
struct TargetSample {
    TargetState state = TargetState::Unavailable;
    float x = 0, y = 0, z = 0;
    unsigned id = 0;
    ULONGLONG time = 0;
};
struct Sample {
    float x, y, z; ULONGLONG time; bool valid;
    float cameraYaw; bool cameraValid;
    float screenX = 0, screenY = 0;
    int viewportWidth = 0, viewportHeight = 0;
    bool screenValid = false;
    TargetSample target;
    TargetSample shrine;
    std::shared_ptr<const guides::Guide> guide = std::atomic_load(&guides::published);
    std::vector<TargetSample> targets = std::vector<TargetSample>(guide->targets.size());
    wchar_t zone[256]; char zoneTag[256]; ULONGLONG zoneTime;
    QuestSnapshot quests;
} sample{};
std::vector<connections::Observation> observedConnections; // Guarded by sample lock; no guide edits in hooks.
bool observedConnectionsDirty=false;
volatile LONG enabled = 1;
enum class DisplayMode { TriangleOnly, TriangleAndPanel, Hidden };
DisplayMode displayMode = DisplayMode::TriangleOnly; // Window thread only.
HWND captureNoticeWindow = nullptr;
ULONGLONG captureNoticeStart = 0;
constexpr ULONGLONG CaptureNoticeDuration = 2000;
constexpr int CaptureNoticeWidth = 380, CaptureNoticeHeight = 84;
BYTE CaptureNoticeAlpha(ULONGLONG elapsed) {
    if(elapsed>=CaptureNoticeDuration) return 0;
    return static_cast<BYTE>(245*(CaptureNoticeDuration-elapsed)/CaptureNoticeDuration);
}
void StartCaptureNotice() {captureNoticeStart=GetTickCount64();}
LRESULT CALLBACK CaptureNoticeProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_ERASEBKGND) return 1;
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);
        RECT bounds{};GetClientRect(hwnd,&bounds);
        HBRUSH background=CreateSolidBrush(RGB(19,24,27));FillRect(dc,&bounds,background);DeleteObject(background);
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(229,211,161));
        HFONT noticeFont=CreateFontW(-29,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        HGDIOBJ previous=SelectObject(dc,noticeFont);
        DrawTextW(dc,L"LOCATION CAPTURED",-1,&bounds,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(dc,previous);DeleteObject(noticeFont);
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
DisplayMode NextDisplayMode(DisplayMode mode) {
    return mode == DisplayMode::TriangleOnly ? DisplayMode::TriangleAndPanel :
        mode == DisplayMode::TriangleAndPanel ? DisplayMode::Hidden : DisplayMode::TriangleOnly;
}
bool ShowPanel(DisplayMode mode) { return mode == DisplayMode::TriangleAndPanel; }
bool ShowTriangle(DisplayMode mode) { return mode != DisplayMode::Hidden; }
const wchar_t* DisplayModeLabel(DisplayMode mode) {
    return mode == DisplayMode::Hidden ? L"HIDDEN" :
        mode == DisplayMode::TriangleAndPanel ? L"VISIBLE WITH PANEL" : L"VISIBLE";
}
bool hasActiveQuest = false;
QuestRow activeQuest{}; // Overlay-only selection, scoped to this running session.
HFONT font;
FILE* logFile;
void Log(const char* text) { if (logFile) { fprintf(logFile, "%llu %s\n", GetTickCount64(), text); fflush(logFile); } }
using Update = void (*)(void*, int);
Update original;
void* (*mainPlayer)(const void*);
void* (*getCamera)(void*);
float (*getCameraYaw)(const void*);
void* (*projectWorldPoint)(const void*, void*, const void*, const void*);
void* (*makeViewport)(void*, int, int, int, int);
void* (*footCoords)(void*, void*, bool);
void* (*regionCoords)(const void*, void*);
void* (*entityRegion)(const void*);
void* (*worldVec)(void*, void*, const void*);
void* (*worldPosition)(const void*, void*);
struct GameString {
    union { char small[16]; const char* heap; } storage;
    size_t size, capacity;
};
static_assert(sizeof(GameString) == 32);
const GameString* (*areaNameTag)(const void*);
void* (*localizationInstance)();
const wchar_t* (*localize)(void*, const char*);
using EngineUpdate = void (*)(void*, const void*, const void*, bool, const void*);
EngineUpdate originalEngineUpdate;
struct GameWideString {
    union { wchar_t small[8]; const wchar_t* heap; } storage;
    size_t size, capacity;
};
static_assert(sizeof(GameWideString) == 32);
struct QuestVector { void** begin; void** end; void** capacity; };
// Initially empty; only the game's allocator grows this buffer. Retained until
// process exit with the injected DLL, avoiding cross-allocator frees.
QuestVector questBuffer{};
QuestVector entityBuffer{}; // Game-allocated, reused; never free across allocators.
void (*entitiesInSphere)(void*, QuestVector*, const void*, bool, int);
const char* (*objectName)(const void*);
unsigned (*objectId)(const void*);
void* (*entityCoords)(const void*, void*);
bool (*characterAlive)(const void*);
const void* (*objectRtti)(const void*);
void** objectVtable;
const void* (*staticShrineClassInfo)();
bool (*shrineCleansed)(const void*);
int objectRttiSlot=-1;
constexpr const char* KyzoggRecord = "records/creatures/enemies/boss&quest/reanimator.dbr";
constexpr const char* MogdrogenShrineRecord = "records/interactive/devotionshrine_mogdrogen.dbr";
bool RecordMatches(const char* name, const char* expected) {
    if (!name) return false;
    for (size_t i = 0; ; ++i) {
        char c = name[i]; if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c-'A'+'a');
        if (c != expected[i]) return false;
        if (!c) return true;
    }
}
bool IsKyzoggRecord(const char* name) { return RecordMatches(name, KyzoggRecord); }
bool ShrineEligible(const char* record, bool cleansed) {
    return record && !cleansed && !RecordMatches(record, MogdrogenShrineRecord);
}
bool ReadShrine(void* entity, TargetSample* out, ULONGLONG now) {
    __try {
        if(!entity || objectRttiSlot<0 || !staticShrineClassInfo || !shrineCleansed ||
           !objectName || !objectId || !entityCoords) return false;
        void** vtable=*reinterpret_cast<void***>(entity);
        if(!vtable) return false;
        auto dynamicClass=reinterpret_cast<const void* (*)(const void*)>(vtable[objectRttiSlot])(entity);
        const void* shrineClass=staticShrineClassInfo();
        bool isShrine=false;
        for(int depth=0;dynamicClass && depth<16;++depth) {
            if(dynamicClass==shrineClass) {isShrine=true;break;}
            memcpy(&dynamicClass,static_cast<const char*>(dynamicClass)+0x10,sizeof(dynamicClass));
        }
        if(!isShrine || !ShrineEligible(objectName(entity),shrineCleansed(entity))) return false;
        void* region=entityRegion(entity);
        if(!region) return false;
        alignas(16) unsigned char wc[256]{},coords[256]{},wv[256]{},result[256]{};
        entityCoords(entity,wc);regionCoords(wc,coords);
        worldVec(wv,region,coords+36);worldPosition(wv,result);
        memcpy(&out->x,result,3*sizeof(float));
        if(!std::isfinite(out->x)||!std::isfinite(out->y)||!std::isfinite(out->z)) return false;
        out->id=objectId(entity);out->state=TargetState::Live;out->time=now;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool ReadTarget(void* entity, TargetSample* out, size_t* targetIndex, const guides::Guide& guide) {
    __try {
        // Match the database record before any Character-specific call.
        if (!entity) return false;
        const char* name = objectName(entity);
        size_t index = 0;
        while (index < guide.targets.size() && (!*guide.targets[index].record || !RecordMatches(name, guide.targets[index].record))) ++index;
        if (index == guide.targets.size()) return false;
        *targetIndex = index;
        out->id = objectId(entity);
        out->state = !guide.targets[index].enemy || characterAlive(entity) ? TargetState::Live : TargetState::Dead;
        void* region = entityRegion(entity);
        if (!region) { out->state = TargetState::Unavailable; return true; }
        alignas(16) unsigned char wc[256]{}, coords[256]{}, wv[256]{}, result[256]{};
        entityCoords(entity, wc); regionCoords(wc, coords);
        worldVec(wv, region, coords+36); worldPosition(wv, result);
        memcpy(&out->x, result, 3*sizeof(float));
        if (!std::isfinite(out->x) || !std::isfinite(out->y) || !std::isfinite(out->z))
            out->state = TargetState::Unavailable;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void ScanTargets(void* engine, ULONGLONG now, TargetSample* found, TargetSample* shrine,
                 const guides::Guide& guide, double playerX, double playerZ) {
    for (size_t i = 0; i < guide.targets.size(); ++i) { found[i] = {}; found[i].time = now; }
    *shrine={};shrine->time=now;
    __try {
        if (!entitiesInSphere || !objectName || !objectId || !entityCoords) return;
        bool targetsAvailable=characterAlive!=nullptr;
        bool shrinesAvailable=objectRttiSlot>=0 && staticShrineClassInfo && shrineCleansed;
        if(!targetsAvailable && !shrinesAvailable) return;
        void* player = mainPlayer(engine);
        void* region = player ? entityRegion(player) : nullptr;
        if (!region) return;
        alignas(16) unsigned char wc[256]{}, coords[256]{};
        footCoords(player, wc, false); regionCoords(wc, coords);
        struct { float x, y, z, radius; } sphere{};
        memcpy(&sphere, coords+36, 3*sizeof(float)); sphere.radius = 100.0f;
        entityBuffer.end = entityBuffer.begin;
        entitiesInSphere(region, &entityBuffer, &sphere, false, 0);
        uintptr_t first = reinterpret_cast<uintptr_t>(entityBuffer.begin);
        uintptr_t last = reinterpret_cast<uintptr_t>(entityBuffer.end);
        uintptr_t cap = reinterpret_cast<uintptr_t>(entityBuffer.capacity);
        if (last < first || cap < last || (last-first)%sizeof(void*) || (last-first)/sizeof(void*) > 16384) return;
        if(targetsAvailable) for (size_t j = 0; j < guide.targets.size(); ++j) found[j].state = TargetState::NotDetected;
        if(shrinesAvailable) shrine->state=TargetState::NotDetected;
        double nearestShrine=1e30;
        for (size_t i = 0; i < (last-first)/sizeof(void*); ++i) {
            if(targetsAvailable) {
                TargetSample candidate{}; candidate.time = now;
                size_t index = 0;
                if (ReadTarget(entityBuffer.begin[i], &candidate, &index, guide)) {
                    // A living match wins over a corpse. No entity pointer is retained.
                    for (size_t j = 0; j < guide.targets.size(); ++j) if (!strcmp(guide.targets[j].record, guide.targets[index].record) && found[j].state != TargetState::Live) found[j] = candidate;
                }
            }
            if(shrinesAvailable) {
                TargetSample candidate{};
                if(ReadShrine(entityBuffer.begin[i],&candidate,now)) {
                    double distance=std::hypot(candidate.x-playerX,candidate.z-playerZ);
                    if(distance<nearestShrine) {nearestShrine=distance;*shrine=candidate;}
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        for (size_t j = 0; j < guide.targets.size(); ++j) found[j].state = TargetState::Unavailable;
        shrine->state=TargetState::Unavailable;
    }
}
void* (*questRepository)();
void (*getQuests)(void*, QuestVector*, int);
const GameWideString* (*questName)(const void*);
unsigned (*questId)(const void*);
bool (*questTracked)(const void*);
bool questsAvailable = false;
unsigned (*taskCount)(const void*);
unsigned (*taskUid)(const void*);
void* (*taskAt)(const void*, int);
const GameWideString* (*taskName)(const void*);
const GameWideString* (*taskDescription)(const void*);
int (*taskState)(const void*);
const QuestVector* (*taskObjectives)(const void*);
void (*objectiveText)(const void*, GameWideString*);
int (*objectiveState)(const void*);
unsigned (*objectiveUid)(const void*);
bool detailsAvailable = false;
// Like questBuffer, this output string is allocated/reused only by game code.
GameWideString objectiveTextBuffer = {{}, 0, 7};

bool CopyGameText(const GameWideString* source, wchar_t* dest, size_t capacity) {
    if (!source || source->size > source->capacity || source->size > 65536) return false;
    const wchar_t* p = source->capacity < 8 ? source->storage.small : source->storage.heap;
    if (!p) return false;
    size_t n = source->size < capacity-1 ? source->size : capacity-1;
    memcpy(dest, p, n*sizeof(wchar_t)); dest[n] = 0;
    return true;
}
bool ReadDetails(void* quest, std::vector<DetailRow>* rows) {
    __try {
        if (!detailsAvailable) return false;
        unsigned count = taskCount(quest);
        if (count > 256) return false;
        for (unsigned i = 0; i < count; ++i) {
            void* task = taskAt(quest, static_cast<int>(i));
            if (!task) return false;
            wchar_t name[1024]{}, description[1024]{};
            if (!CopyGameText(taskName(task), name, _countof(name)) ||
                !CopyGameText(taskDescription(task), description, _countof(description))) return false;
            rows->emplace_back();
            int state = taskState(task);
            rows->back().taskIndex = i; rows->back().taskState = state; rows->back().kind = DetailKind::Task;
            rows->back().taskUid = taskUid(task);
            _snwprintf_s(rows->back().text, _countof(rows->back().text), _TRUNCATE,
                L"Task %u [state %d]: %s", i+1, state, *name ? name : L"(unnamed)");
            if (*description) {
                rows->emplace_back();
                rows->back().taskIndex = i; rows->back().taskState = state; rows->back().kind = DetailKind::Description;
                _snwprintf_s(rows->back().text, _countof(rows->back().text), _TRUNCATE, L"Description: %s", description);
            }
            const QuestVector* v = taskObjectives(task);
            if (!v) return false;
            uintptr_t begin = reinterpret_cast<uintptr_t>(v->begin), end = reinterpret_cast<uintptr_t>(v->end);
            if (end < begin || end > reinterpret_cast<uintptr_t>(v->capacity) ||
                (end-begin)%sizeof(void*) || (end-begin)/sizeof(void*) > 256) return false;
            for (size_t j = 0; j < (end-begin)/sizeof(void*); ++j) {
                void* objective = v->begin[j]; if (!objective) return false;
                objectiveText(objective, &objectiveTextBuffer);
                if (!CopyGameText(&objectiveTextBuffer, name, _countof(name))) return false;
                rows->emplace_back();
                rows->back().taskIndex = i; rows->back().taskState = state; rows->back().kind = DetailKind::Objective;
                rows->back().taskUid = taskUid(task);
                rows->back().objectiveUid = objectiveUid ? objectiveUid(objective) : 0;
                rows->back().objectiveState = objectiveState(objective);
                _snwprintf_s(rows->back().text, _countof(rows->back().text), _TRUNCATE,
                    L"Objective %u.%zu [state %d]: %s", i+1, j+1, objectiveState(objective), *name ? name : L"(no text)");
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void CaptureDetails(void* quest, QuestRow* row) {
    row->details = std::make_shared<std::vector<DetailRow>>();
    row->detailsValid = ReadDetails(quest, row->details.get());
    if (!row->detailsValid) row->details->clear();
}

bool ReadQuests(QuestSnapshot* out) {
    __try {
        if (!questsAvailable) return false;
        void* repo = questRepository();
        if (!repo) return false;
        questBuffer.end = questBuffer.begin;
        getQuests(repo, &questBuffer, 4); // Tracked filter; still validate each row.
        uintptr_t first = reinterpret_cast<uintptr_t>(questBuffer.begin);
        uintptr_t last = reinterpret_cast<uintptr_t>(questBuffer.end);
        uintptr_t cap = reinterpret_cast<uintptr_t>(questBuffer.capacity);
        if (last < first || cap < last || (last-first) % sizeof(void*) || (last-first)/sizeof(void*) > 65536) return false;
        size_t n = (last-first)/sizeof(void*);
        for (size_t i = 0; i < n; ++i) {
            void* quest = questBuffer.begin[i];
            // Filter 4 already requires an in-progress task and the tracking flag.
            // IsComplete(false) means ANY task complete on this build, so using
            // it here would incorrectly hide partially completed quests.
            if (!quest || !questTracked(quest)) continue;
            ++out->total;
            out->rows.emplace_back();
            QuestRow* row = &out->rows[out->count];
            row->id = questId(quest);
            const GameWideString* name = questName(quest);
            if (!name || name->size > name->capacity || name->size > 65536) return false;
            const wchar_t* data = name->capacity < 8 ? name->storage.small : name->storage.heap;
            if (!data) return false;
            size_t len = name->size < _countof(row->name)-1 ? name->size : _countof(row->name)-1;
            memcpy(row->name, data, len * sizeof(wchar_t));
            row->name[len] = 0;
            if (!len) wcscpy_s(row->name, L"Unnamed quest");
            CaptureDetails(quest, row);
            ++out->count;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Engine and GameEngine are distinct objects. Read the name using Engine's
// own update receiver, and copy the localized text before leaving that thread.
bool ReadZone(void* engine, Sample* out) {
    __try {
        const GameString* tag = areaNameTag(engine);
        if (!tag || !tag->size || tag->size >= sizeof(out->zoneTag) || tag->size > tag->capacity) return false;
        const char* data = tag->capacity < 16 ? tag->storage.small : tag->storage.heap;
        if (!data) return false;
        memcpy(out->zoneTag, data, tag->size);
        out->zoneTag[tag->size] = 0;
        void* manager = localizationInstance();
        const wchar_t* name = manager ? localize(manager, out->zoneTag) : nullptr;
        if (!name || !*name || wcsncmp(name, L"Tag not found", 13) == 0) return false;
        size_t n = 0;
        while (n < _countof(out->zone) - 1 && name[n]) { out->zone[n] = name[n]; ++n; }
        out->zone[n] = 0;
        return n != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void OnEngineUpdate(void* self, const void* sphere, const void* frustum, bool b, const void* frustum2) {
    originalEngineUpdate(self, sphere, frustum, b, frustum2);
    if (!InterlockedCompareExchange(&enabled, 1, 1)) return;
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last < 100) return;
    last = now;
    Sample next{};
    if (!ReadZone(self, &next)) next.zone[0] = 0;
    if (TryAcquireSRWLockExclusive(&lock)) {
        memcpy(sample.zone, next.zone, sizeof(sample.zone));
        memcpy(sample.zoneTag, next.zoneTag, sizeof(sample.zoneTag));
        sample.zoneTime = now;
        ReleaseSRWLockExclusive(&lock);
    }
}

// All game calls occur on the game's own Update thread. No game pointers cross
// into the window thread. Buffers deliberately exceed the verified value sizes.
bool ReadPosition(void* engine, Sample* out) {
    __try {
        void* player = mainPlayer(engine);
        if (!player) return false;
        void* region = entityRegion(player);
        if (!region) return false;
        alignas(16) unsigned char wc[256]{}, coords[256]{}, wv[256]{}, result[256]{};
        footCoords(player, wc, false);
        regionCoords(wc, coords);
        // Coords origin at byte 36 verified in this installed Engine.dll.
        worldVec(wv, region, coords + 36);
        worldPosition(wv, result);
        memcpy(&out->x, result, 3 * sizeof(float));
        // Project a point one world unit above the player's feet (body anchor).
        // Only copied pixels/viewport dimensions cross to the UI thread.
        if (getCamera && projectWorldPoint && makeViewport) {
            __try {
            HWND gameWindow = FindWindowW(L"Grim Dawn", nullptr);
            DWORD pid = 0; if (gameWindow) GetWindowThreadProcessId(gameWindow, &pid);
            RECT client{};
            void* camera = getCamera(engine);
            if (pid == GetCurrentProcessId() && camera && GetClientRect(gameWindow, &client) && client.right > 0 && client.bottom > 0) {
                float height = 0; memcpy(&height, coords+40, sizeof(height)); height += 1.0f;
                memcpy(coords+40, &height, sizeof(height)); worldVec(wv, region, coords+36);
                alignas(16) unsigned char viewport[256]{}; alignas(16) float pixel[4]{};
                makeViewport(viewport, 0, 0, client.right, client.bottom);
                projectWorldPoint(camera, pixel, wv, viewport);
                out->screenX = pixel[0]; out->screenY = pixel[1];
                out->viewportWidth = client.right; out->viewportHeight = client.bottom;
                out->screenValid = std::isfinite(pixel[0]) && std::isfinite(pixel[1]) &&
                    pixel[0] >= 0 && pixel[0] < client.right && pixel[1] >= 0 && pixel[1] < client.bottom;
            }
            } __except (EXCEPTION_EXECUTE_HANDLER) { out->screenValid = false; }
        }
        return std::isfinite(out->x) && std::isfinite(out->y) && std::isfinite(out->z);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadCamera(void* engine, float* yaw) {
    __try {
        if (!getCamera || !getCameraYaw) return false;
        void* camera = getCamera(engine);
        if (!camera) return false;
        *yaw = getCameraYaw(camera);
        return std::isfinite(*yaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void OnUpdate(void* self, int dt) {
    original(self, dt);
    if (!InterlockedCompareExchange(&enabled, 1, 1)) return;
    // Cheap position/camera telemetry follows game updates. Entity scans and
    // quest queries retain their independent 250/500 ms limits below.
    ULONGLONG now = GetTickCount64();
    Sample next{};
    next.time = now;
    next.valid = ReadPosition(self, &next);
    next.cameraValid = next.valid && ReadCamera(self, &next.cameraYaw);
    static ULONGLONG lastTarget = 0;
    static std::shared_ptr<const guides::Guide> scannedGuide;
    bool refreshTarget = !next.valid || now-lastTarget >= 250 || scannedGuide != next.guide;
    if (refreshTarget) {
        lastTarget = now;
        if (next.valid) ScanTargets(self, now, next.targets.data(), &next.shrine, *next.guide, next.x, next.z);
        scannedGuide = next.guide;
        for (size_t i = 0; i < next.guide->targets.size(); ++i) if (IsKyzoggRecord(next.guide->targets[i].record)) { next.target = next.targets[i]; break; }
    }
    static ULONGLONG lastQuests = 0;
    bool refreshQuests = !next.valid || now - lastQuests >= 500;
    if (refreshQuests) {
        lastQuests = now;
        next.quests.time = now;
        next.quests.valid = next.valid && ReadQuests(&next.quests);
    }
    if (TryAcquireSRWLockExclusive(&lock)) {
        static connections::Detector connectionDetector;
        if(next.valid && sample.zoneTime && now-sample.zoneTime<=500) {
            auto crossing=connectionDetector.observe(now,sample.zoneTag,sample.zone,next.x,next.y,next.z);
            if(crossing && !guides::shares(*next.guide,crossing->from.c_str(),crossing->to.c_str()) &&
               observedConnections.size()<128 &&
               std::none_of(observedConnections.begin(),observedConnections.end(),[&](const auto& seen){return connections::samePair(seen,*crossing);})) {
                observedConnections.push_back(*crossing);observedConnectionsDirty=true;
            }
        } else connectionDetector.observe(now,nullptr,nullptr,0,0,0);
        sample.x = next.x; sample.y = next.y; sample.z = next.z;
        sample.time = next.time; sample.valid = next.valid;
        sample.cameraYaw = next.cameraYaw; sample.cameraValid = next.cameraValid;
        sample.screenX = next.screenX; sample.screenY = next.screenY;
        sample.viewportWidth = next.viewportWidth; sample.viewportHeight = next.viewportHeight;
        sample.screenValid = next.screenValid;
        if (refreshTarget) { sample.target = next.target; sample.shrine=next.shrine; sample.targets = std::move(next.targets); sample.guide = next.guide; }
        if (refreshQuests) sample.quests = next.quests;
        ReleaseSRWLockExclusive(&lock);
    }
}
Sample Snapshot() {
    AcquireSRWLockShared(&lock);
    Sample result = sample;
    ReleaseSRWLockShared(&lock);
    return result;
}
bool QuestsLive(const Sample& s) {
    ULONGLONG now = GetTickCount64();
    return s.valid && s.time && now-s.time <= 1000 && s.quests.valid &&
        s.quests.time && now-s.quests.time <= 2000;
}
// Explicit test route, independent of quest selection/completion. Coordinates
// and zone associations are documented in data/coordinate-investigation.
struct CompassReading {
    const wchar_t* destination = L"Kyzogg test route";
    const wchar_t* status = L"Waiting for live position / zone";
    bool valid = false, nearby = false, bearingValid = false;
    double dx = 0, dz = 0, distance = 0;
    double screenRight = 0, screenUp = 0;
    const wchar_t* mode = L"";
    unsigned remaining = 0;
    unsigned approachNumber = 0, approachCount = 0;
};
CompassReading ReadCompass(const Sample& s, ULONGLONG now) {
    CompassReading r;
    if (!s.valid || !s.time || now-s.time > 1000 || !s.zoneTime ||
        now-s.zoneTime > 1000 || !s.zone[0] ||
        !std::isfinite(s.x) || !std::isfinite(s.z)) return r;
    double targetX, targetZ;
    if (!strcmp(s.zoneTag, "tagUGBurialCave")) {
        bool fresh = s.target.time && now-s.target.time <= 1000;
        if (fresh && s.target.state == TargetState::Dead) {
            r.destination = L"Kyzogg - dead (observed)";
            r.status = L"No living target detected";
            return r;
        }
        r.destination = fresh && s.target.state == TargetState::NotDetected ?
            L"Kyzogg not detected - approximate point" : L"Live scan unavailable - approximate point";
        targetX = 1286.50; targetZ = -449.29;
        if (fresh && s.target.state == TargetState::Live &&
            std::isfinite(s.target.x) && std::isfinite(s.target.z)) {
            r.destination = L"Kyzogg - live position";
            targetX = s.target.x; targetZ = s.target.z;
        }
    } else if (!strcmp(s.zoneTag, "tagMapBurialHill")) {
        // User prefers the north approach. This is the other extracted entrance;
        // its north label is inferred until a live proximity check confirms it.
        r.destination = L"North cave entrance (approximate)";
        targetX = -135.13458251953125; targetZ = -330.0594177246094;
    } else {
        r.status = L"Go to Burial Hill to begin this route";
        return r; // Never display a straight-line bearing across zones.
    }
    r.valid = true;
    r.dx = targetX-s.x; r.dz = targetZ-s.z;
    r.distance = std::hypot(r.dx, r.dz);
    r.bearingValid = s.cameraValid && std::isfinite(s.cameraYaw);
    if (r.bearingValid) {
        // GD Access's measured screen axes: forward=(-sin yaw,-cos yaw),
        // right=(cos yaw,-sin yaw). Use yaw only; never modify the camera.
        r.screenRight = r.dx*std::cos(s.cameraYaw)-r.dz*std::sin(s.cameraYaw);
        r.screenUp = -r.dx*std::sin(s.cameraYaw)-r.dz*std::cos(s.cameraYaw);
    }
    r.nearby = r.distance <= 5.0; // Proximity only; never quest completion.
    r.status = r.nearby ? L"Nearby" : L"Straight-line distance";
    return r;
}
int selectedTarget = -1;
int selectedWaypoint = -1, waypointTarget = -1;
char waypointZone[256]{};
bool waypointArrivalArmed = true, waitingWaypointArea = false;
unsigned targetQuest = 0;
bool manualTarget = false;
unsigned recordedQuest = 0;
std::string recordedTarget, recordedLocation;
// Overlay selection is a preference, never evidence of quest completion.
// Keep it apart from the editable guide so recorder undo/reload cannot erase it.
unsigned savedQuest = 0;
std::string savedTarget;
bool selectionStorageReady = false;
void SaveSelection(unsigned quest, const std::string& target) {
    if (!selectionStorageReady || (savedQuest == quest && savedTarget == target)) return;
    guides::Value state = guides::Value::dict();
    state["schema_version"] = 1u; state["quest_uid"] = quest; state["target_id"] = target;
    auto path = guides::guideDirectory + L"/active-selection.json", temp = path + L".tmp";
    auto bytes = guidejson::dump(state) + "\n";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { Log("Active selection save failed: cannot create temporary file."); return; }
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) { DeleteFileW(temp.c_str()); Log("Active selection save failed; previous choice retained on disk."); return; }
    savedQuest = quest; savedTarget = target;
}
void LoadSelection() {
    auto path = guides::guideDirectory + L"/active-selection.json";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) try {
        auto state = guides::readFile(path);
        if (state.at("schema_version").uid() != 1) throw std::runtime_error("Unsupported selection schema");
        savedQuest = state.at("quest_uid").uid();
        savedTarget = state.at("target_id").str();
        if (savedTarget.size() > 95) throw std::runtime_error("Invalid saved target ID");
        Log("Active selection loaded; awaiting live quest validation.");
    } catch (const std::exception& e) {
        savedQuest = 0; savedTarget.clear();
        std::string message = std::string("Active selection ignored: ") + e.what(); Log(message.c_str());
    }
    selectionStorageReady = true;
}
void ReconcileGuide(const std::shared_ptr<const guides::Guide>& next) {
    static std::shared_ptr<const guides::Guide> previous;
    if (previous == next) return;
    std::string targetId, candidateId;
    if (previous && selectedTarget >= 0 && static_cast<size_t>(selectedTarget) < previous->targets.size()) targetId = previous->targets[selectedTarget].id;
    if (previous && selectedWaypoint >= 0 && static_cast<size_t>(selectedWaypoint) < previous->candidates.size()) candidateId = previous->candidates[selectedWaypoint].id;
    selectedTarget = selectedWaypoint = waypointTarget = -1;
    for (size_t i=0; i<next->targets.size(); ++i) if (targetId == next->targets[i].id) selectedTarget = static_cast<int>(i);
    for (size_t i=0; i<next->candidates.size(); ++i) if (candidateId == next->candidates[i].id && static_cast<int>(next->candidates[i].target) == selectedTarget) selectedWaypoint = static_cast<int>(i);
    if (selectedTarget >= 0) waypointTarget = selectedTarget; else manualTarget = false;
    if(selectedWaypoint<0) {waitingWaypointArea=false;waypointArrivalArmed=true;}
    previous = next;
}
std::vector<int> PendingTargets(const QuestRow& quest, const guides::Guide& guide = *std::atomic_load(&guides::published)) {
    std::vector<int> result;
    if (!quest.detailsValid || !quest.details) return result;
    for (size_t i = 0; i < guide.targets.size(); ++i) {
        const auto& definition = guide.targets[i];
        if (definition.questUid != quest.id || !definition.objectiveUid) continue;
        // Reviewed prerequisites use quest progress, never proximity or list order.
        // Missing/unknown objective state must not unlock a later destination.
        bool ready = true;
        for (const auto& required : definition.requiresCompleted) {
            bool complete = false;
            for (const auto& row : *quest.details)
                if (row.kind == DetailKind::Objective && row.taskUid == required.taskUid &&
                    row.objectiveUid == required.objectiveUid &&
                    (row.taskState == 3 || (row.taskState == 2 && row.objectiveState == 1))) complete = true;
            if (!complete) { ready = false; break; }
        }
        if (!ready) continue;
        for (const auto& row : *quest.details) {
            if (row.kind == DetailKind::Objective && row.taskUid == definition.taskUid &&
                row.objectiveUid == definition.objectiveUid && row.taskState == 2 && row.objectiveState == 2) {
                result.push_back(static_cast<int>(i)); break;
            }
        }
    }
    return result;
}
bool LiveTarget(const TargetSample& target, ULONGLONG now) {
    return target.time && now-target.time <= 1000 && target.state == TargetState::Live &&
        std::isfinite(target.x) && std::isfinite(target.z);
}
std::vector<int> EligibleWaypoints(int target, const char* zone, const guides::Guide& guide) {
    std::vector<int> result;
    for (size_t i = 0; i < guide.candidates.size(); ++i)
        if (guide.candidates[i].target == static_cast<unsigned>(target) &&
            guides::shares(guide, zone, guide.candidates[i].zone)) result.push_back(static_cast<int>(i));
    return result;
}
int ChooseWaypoint(const Sample& s, int target, bool cycle = false, bool advance = true) {
    const auto& guide = *s.guide;
    auto eligible = EligibleWaypoints(target, s.zoneTag, guide);
    std::vector<int> sequence;
    for(size_t i=0;i<guide.candidates.size();++i)
        if(guide.candidates[i].target==static_cast<unsigned>(target)) sequence.push_back(static_cast<int>(i));
    if(waypointTarget!=target) waitingWaypointArea=false;
    bool sameArea = guides::shares(guide, s.zoneTag, waypointZone);
    bool reset = waypointTarget != target || (!sameArea && !waitingWaypointArea);
    if(waitingWaypointArea && waypointTarget==target && !cycle) {
        if(std::find(eligible.begin(),eligible.end(),selectedWaypoint)==eligible.end()) return -1;
        waitingWaypointArea=false;
    }
    waypointTarget = target; strcpy_s(waypointZone, s.zoneTag);
    size_t current = 0;
    while (current < eligible.size() && eligible[current] != selectedWaypoint) ++current;
    if (eligible.empty()) { selectedWaypoint = -1; waitingWaypointArea=false; return -1; }
    if (reset || current == eligible.size()) {
        waypointArrivalArmed=true;
        double nearest = 1e30;
        for (size_t i = 0; i < eligible.size(); ++i) {
            const auto& candidate = guide.candidates[eligible[i]];
            double distance = std::hypot(candidate.x-s.x, candidate.z-s.z);
            if (distance < nearest) { nearest = distance; selectedWaypoint = eligible[i]; current = i; }
        }
    }
    if (cycle) { selectedWaypoint = eligible[(current+1)%eligible.size()]; waypointArrivalArmed=false; waitingWaypointArea=false; return selectedWaypoint; }
    double distance=std::hypot(guide.candidates[selectedWaypoint].x-s.x,guide.candidates[selectedWaypoint].z-s.z);
    if(distance>5) waypointArrivalArmed=true;
    else if(advance && waypointArrivalArmed) {
        auto step=std::find(sequence.begin(),sequence.end(),selectedWaypoint);
        if(step!=sequence.end() && step+1!=sequence.end()) {
            selectedWaypoint=*(step+1);waypointArrivalArmed=false;
            if(std::find(eligible.begin(),eligible.end(),selectedWaypoint)==eligible.end()) {waitingWaypointArea=true;return -1;}
        }
    }
    return selectedWaypoint;
}
int ChooseTarget(const Sample& s, const QuestRow& quest, ULONGLONG now, bool cycle = false) {
    const auto& guide = *s.guide;
    ReconcileGuide(s.guide);
    if (cycle) { recordedTarget.clear(); recordedLocation.clear(); }
    if (targetQuest != quest.id) {
        targetQuest = quest.id; selectedTarget = -1; manualTarget = false; waypointTarget = -1;
        if (savedQuest == quest.id) for (size_t i = 0; i < guide.targets.size(); ++i)
            if (savedTarget == guide.targets[i].id && guide.targets[i].questUid == quest.id) {
                selectedTarget = static_cast<int>(i); manualTarget = true; break;
            }
    }
    auto pending = PendingTargets(quest, guide);
    if (pending.empty()) {
        if (recordedQuest == quest.id && s.guide == std::atomic_load(&guides::published)) {
            recordedTarget.clear(); recordedLocation.clear();
        }
        selectedTarget = -1; manualTarget = false; waypointTarget = -1; return -1;
    }
    size_t current = 0;
    while (current < pending.size() && pending[current] != selectedTarget) ++current;
    if (current == pending.size()) { selectedTarget = pending[0]; manualTarget = false; current = 0; }
    // The recorder publishes before the next game sample receives the new guide.
    // Apply its choice by stable IDs once that sample contains the saved binding.
    if (!recordedLocation.empty()) {
        if (recordedQuest != quest.id) { recordedTarget.clear(); recordedLocation.clear(); }
        else for (int i : pending) if (recordedTarget == guide.targets[i].id) {
            for (size_t j=0; j<guide.candidates.size(); ++j) {
                const auto& candidate=guide.candidates[j];
                if (candidate.target != static_cast<unsigned>(i) || recordedLocation != candidate.locationId) continue;
                if (guides::shares(guide,s.zoneTag,candidate.zone)) {
                    selectedTarget=i; selectedWaypoint=static_cast<int>(j);
                    waypointTarget=i; strcpy_s(waypointZone,s.zoneTag); manualTarget=true;
                }
                recordedTarget.clear(); recordedLocation.clear();
                break;
            }
            break;
        }
    }
    if (cycle) {
        // Cycle alternate approaches before advancing to another objective.
        if (!LiveTarget(s.targets[selectedTarget], now) && EligibleWaypoints(selectedTarget, s.zoneTag, guide).size() > 1) {
            int prior = ChooseWaypoint(s, selectedTarget, false, false);
            auto eligible = EligibleWaypoints(selectedTarget, s.zoneTag, guide);
            ChooseWaypoint(s, selectedTarget, true);
            if (pending.size() == 1 || prior != eligible.back()) { manualTarget = true; return selectedTarget; }
        }
        selectedTarget = pending[(current+1)%pending.size()]; manualTarget = true;
    }
    // Keep a manual choice, or an already detected target. Otherwise prefer a
    // nearby living unfinished target; never infer completion from a corpse.
    if (!manualTarget && !LiveTarget(s.targets[selectedTarget], now)) {
        double nearest = 1e30;
        for (int i : pending) if (LiveTarget(s.targets[i], now)) {
            double distance = std::hypot(s.targets[i].x-s.x, s.targets[i].z-s.z);
            if (distance < nearest) { nearest = distance; selectedTarget = i; }
        }
    }
    if (hasActiveQuest && activeQuest.id == quest.id && selectedTarget >= 0)
        SaveSelection(quest.id, guide.targets[selectedTarget].id);
    return selectedTarget;
}
CompassReading ReadQuestCompass(const Sample& s, const QuestRow* quest, ULONGLONG now) {
    const auto& guide = *s.guide;
    CompassReading r;
    r.destination = L"Quest destination";
    if (s.targets.size() != guide.targets.size()) { r.status = L"Waiting for guide refresh"; return r; }
    if (!s.valid || !s.time || now-s.time > 1000 || !s.zoneTime || now-s.zoneTime > 1000 || !s.zone[0] ||
        !std::isfinite(s.x) || !std::isfinite(s.z)) return r;
    if (!quest) { r.status = L"Select a tracked quest"; return r; }
    if (!s.quests.valid || !s.quests.time || now-s.quests.time > 2000 || !quest->detailsValid) {
        r.status = L"Waiting for live quest details"; return r;
    }
    r.remaining = static_cast<unsigned>(PendingTargets(*quest, guide).size());
    int index = ChooseTarget(s, *quest, now);
    if (index < 0) { r.status = L"No mapped unfinished objective"; return r; }
    const auto& definition = guide.targets[index];
    const auto& target = s.targets[index];
    int waypoint = ChooseWaypoint(s, index, false, !LiveTarget(target, now));
    r.destination = definition.name;
    double x = 0, z = 0;
    if (LiveTarget(target, now)) {
        x = target.x; z = target.z; r.mode = L"Live position";
    } else if (target.time && now-target.time <= 1000 && target.state == TargetState::Dead) {
        r.mode = L"Dead (observed)"; r.status = L"Waiting for quest progress"; return r;
    } else if (waypoint >= 0) {
        const auto& candidate = guide.candidates[waypoint];
        x = candidate.x; z = candidate.z; r.destination = candidate.name;
        r.mode = candidate.mode;
        const auto alternatives = EligibleWaypoints(index, s.zoneTag, guide);
        r.approachCount = static_cast<unsigned>(alternatives.size());
        for (size_t i = 0; i < alternatives.size(); ++i)
            if (alternatives[i] == waypoint) r.approachNumber = static_cast<unsigned>(i+1);
    } else {
        if(waitingWaypointArea && selectedWaypoint>=0 && static_cast<size_t>(selectedWaypoint)<guide.candidates.size()) {
            r.destination=guide.candidates[selectedWaypoint].name;
            r.mode=L"Next waypoint";r.status=L"Enter next waypoint's area";return r;
        }
        r.mode = target.time && now-target.time <= 1000 && target.state == TargetState::NotDetected ?
            L"Not detected" : L"Live scan unavailable";
        r.status = definition.searchHint; return r;
    }
    r.valid = true; r.dx = x-s.x; r.dz = z-s.z; r.distance = std::hypot(r.dx, r.dz);
    r.bearingValid = s.cameraValid && std::isfinite(s.cameraYaw);
    if (r.bearingValid) {
        r.screenRight = r.dx*std::cos(s.cameraYaw)-r.dz*std::sin(s.cameraYaw);
        r.screenUp = -r.dx*std::sin(s.cameraYaw)-r.dz*std::cos(s.cameraYaw);
    }
    r.nearby = r.distance <= 5; r.status = r.nearby ? L"Nearby" : L"Straight-line distance";
    return r;
}
CompassReading ReadSecretCompass(const Sample& s, ULONGLONG now) {
    CompassReading r;
    if(!s.guide || !s.valid || !s.time || now-s.time>1000 || !s.zoneTime || now-s.zoneTime>1000 ||
       !*s.zoneTag || !std::isfinite(s.x) || !std::isfinite(s.z)) return r;
    for(const auto& secret:s.guide->secrets) {
        const auto& l=secret.location;
        if(strcmp(l.zone,s.zoneTag)) continue; // Secrets never cross zone boundaries.
        double dx=l.x-s.x,dz=l.z-s.z,distance=std::hypot(dx,dz);
        if(distance>secret.radius || (r.valid && distance>=r.distance)) continue;
        r.valid=true;r.destination=l.name;r.dx=dx;r.dz=dz;r.distance=distance;
    }
    r.bearingValid=r.valid && s.cameraValid && std::isfinite(s.cameraYaw);
    if(r.bearingValid) {
        r.screenRight=r.dx*std::cos(s.cameraYaw)-r.dz*std::sin(s.cameraYaw);
        r.screenUp=-r.dx*std::sin(s.cameraYaw)-r.dz*std::cos(s.cameraYaw);
    }
    return r;
}
CompassReading ReadShrineCompass(const Sample& s, ULONGLONG now) {
    CompassReading r;
    if(!s.valid || !s.time || now-s.time>1000 || !LiveTarget(s.shrine,now) ||
       !std::isfinite(s.x) || !std::isfinite(s.z)) return r;
    r.valid=true;r.destination=L"Devotion shrine";
    r.dx=s.shrine.x-s.x;r.dz=s.shrine.z-s.z;r.distance=std::hypot(r.dx,r.dz);
    r.bearingValid=s.cameraValid && std::isfinite(s.cameraYaw);
    if(r.bearingValid) {
        r.screenRight=r.dx*std::cos(s.cameraYaw)-r.dz*std::sin(s.cameraYaw);
        r.screenUp=-r.dx*std::sin(s.cameraYaw)-r.dz*std::cos(s.cameraYaw);
    }
    return r;
}
#include "orbit_indicator.h"
void DrawCompass(HDC dc, const CompassReading& r) {
    SetTextColor(dc, RGB(121, 218, 191));
    RECT heading{16, 107, 444, 130};
    wchar_t headingText[120];
    swprintf_s(headingText, L"DESTINATION | %u unfinished mapped", r.remaining);
    DrawTextW(dc, headingText, -1, &heading, DT_LEFT);
    SetTextColor(dc, RGB(255, 207, 92));
    RECT name{16, 131, 444, 154};
    DrawTextW(dc, r.destination, -1, &name, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t text[200];
    // Guide hints can exceed this buffer; truncate rather than abort the game.
    if (r.valid) _snwprintf_s(text, _TRUNCATE, L"%s | %.1f units", r.status, r.distance);
    else wcsncpy_s(text, r.status, _TRUNCATE);
    RECT status{16, 157, 370, 180};
    DrawTextW(dc, text, -1, &status, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SetTextColor(dc, RGB(163, 176, 190));
    RECT note{16, 182, 370, 217};
    const wchar_t* cameraNote = r.valid && !r.bearingValid ? L"Camera unavailable: bearing hidden" : L"";
    if (r.approachCount > 1)
        _snwprintf_s(text, _TRUNCATE, L"Approach %u of %u | %s\n%s", r.approachNumber, r.approachCount, r.mode, cameraNote);
    else
        _snwprintf_s(text, _TRUNCATE, L"%s\n%s", r.mode, cameraNote);
    DrawTextW(dc, text, -1, &note, DT_LEFT);
}
void ResetTaskFocus() {
    detailPage = 0; detailAnchor = 0; taskFocusInitialized = false; focusedTasks.clear();
}
bool IsSelectedObjective(const guides::Guide& guide, const QuestRow& quest, const DetailRow& row) {
    if (selectedTarget < 0 || static_cast<size_t>(selectedTarget) >= guide.targets.size() ||
        row.kind != DetailKind::Objective || row.taskState != 2 || row.objectiveState != 2) return false;
    const auto& target = guide.targets[selectedTarget];
    return target.questUid == quest.id && target.taskUid == row.taskUid && target.objectiveUid == row.objectiveUid;
}
void ShowSelectedObjective(const guides::Guide& guide, const QuestRow& quest) {
    if (!quest.detailsValid || !quest.details || quest.details->empty()) return;
    for (size_t offset = 0; offset < quest.details->size(); ++offset)
        if (IsSelectedObjective(guide, quest, (*quest.details)[(detailAnchor + offset) % quest.details->size()])) {
            detailPage = static_cast<unsigned>(offset / DetailPageRows); return;
        }
}
void FollowCurrentTasks(const QuestRow& quest) {
    if (!quest.detailsValid || !quest.details) return;
    std::vector<unsigned> current;
    size_t first = 0;
    for (size_t i = 0; i < quest.details->size(); ++i) {
        const DetailRow& row = (*quest.details)[i];
        if (row.kind == DetailKind::Task && row.taskState == 2) {
            if (current.empty()) first = i;
            current.push_back(row.taskIndex);
        }
    }
    // Don't undo manual paging on every refresh. Refocus only when the active
    // task set changes (or a different quest has just been selected).
    if (!taskFocusInitialized || current != focusedTasks) {
        detailPage = 0; detailAnchor = first;
        focusedTasks = current; taskFocusInitialized = true;
        char text[160];
        sprintf_s(text, "Current task focus quest=%u state2_tasks=%zu first_detail_row=%zu", quest.id, current.size(), first);
        Log(text);
    }
    if (detailAnchor >= quest.details->size()) detailAnchor = 0;
}
void SelectQuest(const QuestRow* row) {
    if (!row) {
        if (hasActiveQuest) Log("Overlay active quest cleared.");
        hasActiveQuest = false; activeQuest = {}; selectedTarget = -1; targetQuest = 0; manualTarget = false; waypointTarget = -1; ResetTaskFocus(); return;
    }
    if (hasActiveQuest && activeQuest.id == row->id && !wcscmp(activeQuest.name, row->name)) return;
    activeQuest.id = row->id; wcscpy_s(activeQuest.name, row->name);
    hasActiveQuest = true; ResetTaskFocus(); FollowCurrentTasks(*row);
    char name[1024]{}, text[1200];
    WideCharToMultiByte(CP_UTF8, 0, row->name, -1, name, sizeof(name), nullptr, nullptr);
    sprintf_s(text, "Overlay active quest id=%u name=%s", row->id, name); Log(text);
}
int ReconcileSelection(const Sample& s) {
    // Temporary staleness (such as alt-tab) preserves the choice but suppresses
    // selection/display until fresh data returns. A missing player clears it.
    if (!s.valid) { SelectQuest(nullptr); return -1; }
    if (!QuestsLive(s)) return -1;
    if (!s.quests.count) { SelectQuest(nullptr); return -1; }
    if (hasActiveQuest) for (unsigned i = 0; i < s.quests.count; ++i) {
        if (s.quests.rows[i].id == activeQuest.id && !wcscmp(s.quests.rows[i].name, activeQuest.name)) {
            FollowCurrentTasks(s.quests.rows[i]); return static_cast<int>(i);
        }
    }
    if (!hasActiveQuest && savedQuest) for (unsigned i = 0; i < s.quests.count; ++i)
        if (s.quests.rows[i].id == savedQuest) { SelectQuest(&s.quests.rows[i]); return static_cast<int>(i); }
    SelectQuest(&s.quests.rows[0]);
    SaveSelection(s.quests.rows[0].id, "");
    return 0;
}
void CycleQuest(const Sample& s) {
    int current = ReconcileSelection(s);
    if (current >= 0) {
        const auto& next = s.quests.rows[(static_cast<unsigned>(current)+1) % s.quests.count];
        SelectQuest(&next); targetQuest = 0; selectedTarget = -1; manualTarget = false;
        SaveSelection(next.id, "");
    }
}
unsigned QuestPageFirst(unsigned count, unsigned shown, int activeIndex) {
    if (!shown || shown >= count) return 0;
    unsigned first = activeIndex >= 0 ? (static_cast<unsigned>(activeIndex)/shown)*shown : 0;
    return first+shown > count ? count-shown : first;
}
unsigned DetailPages(size_t rows) {
    return rows ? static_cast<unsigned>((rows+DetailPageRows-1)/DetailPageRows) : 1;
}
bool Matches(HMODULE dll, DWORD stamp, DWORD size) {
    if (!dll) return false;
    auto base = reinterpret_cast<unsigned char*>(dll);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    return nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->FileHeader.TimeDateStamp == stamp && nt->OptionalHeader.SizeOfImage == size;
}
bool Install() {
    HMODULE game = GetModuleHandleW(L"Game.dll"), engine = GetModuleHandleW(L"Engine.dll");
    // Only the binary pair inspected on 2026-10-10 (Steam build 25813250) is admitted.
    if (!Matches(game, 0x6AC7F64B, 0xAE6000) || !Matches(engine, 0x6AC7F5F7, 0x450000)) {
        Log("Unsupported Game.dll / Engine.dll fingerprint; no hook installed.");
        Log("Game update reminder: use gd-cli init to rebuild the extracted database from the updated game installation. Preserve personal recordings separately. Verify overlay compatibility before updating accepted fingerprints; a fresh database alone does not make the overlay compatible.");
        Log("For gd-cli init, use the installation directory of the running Grim Dawn executable.");
        MessageBoxW(nullptr,
            L"The installed game DLLs do not match the inspected version. No overlay hooks were installed.\n\n"
            L"After a Grim Dawn update, use gd-cli to rebuild its database from the updated game installation. "
            L"Keep personal recordings separate from that database.\n\n"
            L"Verify overlay compatibility before updating its version checks. Rebuilding the database alone does not establish compatibility.\n\n"
            L"See the project README for update guidance.",
            L"Grim Dawn overlay - game version changed", MB_OK | MB_ICONWARNING);
        return false;
    }
    original = reinterpret_cast<Update>(GetProcAddress(game, "?Update@GameEngine@GAME@@QEAAXH@Z"));
    mainPlayer = reinterpret_cast<decltype(mainPlayer)>(GetProcAddress(game, "?GetMainPlayer@GameEngine@GAME@@QEBAPEAVPlayer@2@XZ"));
    getCamera = reinterpret_cast<decltype(getCamera)>(GetProcAddress(game, "?GetCamera@GameEngine@GAME@@QEAAPEAVGameCamera@2@XZ"));
    getCameraYaw = reinterpret_cast<decltype(getCameraYaw)>(GetProcAddress(engine, "?GetCameraYaw@WorldCamera@GAME@@QEBAMXZ"));
    projectWorldPoint = reinterpret_cast<decltype(projectWorldPoint)>(GetProcAddress(engine, "?Project@WorldCamera@GAME@@QEBA?AVVec2@2@AEBVWorldVec3@2@AEBVViewport@2@@Z"));
    makeViewport = reinterpret_cast<decltype(makeViewport)>(GetProcAddress(engine, "??0Viewport@GAME@@QEAA@HHHH@Z"));
    Log(projectWorldPoint && makeViewport ? "Player screen projection exports resolved." : "Player screen projection unavailable; orbit triangle disabled.");
    entitiesInSphere = reinterpret_cast<decltype(entitiesInSphere)>(GetProcAddress(engine, "?GetEntitiesInSphere@Region@GAME@@QEAAXAEAV?$vector@PEAVEntity@GAME@@@mem@@AEBVSphere@2@_NW4EntityListType@2@@Z"));
    objectName = reinterpret_cast<decltype(objectName)>(GetProcAddress(engine, "?GetObjectName@Object@GAME@@QEBAPEBDXZ"));
    objectId = reinterpret_cast<decltype(objectId)>(GetProcAddress(engine, "?GetObjectId@Object@GAME@@QEBAIXZ"));
    entityCoords = reinterpret_cast<decltype(entityCoords)>(GetProcAddress(engine, "?GetCoords@Entity@GAME@@QEBA?AVWorldCoords@2@XZ"));
    characterAlive = reinterpret_cast<decltype(characterAlive)>(GetProcAddress(game, "?IsAlive@Character@GAME@@UEBA_NXZ"));
    objectRtti = reinterpret_cast<decltype(objectRtti)>(GetProcAddress(engine, "?GetRTTIClassInfo@Object@GAME@@UEBAAEBVRTTI_ClassInfo@2@XZ"));
    objectVtable = reinterpret_cast<void**>(GetProcAddress(engine, "??_7Object@GAME@@6B@"));
    staticShrineClassInfo = reinterpret_cast<decltype(staticShrineClassInfo)>(GetProcAddress(game, "?GetStaticClassInfo@StaticShrine@GAME@@SAAEBVRTTI_ClassInfo@2@XZ"));
    shrineCleansed = reinterpret_cast<decltype(shrineCleansed)>(GetProcAddress(game, "?IsCleansed@StaticShrine@GAME@@QEBA_NXZ"));
    objectRttiSlot=-1;
    if(objectRtti && objectVtable) for(int i=0;i<64;++i) if(objectVtable[i]==reinterpret_cast<void*>(objectRtti)) {objectRttiSlot=i;break;}
    Log(entitiesInSphere && objectName && objectId && entityCoords && characterAlive ?
        "Live Kyzogg scan exports resolved." : "Live Kyzogg scan unavailable: missing exports.");
    Log(objectRttiSlot>=0 && staticShrineClassInfo && shrineCleansed ?
        "Generic devotion shrine scan exports resolved." : "Generic devotion shrine scan unavailable: missing RTTI or shrine exports.");
    Log(getCamera && getCameraYaw ? "Camera direction exports resolved." : "Camera direction unavailable; bearing hidden.");
    footCoords = reinterpret_cast<decltype(footCoords)>(GetProcAddress(game, "?GetFootCoords@Character@GAME@@MEAA?AVWorldCoords@2@_N@Z"));
    regionCoords = reinterpret_cast<decltype(regionCoords)>(GetProcAddress(engine, "?GetRegionCoords@WorldCoords@GAME@@QEBA?AVCoords@2@XZ"));
    entityRegion = reinterpret_cast<decltype(entityRegion)>(GetProcAddress(engine, "?GetRegion@Entity@GAME@@QEBAPEAVRegion@2@XZ"));
    worldVec = reinterpret_cast<decltype(worldVec)>(GetProcAddress(engine, "??0WorldVec3@GAME@@QEAA@PEAVRegion@1@AEBVVec3@1@@Z"));
    worldPosition = reinterpret_cast<decltype(worldPosition)>(GetProcAddress(engine, "?GetWorldPosition@WorldVec3@GAME@@QEBA?AVVec3@2@XZ"));
    areaNameTag = reinterpret_cast<decltype(areaNameTag)>(GetProcAddress(engine, "?GetAreaNameTag@Engine@GAME@@QEBAAEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ"));
    localizationInstance = reinterpret_cast<decltype(localizationInstance)>(GetProcAddress(engine, "?Instance@LocalizationManager@GAME@@SAAEAV12@XZ"));
    localize = reinterpret_cast<decltype(localize)>(GetProcAddress(engine, "?LocalizeWithoutParams@LocalizationManager@GAME@@QEAAPEBGPEBD@Z"));
    originalEngineUpdate = reinterpret_cast<EngineUpdate>(GetProcAddress(engine, "?Update@Engine@GAME@@QEAAXPEBVSphere@2@PEBVWorldFrustum@2@_N1@Z"));
    questRepository = reinterpret_cast<decltype(questRepository)>(GetProcAddress(game, "?Get@?$Singleton@VQuest2Repository@GAME@@@GAME@@SAPEAVQuest2Repository@2@XZ"));
    getQuests = reinterpret_cast<decltype(getQuests)>(GetProcAddress(game, "?GetQuests@Quest2Repository@GAME@@QEAAXAEAV?$vector@PEAVQuest2@GAME@@@mem@@W4Filter@12@@Z"));
    questName = reinterpret_cast<decltype(questName)>(GetProcAddress(game, "?GetName@Quest2@GAME@@QEBAAEBV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@XZ"));
    questId = reinterpret_cast<decltype(questId)>(GetProcAddress(game, "?GetId@Quest2@GAME@@QEBAIXZ"));
    questTracked = reinterpret_cast<decltype(questTracked)>(GetProcAddress(game, "?IsTracked@Quest2@GAME@@QEBA_NXZ"));
    questsAvailable = questRepository && getQuests && questName && questId && questTracked;
    taskCount = reinterpret_cast<decltype(taskCount)>(GetProcAddress(game, "?GetNumTasks@Quest2@GAME@@QEBAIXZ"));
    taskUid = reinterpret_cast<decltype(taskUid)>(GetProcAddress(game, "?GetUid@Quest2Task@GAME@@QEBAIXZ"));
    taskAt = reinterpret_cast<decltype(taskAt)>(GetProcAddress(game, "?GetTaskByIndex@Quest2@GAME@@QEBAPEAVQuest2Task@2@H@Z"));
    taskName = reinterpret_cast<decltype(taskName)>(GetProcAddress(game, "?GetName@Quest2Task@GAME@@QEBAAEBV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@XZ"));
    taskDescription = reinterpret_cast<decltype(taskDescription)>(GetProcAddress(game, "?GetDescription@Quest2Task@GAME@@QEBAAEBV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@XZ"));
    taskState = reinterpret_cast<decltype(taskState)>(GetProcAddress(game, "?GetState@Quest2Task@GAME@@QEBA?AW4State@12@XZ"));
    taskObjectives = reinterpret_cast<decltype(taskObjectives)>(GetProcAddress(game, "?GetObjectives@Quest2Task@GAME@@QEBAAEBV?$vector@PEAVQuest2Objective@GAME@@@mem@@XZ"));
    objectiveText = reinterpret_cast<decltype(objectiveText)>(GetProcAddress(game, "?GetText@Quest2Objective@GAME@@QEBAXAEAV?$basic_string@GU?$char_traits@G@std@@V?$allocator@G@2@@std@@@Z"));
    objectiveState = reinterpret_cast<decltype(objectiveState)>(GetProcAddress(game, "?IsSatisfied@Quest2Objective@GAME@@QEBA?AW4Satisfied@12@XZ"));
    objectiveUid = reinterpret_cast<decltype(objectiveUid)>(GetProcAddress(game, "?GetUid@Quest2Objective@GAME@@QEBAIXZ"));
    Log(objectiveUid ? "Objective UID export resolved." : "Objective UID missing: mapped guidance unavailable.");
    detailsAvailable = taskCount && taskUid && taskAt && taskName && taskDescription && taskState && taskObjectives && objectiveText && objectiveState;
    Log(detailsAvailable ? "Task/objective exports resolved." : "Task/objective details unavailable: missing exports.");
    Log(questsAvailable ? "Tracked-quest exports resolved." : "Quest exports missing; position and zone remain available.");
    if (!original || !mainPlayer || !footCoords || !regionCoords || !entityRegion || !worldVec || !worldPosition ||
        !areaNameTag || !localizationInstance || !localize || !originalEngineUpdate) {
        Log("Required export missing; no hook installed."); return false;
    }
    // Enlist all existing threads, so Detours can relocate any instruction
    // pointer currently in the prologue. Abort if a live thread cannot be opened.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    std::vector<HANDLE> threads;
    THREADENTRY32 entry{sizeof(entry)};
    bool ok = Thread32First(snap, &entry) != FALSE;
    if (ok) do {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        if (!t) { if (GetLastError() != ERROR_INVALID_PARAMETER) ok = false; }
        else threads.push_back(t);
    } while (Thread32Next(snap, &entry));
    CloseHandle(snap);
    if (ok) {
        ok = DetourTransactionBegin() == NO_ERROR;
        if (ok) {
            ok = DetourUpdateThread(GetCurrentThread()) == NO_ERROR;
            for (HANDLE t : threads) if (DetourUpdateThread(t) != NO_ERROR) ok = false;
            if (ok) ok = DetourAttach(reinterpret_cast<PVOID*>(&original), reinterpret_cast<PVOID>(OnUpdate)) == NO_ERROR;
            if (ok) ok = DetourAttach(reinterpret_cast<PVOID*>(&originalEngineUpdate), reinterpret_cast<PVOID>(OnEngineUpdate)) == NO_ERROR;
            if (ok) ok = DetourTransactionCommit() == NO_ERROR;
            else DetourTransactionAbort();
        }
    }
    for (HANDLE t : threads) CloseHandle(t);
    Log(ok ? "Position and zone hooks installed." : "Hook transaction failed.");
    return ok;
}
#include "guide_recorder.h"
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_HOTKEY:
        if (w == 6) { RecordActiveObjectiveHotkey(); return 0; }
        if (w == 5) {
            DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            if (pid == GetCurrentProcessId()) OpenRecorder();
            return 0;
        }
        if (w == 4) {
            DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (foregroundPid != GetCurrentProcessId()) return 0;
            Sample s = Snapshot(); int index = ReconcileSelection(s);
            if (index >= 0) {
                ChooseTarget(s, s.quests.rows[index], GetTickCount64(), true);
                ShowSelectedObjective(*s.guide, s.quests.rows[index]);
            }
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        if (w == 3) {
            DWORD foregroundPid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (foregroundPid != GetCurrentProcessId()) return 0;
            Sample s = Snapshot(); int index = ReconcileSelection(s);
            if (index >= 0) {
                const QuestRow& q = s.quests.rows[index];
                if (q.detailsValid && q.details) detailPage = (detailPage+1) % DetailPages(q.details->size());
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (w == 2) {
            DWORD foregroundPid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            if (foregroundPid != GetCurrentProcessId()) return 0;
            Sample s = Snapshot();
            CycleQuest(s);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (w != 1) return 0;
        displayMode = NextDisplayMode(displayMode);
        Log(displayMode == DisplayMode::TriangleOnly ? "Display mode: triangle only." : displayMode == DisplayMode::TriangleAndPanel ? "Display mode: triangle and panel." : "Display mode: hidden.");
        UpdateRecorderHeader();
        // Keep the window, timer, and hotkey alive while hidden. Reuse the
        // foreground check below so toggling from another app never steals focus.
        SendMessageW(hwnd, WM_TIMER, 1, 0);
        return 0;
    case WM_TIMER: {
        FlushObservedConnections();
        Sample timerSample=Snapshot();
        // A second, one-second cadence follows the same copied samples as the
        // diagnostic log. It catches seamless crossings if game updates pause
        // or the zone and position hooks arrive out of phase at the boundary.
        static connections::Detector timerConnectionDetector;
        static ULONGLONG lastConnectionSample=0;
        ULONGLONG connectionNow=GetTickCount64();
        if(connectionNow-lastConnectionSample>=1000) {
            lastConnectionSample=connectionNow;
            bool fresh=timerSample.valid && timerSample.time && connectionNow-timerSample.time<=1000 &&
                timerSample.zoneTime && connectionNow-timerSample.zoneTime<=1000;
            auto crossing=fresh?timerConnectionDetector.observe(timerSample.time,timerSample.zoneTag,timerSample.zone,
                    timerSample.x,timerSample.y,timerSample.z):timerConnectionDetector.observe(connectionNow,nullptr,nullptr,0,0,0);
            if(crossing && !guides::shares(*timerSample.guide,crossing->from.c_str(),crossing->to.c_str())) {
                AcquireSRWLockExclusive(&lock);
                if(observedConnections.size()<128 &&
                   std::none_of(observedConnections.begin(),observedConnections.end(),[&](const auto& seen){return connections::samePair(seen,*crossing);})) {
                    observedConnections.push_back(*crossing);observedConnectionsDirty=true;
                }
                ReleaseSRWLockExclusive(&lock);
            }
        }
        ReconcileSelection(timerSample);
        HWND foreground = GetForegroundWindow();
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (pid != GetCurrentProcessId() || foreground == hwnd || foreground == orbitWindow || foreground == recorderWindow || IsIconic(foreground)) {
            ShowWindow(hwnd, SW_HIDE); if (orbitWindow) ShowWindow(orbitWindow, SW_HIDE);
            if(captureNoticeWindow) ShowWindow(captureNoticeWindow,SW_HIDE);
            return 0;
        }
        RECT client{}; POINT origin{};
        if (GetClientRect(foreground, &client) && ClientToScreen(foreground, &origin)) {
            if(captureNoticeWindow && captureNoticeStart) {
                ULONGLONG elapsed=GetTickCount64()-captureNoticeStart;
                BYTE alpha=CaptureNoticeAlpha(elapsed);
                if(alpha) {
                    SetLayeredWindowAttributes(captureNoticeWindow,0,alpha,LWA_ALPHA);
                    SetWindowPos(captureNoticeWindow,HWND_TOPMOST,
                        origin.x+(client.right-CaptureNoticeWidth)/2,
                        origin.y+(client.bottom-CaptureNoticeHeight)/2,
                        CaptureNoticeWidth,CaptureNoticeHeight,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                } else {captureNoticeStart=0;ShowWindow(captureNoticeWindow,SW_HIDE);}
            }
            Sample s = Snapshot();
            unsigned rows = s.quests.valid && s.quests.count ? s.quests.count : 1;
            if (rows > VisibleQuestRows) rows = VisibleQuestRows;
            int height = PanelBaseHeight + static_cast<int>(rows) * 24;
            int maxHeight = client.bottom - 114;
            if (height > maxHeight && maxHeight >= PanelMinimumHeight) height = maxHeight;
            if (ShowPanel(displayMode)) SetWindowPos(hwnd, HWND_TOPMOST, origin.x + 24, origin.y + 90, 460, height,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
            else ShowWindow(hwnd, SW_HIDE);
            int active = ReconcileSelection(s);
            auto bearing = ReadQuestCompass(s, active >= 0 ? &s.quests.rows[active] : nullptr, GetTickCount64());
            orbitGeometry = OrbitFor(s, bearing, client.right, client.bottom, GetTickCount64());
            secretGeometry = OrbitFor(s, ReadSecretCompass(s,GetTickCount64()), client.right, client.bottom, GetTickCount64(), -30.0);
            shrineGeometry = OrbitFor(s, ReadShrineCompass(s,GetTickCount64()), client.right, client.bottom, GetTickCount64(), 22.0);
            if(!s.guide->questArrow) orbitGeometry.visible=false;
            if(!s.guide->shrineArrow) shrineGeometry.visible=false;
            if(!s.guide->secretArrow) secretGeometry.visible=false;
            const auto& anchor=OrbitAnchor(orbitGeometry,secretGeometry,shrineGeometry);
            if (orbitWindow && ShowTriangle(displayMode) && anchor.visible) {
                SetWindowPos(orbitWindow, HWND_TOPMOST, origin.x+anchor.left, origin.y+anchor.top, OrbitWindowSize, OrbitWindowSize, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                InvalidateRect(orbitWindow, nullptr, FALSE);
            } else if (orbitWindow) ShowWindow(orbitWindow, SW_HIDE);
        } else {
            ShowWindow(hwnd, SW_HIDE);
            if (orbitWindow) ShowWindow(orbitWindow, SW_HIDE);
            if (captureNoticeWindow) ShowWindow(captureNoticeWindow, SW_HIDE);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        // A low-frequency diagnostic record permits validation without reading
        // game memory from another process. Only position and freshness are logged.
        static ULONGLONG lastLog = 0;
        if (GetTickCount64() - lastLog >= 1000) {
            lastLog = GetTickCount64();
            Sample s = Snapshot(); char text[1600], zoneUtf8[1024]{};
            WideCharToMultiByte(CP_UTF8, 0, s.zone, -1, zoneUtf8, sizeof(zoneUtf8), nullptr, nullptr);
            sprintf_s(text, "position valid=%d age=%llu x=%.3f y=%.3f z=%.3f zoneAge=%llu tag=%s zone=%s", s.valid, s.time ? lastLog-s.time : 0, s.x, s.y, s.z, s.zoneTime ? lastLog-s.zoneTime : 0, s.zoneTag, zoneUtf8);
            Log(text);
            sprintf_s(text, "player screen valid=%d x=%.1f y=%.1f viewport=%dx%d displayMode=%d",
                s.screenValid, s.screenX, s.screenY, s.viewportWidth, s.viewportHeight, static_cast<int>(displayMode)); Log(text);
            int compassQuest = ReconcileSelection(s);
            CompassReading compass = ReadQuestCompass(s, compassQuest >= 0 ? &s.quests.rows[compassQuest] : nullptr, lastLog);
            sprintf_s(text, "target state=%d id=%u age=%llu x=%.3f y=%.3f z=%.3f",
                static_cast<int>(s.target.state), s.target.id, s.target.time ? lastLog-s.target.time : 0,
                s.target.x, s.target.y, s.target.z); Log(text);
            sprintf_s(text, "shrine state=%d id=%u age=%llu x=%.3f y=%.3f z=%.3f",
                static_cast<int>(s.shrine.state), s.shrine.id, s.shrine.time ? lastLog-s.shrine.time : 0,
                s.shrine.x, s.shrine.y, s.shrine.z); Log(text);
            for (size_t ti = 0; ti < s.guide->targets.size(); ++ti) {
                const auto& target = s.targets[ti];
                sprintf_s(text, "mapped target=%zu objective=%u state=%d id=%u age=%llu x=%.3f y=%.3f z=%.3f selected=%d",
                    ti, s.guide->targets[ti].objectiveUid, static_cast<int>(target.state), target.id,
                    target.time ? lastLog-target.time : 0, target.x, target.y, target.z, selectedTarget == static_cast<int>(ti)); Log(text);
            }
            sprintf_s(text, "compass valid=%d nearby=%d dx=%.2f dz=%.2f distance=%.2f camera=%d yaw=%.4f right=%.2f up=%.2f zone=%s",
                compass.valid, compass.nearby, compass.dx, compass.dz, compass.distance,
                compass.bearingValid, s.cameraYaw, compass.screenRight, compass.screenUp, s.zoneTag);
            Log(text);
            static QuestSnapshot previous{};
            const QuestSnapshot& q = s.quests;
            bool changed = q.valid != previous.valid || q.count != previous.count || q.total != previous.total;
            if (!changed) for (unsigned i = 0; i < q.count; ++i) {
                if (q.rows[i].id != previous.rows[i].id || wcscmp(q.rows[i].name, previous.rows[i].name)) { changed = true; break; }
            }
            if (changed) {
                sprintf_s(text, "tracked quests valid=%d count=%u total=%u", q.valid, q.count, q.total); Log(text);
                if (q.valid) for (unsigned i = 0; i < q.count; ++i) {
                    char name[1024]{};
                    WideCharToMultiByte(CP_UTF8, 0, q.rows[i].name, -1, name, sizeof(name), nullptr, nullptr);
                    sprintf_s(text, "quest id=%u name=%s", q.rows[i].id, name); Log(text);
                }
                previous = q;
            }
            static unsigned loggedQuest = 0;
            static bool loggedValid = false;
            static std::shared_ptr<std::vector<DetailRow>> loggedDetails;
            int index = ReconcileSelection(s);
            if (index >= 0) {
                const QuestRow& selected = q.rows[index];
                bool detailChanged = loggedQuest != selected.id || loggedValid != selected.detailsValid ||
                    !loggedDetails || !selected.details || loggedDetails->size() != selected.details->size();
                if (!detailChanged) for (size_t i = 0; i < selected.details->size(); ++i) {
                    if (wcscmp((*loggedDetails)[i].text, (*selected.details)[i].text)) { detailChanged = true; break; }
                }
                if (detailChanged) {
                    sprintf_s(text, "task details quest=%u valid=%d rows=%zu", selected.id, selected.detailsValid,
                        selected.details ? selected.details->size() : 0); Log(text);
                    if (selected.detailsValid && selected.details) for (const DetailRow& row : *selected.details) {
                        if (row.kind == DetailKind::Task) {
                            sprintf_s(text, "task identity quest=%u index=%u uid=%u", selected.id, row.taskIndex, row.taskUid); Log(text);
                        }
                        if (row.kind == DetailKind::Objective) {
                            sprintf_s(text, "objective identity quest=%u task=%u uid=%u state=%d", selected.id, row.taskUid, row.objectiveUid, row.objectiveState); Log(text);
                        }
                        char utf8[4096]{};
                        WideCharToMultiByte(CP_UTF8, 0, row.text, -1, utf8, sizeof(utf8), nullptr, nullptr); Log(utf8);
                    }
                    loggedQuest = selected.id; loggedValid = selected.detailsValid; loggedDetails = selected.details;
                }
            } else { loggedQuest = 0; loggedValid = false; loggedDetails.reset(); }
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1; // Background is painted with the complete panel frame.
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC paintDc = BeginPaint(hwnd, &ps);
        RECT bounds; GetClientRect(hwnd, &bounds);
        // Compose the background and all text off-screen, then present once.
        // Keep drawing directly as a fallback if a GDI allocation fails.
        HDC buffer = CreateCompatibleDC(paintDc);
        HBITMAP bitmap = bounds.right>0 && bounds.bottom>0 ? CreateCompatibleBitmap(paintDc,bounds.right,bounds.bottom) : nullptr;
        HGDIOBJ oldBitmap = buffer && bitmap ? SelectObject(buffer,bitmap) : nullptr;
        bool buffered = oldBitmap && oldBitmap!=HGDI_ERROR;
        HDC dc = buffered ? buffer : paintDc;
        HBRUSH bg = CreateSolidBrush(RGB(18, 23, 30)); FillRect(dc, &bounds, bg); DeleteObject(bg);
        HGDIOBJ old = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(121, 218, 191));
        RECT title{16, 12, 444, 38};
        bool guideNotice = !std::atomic_load(&guides::published)->diagnostics.empty() || guides::status.find("failed") != std::string::npos || guides::status.find("Not saved") != std::string::npos;
        DrawTextW(dc, guideNotice ? L"GUIDE NEEDS ATTENTION" : L"GRIM DAWN  |  QUEST COMPASS", -1, &title, DT_LEFT);
        Sample s = Snapshot(); wchar_t text[200];
        int activeIndex = ReconcileSelection(s);
        bool live = s.valid && s.time && GetTickCount64() - s.time <= 1000;
        bool zoneLive = live && s.zone[0] && s.zoneTime && GetTickCount64() - s.zoneTime <= 1000;
        SetTextColor(dc, RGB(239, 241, 244)); RECT zoneRect{16, 40, 404, 65};
        DrawTextW(dc, zoneLive ? s.zone : L"Zone unavailable", -1, &zoneRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (live) swprintf_s(text, L"Location: %.2f, %.2f, %.2f", s.x, s.y, s.z);
        else wcscpy_s(text, L"Position unavailable");
        SetTextColor(dc, RGB(239, 241, 244)); RECT body{16, 74, 444, 100}; DrawTextW(dc, text, -1, &body, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        DrawCompass(dc, ReadQuestCompass(s, activeIndex >= 0 ? &s.quests.rows[activeIndex] : nullptr, GetTickCount64()));
        SetTextColor(dc, RGB(121, 218, 191)); RECT heading{16, 229, 444, 252};
        bool questsLive = QuestsLive(s);
        if (questsLive) swprintf_s(text, L"TRACKED QUESTS (%u)", s.quests.total);
        else wcscpy_s(text, L"TRACKED QUESTS");
        DrawTextW(dc, text, -1, &heading, DT_LEFT);
        SetTextColor(dc, RGB(239, 241, 244));
        int y = 257;
        if (!questsLive || !s.quests.total) {
            RECT row{16, y, 444, y+24};
            DrawTextW(dc, questsLive ? L"No tracked quests" : L"Quest list unavailable", -1, &row, DT_LEFT);
        } else {
            unsigned capacity = bounds.bottom >= PanelMinimumHeight ? static_cast<unsigned>((bounds.bottom - PanelBaseHeight) / 24) : 1;
            if (capacity > VisibleQuestRows) capacity = VisibleQuestRows;
            unsigned shown = s.quests.count < capacity ? s.quests.count : capacity;
            // Keep the selected quest visible when the list exceeds panel height.
            unsigned first = QuestPageFirst(s.quests.count, shown, activeIndex);
            for (unsigned i = first; i < first+shown; ++i, y += 24) {
                bool active = static_cast<int>(i) == activeIndex;
                SetTextColor(dc, active ? RGB(255, 207, 92) : RGB(239, 241, 244));
                if (active) { RECT marker{16, y, 32, y+24}; DrawTextW(dc, L">", -1, &marker, DT_LEFT); }
                RECT row{34, y, 444, y+24};
                DrawTextW(dc, s.quests.rows[i].name, -1, &row, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            if (s.quests.total > shown) {
                SetTextColor(dc, RGB(163, 176, 190));
                swprintf_s(text, L"Showing %u-%u of %u", first+1, first+shown, s.quests.total);
                RECT row{16, y, 444, y+24}; DrawTextW(dc, text, -1, &row, DT_LEFT);
            }
        }
        int detailTop = bounds.bottom - PanelFooterHeight - 278;
        SetTextColor(dc, RGB(121, 218, 191)); RECT detailHeading{16, detailTop, 444, detailTop+24};
        DrawTextW(dc, L"TASKS / OBJECTIVES | > destination", -1, &detailHeading, DT_LEFT);
        SetTextColor(dc, RGB(239, 241, 244));
        const QuestRow* selected = activeIndex >= 0 ? &s.quests.rows[activeIndex] : nullptr;
        if (!selected || !selected->detailsValid || !selected->details || selected->details->empty()) {
            RECT row{16, detailTop+28, 444, detailTop+72};
            DrawTextW(dc, !selected ? L"Select a tracked quest" : !selected->detailsValid ? L"Task details unavailable" : L"No tasks reported",
                -1, &row, DT_LEFT | DT_WORDBREAK);
        } else {
            unsigned pages = DetailPages(selected->details->size());
            detailPage %= pages;
            size_t first = detailPage*DetailPageRows;
            for (unsigned i = 0; i < DetailPageRows && first+i < selected->details->size(); ++i) {
                const DetailRow& detail = (*selected->details)[(detailAnchor+first+i) % selected->details->size()];
                bool highlighted = detail.taskState == 2 && detail.kind != DetailKind::Description;
                bool destination = questsLive && IsSelectedObjective(*s.guide, *selected, detail);
                SetTextColor(dc, destination ? RGB(32, 224, 88) : highlighted ? RGB(255, 207, 92) : detail.taskState == 3 ? RGB(163, 176, 190) : RGB(239, 241, 244));
                RECT row{34, detailTop+28+static_cast<int>(i)*44, 444, detailTop+70+static_cast<int>(i)*44};
                if (destination) { RECT marker{16, row.top, 32, row.bottom}; DrawTextW(dc, L">", -1, &marker, DT_LEFT); }
                DrawTextW(dc, detail.text, -1, &row,
                    DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            swprintf_s(text, L"Details page %u of %u | raw states", detailPage+1, pages);
            SetTextColor(dc, RGB(163, 176, 190)); RECT page{16, detailTop+252, 444, detailTop+274};
            DrawTextW(dc, text, -1, &page, DT_LEFT);
        }
        SetTextColor(dc, RGB(163, 176, 190)); RECT footer{16, bounds.bottom-PanelFooterHeight, 444, bounds.bottom-12};
        DrawTextW(dc, L"Ctrl+Shift+F7   Record waypoint\nCtrl+Shift+F8   Open guide recorder\nCtrl+Shift+F9   Next approach / target\nCtrl+Shift+F10  Cycle display mode\nCtrl+Shift+F11  Next tracked quest\nCtrl+Shift+F12  Next details page", -1, &footer, DT_LEFT);
        SelectObject(dc, old);
        if(buffered) {
            BitBlt(paintDc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,oldBitmap);
        }
        if(bitmap) DeleteObject(bitmap);
        if(buffer) DeleteDC(buffer);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_DESTROY:
        if(orbitWindow) {DestroyWindow(orbitWindow);orbitWindow=nullptr;}
        if(captureNoticeWindow) {DestroyWindow(captureNoticeWindow);captureNoticeWindow=nullptr;}
        for(int id=1;id<=6;++id) UnregisterHotKey(hwnd,id);
        PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
DWORD WINAPI Run(void*) {
    // The loader holds this event only for an explicit logging launch. Signal
    // after opening the file so it can report whether logging actually started.
    wchar_t eventName[80];
    swprintf_s(eventName, L"Local\\GDQuestCompassLog-%lu", GetCurrentProcessId());
    HANDLE logRequest = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
    if (logRequest) {
        wchar_t path[MAX_PATH]; GetModuleFileNameW(module, path, MAX_PATH);
        wchar_t* slash = wcsrchr(path, L'\\');
        if (slash) {
            wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"position-overlay.log");
            logFile = _wfsopen(path, L"w", _SH_DENYNO);
        }
        if (logFile) SetEvent(logRequest);
        CloseHandle(logRequest);
    }
    Log("Position + zone prototype starting.");
    wchar_t dllPath[MAX_PATH]; GetModuleFileNameW(module, dllPath, MAX_PATH);
    std::wstring folder(dllPath); folder.resize(folder.find_last_of(L"\\/")); folder.resize(folder.find_last_of(L"\\/"));
    guides::guideDirectory = folder + L"/data/guide";
    guides::reload(); Log(guides::status.c_str());
    LoadSelection();
    LoadObservedConnections();
    if (!Install()) { if (logFile) fclose(logFile); return 1; }
    // This thread owns its window and graphics resources. Never change the
    // game's process-wide DPI awareness or graphics/input configuration.
    font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    WNDCLASSW cls{}; cls.lpfnWndProc = WindowProc; cls.hInstance = module; cls.lpszClassName = L"GDPositionPrototype";
    if (!RegisterClassW(&cls)) { Log("Window class failed."); InterlockedExchange(&enabled, 0); return 2; }
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        cls.lpszClassName, L"Grim Dawn Position", WS_POPUP, 0, 0, 420, 180, nullptr, nullptr, module, nullptr);
    if (!hwnd) { Log("Window creation failed."); InterlockedExchange(&enabled, 0); return 3; }
    SetLayeredWindowAttributes(hwnd, 0, 225, LWA_ALPHA);
    WNDCLASSW orbitClass{}; orbitClass.lpfnWndProc = OrbitProc; orbitClass.hInstance = module; orbitClass.lpszClassName = L"GDObjectiveOrbit";
    if (RegisterClassW(&orbitClass)) orbitWindow = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        orbitClass.lpszClassName, L"Grim Dawn Objective", WS_POPUP, 0, 0, OrbitWindowSize, OrbitWindowSize, nullptr, nullptr, module, nullptr);
    if (orbitWindow) SetLayeredWindowAttributes(orbitWindow, RGB(0,0,0), 255, LWA_COLORKEY);
    else Log("Orbit window creation failed; F10 still opens the panel.");
    WNDCLASSW noticeClass{};noticeClass.lpfnWndProc=CaptureNoticeProc;noticeClass.hInstance=module;
    noticeClass.lpszClassName=L"GDLocationCapturedNotice";
    if(RegisterClassW(&noticeClass)) captureNoticeWindow=CreateWindowExW(
        WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,
        noticeClass.lpszClassName,L"Location captured",WS_POPUP,0,0,CaptureNoticeWidth,CaptureNoticeHeight,
        nullptr,nullptr,module,nullptr);
    if(captureNoticeWindow) SetLayeredWindowAttributes(captureNoticeWindow,0,0,LWA_ALPHA);
    else Log("Location capture notice window unavailable.");
    if (!RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F10)) Log("Toggle hotkey unavailable; exit game to stop.");
    if (!RegisterHotKey(hwnd, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F11)) Log("Active quest hotkey unavailable.");
    if (!RegisterHotKey(hwnd, 3, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F12)) Log("Details page hotkey unavailable.");
    if (!RegisterHotKey(hwnd, 4, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F9)) Log("Target cycling hotkey unavailable.");
    if (!RegisterHotKey(hwnd, 5, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F8)) Log("Recorder hotkey unavailable.");
    if (!RegisterHotKey(hwnd, 6, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, VK_F7)) Log("Waypoint hotkey unavailable.");
    if (!SetTimer(hwnd, 1, 16, nullptr)) { Log("Window timer failed."); DestroyWindow(hwnd); }
    Log("Overlay window ready.");
    MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!recorderWindow || !IsWindowVisible(recorderWindow) || !IsDialogMessageW(recorderWindow, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    InterlockedExchange(&enabled, 0);
    DeleteObject(font);
    if (logFile) fclose(logFile);
    // Never unload a DLL with a live trampoline. The hook now only calls original.
    return 0;
}
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
        DisableThreadLibraryCalls(instance);
        HANDLE thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread); else return FALSE;
    }
    return TRUE;
}
