// Ao Oni PS4 Port (OpenOrbis + SDL2)
// Lee los datos exportados de RPG Maker XP (/app0/assets/data/*.bin) y los graficos (/app0/assets/Graphics/...)
// Sin depender de libSceFreeTypeOl para funcionar tanto en PS4 real (GoldHEN 11.00) como en shadPS4.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <orbis/libkernel.h>

#define SCREEN_W 1920
#define SCREEN_H 1080
#define GAME_W   640
#define GAME_H   480
#define TILE_SZ  32

// Botones de mando PS4 en OpenOrbis SDL2
#define PAD_CROSS    0
#define PAD_CIRCLE   1
#define PAD_SQUARE   2
#define PAD_TRIANGLE 3
#define PAD_OPTIONS  9
#define PAD_UP       11
#define PAD_DOWN     12
#define PAD_LEFT     13
#define PAD_RIGHT    14

// Fuente bitmap 8x8 ASCII embebida (caracteres 32..127) para dibujar texto sin FreeType
static const uint8_t FONT8X8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // '#'
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // '$'
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // '%'
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // '&'
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '\''
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // '('
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ','
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // '.'
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // '/'
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // '0'
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // '1'
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // '2'
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // '3'
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // '4'
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // '5'
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // '6'
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // '7'
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // '8'
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // '9'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // ':'
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ';'
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // '<'
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // '='
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // '>'
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // '?'
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // '@'
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // 'A'
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // 'B'
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // 'C'
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // 'D'
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // 'E'
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // 'F'
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // 'G'
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // 'H'
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'I'
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // 'J'
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // 'K'
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // 'L'
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // 'M'
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // 'N'
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // 'O'
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // 'P'
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // 'Q'
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // 'R'
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // 'S'
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'T'
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // 'U'
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'V'
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 'W'
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // 'X'
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // 'Y'
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // 'Z'
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // '['
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // '\\'
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ']'
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // 'a'
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // 'b'
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // 'c'
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, // 'd'
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, // 'e'
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, // 'f'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // 'g'
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // 'h'
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // 'i'
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // 'j'
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // 'k'
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // 'l'
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // 'm'
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // 'n'
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // 'o'
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // 'p'
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // 'q'
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // 'r'
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // 's'
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // 't'
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // 'u'
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // 'v'
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // 'w'
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // 'x'
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // 'y'
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // 'z'
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // '}'
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // '~'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

static void drawChar(SDL_Renderer* r, int x, int y, char c, int scale, uint8_t cr, uint8_t cg, uint8_t cb)
{
    int idx = (unsigned char)c;
    if (idx < 32 || idx > 127) idx = '?';
    const uint8_t* glyph = FONT8X8[idx - 32];
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
        {
            if (bits & (1 << col))
            {
                SDL_Rect px = { x + col * scale, y + row * scale, scale, scale };
                SDL_RenderFillRect(r, &px);
            }
        }
    }
}

static void drawText(SDL_Renderer* r, int x, int y, const char* s, int scale, uint8_t cr, uint8_t cg, uint8_t cb)
{
    int cx = x;
    for (const char* p = s; *p; ++p)
    {
        if (*p == '\n')
        {
            y += 10 * scale;
            cx = x;
            continue;
        }
        drawChar(r, cx + scale, y + scale, *p, scale, 0, 0, 0);
        drawChar(r, cx, y, *p, scale, cr, cg, cb);
        cx += 8 * scale;
    }
}

struct WarpEvent {
    int x, y;
    int targetMapId;
    int targetX, targetY;
    int dir;
    char charName[64];
    int charDir;
};

struct MapData {
    int id = 0;
    int width = 20;
    int height = 15;
    int tilesetId = 1;
    char tilesetFile[64] = {0};
    std::vector<int16_t> layers; // width * height * 3
    std::vector<uint8_t> passable; // tile passage flags
    std::vector<WarpEvent> events;
    SDL_Texture* tilesetTex = nullptr;
};

enum GameState {
    STATE_TITLE = 0,
    STATE_MAP   = 1
};

static SDL_Texture* loadTex(SDL_Renderer* r, const std::string& path)
{
    SDL_Surface* s = IMG_Load(path.c_str());
    if (!s)
    {
        printf("[AoOni] No se pudo cargar imagen: %s (%s)\n", path.c_str(), SDL_GetError());
        return nullptr;
    }
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    return t;
}

