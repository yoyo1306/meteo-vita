#!/usr/bin/env python3
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = root / "build" / "meteo_vita.vpk_param.sfo"
data = open(p, "rb").read()
magic, ver, keyofs, valofs, count = struct.unpack_from("<IIIII", data, 0)
print("keyofs", keyofs, "valofs", valofs, "count", count)
off = 20
for i in range(count):
    nameofs, alignment, typ, valsize, totalsize, dataofs = struct.unpack_from("<HBBIII", data, off)
    off += 16
    name = data[keyofs + nameofs :].split(b"\0", 1)[0].decode()
    if typ == 4:
        val = struct.unpack_from("<I", data, valofs + dataofs)[0]
        print("%s: 0x%08X" % (name, val))
    else:
        s = data[valofs + dataofs : valofs + dataofs + valsize].split(b"\0", 1)[0].decode(errors="replace")
        print("%s: %r" % (name, s))
