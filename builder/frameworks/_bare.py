#
# Default flags for bare-metal programming (no framework / no CSL)
#

import os
from os.path import isdir, join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

if "BOARD" in env:
    env.Append(
        ASFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")],
        CCFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")],
        LINKFLAGS=["-mcpu=%s" % env.BoardConfig().get("build.cpu")],
    )
    # when a framework script imported this file the framework owns the
    # floating point ABI (it knows the real capabilities of the core)
    if not env.get("PIOFRAMEWORK"):
        float_abi = env.BoardConfig().get("build.float_abi")
        if float_abi:
            for flag_group in ("ASFLAGS", "CCFLAGS", "LINKFLAGS"):
                env.Append(**{flag_group: ["-mfloat-abi=%s" % float_abi]})

def _append_project_include_dirs():
    """Expose the project's headers to lib_deps libraries too.

    PlatformIO only exposes $PROJECT_INCLUDE_DIR to the project's own sources;
    libraries are built in their own cloned env and would not see headers such
    as lv_conf.h / SEGGER_RTT_Conf.h / ff.h unless platformio.ini adds an
    explicit `-I$PROJECT_DIR/...`. Since framework scripts run before
    ProcessProjectDeps(), appending here reaches every library build.
    """
    current = [env.subst(p) for p in env.get("CPPPATH", [])]
    dirs = []

    def add(path):
        if path and isdir(path) and path not in current and path not in dirs:
            dirs.append(path)

    add(env.subst("$PROJECT_INCLUDE_DIR"))
    src_dir = env.subst("$PROJECT_SRC_DIR")
    add(src_dir)
    if src_dir and isdir(src_dir):
        for root, subdirs, _ in os.walk(src_dir):
            subdirs[:] = [d for d in sorted(subdirs) if not d.startswith(".")]
            for d in subdirs:
                add(join(root, d))

    env.Append(CPPPATH=dirs)


_append_project_include_dirs()

env.Append(
    ASFLAGS=["-mthumb"],
    ASPPFLAGS=["-x", "assembler-with-cpp"],
    CCFLAGS=[
        "-Os",  # optimize for size
        "-ffunction-sections",  # place each function in its own section
        "-fdata-sections",
        "-Wall",
        "-funsigned-char",
        "-Wpointer-arith",
        "-mthumb",
    ],
    CXXFLAGS=[
        "-fno-rtti",
        "-fno-exceptions",
    ],
    CPPDEFINES=[("F_CPU", "$BOARD_F_CPU")],
    LINKFLAGS=[
        "-Os",
        "-Wl,--gc-sections,--relax",
        "-mthumb",
    ],
    LIBS=["c", "gcc", "m", "stdc++"],
)
