#!/usr/bin/env python3
"""Import a Synwit (华芯微特) CSL SDK into the PlatformIO framework package.

The script is the single place where chip knowledge is *derived*: it reads the
vendor CMSIS pack description (kernel, flash/ram, SVD), unpacks the CSL from the
vendor SDK archive, guards the clock macros of `system_<CHIP>.c` so they can be
overridden from platformio.ini, produces a linker script per device and finally
writes framework-synwit/mcu.json + platform-synwit/boards/<device>.json.

Usage
-----
    python3 tools/import_csl.py                       # import everything
    python3 tools/import_csl.py --only swm181xc swm260xb
    python3 tools/import_csl.py --sdk-root ~/Downloads/华芯微特_new

Run it again after dropping a newer SDK into ~/Downloads: it is idempotent.
"""

import argparse
import json
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
PLATFORM_DIR = os.path.abspath(os.path.join(HERE, ".."))
FRAMEWORK_DIR = os.path.abspath(os.path.join(PLATFORM_DIR, "..", "framework-synwit"))

# --------------------------------------------------------------------------
# where the vendor SDK of each series lives inside the download package
# --------------------------------------------------------------------------
SDK_SOURCES = {
    "SWM166": "05.SWM166/02.SDK/00.标准外设库及参考示例/SWM166_Lib-main_241128.zip",
    "SWM181": "06.SWM181/02.SDK/00.标准外设库及参考示例/SWM181_Lib-240513.zip",
    "SWM190": "07.SWM190(S)/02.SDK/00.标准外设库及参考示例/SWM190_Lib-240513.zip",
    "SWM201": "08.SWM201/02.SDK/00.标准外设库及参考示例/SWM2X1_Lib-240513.zip",
    "SWM211": "09.SWM2X1/02.SDK/00.标准外设库及参考示例/SWM2X1_Lib-240513.zip",
    "SWM221": "14.SWM221/02.SDK/00.标准外设库及参考示例/SWM221_Lib-250610.zip",
    "SWM241": "10.SWM241/02.SDK/00.标准外设库及参考示例/SWM241_Lib-240513.zip",
    "SWM260": "11.SWM260/02.SDK/00.标准外设库及参考示例/SWM260_Lib-240513.zip",
    "SWM320": "12.SWM320(S)/02.SDK/00.标准外设库及参考示例/SWM320_Lib-240513.zip",
    "SWM341": "13.SWM341(S)/02.SDK/00.标准外设库及参考示例/SWM341_Lib",
}

DSP_LIB_320 = {
    "name": "FrameworkSynwitDSP",
    "dir": "CMSIS/DSP_Lib",
    "defines": ["ARM_MATH_CM4"],
    "src_filter": ["+<*>", "-<*.S>", "-<*.txt>"],
}

# --------------------------------------------------------------------------
# per device tweaks that cannot be derived from the pack description
# --------------------------------------------------------------------------
OVERRIDES = {
    # no crystal on most boards -> boot from the internal RC
    "swm341xe": {
        "clock_preset_default": "sys_clk_20mhz",
        "clock_presets_extra": {
            "xtal12m": ["SYS_CLK=SYS_CLK_XTAL", "__HSE=12000000UL"],
            "pll_xtal12m_120m": [
                "SYS_CLK=SYS_CLK_PLL", "SYS_PLL_SRC=SYS_CLK_XTAL",
                "__HSE=12000000UL", "PLL_IN_DIV=3", "PLL_FB_DIV=60",
                "PLL_OUT_DIV=PLL_OUT_DIV8",
            ],
        },
        # vendor CSL: DSP_Src ships the sort functions, DSP_Inc misses arm_sorting.h
        "optional_libraries": {
            "dsp": {
                "name": "FrameworkSynwitDSP",
                "dir": "CMSIS/DSP_Src",
                "include_dirs": ["CMSIS/DSP_Inc"],
                "defines": ["ARM_MATH_CM33"],
                "src_filter": [
                    "+<*>", "-<*.txt>", "-<*.BIN>",
                    "-<TransformFunctions/arm_bitreversal2.S>",
                    "-<SupportFunctions/arm_*.S>",
                    "-<SupportFunctions/arm_*sort*.c>",
                    "-<SupportFunctions/SupportFunctions.c>",
                ],
            },
            "usb_host": {
                "name": "FrameworkSynwitUsbHost",
                "dir": "SWM341_UsbHost_Lib",
                "src_filter": ["+<*.c>"],
            },
        },
    },
    "swm320xc": {"optional_libraries": {"dsp": DSP_LIB_320}},
    "swm320xe": {"optional_libraries": {"dsp": DSP_LIB_320}},

    # default of the vendor SDK is the PLL fed by an external crystal
    "swm166x8": {"clock_preset_default": "sys_clk_12mhz"},
}

