// Ao Oni PS4 Port - Interprete RPG Maker XP (RGSS1) nativo en C++/SDL2
// Ejecuta el juego original (Map075 -> Map003 -> Map004 -> Mansion completa) con
// eventos, switches, variables, rutas de movimiento, dialogos con rostros, inventario y persecuciones.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include <SDL2/SDL.h>
#include <orbis/libkernel.h>
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

#define PAD_CROSS    0
#define PAD_CIRCLE   1
#define PAD_SQUARE   2
#define PAD_TRIANGLE 3
#define PAD_L1       4
#define PAD_R1       5
#define PAD_OPTIONS  9
#define PAD_UP       11
#define PAD_DOWN     12
#define PAD_LEFT     13
#define PAD_RIGHT    14

static const uint8_t FONT8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},{0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},{0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},{0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},{0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},{0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},{0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},{0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},{0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},{0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},{0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},{0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},{0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},{0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},{0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},{0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},{0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},{0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},{0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},{0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},{0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},{0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},{0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},{0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},{0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},{0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},{0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},{0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},{0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},{0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},{0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},{0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},{0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},{0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},{0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},{0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},{0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},{0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},{0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},{0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},{0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},{0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

// Convierte caracteres UTF-8 comunes en español a ASCII legible para la fuente 8x8
static std::string utf8ToAscii(const std::string& in) {
    std::string out;
    for (size_t i = 0; i < in.size(); i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == 0xC3 && i + 1 < in.size()) {
            unsigned char c2 = (unsigned char)in[++i];
            switch (c2) {
                case 0xA1: out += 'a'; break; case 0xA9: out += 'e'; break;
                case 0xAD: out += 'i'; break; case 0xB3: out += 'o'; break;
                case 0xBA: case 0xBC: out += 'u'; break; case 0xB1: out += 'n'; break;
                case 0x81: out += 'A'; break; case 0x89: out += 'E'; break;
                case 0x8D: out += 'I'; break; case 0x93: out += 'O'; break;
                case 0x9A: case 0x9C: out += 'U'; break; case 0x91: out += 'N'; break;
                default: out += '?'; break;
            }
        } else if (c == 0xC2 && i + 1 < in.size()) {
            unsigned char c2 = (unsigned char)in[++i];
            if (c2 == 0xBF) out += '?';
            else if (c2 == 0xA1) out += '!';
        } else if (c < 128) {
            out.push_back((char)c);
        }
    }
    return out;
}

static void drawChar(SDL_Renderer* r, int x, int y, char c, int scale, uint8_t cr, uint8_t cg, uint8_t cb) {
    int idx = (unsigned char)c;
    if (idx < 32 || idx > 127) idx = '?';
    const uint8_t* glyph = FONT8X8[idx - 32];
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(r, &px);
            }
        }
    }
}

static void drawText(SDL_Renderer* r, int x, int y, const std::string& raw, int scale, uint8_t cr, uint8_t cg, uint8_t cb) {
    std::string s = utf8ToAscii(raw);
    int cx = x;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\n') { y += 11 * scale; cx = x; continue; }
        drawChar(r, cx + scale, y + scale, s[i], scale, 0, 0, 0);
        drawChar(r, cx, y, s[i], scale, cr, cg, cb);
        cx += 8 * scale;
    }
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
    std::vector<uint8_t> buf;
    uint8_t chunk[16384];
    size_t n = 0;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf.insert(buf.end(), chunk, chunk + n);
    fclose(f);
    if (buf.empty()) { g_surfCache[path] = nullptr; return nullptr; }

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

static void blitSurf(SDL_Surface* src, const SDL_Rect* sr, SDL_Surface* dst, SDL_Rect* dr, uint8_t alphaMul = 255) {
    if (!src || !dst || !src->pixels || !dst->pixels || alphaMul == 0) return;
    int sx0 = sr ? sr->x : 0, sy0 = sr ? sr->y : 0;
    int sw  = sr ? sr->w : src->w, sh  = sr ? sr->h : src->h;
    int dx0 = dr ? dr->x : 0, dy0 = dr ? dr->y : 0;
    int dw  = dr ? dr->w : sw,    dh  = dr ? dr->h : sh;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

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
            uint8_t sr8 = sp[0], sg8 = sp[1], sb8 = sp[2];
            uint32_t sa8 = (sp[3] * alphaMul) / 255;
            if (sa8 == 0) continue;
            if (sa8 == 255) {
                drow[px] = 0xFF000000u | ((uint32_t)sr8 << 16) | ((uint32_t)sg8 << 8) | (uint32_t)sb8;
            } else {
                uint32_t dp = drow[px];
                uint8_t dr8 = (dp >> 16) & 0xFF, dg8 = (dp >> 8) & 0xFF, db8 = dp & 0xFF;
                uint8_t or8 = (uint8_t)((sr8 * sa8 + dr8 * (255 - sa8)) / 255);
                uint8_t og8 = (uint8_t)((sg8 * sa8 + dg8 * (255 - sa8)) / 255);
                uint8_t ob8 = (uint8_t)((sb8 * sa8 + db8 * (255 - sa8)) / 255);
                drow[px] = 0xFF000000u | ((uint32_t)or8 << 16) | ((uint32_t)og8 << 8) | (uint32_t)ob8;
            }
        }
    }
}

// Estado global del juego RPG Maker XP
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
    int trigger = 0; // 0=action, 1=player_touch, 2=event_touch, 3=autorun, 4=parallel
    JVal list; // array de {c, i, p}
};

struct MapEvent {
    int id = 0;
    std::string name;
    int x = 0, y = 0;
    int dir = 2, pattern = 0, opacity = 255;
    std::string char_name;
    bool through = false, always_top = false, erased = false;
    int move_type = 0, move_speed = 3, move_freq = 3;
    std::vector<EventPage> pages;
    int activePage = -1;
    uint32_t lastAutoMoveTick = 0;

    // Ruta forzada por Set Move Route (cmd 209)
    bool routeActive = false;
    MoveRoute forcedRoute;
    size_t routeIdx = 0;
    uint32_t lastRouteStepTick = 0;

    // Intérprete paralelo propio (trigger == 4)
    size_t parIp = 0;
    int parWait = 0;
};

struct Picture {
    bool active = false;
    std::string name;
    int x = 0, y = 0, opacity = 255;
};

struct ItemDef { std::string name, icon, desc; };
struct CommonEventDef { int id = 0, trigger = 0, switch_id = 1; JVal list; size_t parIp = 0; int parWait = 0; };

struct GameRuntime {
    bool switches[512] = {false};
    int variables[512] = {0};
    std::map<std::string, bool> selfSwitches; // "mapId_evId_ch" -> bool
    std::map<int, int> inventory; // itemId -> count
    std::map<int, std::string> actorNames;
    std::map<int, std::string> actorChars;
    std::map<int, ItemDef> itemDefs;
    std::vector<CommonEventDef> commonEvents;
    Picture pictures[21];

