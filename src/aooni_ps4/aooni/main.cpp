// Ao Oni PS4 Port - Motor RPG Maker XP (RGSS1) 1:1 Optimizado con Audio PS4 (sceAudioOut)
// Soporta todos los mapas, eventos, rutas de movimiento, autotiles 48-patrones, prioridades,
// audio BGM/BGS/SE a 48kHz con pitch/volumen, puzzles (cajas fuertes cmd 103, armarios cmd 105),
// uso de objetos desde inventario (CommonEvents) y guardado/carga.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <pthread.h>
#include <unistd.h>

#include <SDL2/SDL.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <orbis/Pad.h>
#include "mini_json.h"

#define STBI_NO_SIMD
#define STBI_ASSERT(x)
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define GAME_W   640
#define GAME_H   480
#define TILE_SZ  32

// Mapeo exacto de botones de libSDL2.a (SDL_sysjoystick.c.o para DualShock 4)
#define PAD_CROSS    0
#define PAD_CIRCLE   1
#define PAD_SQUARE   2
#define PAD_TRIANGLE 3
#define PAD_L1       4
#define PAD_R1       5
#define PAD_OPTIONS  9
#define PAD_L3       11
#define PAD_R3       12
#define PAD_UP       13
#define PAD_DOWN     14
#define PAD_LEFT     15
#define PAD_RIGHT    16
#define PAD_TOUCHPAD 17
#define PAD_L2       18
#define PAD_R2       19

// ==================== SALIDA DE VIDEO PS4 / PS4 PRO 4K NATIVO (sceVideoOut) ====================
extern "C" void* SDL_GetVideoDevice(void);

struct PS4_VideoData {
    int32_t handle;                        // +0x00
    uint32_t width;                        // +0x04
    uint32_t height;                       // +0x08
    uint32_t pad0;                         // +0x0C
    OrbisKernelEqueue flipQueue;           // +0x10
    OrbisVideoOutBufferAttribute attr;     // +0x18 (0x28 bytes)
    int32_t currentBuffer;                 // +0x40
    uint32_t pad1;                         // +0x44
    void* buffers[2];                      // +0x48, +0x50
    off_t directMemOff;                    // +0x58
    void* videoMem;                        // +0x60
    uint32_t singleBufferSize;             // +0x68
    uint32_t totalAllocSize;               // +0x6C
};

enum VideoScaleMode {
    SCALE_4_3_FIT = 0,       // 4:3 Nítido Completo (ej: 2880x2160 en 4K, 1440x1080 en 1080p, 960x720 en 720p)
    SCALE_16_9_FULL = 1,     // 16:9 Pantalla Completa (3840x2160 en 4K)
    SCALE_PIXEL_PERFECT = 2  // Entero Pixel-Perfect (4x = 2560x1920 en 4K, 2x = 1280x960 en 1080p)
};

static int g_videoScaleMode = SCALE_4_3_FIT;
static int g_videoResIndex = 0; // 0 = 720p (1280x720), 1 = 1080p (1920x1080), 2 = 4K Nativo PS4 Pro (3840x2160)
static bool g_videoClearNeeded = true;
static bool g_highlightObjects = true; // QoL: brillo y pistas contextuales para objetos pasados por alto (Touchpad ON/OFF)

static inline int getFpsTarget() {
    return 60;
}

static PS4_VideoData* getPs4VideoData() {
    void* dev = SDL_GetVideoDevice();
    if (!dev) return nullptr;
    return *(PS4_VideoData**)((uint8_t*)dev + 0x530);
}

static bool setVideoOutputResolution(int targetW, int targetH) {
    PS4_VideoData* vd = getPs4VideoData();
    if (!vd || vd->handle <= 0) return false;
    if ((int)vd->width == targetW && (int)vd->height == targetH) {
        g_videoClearNeeded = true;
        return true;
    }

    uint32_t singleSz = (uint32_t)targetW * (uint32_t)targetH * 4u;
    uint32_t totalSz  = (singleSz * 2u + 0xFFFFFu) & ~0xFFFFFu; // Alineado a 1 MB

    off_t newPhys = 0;
    int32_t ret = sceKernelAllocateDirectMemory(0, 0x180000000ULL, totalSz, 0x100000, 3, &newPhys);
    if (ret < 0) {
        printf("[AoOni] No se pudo reservar DirectMemory para %dx%d (err=0x%08x)\n", targetW, targetH, ret);
        return false;
    }

    void* newMem = nullptr;
    ret = sceKernelMapDirectMemory(&newMem, totalSz, 0x33, 0, newPhys, 0x100000);
    if (ret < 0 || !newMem) {
        sceKernelReleaseDirectMemory(newPhys, totalSz);
        printf("[AoOni] No se pudo mapear DirectMemory para %dx%d (err=0x%08x)\n", targetW, targetH, ret);
        return false;
    }

    memset(newMem, 0, totalSz);
    void* newBufs[2] = { newMem, (uint8_t*)newMem + singleSz };
    OrbisVideoOutBufferAttribute newAttr;
    memset(&newAttr, 0, sizeof(newAttr));
    sceVideoOutSetBufferAttribute(&newAttr, ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,
                                  ORBIS_VIDEO_OUT_TILING_MODE_LINEAR, ORBIS_VIDEO_OUT_ASPECT_RATIO_16_9,
                                  targetW, targetH, targetW);

    sceVideoOutUnregisterBuffers(vd->handle, 0);
    ret = sceVideoOutRegisterBuffers(vd->handle, 0, newBufs, 2, &newAttr);
    if (ret < 0) {
        // Fallback seguro a los buffers anteriores si el monitor/emulador no soporta esa resolución
        sceVideoOutRegisterBuffers(vd->handle, 0, vd->buffers, 2, &vd->attr);
        sceKernelMunmap(newMem, totalSz);
        sceKernelReleaseDirectMemory(newPhys, totalSz);
        printf("[AoOni] sceVideoOutRegisterBuffers rechazo %dx%d (err=0x%08x), manteniendo %ux%u\n",
               targetW, targetH, ret, vd->width, vd->height);
        return false;
    }

    if (vd->videoMem && vd->totalAllocSize > 0) {
        sceKernelMunmap(vd->videoMem, vd->totalAllocSize);
    }
    if (vd->directMemOff != 0 && vd->totalAllocSize > 0) {
        sceKernelReleaseDirectMemory(vd->directMemOff, vd->totalAllocSize);
    }

    vd->width = (uint32_t)targetW;
    vd->height = (uint32_t)targetH;
    vd->attr = newAttr;
    vd->currentBuffer = 0;
    vd->buffers[0] = newBufs[0];
    vd->buffers[1] = newBufs[1];
    vd->directMemOff = newPhys;
    vd->videoMem = newMem;
    vd->singleBufferSize = singleSz;
    vd->totalAllocSize = totalSz;
    g_videoClearNeeded = true;
    printf("[AoOni] Modo de salida de video activo: %dx%d (%s)\n",
           targetW, targetH, (targetW >= 3840) ? "4K Nativo PS4 Pro" : ((targetW >= 1920) ? "1080p Full HD" : "720p HD"));
    return true;
}

static void cycleVideoResolution() {
    const int resTable[3][2] = { {1280, 720}, {1920, 1080}, {3840, 2160} };
    for (int attempt = 0; attempt < 3; attempt++) {
        g_videoResIndex = (g_videoResIndex + 1) % 3;
        if (setVideoOutputResolution(resTable[g_videoResIndex][0], resTable[g_videoResIndex][1]))
            return;
    }
}

static const char* getVideoResLabel() {
    PS4_VideoData* vd = getPs4VideoData();
    uint32_t w = vd ? vd->width : SCREEN_W;
    if (w >= 3840) return "4K Nativo (3840x2160 PS4 Pro)";
    if (w >= 1920) return "1080p Full HD (1920x1080)";
    return "720p HD (1280x720)";
}

static const char* getVideoScaleLabel() {
    if (g_videoScaleMode == SCALE_16_9_FULL) return "16:9 Completa";
    if (g_videoScaleMode == SCALE_PIXEL_PERFECT) return "Pixel-Perfect";
    return "4:3 Nitido";
}

// Escalador Nearest-Neighbor con caché de scanlines (escala 640x480 -> hasta 3840x2160 4K sin re-limpiar bordes cada frame)
static bool presentScaledCanvas(SDL_Window* window, SDL_Surface* gameSurf) {
    PS4_VideoData* vd = getPs4VideoData();
    if (!vd || !vd->buffers[0] || !vd->buffers[1]) {
        SDL_Surface* winSurf = SDL_GetWindowSurface(window);
        if (winSurf && gameSurf) {
            SDL_BlitScaled(gameSurf, NULL, winSurf, NULL);
            SDL_UpdateWindowSurface(window);
        }
        return false;
    }

    if (g_videoClearNeeded) {
        if (vd->buffers[0]) memset(vd->buffers[0], 0, vd->singleBufferSize);
        if (vd->buffers[1]) memset(vd->buffers[1], 0, vd->singleBufferSize);
        g_videoClearNeeded = false;
    }

    const int outW = (int)vd->width;
    const int outH = (int)vd->height;
    int dstW = outW, dstH = outH;

    if (g_videoScaleMode == SCALE_4_3_FIT) {
        dstH = outH;
        dstW = (outH * 4) / 3;
        if (dstW > outW) { dstW = outW; dstH = (outW * 3) / 4; }
    } else if (g_videoScaleMode == SCALE_PIXEL_PERFECT) {
        int factor = std::min(outW / GAME_W, outH / GAME_H);
        if (factor < 1) factor = 1;
        dstW = GAME_W * factor;
        dstH = GAME_H * factor;
    } else {
        dstW = outW;
        dstH = outH;
    }

    const int dstX = (outW - dstW) / 2;
    const int dstY = (outH - dstH) / 2;
    uint32_t* __restrict dstBase = (uint32_t*)vd->buffers[vd->currentBuffer];
    const uint32_t* __restrict srcBase = (const uint32_t*)gameSurf->pixels;
    const int srcPitch = gameSurf->pitch / 4;

    static int s_cachedDstW = 0;
    static int s_cachedDstH = 0;
    static int s_xMap[4096];
    static int s_yMap[2160];
    if (s_cachedDstW != dstW) {
        for (int x = 0; x < dstW && x < 4096; x++) {
            s_xMap[x] = (x * GAME_W) / dstW;
        }
        s_cachedDstW = dstW;
    }
    if (s_cachedDstH != dstH) {
        for (int y = 0; y < dstH && y < 2160; y++) {
            s_yMap[y] = (y * GAME_H) / dstH;
        }
        s_cachedDstH = dstH;
    }

    int prevSy = -1;
    const uint32_t* prevDstRow = nullptr;
    const size_t rowBytes = (size_t)dstW * 4u;
    const int* __restrict xmap = s_xMap;

    for (int y = 0; y < dstH; y++) {
        int sy = (y < 2160) ? s_yMap[y] : ((y * GAME_H) / dstH);
        uint32_t* __restrict dstRow = dstBase + (size_t)(dstY + y) * outW + dstX;
        if (sy == prevSy && prevDstRow) {
            memcpy(dstRow, prevDstRow, rowBytes);
        } else {
            const uint32_t* __restrict srcRow = srcBase + (size_t)sy * srcPitch;
            int x = 0;
            for (; x + 3 < dstW; x += 4) {
                dstRow[x]     = srcRow[xmap[x]];
                dstRow[x + 1] = srcRow[xmap[x + 1]];
                dstRow[x + 2] = srcRow[xmap[x + 2]];
                dstRow[x + 3] = srcRow[xmap[x + 3]];
            }
            for (; x < dstW; x++) {
                dstRow[x] = srcRow[xmap[x]];
            }
            prevSy = sy;
            prevDstRow = dstRow;
        }
    }

    sceVideoOutSubmitFlip(vd->handle, vd->currentBuffer, ORBIS_VIDEO_OUT_FLIP_VSYNC, 0);
    OrbisKernelEvent ev;
    int outEv = 0;
    sceKernelWaitEqueue(vd->flipQueue, &ev, 1, &outEv, NULL);
    vd->currentBuffer = (vd->currentBuffer + 1) & 1;
    return true;
}

// ==================== SISTEMA DE AUDIO PS4 (sceAudioOut 48kHz Stereo) ====================
struct PcmBuffer {
    std::vector<int16_t> samples; // Interleaved L, R a 48000 Hz
    size_t frames = 0;
};

struct ActiveSound {
    const PcmBuffer* buf = nullptr;
    uint64_t posFP = 0;   // Posición en frames con 16 bits fraccionarios
    uint32_t stepFP = 65536; // 65536 = 1.0x (pitch 100)
    int volume = 100;     // 0..100
    int fadeTotalFrames = 0;
    int fadeRemFrames = 0;
    bool fadeIn = false;
    bool active = false;
    bool loop = false;
    std::string name;
    int origPitch = 100;
};

static pthread_mutex_t g_audioMutex = PTHREAD_MUTEX_INITIALIZER;
static std::map<std::string, PcmBuffer*> g_pcmCache;
static ActiveSound g_bgmTrack;
static ActiveSound g_bgsTrack;
static ActiveSound g_savedBgm;
static ActiveSound g_savedBgs;
static ActiveSound g_seChannels[6];
static volatile bool g_audioRunning = true;

static std::string toLowerStr(const std::string& s) {
    std::string out = s;
    for (size_t i = 0; i < out.size(); i++)
        out[i] = (char)std::tolower((unsigned char)out[i]);
    return out;
}

static const PcmBuffer* getPcm(const std::string& prefix, const std::string& rawName) {
    if (rawName.empty()) return nullptr;
    std::string low = toLowerStr(rawName);
    // Quitar extension si la trae
    size_t dot = low.rfind('.');
    if (dot != std::string::npos) low = low.substr(0, dot);

    std::string path = "/app0/assets/audio/" + prefix + low + ".pcm";
    auto it = g_pcmCache.find(path);
    if (it != g_pcmCache.end()) return it->second;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f && prefix == "bgs_") {
        // Fallback a se_ si el BGS fue exportado como SE
        std::string alt = "/app0/assets/audio/se_" + low + ".pcm";
        f = fopen(alt.c_str(), "rb");
    }
    if (!f) {
        g_pcmCache[path] = nullptr;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        g_pcmCache[path] = nullptr;
        return nullptr;
    }
    PcmBuffer* pb = new PcmBuffer();
    pb->samples.resize(sz / 2);
    fread(pb->samples.data(), 1, pb->samples.size() * 2, f);
    fclose(f);
    pb->frames = pb->samples.size() / 2;
    g_pcmCache[path] = pb;
    return pb;
}

// ==================== VIBRACIÓN Y BARRA DE LUZ DUALSHOCK 4 (scePad) ====================
static int g_padHandle = -1;
static int g_rumbleFrames = 0;
static uint8_t g_rumbleLarge = 0;
static uint8_t g_rumbleSmall = 0;
static uint8_t g_lastLightR = 0, g_lastLightG = 0, g_lastLightB = 0;

static void initPadHandle() {
    int32_t userId = 0;
    if (sceUserServiceGetInitialUser(&userId) == 0) {
        g_padHandle = scePadGetHandle(userId, 0, 0);
    }
    if (g_padHandle < 0) {
        g_padHandle = 1; // Handle abierto por SDL_JoystickOpen(0) en PS4 / shadPS4
    }
}

static void triggerRumble(uint8_t largeMotor, uint8_t smallMotor, int frames) {
    if (g_padHandle < 0) initPadHandle();
    if (frames >= g_rumbleFrames || largeMotor >= g_rumbleLarge) {
        g_rumbleLarge = largeMotor;
        g_rumbleSmall = smallMotor;
        g_rumbleFrames = std::max(g_rumbleFrames, frames);
        OrbisPadVibeParam vp;
        vp.lgMotor = largeMotor;
        vp.smMotor = smallMotor;
        scePadSetVibration(g_padHandle, &vp);
    }
}

static void setPadLightbar(uint8_t r, uint8_t g, uint8_t b) {
    if (r == g_lastLightR && g == g_lastLightG && b == g_lastLightB) return;
    if (g_padHandle < 0) initPadHandle();
    g_lastLightR = r; g_lastLightG = g; g_lastLightB = b;
    OrbisPadColor col = { r, g, b, 255 };
    scePadSetLightBar(g_padHandle, &col);
}

static void updateRumble() {
    if (g_rumbleFrames > 0) {
        g_rumbleFrames--;
        if (g_rumbleFrames == 0 && g_padHandle >= 0) {
            OrbisPadVibeParam vp = { 0, 0 };
            scePadSetVibration(g_padHandle, &vp);
            g_rumbleLarge = 0;
            g_rumbleSmall = 0;
        }
    }
}

static void playBgm(const std::string& name, int volume = 100, int pitch = 100, int fadeInSeconds = 0) {
    if (name == "horor-b") {
        // Pulso de susto al iniciar la música de persecución de Ao Oni
        triggerRumble(210, 180, 24);
    }
    pthread_mutex_lock(&g_audioMutex);
    if (name.empty()) {
        g_bgmTrack.active = false;
        g_bgmTrack.name.clear();
        pthread_mutex_unlock(&g_audioMutex);
        return;
    }
    if (g_bgmTrack.active && g_bgmTrack.name == name && g_bgmTrack.fadeTotalFrames == 0) {
        g_bgmTrack.volume = std::max(0, std::min(100, volume));
        g_bgmTrack.origPitch = pitch;
        g_bgmTrack.stepFP = (uint32_t)((std::max(25, std::min(250, pitch)) * 65536ULL) / 100ULL);
        pthread_mutex_unlock(&g_audioMutex);
        return;
    }
    const PcmBuffer* pb = getPcm("bgm_", name);
    if (!pb) pb = getPcm("se_", name);
    if (pb && pb->frames > 0) {
        g_bgmTrack.buf = pb;
        g_bgmTrack.posFP = 0;
        g_bgmTrack.origPitch = pitch;
        g_bgmTrack.stepFP = (uint32_t)((std::max(25, std::min(250, pitch)) * 65536ULL) / 100ULL);
        g_bgmTrack.volume = std::max(0, std::min(100, volume));
        int f = (fadeInSeconds > 0) ? (fadeInSeconds * 48000) : 0;
        g_bgmTrack.fadeTotalFrames = f;
        g_bgmTrack.fadeRemFrames = f;
        g_bgmTrack.fadeIn = (f > 0);
        g_bgmTrack.loop = true;
        g_bgmTrack.name = name;
        g_bgmTrack.active = true;
    } else {
        g_bgmTrack.active = false;
    }
    pthread_mutex_unlock(&g_audioMutex);
}

static void fadeOutBgm(int seconds) {
    pthread_mutex_lock(&g_audioMutex);
    if (g_bgmTrack.active) {
        int f = std::max(1, seconds) * 48000;
        g_bgmTrack.fadeTotalFrames = f;
        g_bgmTrack.fadeRemFrames = f;
        g_bgmTrack.fadeIn = false;
    }
    pthread_mutex_unlock(&g_audioMutex);
}

static void playBgs(const std::string& name, int volume = 100, int pitch = 100, int fadeInSeconds = 0) {
    pthread_mutex_lock(&g_audioMutex);
    if (name.empty()) {
        g_bgsTrack.active = false;
        g_bgsTrack.name.clear();
        g_bgsTrack.fadeTotalFrames = 0;
        g_bgsTrack.fadeRemFrames = 0;
        pthread_mutex_unlock(&g_audioMutex);
        return;
    }
    if (g_bgsTrack.active && g_bgsTrack.name == name && g_bgsTrack.fadeTotalFrames == 0 && fadeInSeconds <= 0) {
        g_bgsTrack.volume = std::max(0, std::min(100, volume));
        g_bgsTrack.origPitch = pitch;
        g_bgsTrack.stepFP = (uint32_t)((std::max(25, std::min(250, pitch)) * 65536ULL) / 100ULL);
        pthread_mutex_unlock(&g_audioMutex);
        return;
    }
    const PcmBuffer* pb = getPcm("bgs_", name);
    if (!pb) pb = getPcm("se_", name);
    if (pb && pb->frames > 0) {
        g_bgsTrack.buf = pb;
        g_bgsTrack.posFP = 0;
        g_bgsTrack.origPitch = pitch;
        g_bgsTrack.stepFP = (uint32_t)((std::max(25, std::min(250, pitch)) * 65536ULL) / 100ULL);
        g_bgsTrack.volume = std::max(0, std::min(100, volume));
        int f = (fadeInSeconds > 0) ? (fadeInSeconds * 48000) : 0;
        g_bgsTrack.fadeTotalFrames = f;
        g_bgsTrack.fadeRemFrames = f;
        g_bgsTrack.fadeIn = (f > 0);
        g_bgsTrack.loop = true;
        g_bgsTrack.name = name;
        g_bgsTrack.active = true;
    } else {
        g_bgsTrack.active = false;
        g_bgsTrack.name.clear();
    }
    pthread_mutex_unlock(&g_audioMutex);
}

static void fadeOutBgs(int seconds) {
    pthread_mutex_lock(&g_audioMutex);
    if (seconds <= 0) {
        g_bgsTrack.active = false;
        g_bgsTrack.name.clear();
        g_bgsTrack.fadeTotalFrames = 0;
        g_bgsTrack.fadeRemFrames = 0;
    } else if (g_bgsTrack.active) {
        int f = seconds * 48000;
        g_bgsTrack.fadeTotalFrames = f;
        g_bgsTrack.fadeRemFrames = f;
        g_bgsTrack.fadeIn = false;
    }
    pthread_mutex_unlock(&g_audioMutex);
}

static void playSe(const std::string& name, int volume = 100, int pitch = 100) {
    if (name.empty()) return;
    if (name == "bikkuri" || name == "crash23" || name == "a5_05142") {
        triggerRumble(220, 180, 22);
    } else if (name == "025-Door02" && pitch >= 140) {
        triggerRumble(255, 255, 45);
    }
    pthread_mutex_lock(&g_audioMutex);
    const PcmBuffer* pb = getPcm("se_", name);
    if (pb && pb->frames > 0) {
        int slot = 0;
        for (int i = 0; i < 6; i++) {
            if (!g_seChannels[i].active) { slot = i; break; }
        }
        g_seChannels[slot].buf = pb;
        g_seChannels[slot].posFP = 0;
        g_seChannels[slot].origPitch = pitch;
        g_seChannels[slot].stepFP = (uint32_t)((std::max(25, std::min(250, pitch)) * 65536ULL) / 100ULL);
        g_seChannels[slot].volume = std::max(0, std::min(100, volume));
        g_seChannels[slot].fadeTotalFrames = 0;
        g_seChannels[slot].fadeRemFrames = 0;
        g_seChannels[slot].loop = false;
        g_seChannels[slot].name = name;
        g_seChannels[slot].active = true;
    }
    pthread_mutex_unlock(&g_audioMutex);
}

static void stopAllSe() {
    pthread_mutex_lock(&g_audioMutex);
    for (int i = 0; i < 6; i++) g_seChannels[i].active = false;
    pthread_mutex_unlock(&g_audioMutex);
}

static inline void mixTrack(ActiveSound& tr, int32_t* accum, int frames) {
    if (!tr.active || !tr.buf || tr.buf->frames == 0) return;
    const int16_t* smp = tr.buf->samples.data();
    size_t totalF = tr.buf->frames;

    for (int i = 0; i < frames; i++) {
        size_t idx = (size_t)(tr.posFP >> 16);
        if (idx >= totalF) {
            if (tr.loop) {
                tr.posFP %= ((uint64_t)totalF << 16);
                idx = (size_t)(tr.posFP >> 16);
            } else {
                tr.active = false;
                return;
            }
        }
        int vol = tr.volume;
        if (tr.fadeTotalFrames > 0) {
            if (tr.fadeIn) {
                if (tr.fadeRemFrames <= 0) {
                    tr.fadeTotalFrames = 0;
                    tr.fadeIn = false;
                } else {
                    int elapsed = tr.fadeTotalFrames - tr.fadeRemFrames;
                    vol = (vol * (elapsed / 256)) / std::max(1, tr.fadeTotalFrames / 256);
                    tr.fadeRemFrames--;
                }
            } else {
                if (tr.fadeRemFrames <= 0) {
                    tr.active = false;
                    tr.fadeTotalFrames = 0;
                    return;
                }
                vol = (vol * (tr.fadeRemFrames / 256)) / std::max(1, tr.fadeTotalFrames / 256);
                tr.fadeRemFrames--;
            }
        }
        accum[i * 2]     += ((int32_t)smp[idx * 2]     * vol) / 100;
        accum[i * 2 + 1] += ((int32_t)smp[idx * 2 + 1] * vol) / 100;
        tr.posFP += tr.stepFP;
    }
}

static void* audioThreadFunc(void*) {
    sceUserServiceInitialize(NULL);
    if (sceAudioOutInit() != 0) {
        printf("[AoOni] Warn: sceAudioOutInit fallo\n");
    }
    int32_t handle = sceAudioOutOpen(
        ORBIS_USER_SERVICE_USER_ID_SYSTEM,
        ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,
        0, 256, 48000,
        ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO
    );
    if (handle <= 0) {
        printf("[AoOni] Warn: sceAudioOutOpen fallo (%d)\n", handle);
        return nullptr;
    }
    printf("[AoOni] Audio PS4 inicializado OK (handle=%d, 48000Hz Stereo)\n", handle);

    int16_t outBuf[2][256 * 2];
    int32_t accum[256 * 2];
    int flip = 0;

    while (g_audioRunning) {
        memset(accum, 0, sizeof(accum));
        pthread_mutex_lock(&g_audioMutex);
        mixTrack(g_bgmTrack, accum, 256);
        mixTrack(g_bgsTrack, accum, 256);
        for (int ch = 0; ch < 6; ch++)
            mixTrack(g_seChannels[ch], accum, 256);
        pthread_mutex_unlock(&g_audioMutex);

        int16_t* dst = outBuf[flip];
        for (int i = 0; i < 256 * 2; i++) {
            int32_t v = accum[i];
            if (v > 32767) v = 32767;
            else if (v < -32768) v = -32768;
            dst[i] = (int16_t)v;
        }
        sceAudioOutOutput(handle, dst);
        flip ^= 1;
    }
    sceAudioOutOutput(handle, NULL);
    sceAudioOutClose(handle);
    return nullptr;
}

// ==================== FUENTE PROPORCIONAL ESPAÑOLA Y RENDERIZADO DIRECTO ULTRARRÁPIDO ====================
#include "font_prop18.h"

static std::string normalizeUtf8Text(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        // Normalizar puntos suspensivos Unicode (U+2026: E2 80 A6) -> "..."
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x80 && (unsigned char)in[i + 2] == 0xA6) {
            out += "...";
            i += 2;
            continue;
        }
        // Flecha Unicode (U+2192: E2 86 92) -> "->"
        if (c == 0xE2 && i + 2 < in.size() && (unsigned char)in[i + 1] == 0x86 && (unsigned char)in[i + 2] == 0x92) {
            out += "->";
            i += 2;
            continue;
        }
        // Digitos fullwidth (U+FF10..U+FF19: EF BC 90..99)
        if (c == 0xEF && i + 2 < in.size() && (unsigned char)in[i + 1] == 0xBC) {
            unsigned char c3 = (unsigned char)in[i + 2];
            if (c3 >= 0x90 && c3 <= 0x99) {
                out.push_back((char)('0' + (c3 - 0x90)));
                i += 2;
                continue;
            }
        }
        out.push_back((char)c);
    }
    return out;
}

// Decodifica el siguiente punto de codigo UTF-8 a indice de FONT18 (0..110), o -1 si es '\n'. Avanza i al final del caracter.
static int decodeNextUtf8Glyph(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char c = (unsigned char)s[i++];
    if (c == '\n') return -1;
    if (c == '\r' || c == '\t') return 0;
    if (c < 128) {
        if (c >= 32 && c <= 126) return (int)(c - 32);
        return (int)('?' - 32);
    }
    if (c == 0xC3 && i < s.size()) {
        unsigned char c2 = (unsigned char)s[i++];
        switch (c2) {
            case 0xA1: return 95;  // á
            case 0xA9: return 96;  // é
            case 0xAD: return 97;  // í
            case 0xB3: return 98;  // ó
            case 0xBA: return 99;  // ú
            case 0xBC: return 100; // ü
            case 0xB1: return 101; // ñ
            case 0x81: return 102; // Á
            case 0x89: return 103; // É
            case 0x8D: return 104; // Í
            case 0x93: return 105; // Ó
            case 0x9A: return 106; // Ú
            case 0x9C: return 107; // Ü
            case 0x91: return 108; // Ñ
            case 0xA0: case 0xA2: case 0xA4: return (int)('a' - 32);
            case 0xA8: case 0xAA: case 0xAB: return (int)('e' - 32);
            case 0xAC: case 0xAE: case 0xAF: return (int)('i' - 32);
            case 0xB2: case 0xB4: case 0xB6: return (int)('o' - 32);
            case 0xB9: case 0xBB: return (int)('u' - 32);
            default: return (int)('?' - 32);
        }
    }
    if (c == 0xC2 && i < s.size()) {
        unsigned char c2 = (unsigned char)s[i++];
        if (c2 == 0xBF) return 109; // ¿
        if (c2 == 0xA1) return 110; // ¡
        return (int)(' ' - 32);
    }
    // Saltar cualquier byte de continuacion UTF-8 restante (0x80..0xBF)
    while (i < s.size() && (((unsigned char)s[i] & 0xC0) == 0x80)) i++;
    return (int)('?' - 32);
}

static inline int glyphAdvancePx(int gidx, int scale) {
    if (gidx < 0 || gidx >= FONT18_COUNT) gidx = (int)('?' - 32);
    if (scale == 2) return FONT18_ADV[gidx];
    return std::max(1, (FONT18_ADV[gidx] * scale + 1) / 2);
}

static int measureTextWidth(const std::string& raw, int scale) {
    std::string s = normalizeUtf8Text(raw);
    int maxW = 0, curW = 0;
    size_t i = 0;
    while (i < s.size()) {
        int g = decodeNextUtf8Glyph(s, i);
        if (g < 0) {
            if (curW > maxW) maxW = curW;
            curW = 0;
            continue;
        }
        curW += glyphAdvancePx(g, scale);
    }
    return std::max(maxW, curW);
}

// Ajusta el texto por ancho exacto en pixeles respetando saltos '\n' existentes
static std::string wrapTextToWidth(const std::string& raw, int maxWidthPx, int scale = 2) {
    if (maxWidthPx <= 16) return normalizeUtf8Text(raw);
    std::string s = normalizeUtf8Text(raw);
    std::string result;
    result.reserve(s.size() + 16);

    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = (nl == std::string::npos) ? s.substr(start) : s.substr(start, nl - start);
        if (!result.empty() || start > 0) {
            if (start > 0) result.push_back('\n');
        }

        // Tokenizar palabras conservando espacios al inicio de parrafo y partiendo palabras si exceden maxWidthPx
        size_t pos = 0;
        std::string curLine;
        int curW = 0;
        while (pos < para.size()) {
            // Leer espacios antes de palabra
            std::string spaces;
            int spacesW = 0;
            while (pos < para.size() && para[pos] == ' ') {
                spaces.push_back(' ');
                spacesW += glyphAdvancePx(0, scale);
                pos++;
            }
            // Leer palabra siguiente hasta espacio
            size_t wStart = pos;
            int wordW = 0;
            while (pos < para.size() && para[pos] != ' ') {
                size_t nextI = pos;
                int g = decodeNextUtf8Glyph(para, nextI);
                wordW += glyphAdvancePx(g, scale);
                pos = nextI;
            }
            std::string word = para.substr(wStart, pos - wStart);

            if (word.empty()) {
                if (curLine.empty() && !spaces.empty()) {
                    if (spacesW <= maxWidthPx) { curLine += spaces; curW += spacesW; }
                }
                break;
            }

            if (curLine.empty()) {
                int leadW = spacesW + wordW;
                if (leadW <= maxWidthPx) {
                    curLine = spaces + word;
                    curW = leadW;
                } else {
                    // Palabra mas larga que toda la linea: partir por glifos
                    size_t wi = 0;
                    while (wi < word.size()) {
                        size_t nextI = wi;
                        int g = decodeNextUtf8Glyph(word, nextI);
                        int adv = glyphAdvancePx(g, scale);
                        if (!curLine.empty() && curW + adv > maxWidthPx) {
                            result += curLine;
                            result.push_back('\n');
                            curLine.clear();
                            curW = 0;
                        }
                        curLine.append(word, wi, nextI - wi);
                        curW += adv;
                        wi = nextI;
                    }
                }
            } else {
                if (curW + spacesW + wordW <= maxWidthPx) {
                    curLine += spaces + word;
                    curW += spacesW + wordW;
                } else {
                    result += curLine;
                    result.push_back('\n');
                    curLine.clear();
                    curW = 0;
                    if (wordW <= maxWidthPx) {
                        curLine = word;
                        curW = wordW;
                    } else {
                        size_t wi = 0;
                        while (wi < word.size()) {
                            size_t nextI = wi;
                            int g = decodeNextUtf8Glyph(word, nextI);
                            int adv = glyphAdvancePx(g, scale);
                            if (!curLine.empty() && curW + adv > maxWidthPx) {
                                result += curLine;
                                result.push_back('\n');
                                curLine.clear();
                                curW = 0;
                            }
                            curLine.append(word, wi, nextI - wi);
                            curW += adv;
                            wi = nextI;
                        }
                    }
                }
            }
        }
        result += curLine;

        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return result;
}

