#!/usr/bin/env python3
"""Load an encrypted, basic-compressed XEX2 the way the SDK's XexModule does and
write the in-memory PE image (based at its load address) to a file.

    python -I tools/xex_image.py assets/default.xex image.bin
"""
import struct
import sys

from Crypto.Cipher import AES

# Public retail XEX2 key, same constant as rexglue-sdk src/system/xex_module.cpp.
RETAIL_KEY = bytes([0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
                    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91])


def load(path):
    d = open(path, "rb").read()
    _, _, pe_off, _, sec_off, n = struct.unpack_from(">4sIIIII", d, 0)
    ffi = None
    for i in range(n):
        key, val = struct.unpack_from(">II", d, 24 + i * 8)
        if key == 0x3FF:
            ffi = val
    load_addr = struct.unpack_from(">I", d, sec_off + 0x110)[0]
    aes_key = d[sec_off + 0x150:sec_off + 0x160]
    info_size, enc, comp = struct.unpack_from(">IHH", d, ffi)
    if comp != 1:
        sys.exit(f"compression type {comp} not supported (basic only)")
    session = AES.new(RETAIL_KEY, AES.MODE_CBC, iv=bytes(16)).decrypt(aes_key)
    blocks = [struct.unpack_from(">II", d, ffi + 8 + i * 8) for i in range((info_size - 8) // 8)]
    data = d[pe_off:pe_off + sum(b[0] for b in blocks)]
    if enc == 1:
        data = AES.new(session, AES.MODE_CBC, iv=bytes(16)).decrypt(data)  # chained across blocks
    out = bytearray()
    p = 0
    for data_size, zero_size in blocks:
        out += data[p:p + data_size] + bytes(zero_size)
        p += data_size
    if out[:2] != b"MZ":
        sys.exit("decryption failed (no MZ header)")
    return load_addr, bytes(out)


def sections(img, base):
    """[(name, start, end, flags)] from the PE section table."""
    pe = struct.unpack_from("<I", img, 0x3C)[0]
    count = struct.unpack_from("<H", img, pe + 6)[0]
    opt_size = struct.unpack_from("<H", img, pe + 20)[0]
    out = []
    for i in range(count):
        o = pe + 24 + opt_size + i * 40
        name = img[o:o + 8].rstrip(b"\0").decode()
        vsize, va = struct.unpack_from("<II", img, o + 8)
        flags = struct.unpack_from("<I", img, o + 36)[0]
        out.append((name, base + va, base + va + vsize, flags))
    return out


if __name__ == "__main__":
    base, img = load(sys.argv[1])
    open(sys.argv[2], "wb").write(img)
    print(f"base 0x{base:08X}, {len(img)} bytes")
    for name, s, e, f in sections(img, base):
        print(f"  {name:8} {s:08X}-{e:08X} flags {f:08X}")
