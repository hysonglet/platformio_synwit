"""
Synwit StdPeriph Library (framework-synwit)

Everything that is chip specific lives in `mcu.json` inside the framework
package, so this script stays identical no matter which SWM series is used.
Adding support for another series therefore means: unpack the SDK under
`csl/<series>` and add one record to `mcu.json`.
"""

import json
import os
import sys
from os.path import isdir, isfile, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

env.SConscript("_bare.py")

FRAMEWORK_DIR = platform.get_package_dir("framework-synwit")
assert isdir(FRAMEWORK_DIR), "Cannot find the framework-synwit package"

MCU_DB_PATH = join(FRAMEWORK_DIR, "mcu.json")
assert isfile(MCU_DB_PATH), "Missing %s" % MCU_DB_PATH

with open(MCU_DB_PATH, "r", encoding="utf-8") as fp:
    MCU_DB = json.load(fp)

FAMILIES = MCU_DB.get("families", {})


#
# helpers
#

def board_opt(name, default=None):
    """Read a custom `build.<name>` option (board JSON or board_build.<name>)."""
    return board.get("build.%s" % name, default)


def _to_list(value):
    if not value:
        return []
    if isinstance(value, (list, tuple)):
        return [str(v).strip() for v in value if str(v).strip()]
    return [v.strip() for v in str(value).split(",") if v.strip()]


def find_family(mcu):
    """Resolve `board_build.mcu` to a family record.

    Accepts the family key itself ("swm341") as well as concrete part numbers
    ("swm341cet7"), the longest matching prefix wins.
    """
    key = str(mcu or "").lower()
    if not key:
        return None, None
    for name in sorted(FAMILIES, key=len, reverse=True):
        if key == name or key.startswith(name):
            return name, FAMILIES[name]
    return None, None


def resolve_clock(family, mcu_key):
    """Translate `board_build.synwit_clock` into -D flags.

    The value may either be a name of one of the `clock_presets` of the family
    or an explicit list of `NAME=VALUE` pairs. Presets work because
    `system_<芯片>.c` guards all clock macros with #ifndef (see PATCHES.md).
    """
    raw = board_opt("synwit_clock")
    if raw is None:
        raw = family.get("clock_preset_default")
    if not raw:
        return []

    items = _to_list(raw)
    if len(items) == 1:
        preset = family.get("clock_presets", {}).get(items[0])
        if preset is not None:
            items = _to_list(preset)
    for item in items:
        if "=" not in item:
            known = sorted(family.get("clock_presets", {}))
            sys.stderr.write(
                "Warning! Ignore board_build.synwit_clock=%s for %s. "
                "It is neither a preset (%s) nor a NAME=VALUE pair.\n"
                % (raw, mcu_key, ", ".join(known) or "none")
            )
            return []
    return items


#
# resolve the target series
#

MCU = board.get("build.mcu", "")
MCU_KEY, FAMILY = find_family(MCU)
assert FAMILY, "Unknown Synwit MCU '%s'. Supported series: %s" % (
    MCU or "<empty>", ", ".join(sorted(FAMILIES)))

ROOT = join(FRAMEWORK_DIR, FAMILY["root"])
assert isdir(ROOT), "Missing CSL root %s" % ROOT


def fam_path(path):
    if os.path.isabs(path):
        return path
    return str(join(ROOT, path))


def pkg_path(path):
    return str(join(FRAMEWORK_DIR, path))


#
# compiler flags
#

cpu = board.get("build.cpu", FAMILY.get("cpu", ""))
float_abi = board_opt("float_abi", FAMILY.get("float_abi", "soft"))

if cpu:
    for flag_group in ("ASFLAGS", "CCFLAGS", "LINKFLAGS"):
        env.Append(**{flag_group: ["-mcpu=%s" % cpu]})
for flag_group in ("ASFLAGS", "CCFLAGS", "LINKFLAGS"):
    env.Append(**{flag_group: ["-mfloat-abi=%s" % float_abi]})

env.Append(
    CPPPATH=[fam_path(p) for p in FAMILY.get("include_dirs", [])],
    CPPDEFINES=[d for d in FAMILY.get("defines", [])] + resolve_clock(FAMILY, MCU_KEY),
    ASFLAGS=["-mthumb"],
    CCFLAGS=["-mthumb"],
    # Synwit CSL relies on printf("%f") and does not need syscalls from the
    # host: use the size-optimised nano C library together with nosys stubs
    LINKFLAGS=[
        "-specs=nosys.specs",
        "-specs=nano.specs",
        "-u", "_printf_float",
        "-mfloat-abi=%s" % float_abi,
        "-Wl,--gc-sections",
    ],
)

if not board.get("build.ldscript", ""):
    env.Replace(LDSCRIPT_PATH=pkg_path(FAMILY["ldscript"]))


#
# Target: build the CSL into static libraries
#

libs = []

# order matters: dependencies must be listed before their users
def _build_library(lib):
    src_filter = " ".join(_to_list(lib.get("src_filter", ["+<*.c>"])))
    short = lib["name"].replace("FrameworkSynwit", "").lower()
    override = board_opt("synwit_%s_filter" % short, "")
    return env.BuildLibrary(
        join("$BUILD_DIR", lib["name"]),
        fam_path(lib["dir"]),
        src_filter=override or src_filter
    )

for lib in FAMILY.get("libraries", []):
    libs.append(_build_library(lib))

# enable add-ons with board_build.synwit_features = dsp, usb_host
features = _to_list(board_opt("synwit_features"))
for feature, lib in (FAMILY.get("optional_libraries") or {}).items():
    if feature not in features:
        continue
    env.Append(
        CPPPATH=[fam_path(p) for p in lib.get("include_dirs", [])],
        CPPDEFINES=[d for d in lib.get("defines", [])],
    )
    libs.append(_build_library(lib))

env.Append(LIBS=libs)
