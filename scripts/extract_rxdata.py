#!/usr/bin/env python3
"""
Extrae y convierte los archivos Ruby Marshal (.rxdata) de RPG Maker XP (Ao Oni)
a archivos JSON y scripts Ruby legibles en una carpeta de salida.
No sube nada al repositorio si la salida apunta a una carpeta ignorada.
"""
import os
import sys
import json
import zlib
import struct

class RubyMarshalReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.symbols = []
        self.objects = []

    def read_byte(self) -> int:
        b = self.data[self.pos]
        self.pos += 1
        return b

    def read_bytes(self, n: int) -> bytes:
        b = self.data[self.pos:self.pos + n]
        self.pos += n
        return b

    def read_fixnum(self) -> int:
        c = struct.unpack('b', self.read_bytes(1))[0]
        if c == 0:
            return 0
        if c > 0:
            if c > 4:
                return c - 5
            res = 0
            for i in range(c):
                res |= self.read_byte() << (8 * i)
            return res
        else:
            if c < -4:
                return c + 5
            c = -c
            res = -1
            for i in range(c):
                res &= ~(0xff << (8 * i))
                res |= self.read_byte() << (8 * i)
            return res

    def read_value(self):
        code = chr(self.read_byte())
        if code == '0':
            return None
        if code == 'T':
            return True
        if code == 'F':
            return False
        if code == 'i':
            return self.read_fixnum()
        if code == ':':
            n = self.read_fixnum()
            sym = self.read_bytes(n).decode('utf-8', errors='replace')
            self.symbols.append(sym)
            return sym
        if code == ';':
            idx = self.read_fixnum()
            return self.symbols[idx] if idx < len(self.symbols) else f":sym_{idx}"
        if code == '@':
            idx = self.read_fixnum()
            return self.objects[idx] if idx < len(self.objects) else f"@obj_{idx}"
        if code == 'I':
            val = self.read_value()
            num_ivars = self.read_fixnum()
            for _ in range(num_ivars):
                _k = self.read_value()
                _v = self.read_value()
            return val
        if code == '"':
            n = self.read_fixnum()
            raw = self.read_bytes(n)
            s = raw.decode('utf-8', errors='replace')
            self.objects.append(s)
            return s
        if code == '[':
            n = self.read_fixnum()
            arr = []
            self.objects.append(arr)
            for _ in range(n):
                arr.append(self.read_value())
            return arr
        if code == '{':
            n = self.read_fixnum()
            d = {}
            self.objects.append(d)
            for _ in range(n):
                k = self.read_value()
                v = self.read_value()
                d[str(k)] = v
            return d
        if code == 'o':
            cls_name = self.read_value()
            obj = {"__class__": cls_name}
            self.objects.append(obj)
            n = self.read_fixnum()
            for _ in range(n):
                k = self.read_value()
                v = self.read_value()
                obj[str(k)] = v
            return obj
        if code == 'u':
            cls_name = self.read_value()
            n = self.read_fixnum()
            raw = self.read_bytes(n)
            obj = {"__class__": cls_name}
            if cls_name == "Table" and len(raw) >= 20:
                dim, xsize, ysize, zsize, total = struct.unpack('<5I', raw[:20])
                elems = list(struct.unpack(f'<{total}h', raw[20:20 + total * 2]))
                obj.update({"dim": dim, "xsize": xsize, "ysize": ysize, "zsize": zsize, "data": elems})
            elif cls_name in ("Color", "Tone") and len(raw) >= 32:
                r, g, b, a = struct.unpack('<4d', raw[:32])
                obj.update({"r": r, "g": g, "b": b, "a": a})
            else:
                obj["hex"] = raw.hex()
            self.objects.append(obj)
            return obj
        if code == 'f':
            n = self.read_fixnum()
            s = self.read_bytes(n).decode('ascii', errors='replace')
            self.objects.append(s)
            return s
        raise ValueError(f"Unsupported marshal code {code!r} at pos {self.pos - 1}")

def load_rxdata(path: str):
    data = open(path, 'rb').read()
    assert data[:2] == b'\x04\x08', f"Invalid header in {path}"
    reader = RubyMarshalReader(data[2:])
    return reader.read_value()

if __name__ == '__main__':
    game_dir = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/Users/DELL/Downloads/Proyecto/Ao Oni"
    sys_obj = load_rxdata(os.path.join(game_dir, "Data", "System.rxdata"))
    print("System keys:", list(sys_obj.keys()))
    print("Game title:", sys_obj.get("@title_name"), "Start map:", sys_obj.get("@start_map_id"),
          "Start X/Y:", sys_obj.get("@start_x"), sys_obj.get("@start_y"))
    m1 = load_rxdata(os.path.join(game_dir, "Data", "Map001.rxdata"))
    print("Map001 size:", m1.get("@width"), "x", m1.get("@height"), "tileset_id:", m1.get("@tileset_id"), "events:", len(m1.get("@events", {})))
