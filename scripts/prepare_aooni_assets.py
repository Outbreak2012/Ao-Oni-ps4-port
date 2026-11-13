#!/usr/bin/env python3
"""
Exporta el juego completo de Ao Oni (Data/*.rxdata, Graphics/*, Audio/*)
al formato nativo de PS4 (/app0/assets/misc/game_db.json, map_XXX.bin, map_XXX.json,
imagenes PNG/JPG y audio PCM 48000 Hz 16-bit estereo para sceAudioOut).
"""
import os
import sys
import glob
import json
import math
import base64
import random
import shutil
import struct
import wave
import zlib
from extract_rxdata import load_rxdata

def write_rgba_png(path, width, height, rgba_bytes):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw.extend(rgba_bytes[y * stride:(y + 1) * stride])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)

def make_solid_autotile(path, r, g, b, r2=None, g2=None, b2=None, alpha=255):
    if os.path.exists(path):
        return
    if r2 is None:
        r2, g2, b2 = max(0, r - 12), max(0, g - 12), max(0, b - 12)
    w, h = 96, 128
    buf = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            c = ((x // 8) + (y // 8)) & 1
            cr = r if c == 0 else r2
            cg = g if c == 0 else g2
            cb = b if c == 0 else b2
            idx = (y * w + x) * 4
            buf[idx:idx + 4] = bytes((cr, cg, cb, alpha))
    write_rgba_png(path, w, h, buf)

def read_png_rgba(path):
    """Decodifica PNG (RGB/Palette/RGBA de 8 bits) a (w, h, rgba_bytes) en Python puro."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"PNG invalido: {path}")
    pos = 8
    idat = bytearray()
    plte = None
    trns = None
    w = h = bd = ct = 0
    while pos < len(data):
        ln, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if ctype == b"IHDR":
            w, h, bd, ct = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"PLTE":
            plte = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat.extend(chunk)
        elif ctype == b"IEND":
            break
    if bd != 8 or ct not in (2, 3, 6):
        raise ValueError(f"PNG no soportado ({bd}bpp ct={ct}): {path}")
    raw = zlib.decompress(bytes(idat))
    bpp = 4 if ct == 6 else (3 if ct == 2 else 1)
    stride = w * bpp
    unfilt = bytearray(h * stride)
    in_pos = 0
    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c
    for y in range(h):
        ftype = raw[in_pos]
        in_pos += 1
        row_start = y * stride
        prev_start = (y - 1) * stride
        for i in range(stride):
            x = raw[in_pos]
            in_pos += 1
            a = unfilt[row_start + i - bpp] if i >= bpp else 0
            b = unfilt[prev_start + i] if y > 0 else 0
            c = unfilt[prev_start + i - bpp] if (y > 0 and i >= bpp) else 0
            if ftype == 0:
                v = x
            elif ftype == 1:
                v = (x + a) & 0xFF
            elif ftype == 2:
                v = (x + b) & 0xFF
            elif ftype == 3:
                v = (x + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                v = (x + paeth(a, b, c)) & 0xFF
            else:
                raise ValueError(f"Filtro PNG desconocido {ftype} en {path}")
            unfilt[row_start + i] = v
    if ct == 6:
        return w, h, bytes(unfilt)
    out = bytearray(w * h * 4)
    if ct == 2:
        for i in range(w * h):
            out[i * 4:i * 4 + 3] = unfilt[i * 3:i * 3 + 3]
            out[i * 4 + 3] = 255
    else:
        for i, idx in enumerate(unfilt):
            out[i * 4:i * 4 + 3] = plte[idx * 3:idx * 3 + 3]
            out[i * 4 + 3] = trns[idx] if (trns and idx < len(trns)) else 255
    return w, h, bytes(out)

class PixelCanvas:
    """Lienzo RGBA sencillo para generar sprites pixel-art con recorte automatico."""
    def __init__(self, w, h):
        self.w = w
        self.h = h
        self.buf = bytearray(w * h * 4)

    def pset(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            self.buf[i:i + 4] = bytes(c)

    def rect(self, x, y, rw, rh, c):
        for py in range(max(0, y), min(self.h, y + rh)):
            for px in range(max(0, x), min(self.w, x + rw)):
                i = (py * self.w + px) * 4
                self.buf[i:i + 4] = bytes(c)

    def disc(self, cx, cy, r, c):
        r2 = r * r
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r2:
                    self.pset(cx + dx, cy + dy, c)

    def oval(self, cx, cy, rx, ry, c):
        for dy in range(-ry, ry + 1):
            for dx in range(-rx, rx + 1):
                if (dx * dx) / float(max(1, rx * rx)) + (dy * dy) / float(max(1, ry * ry)) <= 1.0:
                    self.pset(cx + dx, cy + dy, c)

def make_bird_charset(path):
    """Genera el sprite animado de cuervos en vuelo (165-Small07) con 4 direcciones y aleteo."""
    w, h = 128, 128
    cv = PixelCanvas(w, h)
    blk = (18, 20, 26, 255)
    body = (34, 38, 52, 255)
    sheen = (66, 74, 98, 255)
    beak = (128, 130, 142, 255)
    eye = (225, 225, 235, 255)
    # 0=Abajo, 1=Izquierda, 2=Derecha, 3=Arriba
    for row in range(4):
        for col in range(4):
            ox, oy = col * 32, row * 32
            cx, cy = ox + 16, oy + 16
            bob = -1 if col == 0 else (1 if col == 2 else 0)
            flap = (-5, -1, 4, -1)[col]
            if row in (0, 3):  # Vista frontal (abajo) o trasera (arriba)
                # Alas extendidas simetricas
                for sx in (-1, 1):
                    for s in range(3, 12):
                        wy = cy + bob + (flap * s) // 10 - (1 if s > 7 else 0)
                        cv.rect(cx + sx * s, wy - 2, 1, 4, blk)
                        cv.pset(cx + sx * s, wy - 1, sheen if flap <= 0 else body)
                        cv.pset(cx + sx * s, wy, body)
                # Cola
                tail_y = cy + bob + (-5 if row == 0 else 4)
                cv.rect(cx - 2, tail_y, 5, 4, blk)
                cv.rect(cx - 1, tail_y + 1, 3, 2, sheen)
                # Cuerpo y cabeza
                cv.oval(cx, cy + bob, 4, 5, blk)
                cv.oval(cx, cy + bob, 3, 4, body)
                hy = cy + bob + (4 if row == 0 else -4)
                cv.disc(cx, hy, 3, blk)
                cv.disc(cx, hy, 2, body)
                if row == 0:
                    cv.pset(cx - 1, hy, eye)
                    cv.pset(cx + 1, hy, eye)
                    cv.rect(cx - 1, hy + 2, 3, 2, beak)
                else:
                    cv.rect(cx - 1, hy - 3, 3, 2, beak)
            else:  # Perfil izquierda (1) o derecha (2)
                fd = -1 if row == 1 else 1
                # Ala trasera y ala principal
                for s in range(1, 9):
                    wy = cy + bob + (flap * s) // 8 - 1
                    cv.rect(cx - fd * s, wy - 2, 2, 3, blk)
                    cv.pset(cx - fd * s, wy - 1, sheen)
                # Cola hacia atras
                cv.rect(cx - fd * 8 if fd > 0 else cx + 3, cy + bob, 5, 3, blk)
                # Cuerpo ovalado
                cv.oval(cx, cy + bob + 1, 5, 3, blk)
                cv.oval(cx, cy + bob + 1, 4, 2, body)
                # Cabeza y pico apuntando a la direccion
                hx = cx + fd * 4
                hy = cy + bob - 2
                cv.disc(hx, hy, 3, blk)
                cv.disc(hx, hy, 2, body)
                cv.pset(hx + fd, hy - 1, eye)
                cv.rect(hx + fd * 3, hy, 3, 2, beak)
    write_rgba_png(path, w, h, cv.buf)

def make_bird_perched_charset(path):
    """Genera cuervos posados en tierra (166-Small08) para Map003 y Map036."""
    w, h = 128, 128
    cv = PixelCanvas(w, h)
    blk = (18, 20, 26, 255)
    body = (30, 34, 46, 255)
    sheen = (68, 76, 102, 255)
    beak = (128, 130, 142, 255)
    leg = (95, 86, 76, 255)
    eye = (230, 230, 240, 255)
    for row in range(4):
        for col in range(4):
            ox, oy = col * 32, row * 32
            cx, cy = ox + 16, oy + 20
            hop = -1 if col == 2 else 0
            # Patas posadas
            cv.rect(cx - 2, oy + 25, 1, 3, leg)
            cv.rect(cx + 2, oy + 25, 1, 3, leg)
            cv.pset(cx - 3, oy + 28, leg)
            cv.pset(cx + 3, oy + 28, leg)
            if row in (0, 3):
                # Cuerpo compacto con alas plegadas
                cv.oval(cx, cy + hop, 5, 5, blk)
                cv.oval(cx, cy + hop, 4, 4, body)
                cv.rect(cx - 4, cy + hop - 1, 2, 5, sheen)
                cv.rect(cx + 3, cy + hop - 1, 2, 5, sheen)
                hx = cx + (1 if col == 1 else (-1 if col == 3 else 0))
                cv.disc(hx, cy + hop - 5, 3, blk)
                cv.disc(hx, cy + hop - 5, 2, body)
                if row == 0:
                    cv.pset(hx - 1, cy + hop - 5, eye)
                    cv.pset(hx + 1, cy + hop - 5, eye)
                    cv.rect(hx, cy + hop - 3, 2, 2, beak)
            else:
                fd = -1 if row == 1 else 1
                cv.oval(cx - fd, cy + hop + 1, 5, 4, blk)
                cv.oval(cx - fd, cy + hop + 1, 4, 3, body)
                cv.rect(cx - fd * 3, cy + hop, 4, 3, sheen)
                cv.rect(cx - fd * 6 if fd > 0 else cx + 3, cy + hop + 2, 4, 2, blk)
                hx = cx + fd * 3
                hy = cy + hop - 4
                cv.disc(hx, hy, 3, blk)
                cv.disc(hx, hy, 2, body)
                cv.pset(hx + fd, hy - 1, eye)
                cv.rect(hx + fd * 3, hy, 3, 2, beak)
    write_rgba_png(path, w, h, cv.buf)

def make_cell_door_charset(path):
    """Genera puerta de barrotes transparentes (173-Door04, 128x256, celdas 32x64) para celdas y jaulas."""
    w, h = 128, 256
    cv = PixelCanvas(w, h)
    # Paletas por columna (col 2: acero frio Map018/039/044/068, col 3: hierro oscuro con candado Map012)
    palettes = [
        ((105, 112, 124, 255), (156, 166, 180, 255), (52, 56, 66, 255), (176, 182, 190, 255)),
        ((96, 84, 74, 255),   (144, 128, 114, 255), (48, 40, 34, 255), (168, 152, 132, 255)),
        ((76, 84, 96, 255),   (132, 142, 158, 255), (34, 38, 46, 255), (170, 178, 192, 255)),
        ((66, 72, 82, 255),   (120, 128, 144, 255), (28, 30, 36, 255), (196, 202, 212, 255)),
    ]
    bar_xs = (5, 10, 15, 20, 25)
    for col in range(4):
        mid, hi, sh, lck = palettes[col]
        for row in range(4):
            ox, oy = col * 32, row * 64
            # Cuanto baja la reja en cada estado (row 0=cerrada, row 1/2=subiendo, row 3=abierta)
            bottom_y = (62, 42, 22, 7)[row]
            # Rieles laterales finos solo dentro de la zona de reja + guias superiores
            if row < 3:
                cv.rect(ox + 0, oy + 1, 2, bottom_y, sh)
                cv.rect(ox + 1, oy + 1, 1, bottom_y, mid)
                cv.rect(ox + 30, oy + 1, 2, bottom_y, sh)
                cv.rect(ox + 30, oy + 1, 1, bottom_y, mid)
                # Barrotes verticales con brillo cilindrico
                for bx in bar_xs:
                    cv.rect(ox + bx, oy + 2, 1, bottom_y - 2, hi)
                    cv.rect(ox + bx + 1, oy + 2, 1, bottom_y - 2, mid)
                    cv.rect(ox + bx + 2, oy + 2, 1, bottom_y - 2, sh)
                # Travesano superior
                cv.rect(ox + 0, oy + 2, 32, 3, hi)
                cv.rect(ox + 0, oy + 4, 32, 2, sh)
                # Travesano medio (se desplaza con la reja al subir)
                mid_bar_y = oy + max(6, bottom_y - 31)
                if bottom_y > 18:
                    cv.rect(ox + 1, mid_bar_y, 30, 1, hi)
                    cv.rect(ox + 1, mid_bar_y + 1, 30, 2, mid)
                    cv.rect(ox + 1, mid_bar_y + 3, 30, 1, sh)
                # Travesano inferior de la reja
                cv.rect(ox + 0, oy + bottom_y - 2, 32, 2, mid)
                cv.rect(ox + 0, oy + bottom_y, 32, 1, sh)
                # Placa de cerradura / candado en el lado derecho del travesano medio
                if bottom_y > 26:
                    lx = ox + (19 if col == 3 else 21)
                    ly = mid_bar_y - 1
                    lw = 8 if col == 3 else 6
                    cv.rect(lx, ly, lw, 7, sh)
                    cv.rect(lx + 1, ly + 1, lw - 2, 5, lck)
                    cv.rect(lx + lw // 2, ly + 3, 1, 2, sh)
            else:
                # row 3 (dir=8): puerta abierta, solo dintel superior recogido y puerta totalmente transparente
                cv.rect(ox + 0, oy + 0, 32, 3, sh)
                cv.rect(ox + 0, oy + 3, 32, 2, hi)
                cv.rect(ox + 0, oy + 5, 32, 2, sh)
                for bx in bar_xs:
                    cv.rect(ox + bx, oy + 6, 1, 2, hi)
                    cv.rect(ox + bx + 1, oy + 6, 2, 2, sh)
    write_rgba_png(path, w, h, cv.buf)

def make_switch02_charset(path):
    """Genera interruptor electrico de pared animado (179-Switch02, 128x128, celdas 32x32)."""
    w, h = 128, 128
    cv = PixelCanvas(w, h)
    outline = (22, 24, 30, 255)
    case_dk = (52, 58, 68, 255)
    case_md = (94, 102, 116, 255)
    case_hi = (138, 146, 162, 255)
    slot = (20, 22, 28, 255)
    lever_arm = (168, 174, 186, 255)
    led_cols = [
        (225, 45, 45, 255),   # row 0 (dir=2): apagado rojo
        (235, 140, 45, 255),  # row 1 (dir=4): transicion ambar
        (175, 225, 55, 255),  # row 2 (dir=6): transicion lima
        (55, 240, 105, 255),  # row 3 (dir=8): encendido verde
    ]
    lever_ys = (11, 14, 17, 20)
    for row in range(4):
        for col in range(4):
            ox, oy = col * 32, row * 32
            # Carcasa metalica centrada
            cv.rect(ox + 8, oy + 4, 16, 23, outline)
            cv.rect(ox + 9, oy + 5, 14, 21, case_md)
            cv.rect(ox + 9, oy + 5, 14, 2, case_hi)
            cv.rect(ox + 9, oy + 24, 14, 2, case_dk)
            # Remaches
            for rx, ry in ((10, 6), (21, 6), (10, 23), (21, 23)):
                cv.pset(ox + rx, oy + ry, case_dk)
            # LED indicador superior
            cv.rect(ox + 13, oy + 6, 6, 3, outline)
            cv.rect(ox + 14, oy + 7, 4, 1, led_cols[row])
            # Ranura vertical de la palanca
            cv.rect(ox + 14, oy + 10, 4, 14, slot)
            # Empunadura de palanca en la altura correspondiente al estado
            ly = oy + lever_ys[row]
            cv.rect(ox + 10, ly, 12, 4, outline)
            cv.rect(ox + 11, ly + 1, 10, 2, lever_arm)
            grip = (195, 52, 52, 255) if row < 2 else (56, 172, 86, 255)
            cv.rect(ox + 10, ly + 1, 2, 2, grip)
            cv.rect(ox + 20, ly + 1, 2, 2, grip)
    write_rgba_png(path, w, h, cv.buf)

def make_storekeeper02_charset(path):
    """Reconstruye 1:1 el charset RTP 148-Storekeeper02 (128x192, 32x48) a partir de las siluetas exactas de pic_t1/t2/t3."""
    w, h = 128, 192
    cv = PixelCanvas(w, h)

    t3_front = [
        "............00000............",
        "..........001111100..........",
        ".........01122222110.........",
        "........0122222222213........",
        "........0422222222213........",
        "........0041222221103........",
        "........0022144410003........",
        ".......560220000000065.......",
        ".......706820000000607.......",
        "......5970AB88888A60795......",
        "......7CC970ABBBA079CC7......",
        "......7CDDC7700077CDDC7......",
        "......7CDD77CCCCC77DDC7......",
        "......59C7557EEE7557C95......",
        "......5799CC9EEE9CC9975......",
        "......FC79GG7DDD7GG97CF......",
        "......FC756HGDDDGH657CF......",
        ".......F75IGG999GGI57F.......",
        ".......F9DC9557559CD9F.......",
        ".....55F7CC5777775CC7F55.....",
        "....5JJ0F79K75757K97F0JJ5....",
        "....5JLL00FFK5K5K5KFF00LLJ5.."[:29],
        "..5JMNL0220JFFFFFJ0220LNMJ5..",
        "..5MNNJ2110JNFFFNJ0112JNNM5..",
        ".5JNNMJ2110MJNNNJM0112JMNNJ5.",
        ".5LNML52112MMJJJMM01125LMNL5.",
        "5NJLML52212LMNNNML21225LMLJN5",
        "5JNJJL02122OLLLLLO22120LJJNJ5",
        "55JLNJP2212QOQOQOQ2122PJNLJ55",
        "595JJ5RP022QSQSQSQ220PR5JJ595",
        "599R5QRRQ02O4O4O4O20QRRQ5R995",
        ".RR..RRQRRQO4O4O4OQRRQRR..RR.",
        ".....RQQQQQRRRRRRRQQQQQR.....",
        ".....RQQQQQOOOOOOOQQQQQR.....",
        ".....RQOQOQRQOOOQRQOQOQR.....",
        ".....RQOOQOQRRRRRQOQOOQR.....",
        ".....RQO4OQOQRPRQOQO4OQR.....",
        "......ROO4OQQRPRQQO4OOR......",
        "......RO44OOQRPRQOO44OR......",
        "......RQO4OQQRPRQQO4OQR......",
        ".......ROOOOQRPRQOOOOR.......",
        ".......RQOOQQR.RQQOOQR.......",
        "........RRQRRT.TRRQRR........",
        "........TTTTTT.TTTTTT........",
        "........GTU8TG.GT8UTG........",
        ".........GGGG...GGGG.........",
    ]
    # Corregir fila 21 a ancho exacto 29
    t3_front[21] = "...5JLL00FFK5K5K5KFF00LLJ5..."

    t2_left = [
        "........000.......",
        "......0011100.....",
        ".....0112221100...",
        ".....01222222120..",
        "....0212222222123.",
        "....0221222222143.",
        "...02200112221423.",
        "...82000001144203.",
        "...06800000000003.",
        "..F70AB8000000003.",
        ".F7C970AB80000080.",
        ".F9CDC970ABIAA805F",
        ".FCEED77970000057F",
        ".FCEE7557CC977999F",
        "..7DD9CCCCCCC9999K",
        "..FDD7GG9DDC97797K",
        "..FCCGH65DDD9DF97K",
        "..559GGI5DDD9CF75K",
        ".57755CDDDDD97F7K.",
        ".557775CDDDC9F55K.",
        ".KK75759CCC9FP5K..",
        "..KKK5K77970KP....",
        "...5KKKK002120P...",
        "..5MFF50211122P...",
        ".5MJNM02115J520P..",
        ".JNMJ50112LMM50P..",
        "JMNNM02115MNNJ00P.",
        "JMNNJ0111JNNNM00P.",
        "QJMM50212JNNNM50P.",
        "Q4JJ02122MNNNM500.",
        "54Q502220JNNML500.",
        "545402220NJLLM50..",
        ".R5452005JNNNL5R..",
        "..RQQ45455975RQR..",
        "...RQQQQQQ995RQR..",
        "...RQQOO4OORRQQR..",
        "....RQQO44OQQQQR..",
        "....RQOO4OQOQQR...",
        "....RPQOO4OQOQR...",
        ".....PQOOOQOQQR...",
        ".....PRQOOOQOR....",
        "....FFPRQOOOQR....",
        "....FFFPRRRRRP....",
        "......FTTTTTTP....",
        ".......T88TTP.....",
        "........TTPP......",
    ]

    t1_back = [
        "............00000............",
        "..........001111100..........",
        ".........01122222110.........",
        "........0122222222213........",
        "........0422222222213........",
        "........0422222222213........",
        "........0041222221103........",
        ".......500221444100035.......",
        ".......760220000000067.......",
        "......59068200000006095......",
        "......7970AB88888A607C7......",
        "......7CC970ABBBA079CC7......",
        "......7CCCC9700079CCCC7......",
        "......5CCCCCCCCCCCCCCC5......",
        "......59CCCCCCCCCCCCC95......",
        "......F9CCCCCCCCCCCCC9F......",
        "......F7CCCCCCCCCCCCC7F......",
        ".......F9CCCCCCCCCCC9F.......",
        ".......F79CCCCCCCCC97F.......",
        ".....550F799CCCCC997F055.....",
        "....5JJ50FF7799977FF05JJ5....",
        "...5JLL5000KKKKKKK0005LLJ5...",
        "..5JMNL522000000000225LNMJ5..",
        "..5MNNJ511122222221115JNNM5..",
        ".5JNNMJ211111111111112JMNNJ5.",
        ".5LNML52111111111111125LMNL5.",
        "5NJLML52212121112121225LMLJN5",
        "5JNJJL02121212121212120LJJNJ5",
        "55JLNJP221212121212122PJNLJ55",
        "595JJ5RP0222121212220PR5JJ595",
        "599R5QRRQ00022222000QRRQ5R995",
        ".RR..RRQRRQO4O4O4OQRRQRR..RR.",
        ".....RQOOOQRRRRRRRQOOOQR.....",
        ".....RQOO4O4444444O4OOQR.....",
        ".....RQOOO4O44444O4OOOQR.....",
        ".....RQOOQOQQQQQQQOQOOQR.....",
        ".....RQO4OQOQRPRQOQO4OQR.....",
        "......ROO4OQQRPRQQO4OOR......",
        "......RO44OOQRPRQOO44OR......",
        "......RQO4OQQRPRQQO4OQR......",
        ".......ROOOOQRPRQOOOOR.......",
        ".......RQOOQQR.RQQOOQR.......",
        "........RRQRRT.TRRQRR........",
        "........TTTTTT.TTTTTT........",
        "........GT88TG.GT88TG........",
        ".........GGGG...GGGG.........",
    ]

    def map_color(sym, x, y, gw):
        # Region anatomica para colores fieles al RTP 148-Storekeeper02
        if sym == ".":
            return None
        # Sombrero azul (y <= 12): azul pizarra / marino con visera y brillo
        if sym in ("0", "1", "2", "3", "4", "6", "8", "A", "B") and (y <= 12 or (y <= 32 and sym in ("0", "1", "2"))):
            if sym == "0":
                return (28, 38, 68, 255)
            if sym == "1":
                return (46, 68, 122, 255)
            if sym == "2":
                return (68, 98, 162, 255)
            if sym == "3":
                return (34, 46, 82, 255)
            if sym == "4":
                return (108, 144, 208, 255)
            if sym == "6":
                return (40, 56, 98, 255)
            if sym == "8":
                return (132, 168, 224, 255)
            if sym == "A":
                return (54, 78, 134, 255)
            if sym == "B":
                return (88, 122, 182, 255)

        # Ojos (y=15 esclerotica/parpado superior, y=16 pupila H + iris 6 + esclerotica G, y=17 pomulo iluminado)
        if y == 15:
            if sym == "G":
                return (230, 232, 240, 255)
        if y == 16:
            if sym == "G":
                return (246, 248, 255, 255)
            if sym == "H":
                return (24, 26, 38, 255)
            if sym == "6":
                return (54, 68, 102, 255)
        if y == 17:
            if sym == "G":
                return (244, 212, 182, 255)
            if sym == "I":
                return (182, 130, 102, 255)

        # Zapatos oscuros (y >= 42)
        if y >= 42 and sym in ("T", "G", "U", "8"):
            if sym == "T":
                return (62, 46, 40, 255)
            if sym == "G":
                return (30, 22, 20, 255)
            return (104, 84, 74, 255)

        # Pantalones holgados marrones (cinturon/piernas y >= 27)
        if y >= 27 and sym in ("R", "Q", "O", "P", "4"):
            if sym == "R":
                return (74, 48, 34, 255)
            if sym == "P":
                return (48, 32, 22, 255)
            if sym == "Q":
                return (108, 72, 48, 255)
            if sym == "O":
                return (138, 96, 64, 255)
            if sym == "4":
                return (172, 122, 84, 255)
        if sym == "S":
            return (192, 172, 140, 255)

        # Sueter beige claro / crema (brazos y pecho central)
        if sym == "N":
            return (242, 232, 210, 255)
        if sym == "M":
            return (226, 210, 182, 255)
        if sym == "J":
            return (202, 184, 152, 255)
        if sym == "L":
            return (174, 156, 126, 255)

        # Cabeza calva / piel / cejas y bigote castaño (y <= 31)
        if sym == "E":
            return (250, 220, 192, 255)
        if sym == "C":
            return (240, 198, 166, 255)
        if sym == "D":
            return (226, 176, 142, 255)
        if sym == "9":
            return (196, 144, 110, 255)
        if sym == "7":
            # Bigote / contorno calido castano
            if 18 <= y <= 21:
                return (134, 84, 52, 255)
            return (156, 108, 76, 255)
        if sym == "5":
            # Contorno calido y cejas/bigote oscuro
            if y in (13, 18, 19, 20, 21):
                return (88, 52, 34, 255)
            return (64, 42, 32, 255)
        if sym == "F":
            return (54, 34, 24, 255)
        if sym == "K":
            return (78, 46, 28, 255)
        return (56, 40, 32, 255)

    def render_grid_to_cell(grid, ox, oy, flip_x=False, col=0, is_side=False):
        gh = len(grid)
        gw = len(grid[0])
        dx0 = (32 - gw) // 2
        dy0 = 1
        # Sombra bajo los pies en la base 32x48
        cv.rect(ox + 10, oy + 45, 12, 2, (20, 20, 24, 80))

        # Caminata suave sin cortes ni grietas horizontales/verticales
        bob = -1 if col in (1, 3) else 0
        # 1) Dibujamos cadera y muslos continuos (y >= 32) sin partir verticalmente el muslo
        for y in range(32, gh):
            for rx in range(gw):
                sx = (gw - 1 - rx) if flip_x else rx
                rgba = map_color(grid[y][sx], rx, y, gw)
                if rgba is None:
                    continue
                px = ox + dx0 + rx
                py = oy + dy0 + y
                if col in (1, 3):
                    if not is_side:
                        # Paso frontal/trasero: una pierna se eleva 1px en la pantorrilla/pie
                        if ((col == 1 and rx < gw // 2) or (col == 3 and rx >= gw // 2)) and y >= 38:
                            py -= 1
                    else:
                        # Paso lateral continuo: ligera oscilacion de pantorrilla/pie sin abrir grietas en el muslo
                        if y >= 40:
                            is_front_leg = (rx < gw // 2) if not flip_x else (rx >= gw // 2)
                            if (col == 1 and is_front_leg) or (col == 3 and not is_front_leg):
                                py -= 1
                if ox <= px < ox + 32 and oy <= py < oy + 48:
                    cv.pset(px, py, rgba)

        # 2) Cabeza, sombrero, sueter, chaleco y cintura (y <= 32) con bob = -1 y balanceo de brazos
        for y in range(33):
            for rx in range(gw):
                sx = (gw - 1 - rx) if flip_x else rx
                rgba = map_color(grid[y][sx], rx, y, gw)
                if rgba is None:
                    continue
                px = ox + dx0 + rx
                py = oy + dy0 + y + bob
                if is_side and col in (1, 3) and 24 <= y <= 32 and 7 <= sx <= 13:
                    # Pintar fondo de chaleco azul debajo de la manga desplazada para evitar huecos de 1px
                    rgba = (54, 78, 134, 255)
                elif not is_side and col in (1, 3) and y >= 21:
                    if rx <= 5:
                        py += (1 if col == 1 else -1)
                    elif rx >= gw - 6:
                        py += (-1 if col == 1 else 1)
                if ox <= px < ox + 32 and oy <= py < oy + 48:
                    cv.pset(px, py, rgba)

        # 3) Manga lateral sobre el chaleco en col=1,3 para mantener el contorno limpio en ambos sentidos
        if is_side and col in (1, 3):
            arm_dx = (-1 if col == 1 else 1) * (-1 if flip_x else 1)
            for y in range(24, 33):
                for rx in range(gw):
                    sx = (gw - 1 - rx) if flip_x else rx
                    if not (7 <= sx <= 13):
                        continue
                    rgba = map_color(grid[y][sx], rx, y, gw)
                    if rgba is None:
                        continue
                    px = ox + dx0 + rx + arm_dx
                    py = oy + dy0 + y + bob
                    if ox <= px < ox + 32 and oy <= py < oy + 48:
                        cv.pset(px, py, rgba)

    for row in range(4):  # 0=Abajo, 1=Izquierda, 2=Derecha, 3=Arriba
        for col in range(4):
            ox, oy = col * 32, row * 48
            if row == 0:
                render_grid_to_cell(t3_front, ox, oy, flip_x=False, col=col, is_side=False)
            elif row == 1:
                render_grid_to_cell(t2_left, ox, oy, flip_x=False, col=col, is_side=True)
            elif row == 2:
                render_grid_to_cell(t2_left, ox, oy, flip_x=True, col=col, is_side=True)
            else:
                render_grid_to_cell(t1_back, ox, oy, flip_x=False, col=col, is_side=False)

    write_rgba_png(path, w, h, cv.buf)

def patch_tileset_floor_holes(out_img):
    """Rellena pixeles alpha==0 en losetas interiores de room-01-* y Forest01 que dejan huecos negros."""
    from collections import deque
    floor_hole_tiles = {
        "ts_room-01-ud.png":    [563, 586, 587],
        "ts_room-01-udw.png":   [492, 493, 494, 495, 500, 508, 509, 510, 1298, 1304],
        "ts_room-01-udw2.png":  [492, 493, 494, 495, 500, 508, 509],
        "ts_room-01-udw2a.png": [407, 459, 520],
        "ts_room-01-udy.png":   [572, 574, 575, 581, 582, 583, 604, 606, 644, 645, 646, 647, 755],
    }
    patched_tiles = 0
    patched_pixels = 0

    def fill_holes_in_tile(buf, w, tx, ty, holes, dim_factor):
        fills = []
        for dx, dy, off in holes:
            best = None
            for r in range(1, 10):
                for oy in range(max(0, dy - r), min(32, dy + r + 1)):
                    for ox in range(max(0, dx - r), min(32, dx + r + 1)):
                        so = ((ty + oy) * w + (tx + ox)) * 4
                        if buf[so + 3] >= 200:
                            d2 = (ox - dx) * (ox - dx) + (oy - dy) * (oy - dy)
                            if best is None or d2 < best[0]:
                                best = (d2, buf[so], buf[so + 1], buf[so + 2])
                if best is not None:
                    break
            if best is not None:
                _, cr, cg, cb = best
                fills.append((off, int(cr * dim_factor), int(cg * dim_factor), int(cb * dim_factor)))
        for off, cr, cg, cb in fills:
            buf[off] = cr
            buf[off + 1] = cg
            buf[off + 2] = cb
            buf[off + 3] = 255
        return len(fills)

    for fn in sorted(os.listdir(out_img)):
        if not (fn.startswith("ts_room-01-") or fn == "ts_003-Forest01.png") or not fn.endswith(".png"):
            continue
        p = os.path.join(out_img, fn)
        w, h, raw = read_png_rgba(p)
        buf = bytearray(raw)
        modified = False
        explicit_set = set(floor_hole_tiles.get(fn, []))
        cols, rows = w // 32, h // 32
        for row in range(rows):
            for col in range(cols):
                tid = 384 + row * 8 + col
                tx, ty = col * 32, row * 32
                transp = 0
                for dy in range(32):
                    for dx in range(32):
                        a = buf[((ty + dy) * w + (tx + dx)) * 4 + 3]
                        if a < 128:
                            transp += 1
                if tid in explicit_set and 0 < transp <= 80:
                    holes = []
                    for dy in range(32):
                        for dx in range(32):
                            off = ((ty + dy) * w + (tx + dx)) * 4
                            if buf[off + 3] == 0:
                                holes.append((dx, dy, off))
                    nfill = fill_holes_in_tile(buf, w, tx, ty, holes, 0.58)
                    if nfill > 0:
                        patched_tiles += 1
                        patched_pixels += nfill
                        modified = True
                elif (1024 - transp) >= 650 and transp > 0:
                    # Escaneo automático seguro de microporos interiores (sin tocar rejas/celosías ni transparencia de bordes)
                    vis = bytearray(1024)
                    interior_comps = []
                    for dy in range(32):
                        for dx in range(32):
                            if vis[dy * 32 + dx]:
                                continue
                            off = ((ty + dy) * w + (tx + dx)) * 4
                            if buf[off + 3] != 0:
                                continue
                            q = deque([(dx, dy)])
                            vis[dy * 32 + dx] = 1
                            comp = []
                            touches_edge = False
                            while q:
                                cx, cy = q.popleft()
                                comp.append((cx, cy, ((ty + cy) * w + (tx + cx)) * 4))
                                if cx == 0 or cy == 0 or cx == 31 or cy == 31:
                                    touches_edge = True
                                for nx, ny in ((cx - 1, cy), (cx + 1, cy), (cx, cy - 1), (cx, cy + 1)):
                                    if 0 <= nx < 32 and 0 <= ny < 32 and not vis[ny * 32 + nx]:
                                        if buf[((ty + ny) * w + (tx + nx)) * 4 + 3] == 0:
                                            vis[ny * 32 + nx] = 1
                                            q.append((nx, ny))
                            if not touches_edge and 1 <= len(comp) <= 18:
                                interior_comps.append(comp)
                    if 1 <= len(interior_comps) <= 6:
                        holes = [pt for comp in interior_comps for pt in comp]
                        nfill = fill_holes_in_tile(buf, w, tx, ty, holes, 0.78)
                        if nfill > 0:
                            patched_tiles += 1
                            patched_pixels += nfill
                            modified = True
        if modified:
            write_rgba_png(p, w, h, buf)
    print(f"Parcheados {patched_pixels} pixeles en {patched_tiles} losetas (room-01-* / Forest01).")

def make_missing_icons(out_img):
    """Genera los iconos RTP faltantes (031-Key03 para llaves y 023-Potion03 para Vinagre) 24x24 RGBA."""
    # 1) icon_031-Key03.png basado en la geometría exacta de icon_031-Key03_red.png en tono latón/oro viejo
    key_out = os.path.join(out_img, "icon_031-Key03.png")
    key_src = os.path.join(out_img, "icon_031-Key03_red.png")
    if not os.path.exists(key_out):
        if os.path.exists(key_src):
            kw, kh, kbuf = read_png_rgba(key_src)
            out = bytearray(kbuf)
            for i in range(0, len(out), 4):
                r, g, b, a = out[i], out[i + 1], out[i + 2], out[i + 3]
                if a == 0:
                    continue
                # Convertir pigmento rojo/carmesí de la llave a latón dorado brillante
                if r > g + 16 and r > b + 16:
                    lum = (r * 299 + g * 587 + b * 114) // 1000
                    nr = min(255, int(lum * 1.35 + 48))
                    ng = min(245, int(lum * 1.15 + 32))
                    nb = min(195, int(lum * 0.55 + 16))
                    out[i], out[i + 1], out[i + 2] = nr, ng, nb
                else:
                    out[i] = min(255, r + 14)
                    out[i + 1] = min(255, g + 14)
                    out[i + 2] = min(255, b + 12)
            write_rgba_png(key_out, kw, kh, out)
        else:
            cv = PixelCanvas(24, 24)
            ol = (28, 22, 16, 255)
            gold = (224, 184, 72, 255)
            hi = (252, 232, 148, 255)
            cv.disc(7, 16, 5, ol)
            cv.disc(7, 16, 4, gold)
            cv.disc(7, 16, 2, (0, 0, 0, 0))
            cv.pset(5, 13, hi)
            for k in range(10, 20):
                cv.pset(k, 23 - k, ol)
                cv.pset(k + 1, 24 - k, gold)
                cv.pset(k, 22 - k, hi)
            cv.rect(17, 7, 3, 2, gold)
            cv.rect(15, 9, 3, 2, gold)
            write_rgba_png(key_out, 24, 24, cv.buf)

    # 2) icon_023-Potion03.png (Frasco con corcho para Vinagre, estilo RMXP 24x24)
    pot_out = os.path.join(out_img, "icon_023-Potion03.png")
    if not os.path.exists(pot_out):
        cv = PixelCanvas(24, 24)
        ol = (26, 28, 38, 255)
        cork_dk = (128, 86, 48, 255)
        cork_lt = (182, 132, 82, 255)
        glass = (206, 226, 242, 255)
        glass_hi = (246, 252, 255, 255)
        glass_sh = (132, 160, 188, 255)
        liq = (224, 206, 118, 255)
        liq_hi = (250, 238, 168, 255)
        liq_dk = (176, 154, 74, 255)
        # Corcho
        cv.rect(10, 2, 4, 3, ol)
        cv.rect(10, 3, 4, 2, cork_dk)
        cv.rect(11, 2, 2, 2, cork_lt)
        # Cuello de vidrio
        cv.rect(9, 5, 6, 2, ol)
        cv.rect(10, 5, 4, 1, glass_hi)
        cv.rect(10, 6, 4, 4, ol)
        cv.rect(11, 6, 2, 4, glass)
        # Cuerpo redondeado de la botella
        cv.oval(12, 15, 6, 6, ol)
        cv.oval(12, 15, 5, 5, glass)
        cv.oval(12, 16, 4, 4, liq)
        cv.rect(9, 13, 6, 2, liq_hi)
        cv.rect(9, 18, 6, 2, liq_dk)
        cv.rect(10, 12, 4, 1, glass_sh)
        # Brillo cristalino izquierdo
        cv.rect(8, 13, 1, 4, glass_hi)
        cv.pset(9, 12, glass_hi)
        write_rgba_png(pot_out, 24, 24, cv.buf)

def enhance_tiny_object_sprites(out_img):
    """Mejora sutilmente contraste/visibilidad en sprites tiny/oscuros sin perder su estilo original."""
    targets = {
        "char_nejiana.png": 1.65,
        "char_oil.png": 1.32,
        "char_bou.png": 1.25,
    }
    for fn, gain in targets.items():
        p = os.path.join(out_img, fn)
        if not os.path.exists(p):
            continue
        w, h, raw = read_png_rgba(p)
        buf = bytearray(raw)
        for i in range(0, len(buf), 4):
            a = buf[i + 3]
            if a < 20:
                continue
            r, g, b = buf[i], buf[i + 1], buf[i + 2]
            buf[i] = min(245, int(r * gain + 18))
            buf[i + 1] = min(245, int(g * gain + 18))
            buf[i + 2] = min(245, int(b * gain + 18))
        write_rgba_png(p, w, h, buf)

def make_bird_charset_legacy_unused(path):
    """Genera el sprite de cuervos (165-Small07) para la intro exterior de Map003."""
    if os.path.exists(path):
        return
    w, h = 128, 128
    buf = bytearray(w * h * 4)
    for row in range(4):
        for col in range(4):
            cx = col * 32 + 16
            cy = row * 32 + 16
            wing_dy = -4 if (col % 2 == 0) else 3
            for dy in range(-6, 7):
                for dx in range(-10, 11):
                    px, py = cx + dx, cy + dy
                    if abs(dx) <= 4 and abs(dy) <= 3:
                        idx = (py * w + px) * 4
                        buf[idx:idx + 4] = bytes((25, 25, 35, 255))
                    elif 4 < abs(dx) <= 10 and abs(dy - (wing_dy * abs(dx) // 8)) <= 2:
                        idx = (py * w + px) * 4
                        buf[idx:idx + 4] = bytes((40, 40, 55, 255))
    write_rgba_png(path, w, h, buf)

def make_fallback_rtp_images(out_img):
    """Crea texturas equivalentes para recursos RTP de RPG Maker XP no incluidos en Ao Oni/Graphics."""
    make_solid_autotile(os.path.join(out_img, "at_009-G2_Water01.png"), 18, 36, 58, 14, 28, 48)
    make_solid_autotile(os.path.join(out_img, "at_010-G2_Ground01.png"), 22, 34, 24, 18, 28, 20)
    make_solid_autotile(os.path.join(out_img, "at_011-G2_Ground02.png"), 28, 38, 28, 22, 30, 22)
    make_solid_autotile(os.path.join(out_img, "at_038-Tree03.png"), 16, 28, 18, 12, 22, 14)
    make_solid_autotile(os.path.join(out_img, "at_039-Tree04.png"), 14, 24, 16, 10, 18, 12)
    make_solid_autotile(os.path.join(out_img, "at_042-Ground02.png"), 38, 36, 34, 32, 30, 28)
    make_solid_autotile(os.path.join(out_img, "at_062-CF_Lava01.png"), 45, 20, 20, 35, 15, 15)
    make_solid_autotile(os.path.join(out_img, "at_063-CF_Shadow01.png"), 0, 0, 0, 0, 0, 0, 120)
    make_solid_autotile(os.path.join(out_img, "at_064-CF_Ground01.png"), 48, 44, 40, 42, 38, 34)
    make_solid_autotile(os.path.join(out_img, "at_065-CF_Ground02.png"), 42, 38, 34, 36, 32, 28)
    make_solid_autotile(os.path.join(out_img, "at_066-CF_Ground03.png"), 36, 32, 28, 30, 26, 22)
    make_solid_autotile(os.path.join(out_img, "at_067-CF_Ground04.png"), 30, 28, 26, 24, 22, 20)
    make_bird_charset(os.path.join(out_img, "char_165-Small07.png"))
    make_bird_perched_charset(os.path.join(out_img, "char_166-Small08.png"))
    make_cell_door_charset(os.path.join(out_img, "char_173-Door04.png"))
    make_switch02_charset(os.path.join(out_img, "char_179-Switch02.png"))
    make_storekeeper02_charset(os.path.join(out_img, "char_148-Storekeeper02.png"))
    patch_tileset_floor_holes(out_img)
    make_missing_icons(out_img)
    enhance_tiny_object_sprites(out_img)

    # Equivalente de tileset cueva si falta RTP
    ts_src = os.path.join(out_img, "ts_room-03-ud.png")
    if os.path.exists(ts_src) and not os.path.exists(os.path.join(out_img, "ts_044-Cave02.png")):
        shutil.copy2(ts_src, os.path.join(out_img, "ts_044-Cave02.png"))

def synth_pcm(path, duration_sec, fn_sample):
    if os.path.exists(path):
        return
    nframes = int(duration_sec * 48000)
    buf = bytearray(nframes * 4)
    for i in range(nframes):
        t = i / 48000.0
        val = int(max(-32000, min(32000, fn_sample(t, duration_sec))))
        struct.pack_into("<hh", buf, i * 4, val, val)
    with open(path, "wb") as f:
        f.write(buf)

def clean_obj(obj):
    if isinstance(obj, dict):
        cls = obj.get("__class__", "")
        if cls == "RPG::MoveRoute":
            return {
                "repeat": bool(obj.get("@repeat", False)),
                "skippable": bool(obj.get("@skippable", False)),
                "list": [clean_obj(x) for x in obj.get("@list", [])]
            }
        if cls == "RPG::MoveCommand":
            return {
                "code": int(obj.get("@code", 0)),
                "params": [clean_obj(x) for x in obj.get("@parameters", [])]
            }
        if cls == "RPG::AudioFile":
            return {
                "name": str(obj.get("@name", "")),
                "volume": int(obj.get("@volume", 100)),
                "pitch": int(obj.get("@pitch", 100))
            }
        if cls in ("Tone", "Color"):
            return {
                "r": float(obj.get("r", 0.0)),
                "g": float(obj.get("g", 0.0)),
                "b": float(obj.get("b", 0.0)),
                "a": float(obj.get("a", 0.0))
            }
        return {k: clean_obj(v) for k, v in obj.items() if k != "__class__"}
    if isinstance(obj, list):
        return [clean_obj(x) for x in obj]
    if isinstance(obj, (int, float, bool, str)) or obj is None:
        return obj
    return str(obj)

def serialize_cmd_list(cmd_list):
    out = []
    for c in cmd_list:
        if not isinstance(c, dict):
            continue
        code = int(c.get("@code", 0))
        indent = int(c.get("@indent", 0))
        params = [clean_obj(x) for x in c.get("@parameters", [])]
        out.append({"c": code, "i": indent, "p": params})
    return out

def serialize_page(p):
    cond = p.get("@condition", {})
    gfx = p.get("@graphic", {})
    mr = p.get("@move_route", {})
    return {
        "sw1_valid": bool(cond.get("@switch1_valid", False)),
        "sw1_id": int(cond.get("@switch1_id", 1)),
        "sw2_valid": bool(cond.get("@switch2_valid", False)),
        "sw2_id": int(cond.get("@switch2_id", 1)),
        "var_valid": bool(cond.get("@variable_valid", False)),
        "var_id": int(cond.get("@variable_id", 1)),
        "var_val": int(cond.get("@variable_value", 0)),
        "self_valid": bool(cond.get("@self_switch_valid", False)),
        "self_ch": str(cond.get("@self_switch_ch", "A"))[:1],
        "char_name": str(gfx.get("@character_name", "")),
        "char_hue": int(gfx.get("@character_hue", 0)),
        "dir": int(gfx.get("@direction", 2)),
        "pattern": int(gfx.get("@pattern", 0)),
        "opacity": int(gfx.get("@opacity", 255)),
        "blend": int(gfx.get("@blend_type", 0)),
        "tile_id": int(gfx.get("@tile_id", 0)),
        "move_type": int(p.get("@move_type", 0)),
        "move_speed": int(p.get("@move_speed", 3)),
        "move_freq": int(p.get("@move_frequency", 3)),
        "move_route": clean_obj(mr),
        "walk_anime": bool(p.get("@walk_anime", True)),
        "step_anime": bool(p.get("@step_anime", False)),
        "dir_fix": bool(p.get("@direction_fix", False)),
        "through": bool(p.get("@through", False)),
        "always_top": bool(p.get("@always_on_top", False)),
        "trigger": int(p.get("@trigger", 0)),
        "list": serialize_cmd_list(p.get("@list", []))
    }

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    game_dir = os.path.join(root, "Ao Oni")
    out_assets = os.path.join(root, "src", "aooni_ps4", "assets")
    if os.path.isdir(out_assets):
        shutil.rmtree(out_assets)
    out_data = os.path.join(out_assets, "misc")
    os.makedirs(out_data, exist_ok=True)
    for d in ("audio", "fonts", "images", "videos"):
        os.makedirs(os.path.join(out_assets, d), exist_ok=True)

    # 1) Cargar System, Actors, Items, CommonEvents, Tilesets
    sys_raw = load_rxdata(os.path.join(game_dir, "Data", "System.rxdata"))
    actors_raw = load_rxdata(os.path.join(game_dir, "Data", "Actors.rxdata"))
    items_raw = load_rxdata(os.path.join(game_dir, "Data", "Items.rxdata"))
    ces_raw = load_rxdata(os.path.join(game_dir, "Data", "CommonEvents.rxdata"))
    tilesets_raw = load_rxdata(os.path.join(game_dir, "Data", "Tilesets.rxdata"))

    actors = {}
    for a in actors_raw:
        if isinstance(a, dict) and a.get("@id"):
            actors[int(a["@id"])] = {
                "name": str(a.get("@name", "")),
                "char_name": str(a.get("@character_name", ""))
            }

    items = {}
    for it in items_raw:
        if isinstance(it, dict) and it.get("@id"):
            items[int(it["@id"])] = {
                "name": str(it.get("@name", "")),
                "icon": str(it.get("@icon_name", "")),
                "desc": str(it.get("@description", "")),
                "common_event_id": int(it.get("@common_event_id", 0)),
                "consumable": bool(it.get("@consumable", False))
            }

    common_events = []
    for ce in ces_raw:
        if isinstance(ce, dict) and ce.get("@id"):
            lst = serialize_cmd_list(ce.get("@list", []))
            if len(lst) > 1:
                common_events.append({
                    "id": int(ce["@id"]),
                    "name": str(ce.get("@name", "")),
                    "trigger": int(ce.get("@trigger", 0)),
                    "switch_id": int(ce.get("@switch_id", 1)),
                    "list": lst
                })

    tilesets = {}
    for t in tilesets_raw:
        if not t:
            continue
        tid = int(t.get("@id", 0))
        tname = str(t.get("@tileset_name", ""))
        at_names = [str(x or "") for x in (t.get("@autotile_names") or [])]
        while len(at_names) < 7:
            at_names.append("")
        passages = t.get("@passages", {}).get("data", [])
        priorities = t.get("@priorities", {}).get("data", [])
        tilesets[tid] = (tname, at_names[:7], passages, priorities)

    game_db = {
        "start_map_id": int(sys_raw.get("@start_map_id", 75)),
        "start_x": int(sys_raw.get("@start_x", 9)),
        "start_y": int(sys_raw.get("@start_y", 8)),
        "party": [int(x) for x in sys_raw.get("@party_members", [1])],
        "actors": actors,
        "items": items,
        "common_events": common_events
    }
    with open(os.path.join(out_data, "game_db.json"), "w", encoding="utf-8") as f:
        json.dump(game_db, f, ensure_ascii=False, separators=(",", ":"))

    # 2) Convertir cada MapXXX.rxdata a map_XXX.bin + map_XXX.json
    map_files = sorted(glob.glob(os.path.join(game_dir, "Data", "Map[0-9][0-9][0-9].rxdata")))
    converted = 0
    for mf in map_files:
        base = os.path.basename(mf)
        mid = int(base[3:6])
        m = load_rxdata(mf)
        w = int(m.get("@width", 20))
        h = int(m.get("@height", 15))
        ts_id = int(m.get("@tileset_id", 1))
        ts_name, at_names, passages, priorities = tilesets.get(ts_id, ("", [""] * 7, [], []))
        tbl = m.get("@data", {}).get("data", [0] * (w * h * 3))
        if len(tbl) < w * h * 3:
            tbl = list(tbl) + [0] * (w * h * 3 - len(tbl))

        bin_path = os.path.join(out_data, f"map_{mid:03d}.bin")
        with open(bin_path, "wb") as f:
            ts_bytes = ts_name.encode("utf-8")[:63].ljust(64, b"\x00")
            pass_bytes = bytes([p & 0xFF for p in passages[:4096]])
            prio_bytes = bytes([p & 0xFF for p in priorities[:4096]])
            f.write(struct.pack("<4I", 0x4D504F42, w, h, ts_id))
            f.write(ts_bytes)
            for an in at_names[:7]:
                f.write(an.encode("utf-8")[:63].ljust(64, b"\x00"))
            f.write(struct.pack(f"<{w * h * 3}h", *tbl[:w * h * 3]))
            f.write(struct.pack("<I", len(pass_bytes)))
            f.write(pass_bytes)
            f.write(struct.pack("<I", len(prio_bytes)))
            f.write(prio_bytes)

        ev_list = []
        ev_dict = m.get("@events", {})
        for k, ev in sorted(ev_dict.items(), key=lambda x: int(x[0])):
            if not isinstance(ev, dict):
                continue
            ev_list.append({
                "id": int(ev.get("@id", int(k))),
                "name": str(ev.get("@name", "")),
                "x": int(ev.get("@x", 0)),
                "y": int(ev.get("@y", 0)),
                "pages": [serialize_page(p) for p in ev.get("@pages", [])]
            })
        bgm_obj = clean_obj(m.get("@bgm", {}))
        bgs_obj = clean_obj(m.get("@bgs", {}))
        map_meta = {
            "id": mid,
            "autoplay_bgm": bool(m.get("@autoplay_bgm", False)),
            "bgm": bgm_obj if isinstance(bgm_obj, dict) else {},
            "autoplay_bgs": bool(m.get("@autoplay_bgs", False)),
            "bgs": bgs_obj if isinstance(bgs_obj, dict) else {},
            "events": ev_list
        }
        # Parches PS4: interacciones faltantes en los datos originales (estanteria alta Map 17 y cuerpo de Mika Map 25)
        if mid == 17:
            for ev in ev_list:
                if ev["id"] == 8 and ev["pages"] and len(ev["pages"][0]["list"]) <= 1:
                    ev["pages"][0]["list"] = [
                        {"c": 111, "i": 0, "p": [0, 49, 1]},
                        {"c": 104, "i": 1, "p": [2, 0]},
                        {"c": 101, "i": 1, "p": ["\\f[hirosif]\\name[\\N[2]]Algo brilla en lo alto"]},
                        {"c": 401, "i": 1, "p": ["de la estantería..."]},
                        {"c": 101, "i": 1, "p": ["\\f[hirosif]\\name[\\N[2]]Necesito una silla aquí"]},
                        {"c": 401, "i": 1, "p": ["para poder alcanzarlo."]},
                        {"c": 0, "i": 1, "p": []},
                        {"c": 412, "i": 0, "p": []},
                        {"c": 0, "i": 0, "p": []},
                    ]
                    break
        elif mid == 25:
            for ev in ev_list:
                if ev["id"] == 9 and ev["pages"] and len(ev["pages"][0]["list"]) <= 1:
                    ev["pages"][0]["list"] = [
                        {"c": 111, "i": 0, "p": [0, 49, 1]},
                        {"c": 104, "i": 1, "p": [2, 0]},
                        {"c": 101, "i": 1, "p": ["\\f[hirosif]\\name[\\N[2]]Es el cuerpo de Mika..."]},
                        {"c": 401, "i": 1, "p": ["Ya no puedo hacer nada."]},
                        {"c": 0, "i": 1, "p": []},
                        {"c": 412, "i": 0, "p": []},
                        {"c": 0, "i": 0, "p": []},
                    ]
                    break

        with open(os.path.join(out_data, f"map_{mid:03d}.json"), "w", encoding="utf-8") as f:
            json.dump(map_meta, f, ensure_ascii=False, separators=(",", ":"))
        converted += 1

    # 3) Copiar todos los Graphics a assets/images/ y generar RTP faltantes
    out_img = os.path.join(out_assets, "images")
    cats = (
        ("Animations", "anim_"),
        ("Titles", "title_"),
        ("Characters", "char_"),
        ("Tilesets", "ts_"),
        ("Autotiles", "at_"),
        ("Windowskins", "ws_"),
        ("Gameovers", "go_"),
        ("Pictures", "pic_"),
        ("Icons", "icon_"),
        ("Fogs", "fog_"),
        ("Panoramas", "pan_")
    )
    for sub, prefix in cats:
        src_sub = os.path.join(game_dir, "Graphics", sub)
        if os.path.isdir(src_sub):
            for fn in os.listdir(src_sub):
                if fn.lower().endswith((".png", ".jpg", ".jpeg")):
                    shutil.copy2(os.path.join(src_sub, fn), os.path.join(out_img, prefix + fn))
    make_fallback_rtp_images(out_img)

    # Parche PS4 para el tileset del puzzle de la muñeca (Map 060): cartel localizado "Volver: Pulsa ○ (Círculo)"
    puzzle_ts_b64 = (
        "iVBORw0KGgoAAAANSUhEUgAAAQAAAACACAMAAADTa0c4AAAADFBMVEVTU1N4eHi7u7v////77XmaAAAABHRSTlP/////bShGeQAAAxNJREFUeNrt3Nt2gjAQ"
        "BdD05P//uasKYW5BEMROOLxUNK2dbW5kMKX88+Pnw0chAAEIQAACEIAABCAAAQhAAALcGwC1VswnVZ4sx9/T49YAGV0n0tsA4JYAog10WsDgACI8Wxmg"
        "SixS+pEpZsomAYCOq84HJEzzmR+0YuIP1XwAkPHo+KcophcgXocuVoxHKoBiAeZ67WSmqLB4zMWak2skWQBsIOqBgRA/YAGRsBOcP2nXxFsVkFVDt4A"
        "inj5zsLh4Kvz4z/Ea4Bk6bBdoK0o+AMzdN14DVD8G5AcowXAW9QFNCmGNHwagOwq0gnbijBrMo5AIAHLa050HtIJwxbIDFFkBujPB9krxUEMAwNYJ+M"
        "atoXyNyAqQbUUIBw8CEIAABCAAAQhAAAIQgAAEIAABzj8Ov01uAGBeT8IQADsXd1r4RwguBTALgAcBVPxvC3wHoBPoLoAp/se7HBC4HGAtt7kPQAQNvJ"
        "0u/QrAyi1S2Bd/7zQJQJD1hl8DR9hoEKyuDwPgkiAmmxJ94u9Wgf8IgHaTwONJbPvAkQhA5IIjgDASUwXyAphUaAdADmqI2kByAOheP2gCOmE8EkA07N"
        "lOUAp0bgoZG2CZ5/k768YYBTQAAoA2GHQAks8D9Hm1fYC8WUK0hDrOTFD37+h1grXfCSa/FlBBbJkKI/jdhFeDH1oOyrMe8EmBFCtCXBPkqjDzAgQgAAE"
        "IQAACEIAABCAAAQhwR4DOzimvneALnfPdyWsBYPaNwOZAQoCaDsDtG9FywG8A4JSv0F8KAJEdf37421d+gmhPqQJXL4svKR8VGaZvy8sUB1pSUABA51Z"
        "zAtiUDpZcSGsdiAGq+U41sgFgBQCidiAGQNs3JSUA1gGKTRB7AHcv0UgAsO16PIByMkDGPmB1FNjbBNICqHmAAdCdYJV7KblOMN8wCLNBSPUAxewVs8wX"
        "/TCYD6Co+v28FrAARe+yg/5ECAmvBXDmboEZL4bKaVuAJb0c5oIIAQhAAAIQgAAEIAABCEAAAhCAAAQgAAEIQAACEIAABCAAAQgwKsAvek1dSLehWOsAAAA"
        "ASUVORK5CYII="
    )
    with open(os.path.join(out_img, "ts_puzzle_e.png"), "wb") as f:
        f.write(base64.b64decode(puzzle_ts_b64))

    # 4) Convertir Audio WAV (8-bit y 16-bit) a PCM 48000 Hz 16-bit estereo
    out_aud = os.path.join(out_assets, "audio")
    for sub, prefix in (("BGM", "bgm_"), ("SE", "se_")):
        src_sub = os.path.join(game_dir, "Audio", sub)
        if os.path.isdir(src_sub):
            for fn in os.listdir(src_sub):
                if fn.lower().endswith(".wav"):
                    in_p = os.path.join(src_sub, fn)
                    out_p = os.path.join(out_aud, prefix + os.path.splitext(fn.lower())[0] + ".pcm")
                    try:
                        with wave.open(in_p, "rb") as w:
                            nch = w.getnchannels()
                            sw = w.getsampwidth()
                            fr = w.getframerate()
                            nfr = w.getnframes()
                            raw = w.readframes(nfr)
                        if nfr <= 0:
                            continue
                        if sw == 2:
                            samples = struct.unpack(f"<{nfr * nch}h", raw)
                        elif sw == 1:
                            # 8-bit unsigned PCM (0..255 -> -32768..32767)
                            samples = [((b - 128) << 8) for b in raw]
                        else:
                            continue
                        out_frames = int(nfr * 48000 / fr)
                        out_buf = bytearray(out_frames * 4)
                        for i in range(out_frames):
                            src_idx = min(int(i * fr / 48000), nfr - 1)
                            if nch == 1:
                                l = r = samples[src_idx]
                            else:
                                l = samples[src_idx * nch]
                                r = samples[src_idx * nch + 1]
                            struct.pack_into("<hh", out_buf, i * 4, l, r)
                        with open(out_p, "wb") as fo:
                            fo.write(out_buf)
                    except Exception as e:
                        print("Warn audio:", fn, e)

    # Mapear y sintetizar los efectos de sonido RTP estandar que usa Ao Oni
    rtp_map = {
        "se_024-door01.pcm": "se_a5_05142.pcm",
        "se_025-door02.pcm": "se_a5_04107.pcm",
        "se_027-door04.pcm": "se_kachi19.pcm",
        "se_028-door05.pcm": "se_kachi19.pcm",
        "se_032-switch01.pcm": "se_kachi19.pcm",
        "se_033-switch02.pcm": "se_kachi19.pcm",
        "se_034-switch03.pcm": "se_kachi19.pcm",
        "se_035-switch04.pcm": "se_kachi19.pcm",
        "se_045-push01.pcm": "se_sha02.pcm",
        "se_046-book01.pcm": "se_swing18.pcm",
        "se_047-book02.pcm": "se_swing40_a.pcm",
        "se_052-cannon01.pcm": "se_bosu14.pcm",
        "se_061-thunderclap01.pcm": "se_explosiond@11st.pcm",
        "se_067-animal02.pcm": "se_insect00.pcm",
        "se_090-attack02.pcm": "se_bosu02.pcm",
        "se_093-attack05.pcm": "se_bosu14.pcm",
        "se_104-attack16.pcm": "se_crash23.pcm",
        "se_129-earth01.pcm": "se_pailc@11.pcm",
    }
    for dst_name, src_name in rtp_map.items():
        src_p = os.path.join(out_aud, src_name)
        dst_p = os.path.join(out_aud, dst_name)
        if os.path.exists(src_p) and not os.path.exists(dst_p):
            shutil.copy2(src_p, dst_p)

    # BGS ambientales RTP faltantes (viento, lluvia, temblor y presencia en armarios/jaula)
    def write_bgs_loop(path, samples):
        sr = 48000
        m = int(0.15 * sr)
        n = len(samples) - m
        buf = bytearray(n * 4)
        for i in range(n):
            v = samples[i]
            if i < m:
                alpha = 0.5 - 0.5 * math.cos(math.pi * i / float(m))
                v = v * alpha + samples[n + i] * (1.0 - alpha)
            iv = int(max(-30000, min(30000, v)))
            struct.pack_into("<hh", buf, i * 4, iv, iv)
        with open(path, "wb") as fb:
            fb.write(buf)

    def gen_wind(seed, dur, f1, f2, tone_amp, noise_lp, amp):
        rng = random.Random(seed)
        sr = 48000
        tot = int((dur + 0.15) * sr)
        lp1 = lp2 = hp = prev_n = 0.0
        out = [0.0] * tot
        two_pi = 2.0 * math.pi
        for i in range(tot):
            t = i / float(sr)
            w = rng.uniform(-1.0, 1.0)
            lp1 += noise_lp * (w - lp1)
            lp2 += (noise_lp * 0.45) * (lp1 - lp2)
            env = 0.62 + 0.28 * math.sin(two_pi * 0.35 * t) + 0.10 * math.cos(two_pi * 0.70 * t)
            whistle = tone_amp * (math.sin(two_pi * f1 * t) * 0.65 + math.sin(two_pi * f2 * t) * 0.35) * env
            hp = 0.995 * (hp + (lp2 - prev_n))
            prev_n = lp2
            out[i] = amp * (lp2 * env * 2.4 + whistle) + 0.0 * hp
        return out

    def gen_rain(seed, dur, hiss_lp, rumble_amp, drop_prob, amp):
        rng = random.Random(seed)
        sr = 48000
        tot = int((dur + 0.15) * sr)
        lp = bp = br = drop = 0.0
        out = [0.0] * tot
        for i in range(tot):
            w = rng.uniform(-1.0, 1.0)
            lp += hiss_lp * (w - lp)
            bp = 0.94 * bp + 0.18 * (w - lp)
            br += 0.008 * (w - br)
            if rng.random() < drop_prob:
                drop = rng.uniform(0.35, 0.75) * (1.0 if rng.random() < 0.5 else -1.0)
            drop *= 0.985
            out[i] = amp * (lp * 0.85 + bp * 0.45 + br * rumble_amp + drop * 0.25)
        return out

    def gen_quake(seed, dur, amp):
        rng = random.Random(seed)
        sr = 48000
        tot = int((dur + 0.15) * sr)
        lp1 = lp2 = 0.0
        out = [0.0] * tot
        two_pi = 2.0 * math.pi
        for i in range(tot):
            t = i / float(sr)
            w = rng.uniform(-1.0, 1.0)
            lp1 += 0.012 * (w - lp1)
            lp2 += 0.018 * (lp1 - lp2)
            sub = math.sin(two_pi * 42.0 * t) * 0.55 + math.sin(two_pi * 63.0 * t) * 0.30
            lfo = 0.75 + 0.25 * math.sin(two_pi * 1.0 * t)
            out[i] = amp * lfo * (lp2 * 4.2 + sub * 0.45)
        return out

    def gen_creature_bgs(seed, dur, amp):
        """Latido grave + respiracion tenebrosa sutil en bucle continuo para 017-Creature01."""
        rng = random.Random(seed)
        sr = 48000
        tot = int((dur + 0.15) * sr)
        lp = breath = 0.0
        out = [0.0] * tot
        two_pi = 2.0 * math.pi
        for i in range(tot):
            t = i / float(sr)
            cyc = t % 1.0
            # Doble latido grave amortiguado (lub-dub) cada 1.0s
            hb1 = math.exp(-((cyc - 0.12) * 18.0) ** 2) * math.sin(two_pi * 52.0 * cyc)
            hb2 = 0.72 * math.exp(-((cyc - 0.32) * 20.0) ** 2) * math.sin(two_pi * 44.0 * cyc)
            # Respiracion de cueva/monstruo muy suave de fondo (0.5 Hz)
            w = rng.uniform(-1.0, 1.0)
            lp += 0.018 * (w - lp)
            breath += 0.012 * (lp - breath)
            inhale = 0.45 + 0.55 * (0.5 - 0.5 * math.cos(two_pi * 0.5 * t))
            drone = math.sin(two_pi * 64.0 * t) * 0.12
            out[i] = amp * ((hb1 + hb2) * 0.65 + breath * inhale * 2.2 + drone * inhale)
        return out

    bgs_specs = [
        ("bgs_001-wind01.pcm",     lambda: gen_wind(101, 3.0, 185.0, 278.0, 0.08, 0.028, 3200.0)),
        ("bgs_003-wind03.pcm",     lambda: gen_wind(103, 3.0, 132.0, 198.0, 0.11, 0.022, 3400.0)),
        ("bgs_005-rain01.pcm",     lambda: gen_rain(105, 3.0, 0.14, 1.8, 0.0018, 2800.0)),
        ("bgs_006-rain02.pcm",     lambda: gen_rain(106, 3.0, 0.18, 3.0, 0.0026, 3500.0)),
        ("bgs_015-quake01.pcm",    lambda: gen_quake(115, 3.0, 3800.0)),
        ("bgs_017-creature01.pcm", lambda: gen_creature_bgs(117, 4.0, 4200.0)),
    ]
    for bfn, bgen in bgs_specs:
        bp = os.path.join(out_aud, bfn)
        write_bgs_loop(bp, bgen())

    # Sonidos del sistema de menu (cursor, confirmar, cancelar, buzzer)
    synth_pcm(os.path.join(out_aud, "se_001-system01.pcm"), 0.055,
              lambda t, d: 9000 * math.sin(2 * math.pi * 880 * t) * (1.0 - t / d))
    synth_pcm(os.path.join(out_aud, "se_002-system02.pcm"), 0.095,
              lambda t, d: 11000 * math.sin(2 * math.pi * (1046 if t < 0.04 else 1318) * t) * (1.0 - t / d))
    synth_pcm(os.path.join(out_aud, "se_003-system03.pcm"), 0.085,
              lambda t, d: 10000 * math.sin(2 * math.pi * (784 if t < 0.04 else 587) * t) * (1.0 - t / d))
    synth_pcm(os.path.join(out_aud, "se_004-system04.pcm"), 0.14,
              lambda t, d: 12000 * (1 if math.sin(2 * math.pi * 160 * t) >= 0 else -1) * (1.0 - t / d))

    print(f"Exportados {converted} mapas completos, game_db.json, imagenes (+RTP) y audio completo a {out_assets}")

if __name__ == "__main__":
    main()