static bool loadMapBin(SDL_Renderer* r, int mapId, MapData& out)
{
    char path[256];
    snprintf(path, sizeof(path), "/app0/assets/misc/map_%03d.bin", mapId);
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        printf("[AoOni] No se encontro %s\n", path);
        return false;
    }
    uint32_t magic = 0, w = 0, h = 0, tsId = 0, passCount = 0, evCount = 0;
    fread(&magic, 4, 1, f);
    if (magic != 0x4D504F41) // 'AOPM'
    {
        fclose(f);
        return false;
    }
    fread(&w, 4, 1, f);
    fread(&h, 4, 1, f);
    fread(&tsId, 4, 1, f);
    fread(out.tilesetFile, 1, 64, f);
    out.id = mapId;
    out.width = (int)w;
    out.height = (int)h;
    out.tilesetId = (int)tsId;

    out.layers.resize(w * h * 3);
    fread(out.layers.data(), sizeof(int16_t), w * h * 3, f);

    fread(&passCount, 4, 1, f);
    out.passable.resize(passCount);
    if (passCount > 0)
        fread(out.passable.data(), 1, passCount, f);

    fread(&evCount, 4, 1, f);
    out.events.resize(evCount);
    for (uint32_t i = 0; i < evCount; i++)
    {
        int32_t vals[6];
        fread(vals, 4, 6, f);
        out.events[i].x = vals[0];
        out.events[i].y = vals[1];
        out.events[i].targetMapId = vals[2];
        out.events[i].targetX = vals[3];
        out.events[i].targetY = vals[4];
        out.events[i].dir = vals[5];
        fread(out.events[i].charName, 1, 64, f);
        int32_t cdir = 0;
        fread(&cdir, 4, 1, f);
        out.events[i].charDir = cdir;
    }
    fclose(f);

    if (out.tilesetTex)
    {
        SDL_DestroyTexture(out.tilesetTex);
        out.tilesetTex = nullptr;
    }
    if (out.tilesetFile[0])
    {
        std::string tpath = std::string("/app0/assets/images/ts_") + out.tilesetFile + ".png";
        out.tilesetTex = loadTex(r, tpath);
    }
    printf("[AoOni] Mapa %d cargado (%dx%d, tileset=%s, eventos=%zu)\n",
           mapId, out.width, out.height, out.tilesetFile, out.events.size());
    return true;
}

