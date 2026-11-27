import hashlib, os, subprocess, xml.etree.ElementTree as ET

HOME = os.path.expanduser("~")
XDK = os.path.join(HOME, "xdk-full", "XDK")
BIN = os.path.join(XDK, "bin", "win32")
INC = os.path.join(XDK, "include", "xbox")
LIB = os.path.join(XDK, "lib", "xbox")

def wpath(p):
    return "Z:" + os.path.abspath(p).replace("/", "\\")

ENV = os.environ.copy()
ENV["WINEDEBUG"] = "-all"
ENV["WINEPREFIX"] = os.path.join(HOME, ".wine32")
ENV["INCLUDE"] = wpath(INC)
ENV["LIB"] = wpath(LIB)

OUT_DIR = os.path.abspath("Release")
os.makedirs(OUT_DIR, exist_ok=True)
os.makedirs(os.path.join(OUT_DIR, "Media", "Fonts"), exist_ok=True)

ALL_INCS = [".", "xiso extract", "user interface", "7zip", "Common", "SSL", "updater", "miniz"]

PROJECTS = [
    ("7zip/7z decompress.vcxproj", "7z decompress.lib", ["NDEBUG", "_XBOX", "_LIB"], ALL_INCS, list()),
    ("miniz/miniz.vcxproj", "miniz.lib", ["NDEBUG", "_XBOX", "_LIB"], ALL_INCS, list()),
    ("user interface/user interface.vcxproj", "user interface.lib", ["NDEBUG", "_XBOX", "_LIB"], ALL_INCS, list()),
    ("xiso extract/xiso extract.vcxproj", "xiso extract.lib", ["NDEBUG", "_XBOX", "_LIB"], ALL_INCS, ["/TP"]),
    ("Common/AtgFramework2010.vcxproj", "AtgFramework.lib", ["NDEBUG", "_XBOX", "XBOX", "_XBOX_CRT_DEPRECATE_INSECURE"], ALL_INCS, list()),
    ("updater/updater.vcxproj", "updater.lib", ["NDEBUG", "_XBOX", "_LIB"], ALL_INCS, list()),
]

def resolve_case(path):
    if os.path.exists(path): return path
    d, f = os.path.split(path)
    if os.path.isdir(d):
        for entry in os.listdir(d):
            if entry.lower() == f.lower():
                return os.path.join(d, entry)
    return path

def run_wine(exe, args):
    cmd = ["wine", os.path.join(BIN, exe)] + args
    res = subprocess.run(cmd, env=ENV)
    if res.returncode != 0:
        raise SystemExit(f"Failed: {' '.join(cmd)}")

def get_sources(vcxproj):
    root = ET.parse(vcxproj).getroot()
    proj_dir = os.path.dirname(os.path.abspath(vcxproj))
    srcs = list()
    for e in root.iter():
        if e.tag.endswith("ClCompile") and "Include" in e.attrib:
            rel = e.attrib["Include"].replace("\\", "/")
            full = resolve_case(os.path.join(proj_dir, rel))
            file_flags = list()
            for c in e:
                if c.tag.endswith("CompileAs"):
                    if c.text == "CompileAsCpp" or rel.endswith("decompress7z.c"): file_flags.append("/TP")
                    elif c.text == "CompileAsC": file_flags.append("/TC")
            srcs.append((full, file_flags))
    return srcs

