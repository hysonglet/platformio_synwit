#
# Default flags for bare-metal programming (no framework / no CSL)
#

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