// PS4 sceVideoOut usa formato A8B8G8R8Srgb (en little-endian: byte0=R, byte1=G, byte2=B, byte3=A)
static inline uint32_t packWinPixel(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline void unpackWinPixel(uint32_t d, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (uint8_t)(d & 0xFFu);
    g = (uint8_t)((d >> 8) & 0xFFu);
    b = (uint8_t)((d >> 16) & 0xFFu);
}

static inline void clearSurfFast(SDL_Surface* dst) {
    if (!dst || !dst->pixels) return;
    uint64_t* p64 = (uint64_t*)dst->pixels;
    int count64 = (dst->pitch * dst->h) / 8;
    const uint64_t black = 0xFF000000FF000000ULL;
    for (int i = 0; i < count64; i++) p64[i] = black;
}

static inline void fillRectFast(SDL_Surface* dst, int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    if (!dst || !dst->pixels || rw <= 0 || rh <= 0 || a == 0) return;
    int x0 = std::max(0, rx), y0 = std::max(0, ry);
    int x1 = std::min(dst->w, rx + rw), y1 = std::min(dst->h, ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

    if (a == 255) {
        uint32_t col = packWinPixel(r, g, b);
        for (int y = y0; y < y1; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)dst->pixels + y * dst->pitch);
            for (int x = x0; x < x1; x++) row[x] = col;
        }
    } else {
        uint32_t invA = 255 - a;
        for (int y = y0; y < y1; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)dst->pixels + y * dst->pitch);
            for (int x = x0; x < x1; x++) {
                uint8_t dr, dg, db;
                unpackWinPixel(row[x], dr, dg, db);
                uint8_t nr = (uint8_t)((r * a + dr * invA) >> 8);
                uint8_t ng = (uint8_t)((g * a + dg * invA) >> 8);
                uint8_t nb = (uint8_t)((b * a + db * invA) >> 8);
                row[x] = packWinPixel(nr, ng, nb);
            }
        }
    }
}

static inline void applyToneRectFast(SDL_Surface* dst, int rx, int ry, int rw, int rh, int tr, int tg, int tb) {
    if (!dst || !dst->pixels || (tr == 0 && tg == 0 && tb == 0)) return;
    int x0 = std::max(0, rx), y0 = std::max(0, ry);
    int x1 = std::min(dst->w, rx + rw), y1 = std::min(dst->h, ry + rh);
    if (x0 >= x1 || y0 >= y1) return;

    // LUT de 256 entradas precalculada (solo se regenera al cambiar el tono de pantalla)
    static int s_lastTr = 999999, s_lastTg = 999999, s_lastTb = 999999;
    static uint8_t s_lutR[256], s_lutG[256], s_lutB[256];
    if (s_lastTr != tr || s_lastTg != tg || s_lastTb != tb) {
        for (int i = 0; i < 256; i++) {
            s_lutR[i] = (uint8_t)std::max(0, std::min(255, i + tr));
            s_lutG[i] = (uint8_t)std::max(0, std::min(255, i + tg));
            s_lutB[i] = (uint8_t)std::max(0, std::min(255, i + tb));
        }
        s_lastTr = tr; s_lastTg = tg; s_lastTb = tb;
    }

    for (int y = y0; y < y1; y++) {
        uint32_t* row = (uint32_t*)((uint8_t*)dst->pixels + y * dst->pitch);
        for (int x = x0; x < x1; x++) {
            uint8_t r, g, b;
            unpackWinPixel(row[x], r, g, b);
            row[x] = packWinPixel(s_lutR[r], s_lutG[g], s_lutB[b]);
        }
    }
}

static inline void drawRectFast(SDL_Surface* dst, int rx, int ry, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    fillRectFast(dst, rx, ry, rw, 2, r, g, b);
    fillRectFast(dst, rx, ry + rh - 2, rw, 2, r, g, b);
    fillRectFast(dst, rx, ry, 2, rh, r, g, b);
    fillRectFast(dst, rx + rw - 2, ry, 2, rh, r, g, b);
}

static inline void blendGlyphPixel(uint32_t* drow, int px, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t a) {
    if (a >= 250) {
        drow[px] = packWinPixel(cr, cg, cb);
        return;
    }
    uint32_t dst = drow[px];
    uint32_t dr = dst & 0xFFu;
    uint32_t dg = (dst >> 8) & 0xFFu;
    uint32_t db = (dst >> 16) & 0xFFu;
    uint32_t inv = 255u - (uint32_t)a;
    uint8_t nr = (uint8_t)((dr * inv + (uint32_t)cr * a) / 255u);
    uint8_t ng = (uint8_t)((dg * inv + (uint32_t)cg * a) / 255u);
    uint8_t nb = (uint8_t)((db * inv + (uint32_t)cb * a) / 255u);
    drow[px] = packWinPixel(nr, ng, nb);
}

static inline void drawChar18Scaled(SDL_Surface* dst, int x, int y, int gidx, int scale, uint8_t cr, uint8_t cg, uint8_t cb) {
    if (gidx < 0 || gidx >= FONT18_COUNT) gidx = (int)('?' - 32);
    int gw = FONT18_GW[gidx];
    if (gw <= 0) return;

    if (scale == 2) {
        for (int row = 0; row < FONT18_H; row++) {
            int py = y + row;
            if (py < 0 || py >= dst->h) continue;
            uint32_t* drow = (uint32_t*)((uint8_t*)dst->pixels + py * dst->pitch);
            const uint8_t* rbytes = FONT18_AA[gidx][row];
            for (int col = 0; col < gw; col++) {
                int px = x + col;
                if (px < 0 || px >= dst->w) continue;
                uint8_t b = rbytes[col >> 1];
                uint8_t niv = (col & 1) ? (b & 0x0Fu) : (b >> 4);
                if (niv > 0) blendGlyphPixel(drow, px, cr, cg, cb, (uint8_t)(niv * 17));
            }
        }
        return;
    }

    int sw = std::max(1, (gw * scale + 1) / 2);
    int sh = std::max(1, (FONT18_H * scale + 1) / 2);
    for (int dy = 0; dy < sh; dy++) {
        int py = y + dy;
        if (py < 0 || py >= dst->h) continue;
        int sy = std::min(FONT18_H - 1, (dy * 2) / std::max(1, scale));
        uint32_t* drow = (uint32_t*)((uint8_t*)dst->pixels + py * dst->pitch);
        const uint8_t* rbytes = FONT18_AA[gidx][sy];
        for (int dx = 0; dx < sw; dx++) {
            int px = x + dx;
            if (px < 0 || px >= dst->w) continue;
            int sx = std::min(gw - 1, (dx * 2) / std::max(1, scale));
            uint8_t b = rbytes[sx >> 1];
            uint8_t niv = (sx & 1) ? (b & 0x0Fu) : (b >> 4);
            if (niv > 0) blendGlyphPixel(drow, px, cr, cg, cb, (uint8_t)(niv * 17));
        }
    }
}

static void drawTextLineStep(SDL_Surface* dst, int x, int y, const std::string& raw, int scale, int lineStep, uint8_t cr, uint8_t cg, uint8_t cb) {
    if (!dst || raw.empty()) return;
    std::string s = normalizeUtf8Text(raw);
    int shOff = (scale >= 3) ? 2 : 1;
    int stepY = (lineStep > 0) ? lineStep : ((scale == 2) ? 21 : std::max(10, (FONT18_H * scale + 1) / 2));

    // Dos pasadas para que la sombra del siguiente glifo nunca ensombrezca el borde suavizado del anterior
    for (int pass = 0; pass < 2; pass++) {
        int cx = x + (pass == 0 ? shOff : 0);
        int cy = y + (pass == 0 ? shOff : 0);
        uint8_t pr = (pass == 0) ? 0 : cr;
        uint8_t pg = (pass == 0) ? 0 : cg;
        uint8_t pb = (pass == 0) ? 0 : cb;
        size_t i = 0;
        while (i < s.size()) {
            int g = decodeNextUtf8Glyph(s, i);
            if (g < 0) {
                cy += stepY;
                cx = x + (pass == 0 ? shOff : 0);
                continue;
            }
            drawChar18Scaled(dst, cx, cy, g, scale, pr, pg, pb);
            cx += glyphAdvancePx(g, scale);
        }
    }
}

static void drawText(SDL_Surface* dst, int x, int y, const std::string& raw, int scale, uint8_t cr, uint8_t cg, uint8_t cb) {
    drawTextLineStep(dst, x, y, raw, scale, 0, cr, cg, cb);
}

// Cache global de superficies cargadas con stb_image
static std::map<std::string, SDL_Surface*> g_surfCache;

static SDL_Surface* getSurf(const std::string& path) {
    if (path.empty()) return nullptr;
    auto it = g_surfCache.find(path);
    if (it != g_surfCache.end()) return it->second;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        g_surfCache[path] = nullptr;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) {
        fclose(f);
        g_surfCache[path] = nullptr;
        return nullptr;
    }
    std::vector<uint8_t> buf(fsz);
    fread(buf.data(), 1, fsz, f);
    fclose(f);

    int w = 0, h = 0, ch = 0;
    uint8_t* rgba = stbi_load_from_memory(buf.data(), (int)buf.size(), &w, &h, &ch, 4);
    if (!rgba) { g_surfCache[path] = nullptr; return nullptr; }

    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (s) {
        for (int y = 0; y < h; y++)
            memcpy((uint8_t*)s->pixels + y * s->pitch, rgba + y * w * 4, w * 4);
    }
    stbi_image_free(rgba);
    g_surfCache[path] = s;
    return s;
}

// Blitter optimizado con ruta rápida 1:1 sin divisiones por píxel
static void blitSurf(SDL_Surface* src, const SDL_Rect* sr, SDL_Surface* dst, SDL_Rect* dr, uint8_t alphaMul = 255, bool dstIsWindow = true) {
    if (!src || !dst || !src->pixels || !dst->pixels || alphaMul == 0) return;
    int sx0 = sr ? sr->x : 0, sy0 = sr ? sr->y : 0;
    int sw  = sr ? sr->w : src->w, sh  = sr ? sr->h : src->h;
    int dx0 = dr ? dr->x : 0, dy0 = dr ? dr->y : 0;
    int dw  = dr ? dr->w : sw,    dh  = dr ? dr->h : sh;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    // Ruta rápida 1:1 (sin escalado)
    if (sw == dw && sh == dh) {
        int clipX0 = 0, clipY0 = 0;
        if (dx0 < 0) { clipX0 = -dx0; sx0 += clipX0; dw -= clipX0; dx0 = 0; }
        if (dy0 < 0) { clipY0 = -dy0; sy0 += clipY0; dh -= clipY0; dy0 = 0; }
        if (sx0 < 0) { dx0 -= sx0; dw += sx0; sx0 = 0; }
        if (sy0 < 0) { dy0 -= sy0; dh += sy0; sy0 = 0; }
        if (dx0 + dw > dst->w) dw = dst->w - dx0;
        if (dy0 + dh > dst->h) dh = dst->h - dy0;
        if (sx0 + dw > src->w) dw = src->w - sx0;
        if (sy0 + dh > src->h) dh = src->h - sy0;
        if (dw <= 0 || dh <= 0) return;

        for (int y = 0; y < dh; y++) {
            const uint8_t* sp = (const uint8_t*)src->pixels + (sy0 + y) * src->pitch + sx0 * 4;
            if (dstIsWindow) {
                uint32_t* dp = (uint32_t*)((uint8_t*)dst->pixels + (dy0 + y) * dst->pitch) + dx0;
                for (int x = 0; x < dw; x++, sp += 4) {
                    uint32_t sa = (alphaMul == 255) ? sp[3] : ((sp[3] * alphaMul) >> 8);
                    if (sa == 0) continue;
                    if (sa >= 254) {
                        dp[x] = packWinPixel(sp[0], sp[1], sp[2]);
                    } else {
                        uint8_t dr, dg, db;
                        unpackWinPixel(dp[x], dr, dg, db);
                        uint32_t inv = 255 - sa;
                        uint8_t r = (uint8_t)((sp[0] * sa + dr * inv) >> 8);
                        uint8_t g = (uint8_t)((sp[1] * sa + dg * inv) >> 8);
                        uint8_t b = (uint8_t)((sp[2] * sa + db * inv) >> 8);
                        dp[x] = packWinPixel(r, g, b);
                    }
                }
            } else {
                // Ambos RGBA32 (pre-renderizado de capas de mapa)
                uint8_t* dp = (uint8_t*)dst->pixels + (dy0 + y) * dst->pitch + dx0 * 4;
                for (int x = 0; x < dw; x++, sp += 4, dp += 4) {
                    uint32_t sa = (alphaMul == 255) ? sp[3] : ((sp[3] * alphaMul) >> 8);
                    if (sa == 0) continue;
                    if (sa >= 254 || dp[3] == 0) {
                        dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = (uint8_t)sa;
                    } else {
                        uint32_t inv = 255 - sa;
                        dp[0] = (uint8_t)((sp[0] * sa + dp[0] * inv) >> 8);
                        dp[1] = (uint8_t)((sp[1] * sa + dp[1] * inv) >> 8);
                        dp[2] = (uint8_t)((sp[2] * sa + dp[2] * inv) >> 8);
                        dp[3] = (uint8_t)std::min(255u, dp[3] + sa);
                    }
                }
            }
        }
        return;
    }

    // Ruta con escalado (zoom de Pictures / sprite de título)
    for (int dy = 0; dy < dh; dy++) {
        int py = dy0 + dy;
        if (py < 0 || py >= dst->h) continue;
        int sy = sy0 + (dy * sh) / dh;
        if (sy < 0 || sy >= src->h) continue;

        const uint8_t* srow = (const uint8_t*)src->pixels + sy * src->pitch;
        uint32_t* drow = (uint32_t*)((uint8_t*)dst->pixels + py * dst->pitch);

        for (int dx = 0; dx < dw; dx++) {
            int px = dx0 + dx;
            if (px < 0 || px >= dst->w) continue;
            int sx = sx0 + (dx * sw) / dw;
            if (sx < 0 || sx >= src->w) continue;

            const uint8_t* sp = srow + sx * 4;
            uint32_t sa = (alphaMul == 255) ? sp[3] : ((sp[3] * alphaMul) >> 8);
            if (sa == 0) continue;
            if (sa >= 254) {
                drow[px] = packWinPixel(sp[0], sp[1], sp[2]);
            } else {
                uint8_t dr, dg, db;
                unpackWinPixel(drow[px], dr, dg, db);
                uint32_t inv = 255 - sa;
                uint8_t r = (uint8_t)((sp[0] * sa + dr * inv) >> 8);
                uint8_t g = (uint8_t)((sp[1] * sa + dg * inv) >> 8);
                uint8_t b = (uint8_t)((sp[2] * sa + db * inv) >> 8);
                drow[px] = packWinPixel(r, g, b);
            }
        }
    }
}

// Blitter con rotación/escala para PictureRotate (cmd 233, ej: fragmentos de cabeza en Map 021)
static void blitSurfRotated(SDL_Surface* src, SDL_Surface* dst, float centerX, float centerY,
                            float zoomX, float zoomY, float angleDeg, uint8_t alphaMul = 255) {
    if (!src || !dst || !src->pixels || !dst->pixels || alphaMul == 0) return;
    float zx = std::max(1.0f, zoomX) / 100.0f;
    float zy = std::max(1.0f, zoomY) / 100.0f;
    float dw = src->w * zx;
    float dh = src->h * zy;
    float rad = -angleDeg * (3.14159265f / 180.0f);
    float c = std::cos(rad);
    float s = std::sin(rad);
    float halfDiag = 0.5f * std::sqrt(dw * dw + dh * dh) + 2.0f;

    int x0 = std::max(0, (int)std::floor(centerX - halfDiag));
    int y0 = std::max(0, (int)std::floor(centerY - halfDiag));
    int x1 = std::min(dst->w, (int)std::ceil(centerX + halfDiag));
    int y1 = std::min(dst->h, (int)std::ceil(centerY + halfDiag));
    if (x0 >= x1 || y0 >= y1) return;

    float invZx = 1.0f / zx;
    float invZy = 1.0f / zy;
    float hSrcW = src->w * 0.5f;
    float hSrcH = src->h * 0.5f;

    for (int py = y0; py < y1; py++) {
        float dy = (py + 0.5f) - centerY;
        uint32_t* drow = (uint32_t*)((uint8_t*)dst->pixels + py * dst->pitch);
        for (int px = x0; px < x1; px++) {
            float dx = (px + 0.5f) - centerX;
            int sx = (int)std::floor((dx * c + dy * s) * invZx + hSrcW);
            int sy = (int)std::floor((-dx * s + dy * c) * invZy + hSrcH);
            if (sx < 0 || sx >= src->w || sy < 0 || sy >= src->h) continue;
            const uint8_t* sp = (const uint8_t*)src->pixels + sy * src->pitch + sx * 4;
            uint32_t sa = (alphaMul == 255) ? sp[3] : ((sp[3] * alphaMul) >> 8);
            if (sa == 0) continue;
            if (sa >= 254) {
                drow[px] = packWinPixel(sp[0], sp[1], sp[2]);
            } else {
                uint8_t dr, dg, db;
                unpackWinPixel(drow[px], dr, dg, db);
                uint32_t inv = 255 - sa;
                drow[px] = packWinPixel((uint8_t)((sp[0] * sa + dr * inv) >> 8),
                                        (uint8_t)((sp[1] * sa + dg * inv) >> 8),
                                        (uint8_t)((sp[2] * sa + db * inv) >> 8));
            }
        }
    }
}

// Marco de ventana RPG Maker XP usando ws_wskin_n.png (con respaldo limpio para recuadros pequeños)
static void drawWindowBox(SDL_Surface* dst, int x, int y, int w, int h, uint8_t fillAlpha = 220) {
    if (!dst || w <= 4 || h <= 4) return;
    fillRectFast(dst, x + 2, y + 2, w - 4, h - 4, 12, 16, 36, fillAlpha);
    static SDL_Surface* s_ws = nullptr;
    static bool s_wsInit = false;
    if (!s_wsInit) {
        s_wsInit = true;
        s_ws = getSurf("/app0/assets/images/ws_wskin_n.png");
    }
    if (s_ws && w >= 32 && h >= 32) {
        SDL_Rect tlS = { 128, 0, 16, 16 },  tlD = { x, y, 16, 16 };
        SDL_Rect trS = { 176, 0, 16, 16 },  trD = { x + w - 16, y, 16, 16 };
        SDL_Rect blS = { 128, 48, 16, 16 }, blD = { x, y + h - 16, 16, 16 };
        SDL_Rect brS = { 176, 48, 16, 16 }, brD = { x + w - 16, y + h - 16, 16, 16 };
        SDL_Rect tS  = { 144, 0, 32, 16 },  tD  = { x + 16, y, w - 32, 16 };
        SDL_Rect bS  = { 144, 48, 32, 16 }, bD  = { x + 16, y + h - 16, w - 32, 16 };
        SDL_Rect lS  = { 128, 16, 16, 32 }, lD  = { x, y + 16, 16, h - 32 };
        SDL_Rect rS  = { 176, 16, 16, 32 }, rD  = { x + w - 16, y + 16, 16, h - 32 };
        blitSurf(s_ws, &tlS, dst, &tlD);
        blitSurf(s_ws, &trS, dst, &trD);
        blitSurf(s_ws, &blS, dst, &blD);
        blitSurf(s_ws, &brS, dst, &brD);
        if (w > 32) { blitSurf(s_ws, &tS, dst, &tD); blitSurf(s_ws, &bS, dst, &bD); }
        if (h > 32) { blitSurf(s_ws, &lS, dst, &lD); blitSurf(s_ws, &rS, dst, &rD); }
    } else {
        drawRectFast(dst, x, y, w, h, 225, 232, 255);
    }
}

// Tabla oficial de RPG Maker XP para los 48 sub-patrones de Autotiles (4 cuadrantes de 16x16)
static const uint8_t AUTOTILE_LUT[48][4] = {
    {26,27,32,33},{ 4,27,32,33},{26, 5,32,33},{ 4, 5,32,33},{26,27,32,11},{ 4,27,32,11},{26, 5,32,11},{ 4, 5,32,11},
    {26,27,10,33},{ 4,27,10,33},{26, 5,10,33},{ 4, 5,10,33},{26,27,10,11},{ 4,27,10,11},{26, 5,10,11},{ 4, 5,10,11},
    {24,25,30,31},{24, 5,30,31},{24,25,30,11},{24, 5,30,11},{14,15,20,21},{14,15,20,11},{14,15,10,21},{14,15,10,11},
    {28,29,34,35},{28,29,10,35},{ 4,29,34,35},{ 4,29,10,35},{38,39,44,45},{ 4,39,44,45},{38, 5,44,45},{ 4, 5,44,45},
    {24,29,30,35},{14,15,44,45},{12,13,18,19},{12,13,18,11},{16,17,22,23},{16,17,10,23},{40,41,46,47},{ 4,41,46,47},
    {36,37,42,43},{36, 5,42,43},{12,17,18,23},{12,13,42,43},{36,41,42,47},{16,17,46,47},{12,17,42,47},{ 0, 1, 6, 7}
};

static void blitAutotileToCache(SDL_Surface* atSurf, int pattern, SDL_Surface* dstSurf, int dx, int dy, bool dstIsWindow = false) {
    if (!atSurf || !dstSurf) return;
    if (pattern < 0 || pattern >= 48) pattern = 0;
    if (atSurf->w < 96 || atSurf->h < 128) {
        SDL_Rect sr = { 0, 0, std::min(32, atSurf->w), std::min(32, atSurf->h) };
        SDL_Rect dr = { dx, dy, 32, 32 };
        blitSurf(atSurf, &sr, dstSurf, &dr, 255, dstIsWindow);
        return;
    }
    for (int q = 0; q < 4; q++) {
        uint8_t sub = AUTOTILE_LUT[pattern][q];
        SDL_Rect sr = { (sub % 6) * 16, (sub / 6) * 16, 16, 16 };
        SDL_Rect dr = { dx + (q % 2) * 16, dy + (q / 2) * 16, 16, 16 };
        blitSurf(atSurf, &sr, dstSurf, &dr, 255, dstIsWindow);
    }
}

static inline int calcTileScreenZ(int ty, uint8_t prio) {
    return (ty + (int)prio + 1) * TILE_SZ;
}

static inline int calcSpriteScreenZ(float realY, int spriteH, int tileId, uint8_t tilePrio, bool alwaysTop) {
    if (alwaysTop) return 999999;
    int py = (int)std::round(realY);
    if (tileId >= 384) return py + TILE_SZ + (int)tilePrio * TILE_SZ;
    return py + TILE_SZ + ((spriteH > TILE_SZ) ? (TILE_SZ - 1) : 0);
}

// ==================== ESTRUCTURAS DEL MOTOR RPG MAKER XP ====================
struct MoveCmd { int code = 0; JVal params; };
struct MoveRoute {
    bool repeat = false;
    bool skippable = false;
    std::vector<MoveCmd> list;
};

struct EventPage {
    bool sw1_valid = false; int sw1_id = 1;
    bool sw2_valid = false; int sw2_id = 1;
    bool var_valid = false; int var_id = 1; int var_val = 0;
    bool self_valid = false; std::string self_ch = "A";
    std::string char_name;
    int dir = 2, pattern = 0, opacity = 255, tile_id = 0;
    int move_type = 0, move_speed = 3, move_freq = 3;
    MoveRoute move_route;
    bool walk_anime = true, step_anime = false, dir_fix = false, through = false, always_top = false;
    int trigger = 0;
    JVal list;
};

struct MapEvent {
    int id = 0;
    std::string name;
    int x = 0, y = 0;
    float realX = 0.0f, realY = 0.0f;
    int animTicks = 0;
    int dir = 2, pattern = 0, original_pattern = 0, opacity = 255, tile_id = 0;
    std::string char_name;
    SDL_Surface* cachedSurf = nullptr;
    bool walk_anime = true, through = false, always_top = false, dir_fix = false, step_anime = false, erased = false;
    bool collisionAttack = false; // Activado por MoveCmd 45 "attack_on" (XAS_BA)
    bool isBattler = false;       // Identificado por página dummy con variable_id == 25 (XAS_BA::ENEMY_ID_VARIABLE_ID)
    bool grantsItem = false;      // Página activa trigger==0 contiene cmd 126 (añadir objeto)
    bool isTinyInteractive = false; // Interruptor/pista pequeña o contenedor visible QoL
    bool isInteractable = false;  // Página activa trigger==0 con acción interactuable
    int jumpTimer = 0, jumpTotal = 0;
    int move_type = 0;
    float move_speed = 3.0f;
    int move_freq = 3;
    std::vector<EventPage> pages;
    int activePage = -1;
    uint32_t lastAutoMoveTick = 0;

    bool routeActive = false;
    MoveRoute forcedRoute;
    size_t routeIdx = 0;
    uint32_t lastRouteStepTick = 0;
    int routeWaitFrames = 0;

    size_t parIp = 0;
    int parWait = 0;
};

struct Picture {
    bool active = false;
    std::string name;
    SDL_Surface* cachedSurf = nullptr;
    int origin = 0; // 0=upper-left, 1=center
    float x = 0, y = 0, zoomX = 100.0f, zoomY = 100.0f, opacity = 255.0f;
    float targetX = 0, targetY = 0, targetZoomX = 100.0f, targetZoomY = 100.0f, targetOpacity = 255.0f;
    float angle = 0.0f, rotSpeed = 0.0f;
    int moveFrames = 0;
};

struct ItemDef {
    std::string name, icon, desc;
    int common_event_id = 0;
    bool consumable = false;
};

struct CommonEventDef {
    int id = 0, trigger = 0, switch_id = 1;
    JVal list;
    size_t parIp = 0;
    int parWait = 0;
};

struct InterpFrame {
    JVal list;
    size_t ip = 0;
    int evId = 0;
    int mapId = 0;
};

struct GameRuntime {
    bool switches[512] = {false};
    int variables[512] = {0};
    std::map<std::string, bool> selfSwitches;
    std::map<int, int> inventory;
    std::map<int, std::string> actorNames;
    std::map<int, std::string> actorChars;
    std::vector<int> startingParty = {1};
    std::vector<int> partyActors = {1};
    std::map<int, ItemDef> itemDefs;
    std::vector<CommonEventDef> commonEvents;
    Picture pictures[52];

    int mapId = 75;
    int mapW = 20, mapH = 15, tilesetId = 1;
    char tilesetFile[64] = {0};
    char autotileFiles[7][64] = {{0}};
    std::vector<int16_t> layers;
    std::vector<uint8_t> passable;
    std::vector<uint8_t> priorities;
    std::vector<uint8_t> maskedPenTiles;
    bool autoplayBgs = false;
    std::string mapBgsName;
    int mapBgsVol = 100;
    int mapBgsPitch = 100;
    std::vector<MapEvent> events;

    SDL_Surface* mapLowerSurf = nullptr;
    SDL_Surface* mapUpperSurf = nullptr;
    struct PrioTile { int tx, ty; int16_t tid; int z; };
    std::vector<PrioTile> prioTiles;

    int playerX = 9, playerY = 8, playerDir = 2, playerPattern = 0;
    float playerRealX = 9.0f * TILE_SZ, playerRealY = 8.0f * TILE_SZ;
    int playerAnimTicks = 0;
    std::string playerChar = "hirosi";
    bool playerTransparent = true;
    bool playerThrough = false;
    bool playerDirFix = false;
    float playerSpeed = 4.0f;
    bool menuDisabled = false;

    bool playerRouteActive = false;
    MoveRoute playerForcedRoute;
    size_t playerRouteIdx = 0;
    uint32_t lastPlayerRouteTick = 0;
    int playerRouteWaitFrames = 0;
    int playerJumpTimer = 0, playerJumpTotal = 0;

    bool timerWorking = false;
    int timerFrames = 0;

    int weatherType = 0, weatherPower = 0; // cmd 236 (lluvia en exteriores/ánexo)
    bool animActive = false;               // cmd 207 (ej: anim_bang.png en Map 021)
    int animTargetId = 0, animFrame = 0, animTotalFrames = 15;

    int toneR = 0, toneG = 0, toneB = 0;
    float curToneR = 0.0f, curToneG = 0.0f, curToneB = 0.0f;
    int toneFrames = 0;
    bool screenBlackout = false; // cmd 221 / 222
    int transPhase = 0;          // 0=normal, 1=fading out to transfer, 2=fading in from transfer/transition
    int fadeAlpha = 0;

    // Pila de ejecución del intérprete foreground (soporta Call Common Event 117 anidado)
    std::vector<InterpFrame> fgStack;
    int fgWaitFrames = 0;
    bool fgWaitForMove = false;

    // Cuadro de mensaje / elecciones
    bool msgActive = false;
    std::string msgFace;
    std::string msgSpeaker;
    std::string msgText;
    size_t msgCharReveal = 0; // Efecto typewriter de XRXS9 Window_Message
    int msgPos = 2;
    bool choiceActive = false;
    std::vector<std::string> choices;
    int choiceIndex = 0;
    int choiceIndent = 0;

    // Entrada de número de 3/4 dígitos (cmd 103 - Cajas fuertes / candados)
    bool numInputActive = false;
    int numInputVarId = 12;
    int numInputDigits = 4;
    int numInputCursor = 0;
    int numInputVals[8] = {0};

    // Espera de botón (cmd 105 - Armarios tansu / puzzles)
    bool btnInputActive = false;
    int btnInputVarId = 8;

    // Entrada de nombre del protagonista (cmd 303 - Teclado en pantalla + Easter Eggs oficiales)
    bool nameInputActive = false;
    int nameInputActorId = 2;
    int nameInputMaxLen = 10;
    std::string nameInputStr = "Hiroshi";
    int nameInputRow = 0; // Fila 0, Col 0 = letra 'A' para selección directa
    int nameInputCol = 0;
    int nameInputCooldown = 0;

    bool pendingTransfer = false;
    bool nextNoFade = false;
    int nextMapId = 0, nextX = 0, nextY = 0, nextDir = 0;
    uint32_t parBtnJustPressedMask = 0;
    bool gameOverTriggered = false;
    bool returnToTitleTriggered = false;
    uint32_t frameCount = 0;
} g_game;

static inline bool isPlayerMoving() {
    if (g_game.playerJumpTimer > 0) return true;
    float dx = std::fabs(g_game.playerRealX - g_game.playerX * (float)TILE_SZ);
    float dy = std::fabs(g_game.playerRealY - g_game.playerY * (float)TILE_SZ);
    return dx > 0.08f || dy > 0.08f;
}

static inline bool isEventMoving(const MapEvent& ev) {
    if (ev.jumpTimer > 0) return true;
    return std::fabs(ev.realX - ev.x * (float)TILE_SZ) > 0.05f ||
           std::fabs(ev.realY - ev.y * (float)TILE_SZ) > 0.05f;
}

static inline float rmxpSpeedPxPerFrame(float spd) {
    float s = std::max(1.0f, std::min(7.0f, spd));
    // Modo fijo a 60 FPS: px/frame = (2^s / 4) * (40 / 60) = (2^s) / 6.0f
    return std::pow(2.0f, s) / 6.0f;
}

static int rmDirToRow(int d) {
    if (d == 4) return 1;
    if (d == 6) return 2;
    if (d == 8) return 3;
    return 0;
}

static MapEvent* findEvent(int id) {
    for (auto& ev : g_game.events)
        if (ev.id == id) return &ev;
    return nullptr;
}

static inline bool isAoOniLethalEvent(const MapEvent& ev) {
    if (ev.activePage < 0) return false;
    if (ev.collisionAttack) return true;
    return ev.isBattler && !ev.through && !ev.char_name.empty();
}

static inline bool isAoOniBattlerEvent(const MapEvent& ev) {
    return ev.isBattler || ev.collisionAttack;
}

static bool isActorInParty(int actorId) {
    for (int id : g_game.partyActors) {
        if (id == actorId) return true;
    }
    return false;
}

static std::string resolveLeaderPartyChar() {
    for (int id : g_game.partyActors) {
        auto it = g_game.actorChars.find(id);
        if (it != g_game.actorChars.end() && !it->second.empty()) {
            if (id == 2 && g_game.switches[76] && g_game.actorChars.count(3) && !g_game.actorChars[3].empty()) {
                return g_game.actorChars[3];
            }
            return it->second;
        }
    }
    return "";
}

static void refreshPlayerPartyGraphic() {
    if (g_game.playerChar == "puzzle_player") return;
    g_game.playerChar = resolveLeaderPartyChar();
}

static void addPartyActor(int actorId) {
    if (actorId <= 0) return;
    if (!isActorInParty(actorId)) {
        g_game.partyActors.push_back(actorId);
    }
    refreshPlayerPartyGraphic();
}

static void removePartyActor(int actorId) {
    for (auto it = g_game.partyActors.begin(); it != g_game.partyActors.end(); ) {
        if (*it == actorId) it = g_game.partyActors.erase(it);
        else ++it;
    }
    refreshPlayerPartyGraphic();
}

static bool checkPageCond(int mapId, int evId, const EventPage& p) {
    // En XAS_BA (092_battler_attachment.rb), la página con variable_id == 25 es solo una etiqueta de metadatos para registrar el enemigo
    if (p.var_valid && p.var_id == 25) return false;
    if (p.sw1_valid && (p.sw1_id < 0 || p.sw1_id >= 512 || !g_game.switches[p.sw1_id])) return false;
    if (p.sw2_valid && (p.sw2_id < 0 || p.sw2_id >= 512 || !g_game.switches[p.sw2_id])) return false;
    if (p.var_valid && (p.var_id < 0 || p.var_id >= 512 || g_game.variables[p.var_id] < p.var_val)) return false;
    if (p.self_valid) {
        char key[64];
        snprintf(key, sizeof(key), "%d_%d_%s", mapId, evId, p.self_ch.c_str());
        if (!g_game.selfSwitches[key]) return false;
    }
    return true;
}

// Implementación 1:1 de Game_Event#refresh (021_Game_Event.rb:141-163) y Game_Character#move_type_custom (019_Game_Character_2.rb:322-344)
static std::string resolveAoOniCharName(const std::string& rawName) {
    if (rawName != "ao") return rawName;
    if (g_game.switches[76]) { // Modo SOUTHPARK
        return (g_game.variables[1] == 0) ? "ao_s" : "ao_s1";
    }
    if (g_game.switches[175]) { // Modo BLOCKMAN / Fuatti
        return "ao_h";
    }
    switch (g_game.variables[1]) {
        case 1: return "ao_mika";
        case 2: return "ao_takesi";
        case 3: return "ao_takurou";
        default: return "ao";
    }
}

