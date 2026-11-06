#!/usr/bin/env python3
"""
Exporta el juego completo de Ao Oni (Data/*.rxdata, Graphics/*, Audio/*)
al formato binario /app0/assets/misc/game_db.bin y map_XXX.bin para el
interprete nativo de RPG Maker XP en PS4 (src/aooni_ps4).
"""
import os
import sys
import glob
import json
import shutil
import struct
import wave
from extract_rxdata import load_rxdata

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
                "desc": str(it.get("@description", ""))
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

        # Guardar geometria y pasabilidad en map_XXX.bin
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

        # Guardar todos los eventos y paginas completas en map_XXX.json
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
        with open(os.path.join(out_data, f"map_{mid:03d}.json"), "w", encoding="utf-8") as f:
            json.dump(map_meta, f, ensure_ascii=False, separators=(",", ":"))
        converted += 1

    # 3) Copiar todos los Graphics a assets/images/ (con prefijo de categoria)
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

    # 4) Convertir Audio WAV (BGM y SE) a PCM 48000 Hz 16-bit estereo para sceAudioOut
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
                        if sw != 2 or nfr <= 0:
                            continue
                        samples = struct.unpack(f"<{nfr * nch}h", raw)
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

    print(f"Exportados {converted} mapas completos, game_db.json, imagenes y audio a {out_assets}")

if __name__ == "__main__":
    main()