static bool canPass(const MapData& m, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= m.width || ty >= m.height)
        return false;
    for (int z = 2; z >= 0; z--)
    {
        int16_t tid = m.layers[tx + ty * m.width + z * m.width * m.height];
        if (tid >= 384 && (size_t)tid < m.passable.size())
        {
            uint8_t p = m.passable[tid];
            if (p & 0x0F) return false;
            return true;
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[AoOni] Iniciando Ao Oni PS4 Port...\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0)
    {
        printf("[AoOni] Error SDL_Init: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Window* window = SDL_CreateWindow("Ao Oni PS4", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H, 0);
    if (!window)
    {
        printf("[AoOni] Error SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Surface* winSurf = SDL_GetWindowSurface(window);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(winSurf);
    if (!renderer)
    {
        printf("[AoOni] Error SDL_CreateSoftwareRenderer: %s\n", SDL_GetError());
        return -1;
    }

    SDL_Joystick* pad = nullptr;
    if (SDL_NumJoysticks() >= 1)
        pad = SDL_JoystickOpen(0);

    // Cargar recursos principales de Ao Oni
    SDL_Texture* titleTex   = loadTex(renderer, "/app0/assets/images/title_title_black.png");
    SDL_Texture* hiroshiTex = loadTex(renderer, "/app0/assets/images/char_000.png");
    SDL_Texture* aooniTex   = loadTex(renderer, "/app0/assets/images/char_ao.png");

    GameState state = STATE_TITLE;
    int menuIndex = 0;

    // Estado del jugador (Hiroshi) en el mapa
    MapData curMap;
    int startMapId = 2; // Map002 = entrada de la mansion (Map075 es cutscene inicial)
    int playerX = 9, playerY = 12;
    int playerDir = 0; // 0=abajo, 1=izquierda, 2=derecha, 3=arriba
    int animFrame = 0;
    uint32_t lastStepTick = 0;

    // Escala de 640x480 a 1440x1080 centrada (4:3)
    const int scale = 2;
    const int viewW = GAME_W * scale;
    const int viewH = GAME_H * scale;
    const int offX  = (SCREEN_W - viewW) / 2;
    const int offY  = (SCREEN_H - viewH) / 2;

    bool dpadUp = false, dpadDown = false, dpadLeft = false, dpadRight = false;

    printf("[AoOni] Entrando al bucle principal...\n");
    for (uint32_t frame = 0;; frame++)
    {
        uint32_t now = SDL_GetTicks();
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_JOYBUTTONDOWN)
            {
                int b = ev.jbutton.button;
                if (state == STATE_TITLE)
                {
                    if (b == PAD_UP)   menuIndex = (menuIndex + 2) % 3;
                    if (b == PAD_DOWN) menuIndex = (menuIndex + 1) % 3;
                    if (b == PAD_CROSS || b == PAD_OPTIONS)
                    {
                        if (loadMapBin(renderer, startMapId, curMap) || loadMapBin(renderer, 1, curMap))
                        {
                            playerX = curMap.width / 2;
                            playerY = curMap.height / 2;
                        }
                        state = STATE_MAP;
                    }
                }
                else if (state == STATE_MAP)
                {
                    if (b == PAD_UP)    dpadUp = true;
                    if (b == PAD_DOWN)  dpadDown = true;
                    if (b == PAD_LEFT)  dpadLeft = true;
                    if (b == PAD_RIGHT) dpadRight = true;
                    if (b == PAD_TRIANGLE)
                    {
                        // Cambiar de mapa para explorar (L/R o Triangulo)
                        int nextId = (curMap.id % 77) + 1;
                        if (loadMapBin(renderer, nextId, curMap))
                        {
                            playerX = curMap.width / 2;
                            playerY = curMap.height / 2;
                        }
                    }
                    if (b == PAD_OPTIONS)
                        state = STATE_TITLE;
                }
            }
            else if (ev.type == SDL_JOYBUTTONUP)
            {
                int b = ev.jbutton.button;
                if (b == PAD_UP)    dpadUp = false;
                if (b == PAD_DOWN)  dpadDown = false;
                if (b == PAD_LEFT)  dpadLeft = false;
                if (b == PAD_RIGHT) dpadRight = false;
            }
            else if (ev.type == SDL_JOYAXISMOTION)
            {
                if (ev.jaxis.axis == 0)
                {
                    dpadLeft  = (ev.jaxis.value < -12000);
                    dpadRight = (ev.jaxis.value >  12000);
                }
                else if (ev.jaxis.axis == 1)
                {
                    dpadUp   = (ev.jaxis.value < -12000);
                    dpadDown = (ev.jaxis.value >  12000);
                }
            }
        }

        if (state == STATE_MAP && (now - lastStepTick) >= 140)
        {
            int nx = playerX, ny = playerY;
            bool moved = false;
            if (dpadDown)  { playerDir = 0; ny++; moved = true; }
            else if (dpadLeft)  { playerDir = 1; nx--; moved = true; }
            else if (dpadRight) { playerDir = 2; nx++; moved = true; }
            else if (dpadUp)    { playerDir = 3; ny--; moved = true; }

            if (moved)
            {
                lastStepTick = now;
                animFrame = (animFrame + 1) % 4;
                if (canPass(curMap, nx, ny))
                {
                    playerX = nx;
                    playerY = ny;
                    // Comprobar eventos de teletransporte (puertas / escaleras)
                    for (const auto& ev : curMap.events)
                    {
                        if (ev.x == playerX && ev.y == playerY && ev.targetMapId > 0)
                        {
                            int tm = ev.targetMapId, tx = ev.targetX, ty = ev.targetY;
                            if (loadMapBin(renderer, tm, curMap))
                            {
                                playerX = tx;
                                playerY = ty;
                            }
                            break;
                        }
                    }
                }
            }
        }

        // Renderizado
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        if (state == STATE_TITLE)
        {
            if (titleTex)
            {
                SDL_Rect dst = { offX, offY, viewW, viewH };
                SDL_RenderCopy(renderer, titleTex, NULL, &dst);
            }
            drawText(renderer, offX + 420, offY + 260, "AO ONI - PS4 PORT", 4, 220, 220, 255);

            // Ventana de menu estilo RPG Maker XP
            SDL_Rect box = { offX + 480, offY + 500, 320, 170 };
            SDL_SetRenderDrawColor(renderer, 20, 30, 80, 255);
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 220, 220, 255, 255);
            SDL_RenderDrawRect(renderer, &box);

            const char* opts[3] = { "NUEVA PARTIDA", "CONTINUAR", "SALIR" };
            for (int i = 0; i < 3; i++)
            {
                if (i == menuIndex)
                {
                    SDL_Rect sel = { box.x + 12, box.y + 18 + i * 46, box.w - 24, 36 };
                    SDL_SetRenderDrawColor(renderer, 60, 90, 160, 255);
                    SDL_RenderFillRect(renderer, &sel);
                }
                drawText(renderer, box.x + 28, box.y + 24 + i * 46, opts[i], 3, 255, 255, 255);
            }

            if (aooniTex)
            {
                int tw = 0, th = 0;
                SDL_QueryTexture(aooniTex, NULL, NULL, &tw, &th);
                int cw = tw / 4, ch = th / 4;
                SDL_Rect src = { (int)((frame / 15) % 4) * cw, 0, cw, ch };
                SDL_Rect dst = { offX + 180, offY + 460, cw * 3, ch * 3 };
                SDL_RenderCopy(renderer, aooniTex, &src, &dst);
            }
            drawText(renderer, offX + 320, offY + 880, "Pulsa X o OPTIONS para iniciar", 3, 180, 180, 200);
        }
        else if (state == STATE_MAP)
        {
            int camX = playerX * TILE_SZ - GAME_W / 2 + TILE_SZ / 2;
            int camY = playerY * TILE_SZ - GAME_H / 2 + TILE_SZ / 2;

            // Dibujar capas del mapa
            for (int z = 0; z < 3; z++)
            {
                for (int ty = 0; ty < curMap.height; ty++)
                {
                    for (int tx = 0; tx < curMap.width; tx++)
                    {
                        int16_t tid = curMap.layers[tx + ty * curMap.width + z * curMap.width * curMap.height];
                        int sx = offX + (tx * TILE_SZ - camX) * scale;
                        int sy = offY + (ty * TILE_SZ - camY) * scale;
                        if (sx + TILE_SZ * scale < offX || sx >= offX + viewW ||
                            sy + TILE_SZ * scale < offY || sy >= offY + viewH)
                            continue;

                        if (tid >= 384 && curMap.tilesetTex)
                        {
                            int tIndex = tid - 384;
                            SDL_Rect src = { (tIndex % 8) * TILE_SZ, (tIndex / 8) * TILE_SZ, TILE_SZ, TILE_SZ };
                            SDL_Rect dst = { sx, sy, TILE_SZ * scale, TILE_SZ * scale };
                            SDL_RenderCopy(renderer, curMap.tilesetTex, &src, &dst);
                        }
                        else if (tid >= 48 && tid < 384 && z == 0)
                        {
                            // Autotile suelo/pared (color base cuando no hay autotile expandido)
                            SDL_Rect dst = { sx, sy, TILE_SZ * scale, TILE_SZ * scale };
                            SDL_SetRenderDrawColor(renderer, 45, 40, 55, 255);
                            SDL_RenderFillRect(renderer, &dst);
                        }
                    }
                }
            }

            // Dibujar a Hiroshi (personaje 000.png, 4x4 frames)
            if (hiroshiTex)
            {
                int tw = 0, th = 0;
                SDL_QueryTexture(hiroshiTex, NULL, NULL, &tw, &th);
                int cw = tw / 4, ch = th / 4;
                SDL_Rect src = { animFrame * cw, playerDir * ch, cw, ch };
                int px = offX + (playerX * TILE_SZ - camX) * scale;
                int py = offY + (playerY * TILE_SZ - camY - (ch - TILE_SZ)) * scale;
                SDL_Rect dst = { px, py, cw * scale, ch * scale };
                SDL_RenderCopy(renderer, hiroshiTex, &src, &dst);
            }

            char info[128];
            snprintf(info, sizeof(info), "AO ONI PS4 | Mapa %03d (%s) Pos (%d,%d) | Triangulo: sig. mapa",
                     curMap.id, curMap.tilesetFile, playerX, playerY);
            drawText(renderer, 40, 30, info, 2, 255, 255, 255);
        }

        SDL_UpdateWindowSurface(window);
    }

    SDL_Quit();
    return 0;
}