static bool isTinyInteractiveSheetName(const std::string& name) {
    return name == "akari_switch2" || name == "179-Switch02" || name == "nejiana" ||
           name == "waretagaku" || name == "hankati" || name == "saraware" ||
           name == "ningyou_c" || name == "bou" || name == "oil" || name == "nawa" ||
           name == "ochitagaku" || name == "jokoueki_tana" || name == "3f_kami" ||
           name == "kinko_haikyo";
}

static bool eventHasAnyPickupCmd(const MapEvent& ev) {
    for (const auto& pg : ev.pages) {
        if (pg.trigger != 0 || pg.list.a.size() <= 1) continue;
        for (size_t i = 0; i < pg.list.a.size(); i++) {
            const JVal& cmd = pg.list[i];
            if (cmd.get("c").asInt(0) == 126 && cmd.get("p")[1].asInt(0) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool evalConditionCmd(int evId, const JVal& p);

static bool pageGrantsUncollectedItem(const MapEvent& ev, const EventPage& pg) {
    if (pg.trigger != 0 || pg.list.a.size() <= 1) return false;
    struct CondFrame { int indent; bool active; bool deterministic; };
    std::vector<CondFrame> stack;
    for (size_t i = 0; i < pg.list.a.size(); i++) {
        const JVal& cmd = pg.list[i];
        int c = cmd.get("c").asInt(0);
        int ind = cmd.get("i").asInt(0);
        const JVal& p = cmd.get("p");
        if (c == 111) {
            int ctype = p[0].asInt(0);
            // Solo ramas persistentes de estado de recolección (Interruptor != 49 persecución y Auto-Interruptor)
            bool det = (ctype == 0 && p[1].asInt(1) != 49) || (ctype == 2);
            bool active = det ? evalConditionCmd(ev.id, p) : true;
            stack.push_back({ind, active, det});
        } else if (c == 411) {
            for (size_t k = stack.size(); k > 0; k--) {
                if (stack[k - 1].indent == ind) {
                    stack[k - 1].active = stack[k - 1].deterministic ? !stack[k - 1].active : true;
                    break;
                }
            }
        } else if (c == 412) {
            while (!stack.empty() && stack.back().indent >= ind) {
                stack.pop_back();
            }
        } else if (c == 126 && p[1].asInt(0) == 0) {
            bool reachable = true;
            for (const auto& f : stack) {
                if (!f.active) { reachable = false; break; }
            }
            if (reachable) return true;
        }
    }
    return false;
}

static void updateEventHighlightFlags(MapEvent& ev) {
    if (ev.erased || ev.activePage < 0 || ev.activePage >= (int)ev.pages.size()) {
        ev.grantsItem = false;
        ev.isTinyInteractive = false;
        ev.isInteractable = false;
        return;
    }
    const EventPage& pg = ev.pages[ev.activePage];
    ev.isInteractable = (pg.trigger == 0 && pg.list.a.size() > 1);
    ev.grantsItem = pageGrantsUncollectedItem(ev, pg);
    if (eventHasAnyPickupCmd(ev) && !ev.grantsItem) {
        ev.isTinyInteractive = false;
    } else {
        ev.isTinyInteractive = (ev.char_name == "oil") ||
                               (ev.isInteractable && isTinyInteractiveSheetName(ev.char_name)) ||
                               (ev.grantsItem && ev.char_name.empty());
    }
}

static void refreshEvents() {
    for (auto& ev : g_game.events) {
        if (ev.erased) {
            ev.activePage = -1;
            ev.grantsItem = false;
            ev.isTinyInteractive = false;
            ev.isInteractable = false;
            ev.cachedSurf = nullptr;
            continue;
        }
        int found = -1;
        for (int i = (int)ev.pages.size() - 1; i >= 0; i--) {
            if (checkPageCond(g_game.mapId, ev.id, ev.pages[i])) {
                found = i;
                break;
            }
        }
        if (found == ev.activePage && found >= 0) {
            updateEventHighlightFlags(ev);
        }
        if (found != ev.activePage) {
            int prevWaitFrames = ev.routeWaitFrames;
            ev.activePage = found;
            ev.parIp = 0;
            ev.parWait = 0;
            ev.routeIdx = 0;
            ev.routeWaitFrames = 0;
            ev.collisionAttack = false;
            ev.grantsItem = false;
            ev.isTinyInteractive = false;
            ev.isInteractable = false;
            ev.cachedSurf = nullptr;
            if (found >= 0) {
                const auto& pg = ev.pages[found];
                ev.animTicks = 0;
                ev.char_name = resolveAoOniCharName(pg.char_name);
                if (!ev.char_name.empty()) {
                    ev.cachedSurf = getSurf(std::string("/app0/assets/images/char_") + ev.char_name + ".png");
                }
                ev.tile_id = pg.tile_id;
                ev.dir = pg.dir;
                ev.original_pattern = pg.pattern % 4;
                ev.pattern = ev.original_pattern;
                ev.walk_anime = pg.walk_anime;
                ev.opacity = pg.opacity;
                ev.through = pg.through;
                ev.always_top = pg.always_top;
                ev.dir_fix = pg.dir_fix;
                ev.step_anime = pg.step_anime;
                ev.move_type = pg.move_type;
                ev.move_speed = (float)pg.move_speed;
                ev.move_freq = pg.move_freq;
                updateEventHighlightFlags(ev);
                // Pre-aplicar modificadores iniciales de velocidad/ataque (code 45: @move_speed = 4.3, self.attack_on) al cambiar de página de IA (var[7])
                if (pg.move_type == 3 && !pg.move_route.list.empty()) {
                    while (ev.routeIdx < pg.move_route.list.size()) {
                        const auto& mc = pg.move_route.list[ev.routeIdx];
                        if (mc.code == 29) {
                            ev.move_speed = (float)mc.params[0].asNum(3.0);
                            ev.routeIdx++;
                        } else if (mc.code == 45) {
                            std::string s = mc.params[0].asStr();
                            float spd = 0.0f;
                            if (s.find("attack_on") != std::string::npos) {
                                ev.collisionAttack = true;
                                ev.routeIdx++;
                            } else if (s.find("attack_off") != std::string::npos) {
                                ev.collisionAttack = false;
                                ev.routeIdx++;
                            } else if (sscanf(s.c_str(), " @move_speed = %f", &spd) == 1 && spd > 0.1f) {
                                ev.move_speed = spd;
                                ev.routeIdx++;
                            } else {
                                break;
                            }
                        } else {
                            break;
                        }
                    }
                }
                // Conservar tiempo de gracia de aparición (cmd 202) si Ao Oni activa su página de persecución justo después
                if ((ev.collisionAttack || (ev.isBattler && !ev.through)) && !ev.char_name.empty()) {
                    ev.routeWaitFrames = prevWaitFrames;
                }
            } else {
                ev.char_name.clear();
                ev.tile_id = 0;
            }
        } else if (found >= 0 && ev.pages[found].char_name == "ao") {
            std::string resolved = resolveAoOniCharName("ao");
            if (resolved != ev.char_name) {
                ev.char_name = resolved;
                ev.cachedSurf = !ev.char_name.empty() ? getSurf(std::string("/app0/assets/images/char_") + ev.char_name + ".png") : nullptr;
            }
        }
    }
}

static void triggerPlayerDeathByAoOni() {
    if (!g_game.switches[150] && !g_game.switches[50]) {
        g_game.switches[50] = true;
        refreshEvents();
        triggerRumble(255, 255, 65);
        setPadLightbar(255, 0, 0);
    }
}

// Implementación 1:1 de Game_Map#passable?(x, y, d, self_event) en RGSS1 (con soporte de bits direccionales 0x01..0x08 y prioridades)
static bool isMapTilePassable(int tx, int ty, int d, int selfEvId = -1) {
    if (tx < 0 || ty < 0 || tx >= g_game.mapW || ty >= g_game.mapH) return false;
    uint8_t bit = (d == 2) ? 0x01 : ((d == 4) ? 0x02 : ((d == 6) ? 0x04 : ((d == 8) ? 0x08 : 0x00)));

    for (const auto& ev : g_game.events) {
        if (ev.id == selfEvId || ev.activePage < 0 || ev.through || ev.tile_id < 384) continue;
        if (ev.x == tx && ev.y == ty) {
            uint8_t p = ((size_t)ev.tile_id < g_game.passable.size()) ? g_game.passable[ev.tile_id] : 0;
            uint8_t pr = ((size_t)ev.tile_id < g_game.priorities.size()) ? g_game.priorities[ev.tile_id] : 0;
            if (bit != 0 && (p & bit) != 0) return false;
            if ((p & 0x0F) == 0x0F) return false;
            if (pr == 0) return true;
        }
    }

    for (int z = 2; z >= 0; z--) {
        int16_t tid = g_game.layers[tx + ty * g_game.mapW + z * g_game.mapW * g_game.mapH];
        if (tid < 0) return false;
        if (tid == 0) continue;
        uint8_t p = ((size_t)tid < g_game.passable.size()) ? g_game.passable[tid] : 0;
        uint8_t pr = ((size_t)tid < g_game.priorities.size()) ? g_game.priorities[tid] : 0;
        if (bit != 0 && (p & bit) != 0) return false;
        if ((p & 0x0F) == 0x0F) return false;
        if (pr == 0) return true;
    }
    return true;
}


// Implementación 1:1 de Game_Map#counter?(x, y) de RGSS1 (bit 0x80 en pasajes para mesas y mostradores)
static bool isMapTileCounter(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= g_game.mapW || ty >= g_game.mapH) return false;
    for (int z = 2; z >= 0; z--) {
        int16_t tid = g_game.layers[tx + ty * g_game.mapW + z * g_game.mapW * g_game.mapH];
        if (tid <= 0) continue;
        if ((size_t)tid < g_game.passable.size() && (g_game.passable[tid] & 0x80) == 0x80)
            return true;
    }
    return false;
}

static bool hasBlockingCharacterAt(int tx, int ty, bool isPlayer, int selfEvId = -1) {
    for (const auto& ev : g_game.events) {
        if (ev.id == selfEvId || ev.activePage < 0 || ev.through) continue;
        if (ev.x == tx && ev.y == ty && !ev.char_name.empty()) {
            int trig = ev.pages[ev.activePage].trigger;
            if (isPlayer && (trig == 1 || trig == 2)) continue;
            return true;
        }
    }
    if (!isPlayer && !g_game.playerThrough && g_game.playerX == tx && g_game.playerY == ty)
        return true;
    return false;
}

// Implementación 1:1 de Game_Character#passable?(x, y, d) con protección anti-atasco si el tile origen es 0x0F
static bool canPassMove(int fx, int fy, int d, bool isPlayer, int selfEvId = -1) {
    int nx = fx + (d == 4 ? -1 : (d == 6 ? 1 : 0));
    int ny = fy + (d == 8 ? -1 : (d == 2 ? 1 : 0));
    if (nx < 0 || ny < 0 || nx >= g_game.mapW || ny >= g_game.mapH) return false;
    if (hasBlockingCharacterAt(nx, ny, isPlayer, selfEvId)) return false;

    int oppDir = (d > 0) ? (10 - d) : 0;
    if (!isMapTilePassable(nx, ny, oppDir, selfEvId)) return false;
    // Solo exigir bit de salida del tile actual si el tile actual es transitable base (evita quedarse atrapado si un evento te coloca sobre un borde)
    if (isMapTilePassable(fx, fy, 0, selfEvId) && !isMapTilePassable(fx, fy, d, selfEvId)) return false;
    return true;
}

static bool canPassTile(int tx, int ty, bool isPlayer, int selfEvId = -1) {
    if (tx < 0 || ty < 0 || tx >= g_game.mapW || ty >= g_game.mapH) return false;
    if (hasBlockingCharacterAt(tx, ty, isPlayer, selfEvId)) return false;
    return isMapTilePassable(tx, ty, 0, selfEvId);
}

static MoveRoute parseMoveRoute(const JVal& mrObj) {
    MoveRoute mr;
    if (mrObj.type != JVal::T_OBJ) return mr;
    mr.repeat = mrObj.get("repeat").asBool(false);
    mr.skippable = mrObj.get("skippable").asBool(false);
    const JVal& lst = mrObj.get("list");
    for (size_t i = 0; i < lst.a.size(); i++) {
        MoveCmd mc;
        mc.code = lst[i].get("code").asInt(0);
        mc.params = lst[i].get("params");
        if (mc.code != 0) mr.list.push_back(mc);
    }
    return mr;
}

static bool stepCharacterDir(int& x, int& y, int& dir, int d, bool dirFix, bool through, bool isPlayer, int evId) {
    if (!dirFix) dir = d;
    int nx = x + (d == 4 ? -1 : (d == 6 ? 1 : 0));
    int ny = y + (d == 8 ? -1 : (d == 2 ? 1 : 0));
    if (!isPlayer && evId > 0 && nx == g_game.playerX && ny == g_game.playerY) {
        MapEvent* selfEv = findEvent(evId);
        if (selfEv && selfEv->activePage >= 0) {
            if (isAoOniLethalEvent(*selfEv)) {
                if (selfEv->routeWaitFrames <= 0 && !g_game.playerTransparent) {
                    triggerPlayerDeathByAoOni();
                }
            } else if (g_game.fgStack.empty() && !g_game.pendingTransfer) {
                const auto& pg = selfEv->pages[selfEv->activePage];
                if (pg.trigger == 2 && pg.list.a.size() > 1) {
                    g_game.fgStack.push_back({ pg.list, 0, selfEv->id, g_game.mapId });
                    g_game.fgWaitFrames = 0;
                    g_game.fgWaitForMove = false;
                }
            }
        }
    }
    if ((through && nx >= 0 && ny >= 0 && nx < g_game.mapW && ny < g_game.mapH) || canPassMove(x, y, d, isPlayer, evId)) {
        x = nx; y = ny;
        return true;
    }
    return false;
}

// Implementación 1:1 de SCRIPT [96] idou_route (move_toward_position / move_toward_event)
static bool stepTowardCoords(int& x, int& y, int& dir, int tx, int ty, bool dirFix, bool through, bool isPlayer, int evId) {
    int sx = x - tx;
    int sy = y - ty;
    if (sx == 0 && sy == 0) return false;
    int abs_sx = std::abs(sx);
    int abs_sy = std::abs(sy);
    if (abs_sx == abs_sy) {
        if (rand() % 2 == 0) abs_sx++; else abs_sy++;
    }
    if (abs_sx > abs_sy) {
        int d1 = (sx > 0) ? 4 : 6;
        int d2 = (sy > 0) ? 8 : 2;
        if (stepCharacterDir(x, y, dir, d1, dirFix, through, isPlayer, evId)) return true;
        if (sy != 0 && stepCharacterDir(x, y, dir, d2, dirFix, through, isPlayer, evId)) return true;
    } else {
        int d1 = (sy > 0) ? 8 : 2;
        int d2 = (sx > 0) ? 4 : 6;
        if (stepCharacterDir(x, y, dir, d1, dirFix, through, isPlayer, evId)) return true;
        if (sx != 0 && stepCharacterDir(x, y, dir, d2, dirFix, through, isPlayer, evId)) return true;
    }
    return false;
}

static bool stepTowardPlayer(MapEvent& ev) {
    return stepTowardCoords(ev.x, ev.y, ev.dir, g_game.playerX, g_game.playerY, ev.dir_fix, ev.through, false, ev.id);
}

static bool stepAwayFromPlayer(MapEvent& ev) {
    int sx = ev.x - g_game.playerX;
    int sy = ev.y - g_game.playerY;
    if (sx == 0 && sy == 0) return false;
    int abs_sx = std::abs(sx);
    int abs_sy = std::abs(sy);
    if (abs_sx == abs_sy) {
        if (rand() % 2 == 0) abs_sx++; else abs_sy++;
    }
    if (abs_sx > abs_sy) {
        int d1 = (sx > 0) ? 6 : 4;
        int d2 = (sy > 0) ? 2 : 8;
        if (stepCharacterDir(ev.x, ev.y, ev.dir, d1, ev.dir_fix, ev.through, false, ev.id)) return true;
        if (sy != 0 && stepCharacterDir(ev.x, ev.y, ev.dir, d2, ev.dir_fix, ev.through, false, ev.id)) return true;
    } else {
        int d1 = (sy > 0) ? 2 : 8;
        int d2 = (sx > 0) ? 6 : 4;
        if (stepCharacterDir(ev.x, ev.y, ev.dir, d1, ev.dir_fix, ev.through, false, ev.id)) return true;
        if (sx != 0 && stepCharacterDir(ev.x, ev.y, ev.dir, d2, ev.dir_fix, ev.through, false, ev.id)) return true;
    }
    return false;
}

// Devuelve true si el comando consume un paso/turno/espera de movimiento, o false si es instantáneo (código 27..45 modificador)
static bool applyMoveCmd(int targetId, const MoveCmd& mc) {
    int* px = (targetId == -1) ? &g_game.playerX : nullptr;
    int* py = (targetId == -1) ? &g_game.playerY : nullptr;
    int* pdir = (targetId == -1) ? &g_game.playerDir : nullptr;
    int* ppat = (targetId == -1) ? &g_game.playerPattern : nullptr;
    bool* pthr = (targetId == -1) ? &g_game.playerThrough : nullptr;
    bool* pdfix = (targetId == -1) ? &g_game.playerDirFix : nullptr;
    std::string* pchar = (targetId == -1) ? &g_game.playerChar : nullptr;

    MapEvent* ev = (targetId > 0) ? findEvent(targetId) : nullptr;
    if (targetId > 0 && !ev) return true;
    if (ev) {
        px = &ev->x; py = &ev->y; pdir = &ev->dir; ppat = &ev->pattern;
        pthr = &ev->through; pdfix = &ev->dir_fix; pchar = &ev->char_name;
    }
    if (!px) return true;

    bool walked = false;
    bool consumesStep = true;
    switch (mc.code) {
        case 1: walked = stepCharacterDir(*px, *py, *pdir, 2, *pdfix, *pthr, targetId == -1, targetId); break;
        case 2: walked = stepCharacterDir(*px, *py, *pdir, 4, *pdfix, *pthr, targetId == -1, targetId); break;
        case 3: walked = stepCharacterDir(*px, *py, *pdir, 6, *pdfix, *pthr, targetId == -1, targetId); break;
        case 4: walked = stepCharacterDir(*px, *py, *pdir, 8, *pdfix, *pthr, targetId == -1, targetId); break;
        case 5: (*px)--; (*py)++; if (!*pdfix) *pdir = 4; walked = true; break;
        case 6: (*px)++; (*py)++; if (!*pdfix) *pdir = 6; walked = true; break;
        case 7: (*px)--; (*py)--; if (!*pdfix) *pdir = 4; walked = true; break;
        case 8: (*px)++; (*py)--; if (!*pdfix) *pdir = 6; walked = true; break;
        case 9: walked = stepCharacterDir(*px, *py, *pdir, (rand() % 4 + 1) * 2, *pdfix, *pthr, targetId == -1, targetId); break;
        case 10: if (ev) { walked = stepTowardPlayer(*ev); } break;
        case 11: if (ev) { walked = stepAwayFromPlayer(*ev); } break;
        case 12: walked = stepCharacterDir(*px, *py, *pdir, *pdir, *pdfix, *pthr, targetId == -1, targetId); break;
        case 13: {
            int od = *pdir;
            int bd = (od == 2 ? 8 : (od == 8 ? 2 : (od == 4 ? 6 : 4)));
            walked = stepCharacterDir(*px, *py, *pdir, bd, true, *pthr, targetId == -1, targetId);
            *pdir = od;
            break;
        }
        case 14: { // Jump [dx, dy] con arco parabólico RMXP
            *px += mc.params[0].asInt(0);
            *py += mc.params[1].asInt(0);
            if (targetId == -1) {
                g_game.playerJumpTotal = 18;
                g_game.playerJumpTimer = 18;
            } else if (ev) {
                ev->jumpTotal = 18;
                ev->jumpTimer = 18;
            }
            walked = true;
            break;
        }
        case 15: { // Wait inside Move Route
            int wf = std::max(1, mc.params[0].asInt(1));
            wf = (wf * 3) / 2;
            if (targetId == -1) g_game.playerRouteWaitFrames = wf;
            else if (ev) ev->routeWaitFrames = wf;
            break;
        }
        case 16: *pdir = 2; consumesStep = false; break;
        case 17: *pdir = 4; consumesStep = false; break;
        case 18: *pdir = 6; consumesStep = false; break;
        case 19: *pdir = 8; consumesStep = false; break;
        case 25: if (ev) {
            int dx = g_game.playerX - ev->x, dy = g_game.playerY - ev->y;
            ev->dir = (std::abs(dx) >= std::abs(dy)) ? (dx >= 0 ? 6 : 4) : (dy >= 0 ? 2 : 8);
        } break;
        case 27: g_game.switches[mc.params[0].asInt(1)] = true; refreshEvents(); consumesStep = false; break;
        case 28:
            g_game.switches[mc.params[0].asInt(1)] = false;
            if (mc.params[0].asInt(1) == 49) g_game.menuDisabled = false;
            refreshEvents();
            consumesStep = false;
            break;
        case 29:
            if (targetId == -1) g_game.playerSpeed = (float)mc.params[0].asNum(4.0);
            else if (ev) ev->move_speed = (float)mc.params[0].asNum(3.0);
            consumesStep = false;
            break;
        case 31: if (ev) ev->walk_anime = true; consumesStep = false; break;
        case 32: if (ev) ev->walk_anime = false; consumesStep = false; break;
        case 33: if (ev) ev->step_anime = true; consumesStep = false; break;
        case 34: if (ev) ev->step_anime = false; consumesStep = false; break;
        case 35: *pdfix = true; consumesStep = false; break;
        case 36: *pdfix = false; consumesStep = false; break;
        case 37: *pthr = true; consumesStep = false; break;
        case 38: *pthr = false; consumesStep = false; break;
        case 39: if (ev) ev->always_top = true; consumesStep = false; break;
        case 40: if (ev) ev->always_top = false; consumesStep = false; break;
        case 41: // Change Graphic: [char_name, hue, dir, pattern]
            *pchar = resolveAoOniCharName(mc.params[0].asStr());
            if (ev) {
                ev->cachedSurf = !pchar->empty() ? getSurf(std::string("/app0/assets/images/char_") + *pchar + ".png") : nullptr;
                updateEventHighlightFlags(*ev);
            }
            if (targetId == -1) g_game.playerTransparent = pchar->empty();
            if (mc.params.a.size() >= 3 && mc.params[2].asInt(0) > 0) *pdir = mc.params[2].asInt(2);
            if (mc.params.a.size() >= 4) {
                *ppat = mc.params[3].asInt(0) % 4;
                if (ev) ev->original_pattern = *ppat;
            }
            consumesStep = false;
            break;
        case 42: if (ev) ev->opacity = mc.params[0].asInt(255); consumesStep = false; break;
        case 44: { // Play SE inside Move Route
            const JVal& af = mc.params[0];
            playSe(af.get("name").asStr(), af.get("volume").asInt(100), af.get("pitch").asInt(100));
            consumesStep = false;
            break;
        }
        case 45: { // Script inside Move Route (ej: @move_speed = 4.1, attack_on, move_toward_event, move_toward_position)
            std::string s = mc.params[0].asStr();
            float spd = 0.0f;
            int arg1 = 0, arg2 = 0;
            if (s.find("attack_on") != std::string::npos) {
                if (ev) {
                    ev->collisionAttack = true;
                    int dist = std::abs(ev->x - g_game.playerX) + std::abs(ev->y - g_game.playerY);
                    if (dist <= 6) {
                        ev->routeWaitFrames = std::max(ev->routeWaitFrames, 45);
                    }
                }
                consumesStep = false;
            } else if (s.find("attack_off") != std::string::npos) {
                if (ev) ev->collisionAttack = false;
                consumesStep = false;
            } else if (sscanf(s.c_str(), " @move_speed = %f", &spd) == 1 && spd > 0.1f) {
                if (targetId == -1) g_game.playerSpeed = spd;
                else if (ev) ev->move_speed = spd;
                consumesStep = false;
            } else if (sscanf(s.c_str(), " move_toward_event ( %d )", &arg1) == 1 ||
                       sscanf(s.c_str(), " move_toward_event(%d)", &arg1) == 1) {
                MapEvent* tev = findEvent(arg1);
                if (tev) walked = stepTowardCoords(*px, *py, *pdir, tev->x, tev->y, *pdfix, *pthr, targetId == -1, targetId);
                consumesStep = true;
            } else if (sscanf(s.c_str(), " move_toward_position ( %d , %d )", &arg1, &arg2) == 2 ||
                       sscanf(s.c_str(), " move_toward_position(%d, %d)", &arg1, &arg2) == 2 ||
                       sscanf(s.c_str(), " move_toward_position(%d,%d)", &arg1, &arg2) == 2) {
                walked = stepTowardCoords(*px, *py, *pdir, arg1, arg2, *pdfix, *pthr, targetId == -1, targetId);
                consumesStep = true;
            } else if (sscanf(s.c_str(), " turn_toward_event(%d)", &arg1) == 1) {
                MapEvent* tev = findEvent(arg1);
                if (tev) {
                    int sx = *px - tev->x, sy = *py - tev->y;
                    if (sx != 0 || sy != 0)
                        *pdir = (std::abs(sx) > std::abs(sy)) ? (sx > 0 ? 4 : 6) : (sy > 0 ? 8 : 2);
                }
                consumesStep = true;
            } else {
                consumesStep = false;
            }
            break;
        }
        default: consumesStep = false; break;
    }
    if (walked && *ppat == 0 && (!ev || ev->walk_anime)) *ppat = 1;
    return consumesStep;
}

static int rmxpSlot1AutotileSub(bool n, bool e, bool s, bool w, bool ne, bool se, bool sw, bool nw) {
    if (n && e && s && w) {
        return (nw ? 0 : 1) | (ne ? 0 : 2) | (se ? 0 : 4) | (sw ? 0 : 8);
    }
    if (!w && n && e && s) return 16 + (ne ? 0 : 1) + (se ? 0 : 2);
    if (!n && e && s && w) return 20 + (se ? 0 : 1) + (sw ? 0 : 2);
    if (!e && n && s && w) return 24 + (sw ? 0 : 1) + (nw ? 0 : 2);
    if (!s && n && e && w) return 28 + (nw ? 0 : 1) + (ne ? 0 : 2);
    if (!w && !e && n && s) return 32;
    if (!n && !s && e && w) return 33;
    if (!n && !w && e && s) return se ? 34 : 35;
    if (!n && !e && s && w) return sw ? 36 : 37;
    if (!s && !e && n && w) return nw ? 38 : 39;
    if (!s && !w && n && e) return ne ? 40 : 41;
    if (!n && !w && !e && s) return 42;
    if (!n && !w && !s && e) return 43;
    if (!w && !e && !s && n) return 44;
    if (!n && !e && !s && w) return 45;
    return 47;
}

static void maskIsolatedHoldingPens() {
    int w = g_game.mapW, h = g_game.mapH;
    g_game.maskedPenTiles.assign((size_t)w * (size_t)h, 0);
    if (strcmp(g_game.autotileFiles[6], "AT-hekitainai") != 0 || g_game.layers.size() < (size_t)w * (size_t)h * 3) {
        return;
    }
    bool isSlot1Heki = (strstr(g_game.autotileFiles[0], "heki") != nullptr);
    std::vector<uint8_t> visited((size_t)w * (size_t)h, 0);
    std::vector<uint8_t> affected((size_t)w * (size_t)h, 0);
    auto L0 = [&](int x, int y) -> int16_t { return g_game.layers[x + y * w]; };
    auto isSlot7 = [](int16_t t) -> bool { return t >= 336 && t < 384; };
    auto hasAoAt = [&](int x, int y) -> bool {
        for (const auto& ev : g_game.events) {
            if (ev.x == x && ev.y == y) {
                for (const auto& pg : ev.pages) {
                    if (pg.char_name.rfind("ao", 0) == 0) return true;
                }
            }
        }
        return false;
    };
    const int d4x[4] = {1, -1, 0, 0};
    const int d4y[4] = {0, 0, 1, -1};
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            if (visited[sy * w + sx] || !isSlot7(L0(sx, sy))) continue;
            std::vector<std::pair<int, int>> comp;
            std::vector<std::pair<int, int>> q;
            q.push_back({sx, sy});
            visited[sy * w + sx] = 1;
            bool touchesReal = false;
            bool hasAo = false;
            while (!q.empty()) {
                auto cur = q.back(); q.pop_back();
                comp.push_back(cur);
                if (hasAoAt(cur.first, cur.second)) hasAo = true;
                for (int k = 0; k < 4; k++) {
                    int nx = cur.first + d4x[k], ny = cur.second + d4y[k];
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        int16_t nt = L0(nx, ny);
                        if (isSlot7(nt)) {
                            if (!visited[ny * w + nx]) {
                                visited[ny * w + nx] = 1;
                                q.push_back({nx, ny});
                            }
                        } else if (!(nt >= 48 && nt < 96) && nt != 0) {
                            if (!(g_game.mapId == 70 && nt == 425)) {
                                touchesReal = true;
                            }
                        }
                    }
                }
            }
            if ((!touchesReal && comp.size() <= 32) || (!isSlot1Heki && comp.size() <= 16 && hasAo)) {
                int16_t fillTile = isSlot1Heki ? 48 : 425;
                for (const auto& c : comp) {
                    int mx = c.first, my = c.second;
                    g_game.maskedPenTiles[my * w + mx] = 1;
                    g_game.layers[mx + my * w] = fillTile;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            int ax = mx + dx, ay = my + dy;
                            if (ax >= 0 && ax < w && ay >= 0 && ay < h) {
                                affected[ay * w + ax] = 1;
                            }
                        }
                    }
                }
            }
        }
    }
    auto isSlot1OrOob = [&](int x, int y) -> bool {
        if (x < 0 || x >= w || y < 0 || y >= h) return true;
        int16_t t = L0(x, y);
        return t >= 48 && t < 96;
    };
    std::vector<int16_t> newSlot1((size_t)w * (size_t)h, -1);
    for (int ay = 0; ay < h; ay++) {
        for (int ax = 0; ax < w; ax++) {
            if (!affected[ay * w + ax]) continue;
            int16_t t = L0(ax, ay);
            if (t >= 48 && t < 96) {
                int sub = rmxpSlot1AutotileSub(
                    isSlot1OrOob(ax, ay - 1), isSlot1OrOob(ax + 1, ay), isSlot1OrOob(ax, ay + 1), isSlot1OrOob(ax - 1, ay),
                    isSlot1OrOob(ax + 1, ay - 1), isSlot1OrOob(ax + 1, ay + 1), isSlot1OrOob(ax - 1, ay + 1), isSlot1OrOob(ax - 1, ay - 1)
                );
                newSlot1[ay * w + ax] = (int16_t)(48 + sub);
            }
        }
    }
    for (int i = 0; i < w * h; i++) {
        if (newSlot1[i] >= 0) g_game.layers[i] = newSlot1[i];
    }
}

static void buildMapCacheSurfaces() {
    if (g_game.mapLowerSurf) { SDL_FreeSurface(g_game.mapLowerSurf); g_game.mapLowerSurf = nullptr; }
    if (g_game.mapUpperSurf) { SDL_FreeSurface(g_game.mapUpperSurf); g_game.mapUpperSurf = nullptr; }
    g_game.prioTiles.clear();

    maskIsolatedHoldingPens();

    int pw = g_game.mapW * TILE_SZ;
    int ph = g_game.mapH * TILE_SZ;
    g_game.mapLowerSurf = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_RGBA32);
    g_game.mapUpperSurf = SDL_CreateRGBSurfaceWithFormat(0, pw, ph, 32, SDL_PIXELFORMAT_RGBA32);
    if (!g_game.mapLowerSurf || !g_game.mapUpperSurf) return;

    memset(g_game.mapLowerSurf->pixels, 0, g_game.mapLowerSurf->pitch * ph);
    memset(g_game.mapUpperSurf->pixels, 0, g_game.mapUpperSurf->pitch * ph);

    SDL_Surface* tsSurf = g_game.tilesetFile[0] ? getSurf(std::string("/app0/assets/images/ts_") + g_game.tilesetFile + ".png") : nullptr;
    SDL_Surface* atSurfs[7] = {nullptr};
    for (int i = 0; i < 7; i++)
        if (g_game.autotileFiles[i][0])
            atSurfs[i] = getSurf(std::string("/app0/assets/images/at_") + g_game.autotileFiles[i] + ".png");

    for (int z = 0; z < 3; z++) {
        for (int ty = 0; ty < g_game.mapH; ty++) {
            for (int tx = 0; tx < g_game.mapW; tx++) {
                int16_t tid = g_game.layers[tx + ty * g_game.mapW + z * g_game.mapW * g_game.mapH];
                if (tid <= 0) continue;
                uint8_t prio = ((size_t)tid < g_game.priorities.size()) ? g_game.priorities[tid] : 0;
                SDL_Surface* target = (prio > 0) ? g_game.mapUpperSurf : g_game.mapLowerSurf;
                if (prio > 0) {
                    g_game.prioTiles.push_back({ tx, ty, tid, calcTileScreenZ(ty, prio) });
                }
                int dx = tx * TILE_SZ, dy = ty * TILE_SZ;

                if (tid >= 384 && tsSurf) {
                    int tIndex = tid - 384;
                    SDL_Rect src = { (tIndex % 8) * TILE_SZ, (tIndex / 8) * TILE_SZ, TILE_SZ, TILE_SZ };
                    SDL_Rect dst = { dx, dy, TILE_SZ, TILE_SZ };
                    blitSurf(tsSurf, &src, target, &dst, 255, false);
                } else if (tid >= 48 && tid < 384) {
                    int atIdx = (tid / 48) - 1;
                    int pat = tid % 48;
                    if (atIdx >= 0 && atIdx < 7 && atSurfs[atIdx])
                        blitAutotileToCache(atSurfs[atIdx], pat, target, dx, dy);
                }
            }
        }
    }
    std::stable_sort(g_game.prioTiles.begin(), g_game.prioTiles.end(), [](const GameRuntime::PrioTile& a, const GameRuntime::PrioTile& b) {
        return a.z < b.z;
    });
}

