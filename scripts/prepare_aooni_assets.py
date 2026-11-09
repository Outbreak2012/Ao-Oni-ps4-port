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

def make_bird_charset(path):
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
    make_bird_charset(os.path.join(out_img, "char_166-Small08.png"))

    # Copiar equivalentes del propio juego para puertas/switches/cuevas RTP
    door_src = os.path.join(out_img, "char_172-Door03a.png")
    if os.path.exists(door_src):
        shutil.copy2(door_src, os.path.join(out_img, "char_173-Door04.png"))
    sw_src = os.path.join(out_img, "char_akari_switch2.png")
    if os.path.exists(sw_src):
        shutil.copy2(sw_src, os.path.join(out_img, "char_179-Switch02.png"))
    npc_src = os.path.join(out_img, "char_hirosi.png")
    if os.path.exists(npc_src):
        shutil.copy2(npc_src, os.path.join(out_img, "char_148-Storekeeper02.png"))
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
        map_meta = {
            "id": mid,
            "autoplay_bgm": bool(m.get("@autoplay_bgm", False)),
            "bgm": bgm_obj if isinstance(bgm_obj, dict) else {},
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
        "bgs_017-creature01.pcm": "se_semaru.pcm",
    }
    for dst_name, src_name in rtp_map.items():
        src_p = os.path.join(out_aud, src_name)
        dst_p = os.path.join(out_aud, dst_name)
        if os.path.exists(src_p) and not os.path.exists(dst_p):
            shutil.copy2(src_p, dst_p)

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