def compile_objs(srcs, obj_dir, defines, inc_dirs, extra_flags=None, use_pch=None):
    os.makedirs(obj_dir, exist_ok=True)
    objs = list()
    base_flags = ["/nologo", "/c", "/O2", "/MT", "/EHsc-", "/GR-", "/GS-", "/Gy", "/W3"] + (extra_flags or list())
    for d in defines:
        base_flags += ["/D", d]
    for i in inc_dirs:
        base_flags += ["/I", wpath(i)]

    if use_pch:
        pch_cpp, pch_h, pch_out = use_pch
        pch_obj = os.path.join(obj_dir, "stdafx.obj")
        if not os.path.exists(pch_obj) or os.path.getmtime(pch_cpp) > os.path.getmtime(pch_obj):
            print(f"  [PCH] {os.path.basename(pch_cpp)}")
            run_wine("cl.exe", base_flags + [f"/Yc{pch_h}", f"/Fp{wpath(pch_out)}", f"/Fo{wpath(pch_obj)}", wpath(pch_cpp)])
        objs.append(wpath(pch_obj))
        base_flags += [f"/Yu{pch_h}", f"/Fp{wpath(pch_out)}"]

    for s, f_flags in srcs:
        if use_pch and os.path.abspath(s) == os.path.abspath(use_pch[0]):
            continue
        rel_hash = hashlib.md5(os.path.relpath(s).encode()).hexdigest()[:8]
        base_name = os.path.splitext(os.path.basename(s))[0]
        obj = os.path.join(obj_dir, f"{rel_hash}_{base_name}.obj")
        objs.append(wpath(obj))
        if os.path.exists(obj) and os.path.getmtime(s) <= os.path.getmtime(obj):
            continue
        print(f"  [CC]  {os.path.relpath(s)}")
        run_wine("cl.exe", base_flags + f_flags + [f"/Fo{wpath(obj)}", wpath(s)])
    return objs

# 1. Bundle embedded font resource (embed:\font)
font_rdf = os.path.abspath("Media/Fonts/font.rdf")
font_xpr = os.path.join(OUT_DIR, "Media", "Fonts", "font.xpr")
print("=== Bundling font.xpr ===")
run_wine("Bundler.exe", [wpath(font_rdf), "-o", wpath(font_xpr)])

# 2. Build static libraries
for vcxproj, lib_name, defs, incs, extra in PROJECTS:
    print(f"=== Building {lib_name} ===")
    srcs = get_sources(vcxproj)
    obj_dir = os.path.join(OUT_DIR, "obj_" + lib_name.replace(" ", "_").replace(".lib", ""))
    pch = ("Common/stdafx.cpp", "stdafx.h", os.path.join(obj_dir, "AtgFramework.pch")) if "AtgFramework" in lib_name else None
    objs = compile_objs(srcs, obj_dir, defs, incs, extra_flags=extra, use_pch=pch)
    run_wine("lib.exe", ["/nologo", f"/OUT:{wpath(os.path.join(OUT_DIR, lib_name))}"] + objs)

# 3. Link & pack X-Store.xex with embedded font section and TitleID 95126000
print("=== Building X-Store.xex ===")
main_objs = compile_objs(get_sources("XboxTLS2.vcxproj"), os.path.join(OUT_DIR, "obj_main"), ["NDEBUG", "_XBOX"], ALL_INCS)
libs = [
    "xapilib.lib", "d3d9.lib", "d3dx9.lib", "xgraphics.lib", "xboxkrnl.lib",
    "xnet.lib", "xaudio2.lib", "xact3.lib", "x3daudio.lib", "xmcore.lib",
    "vcomp.lib", "xuihtml.lib", "xuirun.lib", "xuirender.lib", "xuivideo.lib",
    "xmedia2.lib",
    wpath(os.path.join(OUT_DIR, "7z decompress.lib")),
    wpath(os.path.join(OUT_DIR, "miniz.lib")),
    wpath(os.path.join(OUT_DIR, "user interface.lib")),
    wpath(os.path.join(OUT_DIR, "xiso extract.lib")),
    wpath(os.path.join(OUT_DIR, "AtgFramework.lib")),
    wpath(os.path.join(OUT_DIR, "updater.lib")),
]
exe_out = os.path.join(OUT_DIR, "XboxTLS.exe")
xex_out = os.path.join(OUT_DIR, "X-Store.xex")
run_wine("link.exe", ["/nologo", "/INCREMENTAL:NO", "/DEBUG:NONE", "/OPT:REF", "/OPT:ICF", "/ENTRY:mainCRTStartup", "/SUBSYSTEM:XBOX", f"/OUT:{wpath(exe_out)}"] + main_objs + libs)
run_wine("imagexex.exe", [
    "/nologo",
    "/TITLEID:95126000",
    f"/SECTION:font={wpath(font_xpr)}",
    f"/OUT:{wpath(xex_out)}",
    wpath(exe_out)
])
print(f"SUCCESS! Built {xex_out}")