    int mapId = 75;
    int mapW = 20, mapH = 15, tilesetId = 1;
    char tilesetFile[64] = {0};
    char autotileFiles[7][64] = {{0}};
    std::vector<int16_t> layers;
    std::vector<uint8_t> passable;
    std::vector<uint8_t> priorities;
    std::vector<MapEvent> events;

    int playerX = 9, playerY = 8, playerDir = 2, playerPattern = 0;
    std::string playerChar = "hirosi";
    bool playerTransparent = true; // En Map075 empieza invisible hasta que cambia el actor/sprite
    bool playerThrough = false;
    int playerSpeed = 4;
    bool menuDisabled = false;

    bool playerRouteActive = false;
    MoveRoute playerForcedRoute;
    size_t playerRouteIdx = 0;
    uint32_t lastPlayerRouteTick = 0;

    // Temporizador (cmd 124)
    bool timerWorking = false;
    int timerFrames = 0;

    // Tono de pantalla (cmd 223)
    int toneR = 0, toneG = 0, toneB = 0;

    // Intérprete principal (foreground)
    bool fgRunning = false;
    int fgEventId = 0;
    JVal fgList;
    size_t fgIp = 0;
    int fgWaitFrames = 0;
    bool fgWaitForMove = false;

    // Cuadro de mensaje / elecciones
    bool msgActive = false;
    std::string msgFace;
    std::string msgSpeaker;
    std::string msgText;
    int msgPos = 2; // 0=arriba, 1=centro, 2=abajo
    bool choiceActive = false;
    std::vector<std::string> choices;
    int choiceIndex = 0;
    int choiceIndent = 0;

    // Cambio de mapa pendiente
    bool pendingTransfer = false;
    int nextMapId = 0, nextX = 0, nextY = 0, nextDir = 0;
    bool gameOverTriggered = false;
    uint32_t frameCount = 0;
} g_game;

static int rmDirToRow(int d) {
    if (d == 4) return 1;
    if (d == 6) return 2;
    if (d == 8) return 3;
    return 0; // 2 = abajo
}

static MapEvent* findEvent(int id) {
    for (auto& ev : g_game.events)
        if (ev.id == id) return &ev;
    return nullptr;
}

static bool checkPageCond(int mapId, int evId, const EventPage& p) {
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

static void refreshEvents() {
    for (auto& ev : g_game.events) {
        if (ev.erased) { ev.activePage = -1; continue; }
        int found = -1;
        for (int i = (int)ev.pages.size() - 1; i >= 0; i--) {
            if (checkPageCond(g_game.mapId, ev.id, ev.pages[i])) {
                found = i;
                break;
            }
        }
        if (found != ev.activePage) {
            ev.activePage = found;
            ev.parIp = 0;
            ev.parWait = 0;
            if (found >= 0) {
                const auto& pg = ev.pages[found];
                ev.char_name = pg.char_name;
                ev.dir = pg.dir;
                ev.pattern = pg.pattern;
                ev.opacity = pg.opacity;
                ev.through = pg.through;
                ev.always_top = pg.always_top;
                ev.move_type = pg.move_type;
                ev.move_speed = pg.move_speed;
                ev.move_freq = pg.move_freq;
            } else {
                ev.char_name.clear();
            }
        }
    }
}

static bool canPassTile(int tx, int ty, bool isPlayer, int selfEvId = -1) {
    if (tx < 0 || ty < 0 || tx >= g_game.mapW || ty >= g_game.mapH) return false;
    for (const auto& ev : g_game.events) {
        if (ev.id == selfEvId || ev.activePage < 0 || ev.through) continue;
        if (ev.x == tx && ev.y == ty && !ev.char_name.empty()) {
            // Si es una puerta con trigger de contacto (1 o 2), permitir pisarla para que dispare
            int trig = ev.pages[ev.activePage].trigger;
            if (isPlayer && (trig == 1 || trig == 2)) continue;
            return false;
        }
    }
    if (!isPlayer && !g_game.playerThrough && g_game.playerX == tx && g_game.playerY == ty)
        return false;

    for (int z = 2; z >= 0; z--) {
        int16_t tid = g_game.layers[tx + ty * g_game.mapW + z * g_game.mapW * g_game.mapH];
        if (tid > 0 && (size_t)tid < g_game.passable.size()) {
            uint8_t p = g_game.passable[tid];
            if ((p & 0x0F) == 0x0F) return false;
            if ((p & 0x0F) == 0x00) return true;
        }
    }
    return true;
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

static void stepCharacterDir(int& x, int& y, int& dir, int d, bool through, bool isPlayer, int evId) {
    dir = d;
    int nx = x + (d == 4 ? -1 : (d == 6 ? 1 : 0));
    int ny = y + (d == 8 ? -1 : (d == 2 ? 1 : 0));
    if (through || canPassTile(nx, ny, isPlayer, evId)) {
        x = nx; y = ny;
    }
}

static void stepTowardPlayer(MapEvent& ev) {
    int dx = g_game.playerX - ev.x;
    int dy = g_game.playerY - ev.y;
    int d1 = (std::abs(dx) >= std::abs(dy)) ? (dx > 0 ? 6 : 4) : (dy > 0 ? 2 : 8);
    int d2 = (std::abs(dx) >= std::abs(dy)) ? (dy > 0 ? 2 : 8) : (dx > 0 ? 6 : 4);
    int ox = ev.x, oy = ev.y;
    stepCharacterDir(ev.x, ev.y, ev.dir, d1, ev.through, false, ev.id);
    if (ev.x == ox && ev.y == oy && (dx != 0 || dy != 0))
        stepCharacterDir(ev.x, ev.y, ev.dir, d2, ev.through, false, ev.id);
}

static bool applyMoveCmd(int targetId, const MoveCmd& mc) {
    int* px = (targetId == -1) ? &g_game.playerX : nullptr;
    int* py = (targetId == -1) ? &g_game.playerY : nullptr;
    int* pdir = (targetId == -1) ? &g_game.playerDir : nullptr;
    bool* pthr = (targetId == -1) ? &g_game.playerThrough : nullptr;
    std::string* pchar = (targetId == -1) ? &g_game.playerChar : nullptr;

    MapEvent* ev = (targetId > 0) ? findEvent(targetId) : nullptr;
    if (targetId > 0 && !ev) return true;
    if (ev) { px = &ev->x; py = &ev->y; pdir = &ev->dir; pthr = &ev->through; pchar = &ev->char_name; }

    switch (mc.code) {
        case 1: stepCharacterDir(*px, *py, *pdir, 2, *pthr, targetId == -1, targetId); break; // Down
        case 2: stepCharacterDir(*px, *py, *pdir, 4, *pthr, targetId == -1, targetId); break; // Left
        case 3: stepCharacterDir(*px, *py, *pdir, 6, *pthr, targetId == -1, targetId); break; // Right
        case 4: stepCharacterDir(*px, *py, *pdir, 8, *pthr, targetId == -1, targetId); break; // Up
        case 5: (*px)--; (*py)++; *pdir = 4; break;
        case 6: (*px)++; (*py)++; *pdir = 6; break;
        case 7: (*px)--; (*py)--; *pdir = 4; break;
        case 8: (*px)++; (*py)--; *pdir = 6; break;
        case 9: stepCharacterDir(*px, *py, *pdir, (rand() % 4 + 1) * 2, *pthr, targetId == -1, targetId); break;
        case 10: if (ev) stepTowardPlayer(*ev); break;
        case 12: stepCharacterDir(*px, *py, *pdir, *pdir, *pthr, targetId == -1, targetId); break;
        case 13: { // 1 Step Backward
            int od = *pdir;
            int bd = (od == 2 ? 8 : (od == 8 ? 2 : (od == 4 ? 6 : 4)));
            stepCharacterDir(*px, *py, *pdir, bd, *pthr, targetId == -1, targetId);
            *pdir = od;
            break;
        }
        case 14: // Jump(dx, dy)
            *px += mc.params[0].asInt(0);
            *py += mc.params[1].asInt(0);
            break;
        case 16: *pdir = 2; break;
        case 17: *pdir = 4; break;
        case 18: *pdir = 6; break;
        case 19: *pdir = 8; break;
        case 25: if (ev) {
            int dx = g_game.playerX - ev->x, dy = g_game.playerY - ev->y;
            ev->dir = (std::abs(dx) >= std::abs(dy)) ? (dx >= 0 ? 6 : 4) : (dy >= 0 ? 2 : 8);
        } break;
        case 27: g_game.switches[mc.params[0].asInt(1)] = true; refreshEvents(); break;
        case 28: g_game.switches[mc.params[0].asInt(1)] = false; refreshEvents(); break;
        case 29: if (targetId == -1) g_game.playerSpeed = mc.params[0].asInt(4); else if (ev) ev->move_speed = mc.params[0].asInt(3); break;
        case 37: *pthr = true; break;
        case 38: *pthr = false; break;
        case 41: // Change Graphic
            *pchar = mc.params[0].asStr();
            if (targetId == -1 && !pchar->empty()) g_game.playerTransparent = false;
            if (mc.params.a.size() >= 3 && mc.params[2].asInt(0) > 0) *pdir = mc.params[2].asInt(2);
            break;
        case 42: if (ev) ev->opacity = mc.params[0].asInt(255); break;
        default: break;
    }
    return true;
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
        const JVal& evs = meta.get("events");
        for (size_t i = 0; i < evs.a.size(); i++) {
            const JVal& je = evs[i];
            MapEvent ev;
            ev.id = je.get("id").asInt(0);
            ev.name = je.get("name").asStr();
            ev.x = je.get("x").asInt(0);
            ev.y = je.get("y").asInt(0);
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
                ev.pages.push_back(pg);
            }
            g_game.events.push_back(ev);
        }
    }
    refreshEvents();
    printf("[AoOni] Mapa %03d cargado (%dx%d, eventos=%zu)\n", mapId, g_game.mapW, g_game.mapH, g_game.events.size());
    return true;
}