PATCH_TAG = "/* >>> PIO adaptation: overridable via board_build.synwit_clock <<< */"

# --------------------------------------------------------------------------
# vendor files that do not compile with GCC as shipped
# (relative path inside the framework package, anchor, inserted text)
# --------------------------------------------------------------------------
GCC_CACHE_CLEAR = b"""
#elif defined ( __GNUC__ )

/* >>> PIO adaptation: GCC port of Cache_Clear(), mirrors the IAR branch <<< */
void Cache_Clear(void)
{
\tFMC->CACHE |= FMC_CACHE_CCLR_Msk;\t// Cache Clear

\t__NOP(); __NOP(); __NOP(); __NOP();
}

#endif
"""

SOURCE_PATCHES = [
    # SWM221_StdPeriph_Driver/SWM221_flash.c only implements Cache_Clear() for
    # ARMCC and IAR, so SystemInit() does not link with GCC.
    ("csl/swm221/SWM221_StdPeriph_Driver/SWM221_flash.c",
     ">>> PIO adaptation: GCC port of Cache_Clear", b"\n#endif",
     GCC_CACHE_CLEAR),
]
GUARDED = ("SYS_CLK", "__HSI", "__LSI", "__HSE", "__LSE",
           "SYS_PLL_SRC", "PLL_IN_DIV", "PLL_FB_DIV", "PLL_OUT_DIV")


def hx(text):
    return int(text, 16)


def parse_pdsc(path):
    """Return {series: {...}} straight from the vendor CMSIS pack description."""
    root = ET.parse(path).getroot()
    out = {}
    for fam in root.find("devices"):
        series = fam.get("Dfamily").replace(" Series", "").strip()
        proc = fam.find("processor")
        comp = fam.find("compile")
        svd = fam.find("debug").get("svd").replace("\\", "/")
        # CSL\<series>\<driver>\<chip>.h  ->  the CSL folder name of the SDK
        csl_dir = comp.get("header").replace("\\", "/").split("/")[1]
        devices = []
        for dev in fam.findall("device"):
            mem = {}
            for m in dev.findall("memory"):
                mem[m.get("id")] = (m.get("start"), hx(m.get("size")))
            devices.append({
                "name": dev.get("Dname"),
                "flash": mem["IROM1"][1],
                "ram": mem["IRAM1"][1],
            })
        out[series] = {
            "core": proc.get("Dcore"),
            "fpu": proc.get("Dfpu"),
            "clock": int(proc.get("Dclock")),
            "svd": svd,
            "csl_dir": csl_dir,
            "devices": sorted(devices, key=lambda d: d["flash"]),
        }
    return out