static bool loadMap(int mapId) {
    char binPath[256], jsonPath[256];
    snprintf(binPath, sizeof(binPath), "/app0/assets/misc/map_%03d.bin", mapId);
    snprintf(jsonPath, sizeof(jsonPath), "/app0/assets/misc/map_%03d.json", mapId);

    FILE* f = fopen(binPath, "rb");
    if (!f) return false;
    uint32_t magic = 0, w = 0, h = 0, tsId = 0, passCount = 0, prioCount = 0;
    fread(&magic, 4, 1, f);
    if (magic != 0x4D504F42) { fclose(f); return false; }
    fread(&w, 4, 1, f);
    fread(&h, 4, 1, f);
    fread(&tsId, 4, 1, f);
    fread(g_game.tilesetFile, 1, 64, f);
    for (int i = 0; i < 7; i++) fread(g_game.autotileFiles[i], 1, 64, f);

    g_game.mapId = mapId;
    g_game.mapW = (int)w;
    g_game.mapH = (int)h;
    g_game.tilesetId = (int)tsId;
    g_game.layers.resize(w * h * 3);
    fread(g_game.layers.data(), sizeof(int16_t), w * h * 3, f);

    fread(&passCount, 4, 1, f);
    g_game.passable.resize(passCount);
    if (passCount > 0) fread(g_game.passable.data(), 1, passCount, f);
    fread(&prioCount, 4, 1, f);
    g_game.priorities.resize(prioCount);
    if (prioCount > 0) fread(g_game.priorities.data(), 1, prioCount, f);
    fclose(f);

    g_game.events.clear();
    JVal meta;
    if (loadJsonFile(jsonPath, meta)) {
        if (meta.get("autoplay_bgm").asBool(false)) {
            const JVal& b = meta.get("bgm");
            playBgm(b.get("name").asStr(), b.get("volume").asInt(100), b.get("pitch").asInt(100));
        }
        g_game.autoplayBgs = meta.get("autoplay_bgs").asBool(false);
        if (g_game.autoplayBgs) {
            const JVal& bs = meta.get("bgs");
            g_game.mapBgsName = bs.get("name").asStr();
            g_game.mapBgsVol = bs.get("volume").asInt(100);
            g_game.mapBgsPitch = bs.get("pitch").asInt(100);
            playBgs(g_game.mapBgsName, g_game.mapBgsVol, g_game.mapBgsPitch);
        } else {
            g_game.mapBgsName.clear();
        }
        const JVal& evs = meta.get("events");
        for (size_t i = 0; i < evs.a.size(); i++) {
            const JVal& je = evs[i];
            MapEvent ev;
            ev.id = je.get("id").asInt(0);
            ev.name = je.get("name").asStr();
            ev.x = je.get("x").asInt(0);
            ev.y = je.get("y").asInt(0);
            ev.realX = (float)(ev.x * TILE_SZ);
            ev.realY = (float)(ev.y * TILE_SZ);
            const JVal& pgs = je.get("pages");
            for (size_t p = 0; p < pgs.a.size(); p++) {
                const JVal& jp = pgs[p];
                EventPage pg;
                pg.sw1_valid = jp.get("sw1_valid").asBool(); pg.sw1_id = jp.get("sw1_id").asInt(1);
                pg.sw2_valid = jp.get("sw2_valid").asBool(); pg.sw2_id = jp.get("sw2_id").asInt(1);
                pg.var_valid = jp.get("var_valid").asBool(); pg.var_id = jp.get("var_id").asInt(1); pg.var_val = jp.get("var_val").asInt(0);
                pg.self_valid = jp.get("self_valid").asBool(); pg.self_ch = jp.get("self_ch").asStr();
                pg.char_name = jp.get("char_name").asStr();
                pg.dir = jp.get("dir").asInt(2);
                pg.pattern = jp.get("pattern").asInt(0);
                pg.opacity = jp.get("opacity").asInt(255);
                pg.tile_id = jp.get("tile_id").asInt(0);
                pg.move_type = jp.get("move_type").asInt(0);
                pg.move_speed = jp.get("move_speed").asInt(3);
                pg.move_freq = jp.get("move_freq").asInt(3);
                pg.move_route = parseMoveRoute(jp.get("move_route"));
                pg.walk_anime = jp.get("walk_anime").asBool(true);
                pg.step_anime = jp.get("step_anime").asBool(false);
                pg.dir_fix = jp.get("dir_fix").asBool(false);
                pg.through = jp.get("through").asBool(false);
                pg.always_top = jp.get("always_top").asBool(false);
                pg.trigger = jp.get("trigger").asInt(0);
                pg.list = jp.get("list");
                if (pg.var_valid && pg.var_id == 25) ev.isBattler = true;
                ev.pages.push_back(pg);
            }
            g_game.events.push_back(ev);
        }
    }
    // Reiniciar el estado de waypoints de IA de Ao Oni (var[7]) al entrar a cada sala
    g_game.variables[7] = 0;
    refreshEvents();
    buildMapCacheSurfaces();

    // Limpiar marcos residuales de mapas previos que ya solo tenían el marcador final {"c":0}
    while (!g_game.fgStack.empty()) {
        InterpFrame& top = g_game.fgStack.back();
        while (top.ip < top.list.a.size() && top.list[top.ip].get("c").asInt(0) == 0) {
            top.ip++;
        }
        if (top.ip >= top.list.a.size()) g_game.fgStack.pop_back();
        else break;
    }

    // Si la pila está libre y el nuevo mapa tiene un evento Autorun (trigger == 3), encolarlo en el acto (Frame 0)
    if (g_game.fgStack.empty()) {
        for (const auto& mev : g_game.events) {
            if (mev.activePage < 0) continue;
            const auto& pg = mev.pages[mev.activePage];
            if (pg.trigger == 3 && pg.list.a.size() > 1) {
                g_game.fgStack.push_back({ pg.list, 0, mev.id, g_game.mapId });
                g_game.fgWaitFrames = 0;
                g_game.fgWaitForMove = false;
                break;
            }
        }
    }

    printf("[AoOni] Mapa %03d cargado y cacheado (%dx%d, eventos=%zu)\n", mapId, g_game.mapW, g_game.mapH, g_game.events.size());
    return true;
}

static void formatDialogText(const std::string& raw, std::string& faceOut, std::string& speakerOut, std::string& cleanOut) {
    std::string s = raw;
    // Normalizar códigos con espacios de la traducción al español (Map035: "\ C [2]", "\ _ ")
    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
        size_t p = 0;
        while ((p = str.find(from, p)) != std::string::npos) {
            str.replace(p, from.size(), to);
            p += to.size();
        }
    };
    replaceAll(s, "\\ C [", "\\C[");
    replaceAll(s, "\\ c [", "\\c[");
    replaceAll(s, "\\ _ ", "");
    replaceAll(s, "\\ _", "");

    for (int pass = 0; pass < 3; pass++) {
        size_t pos = 0;
        while ((pos = s.find("\\N[", pos)) != std::string::npos || (pos = s.find("\\n[", pos)) != std::string::npos) {
            size_t end = s.find(']', pos + 3);
            if (end == std::string::npos) break;
            int id = atoi(s.substr(pos + 3, end - (pos + 3)).c_str());
            std::string aname = g_game.actorNames.count(id) ? g_game.actorNames[id] : "Hiroshi";
            s.replace(pos, end - pos + 1, aname);
        }
        pos = 0;
        while ((pos = s.find("\\V[", pos)) != std::string::npos || (pos = s.find("\\v[", pos)) != std::string::npos) {
            size_t end = s.find(']', pos + 3);
            if (end == std::string::npos) break;
            std::string inner = s.substr(pos + 3, end - (pos + 3));
            if (!inner.empty() && (inner[0] == 'i' || inner[0] == 'I')) {
                int itemId = atoi(inner.substr(1).c_str());
                std::string iname = g_game.itemDefs.count(itemId) ? g_game.itemDefs[itemId].name : ("Item #" + std::to_string(itemId));
                s.replace(pos, end - pos + 1, iname);
            } else {
                int id = atoi(inner.c_str());
                int val = (id >= 0 && id < 512) ? g_game.variables[id] : 0;
                s.replace(pos, end - pos + 1, std::to_string(val));
            }
        }
    }

    size_t fpos = s.find("\\f[");
    if (fpos != std::string::npos) {
        size_t fend = s.find(']', fpos + 3);
        if (fend != std::string::npos) {
            std::string baseFace = s.substr(fpos + 3, fend - (fpos + 3));
            // Implementación 1:1 de 093_XRXS9msg.rb:281-297:
            // switch 47 ("HIROSI") = retrato clásico 1.0; switch 76 ("SOUTHPARK") = *s.png; por defecto 6.23 = *2.png
            if (g_game.switches[47]) {
                faceOut = baseFace;
            } else if (g_game.switches[76]) {
                std::string spFace = baseFace + "s";
                faceOut = getSurf(std::string("/app0/assets/images/pic_") + spFace + ".png") ? spFace : baseFace;
            } else {
                std::string v6Face = baseFace + "2";
                faceOut = getSurf(std::string("/app0/assets/images/pic_") + v6Face + ".png") ? v6Face : baseFace;
            }
            s.erase(fpos, fend - fpos + 1);
        }
    }
    size_t npos = s.find("\\name[");
    if (npos != std::string::npos) {
        size_t nend = s.find(']', npos + 6);
        if (nend != std::string::npos) {
            speakerOut = s.substr(npos + 6, nend - (npos + 6));
            s.erase(npos, nend - npos + 1);
        }
    }
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char nc = s[i + 1];
            if (nc == '!' || nc == '.' || nc == '>' || nc == '<' || nc == '^' || nc == '_') { i++; continue; }
            if ((nc == 'C' || nc == 'c') && i + 2 < s.size() && s[i + 2] == '[') {
                size_t ce = s.find(']', i + 3);
                if (ce != std::string::npos) { i = ce; continue; }
            }
        }
        out.push_back(s[i]);
    }
    cleanOut = out;
}

static bool evalSingleTerm(const std::string& rawExpr) {
    std::string expr = rawExpr;
    while (!expr.empty() && (expr.front() == ' ' || expr.front() == '(')) expr.erase(expr.begin());
    while (!expr.empty() && (expr.back() == ' ' || expr.back() == ')')) expr.pop_back();

    int evId = 0, val = 0, varId = 0, modVal = 0;
    char prop[16] = {0}, op[8] = {0};
    if (sscanf(expr.c_str(), "$game_variables[%d] %% %d %7[<>=!] %d", &varId, &modVal, op, &val) == 4 && modVal != 0) {
        int lhs = ((varId >= 0 && varId < 512) ? g_game.variables[varId] : 0) % modVal;
        if (strcmp(op, "==") == 0) return lhs == val;
        if (strcmp(op, "!=") == 0) return lhs != val;
        if (strcmp(op, ">=") == 0) return lhs >= val;
        if (strcmp(op, "<=") == 0) return lhs <= val;
        if (strcmp(op, ">") == 0)  return lhs > val;
        if (strcmp(op, "<") == 0)  return lhs < val;
    }
    if (sscanf(expr.c_str(), "$game_map.events[%d].%15[xy] %7[<>=!] %d", &evId, prop, op, &val) == 4) {
        MapEvent* ev = findEvent(evId);
        int lhs = ev ? (prop[0] == 'x' ? ev->x : ev->y) : -999;
        if (strcmp(op, "==") == 0) return lhs == val;
        if (strcmp(op, "!=") == 0) return lhs != val;
        if (strcmp(op, ">=") == 0) return lhs >= val;
        if (strcmp(op, "<=") == 0) return lhs <= val;
        if (strcmp(op, ">") == 0)  return lhs > val;
        if (strcmp(op, "<") == 0)  return lhs < val;
    }
    int evId2 = 0; char prop2[16] = {0};
    if (sscanf(expr.c_str(), "$game_player.%15[xy] %7[<>=!] $game_map.events[%d].%15[xy]", prop, op, &evId2, prop2) == 4) {
        MapEvent* ev = findEvent(evId2);
        int lhs = (prop[0] == 'x') ? g_game.playerX : g_game.playerY;
        int rhs = ev ? (prop2[0] == 'x' ? ev->x : ev->y) : -999;
        if (strcmp(op, "==") == 0) return lhs == rhs;
        if (strcmp(op, "!=") == 0) return lhs != rhs;
        if (strcmp(op, ">=") == 0) return lhs >= rhs;
        if (strcmp(op, "<=") == 0) return lhs <= rhs;
        if (strcmp(op, ">") == 0)  return lhs > rhs;
        if (strcmp(op, "<") == 0)  return lhs < rhs;
    }
    if (sscanf(expr.c_str(), "$game_player.%15[xy] %7[<>=!] %d", prop, op, &val) == 3) {
        int lhs = (prop[0] == 'x') ? g_game.playerX : g_game.playerY;
        if (strcmp(op, "==") == 0) return lhs == val;
        if (strcmp(op, "!=") == 0) return lhs != val;
        if (strcmp(op, ">=") == 0) return lhs >= val;
        if (strcmp(op, "<=") == 0) return lhs <= val;
        if (strcmp(op, ">") == 0)  return lhs > val;
        if (strcmp(op, "<") == 0)  return lhs < val;
    }
    if (sscanf(expr.c_str(), "$game_variables[%d] %7[<>=!] %d", &varId, op, &val) == 3) {
        int lhs = (varId >= 0 && varId < 512) ? g_game.variables[varId] : 0;
        if (strcmp(op, "==") == 0) return lhs == val;
        if (strcmp(op, "!=") == 0) return lhs != val;
        if (strcmp(op, ">=") == 0) return lhs >= val;
        if (strcmp(op, "<=") == 0) return lhs <= val;
        if (strcmp(op, ">") == 0)  return lhs > val;
        if (strcmp(op, "<") == 0)  return lhs < val;
    }
    return false;
}

static bool evalRubyCond(const std::string& rawExpr) {
    std::string expr = rawExpr;
    while (!expr.empty() && expr.front() == ' ') expr.erase(expr.begin());
    while (!expr.empty() && expr.back() == ' ') expr.pop_back();
    if (expr.size() >= 2 && expr.front() == '(' && expr.back() == ')') {
        int depth = 0;
        bool wrapsAll = true;
        for (size_t i = 0; i < expr.size() - 1; i++) {
            if (expr[i] == '(') depth++;
            else if (expr[i] == ')') depth--;
            if (depth == 0) { wrapsAll = false; break; }
        }
        if (wrapsAll) expr = expr.substr(1, expr.size() - 2);
    }
    int depth = 0;
    for (size_t i = 0; i + 4 <= expr.size(); i++) {
        if (expr[i] == '(') depth++;
        else if (expr[i] == ')') depth--;
        else if (depth == 0 && expr.compare(i, 4, " or ") == 0)
            return evalRubyCond(expr.substr(0, i)) || evalRubyCond(expr.substr(i + 4));
    }
    depth = 0;
    for (size_t i = 0; i + 5 <= expr.size(); i++) {
        if (expr[i] == '(') depth++;
        else if (expr[i] == ')') depth--;
        else if (depth == 0 && expr.compare(i, 5, " and ") == 0)
            return evalRubyCond(expr.substr(0, i)) && evalRubyCond(expr.substr(i + 5));
    }
    return evalSingleTerm(expr);
}

static bool evalConditionCmd(int evId, const JVal& p) {
    int ctype = p[0].asInt(0);
    switch (ctype) {
        case 0: {
            int swId = p[1].asInt(1);
            bool want = (p[2].asInt(0) == 0);
            bool cur = (swId >= 0 && swId < 512) ? g_game.switches[swId] : false;
            return cur == want;
        }
        case 1: {
            int vId = p[1].asInt(1);
            int lhs = (vId >= 0 && vId < 512) ? g_game.variables[vId] : 0;
            int rhs = (p[2].asInt(0) == 0) ? p[3].asInt(0) : g_game.variables[p[3].asInt(0)];
            int op = p[4].asInt(0);
            if (op == 0) return lhs == rhs;
            if (op == 1) return lhs >= rhs;
            if (op == 2) return lhs <= rhs;
            if (op == 3) return lhs > rhs;
            if (op == 4) return lhs < rhs;
            if (op == 5) return lhs != rhs;
            return false;
        }
        case 2: {
            std::string ch = p[1].asStr();
            bool want = (p[2].asInt(0) == 0);
            char key[64];
            snprintf(key, sizeof(key), "%d_%d_%s", g_game.mapId, evId, ch.c_str());
            return g_game.selfSwitches[key] == want;
        }
        case 3: {
            int sec = p[1].asInt(0);
            int cmp = p[2].asInt(0);
            int curSec = g_game.timerFrames / 60;
            return (cmp == 0) ? (curSec >= sec) : (curSec <= sec);
        }
        case 4: {
            int aId = p[1].asInt(1);
            int sub = p[2].asInt(0);
            if (sub == 0) return isActorInParty(aId);
            if (sub == 1) return g_game.actorNames[aId] == p[3].asStr();
            return false;
        }
        case 6: {
            int charId = p[1].asInt(-1);
            if (charId == 0) charId = evId;
            int wantDir = p[2].asInt(2);
            if (charId == -1) return g_game.playerDir == wantDir;
            MapEvent* ev = findEvent(charId);
            return ev ? (ev->dir == wantDir) : false;
        }
        case 8: {
            int itemId = p[1].asInt(1);
            return g_game.inventory[itemId] > 0;
        }
        case 11:
            return false;
        case 12:
            return evalRubyCond(p[1].asStr());
        default:
            return false;
    }
}

static void execControlVars(int evId, const JVal& p) {
    int sId = p[0].asInt(1), eId = p[1].asInt(1);
    int op = p[2].asInt(0), opType = p[3].asInt(0);
    int val = 0;
    if (opType == 0) val = p[4].asInt(0);
    else if (opType == 1) val = g_game.variables[p[4].asInt(0)];
    else if (opType == 2) {
        int lo = p[4].asInt(0), hi = p[5].asInt(0);
        val = (hi >= lo) ? (lo + rand() % (hi - lo + 1)) : lo;
    } else if (opType == 3) {
        val = g_game.inventory[p[4].asInt(1)];
    } else if (opType == 5) {
        int cid = p[4].asInt(-1), prop = p[5].asInt(0);
        if (cid == 0) cid = evId;
        if (cid == -1) {
            if (prop == 0) val = g_game.playerX;
            else if (prop == 1) val = g_game.playerY;
            else if (prop == 2) val = g_game.playerDir;
        } else {
            MapEvent* ev = findEvent(cid);
            if (ev) {
                if (prop == 0) val = ev->x;
                else if (prop == 1) val = ev->y;
                else if (prop == 2) val = ev->dir;
            }
        }
    } else if (opType == 7) {
        int other = p[4].asInt(0);
        if (other == 0) val = g_game.mapId;
        // En RGSS (Interpreter 4, when 4): Play Time = Graphics.frame_count / Graphics.frame_rate (segundos)
        else if (other == 4) val = (int)(g_game.frameCount / 60);
        else if (other == 5) val = g_game.timerFrames / 60;
    }
    for (int id = sId; id <= eId; id++) {
        if (id < 0 || id >= 512) continue;
        int& dst = g_game.variables[id];
        if (op == 0) dst = val;
        else if (op == 1) dst += val;
        else if (op == 2) dst -= val;
        else if (op == 3) dst *= val;
        else if (op == 4 && val != 0) dst /= val;
        else if (op == 5 && val != 0) dst %= val;
    }
    refreshEvents();
}

static void runInterpreterSlice(const JVal& list, size_t& ip, int& waitFrames, int evId, bool isForeground, bool* outExited = nullptr) {
    int startPage = -2;
    if (!isForeground && evId > 0) {
        MapEvent* selfEv = findEvent(evId);
        if (!selfEv || selfEv->erased || selfEv->activePage < 0) return;
        startPage = selfEv->activePage;
    }
    if (isForeground && g_game.fgWaitForMove) {
        bool anyMoving = g_game.playerRouteActive;
        for (const auto& ev : g_game.events) if (ev.routeActive) anyMoving = true;
        if (anyMoving) return;
        g_game.fgWaitForMove = false;
    }
    int steps = 0;
    while (ip < list.a.size() && steps < 1024) {
        if (!isForeground && evId > 0) {
            MapEvent* selfEv = findEvent(evId);
            if (!selfEv || selfEv->erased || selfEv->activePage != startPage) return;
        }
        steps++;
        if (waitFrames > 0) { waitFrames--; return; }
        if (isForeground && (g_game.msgActive || g_game.choiceActive || g_game.numInputActive || g_game.btnInputActive || g_game.nameInputActive || g_game.pendingTransfer || g_game.transPhase == 1))
            return;
        if (isForeground && g_game.fgWaitForMove) {
            bool anyMoving = g_game.playerRouteActive;
            for (const auto& ev : g_game.events) if (ev.routeActive) anyMoving = true;
            if (anyMoving) return;
            g_game.fgWaitForMove = false;
        }

        const JVal& cmd = list[ip];
        int code = cmd.get("c").asInt(0);
        int indent = cmd.get("i").asInt(0);
        const JVal& p = cmd.get("p");

        switch (code) {
            case 101: { // Show Text
                if (!isForeground) { ip++; break; }
                g_game.msgFace.clear();
                g_game.msgSpeaker.clear();
                std::string lines = p[0].asStr();
                ip++;
                while (ip < list.a.size() && list[ip].get("c").asInt(0) == 401) {
                    lines += "\n" + list[ip].get("p")[0].asStr();
                    ip++;
                }
                formatDialogText(lines, g_game.msgFace, g_game.msgSpeaker, g_game.msgText);
                int msgTextRelX = 32;
                if (!g_game.msgFace.empty()) {
                    SDL_Surface* faceSurf = getSurf(std::string("/app0/assets/images/pic_") + g_game.msgFace + ".png");
                    if (faceSurf) msgTextRelX = 26 + faceSurf->w + 14;
                }
                int maxMsgW = (GAME_W - 30) - msgTextRelX;
                g_game.msgText = wrapTextToWidth(g_game.msgText, maxMsgW, 2);
                g_game.msgCharReveal = 0;
                g_game.msgActive = true;
                if (ip < list.a.size() && list[ip].get("c").asInt(0) == 102) {
                    const JVal& cp = list[ip].get("p");
                    g_game.choices.clear();
                    for (size_t k = 0; k < cp[0].a.size(); k++) {
                        std::string fDummy, sDummy, cClean;
                        formatDialogText(cp[0][k].asStr(), fDummy, sDummy, cClean);
                        g_game.choices.push_back(cClean);
                    }
                    g_game.choiceIndex = 0;
                    g_game.choiceIndent = list[ip].get("i").asInt(0);
                    g_game.choiceActive = true;
                    g_game.msgCharReveal = g_game.msgText.size();
                    ip++;
                }
                return;
            }
            case 102: { // Show Choices
                if (!isForeground) { ip++; break; }
                g_game.choices.clear();
                for (size_t k = 0; k < p[0].a.size(); k++) {
                    std::string fDummy, sDummy, cClean;
                    formatDialogText(p[0][k].asStr(), fDummy, sDummy, cClean);
                    g_game.choices.push_back(cClean);
                }
                g_game.choiceIndex = 0;
                g_game.choiceIndent = indent;
                g_game.choiceActive = true;
                ip++;
                return;
            }
            case 402: { // When [choice]
                while (ip < list.a.size()) {
                    if (list[ip].get("i").asInt(0) == indent && list[ip].get("c").asInt(0) == 404) {
                        ip++; break;
                    }
                    ip++;
                }
                break;
            }
            case 103: { // Input Number (Cajas fuertes / candados)
                if (!isForeground) { ip++; break; }
                g_game.numInputVarId = p[0].asInt(12);
                g_game.numInputDigits = std::max(1, std::min(8, p[1].asInt(4)));
                g_game.numInputCursor = 0;
                memset(g_game.numInputVals, 0, sizeof(g_game.numInputVals));
                g_game.numInputActive = true;
                ip++;
                return;
            }
            case 104: // Change Text Options
                g_game.msgPos = p[0].asInt(2);
                ip++;
                break;
            case 105: { // Button Input Processing (Armarios tansu / CE 25 / Puzzle Map 60)
                if (!isForeground) {
                    int rmCode = 0;
                    int hitBit = -1;
                    const uint32_t m = g_game.parBtnJustPressedMask;
                    if (m & (1u << PAD_CIRCLE))        { hitBit = PAD_CIRCLE;   rmCode = 12; }
                    else if (m & (1u << PAD_TRIANGLE)) { hitBit = PAD_TRIANGLE; rmCode = 12; }
                    else if (m & (1u << PAD_CROSS))    { hitBit = PAD_CROSS;    rmCode = 13; }
                    else if (m & (1u << PAD_OPTIONS))  { hitBit = PAD_OPTIONS;  rmCode = 13; }
                    else if (m & (1u << PAD_SQUARE))   { hitBit = PAD_SQUARE;   rmCode = 11; }
                    else if (m & (1u << PAD_DOWN))     { hitBit = PAD_DOWN;     rmCode = 2; }
                    else if (m & (1u << PAD_LEFT))     { hitBit = PAD_LEFT;     rmCode = 4; }
                    else if (m & (1u << PAD_RIGHT))    { hitBit = PAD_RIGHT;    rmCode = 6; }
                    else if (m & (1u << PAD_UP))       { hitBit = PAD_UP;       rmCode = 8; }
                    if (rmCode == 0) return;
                    g_game.parBtnJustPressedMask &= ~(1u << hitBit);
                    int varId = p[0].asInt(8);
                    if (varId >= 0 && varId < 512) g_game.variables[varId] = rmCode;
                    refreshEvents();
                    ip++;
                    break;
                }
                g_game.btnInputVarId = p[0].asInt(8);
                g_game.btnInputActive = true;
                ip++;
                return;
            }
            case 106: // Wait
                waitFrames = (p[0].asInt(1) * 3) / 2;
                ip++;
                return;
            case 111: { // Conditional Branch
                bool ok = evalConditionCmd(evId, p);
                if (ok) {
                    ip++;
                } else {
                    ip++;
                    while (ip < list.a.size()) {
                        int ci = list[ip].get("i").asInt(0);
                        int cc = list[ip].get("c").asInt(0);
                        if (ci == indent && (cc == 411 || cc == 412)) {
                            ip++; break;
                        }
                        ip++;
                    }
                }
                break;
            }
            case 411: { // Else
                ip++;
                while (ip < list.a.size()) {
                    if (list[ip].get("i").asInt(0) == indent && list[ip].get("c").asInt(0) == 412) {
                        ip++; break;
                    }
                    ip++;
                }
                break;
            }
            case 112: // Loop
                ip++;
                break;
            case 413: { // Repeat Above (RGSS1 1:1: repite hacia el 112 coincidente)
                int depth = 0;
                while (ip > 0) {
                    ip--;
                    int c = list[ip].get("c").asInt(0);
                    int ci = list[ip].get("i").asInt(0);
                    if (c == 413) depth++;
                    else if (c == 112) {
                        if (depth == 0 && ci == indent) break;
                        depth--;
                    }
                }
                ip++;
                break;
            }
            case 113: { // Break Loop (RGSS1 1:1: rompe el bucle contenedor más cercano donde 413 tiene indent < break_indent)
                int depth = 0;
                while (ip < list.a.size()) {
                    ip++;
                    if (ip >= list.a.size()) break;
                    int c = list[ip].get("c").asInt(0);
                    int ci = list[ip].get("i").asInt(0);
                    if (c == 112) {
                        depth++;
                    } else if (c == 413) {
                        if (depth == 0 && ci < indent) {
                            ip++; // Saltar el 413 correspondiente y reanudar en la instrucción siguiente
                            break;
                        }
                        depth--;
                    }
                }
                break;
            }
            case 115: // Exit Event Processing
                ip = list.a.size();
                if (outExited) *outExited = true;
                if (isForeground) {
                    for (auto& fr : g_game.fgStack)
                        if (fr.evId == evId) fr.ip = fr.list.a.size();
                }
                return;
            case 116: { // Erase Event
                MapEvent* ev = findEvent(evId);
                if (ev) { ev->erased = true; ev->activePage = -1; }
                ip = list.a.size();
                if (outExited) *outExited = true;
                if (isForeground) {
                    for (auto& fr : g_game.fgStack)
                        if (fr.evId == evId) fr.ip = fr.list.a.size();
                }
                return;
            }
            case 117: { // Call Common Event
                int ceId = p[0].asInt(0);
                ip++;
                if (!isForeground && ceId == 1) {
                    // CE 1 (gameover) llamado desde evento paralelo (ej: CE 25 armario cuando Ao Oni te descubre)
                    triggerPlayerDeathByAoOni();
                    ip = list.a.size();
                    if (outExited) *outExited = true;
                    return;
                }
                for (auto& ce : g_game.commonEvents) {
                    if (ce.id == ceId) {
                        if (isForeground) {
                            g_game.fgStack.push_back({ ce.list, 0, evId, g_game.mapId });
                            return;
                        } else {
                            size_t subIp = 0; int subWait = 0; bool subExited = false;
                            runInterpreterSlice(ce.list, subIp, subWait, evId, false, &subExited);
                            if (subExited) {
                                ip = list.a.size();
                                if (outExited) *outExited = true;
                                return;
                            }
                        }
                        break;
                    }
                }
                break;
            }
            case 121: { // Control Switches
                int sId = p[0].asInt(1), eId = p[1].asInt(1);
                bool val = (p[2].asInt(0) == 0);
                for (int id = sId; id <= eId; id++) {
                    if (id >= 0 && id < 512) {
                        if (id == 50 && val && !g_game.switches[50]) {
                            triggerRumble(255, 255, 65);
                            setPadLightbar(255, 0, 0);
                        }
                        if (id == 49 && !val) {
                            g_game.menuDisabled = false; // Re-habilitar menú al finalizar persecución
                        }
                        g_game.switches[id] = val;
                    }
                }
                refreshEvents();
                ip++;
                break;
            }
            case 122:
                execControlVars(evId, p);
                ip++;
                break;
            case 123: { // Control Self Switch
                std::string ch = p[0].asStr();
                bool val = (p[1].asInt(0) == 0);
                char key[64];
                snprintf(key, sizeof(key), "%d_%d_%s", g_game.mapId, evId, ch.c_str());
                g_game.selfSwitches[key] = val;
                refreshEvents();
                ip++;
                break;
            }
            case 124: { // Control Timer
                if (p[0].asInt(0) == 0) {
                    g_game.timerWorking = true;
                    g_game.timerFrames = p[1].asInt(0) * 60;
                } else {
                    g_game.timerWorking = false;
                }
                ip++;
                break;
            }
            case 126: { // Change Items
                int itemId = p[0].asInt(1);
                int op = p[1].asInt(0);
                int amt = (p[2].asInt(0) == 0) ? p[3].asInt(1) : g_game.variables[p[3].asInt(0)];
                if (op == 0) g_game.inventory[itemId] += amt;
                else g_game.inventory[itemId] = std::max(0, g_game.inventory[itemId] - amt);
                refreshEvents();
                ip++;
                break;
            }
            case 129: { // Change Party Member (RMXP 1:1: mantiene al líder como sprite del jugador)
                int actorId = p[0].asInt(1);
                int op = p[1].asInt(0);
                if (op == 0) {
                    addPartyActor(actorId);
                    if (!g_game.playerChar.empty() && g_game.mapId != 75) {
                        g_game.playerTransparent = false;
                    }
                } else {
                    removePartyActor(actorId);
                }
                ip++;
                break;
            }
            case 135:
                g_game.menuDisabled = (p[0].asInt(0) == 0);
                ip++;
                break;
            case 201: { // Transfer Player
                if (p[0].asInt(0) == 0) {
                    g_game.nextMapId = p[1].asInt(1);
                    g_game.nextX = p[2].asInt(0);
                    g_game.nextY = p[3].asInt(0);
                    g_game.nextDir = p[4].asInt(0);
                } else {
                    g_game.nextMapId = g_game.variables[p[1].asInt(0)];
                    g_game.nextX = g_game.variables[p[2].asInt(0)];
                    g_game.nextY = g_game.variables[p[3].asInt(0)];
                    g_game.nextDir = p[4].asInt(0);
                }
                // En Interpreter 5 (command_201) de Ao Oni: añade 1 segundo al timer al cambiar de sala
                if (g_game.timerWorking) g_game.timerFrames += 60;
                g_game.nextNoFade = (p.a.size() >= 6 && p[5].asInt(0) == 1);
                g_game.pendingTransfer = true;
                ip++;
                while (ip < list.a.size() && list[ip].get("c").asInt(0) == 0) ip++;
                return;
            }
            case 202: { // Set Event Location
                int tId = p[0].asInt(0);
                if (tId == 0) tId = evId;
                MapEvent* tev = findEvent(tId);
                if (tev) {
                    if (p[1].asInt(0) == 0) {
                        tev->x = p[2].asInt(0);
                        tev->y = p[3].asInt(0);
                    } else if (p[1].asInt(0) == 1) {
                        tev->x = g_game.variables[p[2].asInt(0)];
                        tev->y = g_game.variables[p[3].asInt(0)];
                    } else if (p[1].asInt(0) == 2) {
                        MapEvent* oev = findEvent(p[2].asInt(0));
                        if (oev) {
                            std::swap(tev->x, oev->x);
                            std::swap(tev->y, oev->y);
                            oev->realX = (float)(oev->x * TILE_SZ);
                            oev->realY = (float)(oev->y * TILE_SZ);
                        }
                    }
                    tev->realX = (float)(tev->x * TILE_SZ);
                    tev->realY = (float)(tev->y * TILE_SZ);
                    if (p[4].asInt(0) > 0) tev->dir = p[4].asInt(2);
                    // Ventana de gracia para emboscada/aparición de Ao Oni (ej. sala del piano, agujero de suelo, entradas):
                    if (isAoOniBattlerEvent(*tev)) {
                        int dist = std::abs(tev->x - g_game.playerX) + std::abs(tev->y - g_game.playerY);
                        // Si spawnea encima o a <= 2 casillas del jugador, reubicar en una casilla transitable segura (>= 3)
                        if (dist <= 2) {
                            int bestX = tev->x, bestY = tev->y;
                            bool foundSafe = false;
                            for (int wantDist = 3; wantDist <= 6 && !foundSafe; wantDist++) {
                                int bestScore = 999999;
                                for (int dy = -wantDist; dy <= wantDist; dy++) {
                                    for (int dx = -wantDist; dx <= wantDist; dx++) {
                                        if (std::abs(dx) + std::abs(dy) != wantDist) continue;
                                        int cx = g_game.playerX + dx;
                                        int cy = g_game.playerY + dy;
                                        if (!canPassTile(cx, cy, false, tev->id)) continue;
                                        int score = std::abs(cx - tev->x) + std::abs(cy - tev->y);
                                        if (score < bestScore) {
                                            bestScore = score;
                                            bestX = cx;
                                            bestY = cy;
                                            foundSafe = true;
                                        }
                                    }
                                }
                            }
                            if (foundSafe) {
                                tev->x = bestX;
                                tev->y = bestY;
                            }
                            tev->realX = (float)(tev->x * TILE_SZ);
                            tev->realY = (float)(tev->y * TILE_SZ);
                            dist = std::abs(tev->x - g_game.playerX) + std::abs(tev->y - g_game.playerY);
                        }
                        if (dist <= 7) {
                            tev->routeWaitFrames = std::max(tev->routeWaitFrames, 80);
                            triggerRumble(240, 180, 25);
                            setPadLightbar(255, 0, 50);
                        }
                    }
                }
                ip++;
                break;
            }
            case 207: { // Show Animation (ej: [targetId, animId] en Map 021 Ev 2)
                int tId = p[0].asInt(0);
                if (tId == 0) tId = evId;
                g_game.animActive = true;
                g_game.animTargetId = tId;
                g_game.animFrame = 0;
                g_game.animTotalFrames = 15; // 5 frames x 3 ticks a 60 FPS
                ip++;
                break;
            }
            case 208: // Change Transparent Flag
                g_game.playerTransparent = (p[0].asInt(0) == 0);
                ip++;
                break;
            case 209: { // Set Move Route
                int tId = p[0].asInt(-1);
                if (tId == 0) tId = evId;
                MoveRoute mr = parseMoveRoute(p[1]);
                if (tId == -1) {
                    g_game.playerForcedRoute = mr;
                    g_game.playerRouteIdx = 0;
                    g_game.playerRouteWaitFrames = 0;
                    g_game.playerRouteActive = !mr.list.empty();
                    // Ejecutar en el acto los modificadores instantáneos (cambio de gráfico/velocidad/through) como en RGSS1 force_move_route
                    if (g_game.playerRouteActive && !isPlayerMoving()) {
                        int guard = 0;
                        while (g_game.playerRouteIdx < g_game.playerForcedRoute.list.size() && guard++ < 16) {
                            bool consumes = applyMoveCmd(-1, g_game.playerForcedRoute.list[g_game.playerRouteIdx++]);
                            if (consumes || g_game.playerRouteWaitFrames > 0 || isPlayerMoving()) break;
                        }
                        if (g_game.playerRouteIdx >= g_game.playerForcedRoute.list.size() && g_game.playerRouteWaitFrames <= 0 && !isPlayerMoving()) {
                            if (g_game.playerForcedRoute.repeat) g_game.playerRouteIdx = 0;
                            else g_game.playerRouteActive = false;
                        }
                    }
                } else {
                    MapEvent* tev = findEvent(tId);
                    if (tev) {
                        tev->forcedRoute = mr;
                        tev->routeIdx = 0;
                        tev->routeWaitFrames = 0;
                        tev->routeActive = !mr.list.empty();
                        if (tev->routeActive && !isEventMoving(*tev)) {
                            int guard = 0;
                            while (tev->routeIdx < tev->forcedRoute.list.size() && guard++ < 16) {
                                bool consumes = applyMoveCmd(tev->id, tev->forcedRoute.list[tev->routeIdx++]);
                                if (consumes || tev->routeWaitFrames > 0 || isEventMoving(*tev)) break;
                            }
                            if (tev->routeIdx >= tev->forcedRoute.list.size() && tev->routeWaitFrames <= 0 && !isEventMoving(*tev)) {
                                if (tev->forcedRoute.repeat) tev->routeIdx = 0;
                                else tev->routeActive = false;
                            }
                        }
                    }
                }
                ip++;
                break;
            }
            case 210: // Wait for Move's Completion
                if (isForeground) g_game.fgWaitForMove = true;
                ip++;
                return;
            case 221: // Prepare for Transition
                g_game.screenBlackout = true;
                g_game.fadeAlpha = 255;
                g_game.transPhase = 0;
                ip++;
                break;
            case 222: // Execute Transition
                g_game.screenBlackout = false;
                g_game.fadeAlpha = 255;
                g_game.transPhase = 2;
                ip++;
                break;
            case 223: { // Change Screen Color Tone
                const JVal& t = p[0];
                g_game.toneR = (int)t.get("r").asNum(0);
                g_game.toneG = (int)t.get("g").asNum(0);
                g_game.toneB = (int)t.get("b").asNum(0);
                int dur = (p.a.size() >= 2) ? p[1].asInt(0) : 0;
                if (dur <= 0) {
                    g_game.curToneR = (float)g_game.toneR;
                    g_game.curToneG = (float)g_game.toneG;
                    g_game.curToneB = (float)g_game.toneB;
                    g_game.toneFrames = 0;
                } else {
                    g_game.toneFrames = std::max(1, (dur * 3) / 2);
                }
                ip++;
                break;
            }
            case 231: { // Show Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 50) {
                    Picture& pic = g_game.pictures[picId];
                    pic.active = true;
                    pic.name = p[1].asStr();
                    pic.cachedSurf = pic.name.empty() ? nullptr : getSurf(std::string("/app0/assets/images/pic_") + pic.name + ".png");
                    pic.origin = p[2].asInt(0);
                    float px = (p[3].asInt(0) == 0) ? (float)p[4].asNum(0) : (float)g_game.variables[p[4].asInt(0)];
                    float py = (p[3].asInt(0) == 0) ? (float)p[5].asNum(0) : (float)g_game.variables[p[5].asInt(0)];
                    pic.x = pic.targetX = px;
                    pic.y = pic.targetY = py;
                    pic.zoomX = pic.targetZoomX = (float)p[6].asNum(100.0);
                    pic.zoomY = pic.targetZoomY = (float)p[7].asNum(100.0);
                    pic.opacity = pic.targetOpacity = (float)p[8].asNum(255.0);
                    pic.angle = 0.0f;
                    pic.rotSpeed = 0.0f;
                    pic.moveFrames = 0;
                }
                ip++;
                break;
            }
            case 232: { // Move Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 50 && g_game.pictures[picId].active) {
                    Picture& pic = g_game.pictures[picId];
                    int dur = std::max(1, (std::max(1, p[1].asInt(1)) * 3) / 2);
                    pic.origin = p[2].asInt(0);
                    pic.targetX = (p[3].asInt(0) == 0) ? (float)p[4].asNum(0) : (float)g_game.variables[p[4].asInt(0)];
                    pic.targetY = (p[3].asInt(0) == 0) ? (float)p[5].asNum(0) : (float)g_game.variables[p[5].asInt(0)];
                    pic.targetZoomX = (float)p[6].asNum(100.0);
                    pic.targetZoomY = (float)p[7].asNum(100.0);
                    pic.targetOpacity = (float)p[8].asNum(255.0);
                    pic.moveFrames = dur;
                }
                ip++;
                break;
            }
            case 233: { // Rotate Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 50 && g_game.pictures[picId].active) {
                    g_game.pictures[picId].rotSpeed = (float)p[1].asNum(0.0);
                }
                ip++;
                break;
            }
            case 235: { // Erase Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 50) {
                    g_game.pictures[picId].active = false;
                    g_game.pictures[picId].cachedSurf = nullptr;
                }
                ip++;
                break;
            }
            case 236: { // Set Weather Effects (0=Ninguno, 1=Lluvia)
                g_game.weatherType = p[0].asInt(0);
                g_game.weatherPower = p[1].asInt(0);
                ip++;
                break;
            }
            case 241: { // Play BGM (con soporte de Zenith RGSS15 098_fadein.rb FADE_VARIABLE = 10)
                const JVal& af = p[0];
                int fadeSec = (g_game.variables[10] > 0) ? g_game.variables[10] : 0;
                g_game.variables[10] = 0;
                playBgm(af.get("name").asStr(), af.get("volume").asInt(100), af.get("pitch").asInt(100), fadeSec);
                ip++;
                break;
            }
            case 242: // Fade Out BGM
                fadeOutBgm(p[0].asInt(1));
                ip++;
                break;
            case 245: { // Play BGS (con soporte de Zenith RGSS15 098_fadein.rb FADE_VARIABLE = 10)
                const JVal& af = p[0];
                int fadeSec = (g_game.variables[10] > 0) ? g_game.variables[10] : 0;
                g_game.variables[10] = 0;
                playBgs(af.get("name").asStr(), af.get("volume").asInt(100), af.get("pitch").asInt(100), fadeSec);
                ip++;
                break;
            }
            case 246: // Fade Out BGS
                fadeOutBgs(p[0].asInt(1));
                ip++;
                break;
            case 247: // Memorize BGM/BGS
                pthread_mutex_lock(&g_audioMutex);
                g_savedBgm = g_bgmTrack;
                g_savedBgs = g_bgsTrack;
                pthread_mutex_unlock(&g_audioMutex);
                ip++;
                break;
            case 248: // Restore BGM/BGS
                if (g_savedBgm.active && !g_savedBgm.name.empty())
                    playBgm(g_savedBgm.name, g_savedBgm.volume, g_savedBgm.origPitch);
                else
                    playBgm("");
                if (g_savedBgs.active && !g_savedBgs.name.empty())
                    playBgs(g_savedBgs.name, g_savedBgs.volume, g_savedBgs.origPitch);
                else
                    playBgs("");
                ip++;
                break;
            case 249: // Play ME
            case 250: { // Play SE
                const JVal& af = p[0];
                playSe(af.get("name").asStr(), af.get("volume").asInt(100), af.get("pitch").asInt(100));
                ip++;
                break;
            }
            case 251: // Stop SE
                stopAllSe();
                ip++;
                break;
            case 303: { // Name Input Processing (Teclado interactivo + Easter Eggs oficiales de Ao Oni)
                if (!isForeground) { ip++; break; }
                g_game.nameInputActorId = p[0].asInt(2);
                g_game.nameInputMaxLen = std::max(1, std::min(12, p[1].asInt(10)));
                g_game.nameInputStr = g_game.actorNames.count(g_game.nameInputActorId)
                                      ? g_game.actorNames[g_game.nameInputActorId]
                                      : "Hiroshi";
                if (g_game.nameInputStr.empty()) g_game.nameInputStr = "Hiroshi";
                g_game.nameInputRow = 0; // Iniciar en 'A' (Fila 0, Col 0) para que el jugador elija letras libremente
                g_game.nameInputCol = 0;
                g_game.nameInputCooldown = 8; // Debounce de 8 frames solo para evitar confirmar accidentalmente con X
                g_game.nameInputActive = true;
                ip++;
                return;
            }
            case 320: // Change Actor Name
                g_game.actorNames[p[0].asInt(2)] = p[1].asStr();
                ip++;
                break;
            case 353: // Game Over
                triggerRumble(255, 255, 65);
                setPadLightbar(255, 0, 0);
                g_game.gameOverTriggered = true;
                ip = list.a.size();
                return;
            case 354: // Return to Title Screen
                g_game.returnToTitleTriggered = true;
                ip = list.a.size();
                return;
            case 355: { // Script
                std::string s = p[0].asStr();
                while (ip + 1 < list.a.size() && list[ip + 1].get("c").asInt(0) == 655) {
                    ip++;
                    s += "\n" + list[ip].get("p")[0].asStr();
                }
                if (s.find("Graphics.frame_count = 0") != std::string::npos) {
                    g_game.frameCount = 0;
                } else if (s.find("$game_variables[45]") != std::string::npos) {
                    int totalSec = g_game.variables[15];
                    g_game.variables[45] = totalSec / 3600;
                    g_game.variables[46] = (totalSec / 60) % 60;
                    g_game.variables[47] = totalSec % 60;
                }
                ip++;
                break;
            }
            default:
                ip++;
                break;
        }
    }
}