// Formatea un texto de RPG Maker XP extrayendo \f[rostro], \name[nombre], \N[id], \V[id]
static void formatDialogText(const std::string& raw, std::string& faceOut, std::string& speakerOut, std::string& cleanOut) {
    std::string s = raw;
    // Reemplazar \N[x] y \V[x]
    for (int pass = 0; pass < 2; pass++) {
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
            int id = atoi(s.substr(pos + 3, end - (pos + 3)).c_str());
            int val = (id >= 0 && id < 512) ? g_game.variables[id] : 0;
            s.replace(pos, end - pos + 1, std::to_string(val));
        }
    }

    size_t fpos = s.find("\\f[");
    if (fpos != std::string::npos) {
        size_t fend = s.find(']', fpos + 3);
        if (fend != std::string::npos) {
            faceOut = s.substr(fpos + 3, fend - (fpos + 3));
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
    // Limpiar codigos de pausa \! \. \> \< \^
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char nc = s[i + 1];
            if (nc == '!' || nc == '.' || nc == '>' || nc == '<' || nc == '^') { i++; continue; }
            if ((nc == 'C' || nc == 'c') && i + 2 < s.size() && s[i + 2] == '[') {
                size_t ce = s.find(']', i + 3);
                if (ce != std::string::npos) { i = ce; continue; }
            }
        }
        out.push_back(s[i]);
    }
    cleanOut = out;
}

