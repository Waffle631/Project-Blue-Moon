"""Diagnostic: name the import bound to a GOT slot (guest image offsets).

Reads out/boot.bin (G3PROBE2, eboot only) or, with --linked, out/boot-linked.bin (G3PROBE5:
eboot + linked modules, layout written by scripts/link_modules.py). In the linked image, libc.prx
slots are image offsets too (module base + offset in the module)."""
import re, struct, sys
from pathlib import Path
here = Path(__file__).resolve().parent.parent  # repository root
args = sys.argv[1:]
linked = '--linked' in args
args = [a for a in args if a != '--linked']
data = (here / ('out/boot-linked.bin' if linked else 'out/boot.bin')).read_bytes()
size, entry, ns, nr, ni, caps = struct.unpack_from('<QQQQQQ', data, 8)
pos = 56
if linked:
    pos += 8 + 32  # procparam, eboot TLS
    pos += 8 + struct.unpack_from('<Q', data, pos)[0] * 56  # module table
    pos += 8 + struct.unpack_from('<Q', data, pos)[0] * 24  # native bindings
pos += ns * 24
names = [data[pos + i * 128:pos + (i + 1) * 128].split(b'\0')[0].decode() for i in range(ni)]
pos += ni * 128
symbols = dict(re.findall(r'\{"([^"]+)","([^"]+)"\}', (here / 'src/import_names.inc').read_text()))
slots = {}
for i in range(nr):
    target, kind, value, addend = struct.unpack_from('<QQqq', data, pos + i * 32)
    if kind:
        slots[target] = names[value]
for arg in args:
    nid = slots.get(int(arg, 16))
    print(arg, nid, symbols.get(nid) if nid else '')