static void updatePictures() {
    for (int i = 1; i <= 50; i++) {
        Picture& pic = g_game.pictures[i];
        if (!pic.active) continue;
        if (std::fabs(pic.rotSpeed) > 0.001f) {
            pic.angle += pic.rotSpeed * 0.5f;
        }
        if (pic.moveFrames <= 0) continue;
        float d = (float)pic.moveFrames;
        pic.x = (pic.x * (d - 1.0f) + pic.targetX) / d;
        pic.y = (pic.y * (d - 1.0f) + pic.targetY) / d;
        pic.zoomX = (pic.zoomX * (d - 1.0f) + pic.targetZoomX) / d;
        pic.zoomY = (pic.zoomY * (d - 1.0f) + pic.targetZoomY) / d;
        pic.opacity = (pic.opacity * (d - 1.0f) + pic.targetOpacity) / d;
        pic.moveFrames--;
    }
    if (g_game.animActive) {
        g_game.animFrame++;
        if (g_game.animFrame >= g_game.animTotalFrames) {
            g_game.animActive = false;
        }
    }
    if (g_game.toneFrames > 0) {
        float d = (float)g_game.toneFrames;
        g_game.curToneR = (g_game.curToneR * (d - 1.0f) + (float)g_game.toneR) / d;
        g_game.curToneG = (g_game.curToneG * (d - 1.0f) + (float)g_game.toneG) / d;
        g_game.curToneB = (g_game.curToneB * (d - 1.0f) + (float)g_game.toneB) / d;
        g_game.toneFrames--;
    } else {
        g_game.curToneR = (float)g_game.toneR;
        g_game.curToneG = (float)g_game.toneG;
        g_game.curToneB = (float)g_game.toneB;
    }
}

static float getEffectivePlayerStepPx(bool sprinting) {
    float effPlayerSpeed = g_game.playerSpeed;
    if (sprinting && !g_game.playerRouteActive && g_game.fgStack.empty()) {
        effPlayerSpeed = std::min(5.25f, g_game.playerSpeed + 0.75f);
    }
    return rmxpSpeedPxPerFrame(effPlayerSpeed);
}

// Avanza la posición sub-píxel del jugador hacia la casilla destino actual y devuelve el remanente de píxeles del frame
static float advancePlayerSubPixel(bool sprinting) {
    float ptx = (float)(g_game.playerX * TILE_SZ);
    float pty = (float)(g_game.playerY * TILE_SZ);
    float pspd = getEffectivePlayerStepPx(sprinting);
    float pdx = ptx - g_game.playerRealX;
    float pdy = pty - g_game.playerRealY;
    float dist = std::max(std::fabs(pdx), std::fabs(pdy));
    if (dist <= 0.25f) {
        g_game.playerRealX = ptx;
        g_game.playerRealY = pty;
        return 0.0f;
    }
    if (std::fabs(pdx) <= pspd) g_game.playerRealX = ptx;
    else g_game.playerRealX += (pdx > 0.0f ? pspd : -pspd);
    if (std::fabs(pdy) <= pspd) g_game.playerRealY = pty;
    else g_game.playerRealY += (pdy > 0.0f ? pspd : -pspd);
    if (std::fabs(ptx - g_game.playerRealX) < 0.25f) g_game.playerRealX = ptx;
    if (std::fabs(pty - g_game.playerRealY) < 0.25f) g_game.playerRealY = pty;
    return (pspd > dist) ? (pspd - dist) : 0.0f;
}

static void applyCarryoverPlayerSubPixel(float remPx) {
    if (remPx <= 0.001f) return;
    float ptx = (float)(g_game.playerX * TILE_SZ);
    float pty = (float)(g_game.playerY * TILE_SZ);
    float pdx = ptx - g_game.playerRealX;
    float pdy = pty - g_game.playerRealY;
    if (std::fabs(pdx) <= remPx) g_game.playerRealX = ptx;
    else g_game.playerRealX += (pdx > 0.0f ? remPx : -remPx);
    if (std::fabs(pdy) <= remPx) g_game.playerRealY = pty;
    else g_game.playerRealY += (pdy > 0.0f ? remPx : -remPx);
}

static void updateWalkAndEventsSmooth(bool sprinting, bool holdingMoveInput) {
    if (g_game.playerJumpTimer > 0) g_game.playerJumpTimer--;
    static int s_idleGraceFrames = 0;
    if (isPlayerMoving() || holdingMoveInput) {
        s_idleGraceFrames = 3;
        g_game.playerAnimTicks++;
        int pStride = sprinting ? 4 : 6;
        if (g_game.playerAnimTicks >= pStride) {
            g_game.playerAnimTicks = 0;
            g_game.playerPattern = (g_game.playerPattern + 1) % 4;
        }
    } else {
        if (s_idleGraceFrames > 0) {
            s_idleGraceFrames--;
        } else {
            g_game.playerAnimTicks = 0;
            g_game.playerPattern = 0;
        }
    }

    // Interpolación suave a 60 FPS de todos los eventos del mapa (con pausa XAS_BA_BATTLEEVENT_NONPREEMPT para Ao Oni en diálogos)
    bool nonPreemptPause = !g_game.fgStack.empty() || g_game.msgActive || g_game.choiceActive ||
                           g_game.numInputActive || g_game.btnInputActive || g_game.nameInputActive || g_game.pendingTransfer;
    for (auto& ev : g_game.events) {
        if (ev.activePage < 0) continue;
        if (ev.jumpTimer > 0) ev.jumpTimer--;
        bool isAoBattler = isAoOniBattlerEvent(ev);
        if (!nonPreemptPause || ev.routeActive || !isAoBattler) {
            float etx = (float)(ev.x * TILE_SZ);
            float ety = (float)(ev.y * TILE_SZ);
            float espd = rmxpSpeedPxPerFrame(ev.move_speed);
            float edx = etx - ev.realX;
            float edy = ety - ev.realY;
            if (std::fabs(edx) <= espd) ev.realX = etx;
            else ev.realX += (edx > 0.0f ? espd : -espd);
            if (std::fabs(edy) <= espd) ev.realY = ety;
            else ev.realY += (edy > 0.0f ? espd : -espd);
        }

        // Actualización de animación 1:1 RPG Maker XP (018_Game_Character_1.rb:137-166)
        bool moving = isEventMoving(ev);
        int unit = 4;
        int thresh = std::max(6, (int)std::lround((18.0f - ev.move_speed * 2.0f) * 6.0f));
        int inc = 0;
        if (moving) {
            if (ev.walk_anime) inc = (unit * 3) / 2;
            else if (ev.step_anime) inc = unit;
        } else {
            if (ev.step_anime) inc = unit;
            else if (ev.pattern != ev.original_pattern) inc = (unit * 3) / 2;
        }
        if (inc > 0) {
            ev.animTicks += inc;
            if (ev.animTicks > thresh) {
                ev.animTicks = 0;
                if (!ev.step_anime && !moving) {
                    ev.pattern = ev.original_pattern;
                } else {
                    ev.pattern = (ev.pattern + 1) % 4;
                }
            }
        } else {
            ev.animTicks = 0;
        }
    }
}

static void updateMoveRoutes(uint32_t now) {
    if (g_game.playerRouteActive) {
        if (!isPlayerMoving()) {
            if (g_game.playerRouteWaitFrames > 0) {
                g_game.playerRouteWaitFrames--;
            } else {
                int guard = 0;
                while (g_game.playerRouteIdx < g_game.playerForcedRoute.list.size() && guard++ < 16) {
                    bool consumes = applyMoveCmd(-1, g_game.playerForcedRoute.list[g_game.playerRouteIdx++]);
                    if (consumes || g_game.playerRouteWaitFrames > 0 || isPlayerMoving()) break;
                }
                if (g_game.playerRouteIdx >= g_game.playerForcedRoute.list.size() && g_game.playerRouteWaitFrames <= 0 && !isPlayerMoving()) {
                    if (g_game.playerForcedRoute.repeat) g_game.playerRouteIdx = 0;
                    else g_game.playerRouteActive = false;
                }
            }
        }
    }
    for (auto& ev : g_game.events) {
        if (ev.activePage < 0) continue;
        if (ev.routeActive) {
            if (!isEventMoving(ev)) {
                if (ev.routeWaitFrames > 0) {
                    ev.routeWaitFrames--;
                } else {
                    int guard = 0;
                    while (ev.routeIdx < ev.forcedRoute.list.size() && guard++ < 16) {
                        bool consumes = applyMoveCmd(ev.id, ev.forcedRoute.list[ev.routeIdx++]);
                        if (consumes || ev.routeWaitFrames > 0 || isEventMoving(ev)) break;
                    }
                    if (ev.routeIdx >= ev.forcedRoute.list.size() && ev.routeWaitFrames <= 0 && !isEventMoving(ev)) {
                        if (ev.forcedRoute.repeat) ev.routeIdx = 0;
                        else ev.routeActive = false;
                    }
                }
            }
        } else if (g_game.fgStack.empty() && !g_game.btnInputActive && !g_game.msgActive && !g_game.choiceActive &&
                   !g_game.numInputActive && !g_game.nameInputActive && ev.move_type > 0 && !isEventMoving(ev)) {
            if (ev.routeWaitFrames > 0) {
                ev.routeWaitFrames--;
                continue;
            }
            // Con move_freq == 5 (persecución de Ao Oni), avanza de forma continua tile a tile sin pausa
            uint32_t delay = (ev.move_freq >= 5) ? 0u : (uint32_t)std::max(40, (5 - ev.move_freq) * 180);
            if (now - ev.lastAutoMoveTick >= delay) {
                ev.lastAutoMoveTick = now;
                if (ev.move_type == 1) {
                    if (stepCharacterDir(ev.x, ev.y, ev.dir, (rand() % 4 + 1) * 2, ev.dir_fix, ev.through, false, ev.id) &&
                        ev.walk_anime && ev.pattern == 0)
                        ev.pattern = 1;
                } else if (ev.move_type == 2) {
                    if (stepTowardPlayer(ev) && ev.walk_anime && ev.pattern == 0)
                        ev.pattern = 1;
                } else if (ev.move_type == 3) {
                    const auto& mr = ev.pages[ev.activePage].move_route;
                    if (!mr.list.empty()) {
                        int guard = 0;
                        while (guard++ < 16) {
                            const auto& mc = mr.list[ev.routeIdx % mr.list.size()];
                            ev.routeIdx++;
                            bool consumes = applyMoveCmd(ev.id, mc);
                            if (consumes || ev.routeWaitFrames > 0 || isEventMoving(ev)) break;
                        }
                    }
                }
            }
        }
    }
}

static void startForegroundEvent(int evId, const JVal& list) {
    g_game.fgStack.clear();
    g_game.fgStack.push_back({ list, 0, evId, g_game.mapId });
    g_game.fgWaitFrames = 0;
    g_game.fgWaitForMove = false;
}

static void initGameDb() {
    JVal db;
    if (!loadJsonFile("/app0/assets/misc/game_db.json", db)) return;
    const JVal& acts = db.get("actors");
    for (const auto& kv : acts.o) {
        int id = atoi(kv.first.c_str());
        g_game.actorNames[id] = kv.second.get("name").asStr();
        g_game.actorChars[id] = kv.second.get("char_name").asStr();
    }
    const JVal& party = db.get("party");
    if (party.type == JVal::T_ARR && !party.a.empty()) {
        g_game.startingParty.clear();
        for (size_t i = 0; i < party.a.size(); i++) {
            int aid = party[i].asInt(0);
            if (aid > 0) g_game.startingParty.push_back(aid);
        }
        if (g_game.startingParty.empty()) g_game.startingParty.push_back(1);
    }
    const JVal& its = db.get("items");
    for (const auto& kv : its.o) {
        int id = atoi(kv.first.c_str());
        g_game.itemDefs[id] = {
            kv.second.get("name").asStr(),
            kv.second.get("icon").asStr(),
            kv.second.get("desc").asStr(),
            kv.second.get("common_event_id").asInt(0),
            kv.second.get("consumable").asBool(false)
        };
    }
    const JVal& ces = db.get("common_events");
    for (size_t i = 0; i < ces.a.size(); i++) {
        CommonEventDef ce;
        ce.id = ces[i].get("id").asInt(0);
        ce.trigger = ces[i].get("trigger").asInt(0);
        ce.switch_id = ces[i].get("switch_id").asInt(1);
        ce.list = ces[i].get("list");
        g_game.commonEvents.push_back(ce);
    }
}

// Guardado / Carga de partida (en memoria, Auto-Save QoL y en /data/aooni_save.dat en disco)
struct SaveSnapshot {
    bool valid = false;
    uint32_t versionMagic = 0x414F4E36;
    bool switches[512];
    int variables[512];
    std::map<std::string, bool> selfSwitches;
    std::map<int, int> inventory;
    std::vector<int> partyActors;
    int mapId = 4, playerX = 9, playerY = 8, playerDir = 2;
    std::string playerChar = "hirosi";
    std::string heroName = "Hiroshi";
    bool playerTransparent = false;
    int toneR = 0, toneG = 0, toneB = 0;
    uint32_t frameCount = 0;
} g_memorySave, g_autoSave;

static const char* SAVE_FILE_PATH = "/data/aooni_save.dat";
static const uint32_t SAVE_MAGIC = 0x414F4E36; // "AON6"

static void captureSnapshot(SaveSnapshot& snap) {
    snap.valid = true;
    snap.versionMagic = SAVE_MAGIC;
    memcpy(snap.switches, g_game.switches, sizeof(g_game.switches));
    memcpy(snap.variables, g_game.variables, sizeof(g_game.variables));
    snap.selfSwitches = g_game.selfSwitches;
    snap.inventory = g_game.inventory;
    snap.partyActors = g_game.partyActors;
    snap.mapId = g_game.mapId;
    snap.playerX = g_game.playerX;
    snap.playerY = g_game.playerY;
    snap.playerDir = g_game.playerDir;
    snap.playerChar = g_game.playerChar;
    snap.heroName = g_game.actorNames.count(2) ? g_game.actorNames[2] : "Hiroshi";
    snap.playerTransparent = g_game.playerTransparent;
    snap.toneR = g_game.toneR;
    snap.toneG = g_game.toneG;
    snap.toneB = g_game.toneB;
    snap.frameCount = g_game.frameCount;
}

static bool writeSnapshotToDisk(const SaveSnapshot& snap, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&SAVE_MAGIC, 4, 1, f);
    fwrite(snap.switches, sizeof(snap.switches), 1, f);
    fwrite(snap.variables, sizeof(snap.variables), 1, f);
    fwrite(&snap.mapId, 4, 1, f);
    fwrite(&snap.playerX, 4, 1, f);
    fwrite(&snap.playerY, 4, 1, f);
    fwrite(&snap.playerDir, 4, 1, f);
    fwrite(&snap.playerTransparent, sizeof(bool), 1, f);
    fwrite(&snap.toneR, 4, 1, f);
    fwrite(&snap.toneG, 4, 1, f);
    fwrite(&snap.toneB, 4, 1, f);
    fwrite(&snap.frameCount, 4, 1, f);
    uint16_t pcLen = (uint16_t)snap.playerChar.size();
    fwrite(&pcLen, 2, 1, f);
    if (pcLen > 0) fwrite(snap.playerChar.data(), 1, pcLen, f);
    uint16_t hnLen = (uint16_t)snap.heroName.size();
    fwrite(&hnLen, 2, 1, f);
    if (hnLen > 0) fwrite(snap.heroName.data(), 1, hnLen, f);
    uint32_t ssCount = (uint32_t)snap.selfSwitches.size();
    fwrite(&ssCount, 4, 1, f);
    for (const auto& kv : snap.selfSwitches) {
        uint16_t klen = (uint16_t)kv.first.size();
        uint8_t v = kv.second ? 1 : 0;
        fwrite(&klen, 2, 1, f);
        fwrite(kv.first.data(), 1, klen, f);
        fwrite(&v, 1, 1, f);
    }
    uint32_t invCount = (uint32_t)snap.inventory.size();
    fwrite(&invCount, 4, 1, f);
    for (const auto& kv : snap.inventory) {
        fwrite(&kv.first, 4, 1, f);
        fwrite(&kv.second, 4, 1, f);
    }
    uint32_t paCount = (uint32_t)snap.partyActors.size();
    fwrite(&paCount, 4, 1, f);
    for (int aid : snap.partyActors) {
        fwrite(&aid, 4, 1, f);
    }
    fclose(f);
    return true;
}

static bool readSnapshotFromDisk(SaveSnapshot& snap, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint32_t magic = 0;
    if (fread(&magic, 4, 1, f) != 1 || (magic != SAVE_MAGIC && magic != 0x414F4E35 && magic != 0x414F4E34)) { fclose(f); return false; }
    snap.versionMagic = magic;
    fread(snap.switches, sizeof(snap.switches), 1, f);
    fread(snap.variables, sizeof(snap.variables), 1, f);
    fread(&snap.mapId, 4, 1, f);
    fread(&snap.playerX, 4, 1, f);
    fread(&snap.playerY, 4, 1, f);
    fread(&snap.playerDir, 4, 1, f);
    fread(&snap.playerTransparent, sizeof(bool), 1, f);
    fread(&snap.toneR, 4, 1, f);
    fread(&snap.toneG, 4, 1, f);
    fread(&snap.toneB, 4, 1, f);
    fread(&snap.frameCount, 4, 1, f);
    uint16_t pcLen = 0;
    fread(&pcLen, 2, 1, f);
    snap.playerChar.assign(pcLen, '\0');
    if (pcLen > 0) fread(&snap.playerChar[0], 1, pcLen, f);
    if (magic >= 0x414F4E35) {
        uint16_t hnLen = 0;
        fread(&hnLen, 2, 1, f);
        snap.heroName.assign(hnLen, '\0');
        if (hnLen > 0) fread(&snap.heroName[0], 1, hnLen, f);
    } else {
        snap.heroName = "Hiroshi";
    }
    uint32_t ssCount = 0;
    fread(&ssCount, 4, 1, f);
    snap.selfSwitches.clear();
    for (uint32_t i = 0; i < ssCount && i < 4096; i++) {
        uint16_t klen = 0; uint8_t v = 0;
        if (fread(&klen, 2, 1, f) != 1 || klen > 128) break;
        std::string k(klen, '\0');
        fread(&k[0], 1, klen, f);
        fread(&v, 1, 1, f);
        snap.selfSwitches[k] = (v != 0);
    }
    uint32_t invCount = 0;
    fread(&invCount, 4, 1, f);
    snap.inventory.clear();
    for (uint32_t i = 0; i < invCount && i < 512; i++) {
        int id = 0, qty = 0;
        if (fread(&id, 4, 1, f) != 1 || fread(&qty, 4, 1, f) != 1) break;
        snap.inventory[id] = qty;
    }
    snap.partyActors.clear();
    if (magic >= 0x414F4E36) {
        uint32_t paCount = 0;
        if (fread(&paCount, 4, 1, f) == 1) {
            for (uint32_t i = 0; i < paCount && i < 32; i++) {
                int aid = 0;
                if (fread(&aid, 4, 1, f) != 1) break;
                if (aid > 0) snap.partyActors.push_back(aid);
            }
        }
    }
    fclose(f);
    snap.valid = true;
    return true;
}

static void saveGame() {
    captureSnapshot(g_memorySave);
    g_autoSave = g_memorySave;
    writeSnapshotToDisk(g_memorySave, SAVE_FILE_PATH);
    playSe("002-System02", 90, 100);
    triggerRumble(80, 0, 8);
}

