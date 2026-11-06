#!/usr/bin/env python3
"""
Convierte los mapas de Ao Oni (Data/Map*.rxdata y Data/Tilesets.rxdata)
al formato binario compacto /app0/assets/data/map_XXX.bin que lee src/aooni_ps4,
y copia los PNG necesarios de Graphics/ a src/aooni_ps4/assets/Graphics/.
"""
import os
import sys
import glob
import shutil
import struct
from extract_rxdata import load_rxdata

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

    # 1) Cargar Tilesets.rxdata
    tilesets_raw = load_rxdata(os.path.join(game_dir, "Data", "Tilesets.rxdata"))
    tilesets = {}
    for t in tilesets_raw:
        if not t:
            continue
        tid = t.get("@id", 0)
        tname = t.get("@tileset_name", "")
        passages = t.get("@passages", {}).get("data", [])
        tilesets[tid] = (tname, passages)

    # 2) Convertir cada MapXXX.rxdata a map_XXX.bin
    map_files = sorted(glob.glob(os.path.join(game_dir, "Data", "Map[0-9][0-9][0-9].rxdata")))
    converted = 0
    for mf in map_files:
        base = os.path.basename(mf)
        mid = int(base[3:6])
        m = load_rxdata(mf)
        w = int(m.get("@width", 20))
        h = int(m.get("@height", 15))
        ts_id = int(m.get("@tileset_id", 1))
        ts_name, passages = tilesets.get(ts_id, ("", []))
        tbl = m.get("@data", {}).get("data", [0] * (w * h * 3))
        if len(tbl) < w * h * 3:
            tbl = list(tbl) + [0] * (w * h * 3 - len(tbl))

        # Extraer eventos con comando 201 (Transfer Player) o grafico de personaje
        events_out = []
        ev_dict = m.get("@events", {})
        for _, ev in ev_dict.items():
            if not isinstance(ev, dict):
                continue
            ex = int(ev.get("@x", 0))
            ey = int(ev.get("@y", 0))
            pages = ev.get("@pages", [])
            if not pages:
                continue
            p0 = pages[0]
            gfx = p0.get("@graphic", {})
            cname = str(gfx.get("@character_name", ""))[:63]
            cdir = int(gfx.get("@direction", 2))
            t_map, t_x, t_y, t_dir = 0, 0, 0, 0
            for cmd in p0.get("@list", []):
                if isinstance(cmd, dict) and cmd.get("@code") == 201:
                    params = cmd.get("@parameters", [])
                    if len(params) >= 4 and params[0] == 0:
                        t_map = int(params[1])
                        t_x = int(params[2])
                        t_y = int(params[3])
                        t_dir = int(params[4]) if len(params) >= 5 else 0
                        break
            if t_map > 0 or cname:
                events_out.append((ex, ey, t_map, t_x, t_y, t_dir, cname, cdir))

        bin_path = os.path.join(out_data, f"map_{mid:03d}.bin")
        with open(bin_path, "wb") as f:
            ts_bytes = ts_name.encode("utf-8")[:63].ljust(64, b"\x00")
            pass_bytes = bytes([p & 0xFF for p in passages[:4096]])
            f.write(struct.pack("<4I", 0x4D504F41, w, h, ts_id))
            f.write(ts_bytes)
            f.write(struct.pack(f"<{w * h * 3}h", *tbl[:w * h * 3]))
            f.write(struct.pack("<I", len(pass_bytes)))
            f.write(pass_bytes)
            f.write(struct.pack("<I", len(events_out)))
            for ex, ey, tm, tx, ty, td, cn, cd in events_out:
                f.write(struct.pack("<6i", ex, ey, tm, tx, ty, td))
                f.write(cn.encode("utf-8")[:63].ljust(64, b"\x00"))
                f.write(struct.pack("<i", cd))
        converted += 1

    # 3) Copiar Graphics necesarios a assets/images/ (con prefijo de categoria para que queden en 1 nivel)
    out_img = os.path.join(out_assets, "images")
    for sub, prefix in (("Titles", "title_"), ("Characters", "char_"), ("Tilesets", "ts_"), ("Windowskins", "ws_"), ("Gameovers", "go_")):
        src_sub = os.path.join(game_dir, "Graphics", sub)
        if os.path.isdir(src_sub):
            for fn in os.listdir(src_sub):
                if fn.lower().endswith(".png"):
                    shutil.copy2(os.path.join(src_sub, fn), os.path.join(out_img, prefix + fn))

    print(f"Exportados {converted} mapas y graficos a {out_assets}")

if __name__ == "__main__":
    main()
