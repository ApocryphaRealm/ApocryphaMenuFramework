#!/usr/bin/env python3
r"""Off-game check of the SKSE Menu Framework plugin-name stub (2.2.1): reads the BUILT DLL, not the source.

    py -3 tools/smf_stub_check.py [path to !ApocryphaMenuFramework-SMFName.dll ...]

Default: the stub in both build trees (build/relwithdebinfo-se-only and build/relwithdebinfo-17). For each DLL it checks:
  * the file name is not SKSEMenuFramework.dll (the stub never takes SKSE Menu Framework's own name);
  * it exports exactly SKSEPlugin_Load, SKSEPlugin_Query and SKSEPlugin_Version - nothing else (no SMF API);
  * SKSEPlugin_Version (the data SKSE AE / 1.7 reads) declares dataVersion 1, the name "SKSEMenuFramework" and version
    3.14.0 packed (0x030E0000), uses no game structures and needs no Address Library (version independent);
  * it imports nothing from a CommonLib-style game address table (no import of ApocryphaMenuFramework, no XINPUT).
Prints one line per check and "smf_stub_check: N of N passed"; exits 1 on any failure.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
NAME = "!ApocryphaMenuFramework-SMFName.dll"
DEFAULT = [os.path.join(REPO, "build", t, NAME) for t in ("relwithdebinfo-se-only", "relwithdebinfo-17")]

passed = failed = 0


def check(ok, what):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
    print(("PASS " if ok else "FAIL ") + what)


class PE:
    def __init__(self, data):
        self.d = data
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
        nsec, = struct.unpack_from("<H", data, e_lfanew + 6)
        opt_size, = struct.unpack_from("<H", data, e_lfanew + 20)
        opt = e_lfanew + 24
        assert struct.unpack_from("<H", data, opt)[0] == 0x20B, "PE32+ expected"
        self.dirs = [struct.unpack_from("<II", data, opt + 112 + 8 * i) for i in range(16)]
        self.secs = []
        s = opt + opt_size
        for i in range(nsec):
            va_size, va, raw_size, raw = struct.unpack_from("<IIII", data, s + 40 * i + 8)
            self.secs.append((va, max(va_size, raw_size), raw))

    def off(self, rva):
        for va, size, raw in self.secs:
            if va <= rva < va + size:
                return raw + rva - va
        raise ValueError("rva %x outside sections" % rva)

    def cstr(self, rva):
        o = self.off(rva)
        return self.d[o:self.d.index(b"\0", o)].decode("ascii", "replace")

    def exports(self):
        rva, size = self.dirs[0]
        if not rva:
            return {}
        o = self.off(rva)
        nfun, nnames, afun, aname, aord = struct.unpack_from("<IIIII", self.d, o + 20)
        out = {}
        for i in range(nnames):
            name = self.cstr(struct.unpack_from("<I", self.d, self.off(aname) + 4 * i)[0])
            ordi = struct.unpack_from("<H", self.d, self.off(aord) + 2 * i)[0]
            out[name] = struct.unpack_from("<I", self.d, self.off(afun) + 4 * ordi)[0]
        return out

    def imports(self):
        rva, size = self.dirs[1]
        names = []
        if not rva:
            return names
        o = self.off(rva)
        while True:
            ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", self.d, o)
            if not name_rva:
                break
            names.append(self.cstr(name_rva))
            o += 20
        return names


def check_dll(path):
    print("--- %s" % path)
    if not os.path.isfile(path):
        check(False, "built: %s exists" % path)
        return
    check(os.path.basename(path).lower() != "sksemenuframework.dll", "its own file name, not SKSEMenuFramework.dll")
    pe = PE(open(path, "rb").read())
    exp = pe.exports()
    check(sorted(exp) == ["SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version"],
          "exports exactly SKSE's three entry points (got %s)" % ", ".join(sorted(exp)))
    if "SKSEPlugin_Version" in exp:
        o = pe.off(exp["SKSEPlugin_Version"])
        blob = pe.d[o:o + 0x350]
        data_ver, plugin_ver = struct.unpack_from("<II", blob, 0)
        name = blob[8:8 + 256].split(b"\0")[0].decode("ascii", "replace")
        ex, ind = struct.unpack_from("<II", blob, 0x304)
        compat = struct.unpack_from("<16I", blob, 0x30C)
        xse_min, = struct.unpack_from("<I", blob, 0x34C)
        check(data_ver == 1, "SKSEPlugin_Version.dataVersion = 1")
        check(name == "SKSEMenuFramework", "declared name 'SKSEMenuFramework' (got '%s')" % name)
        check(plugin_ver == 0x030E0000, "declared version 3.14.0 = 0x030E0000 = %d (got 0x%08X)" % (0x030E0000, plugin_ver))
        check(ex & 1 == 1, "versionIndependenceEx: no game structures used")
        check(ind & 2 == 2 and ind & 1 == 0, "versionIndependence: address independent, no Address Library needed (got 0x%X)" % ind)
        check(not any(compat) and xse_min == 0, "no runtime list and no minimum SKSE (runtime independent)")
    imps = [i.lower() for i in pe.imports()]
    check(not any("apocrypha" in i or "xinput" in i or "sksemenuframework" in i for i in imps),
          "imports only system DLLs (%s)" % ", ".join(imps))
    check(os.path.isfile(path[:-4] + ".pdb"), "its .pdb is beside it (rule 43)")


def main():
    paths = sys.argv[1:] or DEFAULT
    for p in paths:
        check_dll(p)
    print("smf_stub_check: %d of %d passed" % (passed, passed + failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
