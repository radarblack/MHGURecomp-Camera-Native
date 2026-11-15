#!/usr/bin/env python3
import struct
import sys

try:
    import pefile
except ImportError:
    sys.exit("this tool needs pefile: pip install pefile")

PORT_REQUIRED = ["fnhook_set", "fnhook_call_original", "hle_register"]
PORT_OPTIONAL = ["fnhook_get", "guest_call", "dispatch_lookup"]


def export_names(path):
    pe = pefile.PE(path, fast_load=True)
    entry = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    if not entry.VirtualAddress:
        return set()
    data = bytes(pe.__data__)
    rva = pe.get_offset_from_rva

    def cstr(offset):
        return data[offset:data.find(b"\0", offset)].decode("latin1")

    head = rva(entry.VirtualAddress)
    fields = struct.unpack_from("<IIHHIIIIIII", data, head)
    count, names_rva = fields[7], fields[9]
    table = rva(names_rva)
    return {cstr(rva(struct.unpack_from("<I", data, table + 4 * i)[0])) for i in range(count)}


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: check_exports.py <mhgurecomp.exe>")
    exe = export_names(sys.argv[1])
    print("%s exports %d names" % (sys.argv[1], len(exe)))
    failures = 0

    def line(kind, name, where):
        print("%-5s %-26s %s" % (kind, name, where))

    for name in PORT_REQUIRED:
        if name in exe:
            line("ok", name, "exe")
        else:
            line("MISS", name, "exe")
            failures += 1
    for name in PORT_OPTIONAL:
        if name in exe:
            line("ok", name, "exe (optional)")
        else:
            print("note  %-26s %s (optional: the module degrades gracefully)" % (name, "exe"))
    if failures:
        print("%d required export(s) missing: the module will decline to load on this build" % failures)
        sys.exit(1)
    print("all required exports present")


if __name__ == "__main__":
    main()