// Evaluador de las expresiones Ruby usadas en Ao Oni (cmd 111 tipo 12)
static bool evalSingleTerm(const std::string& expr) {
    int evId = 0, val = 0, varId = 0;
    char prop[16] = {0}, op[8] = {0};
    if (sscanf(expr.c_str(), " $game_map.events[%d].%15[xy] %7[<>=!] %d", &evId, prop, op, &val) == 4) {
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
    if (sscanf(expr.c_str(), " $game_player.%15[xy] %7[<>=!] $game_map.events[%d].%15[xy]", prop, op, &evId2, prop2) == 4) {
        MapEvent* ev = findEvent(evId2);
        int lhs = (prop[0] == 'x') ? g_game.playerX : g_game.playerY;
        int rhs = ev ? (prop2[0] == 'x' ? ev->x : ev->y) : -999;
        if (strcmp(op, "==") == 0) return lhs == rhs;
        if (strcmp(op, "!=") == 0) return lhs != rhs;
        if (strcmp(op, ">=") == 0) return lhs >= rhs;
        if (strcmp(op, "<=") == 0) return lhs <= rhs;
    }
    if (sscanf(expr.c_str(), " $game_player.%15[xy] %7[<>=!] %d", prop, op, &val) == 3) {
        int lhs = (prop[0] == 'x') ? g_game.playerX : g_game.playerY;
        if (strcmp(op, "==") == 0) return lhs == val;
        if (strcmp(op, "!=") == 0) return lhs != val;
        if (strcmp(op, ">=") == 0) return lhs >= val;
        if (strcmp(op, "<=") == 0) return lhs <= val;
        if (strcmp(op, ">") == 0)  return lhs > val;
        if (strcmp(op, "<") == 0)  return lhs < val;
    }
    if (sscanf(expr.c_str(), " $game_variables[%d] %7[<>=!] %d", &varId, op, &val) == 3) {
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

static bool evalRubyCond(const std::string& expr) {
    // Soportar 'or' y 'and'
    size_t orPos = expr.find(" or ");
    if (orPos != std::string::npos)
        return evalRubyCond(expr.substr(0, orPos)) || evalRubyCond(expr.substr(orPos + 4));
    size_t andPos = expr.find(" and ");
    if (andPos != std::string::npos)
        return evalRubyCond(expr.substr(0, andPos)) && evalRubyCond(expr.substr(andPos + 5));
    return evalSingleTerm(expr);
}

static bool evalConditionCmd(int evId, const JVal& p) {
    int ctype = p[0].asInt(0);
    switch (ctype) {
        case 0: { // Switch
            int swId = p[1].asInt(1);
            bool want = (p[2].asInt(0) == 0);
            bool cur = (swId >= 0 && swId < 512) ? g_game.switches[swId] : false;
            return cur == want;
        }
        case 1: { // Variable
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
        case 2: { // Self Switch
            std::string ch = p[1].asStr();
            bool want = (p[2].asInt(0) == 0);
            char key[64];
            snprintf(key, sizeof(key), "%d_%d_%s", g_game.mapId, evId, ch.c_str());
            return g_game.selfSwitches[key] == want;
        }
        case 3: { // Timer
            int sec = p[1].asInt(0);
            int cmp = p[2].asInt(0);
            int curSec = g_game.timerFrames / 60;
            return (cmp == 0) ? (curSec >= sec) : (curSec <= sec);
        }
        case 4: { // Actor
            int aId = p[1].asInt(1);
            int sub = p[2].asInt(0);
            if (sub == 1) return g_game.actorNames[aId] == p[3].asStr();
            return false;
        }
        case 6: { // Character facing
            int charId = p[1].asInt(-1);
            int wantDir = p[2].asInt(2);
            if (charId == -1) return g_game.playerDir == wantDir;
            MapEvent* ev = findEvent(charId);
            return ev ? (ev->dir == wantDir) : false;
        }
        case 8: { // Item in inventory
            int itemId = p[1].asInt(1);
            return g_game.inventory[itemId] > 0;
        }
        case 11: // Key button pressed
            return false;
        case 12: // Script expression
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
        else if (other == 4) val = (int)g_game.frameCount;
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

// Ejecuta una lista de comandos (tanto en foreground como en eventos paralelos)
static void runInterpreterSlice(const JVal& list, size_t& ip, int& waitFrames, int evId, bool isForeground) {
    int steps = 0;
    while (ip < list.a.size() && steps < 120) {
        steps++;
        if (waitFrames > 0) { waitFrames--; return; }
        if (isForeground && (g_game.msgActive || g_game.choiceActive || g_game.pendingTransfer)) return;
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
                g_game.msgActive = true;
                // Si justo despues hay un Show Choices (102), activarlo junto al texto
                if (ip < list.a.size() && list[ip].get("c").asInt(0) == 102) {
                    const JVal& cp = list[ip].get("p");
                    g_game.choices.clear();
                    for (size_t k = 0; k < cp[0].a.size(); k++)
                        g_game.choices.push_back(cp[0][k].asStr());
                    g_game.choiceIndex = 0;
                    g_game.choiceIndent = list[ip].get("i").asInt(0);
                    g_game.choiceActive = true;
                    ip++;
                }
                return;
            }
            case 102: { // Show Choices suelto
                if (!isForeground) { ip++; break; }
                g_game.choices.clear();
                for (size_t k = 0; k < p[0].a.size(); k++)
                    g_game.choices.push_back(p[0][k].asStr());
                g_game.choiceIndex = 0;
                g_game.choiceIndent = indent;
                g_game.choiceActive = true;
                ip++;
                return;
            }
            case 402: { // When [choice] - si llegamos secuencialmente tras ejecutar una rama, saltar hasta 404
                while (ip < list.a.size()) {
                    if (list[ip].get("i").asInt(0) == indent && list[ip].get("c").asInt(0) == 404) {
                        ip++; break;
                    }
                    ip++;
                }
                break;
            }
            case 104: // Change Text Options
                g_game.msgPos = p[0].asInt(2);
                ip++;
                break;
            case 106: // Wait
                waitFrames = p[0].asInt(1);
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
            case 411: { // Else - saltar hasta 412 del mismo indent
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
            case 413: { // Repeat Above (volver al 112 del mismo indent)
                while (ip > 0) {
                    ip--;
                    if (list[ip].get("i").asInt(0) == indent && list[ip].get("c").asInt(0) == 112)
                        break;
                }
                ip++;
                break;
            }
            case 113: { // Break Loop (saltar tras el 413 del mismo indent)
                while (ip < list.a.size()) {
                    if (list[ip].get("c").asInt(0) == 413) { ip++; break; }
                    ip++;
                }
                break;
            }
            case 115: // Exit Event Processing
                ip = list.a.size();
                return;
            case 116: { // Erase Event
                MapEvent* ev = findEvent(evId);
                if (ev) { ev->erased = true; ev->activePage = -1; }
                ip = list.a.size();
                return;
            }
            case 117: { // Call Common Event
                int ceId = p[0].asInt(0);
                for (auto& ce : g_game.commonEvents) {
                    if (ce.id == ceId) {
                        size_t subIp = 0; int subWait = 0;
                        runInterpreterSlice(ce.list, subIp, subWait, evId, false);
                        break;
                    }
                }
                ip++;
                break;
            }
            case 121: { // Control Switches
                int sId = p[0].asInt(1), eId = p[1].asInt(1);
                bool val = (p[2].asInt(0) == 0);
                for (int id = sId; id <= eId; id++)
                    if (id >= 0 && id < 512) g_game.switches[id] = val;
                refreshEvents();
                ip++;
                break;
            }
            case 122: // Control Variables
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
                ip++;
                break;
            }
            case 129: { // Change Party Member
                int actorId = p[0].asInt(1);
                int add = p[1].asInt(0);
                if (add == 0 && g_game.actorChars.count(actorId) && !g_game.actorChars[actorId].empty()) {
                    g_game.playerChar = g_game.actorChars[actorId];
                    g_game.playerTransparent = false;
                }
                ip++;
                break;
            }
            case 135: // Change Menu Access
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
                g_game.pendingTransfer = true;
                ip++;
                return;
            }
            case 202: { // Set Event Location
                int tId = p[0].asInt(0);
                MapEvent* tev = findEvent(tId);
                if (tev) {
                    if (p[1].asInt(0) == 0) {
                        tev->x = p[2].asInt(0);
                        tev->y = p[3].asInt(0);
                    } else if (p[1].asInt(0) == 1) {
                        tev->x = g_game.variables[p[2].asInt(0)];
                        tev->y = g_game.variables[p[3].asInt(0)];
                    }
                    if (p[4].asInt(0) > 0) tev->dir = p[4].asInt(2);
                }
                ip++;
                break;
            }
            case 208: // Change Transparent Flag
                g_game.playerTransparent = (p[0].asInt(0) == 0);
                ip++;
                break;
            case 209: { // Set Move Route
                int tId = p[0].asInt(-1);
                MoveRoute mr = parseMoveRoute(p[1]);
                if (tId == -1) {
                    g_game.playerForcedRoute = mr;
                    g_game.playerRouteIdx = 0;
                    g_game.playerRouteActive = !mr.list.empty();
                } else {
                    MapEvent* tev = findEvent(tId);
                    if (tev) {
                        tev->forcedRoute = mr;
                        tev->routeIdx = 0;
                        tev->routeActive = !mr.list.empty();
                    }
                }
                ip++;
                break;
            }
            case 210: // Wait for Move's Completion
                if (isForeground) g_game.fgWaitForMove = true;
                ip++;
                return;
            case 223: { // Change Screen Color Tone
                const JVal& t = p[0];
                g_game.toneR = (int)t.get("r").asNum(0);
                g_game.toneG = (int)t.get("g").asNum(0);
                g_game.toneB = (int)t.get("b").asNum(0);
                ip++;
                break;
            }
            case 231: { // Show Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 20) {
                    g_game.pictures[picId].active = true;
                    g_game.pictures[picId].name = p[1].asStr();
                    g_game.pictures[picId].x = p[4].asInt(0);
                    g_game.pictures[picId].y = p[5].asInt(0);
                    g_game.pictures[picId].opacity = p[8].asInt(255);
                }
                ip++;
                break;
            }
            case 235: { // Erase Picture
                int picId = p[0].asInt(1);
                if (picId >= 1 && picId <= 20) g_game.pictures[picId].active = false;
                ip++;
                break;
            }
            case 303: // Name Input Processing -> mantener "Hiroshi" por defecto
                g_game.actorNames[p[0].asInt(2)] = "Hiroshi";
                ip++;
                break;
            case 320: // Change Actor Name
                g_game.actorNames[p[0].asInt(2)] = p[1].asStr();
                ip++;
                break;
            case 353: // Game Over
                g_game.gameOverTriggered = true;
                ip = list.a.size();
                return;
            case 355: { // Script
                std::string s = p[0].asStr();
                if (s.find("Graphics.frame_count = 0") != std::string::npos)
                    g_game.frameCount = 0;
                ip++;
                break;
            }
            default:
                ip++;
                break;
        }
    }
}

static void updateMoveRoutes(uint32_t now) {
    if (g_game.playerRouteActive) {
        if (now - g_game.lastPlayerRouteTick >= 90) {
            g_game.lastPlayerRouteTick = now;
            if (g_game.playerRouteIdx < g_game.playerForcedRoute.list.size()) {
                applyMoveCmd(-1, g_game.playerForcedRoute.list[g_game.playerRouteIdx++]);
                g_game.playerPattern = (g_game.playerPattern + 1) % 4;
            }
            if (g_game.playerRouteIdx >= g_game.playerForcedRoute.list.size()) {
                if (g_game.playerForcedRoute.repeat) g_game.playerRouteIdx = 0;
                else g_game.playerRouteActive = false;
            }
        }
    }
    for (auto& ev : g_game.events) {
        if (ev.activePage < 0) continue;
        if (ev.routeActive) {
            if (now - ev.lastRouteStepTick >= 95) {
                ev.lastRouteStepTick = now;
                if (ev.routeIdx < ev.forcedRoute.list.size()) {
                    applyMoveCmd(ev.id, ev.forcedRoute.list[ev.routeIdx++]);
                    ev.pattern = (ev.pattern + 1) % 4;
                }
                if (ev.routeIdx >= ev.forcedRoute.list.size()) {
                    if (ev.forcedRoute.repeat) ev.routeIdx = 0;
                    else ev.routeActive = false;
                }
            }
        } else if (!g_game.fgRunning && ev.move_type > 0) {
            // Movimiento autonomo (Ao Oni usa move_type=2 Approach o move_type=3 Custom)
            uint32_t delay = (uint32_t)std::max(100, 420 - ev.move_speed * 55);
            if (now - ev.lastAutoMoveTick >= delay) {
                ev.lastAutoMoveTick = now;
                if (ev.move_type == 1) {
                    stepCharacterDir(ev.x, ev.y, ev.dir, (rand() % 4 + 1) * 2, ev.through, false, ev.id);
                    ev.pattern = (ev.pattern + 1) % 4;
                } else if (ev.move_type == 2) {
                    stepTowardPlayer(ev);
                    ev.pattern = (ev.pattern + 1) % 4;
                } else if (ev.move_type == 3) {
                    const auto& mr = ev.pages[ev.activePage].move_route;
                    if (!mr.list.empty()) {
                        applyMoveCmd(ev.id, mr.list[ev.routeIdx % mr.list.size()]);
                        ev.routeIdx++;
                        ev.pattern = (ev.pattern + 1) % 4;
                    }
                }
            }
        }
    }
}

static void startForegroundEvent(int evId, const JVal& list) {
    g_game.fgRunning = true;
    g_game.fgEventId = evId;
    g_game.fgList = list;
    g_game.fgIp = 0;
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
    const JVal& its = db.get("items");
    for (const auto& kv : its.o) {
        int id = atoi(kv.first.c_str());
        g_game.itemDefs[id] = { kv.second.get("name").asStr(), kv.second.get("icon").asStr(), kv.second.get("desc").asStr() };
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

static void resetNewGame() {
    memset(g_game.switches, 0, sizeof(g_game.switches));
    memset(g_game.variables, 0, sizeof(g_game.variables));
    g_game.selfSwitches.clear();
    g_game.inventory.clear();
    for (int i = 0; i < 21; i++) g_game.pictures[i].active = false;
    g_game.playerChar = "hirosi";
    g_game.playerTransparent = true;
    g_game.playerThrough = false;
    g_game.playerSpeed = 4;
    g_game.playerDir = 2;
    g_game.toneR = g_game.toneG = g_game.toneB = 0;
    g_game.fgRunning = false;
    g_game.msgActive = false;
    g_game.choiceActive = false;
    g_game.pendingTransfer = false;
    g_game.gameOverTriggered = false;
    g_game.timerWorking = false;
    g_game.playerX = 9;
    g_game.playerY = 8;
    loadMap(75);
}

enum AppState { STATE_TITLE = 0, STATE_MAP = 1, STATE_INVENTORY = 2, STATE_GAMEOVER = 3 };

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    srand(12345);
    printf("[AoOni] Iniciando interprete Ao Oni PS4...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) return -1;
    SDL_Window* window = SDL_CreateWindow("Ao Oni PS4", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    if (!window) return -1;
    SDL_Surface* winSurf = SDL_GetWindowSurface(window);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(winSurf);
    if (!renderer) return -1;
    if (SDL_NumJoysticks() >= 1) SDL_JoystickOpen(0);

    initGameDb();
    SDL_Surface* titleSurf = getSurf("/app0/assets/images/title_title_black.png");
    SDL_Surface* goSurf    = getSurf("/app0/assets/images/go_gameover_3.jpg");
    SDL_Surface* aooniSurf = getSurf("/app0/assets/images/char_ao.png");

    AppState state = STATE_TITLE;
    int menuIndex = 0;
    const int offX = (SCREEN_W - GAME_W) / 2;
    const int offY = (SCREEN_H - GAME_H) / 2;

    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;
    uint32_t lastStepTick = 0;

    for (uint32_t frame = 0;; frame++) {
        g_game.frameCount++;
        uint32_t now = SDL_GetTicks();

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_JOYBUTTONDOWN) {
                int b = ev.jbutton.button;
                if (state == STATE_TITLE) {
                    if (b == PAD_UP)   menuIndex = (menuIndex + 2) % 3;
                    if (b == PAD_DOWN) menuIndex = (menuIndex + 1) % 3;
                    if (b == PAD_CROSS || b == PAD_OPTIONS) {
                        resetNewGame();
                        state = STATE_MAP;
                    }
                } else if (state == STATE_GAMEOVER) {
                    if (b == PAD_CROSS || b == PAD_OPTIONS) state = STATE_TITLE;
                } else if (state == STATE_INVENTORY) {
                    if (b == PAD_TRIANGLE || b == PAD_CIRCLE) state = STATE_MAP;
                } else if (state == STATE_MAP) {
                    if (g_game.choiceActive) {
                        if (b == PAD_UP && !g_game.choices.empty())
                            g_game.choiceIndex = (g_game.choiceIndex + (int)g_game.choices.size() - 1) % (int)g_game.choices.size();
                        if (b == PAD_DOWN && !g_game.choices.empty())
                            g_game.choiceIndex = (g_game.choiceIndex + 1) % (int)g_game.choices.size();
                        if (b == PAD_CROSS) {
                            int wanted = g_game.choiceIndex;
                            g_game.choiceActive = false;
                            g_game.msgActive = false;
                            // Buscar el comando 402 (When) con ese indice y mismo indent
                            while (g_game.fgIp < g_game.fgList.a.size()) {
                                const JVal& c = g_game.fgList[g_game.fgIp];
                                if (c.get("i").asInt(0) == g_game.choiceIndent &&
                                    c.get("c").asInt(0) == 402 &&
                                    c.get("p")[0].asInt(-1) == wanted) {
                                    g_game.fgIp++;
                                    break;
                                }
                                if (c.get("i").asInt(0) == g_game.choiceIndent && c.get("c").asInt(0) == 404)
                                    break;
                                g_game.fgIp++;
                            }
                        }
                    } else if (g_game.msgActive) {
                        if (b == PAD_CROSS || b == PAD_CIRCLE) g_game.msgActive = false;
                    } else if (!g_game.fgRunning) {
                        if (b == PAD_CROSS) {
                            int fx = g_game.playerX + (g_game.playerDir == 4 ? -1 : (g_game.playerDir == 6 ? 1 : 0));
                            int fy = g_game.playerY + (g_game.playerDir == 8 ? -1 : (g_game.playerDir == 2 ? 1 : 0));
                            for (const auto& mev : g_game.events) {
                                if (mev.activePage < 0) continue;
                                const auto& pg = mev.pages[mev.activePage];
                                if (pg.trigger <= 2 && ((mev.x == fx && mev.y == fy) || (mev.x == g_game.playerX && mev.y == g_game.playerY))) {
                                    if (pg.list.a.size() > 1) {
                                        startForegroundEvent(mev.id, pg.list);
                                        break;
                                    }
                                }
                            }
                        } else if (b == PAD_TRIANGLE && !g_game.menuDisabled) {
                            state = STATE_INVENTORY;
                        } else if (b == PAD_OPTIONS) {
                            state = STATE_TITLE;
                        }
                    }
                    if (b == PAD_UP)    dpadUp = true;
                    if (b == PAD_DOWN)  dpadDown = true;
                    if (b == PAD_LEFT)  dpadLeft = true;
                    if (b == PAD_RIGHT) dpadRight = true;
                }
            } else if (ev.type == SDL_JOYBUTTONUP) {
                int b = ev.jbutton.button;
                if (b == PAD_UP)    dpadUp = false;
                if (b == PAD_DOWN)  dpadDown = false;
                if (b == PAD_LEFT)  dpadLeft = false;
                if (b == PAD_RIGHT) dpadRight = false;
            } else if (ev.type == SDL_JOYAXISMOTION) {
                if (ev.jaxis.axis == 0) {
                    dpadLeft  = (ev.jaxis.value < -12000);
                    dpadRight = (ev.jaxis.value >  12000);
                } else if (ev.jaxis.axis == 1) {
                    dpadUp   = (ev.jaxis.value < -12000);
                    dpadDown = (ev.jaxis.value >  12000);
                }
            }
        }

        if (state == STATE_MAP) {
            if (g_game.timerWorking && g_game.timerFrames > 0) g_game.timerFrames--;

            updateMoveRoutes(now);

            // Movimiento manual del jugador cuando no hay evento foreground ni ruta forzada
            if (!g_game.fgRunning && !g_game.msgActive && !g_game.playerRouteActive && (now - lastStepTick) >= 120) {
                int d = 0;
                if (dpadDown) d = 2;
                else if (dpadLeft) d = 4;
                else if (dpadRight) d = 6;
                else if (dpadUp) d = 8;

                if (d != 0) {
                    lastStepTick = now;
                    g_game.playerDir = d;
                    g_game.playerPattern = (g_game.playerPattern + 1) % 4;
                    int nx = g_game.playerX + (d == 4 ? -1 : (d == 6 ? 1 : 0));
                    int ny = g_game.playerY + (d == 8 ? -1 : (d == 2 ? 1 : 0));

                    // Comprobar si en (nx, ny) hay un evento con trigger 1 o 2 (Player/Event Touch)
                    bool triggeredTouch = false;
                    for (const auto& mev : g_game.events) {
                        if (mev.activePage < 0) continue;
                        const auto& pg = mev.pages[mev.activePage];
                        if ((pg.trigger == 1 || pg.trigger == 2) && mev.x == nx && mev.y == ny && pg.list.a.size() > 1) {
                            if (canPassTile(nx, ny, true)) { g_game.playerX = nx; g_game.playerY = ny; }
                            startForegroundEvent(mev.id, pg.list);
                            triggeredTouch = true;
                            break;
                        }
                    }
                    if (!triggeredTouch && (g_game.playerThrough || canPassTile(nx, ny, true))) {
                        g_game.playerX = nx;
                        g_game.playerY = ny;
                        for (const auto& mev : g_game.events) {
                            if (mev.activePage < 0) continue;
                            const auto& pg = mev.pages[mev.activePage];
                            if ((pg.trigger == 1 || pg.trigger == 2) && mev.x == g_game.playerX && mev.y == g_game.playerY && pg.list.a.size() > 1) {
                                startForegroundEvent(mev.id, pg.list);
                                break;
                            }
                        }
                    }
                }
            }

            // Contacto de eventos (Ao Oni persiguiendo al jugador -> Event Touch trigger=2 o contacto con char="ao*")
            if (!g_game.fgRunning) {
                for (const auto& mev : g_game.events) {
                    if (mev.activePage < 0) continue;
                    const auto& pg = mev.pages[mev.activePage];
                    if (mev.x == g_game.playerX && mev.y == g_game.playerY) {
                        if (pg.trigger == 2 && pg.list.a.size() > 1) {
                            startForegroundEvent(mev.id, pg.list);
                            break;
                        } else if (mev.char_name.rfind("ao", 0) == 0) {
                            g_game.switches[50] = true; // Switch 50 = Gameover en Ao Oni
                            refreshEvents();
                        }
                    }
                }
            }

            // Buscar eventos Autorun (trigger == 3)
            if (!g_game.fgRunning) {
                for (const auto& mev : g_game.events) {
                    if (mev.activePage < 0) continue;
                    const auto& pg = mev.pages[mev.activePage];
                    if (pg.trigger == 3 && pg.list.a.size() > 1) {
                        startForegroundEvent(mev.id, pg.list);
                        break;
                    }
                }
            }
            if (!g_game.fgRunning) {
                for (const auto& ce : g_game.commonEvents) {
                    if (ce.trigger == 1 && g_game.switches[ce.switch_id]) {
                        startForegroundEvent(0, ce.list);
                        break;
                    }
                }
            }

            // Ejecutar intérprete foreground
            if (g_game.fgRunning) {
                runInterpreterSlice(g_game.fgList, g_game.fgIp, g_game.fgWaitFrames, g_game.fgEventId, true);
                if (g_game.fgIp >= g_game.fgList.a.size() && !g_game.msgActive && !g_game.choiceActive && !g_game.fgWaitForMove)
                    g_game.fgRunning = false;
            }

            // Ejecutar eventos paralelos del mapa (trigger == 4) y CommonEvents paralelos (trigger == 2)
            for (size_t i = 0; i < g_game.events.size(); i++) {
                auto& mev = g_game.events[i];
                if (mev.activePage < 0) continue;
                const auto& pg = mev.pages[mev.activePage];
                if (pg.trigger == 4 && pg.list.a.size() > 1) {
                    runInterpreterSlice(pg.list, mev.parIp, mev.parWait, mev.id, false);
                    if (mev.parIp >= pg.list.a.size()) mev.parIp = 0;
                }
            }
            for (auto& ce : g_game.commonEvents) {
                if (ce.trigger == 2 && g_game.switches[ce.switch_id]) {
                    runInterpreterSlice(ce.list, ce.parIp, ce.parWait, 0, false);
                    if (ce.parIp >= ce.list.a.size()) ce.parIp = 0;
                }
            }

            // Procesar cambio de mapa pendiente
            if (g_game.pendingTransfer) {
                g_game.pendingTransfer = false;
                int tm = g_game.nextMapId, tx = g_game.nextX, ty = g_game.nextY, td = g_game.nextDir;
                if (loadMap(tm)) {
                    g_game.playerX = tx;
                    g_game.playerY = ty;
                    if (td > 0) g_game.playerDir = td;
                }
            }
            if (g_game.gameOverTriggered) {
                state = STATE_GAMEOVER;
            }
        }

        // ==================== RENDERIZADO ====================
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (state == STATE_TITLE) {
            if (titleSurf) {
                SDL_Rect dst = { offX, offY, GAME_W, GAME_H };
                blitSurf(titleSurf, NULL, winSurf, &dst);
            }
            drawText(renderer, offX + 140, offY + 90, "AO ONI - PS4 PORT", 3, 220, 220, 255);
            SDL_Rect box = { offX + 200, offY + 220, 240, 140 };
            SDL_SetRenderDrawColor(renderer, 20, 30, 80, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
            SDL_RenderDrawRect(renderer, &box);

            const char* opts[3] = { "NUEVA PARTIDA", "CONTINUAR", "SALIR" };
            for (int i = 0; i < 3; i++) {
                if (i == menuIndex) {
                    SDL_Rect sel = { box.x + 10, box.y + 14 + i * 38, box.w - 20, 30 };
                    SDL_SetRenderDrawColor(renderer, 60, 90, 160, 255);
                    SDL_RenderFillRect(renderer, &sel);
                }
                drawText(renderer, box.x + 22, box.y + 20 + i * 38, opts[i], 2, 255, 255, 255);
            }
            if (aooniSurf) {
                int cw = aooniSurf->w / 4, ch = aooniSurf->h / 4;
                SDL_Rect src = { (int)((frame / 15) % 4) * cw, 0, cw, ch };
                SDL_Rect dst = { offX + 60, offY + 200, cw * 2, ch * 2 };
                blitSurf(aooniSurf, &src, winSurf, &dst);
            }
            drawText(renderer, offX + 110, offY + 420, "Pulsa X para iniciar la historia", 2, 180, 180, 200);
        } else if (state == STATE_GAMEOVER) {
            if (goSurf) {
                SDL_Rect dst = { offX, offY, GAME_W, GAME_H };
                blitSurf(goSurf, NULL, winSurf, &dst);
            } else {
                drawText(renderer, offX + 200, offY + 210, "GAME OVER", 4, 220, 40, 40);
            }
            drawText(renderer, offX + 140, offY + 420, "Pulsa X para volver al titulo", 2, 220, 220, 220);
        } else if (state == STATE_INVENTORY) {
            SDL_Rect box = { offX + 40, offY + 40, GAME_W - 80, GAME_H - 80 };
            SDL_SetRenderDrawColor(renderer, 18, 26, 70, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
            SDL_RenderDrawRect(renderer, &box);
            drawText(renderer, box.x + 24, box.y + 20, "INVENTARIO DE OBJETOS (Circulo/Triangulo: cerrar)", 2, 255, 255, 180);
            int iy = box.y + 64;
            bool any = false;
            for (const auto& kv : g_game.inventory) {
                if (kv.second <= 0) continue;
                any = true;
                std::string iname = g_game.itemDefs.count(kv.first) ? g_game.itemDefs[kv.first].name : ("Item #" + std::to_string(kv.first));
                drawText(renderer, box.x + 32, iy, "- " + iname + " x" + std::to_string(kv.second), 2, 255, 255, 255);
                iy += 28;
            }
            if (!any) drawText(renderer, box.x + 32, iy, "(No tienes objetos todavia)", 2, 180, 180, 180);
        } else if (state == STATE_MAP) {
            int camX = g_game.playerX * TILE_SZ - GAME_W / 2 + TILE_SZ / 2;
            int camY = g_game.playerY * TILE_SZ - GAME_H / 2 + TILE_SZ / 2;
            if (g_game.mapW * TILE_SZ > GAME_W)
                camX = std::max(0, std::min(camX, g_game.mapW * TILE_SZ - GAME_W));
            else camX = (g_game.mapW * TILE_SZ - GAME_W) / 2;
            if (g_game.mapH * TILE_SZ > GAME_H)
                camY = std::max(0, std::min(camY, g_game.mapH * TILE_SZ - GAME_H));
            else camY = (g_game.mapH * TILE_SZ - GAME_H) / 2;

            SDL_Surface* tsSurf = g_game.tilesetFile[0] ? getSurf(std::string("/app0/assets/images/ts_") + g_game.tilesetFile + ".png") : nullptr;
            SDL_Surface* atSurfs[7] = {nullptr};
            for (int i = 0; i < 7; i++)
                if (g_game.autotileFiles[i][0])
                    atSurfs[i] = getSurf(std::string("/app0/assets/images/at_") + g_game.autotileFiles[i] + ".png");

            // 1) Dibujar las 3 capas del mapa
            for (int z = 0; z < 3; z++) {
                for (int ty = 0; ty < g_game.mapH; ty++) {
                    for (int tx = 0; tx < g_game.mapW; tx++) {
                        int16_t tid = g_game.layers[tx + ty * g_game.mapW + z * g_game.mapW * g_game.mapH];
                        int sx = offX + (tx * TILE_SZ - camX);
                        int sy = offY + (ty * TILE_SZ - camY);
                        if (sx + TILE_SZ <= offX || sx >= offX + GAME_W || sy + TILE_SZ <= offY || sy >= offY + GAME_H)
                            continue;
                        SDL_Rect dst = { sx, sy, TILE_SZ, TILE_SZ };
                        if (tid >= 384 && tsSurf) {
                            int tIndex = tid - 384;
                            SDL_Rect src = { (tIndex % 8) * TILE_SZ, (tIndex / 8) * TILE_SZ, TILE_SZ, TILE_SZ };
                            blitSurf(tsSurf, &src, winSurf, &dst);
                        } else if (tid >= 48 && tid < 384) {
                            int atIdx = (tid / 48) - 1;
                            SDL_Surface* atSurf = (atIdx >= 0 && atIdx < 7) ? atSurfs[atIdx] : nullptr;
                            if (atSurf) {
                                int ax = (atSurf->w >= 96 && atSurf->h >= 128) ? 32 : 0;
                                int ay = (atSurf->w >= 96 && atSurf->h >= 128) ? 64 : 0;
                                SDL_Rect src = { ax, ay, TILE_SZ, TILE_SZ };
                                blitSurf(atSurf, &src, winSurf, &dst);
                            }
                        }
                    }
                }
            }

            // 2) Ordenar por coordenada Y y dibujar todos los eventos + jugador
            struct SpriteDraw { int y, order; bool isPlayer; const MapEvent* ev; };
            std::vector<SpriteDraw> drawList;
            for (const auto& mev : g_game.events) {
                if (mev.activePage < 0 || mev.char_name.empty()) continue;
                drawList.push_back({ mev.y, mev.always_top ? 2 : 0, false, &mev });
            }
            if (!g_game.playerTransparent && !g_game.playerChar.empty()) {
                drawList.push_back({ g_game.playerY, 1, true, nullptr });
            }
            std::sort(drawList.begin(), drawList.end(), [](const SpriteDraw& a, const SpriteDraw& b) {
                if (a.order != b.order) return a.order < b.order;
                return a.y < b.y;
            });

            for (const auto& sd : drawList) {
                if (sd.isPlayer) {
                    SDL_Surface* ps = getSurf(std::string("/app0/assets/images/char_") + g_game.playerChar + ".png");
                    if (!ps) continue;
                    int cw = ps->w / 4, ch = ps->h / 4;
                    SDL_Rect src = { g_game.playerPattern * cw, rmDirToRow(g_game.playerDir) * ch, cw, ch };
                    int px = offX + (g_game.playerX * TILE_SZ - camX) - (cw - TILE_SZ) / 2;
                    int py = offY + (g_game.playerY * TILE_SZ - camY) - (ch - TILE_SZ);
                    SDL_Rect dst = { px, py, cw, ch };
                    blitSurf(ps, &src, winSurf, &dst);
                } else if (sd.ev) {
                    SDL_Surface* es = getSurf(std::string("/app0/assets/images/char_") + sd.ev->char_name + ".png");
                    if (!es) continue;
                    int cw = es->w / 4, ch = es->h / 4;
                    SDL_Rect src = { sd.ev->pattern * cw, rmDirToRow(sd.ev->dir) * ch, cw, ch };
                    int ex = offX + (sd.ev->x * TILE_SZ - camX) - (cw - TILE_SZ) / 2;
                    int ey = offY + (sd.ev->y * TILE_SZ - camY) - (ch - TILE_SZ);
                    SDL_Rect dst = { ex, ey, cw, ch };
                    blitSurf(es, &src, winSurf, &dst, (uint8_t)sd.ev->opacity);
                }
            }

            // 3) Dibujar Pictures activas (cmd 231: linterna 'light', puzzles, sustos)
            for (int i = 1; i <= 20; i++) {
                if (!g_game.pictures[i].active || g_game.pictures[i].name.empty()) continue;
                SDL_Surface* pic = getSurf(std::string("/app0/assets/images/pic_") + g_game.pictures[i].name + ".png");
                if (!pic) continue;
                SDL_Rect dst = { offX + g_game.pictures[i].x, offY + g_game.pictures[i].y, pic->w, pic->h };
                blitSurf(pic, NULL, winSurf, &dst, (uint8_t)g_game.pictures[i].opacity);
            }

            // 4) Dibujar cuadro de diálogo (con rostro \f[...], nombre y elecciones)
            if (g_game.msgActive) {
                int boxY = (g_game.msgPos == 0) ? (offY + 12) : ((g_game.msgPos == 1) ? (offY + 170) : (offY + GAME_H - 132));
                SDL_Rect mbox = { offX + 16, boxY, GAME_W - 32, 120 };
                SDL_SetRenderDrawColor(renderer, 15, 22, 68, 255);
                SDL_RenderFillRect(renderer, &mbox);
                SDL_SetRenderDrawColor(renderer, 230, 230, 255, 255);
                SDL_RenderDrawRect(renderer, &mbox);

                int textX = mbox.x + 16;
                if (!g_game.msgFace.empty()) {
                    SDL_Surface* faceSurf = getSurf(std::string("/app0/assets/images/pic_") + g_game.msgFace + ".png");
                    if (faceSurf) {
                        SDL_Rect fdst = { mbox.x + 10, mbox.y + 12, faceSurf->w, faceSurf->h };
                        blitSurf(faceSurf, NULL, winSurf, &fdst);
                        textX = mbox.x + faceSurf->w + 22;
                    }
                }
                int textY = mbox.y + 14;
                if (!g_game.msgSpeaker.empty()) {
                    drawText(renderer, textX, textY, "[" + g_game.msgSpeaker + "]", 2, 255, 230, 120);
                    textY += 24;
                }
                drawText(renderer, textX, textY, g_game.msgText, 2, 255, 255, 255);
            }

            if (g_game.choiceActive && !g_game.choices.empty()) {
                int chH = (int)g_game.choices.size() * 28 + 20;
                SDL_Rect cbox = { offX + GAME_W - 220, offY + GAME_H - 132 - chH - 8, 204, chH };
                SDL_SetRenderDrawColor(renderer, 18, 28, 78, 255);
                SDL_RenderFillRect(renderer, &cbox);
                SDL_SetRenderDrawColor(renderer, 230, 230, 255, 255);
                SDL_RenderDrawRect(renderer, &cbox);
                for (size_t i = 0; i < g_game.choices.size(); i++) {
                    if ((int)i == g_game.choiceIndex) {
                        SDL_Rect sel = { cbox.x + 6, cbox.y + 8 + (int)i * 28, cbox.w - 12, 24 };
                        SDL_SetRenderDrawColor(renderer, 65, 95, 175, 255);
                        SDL_RenderFillRect(renderer, &sel);
                    }
                    drawText(renderer, cbox.x + 14, cbox.y + 12 + (int)i * 28, g_game.choices[i], 2, 255, 255, 255);
                }
            }
        }

        if (winSurf && winSurf->pixels) {
            uint32_t* px = (uint32_t*)winSurf->pixels;
            int totalPx = (winSurf->pitch / 4) * winSurf->h;
            for (int i = 0; i < totalPx; i++)
                px[i] |= 0xFF000000u;
        }
        SDL_UpdateWindowSurface(window);
    }

    SDL_Quit();
    return 0;
}