static bool restoreFromSnapshot(const SaveSnapshot& snap) {
    if (!snap.valid) return false;
    playBgm("");
    playBgs("");
    stopAllSe();
    memcpy(g_game.switches, snap.switches, sizeof(g_game.switches));
    memcpy(g_game.variables, snap.variables, sizeof(g_game.variables));
    g_game.selfSwitches = snap.selfSwitches;
    g_game.inventory = snap.inventory;
    if (!snap.partyActors.empty()) {
        g_game.partyActors = snap.partyActors;
    } else {
        g_game.partyActors = (snap.mapId == 75) ? g_game.startingParty : std::vector<int>{ g_game.switches[76] ? 3 : 2 };
    }
    g_game.playerChar = snap.playerChar;
    // Reparar guardados antiguos (< AON6) afectados por el bug de cmd 129 que sobrescribia playerChar con "takurou"
    if (g_game.playerChar == "takurou" || (snap.versionMagic < SAVE_MAGIC && g_game.playerChar != "puzzle_player")) {
        std::string leader = resolveLeaderPartyChar();
        if (!leader.empty()) {
            g_game.playerChar = leader;
        } else if (g_game.playerChar == "takurou") {
            g_game.playerChar = g_game.switches[76] ? "hirosi_s" : "hirosi";
        }
    }
    g_game.actorNames[2] = snap.heroName.empty() ? "Hiroshi" : snap.heroName;
    g_game.playerTransparent = snap.playerTransparent;
    g_game.toneR = snap.toneR;
    g_game.toneG = snap.toneG;
    g_game.toneB = snap.toneB;
    g_game.curToneR = (float)g_game.toneR;
    g_game.curToneG = (float)g_game.toneG;
    g_game.curToneB = (float)g_game.toneB;
    g_game.toneFrames = 0;
    g_game.screenBlackout = false;
    g_game.transPhase = 2;
    g_game.fadeAlpha = 255;
    g_game.frameCount = snap.frameCount;
    g_game.fgStack.clear();
    g_game.msgActive = g_game.choiceActive = g_game.numInputActive = g_game.btnInputActive = g_game.nameInputActive = false;
    g_game.pendingTransfer = g_game.nextNoFade = g_game.gameOverTriggered = g_game.returnToTitleTriggered = false;
    g_game.parBtnJustPressedMask = 0;
    g_game.timerWorking = false;
    g_game.weatherType = 0;
    g_game.weatherPower = 0;
    g_game.animActive = false;
    g_game.playerJumpTimer = g_game.playerJumpTotal = 0;
    g_game.menuDisabled = false; // Restablecer acceso al inventario tras continuar/cargar partida
    for (int i = 0; i < 52; i++) {
        g_game.pictures[i].active = false;
        g_game.pictures[i].cachedSurf = nullptr;
    }
    loadMap(snap.mapId);
    g_game.playerX = snap.playerX;
    g_game.playerY = snap.playerY;
    g_game.playerRealX = (float)(g_game.playerX * TILE_SZ);
    g_game.playerRealY = (float)(g_game.playerY * TILE_SZ);
    g_game.playerDir = snap.playerDir;
    g_game.playerPattern = 0;
    setPadLightbar(25, 45, 160);
    return true;
}

static bool loadSavedGame() {
    if (g_memorySave.valid) return restoreFromSnapshot(g_memorySave);
    if (g_autoSave.valid) return restoreFromSnapshot(g_autoSave);
    return false;
}

static void resetNewGame() {
    playBgm("");
    playBgs("");
    stopAllSe();
    memset(g_game.switches, 0, sizeof(g_game.switches));
    memset(g_game.variables, 0, sizeof(g_game.variables));
    g_game.selfSwitches.clear();
    g_game.inventory.clear();
    for (int i = 0; i < 52; i++) {
        g_game.pictures[i].active = false;
        g_game.pictures[i].cachedSurf = nullptr;
    }
    g_game.weatherType = 0;
    g_game.weatherPower = 0;
    g_game.animActive = false;
    g_game.playerJumpTimer = g_game.playerJumpTotal = 0;
    g_game.partyActors = g_game.startingParty;
    g_game.playerChar = resolveLeaderPartyChar();
    g_game.actorNames[2] = "Hiroshi";
    g_game.playerTransparent = true;
    g_game.playerThrough = false;
    g_game.playerDirFix = false;
    g_game.playerSpeed = 4.0f;
    g_game.playerDir = 2;
    g_game.playerPattern = 0;
    g_game.toneR = g_game.toneG = g_game.toneB = 0;
    g_game.curToneR = g_game.curToneG = g_game.curToneB = 0.0f;
    g_game.toneFrames = 0;
    g_game.screenBlackout = false;
    g_game.transPhase = 0;
    g_game.fadeAlpha = 0;
    g_game.fgStack.clear();
    g_game.msgActive = false;
    g_game.choiceActive = false;
    g_game.numInputActive = false;
    g_game.btnInputActive = false;
    g_game.nameInputActive = false;
    g_game.pendingTransfer = false;
    g_game.nextNoFade = false;
    g_game.parBtnJustPressedMask = 0;
    g_game.gameOverTriggered = false;
    g_game.returnToTitleTriggered = false;
    g_game.timerWorking = false;
    g_game.menuDisabled = false; // Restablecer acceso al inventario para nueva partida
    g_game.playerX = 9;
    g_game.playerY = 8;
    g_game.playerRealX = (float)(9 * TILE_SZ);
    g_game.playerRealY = (float)(8 * TILE_SZ);
    setPadLightbar(25, 45, 160);
    loadMap(75);
}

// Matriz de caracteres del teclado en pantalla (6 filas x 10 columnas) + fila 6 de botones rápidos
static const char NAME_KB_ROWS[6][11] = {
    "ABCDEFGHIJ",
    "KLMNOPQRST",
    "UVWXYZ0123",
    "abcdefghij",
    "klmnopqrst",
    "uvwxyz4567"
};
struct QuickNameBtn {
    const char* label;
    const char* value; // NULL = acción especial (0=Confirmar, 1=Borrar, 2=Espacio)
    const char* hint;
};
static const QuickNameBtn NAME_QUICK_BTNS[9] = {
    { "CONFIRMAR", nullptr,     "Aceptar este nombre y comenzar la partida" },
    { "BORRAR",    nullptr,     "Borrar el ultimo caracter (tambien con Cuadrado/Circulo)" },
    { "Hiroshi",   "Hiroshi",   "Nombre original por defecto del protagonista" },
    { "SOUTHPARK", "SOUTHPARK", "Easter Egg: Activa el Modo South Park (sprites y sonidos)" },
    { "GODMODE",   "GODMODE",   "Easter Egg: Modo Invencible frente a Ao Oni" },
    { "BLOCKMAN",  "BLOCKMAN",  "Easter Egg: Ao Oni se mueve al doble de velocidad" },
    { "JMU",       "JMU",       "Easter Egg: Aspecto clasico de Ao Oni v1.0" },
    { "HIROSI",    "HIROSI",    "Easter Egg: Retrato clasico de Hiroshi en dialogos" },
    { "TIMER",     "TIMER",     "Easter Egg: Cronometro Speedrun en pantalla" }
};

// ==================== BRILLO RESPLANDECIENTE POST-TONO (OBJETOS RECOGIBLES 1:1) ====================
struct SparkleGlintPt {
    int x, y;
    uint8_t r, g, b;
};

struct SparkleGlintCell {
    int cw = 0, ch = 0;
    std::vector<SparkleGlintPt> pts;
    int cx = 0, cy = 0;
    uint8_t tintR = 255, tintG = 255, tintB = 255;
};

static bool isSparkleSheetName(const std::string& name) {
    return name == "hikaru_item" || name == "book_on_tana2" || name == "book_on_tana" ||
           name == "zou_wareta_hikaru" || name == "kinkoa" || name == "kinkob" ||
           name == "kinkoc" || name == "kinko_haikyo" || name == "yokusou" ||
           name == "yokusou2" || name == "e_chara2" || name == "ka-petto" ||
           name == "tansu" || name == "tansu_l" || name == "tansu_la";
}

static const SparkleGlintCell& getCachedSparkleGlints(const std::string& charName, int dir, int pat, SDL_Surface* es) {
    static std::map<std::string, SparkleGlintCell> s_cache;
    int row = rmDirToRow(dir);
    int pIdx = ((pat % 4) + 4) % 4;
    std::string key = charName + "#" + std::to_string(row * 4 + pIdx);
    auto it = s_cache.find(key);
    if (it != s_cache.end()) return it->second;

    SparkleGlintCell cell;
    if (es && es->pixels && es->w >= 4 && es->h >= 4) {
        int cw = es->w / 4, ch = es->h / 4;
        cell.cw = cw;
        cell.ch = ch;
        if (pIdx > 0) {
            std::vector<SparkleGlintPt> cand;
            for (int y = 0; y < ch; y++) {
                const uint8_t* rowCur = (const uint8_t*)es->pixels + (row * ch + y) * es->pitch + (pIdx * cw) * 4;
                const uint8_t* rowRef = (const uint8_t*)es->pixels + (row * ch + y) * es->pitch;
                for (int x = 0; x < cw; x++) {
                    const uint8_t* sp = rowCur + x * 4;
                    const uint8_t* rp = rowRef + x * 4;
                    if (sp[3] < 200) continue;
                    bool diff = (sp[0] != rp[0] || sp[1] != rp[1] || sp[2] != rp[2] || sp[3] != rp[3]);
                    if (!diff) continue;
                    uint8_t mx = std::max(sp[0], std::max(sp[1], sp[2]));
                    int sum = (int)sp[0] + (int)sp[1] + (int)sp[2];
                    if (mx >= 245 && sum >= 520) {
                        cand.push_back({ x, y, sp[0], sp[1], sp[2] });
                    }
                }
            }
            if (!cand.empty() && cand.size() <= 16) {
                cell.pts = cand;
                int sumX = 0, sumY = 0, sumR = 0, sumG = 0, sumB = 0;
                for (const auto& pt : cand) {
                    sumX += pt.x; sumY += pt.y;
                    sumR += pt.r; sumG += pt.g; sumB += pt.b;
                }
                int n = (int)cand.size();
                cell.cx = (sumX + n / 2) / n;
                cell.cy = (sumY + n / 2) / n;
                cell.tintR = (uint8_t)(sumR / n);
                cell.tintG = (uint8_t)(sumG / n);
                cell.tintB = (uint8_t)(sumB / n);
            }
        }
    }
    return s_cache[key] = cell;
}

static inline void blendSparklePixel(SDL_Surface* dst, int offX, int offY, int camX, int camY,
                                      bool alwaysTop, int sx, int sy,
                                      uint8_t gr, uint8_t gg, uint8_t gb, int intensity) {
    if (intensity <= 0) return;
    if (sx < offX || sx >= offX + GAME_W || sy < offY || sy >= offY + GAME_H) return;
    if (!alwaysTop && g_game.mapUpperSurf && g_game.mapUpperSurf->pixels) {
        int wx = camX + (sx - offX);
        int wy = camY + (sy - offY);
        if (wx >= 0 && wy >= 0 && wx < g_game.mapUpperSurf->w && wy < g_game.mapUpperSurf->h) {
            const uint8_t* up = (const uint8_t*)g_game.mapUpperSurf->pixels + wy * g_game.mapUpperSurf->pitch + wx * 4;
            if (up[3] >= 128) return;
        }
    }
    uint32_t* row = (uint32_t*)((uint8_t*)dst->pixels + sy * dst->pitch);
    uint8_t dr, dg, db;
    unpackWinPixel(row[sx], dr, dg, db);
    int nr = std::min(255, (int)dr + ((int)gr * intensity) / 255);
    int ng = std::min(255, (int)dg + ((int)gg * intensity) / 255);
    int nb = std::min(255, (int)db + ((int)gb * intensity) / 255);
    row[sx] = packWinPixel((uint8_t)nr, (uint8_t)ng, (uint8_t)nb);
}

static void drawSparkleStarGlow(SDL_Surface* gameSurf, int offX, int offY, int camX, int camY,
                                const MapEvent& mev, int evPxX, int evPxY) {
    SDL_Surface* es = getSurf(std::string("/app0/assets/images/char_") + mev.char_name + ".png");
    if (!es) return;
    const SparkleGlintCell& gl = getCachedSparkleGlints(mev.char_name, mev.dir, mev.pattern, es);
    if (gl.pts.empty() || gl.cw <= 0 || gl.ch <= 0) return;

    int ex = offX + (evPxX - camX) - (gl.cw - TILE_SZ) / 2;
    int ey = offY + (evPxY - camY) - (gl.ch - TILE_SZ);
    int op = std::min(255, std::max(0, mev.opacity));
    int pat = ((mev.pattern % 4) + 4) % 4;

    uint8_t haloR = (gl.tintR < 220) ? (uint8_t)std::min(255, (int)gl.tintR + 50) : 255;
    uint8_t haloG = (gl.tintG < 220) ? (uint8_t)std::min(255, (int)gl.tintG + 65) : 252;
    uint8_t haloB = 255;

    for (const auto& pt : gl.pts) {
        int sx = ex + pt.x;
        int sy = ey + pt.y;
        uint8_t cr = std::max<uint8_t>(225, pt.r);
        uint8_t cg = std::max<uint8_t>(225, pt.g);
        uint8_t cb = std::max<uint8_t>(240, pt.b);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, sx, sy, cr, cg, cb, (220 * op) >> 8);
    }

    int scx = ex + gl.cx;
    int scy = ey + gl.cy;
    if (pat == 2) {
        for (int d = 1; d <= 4; d++) {
            int w = (d == 1) ? 195 : ((d == 2) ? 145 : ((d == 3) ? 90 : 42));
            int iw = (w * op) >> 8;
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx - d, scy, haloR, haloG, haloB, iw);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx + d, scy, haloR, haloG, haloB, iw);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy - d, haloR, haloG, haloB, iw);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy + d, haloR, haloG, haloB, iw);
        }
        int dw = (72 * op) >> 8;
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx - 1, scy - 1, haloR, haloG, haloB, dw);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx + 1, scy - 1, haloR, haloG, haloB, dw);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx - 1, scy + 1, haloR, haloG, haloB, dw);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx + 1, scy + 1, haloR, haloG, haloB, dw);
    } else {
        int w1 = (95 * op) >> 8;
        int w2 = (36 * op) >> 8;
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx - 1, scy, haloR, haloG, haloB, w1);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx + 1, scy, haloR, haloG, haloB, w1);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy - 1, haloR, haloG, haloB, w1);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy + 1, haloR, haloG, haloB, w1);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx - 2, scy, haloR, haloG, haloB, w2);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx + 2, scy, haloR, haloG, haloB, w2);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy - 2, haloR, haloG, haloB, w2);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, scx, scy + 2, haloR, haloG, haloB, w2);
    }
}

// Brillo sutil y estrella viva para TODOS los objetos recogibles (incluyendo cajones/mesas) e ítems pequeños QoL
static void drawLivingItemGlint(SDL_Surface* gameSurf, int offX, int offY, int camX, int camY,
                                const MapEvent& mev, int evPxX, int evPxY) {
    if (!g_highlightObjects || isSparkleSheetName(mev.char_name)) return;
    if (!mev.grantsItem && !mev.isTinyInteractive) return;

    int cx = offX + (evPxX - camX) + TILE_SZ / 2;
    int cy = offY + (evPxY - camY) + TILE_SZ / 2 - 2;
    if (!mev.char_name.empty() && mev.cachedSurf && mev.cachedSurf->w >= 4 && mev.cachedSurf->h >= 4) {
        int cw = mev.cachedSurf->w / 4;
        int ch = mev.cachedSurf->h / 4;
        int ex = offX + (evPxX - camX) - (cw - TILE_SZ) / 2;
        int ey = offY + (evPxY - camY) - (ch - TILE_SZ);
        cx = ex + cw / 2;
        cy = ey + std::max(8, ch / 2);
    }

    int phase = (int)((g_game.frameCount + (uint32_t)mev.id * 19u) % 90u);
    int op = std::min(255, std::max(0, mev.opacity));
    uint8_t cr = mev.grantsItem ? 255 : 190;
    uint8_t cg = mev.grantsItem ? 242 : 232;
    uint8_t cb = mev.grantsItem ? 150 : 255;

    // Núcleo suave constante para que ítems muy pequeños/oscuros no pasen desapercibidos
    int baseGlow = mev.grantsItem ? 115 : 85;
    blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy, cr, cg, cb, (baseGlow * op) >> 8);
    if (phase < 36) {
        int env = (phase < 18) ? phase : (36 - phase); // 0..18..0
        int centerI = ((baseGlow + env * 7) * op) >> 8;
        int arm1I   = ((35 + env * 9) * op) >> 8;
        int arm2I   = ((env * 5) * op) >> 8;
        int arm3I   = ((env * 2) * op) >> 8;
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy, 255, 255, 235, std::min(255, centerI));
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx - 1, cy, cr, cg, cb, arm1I);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx + 1, cy, cr, cg, cb, arm1I);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy - 1, cr, cg, cb, arm1I);
        blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy + 1, cr, cg, cb, arm1I);
        if (env >= 5) {
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx - 2, cy, cr, cg, cb, arm2I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx + 2, cy, cr, cg, cb, arm2I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy - 2, cr, cg, cb, arm2I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy + 2, cr, cg, cb, arm2I);
        }
        if (env >= 6) {
            int diagI = ((env * 4) * op) >> 8;
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx - 1, cy - 1, cr, cg, cb, diagI);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx + 1, cy - 1, cr, cg, cb, diagI);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx - 1, cy + 1, cr, cg, cb, diagI);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx + 1, cy + 1, cr, cg, cb, diagI);
        }
        if (env >= 11) {
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx - 3, cy, cr, cg, cb, arm3I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx + 3, cy, cr, cg, cb, arm3I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy - 3, cr, cg, cb, arm3I);
            blendSparklePixel(gameSurf, offX, offY, camX, camY, mev.always_top, cx, cy + 3, cr, cg, cb, arm3I);
        }
    }
}

// Clima RMXP (cmd 236): trazos diagonales de lluvia y pequeñas gotas sin asignaciones
static void drawRainWeather(SDL_Surface* gameSurf, int offX, int offY, uint32_t frame) {
    if (!gameSurf || !gameSurf->pixels || g_game.weatherType <= 0 || g_game.weatherPower <= 0) return;
    int drops = std::min(72, g_game.weatherPower * 8);
    int vx = -2;
    int vy = 6;
    for (int i = 0; i < drops; i++) {
        uint32_t seed = (uint32_t)i * 2654435761u;
        int startX = (int)((seed ^ (seed >> 13)) % (uint32_t)GAME_W);
        int startY = (int)(((seed >> 7) ^ (seed >> 19)) % (uint32_t)GAME_H);
        int t = (int)((frame * 3u + (seed & 63u)) % 120u);
        int x = offX + ((startX + t * vx + GAME_W * 4) % GAME_W);
        int y = offY + ((startY + t * vy) % GAME_H);
        for (int s = 0; s < 11; s++) {
            int px = x - s / 3;
            int py = y + s;
            if (px < offX || px >= offX + GAME_W || py < offY || py >= offY + GAME_H) continue;
            uint32_t* row = (uint32_t*)((uint8_t*)gameSurf->pixels + py * gameSurf->pitch);
            uint8_t dr, dg, db;
            unpackWinPixel(row[px], dr, dg, db);
            int boost = (s > 7) ? 95 : 68;
            row[px] = packWinPixel((uint8_t)std::min(255, (int)dr + boost - 14),
                                   (uint8_t)std::min(255, (int)dg + boost - 4),
                                   (uint8_t)std::min(255, (int)db + boost + 18));
        }
    }
}

// Animación de batalla/impacto (cmd 207, ej: anim_bang.png en Map 021 Ev 2)
static void drawActiveAnimation(SDL_Surface* gameSurf, int offX, int offY, int camX, int camY) {
    if (!g_game.animActive) return;
    static SDL_Surface* s_animBang = nullptr;
    static bool s_animTried = false;
    if (!s_animTried) {
        s_animTried = true;
        s_animBang = getSurf("/app0/assets/images/anim_bang.png");
    }
    if (!s_animBang) return;
    int px = (int)std::round(g_game.playerRealX);
    int py = (int)std::round(g_game.playerRealY);
    if (g_game.animTargetId > 0) {
        MapEvent* tev = findEvent(g_game.animTargetId);
        if (tev) {
            px = (int)std::round(tev->realX);
            py = (int)std::round(tev->realY);
        }
    }
    int maxCells = std::max(1, s_animBang->w / 192);
    int cel = std::min(maxCells - 1, g_game.animFrame / 3);
    SDL_Rect src = { cel * 192, 0, 192, std::min(192, s_animBang->h) };
    SDL_Rect dst = { offX + (px - camX) + TILE_SZ / 2 - 96, offY + (py - camY) + TILE_SZ / 2 - 96, 192, std::min(192, s_animBang->h) };
    blitSurf(s_animBang, &src, gameSurf, &dst);
}

// Indicador contextual discreto [X] frente a objetos, puertas o pistas interactuables
static void drawInteractPrompt(SDL_Surface* gameSurf, int offX, int offY, int camX, int camY, uint32_t frame) {
    if (!g_highlightObjects || g_game.playerTransparent || g_game.screenBlackout || g_game.fadeAlpha >= 128) return;
    if (!g_game.fgStack.empty() || g_game.msgActive || g_game.choiceActive ||
        g_game.numInputActive || g_game.btnInputActive || g_game.nameInputActive ||
        g_game.pendingTransfer || g_game.transPhase != 0) return;

    const MapEvent* hit = nullptr;
    auto isMasked = [&](const MapEvent& ev) {
        return ev.x >= 0 && ev.x < g_game.mapW && ev.y >= 0 && ev.y < g_game.mapH &&
               (size_t)(ev.y * g_game.mapW + ev.x) < g_game.maskedPenTiles.size() &&
               g_game.maskedPenTiles[ev.y * g_game.mapW + ev.x] != 0;
    };
    for (const auto& mev : g_game.events) {
        if (mev.activePage >= 0 && mev.isInteractable && !isMasked(mev) &&
            mev.x == g_game.playerX && mev.y == g_game.playerY) {
            hit = &mev; break;
        }
    }
    int fx = g_game.playerX + (g_game.playerDir == 4 ? -1 : (g_game.playerDir == 6 ? 1 : 0));
    int fy = g_game.playerY + (g_game.playerDir == 8 ? -1 : (g_game.playerDir == 2 ? 1 : 0));
    if (!hit) {
        for (const auto& mev : g_game.events) {
            if (mev.activePage >= 0 && mev.isInteractable && !isMasked(mev) && mev.x == fx && mev.y == fy) {
                hit = &mev; break;
            }
        }
    }
    if (!hit && isMapTileCounter(fx, fy)) {
        int f2x = fx + (g_game.playerDir == 4 ? -1 : (g_game.playerDir == 6 ? 1 : 0));
        int f2y = fy + (g_game.playerDir == 8 ? -1 : (g_game.playerDir == 2 ? 1 : 0));
        for (const auto& mev : g_game.events) {
            if (mev.activePage >= 0 && mev.isInteractable && !isMasked(mev) && mev.x == f2x && mev.y == f2y) {
                hit = &mev; break;
            }
        }
    }
    if (!hit) return;

    int bob = (int)std::round(std::sin((float)frame * 0.14f) * 1.5f);
    int tx = offX + ((int)std::round(hit->realX) - camX) + TILE_SZ / 2;
    int ty = offY + ((int)std::round(hit->realY) - camY) - 22 + bob;
    int bw = hit->grantsItem ? 36 : 26;
    int bh = 18;
    int bx = std::max(offX + 6, std::min(offX + GAME_W - bw - 6, tx - bw / 2));
    int by = std::max(offY + 6, std::min(offY + GAME_H - bh - 6, ty));
    fillRectFast(gameSurf, bx, by, bw, bh, 12, 16, 38, 225);
    drawRectFast(gameSurf, bx, by, bw, bh, hit->grantsItem ? 255 : 220, hit->grantsItem ? 232 : 228, hit->grantsItem ? 130 : 255);
    drawText(gameSurf, bx + 5, by + 3, hit->grantsItem ? "X *" : "X", 2, hit->grantsItem ? 255 : 240, hit->grantsItem ? 235 : 245, hit->grantsItem ? 120 : 255);
}