def extract_csl(source, dest):
    """Copy only the CSL/ subtree out of the vendor archive (or directory)."""
    if os.path.isdir(source):
        src_root = None
        for dp, dn, fn in os.walk(source):
            if os.path.basename(dp).upper() == "CSL":
                src_root = dp
                break
        assert src_root, "no CSL/ folder below %s" % source
        shutil.copytree(src_root, dest, dirs_exist_ok=True)
        return

    z = zipfile.ZipFile(source)

    def name_of(zi):
        if zi.flag_bits & 0x800:
            return zi.filename
        try:  # Chinese archives are usually GBK encoded
            return zi.filename.encode("cp437").decode("gbk")
        except Exception:
            return zi.filename

    for zi in z.infolist():
        parts = name_of(zi).replace("\\", "/").split("/")
        upper = [p.upper() for p in parts]
        if "CSL" not in upper:
            continue
        rel = "/".join(parts[upper.index("CSL") + 1:])
        rel = re.sub(r"[^\x20-\x7e\u4e00-\u9fff/._()-]", "_", rel)
        out = os.path.join(dest, rel)
        if zi.is_dir():
            os.makedirs(out, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with z.open(zi) as src, open(out, "wb") as dst:
            dst.write(src.read())


def guard_clock_macros(path):
    """Wrap the tunable clock macros in #ifndef so build_flags can win."""
    text = io_open(path)
    for name in GUARDED:
        pat = re.compile(r"^([ \t]*)#define[ \t]+%s[ \t]+(.*)$" % re.escape(name),
                         re.M)
        m = None
        for cand in pat.finditer(text):
            m = cand  # last definition wins, as in the original file
        if not m:
            continue
        if "#ifndef %s" % name in text:
            continue  # already patched
        indent, value = m.group(1), m.group(2)
        replacement = "#ifndef %s\n%s#define %s %s\n%s\n#endif" % (
            name, indent, name, value, PATCH_TAG)
        text = text[:m.start()] + replacement + text[m.end():]
    io_write(path, text)


def apply_source_patches(fw):
    """Fix vendor sources that lack a GCC branch (idempotent)."""
    for rel, tag, anchor, insert in SOURCE_PATCHES:
        path = os.path.join(fw, rel)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as fp:
            data = fp.read()
        if tag.encode() in data:
            continue
        pos = data.rfind(anchor)
        assert pos > 0, "anchor %r not found in %s" % (anchor, rel)
        with open(path, "wb") as fp:
            fp.write(data[:pos] + insert + data[pos + len(anchor):])
        print("   patched %s" % os.path.basename(rel))


def io_open(path):
    for enc in ("utf-8", "gbk", "latin-1"):
        try:
            with open(path, "r", encoding=enc) as fp:
                return fp.read()
        except UnicodeDecodeError:
            continue
    raise SystemExit("cannot decode %s" % path)


def io_write(path, text):
    with open(path, "w", encoding="utf-8", newline="") as fp:
        fp.write(text)


def clock_presets(path):
    """Turn '#define SYS_CLK_24MHz 0' style selectors into mcu.json presets."""
    text = io_open(path)
    presets = {}
    for m in re.finditer(r"^#define[ \t]+(SYS_CLK_[A-Za-z0-9_]+)[ \t]+(\d+)",
                         text, re.M):
        name, _value = m.group(1), m.group(2)
        presets[pretty_preset(name)] = ["SYS_CLK=%s" % name]
    # the vendor default, reachable under its own name
    m = re.search(r"^#ifndef SYS_CLK[ \t]*\n[ \t]*#define SYS_CLK[ \t]+(\S+)",
                  text, re.M) or re.search(r"^#define SYS_CLK[ \t]+(\S+)", text, re.M)
    if m and m.group(1) in [v[0].split("=")[1] for v in presets.values()]:
        presets["vendor_default"] = ["SYS_CLK=%s" % m.group(1)]
    return presets


def pretty_preset(macro):
    return "sys_clk_" + re.sub(r"[^a-z0-9]", "", macro[len("SYS_CLK_"):].lower())


def preset_hz(preset):
    """Best effort F_CPU for a preset, e.g. SYS_CLK=SYS_CLK_20MHz -> 20e6."""
    for item in preset:
        m = re.match(r"SYS_CLK=(\d+)(MHz|KHz)$", item.split("=", 1)[1])
        if m:
            mul = 1000000 if m.group(2) == "MHz" else 1000
            return int(m.group(1)) * mul
    return None


def make_ld(template, rom, ram, chip):
    """Clone a vendor linker script and fix up the MEMORY sizes."""
    text = io_open(template)
    m = re.search(r"MEMORY\s*\{(.*?)\}", text, re.S)
    assert m, "no MEMORY block in %s" % template
    block = m.group(1)
    sizes = [rom, ram]
    idx = [0]

    def repl(mo):
        i = idx[0]
        idx[0] += 1
        return "LENGTH = 0x%08X" % sizes[i] if i < len(sizes) else mo.group(0)

    new_block = re.sub(r"LENGTH\s*=\s*0x[0-9a-fA-F]+", repl, block)
    new_block = re.sub(r"/\*[^*]*\*/", "", new_block)  # drop stale vendor notes
    text = text[:m.start(1)] + new_block + text[m.end(1):]
    text = ("/* generated by tools/import_csl.py for %s\n"
            "   section layout taken from the vendor %s */\n"
            % (chip, os.path.basename(template))) + text
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk-root",
                    default=os.path.expanduser("~/Downloads/华芯微特_new"))
    ap.add_argument("--only", nargs="*", default=None)
    ap.add_argument("--framework-dir", default=FRAMEWORK_DIR)
    ap.add_argument("--platform-dir", default=PLATFORM_DIR)
    args = ap.parse_args()

    pdsc = os.path.join(args.sdk_root,
                        "01.开发环境搭建(入门必备)/00.Keil MDK Pack",
                        "Synwit.SWM32_DFP.2.1.4", "Synwit.SWM32_DFP.pdsc")
    gcc_cfg = os.path.join(args.sdk_root, "01.开发环境搭建(入门必备)",
                           "02.GCC Config", "Synwit.SWM32_GCC")
    pack_dir = os.path.dirname(pdsc)

    if not os.path.isfile(pdsc):
        raise SystemExit("vendor pack not found: %s" % pdsc)

    fw = args.framework_dir
    series_db = parse_pdsc(pdsc)
    families = {}

    for series, info in sorted(series_db.items()):
        src_rel = SDK_SOURCES.get(series)
        if not src_rel:
            print("! no SDK mapping for %s, skipped" % series)
            continue
        source = os.path.join(args.sdk_root, src_rel)
        csl_key = info["csl_dir"].lower()
        csl_dst = os.path.join(fw, "csl", csl_key)

        print("== %s (core %s, CSL dir %s)" % (series, info["core"], csl_key))
        if not os.path.isdir(csl_dst):
            os.makedirs(csl_dst, exist_ok=True)
            extract_csl(source, csl_dst)
        else:
            print("   csl/%s already imported" % csl_key)

        driver_dir = "%s_StdPeriph_Driver" % series
        if not os.path.isdir(os.path.join(csl_dst, driver_dir)):
            # SWM2X1 and friends keep the driver under the CSL folder name
            driver_dir = "%s_StdPeriph_Driver" % info["csl_dir"]

        # patch every system_<chip>.c of this CSL tree
        dev_dir = os.path.join(csl_dst, "CMSIS", "DeviceSupport")
        for f in sorted(os.listdir(dev_dir)):
            if f.startswith("system_") and f.endswith(".c"):
                guard_clock_macros(os.path.join(dev_dir, f))
                print("   patched %s" % f)

        # linker script template: vendor file of the series, else the M0 one
        ld_template = os.path.join(gcc_cfg, series, "%s.ld" % series.lower())
        if not os.path.isfile(ld_template):
            ld_template = os.path.join(gcc_cfg, "SWM181", "swm181.ld")
        if not os.path.isfile(ld_template):
            raise SystemExit("no linker script template for %s" % series)

        # SVD
        svd_src = os.path.join(pack_dir, info["svd"])
        svd_name = os.path.basename(info["svd"])
        os.makedirs(os.path.join(fw, "svd"), exist_ok=True)
        if os.path.isfile(svd_src):
            shutil.copyfile(svd_src, os.path.join(fw, "svd", svd_name))

        for dev in info["devices"]:
            key = dev["name"].lower()
            if args.only and key not in args.only:
                continue
            system_c = "system_%s.c" % series
            startup_s = "startup/gcc/startup_%s.s" % series
            for name, extra in (("system", system_c), ("startup", startup_s)):
                p = os.path.join(dev_dir, extra.replace("startup/gcc/",
                                                        "startup/gcc/"))
                if not os.path.isfile(p):
                    raise SystemExit("missing %s for %s" % (extra, key))

            defines = [dev["name"]]
            if info["csl_dir"] == "SWM2X1":
                defines.append("CHIP_%s" % series)

            ld_name = "%s.ld" % key
            os.makedirs(os.path.join(fw, "ldscripts"), exist_ok=True)
            io_write(os.path.join(fw, "ldscripts", ld_name),
                     make_ld(ld_template, dev["flash"], dev["ram"],
                             dev["name"]))

            ov = OVERRIDES.get(key, {})
            entry = {
                "title": "Synwit %s (%s, %dK Flash / %dK RAM)"
                         % (dev["name"], info["core"],
                            dev["flash"] // 1024, dev["ram"] // 1024),
                "series": csl_key,
                "root": "csl/%s" % csl_key,
                "cpu": {"Cortex-M0": "cortex-m0",
                        "Cortex-M0+": "cortex-m0plus",
                        "Cortex-M3": "cortex-m3",
                        "Cortex-M4": "cortex-m4",
                        "Cortex-M33": "cortex-m33"}[info["core"]],
                "float_abi": "soft",
                "include_dirs": [
                    "CMSIS/CoreSupport",
                    "CMSIS/DeviceSupport",
                    driver_dir,
                ],
                "defines": defines,
                "libraries": [
                    {
                        "comment": "startup + SystemInit(): keep it first",
                        "name": "FrameworkSynwitDevice",
                        "dir": "CMSIS/DeviceSupport",
                        "src_filter": ["-<*>",
                                       "+<%s>" % system_c,
                                       "+<%s>" % startup_s],
                    },
                    {
                        "name": "FrameworkSynwitCSP",
                        "dir": driver_dir,
                        "src_filter": ["+<*.c>"],
                    },
                ],
                "ldscript": "ldscripts/%s" % ld_name,
                "svd": "svd/%s" % svd_name,
                "maximum_size": dev["flash"],
                "maximum_ram_size": dev["ram"],
                "clock_presets": clock_presets(os.path.join(dev_dir, system_c)),
            }
            if ov.get("clock_presets_extra"):
                entry["clock_presets"].update(ov["clock_presets_extra"])
            if ov.get("optional_libraries"):
                entry["optional_libraries"] = ov["optional_libraries"]
            if ov.get("clock_preset_default"):
                assert ov["clock_preset_default"] in entry["clock_presets"], \
                    "%s: unknown preset %s" % (key, ov["clock_preset_default"])
                entry["clock_preset_default"] = ov["clock_preset_default"]
            families[key] = entry
            print("   %-10s flash %6d  ram %5d  -> %s" %
                  (key, dev["flash"], dev["ram"], ld_name))

            write_board(os.path.join(args.platform_dir, "boards", key + ".json"),
                        entry, info)

    apply_source_patches(fw)

    mcu_path = os.path.join(fw, "mcu.json")
    old = {}
    if os.path.isfile(mcu_path):
        old = json.load(open(mcu_path, encoding="utf-8"))
    header = old.get("_comment", [
        "Auto-generated by platform-synwit/tools/import_csl.py - edit the",
        "generator (or the SDK) instead of this file, your changes are lost",
        "on the next import.  Keys are lower case device names from the vendor",
        "CMSIS pack (Synwit.SWM32_DFP.pdsc); board_build.mcu takes such a key",
        "or any longer name starting with it (SWM341CET7 -> swm341xe).",
    ])
    out = {"_comment": header, "families": dict(sorted(families.items()))}
    with open(mcu_path, "w", encoding="utf-8") as fp:
        json.dump(out, fp, indent=2, ensure_ascii=False)
        fp.write("\n")
    print("\nwrote %s (%d devices)" % (mcu_path, len(families)))


def write_board(path, entry, info):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    clock = entry.get("clock_preset_default") or "vendor_default"
    hz = preset_hz(entry["clock_presets"].get(clock, [])) or info["clock"]
    board = {
        "build": {
            "cpu": entry["cpu"],
            "f_cpu": "%dL" % hz,
            "mcu": os.path.basename(path)[:-5],
            "float_abi": entry["float_abi"],
            "variant": entry["defines"][0],
        },
        "debug": {
            "default_tools": ["jlink"],
            "jlink_device": entry["defines"][0],
            "onboard_tools": [],
            "svd_path": entry["svd"].split("/")[-1],
        },
        "frameworks": ["synwit"],
        "name": "Generic %s" % entry["defines"][0],
        "upload": {
            "maximum_ram_size": entry["maximum_ram_size"],
            "maximum_size": entry["maximum_size"],
            "offset_address": "0x0",
            "protocol": "custom",
            "protocols": ["jlink", "custom"],
        },
        "url": "https://www.synwit.com",
        "vendor": "Synwit",
    }
    board["build"]["synwit_clock"] = clock
    with open(path, "w", encoding="utf-8") as fp:
        json.dump(board, fp, indent=2, ensure_ascii=False)
        fp.write("\n")


if __name__ == "__main__":
    sys.exit(main())
