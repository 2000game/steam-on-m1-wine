#!/usr/bin/env python3
"""Rewrite the Info.plist embedded in a Mach-O (__TEXT,__info_plist) in place.

    set-loader-plist.py <binary> KEY=VALUE ...

VALUE "true"/"false" becomes a plist boolean, anything else a string. The new
plist is written without indentation and padded with spaces to the old
section size, because the section cannot grow. Re-sign the binary afterwards
(codesign -f -s -). Prints "unchanged" if every key already has its value.
"""
import plistlib
import struct
import sys

LC_SEGMENT_64 = 0x19
MH_MAGIC_64 = 0xFEEDFACF


def find_section(data, segname, sectname):
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", data, 0)
    if magic != MH_MAGIC_64:
        sys.exit("not a thin 64-bit Mach-O")
    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == LC_SEGMENT_64:
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            sect = off + 72
            for _ in range(nsects):
                sname = data[sect:sect + 16].rstrip(b"\0").decode()
                gname = data[sect + 16:sect + 32].rstrip(b"\0").decode()
                size, fileoff = struct.unpack_from("<QI", data, sect + 40)
                if (gname, sname) == (segname, sectname):
                    return fileoff, size
                sect += 80
        off += cmdsize
    sys.exit(f"no ({segname},{sectname}) section")


def compact_xml(plist):
    # plistlib indents with tabs; drop them, the section size is fixed.
    lines = plistlib.dumps(plist, sort_keys=False).decode().splitlines()
    return "".join(line.strip() for line in lines).encode()


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    path, pairs = sys.argv[1], sys.argv[2:]
    data = bytearray(open(path, "rb").read())
    fileoff, size = find_section(data, "__TEXT", "__info_plist")
    old = bytes(data[fileoff:fileoff + size])
    plist = plistlib.loads(old.rstrip(b"\0 \n"))

    changed = False
    for pair in pairs:
        key, _, value = pair.partition("=")
        value = {"true": True, "false": False}.get(value, value)
        if plist.get(key) != value:
            plist[key] = value
            changed = True
    if not changed:
        print("unchanged")
        return

    new = compact_xml(plist)
    if len(new) > size:
        sys.exit(f"new plist is {len(new)} bytes, section holds {size}")
    data[fileoff:fileoff + size] = new + b" " * (size - len(new))
    open(path, "wb").write(data)
    print(f"written ({len(new)} of {size} bytes)")


if __name__ == "__main__":
    main()