enum AppState { STATE_TITLE = 0, STATE_MAP = 1, STATE_INVENTORY = 2, STATE_GAMEOVER = 3 };

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(12345);
    printf("[AoOni] Iniciando interprete Ao Oni PS4 / PS4 Pro 4K 1:1 Optimizado...\n");

    pthread_t audioThr;
    pthread_create(&audioThr, NULL, audioThreadFunc, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) return -1;
    SDL_Window* window = SDL_CreateWindow("Ao Oni PS4", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    if (!window) return -1;
    SDL_Surface* winSurf = SDL_GetWindowSurface(window);
    if (!winSurf) return -1;

    // Superficie dedicada de juego a resolución nativa 640x480 (se escala por hardware/scanline a 720p, 1080p o 4K Nativo)
    SDL_Surface* gameSurf = SDL_CreateRGBSurfaceWithFormat(0, GAME_W, GAME_H, 32, SDL_PIXELFORMAT_RGBA32);
    if (!gameSurf) gameSurf = winSurf;

    SDL_Joystick* joy = (SDL_NumJoysticks() >= 1) ? SDL_JoystickOpen(0) : nullptr;

    initPadHandle();
    setPadLightbar(25, 45, 160);

    // Detectar automáticamente PS4 Pro (Neo Mode) o salida 4K/1080p activa
    {
        bool isPro = (sceKernelIsNeoMode() > 0);
        PS4_VideoData* vd = getPs4VideoData();
        OrbisVideoOutResolutionStatus resStat;
        memset(&resStat, 0, sizeof(resStat));
        if (vd && vd->handle > 0 && sceVideoOutGetResolutionStatus(vd->handle, &resStat) == 0) {
            printf("[AoOni] Resolucion de sistema detectada: %ux%u (PS4 Pro NeoMode=%d)\n",
                   resStat.width, resStat.height, isPro ? 1 : 0);
            if (isPro || resStat.width >= 3840) {
                if (setVideoOutputResolution(3840, 2160)) g_videoResIndex = 2;
            } else if (resStat.width >= 1920) {
                if (setVideoOutputResolution(1920, 1080)) g_videoResIndex = 1;
            }
        } else if (isPro) {
            if (setVideoOutputResolution(3840, 2160)) g_videoResIndex = 2;
        }
    }

    initGameDb();
    if (readSnapshotFromDisk(g_memorySave, SAVE_FILE_PATH)) {
        g_autoSave = g_memorySave;
        printf("[AoOni] Partida guardada cargada desde %s (Mapa %d)\n", SAVE_FILE_PATH, g_memorySave.mapId);
    }

    SDL_Surface* titleSurf = getSurf("/app0/assets/images/title_title_black.png");
    SDL_Surface* goSurf    = getSurf("/app0/assets/images/go_gameover_3.jpg");
    SDL_Surface* aooniSurf = getSurf("/app0/assets/images/char_ao.png");

    AppState state = STATE_TITLE;
    int menuIndex = (g_memorySave.valid || g_autoSave.valid) ? 1 : 0;
    int invCursor = 0;
    std::string invStatusMsg;
    const int offX = 0;
    const int offY = 0;

    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
    bool padL1 = false, padR1 = false;
    uint32_t prevPadMask = 0;
    int lastPressedDir = 2; // Última dirección pulsada por el usuario (2=Abajo, 4=Izq, 6=Der, 8=Arriba)
    int dpadRepeatHoldFrames = 0;
    uint32_t autoSaveBannerFrames = 0;
    uint32_t videoBannerFrames = 0;
    std::string videoBannerText;

    for (uint32_t frame = 0;; frame++) {
        uint32_t frameStart = SDL_GetTicks();
        g_game.frameCount++;
        uint32_t now = frameStart;
        updateRumble();

        // Actualizar Lightbar y latido háptico cuando Ao Oni persigue al jugador (switch 49)
        if (state == STATE_MAP) {
            if (g_game.switches[50]) {
                setPadLightbar(255, 0, 0);
            } else if (g_game.switches[49]) {
                uint8_t pulse = (uint8_t)(120 + (int)(110.0f * std::sin(frame * 0.22f)));
                setPadLightbar(pulse, 0, 220);
                // Latido háptico en el mando cuando Ao Oni está a <= 4 casillas de Hiroshi
                if (frame % 26 == 0 && g_rumbleFrames <= 0) {
                    for (const auto& mev : g_game.events) {
                        if (isAoOniLethalEvent(mev)) {
                            int dist = std::abs(mev.x - g_game.playerX) + std::abs(mev.y - g_game.playerY);
                            if (dist <= 4) {
                                uint8_t str = (uint8_t)std::min(190, (5 - dist) * 45);
                                triggerRumble(str, (uint8_t)(str / 2), 6);
                                break;
                            }
                        }
                    }
                }
            } else if (frame % 60 == 0) {
                setPadLightbar(25, 45, 160);
            }
        }

        // Construir lista reutilizable de items actuales para el inventario
        static std::vector<int> ownedItems;
        ownedItems.clear();
        for (const auto& kv : g_game.inventory)
            if (kv.second > 0) ownedItems.push_back(kv.first);

        // 1) Drenar cola de eventos SDL (y capturar pulsaciones ultra-rápidas intra-frame de joystick y teclado)
        uint32_t eventPressedMask = 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_JOYBUTTONDOWN && ev.jbutton.button < 24) {
                eventPressedMask |= (1u << ev.jbutton.button);
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                SDL_Scancode sc = ev.key.keysym.scancode;
                if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_W) eventPressedMask |= (1u << PAD_UP);
                else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_S) eventPressedMask |= (1u << PAD_DOWN);
                else if (sc == SDL_SCANCODE_LEFT || sc == SDL_SCANCODE_A) eventPressedMask |= (1u << PAD_LEFT);
                else if (sc == SDL_SCANCODE_RIGHT || sc == SDL_SCANCODE_D) eventPressedMask |= (1u << PAD_RIGHT);
                else if (sc == SDL_SCANCODE_Z || sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE) eventPressedMask |= (1u << PAD_CROSS);
                else if (sc == SDL_SCANCODE_X || sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE) eventPressedMask |= (1u << PAD_CIRCLE);
                else if (sc == SDL_SCANCODE_C) eventPressedMask |= (1u << PAD_SQUARE);
                else if (sc == SDL_SCANCODE_V) eventPressedMask |= (1u << PAD_TRIANGLE);
                else if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) eventPressedMask |= (1u << PAD_L1);
                else if (sc == SDL_SCANCODE_F) eventPressedMask |= (1u << PAD_TOUCHPAD);
                else if (sc == SDL_SCANCODE_TAB) eventPressedMask |= (1u << PAD_OPTIONS);
            }
        }

        // 2) Sondeo directo por hardware de DualShock 4 (scePadReadState + SDL_Joystick + Keyboard) libre de pérdidas de estado
        uint32_t curPadMask = 0;
        float stickX = 0.0f, stickY = 0.0f;

        const Uint8* kstate = SDL_GetKeyboardState(NULL);
        if (kstate) {
            if (kstate[SDL_SCANCODE_UP]    || kstate[SDL_SCANCODE_W]) curPadMask |= (1u << PAD_UP);
            if (kstate[SDL_SCANCODE_DOWN]  || kstate[SDL_SCANCODE_S]) curPadMask |= (1u << PAD_DOWN);
            if (kstate[SDL_SCANCODE_LEFT]  || kstate[SDL_SCANCODE_A]) curPadMask |= (1u << PAD_LEFT);
            if (kstate[SDL_SCANCODE_RIGHT] || kstate[SDL_SCANCODE_D]) curPadMask |= (1u << PAD_RIGHT);
            if (kstate[SDL_SCANCODE_Z] || kstate[SDL_SCANCODE_RETURN] || kstate[SDL_SCANCODE_SPACE]) curPadMask |= (1u << PAD_CROSS);
            if (kstate[SDL_SCANCODE_X] || kstate[SDL_SCANCODE_ESCAPE] || kstate[SDL_SCANCODE_BACKSPACE]) curPadMask |= (1u << PAD_CIRCLE);
            if (kstate[SDL_SCANCODE_C]) curPadMask |= (1u << PAD_SQUARE);
            if (kstate[SDL_SCANCODE_V]) curPadMask |= (1u << PAD_TRIANGLE);
            if (kstate[SDL_SCANCODE_LSHIFT] || kstate[SDL_SCANCODE_RSHIFT]) curPadMask |= (1u << PAD_L1);
            if (kstate[SDL_SCANCODE_F]) curPadMask |= (1u << PAD_TOUCHPAD);
            if (kstate[SDL_SCANCODE_TAB]) curPadMask |= (1u << PAD_OPTIONS);
        }

        if (g_padHandle >= 0) {
            OrbisPadData pdata;
            memset(&pdata, 0, sizeof(pdata));
            if (scePadReadState(g_padHandle, &pdata) >= 0 && pdata.connected) {
                uint32_t ob = pdata.buttons;
                if (ob & ORBIS_PAD_BUTTON_CROSS)     curPadMask |= (1u << PAD_CROSS);
                if (ob & ORBIS_PAD_BUTTON_CIRCLE)    curPadMask |= (1u << PAD_CIRCLE);
                if (ob & ORBIS_PAD_BUTTON_SQUARE)    curPadMask |= (1u << PAD_SQUARE);
                if (ob & ORBIS_PAD_BUTTON_TRIANGLE)  curPadMask |= (1u << PAD_TRIANGLE);
                if (ob & ORBIS_PAD_BUTTON_L1)        curPadMask |= (1u << PAD_L1);
                if (ob & ORBIS_PAD_BUTTON_R1)        curPadMask |= (1u << PAD_R1);
                if (ob & ORBIS_PAD_BUTTON_L2)        curPadMask |= (1u << PAD_L2);
                if (ob & ORBIS_PAD_BUTTON_R2)        curPadMask |= (1u << PAD_R2);
                if (pdata.analogButtons.l2 > 140)    curPadMask |= (1u << PAD_L2);
                if (pdata.analogButtons.r2 > 140)    curPadMask |= (1u << PAD_R2);
                if (ob & ORBIS_PAD_BUTTON_OPTIONS)   curPadMask |= (1u << PAD_OPTIONS);
                if (ob & ORBIS_PAD_BUTTON_TOUCH_PAD) curPadMask |= (1u << PAD_TOUCHPAD);
                if (ob & ORBIS_PAD_BUTTON_UP)        curPadMask |= (1u << PAD_UP);
                if (ob & ORBIS_PAD_BUTTON_DOWN)      curPadMask |= (1u << PAD_DOWN);
                if (ob & ORBIS_PAD_BUTTON_LEFT)      curPadMask |= (1u << PAD_LEFT);
                if (ob & ORBIS_PAD_BUTTON_RIGHT)     curPadMask |= (1u << PAD_RIGHT);
                // Evitar lecturas erróneas de ceros no inicializados si el stick no está calibrado
                if (pdata.leftStick.x != 0 || pdata.leftStick.y != 0 || pdata.rightStick.x != 0 || pdata.rightStick.y != 0) {
                    float sx = ((int)pdata.leftStick.x - 128) / 127.0f;
                    float sy = ((int)pdata.leftStick.y - 128) / 127.0f;
                    if (std::fabs(sx) > std::fabs(stickX)) stickX = sx;
                    if (std::fabs(sy) > std::fabs(stickY)) stickY = sy;
                }
            }
        }

        if (!joy && SDL_NumJoysticks() >= 1) joy = SDL_JoystickOpen(0);
        if (joy) {
            const int pollBtns[] = {
                PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE,
                PAD_L1, PAD_R1, PAD_OPTIONS, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_TOUCHPAD, PAD_L2, PAD_R2
            };
            for (int b : pollBtns) {
                if (SDL_JoystickGetButton(joy, b)) curPadMask |= (1u << b);
            }
            Uint8 hat = SDL_JoystickGetHat(joy, 0);
            if (hat & SDL_HAT_UP)    curPadMask |= (1u << PAD_UP);
            if (hat & SDL_HAT_DOWN)  curPadMask |= (1u << PAD_DOWN);
            if (hat & SDL_HAT_LEFT)  curPadMask |= (1u << PAD_LEFT);
            if (hat & SDL_HAT_RIGHT) curPadMask |= (1u << PAD_RIGHT);

            if (SDL_JoystickNumAxes(joy) >= 2) {
                float jx = SDL_JoystickGetAxis(joy, 0) / 32767.0f;
                float jy = SDL_JoystickGetAxis(joy, 1) / 32767.0f;
                if (std::fabs(jx) > std::fabs(stickX)) stickX = jx;
                if (std::fabs(jy) > std::fabs(stickY)) stickY = jy;
            }
        }

        // Zona muerta radial (0.55f) con resolución de eje dominante estricto para el stick izquierdo
        // Evita drift, snap-back involuntario y que se disparen dos direcciones simultáneamente
        float stickMag = std::sqrt(stickX * stickX + stickY * stickY);
        const float STICK_DEADZONE = 0.55f;
        if (stickMag >= STICK_DEADZONE) {
            if (std::fabs(stickX) > std::fabs(stickY)) {
                if (stickX > 0.0f) curPadMask |= (1u << PAD_RIGHT);
                else               curPadMask |= (1u << PAD_LEFT);
            } else {
                if (stickY > 0.0f) curPadMask |= (1u << PAD_DOWN);
                else               curPadMask |= (1u << PAD_UP);
            }
        }

        dpadUp    = (curPadMask & (1u << PAD_UP))    != 0;
        dpadDown  = (curPadMask & (1u << PAD_DOWN))  != 0;
        dpadLeft  = (curPadMask & (1u << PAD_LEFT))  != 0;
        dpadRight = (curPadMask & (1u << PAD_RIGHT)) != 0;
        padL1     = (curPadMask & (1u << PAD_L1))    != 0;
        padR1     = (curPadMask & (1u << PAD_R1))    != 0;

        uint32_t justPressedMask = (curPadMask & ~prevPadMask) | (eventPressedMask & ~prevPadMask);
        prevPadMask = curPadMask;

        // Registrar cuál fue la última dirección pulsada para desempate natural en cruceta
        if (justPressedMask & (1u << PAD_DOWN))  lastPressedDir = 2;
        if (justPressedMask & (1u << PAD_LEFT))  lastPressedDir = 4;
        if (justPressedMask & (1u << PAD_RIGHT)) lastPressedDir = 6;
        if (justPressedMask & (1u << PAD_UP))    lastPressedDir = 8;

        // Auto-repeat cómodo en menús, inventario, teclado de nombre y cajas fuertes al mantener cruceta/stick
        const uint32_t dpadBits = (1u << PAD_UP) | (1u << PAD_DOWN) | (1u << PAD_LEFT) | (1u << PAD_RIGHT);
        if (curPadMask & dpadBits) {
            dpadRepeatHoldFrames++;
            bool inMenuOrModal = (state != STATE_MAP) || g_game.nameInputActive || g_game.numInputActive || g_game.choiceActive;
            if (inMenuOrModal && dpadRepeatHoldFrames >= 20 && ((dpadRepeatHoldFrames - 20) % 5 == 0)) {
                justPressedMask |= (curPadMask & dpadBits);
            }
        } else {
            dpadRepeatHoldFrames = 0;
        }

        // Registrar pulsación para eventos paralelos con cmd 105 (ej. Map 60 - Puzzle de la muñeca) sin interferir con L2/R2 ni diálogos
        if (state == STATE_MAP && g_game.transPhase == 0 && !g_game.pendingTransfer &&
            g_game.fgStack.empty() && !g_game.msgActive && !g_game.choiceActive &&
            !g_game.numInputActive && !g_game.btnInputActive && !g_game.nameInputActive) {
            g_game.parBtnJustPressedMask = justPressedMask & ~((1u << PAD_L2) | (1u << PAD_R2));
        } else {
            g_game.parBtnJustPressedMask = 0;
        }

        if (g_game.nameInputActive && g_game.nameInputCooldown > 0) {
            g_game.nameInputCooldown--;
        }

        // Procesar cada botón recién pulsado (o repetido en menú) una sola vez limpia por frame
        for (int b = 0; b <= 20; b++) {
            if (!(justPressedMask & (1u << b))) continue;

            // Atajos globales PS4 / PS4 Pro: R2 cambia resolución (720p / 1080p / 4K Nativo), L2 cambia aspecto (4:3 / 16:9 / Pixel-Perfect)
            if (b == PAD_R2) {
                cycleVideoResolution();
                playSe("001-System01", 80, 100);
                videoBannerText = std::string("RESOLUCION: ") + getVideoResLabel();
                videoBannerFrames = 110;
                continue;
            } else if (b == PAD_L2) {
                g_videoScaleMode = (g_videoScaleMode + 1) % 3;
                g_videoClearNeeded = true;
                playSe("001-System01", 80, 100);
                videoBannerText = std::string("ESCALADO: ") + getVideoScaleLabel();
                videoBannerFrames = 110;
                continue;
            } else if (b == PAD_TOUCHPAD) {
                g_highlightObjects = !g_highlightObjects;
                playSe("001-System01", 80, 100);
                videoBannerText = g_highlightObjects ? "BRILLO OBJETOS: ACTIVADO" : "BRILLO OBJETOS: CLASICO";
                videoBannerFrames = 110;
                continue;
            }

            if (state == STATE_TITLE) {
                if (b == PAD_UP)   { menuIndex = (menuIndex + 3) % 4; playSe("001-System01", 80, 100); }
                if (b == PAD_DOWN) { menuIndex = (menuIndex + 1) % 4; playSe("001-System01", 80, 100); }
                if (b == PAD_LEFT || b == PAD_RIGHT) {
                    if (menuIndex == 2) {
                        cycleVideoResolution();
                        playSe("001-System01", 80, 100);
                    } else if (menuIndex == 3) {
                        g_videoScaleMode = (g_videoScaleMode + (b == PAD_RIGHT ? 1 : 2)) % 3;
                        g_videoClearNeeded = true;
                        playSe("001-System01", 80, 100);
                    }
                }
                if (b == PAD_CROSS || b == PAD_OPTIONS) {
                    bool hasSave = g_memorySave.valid || g_autoSave.valid;
                    if (menuIndex == 0) {
                        playSe("002-System02", 90, 100);
                        resetNewGame();
                        state = STATE_MAP;
                    } else if (menuIndex == 1 && hasSave) {
                        playSe("002-System02", 90, 100);
                        loadSavedGame();
                        state = STATE_MAP;
                    } else if (menuIndex == 2) {
                        cycleVideoResolution();
                        playSe("002-System02", 85, 100);
                    } else if (menuIndex == 3) {
                        g_videoScaleMode = (g_videoScaleMode + 1) % 3;
                        g_videoClearNeeded = true;
                        playSe("002-System02", 85, 100);
                    }
                }
            } else if (state == STATE_GAMEOVER) {
                if (b == PAD_CROSS && (g_autoSave.valid || g_memorySave.valid)) {
                    playSe("002-System02", 90, 100);
                    restoreFromSnapshot(g_autoSave.valid ? g_autoSave : g_memorySave);
                    state = STATE_MAP;
                } else if (b == PAD_CROSS || b == PAD_CIRCLE || b == PAD_OPTIONS) {
                    playSe("002-System02", 90, 100);
                    setPadLightbar(25, 45, 160);
                    state = STATE_TITLE;
                }
            } else if (state == STATE_INVENTORY) {
                if (b == PAD_TRIANGLE || b == PAD_CIRCLE || b == PAD_OPTIONS) {
                    if (g_game.switches[178] && b != PAD_CIRCLE) {
                        // En 072_Scene_Menu.rb:134-137 (evento del sótano del anexo en Map 67), cuando se activa switch 178 en el menú, solo permite salir con Círculo
                        playSe("004-System04", 80, 100);
                    } else if (b == PAD_OPTIONS && padL1 && padR1) {
                        // Salir al título solo si se mantiene intencionalmente L1 + R1 + OPTIONS
                        playSe("003-System03", 80, 100);
                        setPadLightbar(25, 45, 160);
                        state = STATE_TITLE;
                    } else {
                        // Cerrar menú y volver al mapa con OPTIONS, Triángulo o Círculo
                        playSe("003-System03", 80, 100);
                        state = STATE_MAP;
                    }
                } else if (b == PAD_SQUARE) {
                    saveGame();
                    invStatusMsg = "Partida guardada en consola y memoria!";
                } else if (b == PAD_UP && !ownedItems.empty()) {
                    invCursor = (invCursor + (int)ownedItems.size() - 1) % (int)ownedItems.size();
                    playSe("001-System01", 80, 100);
                } else if (b == PAD_DOWN && !ownedItems.empty()) {
                    invCursor = (invCursor + 1) % (int)ownedItems.size();
                    playSe("001-System01", 80, 100);
                } else if (b == PAD_CROSS && !ownedItems.empty()) {
                    if (invCursor >= (int)ownedItems.size()) invCursor = 0;
                    int itemId = ownedItems[invCursor];
                    int ceId = g_game.itemDefs.count(itemId) ? g_game.itemDefs[itemId].common_event_id : 0;
                    if (ceId > 0) {
                        playSe("002-System02", 85, 100);
                        state = STATE_MAP;
                        for (const auto& ce : g_game.commonEvents) {
                            if (ce.id == ceId) {
                                startForegroundEvent(0, ce.list);
                                break;
                            }
                        }
                    } else {
                        playSe("004-System04", 80, 100);
                        invStatusMsg = "Este objeto se usa automaticamente en su cerradura.";
                    }
                }
            } else if (state == STATE_MAP) {
                if (g_game.nameInputActive) {
                    bool isDirBtn = (b == PAD_UP || b == PAD_DOWN || b == PAD_LEFT || b == PAD_RIGHT);
                    if (g_game.nameInputCooldown > 0 && !isDirBtn) {
                        continue;
                    }
                    int maxCols = (g_game.nameInputRow == 6) ? 9 : 10;
                    if (b == PAD_UP) {
                        g_game.nameInputRow = (g_game.nameInputRow + 6) % 7;
                        int newMaxCols = (g_game.nameInputRow == 6) ? 9 : 10;
                        if (g_game.nameInputCol >= newMaxCols) g_game.nameInputCol = newMaxCols - 1;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_DOWN) {
                        g_game.nameInputRow = (g_game.nameInputRow + 1) % 7;
                        int newMaxCols = (g_game.nameInputRow == 6) ? 9 : 10;
                        if (g_game.nameInputCol >= newMaxCols) g_game.nameInputCol = newMaxCols - 1;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_LEFT) {
                        g_game.nameInputCol = (g_game.nameInputCol + maxCols - 1) % maxCols;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_RIGHT) {
                        g_game.nameInputCol = (g_game.nameInputCol + 1) % maxCols;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_SQUARE || b == PAD_CIRCLE) {
                        if (!g_game.nameInputStr.empty()) {
                            g_game.nameInputStr.pop_back();
                            playSe("003-System03", 80, 100);
                        }
                    } else if (b == PAD_TRIANGLE) {
                        // Limpiar nombre completo con Triángulo
                        g_game.nameInputStr.clear();
                        playSe("003-System03", 85, 100);
                    } else if (b == PAD_OPTIONS) {
                        if (g_game.nameInputStr.empty()) g_game.nameInputStr = "Hiroshi";
                        g_game.actorNames[g_game.nameInputActorId] = g_game.nameInputStr;
                        g_game.nameInputActive = false;
                        playSe("002-System02", 90, 100);
                    } else if (b == PAD_CROSS) {
                        if (g_game.nameInputRow < 6) {
                            char ch = NAME_KB_ROWS[g_game.nameInputRow][g_game.nameInputCol];
                            if ((int)g_game.nameInputStr.size() < g_game.nameInputMaxLen) {
                                g_game.nameInputStr.push_back(ch);
                                playSe("001-System01", 85, 100);
                            } else {
                                playSe("004-System04", 80, 100);
                            }
                        } else {
                            int qb = g_game.nameInputCol;
                            if (qb == 0) { // CONFIRMAR
                                if (g_game.nameInputStr.empty()) g_game.nameInputStr = "Hiroshi";
                                g_game.actorNames[g_game.nameInputActorId] = g_game.nameInputStr;
                                g_game.nameInputActive = false;
                                playSe("002-System02", 90, 100);
                            } else if (qb == 1) { // BORRAR
                                if (!g_game.nameInputStr.empty()) {
                                    g_game.nameInputStr.pop_back();
                                    playSe("003-System03", 80, 100);
                                }
                            } else if (qb >= 2 && qb < 9 && NAME_QUICK_BTNS[qb].value) {
                                g_game.nameInputStr = NAME_QUICK_BTNS[qb].value;
                                playSe("002-System02", 85, 100);
                            }
                        }
                    }
                } else if (g_game.numInputActive) {
                    if (b == PAD_LEFT) {
                        g_game.numInputCursor = (g_game.numInputCursor + g_game.numInputDigits - 1) % g_game.numInputDigits;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_RIGHT) {
                        g_game.numInputCursor = (g_game.numInputCursor + 1) % g_game.numInputDigits;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_UP) {
                        g_game.numInputVals[g_game.numInputCursor] = (g_game.numInputVals[g_game.numInputCursor] + 1) % 10;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_DOWN) {
                        g_game.numInputVals[g_game.numInputCursor] = (g_game.numInputVals[g_game.numInputCursor] + 9) % 10;
                        playSe("001-System01", 80, 100);
                    } else if (b == PAD_CROSS) {
                        int total = 0;
                        for (int d = 0; d < g_game.numInputDigits; d++)
                            total = total * 10 + g_game.numInputVals[d];
                        if (g_game.numInputVarId >= 0 && g_game.numInputVarId < 512)
                            g_game.variables[g_game.numInputVarId] = total;
                        g_game.numInputActive = false;
                        playSe("002-System02", 85, 100);
                    }
                } else if (g_game.btnInputActive) {
                    int rmCode = 0;
                    if (b == PAD_DOWN) rmCode = 2;
                    else if (b == PAD_LEFT) rmCode = 4;
                    else if (b == PAD_RIGHT) rmCode = 6;
                    else if (b == PAD_UP) rmCode = 8;
                    else if (b == PAD_SQUARE) rmCode = 11;
                    else if (b == PAD_CIRCLE || b == PAD_TRIANGLE) rmCode = 13; // En CE 8 y CE 25 permite cerrar la imagen con Círculo además de Cruz
                    else if (b == PAD_CROSS || b == PAD_OPTIONS) rmCode = 13;
                    if (rmCode > 0) {
                        if (g_game.btnInputVarId >= 0 && g_game.btnInputVarId < 512)
                            g_game.variables[g_game.btnInputVarId] = rmCode;
                        g_game.btnInputActive = false;
                    }
                } else if (g_game.choiceActive) {
                    if (b == PAD_UP && !g_game.choices.empty()) {
                        g_game.choiceIndex = (g_game.choiceIndex + (int)g_game.choices.size() - 1) % (int)g_game.choices.size();
                        playSe("001-System01", 80, 100);
                    }
                    if (b == PAD_DOWN && !g_game.choices.empty()) {
                        g_game.choiceIndex = (g_game.choiceIndex + 1) % (int)g_game.choices.size();
                        playSe("001-System01", 80, 100);
                    }
                    if (b == PAD_CROSS && !g_game.fgStack.empty()) {
                        playSe("002-System02", 85, 100);
                        int wanted = g_game.choiceIndex;
                        g_game.choiceActive = false;
                        g_game.msgActive = false;
                        if (g_game.switches[49]) g_game.timerWorking = true; // 054_Window_Message.rb:64-66
                        InterpFrame& fr = g_game.fgStack.back();
                        while (fr.ip < fr.list.a.size()) {
                            const JVal& c = fr.list[fr.ip];
                            if (c.get("i").asInt(0) == g_game.choiceIndent &&
                                c.get("c").asInt(0) == 402 &&
                                c.get("p")[0].asInt(-1) == wanted) {
                                fr.ip++;
                                break;
                            }
                            if (c.get("i").asInt(0) == g_game.choiceIndent && c.get("c").asInt(0) == 404)
                                break;
                            fr.ip++;
                        }
                    }
                } else if (g_game.msgActive) {
                    if (b == PAD_CROSS || b == PAD_CIRCLE) {
                        if (g_game.msgCharReveal < g_game.msgText.size()) {
                            g_game.msgCharReveal = g_game.msgText.size();
                        } else {
                            g_game.msgActive = false;
                            if (g_game.switches[49]) g_game.timerWorking = true; // 054_Window_Message.rb:64-66
                        }
                    }
                } else if (g_game.fgStack.empty()) {
                    if (b == PAD_CROSS) {
                        bool triggered = false;
                        // 1) check_event_trigger_here([0])
                        for (const auto& mev : g_game.events) {
                            if (mev.activePage < 0) continue;
                            const auto& pg = mev.pages[mev.activePage];
                            if (pg.trigger == 0 && mev.x == g_game.playerX && mev.y == g_game.playerY && pg.list.a.size() > 1) {
                                startForegroundEvent(mev.id, pg.list);
                                triggered = true;
                                break;
                            }
                        }
                        // 2) check_event_trigger_there([0, 1, 2])
                        int fx = g_game.playerX + (g_game.playerDir == 4 ? -1 : (g_game.playerDir == 6 ? 1 : 0));
                        int fy = g_game.playerY + (g_game.playerDir == 8 ? -1 : (g_game.playerDir == 2 ? 1 : 0));
                        if (!triggered) {
                            for (const auto& mev : g_game.events) {
                                if (mev.activePage < 0) continue;
                                const auto& pg = mev.pages[mev.activePage];
                                if (pg.trigger <= 2 && mev.x == fx && mev.y == fy && pg.list.a.size() > 1) {
                                    startForegroundEvent(mev.id, pg.list);
                                    triggered = true;
                                    break;
                                }
                            }
                        }
                        // 3) Mostrador / Mesa (counter tile): si la casilla frontal es counter, revisar 2 casillas al frente
                        if (!triggered && isMapTileCounter(fx, fy)) {
                            int f2x = fx + (g_game.playerDir == 4 ? -1 : (g_game.playerDir == 6 ? 1 : 0));
                            int f2y = fy + (g_game.playerDir == 8 ? -1 : (g_game.playerDir == 2 ? 1 : 0));
                            for (const auto& mev : g_game.events) {
                                if (mev.activePage < 0) continue;
                                const auto& pg = mev.pages[mev.activePage];
                                if (pg.trigger <= 2 && mev.x == f2x && mev.y == f2y && pg.list.a.size() > 1) {
                                    startForegroundEvent(mev.id, pg.list);
                                    triggered = true;
                                    break;
                                }
                            }
                        }
                    } else if (b == PAD_SQUARE && !g_game.menuDisabled && !g_game.switches[49]) {
                        // Guardado rápido QoL con Cuadrado directamente en el mapa (si no te persigue Ao Oni)
                        saveGame();
                        autoSaveBannerFrames = 90;
                    } else if ((b == PAD_TRIANGLE || b == PAD_OPTIONS) && !g_game.menuDisabled) {
                        playSe("002-System02", 80, 100);
                        invCursor = 0;
                        invStatusMsg.clear();
                        // Implementación 1:1 de 072_Scene_Menu.rb:26-43:
                        // En la celda del sótano del anexo (Map 67, X=9, Y=14, mirando abajo tras colocar las 3 muñecas), abrir el menú activa a Takuro Oni (switch 178)
                        if (g_game.mapId == 67 && g_game.playerX == 9 && g_game.playerY == 14 && g_game.playerDir == 2 &&
                            g_game.switches[168] && g_game.switches[169] && g_game.switches[170] && !g_game.switches[180]) {
                            g_game.variables[1] = 3;
                            g_game.switches[178] = true;
                            refreshEvents();
                            playBgm("horor-b", 100, 100);
                            invStatusMsg = "Se escucha algo acercarse! Pulsa Circulo para salir!";
                        }
                        state = STATE_INVENTORY;
                    }
                }
            }
        }

        // Avance del efecto typewriter (XRXS9 Window_Message) y avance rápido con R1
        if (state == STATE_MAP && g_game.msgActive) {
            if (g_game.msgCharReveal < g_game.msgText.size()) {
                g_game.msgCharReveal = std::min(g_game.msgText.size(), g_game.msgCharReveal + (padR1 ? 8 : 2));
            } else if (!g_game.choiceActive && padR1 && (frame % 4 == 0)) {
                g_game.msgActive = false;
                if (g_game.switches[49]) g_game.timerWorking = true;
            }
        }
        if (videoBannerFrames > 0) videoBannerFrames--;

        if (state == STATE_MAP) {
            if (g_game.timerWorking && g_game.timerFrames > 0) g_game.timerFrames--;
            if (autoSaveBannerFrames > 0) autoSaveBannerFrames--;

            updatePictures();

            // 1) Avanzar primero el sub-píxel del jugador hacia su casilla actual para que no haya ni 1 frame de pausa al llegar a una casilla
            bool sprinting = (padL1 || padR1 || ((curPadMask & (1u << PAD_CIRCLE)) != 0 && g_game.mapId != 60) ||
                              ((curPadMask & (1u << PAD_L2)) != 0) || ((curPadMask & (1u << PAD_R2)) != 0) ||
                              g_game.switches[49]);
            float remMovePx = advancePlayerSubPixel(sprinting);

            updateMoveRoutes(now);

            // 2) Movimiento manual ultra-responsivo del jugador (se evalúa en el mismo frame en que alcanza su casilla)
            bool canManualMove = g_game.fgStack.empty() && !g_game.msgActive && !g_game.numInputActive &&
                                 !g_game.btnInputActive && !g_game.nameInputActive && !g_game.playerRouteActive &&
                                 !g_game.pendingTransfer && g_game.transPhase != 1;
            if (canManualMove) {
                // Red de seguridad: si el jugador puede moverse libremente por el mapa fuera de cutscenes, el sprite DEBE ser visible
                bool isCutsceneMap = (g_game.mapId == 75 || g_game.mapId == 9 || g_game.mapId == 30 ||
                                      g_game.mapId == 31 || g_game.mapId == 33 || g_game.mapId == 35 || g_game.mapId == 36);
                if (g_game.playerTransparent && !isCutsceneMap) {
                    g_game.playerTransparent = false;
                }
                if (g_game.playerChar.empty()) {
                    std::string leader = resolveLeaderPartyChar();
                    g_game.playerChar = !leader.empty() ? leader : (g_game.switches[76] ? "hirosi_s" : "hirosi");
                }
            }
            bool holdingMoveInput = canManualMove && (dpadUp || dpadDown || dpadLeft || dpadRight);

            if (canManualMove && !isPlayerMoving()) {
                int dirH = dpadLeft ? 4 : (dpadRight ? 6 : 0);
                int dirV = dpadDown ? 2 : (dpadUp ? 8 : 0);
                int targetDir = 0;

                if (dirH != 0 && dirV != 0) {
                    // Si se pulsa en diagonal (cruceta o teclado), dar prioridad a la última dirección pulsada (1:1 RGSS1 Input.dir4)
                    if (lastPressedDir == dirH) targetDir = dirH;
                    else if (lastPressedDir == dirV) targetDir = dirV;
                    else targetDir = dirV; // Desempate estándar RPG Maker XP (Vertical)
                } else if (dirH != 0) {
                    targetDir = dirH;
                } else if (dirV != 0) {
                    targetDir = dirV;
                }

                if (targetDir != 0) {
                    int d = targetDir;
                    if (!g_game.playerDirFix) g_game.playerDir = d;

                    int nx = g_game.playerX + (d == 4 ? -1 : (d == 6 ? 1 : 0));
                    int ny = g_game.playerY + (d == 8 ? -1 : (d == 2 ? 1 : 0));

                    // Muerte inmediata si choca con Ao Oni en la casilla de destino (fuera de tiempo de gracia o armario)
                    for (const auto& mev : g_game.events) {
                        if (mev.activePage < 0) continue;
                        if (mev.x == nx && mev.y == ny) {
                            if (isAoOniLethalEvent(mev)) {
                                if (mev.routeWaitFrames <= 0 && !g_game.playerTransparent) {
                                    triggerPlayerDeathByAoOni();
                                    break;
                                }
                            }
                        }
                    }
                    if (g_game.gameOverTriggered) break;

                    bool canPass = (g_game.playerThrough && nx >= 0 && ny >= 0 && nx < g_game.mapW && ny < g_game.mapH) ||
                                   canPassMove(g_game.playerX, g_game.playerY, d, true);

                    int prevX = g_game.playerX, prevY = g_game.playerY;

                    if (canPass) {
                        g_game.playerX = nx;
                        g_game.playerY = ny;

                        // Contacto al entrar a la casilla destino (Player Touch / Event Touch)
                        for (const auto& mev : g_game.events) {
                            if (mev.activePage < 0) continue;
                            const auto& pg = mev.pages[mev.activePage];
                            if ((pg.trigger == 1 || pg.trigger == 2) && mev.x == nx && mev.y == ny && pg.list.a.size() > 1) {
                                startForegroundEvent(mev.id, pg.list);
                                break;
                            }
                        }
                    } else {
                        // Casilla intransitable (pared, puerta cerrada, objeto sólido):
                        // Comprobar eventos touch en la casilla frontal (puertas, transiciones)
                        // y DETENERSE encarando el obstáculo (comportamiento 1:1 RGSS1: sin desvíos ni giros indeseados).
                        for (const auto& mev : g_game.events) {
                            if (mev.activePage < 0) continue;
                            const auto& pg = mev.pages[mev.activePage];
                            if ((pg.trigger == 1 || pg.trigger == 2) && mev.x == nx && mev.y == ny && pg.list.a.size() > 1) {
                                startForegroundEvent(mev.id, pg.list);
                                break;
                            }
                        }
                    }

                    if ((g_game.playerX != prevX || g_game.playerY != prevY) && remMovePx > 0.0f) {
                        applyCarryoverPlayerSubPixel(remMovePx);
                    }
                }
            }

            // Actualizar animación de caminata del jugador y posiciones sub-píxel de eventos a 60 FPS
            updateWalkAndEventsSmooth(sprinting, holdingMoveInput);

            // Contacto de Ao Oni o eventos Event Touch en la misma casilla
            if (g_game.fgStack.empty() && !g_game.pendingTransfer && g_game.transPhase != 1) {
                for (const auto& mev : g_game.events) {
                    if (mev.activePage < 0) continue;
                    const auto& pg = mev.pages[mev.activePage];
                    if (mev.x == g_game.playerX && mev.y == g_game.playerY) {
                        if (isAoOniLethalEvent(mev)) {
                            if (mev.routeWaitFrames <= 0 && !g_game.playerTransparent) {
                                triggerPlayerDeathByAoOni();
                                break;
                            }
                        } else if (pg.trigger == 2 && pg.list.a.size() > 1) {
                            startForegroundEvent(mev.id, pg.list);
                            break;
                        }
                    }
                }
            }

            // Buscar eventos Autorun (trigger == 3)
            if (g_game.fgStack.empty() && !g_game.pendingTransfer && g_game.transPhase != 1) {
                for (const auto& mev : g_game.events) {
                    if (mev.activePage < 0) continue;
                    const auto& pg = mev.pages[mev.activePage];
                    if (pg.trigger == 3 && pg.list.a.size() > 1) {
                        startForegroundEvent(mev.id, pg.list);
                        break;
                    }
                }
            }
            if (g_game.fgStack.empty() && !g_game.pendingTransfer && g_game.transPhase != 1) {
                for (const auto& ce : g_game.commonEvents) {
                    if (ce.trigger == 1 && g_game.switches[ce.switch_id]) {
                        startForegroundEvent(0, ce.list);
                        break;
                    }
                }
            }

            // Ejecutar pila del intérprete foreground
            while (!g_game.fgStack.empty() && !g_game.pendingTransfer && g_game.transPhase != 1) {
                InterpFrame& top = g_game.fgStack.back();
                int finishedEvId = top.evId;
                int finishedMapId = top.mapId;
                size_t finishedListLen = top.list.a.size();
                MapEvent* startEv = (finishedEvId > 0 && finishedMapId == g_game.mapId) ? findEvent(finishedEvId) : nullptr;
                int startActivePage = startEv ? startEv->activePage : -1;
                size_t prevDepth = g_game.fgStack.size();
                runInterpreterSlice(top.list, top.ip, g_game.fgWaitFrames, top.evId, true);
                if (g_game.fgStack.size() != prevDepth) continue; // Se hizo push de un CommonEvent (cmd 117)
                if (top.ip >= top.list.a.size() && !g_game.msgActive && !g_game.choiceActive &&
                    !g_game.numInputActive && !g_game.btnInputActive && !g_game.nameInputActive && !g_game.fgWaitForMove) {
                    g_game.fgStack.pop_back();
                    // Protección anti-bloqueo exclusiva para los 5 eventos Autorun de 1 comando sin Erase Event (Map 23, 27, 32, 38, 71)
                    if (g_game.fgStack.empty() && !g_game.pendingTransfer && finishedEvId > 0 &&
                        finishedMapId == g_game.mapId && finishedListLen <= 3) {
                        MapEvent* endEv = findEvent(finishedEvId);
                        if (endEv && !endEv->erased && endEv->activePage >= 0 &&
                            endEv->activePage == startActivePage &&
                            endEv->pages[endEv->activePage].trigger == 3) {
                            endEv->erased = true;
                            endEv->activePage = -1;
                        }
                    }
                } else {
                    break;
                }
            }

            // Ejecutar eventos paralelos (trigger == 4) y CommonEvents paralelos (trigger == 2) SOLO fuera de transiciones/pantallas negras
            if (g_game.transPhase == 0 && !g_game.pendingTransfer) {
                for (size_t i = 0; i < g_game.events.size(); i++) {
                    auto& mev = g_game.events[i];
                    if (mev.activePage < 0) continue;
                    const auto& pg = mev.pages[mev.activePage];
                    if (pg.trigger == 4 && pg.list.a.size() > 1) {
                        runInterpreterSlice(pg.list, mev.parIp, mev.parWait, mev.id, false);
                        if (mev.activePage >= 0 && !mev.erased && mev.parIp >= mev.pages[mev.activePage].list.a.size())
                            mev.parIp = 0;
                    }
                }
                for (auto& ce : g_game.commonEvents) {
                    if (ce.trigger == 2 && g_game.switches[ce.switch_id]) {
                        runInterpreterSlice(ce.list, ce.parIp, ce.parWait, 0, false);
                        if (ce.parIp >= ce.list.a.size()) ce.parIp = 0;
                    }
                }
            }

            // Gestión de transiciones suaves entre mapas (cmd 201 + fade out / fade in)
            if (g_game.pendingTransfer) {
                bool wasNoFade = g_game.nextNoFade;
                if (wasNoFade || g_game.screenBlackout || g_game.fadeAlpha >= 250) {
                    g_game.pendingTransfer = false;
                    g_game.nextNoFade = false;
                    int tm = g_game.nextMapId, tx = g_game.nextX, ty = g_game.nextY, td = g_game.nextDir;
                    if (loadMap(tm)) {
                        g_game.playerX = tx;
                        g_game.playerY = ty;
                        g_game.playerRealX = (float)(tx * TILE_SZ);
                        g_game.playerRealY = (float)(ty * TILE_SZ);
                        g_game.playerPattern = 0;
                        if (td > 0) g_game.playerDir = td;
                        // QoL: Auto-guardado de punto de control al entrar a una sala segura (fuera de persecución y del minijuego puzzle_player)
                        if (tm != 75 && tm != 35 && tm != 60 && g_game.playerChar != "puzzle_player" &&
                            !g_game.switches[49] && !g_game.switches[50] && !g_game.playerTransparent) {
                            captureSnapshot(g_autoSave);
                        }
                    }
                    if (wasNoFade) {
                        g_game.fadeAlpha = 0;
                        g_game.transPhase = 0;
                    } else if (!g_game.screenBlackout) {
                        g_game.fadeAlpha = 255;
                        g_game.transPhase = 2;
                    }
                } else {
                    g_game.transPhase = 1;
                    g_game.fadeAlpha = std::min(255, g_game.fadeAlpha + 24);
                }
            } else if (g_game.transPhase == 2) {
                g_game.fadeAlpha = std::max(0, g_game.fadeAlpha - 22);
                if (g_game.fadeAlpha == 0) g_game.transPhase = 0;
            }
            if (g_game.gameOverTriggered) {
                playBgm("");
                playBgs("");
                state = STATE_GAMEOVER;
            } else if (g_game.returnToTitleTriggered) {
                playBgm("");
                playBgs("");
                setPadLightbar(25, 45, 160);
                state = STATE_TITLE;
            }
        }

        // ==================== RENDERIZADO EN LIENZO 640x480 + ESCALADOR 4K PS4 PRO ====================
        clearSurfFast(gameSurf);

        if (state == STATE_TITLE) {
            if (titleSurf) {
                SDL_Rect dst = { offX, offY, GAME_W, GAME_H };
                blitSurf(titleSurf, NULL, gameSurf, &dst);
            }
            auto drawCentered = [&](int cy, const std::string& txt, int sc, uint8_t cr, uint8_t cg, uint8_t cb) {
                int tx = offX + std::max(8, (GAME_W - measureTextWidth(txt, sc)) / 2);
                drawText(gameSurf, tx, cy, txt, sc, cr, cg, cb);
            };
            drawCentered(offY + 54, "AO ONI - PS4 PORT", 3, 220, 220, 255);
            drawCentered(offY + 90, "Edici\xC3\xB3n 1:1 PS4 / PS4 Pro 4K  (60 FPS)", 2, 160, 195, 255);
            drawCentered(offY + 114, "Port by Outbreak2012", 2, 255, 225, 130);

            drawWindowBox(gameSurf, offX + 130, offY + 146, 395, 186, 230);

            std::string opt2 = std::string("VIDEO: ") + getVideoResLabel();
            std::string opt3 = std::string("ASPECTO: ") + getVideoScaleLabel();
            const char* opts[4] = { "NUEVA PARTIDA", "CONTINUAR", opt2.c_str(), opt3.c_str() };
            bool hasSave = g_memorySave.valid || g_autoSave.valid;
            for (int i = 0; i < 4; i++) {
                if (i == menuIndex) {
                    fillRectFast(gameSurf, offX + 140, offY + 156 + i * 42, 375, 32, 60, 90, 160, 210);
                }
                uint8_t tc = (i == 1 && !hasSave) ? 120 : 255;
                drawText(gameSurf, offX + 150, offY + 163 + i * 42, opts[i], 2, tc, tc, tc);
            }
            if (aooniSurf) {
                int cw = aooniSurf->w / 4, ch = aooniSurf->h / 4;
                SDL_Rect src = { (int)((frame / 15) % 4) * cw, 0, cw, ch };
                SDL_Rect dst = { offX + 24, offY + 188, cw * 2, ch * 2 };
                blitSurf(aooniSurf, &src, gameSurf, &dst);
            }
            drawCentered(offY + 412, "X: Aceptar | R2: Resoluci\xC3\xB3n | L2: Aspecto | 60 FPS", 2, 190, 205, 235);
            drawCentered(offY + 438, "L1/R1: Correr | []: Guardar | TOUCHPAD: Brillo ON/OFF", 2, 165, 180, 215);
        } else if (state == STATE_GAMEOVER) {
            if (goSurf) {
                SDL_Rect dst = { offX, offY, GAME_W, GAME_H };
                blitSurf(goSurf, NULL, gameSurf, &dst);
            } else {
                int goX = offX + std::max(8, (GAME_W - measureTextWidth("GAME OVER", 4)) / 2);
                drawText(gameSurf, goX, offY + 210, "GAME OVER", 4, 220, 40, 40);
            }
            if (g_autoSave.valid || g_memorySave.valid) {
                std::string msgGo = "X: Reintentar desde \xC3\xBAltima sala | C\xC3\xADrculo: T\xC3\xADtulo";
                int gx = offX + std::max(8, (GAME_W - measureTextWidth(msgGo, 2)) / 2);
                drawText(gameSurf, gx, offY + 424, msgGo, 2, 240, 220, 220);
            } else {
                std::string msgGo = "Pulsa X para volver al t\xC3\xADtulo";
                int gx = offX + std::max(8, (GAME_W - measureTextWidth(msgGo, 2)) / 2);
                drawText(gameSurf, gx, offY + 424, msgGo, 2, 220, 220, 220);
            }
        } else if (state == STATE_INVENTORY) {
            drawWindowBox(gameSurf, offX + 24, offY + 24, GAME_W - 48, GAME_H - 48, 230);
            drawText(gameSurf, offX + 40, offY + 36, "OBJETOS | X: Usar | []: Guardar | O/OPT: Volver", 2, 255, 235, 140);
            int totalSec = (int)(g_game.frameCount / 60);
            char hdrInfo[80];
            snprintf(hdrInfo, sizeof(hdrInfo), "Tiempo: %02d:%02d  |  Brillo: %s  |  Outbreak2012",
                     totalSec / 60, totalSec % 60, g_highlightObjects ? "ON" : "OFF");
            drawText(gameSurf, offX + 40, offY + 58, hdrInfo, 2, 170, 210, 245);
            drawText(gameSurf, offX + 40, offY + GAME_H - 52, "TOUCHPAD: Brillo | L1+R1+OPT: Salir al t\xC3\xADtulo", 2, 160, 175, 210);

            int iy = offY + 88;
            if (ownedItems.empty()) {
                drawText(gameSurf, offX + 48, iy, "(No tienes objetos todav\xC3\xAD" "a)", 2, 180, 180, 180);
            } else {
                const int maxVisible = 9;
                if (invCursor >= (int)ownedItems.size()) invCursor = (int)ownedItems.size() - 1;
                int invScroll = std::max(0, invCursor - (maxVisible - 1));
                size_t endIdx = std::min(ownedItems.size(), (size_t)(invScroll + maxVisible));
                for (size_t idx = (size_t)invScroll; idx < endIdx; idx++) {
                    int id = ownedItems[idx];
                    if ((int)idx == invCursor) {
                        fillRectFast(gameSurf, offX + 38, iy - 4, GAME_W - 76, 28, 60, 90, 165, 215);
                    }
                    std::string iname = g_game.itemDefs.count(id) ? g_game.itemDefs[id].name : ("Item #" + std::to_string(id));
                    std::string iicon = g_game.itemDefs.count(id) ? g_game.itemDefs[id].icon : "";
                    int textX = offX + 48;
                    if (!iicon.empty()) {
                        SDL_Surface* icSurf = getSurf(std::string("/app0/assets/images/icon_") + iicon + ".png");
                        if (icSurf) {
                            SDL_Rect idst = { offX + 44, iy - 2, 24, 24 };
                            blitSurf(icSurf, NULL, gameSurf, &idst);
                            textX = offX + 76;
                        }
                    }
                    drawText(gameSurf, textX, iy + 1, iname, 2, 255, 255, 255);
                    iy += 30;
                }
                if (invCursor >= 0 && invCursor < (int)ownedItems.size()) {
                    int selId = ownedItems[invCursor];
                    std::string desc = g_game.itemDefs.count(selId) ? g_game.itemDefs[selId].desc : "";
                    drawWindowBox(gameSurf, offX + 36, offY + GAME_H - 102, GAME_W - 72, 46, 225);
                    std::string wrappedDesc = wrapTextToWidth(desc, GAME_W - 96, 2);
                    bool multiLineDesc = (wrappedDesc.find('\n') != std::string::npos);
                    drawTextLineStep(gameSurf, offX + 48, multiLineDesc ? (offY + GAME_H - 99) : (offY + GAME_H - 89),
                                     wrappedDesc, 2, 19, 210, 220, 255);
                }
            }
            if (!invStatusMsg.empty()) {
                drawText(gameSurf, offX + 46, offY + GAME_H - 126, invStatusMsg, 2, 140, 255, 160);
            }
        } else if (state == STATE_MAP) {
            int pPxX = (int)std::round(g_game.playerRealX);
            int pPxY = (int)std::round(g_game.playerRealY);
            int camX = pPxX - GAME_W / 2 + TILE_SZ / 2;
            int camY = pPxY - GAME_H / 2 + TILE_SZ / 2;
            if (g_game.mapW * TILE_SZ > GAME_W)
                camX = std::max(0, std::min(camX, g_game.mapW * TILE_SZ - GAME_W));
            else camX = (g_game.mapW * TILE_SZ - GAME_W) / 2;
            if (g_game.mapH * TILE_SZ > GAME_H)
                camY = std::max(0, std::min(camY, g_game.mapH * TILE_SZ - GAME_H));
            else camY = (g_game.mapH * TILE_SZ - GAME_H) / 2;

            // 1) Dibujar capa inferior pre-renderizada del mapa (1 solo blit!)
            if (g_game.mapLowerSurf) {
                SDL_Rect src = { camX, camY, GAME_W, GAME_H };
                SDL_Rect dst = { offX, offY, GAME_W, GAME_H };
                blitSurf(g_game.mapLowerSurf, &src, gameSurf, &dst);
            }

            // 2) Ordenar por screen_z 1:1 RPG Maker XP e intercalar tiles con prioridad > 0 y sprites
            struct SpriteDraw { int z; int order; bool isPlayer; const MapEvent* ev; };
            static std::vector<SpriteDraw> drawList;
            drawList.clear();

            static std::string s_cachedPlayerChar;
            static SDL_Surface* s_cachedPlayerSurf = nullptr;
            if (s_cachedPlayerChar != g_game.playerChar) {
                s_cachedPlayerChar = g_game.playerChar;
                s_cachedPlayerSurf = !s_cachedPlayerChar.empty() ? getSurf(std::string("/app0/assets/images/char_") + s_cachedPlayerChar + ".png") : nullptr;
            }
            static std::string s_cachedTsName;
            static SDL_Surface* s_cachedTsSurf = nullptr;
            if (s_cachedTsName != g_game.tilesetFile) {
                s_cachedTsName = g_game.tilesetFile;
                s_cachedTsSurf = g_game.tilesetFile[0] ? getSurf(std::string("/app0/assets/images/ts_") + s_cachedTsName + ".png") : nullptr;
            }
            static std::string s_cachedAtNames[7];
            static SDL_Surface* s_cachedAtSurfs[7] = {nullptr};
            for (int i = 0; i < 7; i++) {
                if (s_cachedAtNames[i] != g_game.autotileFiles[i]) {
                    s_cachedAtNames[i] = g_game.autotileFiles[i];
                    s_cachedAtSurfs[i] = g_game.autotileFiles[i][0]
                        ? getSurf(std::string("/app0/assets/images/at_") + s_cachedAtNames[i] + ".png")
                        : nullptr;
                }
            }

            for (const auto& mev : g_game.events) {
                if (mev.activePage < 0 || (mev.char_name.empty() && mev.tile_id <= 0)) continue;
                if (mev.x >= 0 && mev.x < g_game.mapW && mev.y >= 0 && mev.y < g_game.mapH &&
                    (size_t)(mev.y * g_game.mapW + mev.x) < g_game.maskedPenTiles.size() &&
                    g_game.maskedPenTiles[mev.y * g_game.mapW + mev.x] != 0) {
                    continue;
                }
                int sh = (!mev.char_name.empty() && mev.cachedSurf) ? (mev.cachedSurf->h / 4) : TILE_SZ;
                uint8_t tPrio = (mev.tile_id >= 384 && (size_t)mev.tile_id < g_game.priorities.size()) ? g_game.priorities[mev.tile_id] : 0;
                int sz = calcSpriteScreenZ(mev.realY, sh, mev.tile_id, tPrio, mev.always_top);
                drawList.push_back({ sz, mev.always_top ? 1 : 0, false, &mev });
            }
            if (!g_game.playerTransparent && !g_game.playerChar.empty()) {
                int psh = s_cachedPlayerSurf ? (s_cachedPlayerSurf->h / 4) : 48;
                int pz = calcSpriteScreenZ(g_game.playerRealY, psh, 0, 0, false);
                drawList.push_back({ pz, 0, true, nullptr });
            }
            std::stable_sort(drawList.begin(), drawList.end(), [](const SpriteDraw& a, const SpriteDraw& b) {
                if (a.order != b.order) return a.order < b.order;
                if (a.z != b.z) return a.z < b.z;
                return (a.isPlayer ? 1 : 0) < (b.isPlayer ? 1 : 0);
            });

            auto drawSpriteEntry = [&](const SpriteDraw& sd) {
                if (sd.isPlayer) {
                    SDL_Surface* ps = s_cachedPlayerSurf;
                    if (!ps) return;
                    int cw = ps->w / 4, ch = ps->h / 4;
                    int jumpOff = (g_game.playerJumpTimer > 0 && g_game.playerJumpTotal > 0)
                        ? (int)(56.0f * ((float)g_game.playerJumpTimer / g_game.playerJumpTotal) * (1.0f - (float)g_game.playerJumpTimer / g_game.playerJumpTotal)) : 0;
                    SDL_Rect src = { g_game.playerPattern * cw, rmDirToRow(g_game.playerDir) * ch, cw, ch };
                    int px = offX + (pPxX - camX) - (cw - TILE_SZ) / 2;
                    int py = offY + (pPxY - camY) - (ch - TILE_SZ) - jumpOff;
                    SDL_Rect dst = { px, py, cw, ch };
                    blitSurf(ps, &src, gameSurf, &dst);
                } else if (sd.ev) {
                    int evPxX = (int)std::round(sd.ev->realX);
                    int evPxY = (int)std::round(sd.ev->realY);
                    int jumpOff = (sd.ev->jumpTimer > 0 && sd.ev->jumpTotal > 0)
                        ? (int)(56.0f * ((float)sd.ev->jumpTimer / sd.ev->jumpTotal) * (1.0f - (float)sd.ev->jumpTimer / sd.ev->jumpTotal)) : 0;
                    if (!sd.ev->char_name.empty()) {
                        SDL_Surface* es = sd.ev->cachedSurf;
                        if (!es) return;
                        int cw = es->w / 4, ch = es->h / 4;
                        SDL_Rect src = { sd.ev->pattern * cw, rmDirToRow(sd.ev->dir) * ch, cw, ch };
                        int ex = offX + (evPxX - camX) - (cw - TILE_SZ) / 2;
                        int ey = offY + (evPxY - camY) - (ch - TILE_SZ) - jumpOff;
                        SDL_Rect dst = { ex, ey, cw, ch };
                        blitSurf(es, &src, gameSurf, &dst, (uint8_t)sd.ev->opacity);
                    } else if (sd.ev->tile_id >= 384) {
                        if (s_cachedTsSurf) {
                            int tIndex = sd.ev->tile_id - 384;
                            SDL_Rect src = { (tIndex % 8) * TILE_SZ, (tIndex / 8) * TILE_SZ, TILE_SZ, TILE_SZ };
                            SDL_Rect dst = { offX + (evPxX - camX), offY + (evPxY - camY) - jumpOff, TILE_SZ, TILE_SZ };
                            blitSurf(s_cachedTsSurf, &src, gameSurf, &dst, (uint8_t)sd.ev->opacity);
                        }
                    }
                }
            };

            auto drawPrioTile = [&](const GameRuntime::PrioTile& pt) {
                int dx = offX + pt.tx * TILE_SZ - camX;
                int dy = offY + pt.ty * TILE_SZ - camY;
                if (dx + TILE_SZ <= 0 || dx >= gameSurf->w || dy + TILE_SZ <= 0 || dy >= gameSurf->h) return;
                if (pt.tid >= 384) {
                    if (s_cachedTsSurf) {
                        int tIndex = pt.tid - 384;
                        SDL_Rect src = { (tIndex % 8) * TILE_SZ, (tIndex / 8) * TILE_SZ, TILE_SZ, TILE_SZ };
                        SDL_Rect dst = { dx, dy, TILE_SZ, TILE_SZ };
                        blitSurf(s_cachedTsSurf, &src, gameSurf, &dst);
                    }
                } else if (pt.tid >= 48) {
                    int atIdx = (pt.tid / 48) - 1;
                    int pat = pt.tid % 48;
                    if (atIdx >= 0 && atIdx < 7 && s_cachedAtSurfs[atIdx])
                        blitAutotileToCache(s_cachedAtSurfs[atIdx], pat, gameSurf, dx, dy, true);
                }
            };

            // 3) Dibujar en orden Z unificado: tiles con prioridad > 0 intercalados con eventos y jugador
            size_t ptIdx = 0;
            const size_t ptCount = g_game.prioTiles.size();
            for (const auto& sd : drawList) {
                if (sd.order > 0) break;
                while (ptIdx < ptCount && g_game.prioTiles[ptIdx].z <= sd.z) {
                    drawPrioTile(g_game.prioTiles[ptIdx++]);
                }
                drawSpriteEntry(sd);
            }
            while (ptIdx < ptCount) {
                drawPrioTile(g_game.prioTiles[ptIdx++]);
            }

            // 3.5) Eventos con always_on_top == true (RGSS1 screen_z = 999, por encima de prio > 0, ej. Map 25 Ev 6 oil, Map 60 puzzle_parts)
            for (const auto& sd : drawList) {
                if (sd.order > 0) drawSpriteEntry(sd);
            }

            // 4) Aplicar tono de pantalla (cmd 223, ej: -85,-85,-85 en el interior de la mansión)
            int tr = (int)std::round(g_game.curToneR);
            int tg = (int)std::round(g_game.curToneG);
            int tb = (int)std::round(g_game.curToneB);
            if (tr != 0 || tg != 0 || tb != 0) {
                applyToneRectFast(gameSurf, offX, offY, GAME_W, GAME_H, tr, tg, tb);
            }

            // 4.5) Brillo resplandeciente post-tono para llaves, ítems recogibles y pistas pequeñas
            if (!g_game.screenBlackout && g_game.fadeAlpha < 254) {
                for (const auto& mev : g_game.events) {
                    if (mev.erased || mev.activePage < 0) continue;
                    if (mev.x >= 0 && mev.x < g_game.mapW && mev.y >= 0 && mev.y < g_game.mapH &&
                        (size_t)(mev.y * g_game.mapW + mev.x) < g_game.maskedPenTiles.size() &&
                        g_game.maskedPenTiles[mev.y * g_game.mapW + mev.x] != 0) {
                        continue;
                    }
                    int evPxX = (int)std::round(mev.realX);
                    int evPxY = (int)std::round(mev.realY);
                    if (mev.step_anime && isSparkleSheetName(mev.char_name)) {
                        drawSparkleStarGlow(gameSurf, offX, offY, camX, camY, mev, evPxX, evPxY);
                    } else if (g_highlightObjects && (mev.grantsItem || mev.isTinyInteractive)) {
                        drawLivingItemGlint(gameSurf, offX, offY, camX, camY, mev, evPxX, evPxY);
                    }
                }
                drawActiveAnimation(gameSurf, offX, offY, camX, camY);
                drawRainWeather(gameSurf, offX, offY, frame);
                drawInteractPrompt(gameSurf, offX, offY, camX, camY, frame);
            }

            // 5) Dibujar Pictures activas (1..50, con soporte de origin, zoom y rotación cmd 233)
            for (int i = 1; i <= 50; i++) {
                const Picture& pic = g_game.pictures[i];
                if (!pic.active || pic.name.empty() || pic.opacity <= 0.5f) continue;
                SDL_Surface* ps = pic.cachedSurf ? pic.cachedSurf : getSurf(std::string("/app0/assets/images/pic_") + pic.name + ".png");
                if (!ps) continue;
                uint8_t picAlpha = (uint8_t)std::min(255.0f, pic.opacity);
                int dw = std::max(1, (int)(ps->w * pic.zoomX / 100.0f));
                int dh = std::max(1, (int)(ps->h * pic.zoomY / 100.0f));
                if (std::fabs(pic.angle) >= 0.1f) {
                    float cx = offX + pic.x + (pic.origin == 1 ? 0.0f : dw * 0.5f);
                    float cy = offY + pic.y + (pic.origin == 1 ? 0.0f : dh * 0.5f);
                    blitSurfRotated(ps, gameSurf, cx, cy, pic.zoomX, pic.zoomY, pic.angle, picAlpha);
                } else {
                    int dx = offX + (int)pic.x - (pic.origin == 1 ? dw / 2 : 0);
                    int dy = offY + (int)pic.y - (pic.origin == 1 ? dh / 2 : 0);
                    SDL_Rect dst = { dx, dy, dw, dh };
                    blitSurf(ps, NULL, gameSurf, &dst, picAlpha);
                }
            }

            // 5.5) Aplicar fundido a negro de transición entre habitaciones o cmd 221/222
            if (g_game.screenBlackout) {
                fillRectFast(gameSurf, offX, offY, GAME_W, GAME_H, 0, 0, 0, 255);
            } else if (g_game.fadeAlpha > 0) {
                fillRectFast(gameSurf, offX, offY, GAME_W, GAME_H, 0, 0, 0, (uint8_t)std::min(255, g_game.fadeAlpha));
            }

            // 5.6) Cronómetro en pantalla si el Easter Egg "TIMER" (switch 43 / 026_Sprite_Timer.rb) está activo
            if (g_game.switches[43]) {
                uint32_t totalFrames = g_game.frameCount;
                int mins = (int)((totalFrames / 3600) % 100);
                int secs = (int)((totalFrames / 60) % 60);
                int cent = (int)(((totalFrames % 60) * 100) / 60);
                char tbuf[32];
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d.%02d", mins, secs, cent);
                fillRectFast(gameSurf, offX + 12, offY + 12, 116, 26, 15, 22, 68, 210);
                drawRectFast(gameSurf, offX + 12, offY + 12, 116, 26, 190, 210, 255);
                drawText(gameSurf, offX + 20, offY + 18, tbuf, 2, 255, 240, 130);
                bool isSprinting = (padL1 || padR1 || ((curPadMask & (1u << PAD_CIRCLE)) != 0 && g_game.mapId != 60) ||
                                    ((curPadMask & (1u << PAD_L2)) != 0) || ((curPadMask & (1u << PAD_R2)) != 0) ||
                                    g_game.switches[49]);
                if (isSprinting && isPlayerMoving()) {
                    fillRectFast(gameSurf, offX + 134, offY + 12, 98, 26, 20, 60, 110, 220);
                    drawRectFast(gameSurf, offX + 134, offY + 12, 98, 26, 140, 210, 255);
                    drawText(gameSurf, offX + 142, offY + 18, "CORRER", 2, 180, 240, 255);
                }
                if (g_game.timerWorking) {
                    int chaseSec = g_game.timerFrames / 60;
                    char cbuf[32];
                    snprintf(cbuf, sizeof(cbuf), "ONI %02d:%02d", chaseSec / 60, chaseSec % 60);
                    int oniBoxX = (isSprinting && isPlayerMoving()) ? (offX + 240) : (offX + 134);
                    fillRectFast(gameSurf, oniBoxX, offY + 12, 126, 26, 55, 16, 48, 220);
                    drawRectFast(gameSurf, oniBoxX, offY + 12, 126, 26, 255, 160, 210);
                    drawText(gameSurf, oniBoxX + 8, offY + 18, cbuf, 2, 255, 190, 220);
                }
            }

            if (autoSaveBannerFrames > 0) {
                std::string sbTxt = "PARTIDA GUARDADA";
                int sbW = std::max(196, measureTextWidth(sbTxt, 2) + 28);
                int sbX = offX + GAME_W - 12 - sbW;
                drawWindowBox(gameSurf, sbX, offY + 10, sbW, 32, 230);
                drawText(gameSurf, sbX + (sbW - measureTextWidth(sbTxt, 2)) / 2, offY + 16, sbTxt, 2, 140, 255, 160);
            }

            // 6) Cuadro de diálogo con Windowskin auténtica de Ao Oni
            int activeMsgBoxY = offY + GAME_H - 132;
            if (g_game.msgActive) {
                int lineCount = 1;
                for (size_t li = 0; li < g_game.msgText.size(); li++) {
                    if (g_game.msgText[li] == '\n') lineCount++;
                }
                int lineStep = (!g_game.msgSpeaker.empty())
                             ? ((lineCount >= 4) ? 18 : 22)
                             : ((lineCount >= 5) ? 17 : ((lineCount == 4) ? 20 : 22));
                int needH = (!g_game.msgSpeaker.empty() ? 38 : 16) + lineCount * lineStep + 14;
                int boxH = std::max(120, needH);
                int boxY = (g_game.msgPos == 0) ? (offY + 12)
                         : ((g_game.msgPos == 1) ? (offY + (GAME_H - boxH) / 2)
                         : (offY + GAME_H - 12 - boxH));
                activeMsgBoxY = boxY;
                drawWindowBox(gameSurf, offX + 16, boxY, GAME_W - 32, boxH, 228);

                int textX = offX + 32;
                if (!g_game.msgFace.empty()) {
                    SDL_Surface* faceSurf = getSurf(std::string("/app0/assets/images/pic_") + g_game.msgFace + ".png");
                    if (faceSurf) {
                        SDL_Rect fdst = { offX + 26, boxY + 12, faceSurf->w, faceSurf->h };
                        blitSurf(faceSurf, NULL, gameSurf, &fdst);
                        textX = offX + 26 + faceSurf->w + 14;
                    }
                }
                int textY = boxY + 12;
                if (!g_game.msgSpeaker.empty()) {
                    drawText(gameSurf, textX, textY, "[" + g_game.msgSpeaker + "]", 2, 255, 230, 120);
                    textY += 22;
                } else {
                    textY += 4;
                }
                size_t revLen = std::min(g_game.msgCharReveal, g_game.msgText.size());
                // Evitar partir secuencias UTF-8 multibyte a la mitad durante el efecto typewriter
                while (revLen < g_game.msgText.size() && (((unsigned char)g_game.msgText[revLen] & 0xC0) == 0x80)) revLen++;
                drawTextLineStep(gameSurf, textX, textY, g_game.msgText.substr(0, revLen), 2, lineStep, 255, 255, 255);
                if (revLen >= g_game.msgText.size() && !g_game.choiceActive && ((frame / 18) % 2 == 0)) {
                    int ax = offX + GAME_W / 2;
                    int ay = boxY + boxH - 11;
                    fillRectFast(gameSurf, ax - 4, ay, 9, 2, 255, 255, 255);
                    fillRectFast(gameSurf, ax - 3, ay + 2, 7, 2, 255, 255, 255);
                    fillRectFast(gameSurf, ax - 1, ay + 4, 3, 2, 255, 255, 255);
                }
            }

            // 7) Cuadro de elecciones (cmd 102)
            if (g_game.choiceActive && !g_game.choices.empty()) {
                int maxChW = 0;
                for (size_t i = 0; i < g_game.choices.size(); i++) {
                    maxChW = std::max(maxChW, measureTextWidth(g_game.choices[i], 2));
                }
                int chW = std::min(GAME_W - 32, std::max(204, maxChW + 32));
                int chH = (int)g_game.choices.size() * 28 + 20;
                int cx = offX + GAME_W - 16 - chW;
                int cy = (g_game.msgActive && g_game.msgPos == 2) ? (activeMsgBoxY - chH - 8) : (offY + GAME_H - 132 - chH - 8);
                if (cy < offY + 8) cy = offY + 8;
                drawWindowBox(gameSurf, cx, cy, chW, chH, 235);
                for (size_t i = 0; i < g_game.choices.size(); i++) {
                    if ((int)i == g_game.choiceIndex) {
                        fillRectFast(gameSurf, cx + 6, cy + 8 + (int)i * 28, chW - 12, 24, 65, 95, 175, 210);
                    }
                    drawText(gameSurf, cx + 14, cy + 10 + (int)i * 28, g_game.choices[i], 2, 255, 255, 255);
                }
            }

            // 8) Cuadro de introducción de código numérico compacto (cmd 103 - Cajas fuertes / Paneles)
            if (g_game.numInputActive) {
                const int cellW = 24;
                const int padX = 14;
                int bw = g_game.numInputDigits * cellW + padX * 2;
                int bh = 44;
                int bx = offX + (GAME_W - bw) / 2;
                int by = (g_game.msgPos == 0) ? (offY + 24)
                       : ((g_game.msgPos == 1) ? (offY + (GAME_H - bh) / 2)
                       : (offY + GAME_H - bh - 24));
                drawWindowBox(gameSurf, bx, by, bw, bh, 235);
                for (int d = 0; d < g_game.numInputDigits; d++) {
                    int slotX = bx + padX + d * cellW;
                    if (d == g_game.numInputCursor) {
                        fillRectFast(gameSurf, slotX + 1, by + 9, cellW - 2, 26, 65, 100, 185);
                        drawRectFast(gameSurf, slotX + 1, by + 9, cellW - 2, 26, 255, 235, 140);
                        int cx = slotX + cellW / 2;
                        fillRectFast(gameSurf, cx, by + 3, 1, 2, 255, 235, 140);
                        fillRectFast(gameSurf, cx - 1, by + 5, 3, 2, 255, 235, 140);
                        fillRectFast(gameSurf, cx - 2, by + 7, 5, 1, 255, 235, 140);
                        fillRectFast(gameSurf, cx - 2, by + 36, 5, 1, 255, 235, 140);
                        fillRectFast(gameSurf, cx - 1, by + 37, 3, 2, 255, 235, 140);
                        fillRectFast(gameSurf, cx, by + 39, 1, 2, 255, 235, 140);
                    }
                    char digStr[4];
                    snprintf(digStr, sizeof(digStr), "%d", g_game.numInputVals[d]);
                    int dx = slotX + std::max(1, (cellW - measureTextWidth(digStr, 2)) / 2);
                    drawText(gameSurf, dx, by + 12, digStr, 2, 255, 255, 255);
                }
            }

            // 9) Pantalla interactiva de Nombre del Personaje (cmd 303 + Easter Eggs Oficiales)
            if (g_game.nameInputActive) {
                drawWindowBox(gameSurf, 28, 22, GAME_W - 56, GAME_H - 44, 244);

                drawText(gameSurf, 44, 34, "NOMBRE DEL PROTAGONISTA (X: Elegir | Cuadrado: Borrar | OPTIONS: OK)", 2, 255, 235, 130);

                // Caja del nombre actual con sprite de Hiroshi
                drawWindowBox(gameSurf, 48, 66, GAME_W - 96, 52, 240);
                SDL_Surface* hSurf = getSurf("/app0/assets/images/char_hirosi.png");
                if (hSurf) {
                    int cw = hSurf->w / 4, ch = hSurf->h / 4;
                    SDL_Rect src = { 0, 0, cw, ch };
                    SDL_Rect dst = { 62, 68, cw, ch };
                    blitSurf(hSurf, &src, gameSurf, &dst);
                }
                std::string dispName = g_game.nameInputStr + ((frame / 15) % 2 == 0 ? "_" : " ");
                drawText(gameSurf, 114, 78, dispName, 3, 255, 255, 255);

                // Cuadrícula de caracteres (6 filas x 10 columnas)
                for (int r = 0; r < 6; r++) {
                    for (int c = 0; c < 10; c++) {
                        int kx = 54 + c * 53;
                        int ky = 132 + r * 34;
                        bool sel = (g_game.nameInputRow == r && g_game.nameInputCol == c);
                        if (sel) {
                            fillRectFast(gameSurf, kx - 6, ky - 4, 38, 28, 70, 108, 195);
                            drawRectFast(gameSurf, kx - 6, ky - 4, 38, 28, 255, 240, 150);
                        }
                        char chStr[2] = { NAME_KB_ROWS[r][c], '\0' };
                        int chX = (kx - 6) + std::max(2, (38 - measureTextWidth(chStr, 2)) / 2);
                        drawText(gameSurf, chX, ky, chStr, 2, 255, 255, 255);
                    }
                }

                // Fila 6: Botones de acción rápida y Nombres Truco / Easter Eggs Oficiales
                drawText(gameSurf, 48, 340, "BOTONES RAPIDOS Y MODOS SECRETOS (EASTER EGGS OFICIALES):", 2, 175, 210, 255);
                int bx = 46;
                int by = 366;
                for (int q = 0; q < 9; q++) {
                    int bw = measureTextWidth(NAME_QUICK_BTNS[q].label, 2) + 16;
                    if (bx + bw > GAME_W - 44) {
                        bx = 46;
                        by += 32;
                    }
                    bool sel = (g_game.nameInputRow == 6 && g_game.nameInputCol == q);
                    if (sel) {
                        fillRectFast(gameSurf, bx, by, bw, 26, 75, 115, 205);
                        drawRectFast(gameSurf, bx, by, bw, 26, 255, 240, 140);
                    } else {
                        fillRectFast(gameSurf, bx, by, bw, 26, 28, 42, 105);
                        drawRectFast(gameSurf, bx, by, bw, 26, 130, 150, 210);
                    }
                    uint8_t rCol = (q == 0) ? 150 : ((q >= 3) ? 255 : 235);
                    uint8_t gCol = (q == 0) ? 255 : ((q >= 3) ? 230 : 235);
                    uint8_t bCol = (q == 0) ? 170 : ((q >= 3) ? 140 : 255);
                    drawText(gameSurf, bx + 8, by + 3, NAME_QUICK_BTNS[q].label, 2, rCol, gCol, bCol);
                    bx += bw + 8;
                }

                // Descripción inferior de la tecla o Easter Egg seleccionado
                std::string hintText = "Selecciona letras con X o elige un nombre r\xC3\xA1pido abajo.";
                if (g_game.nameInputRow == 6 && g_game.nameInputCol >= 0 && g_game.nameInputCol < 9) {
                    hintText = NAME_QUICK_BTNS[g_game.nameInputCol].hint;
                }
                drawText(gameSurf, 48, 434, wrapTextToWidth(hintText, GAME_W - 96, 2), 2, 200, 235, 190);
            }
        }

        if (videoBannerFrames > 0 && !videoBannerText.empty()) {
            int txtW = measureTextWidth(videoBannerText, 2);
            int bw = std::min(GAME_W - 16, txtW + 28);
            int bx = std::max(8, (GAME_W - bw) / 2);
            drawWindowBox(gameSurf, bx, 8, bw, 32, 230);
            drawText(gameSurf, bx + std::max(8, (bw - txtW) / 2), 14, videoBannerText, 2, 255, 240, 140);
        }

        // Presentación con VSync hardware (sceVideoOut); evita doble espera de VSync que provocaría 30 FPS
        bool usedVsync = presentScaledCanvas(window, gameSurf);
        uint32_t elapsed = SDL_GetTicks() - frameStart;
        uint32_t targetMs = (frame % 3 == 0) ? 16 : 17;
        if (!usedVsync) {
            if (elapsed < targetMs) SDL_Delay(targetMs - elapsed);
        } else if (elapsed + 5 < targetMs) {
            SDL_Delay(targetMs - elapsed);
        }
    }

    g_audioRunning = false;
    SDL_Quit();
    return 0;
}
