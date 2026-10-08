import sys
from os import makedirs
from os.path import isdir, join
from platform import system as os_system

from SCons.Script import (ARGUMENTS, COMMAND_LINE_TARGETS, AlwaysBuild,
                          Builder, Default, DefaultEnvironment)

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

env.Replace(
    AR="arm-none-eabi-gcc-ar",
    AS="arm-none-eabi-gcc",
    CC="arm-none-eabi-gcc",
    CXX="arm-none-eabi-g++",
    GDB="arm-none-eabi-gdb",
    OBJCOPY="arm-none-eabi-objcopy",
    RANLIB="arm-none-eabi-gcc-ranlib",
    SIZETOOL="arm-none-eabi-size",

    ARFLAGS=["rc"],

    # SCons assembles `.s` with "$AS $ASFLAGS -o $TARGET $SOURCES" (no -c).
    # We use the gcc driver as assembler (it can preprocess too), so add -c.
    ASCOM="$AS $ASFLAGS $ASPPFLAGS $_ASINCFLAGS -c -o $TARGET $SOURCES",

    # .isr_vector holds the Cortex-M vector table and lives outside .text
    SIZEPROGREGEXP=r"^(?:\.text|\.isr_vector|\.data|\.rodata|\.ARM.exidx)\s+(\d+).*",
    SIZEDATAREGEXP=r"^(?:\.data|\.bss|\.noinit)\s+(\d+).*",
    SIZECHECKCMD="$SIZETOOL -A -d $SOURCES",
    SIZEPRINTCMD="$SIZETOOL -B -d $SOURCES",

    PROGSUFFIX=".elf"
)

# Allow user to override via pre:script
if env.get("PROGNAME", "program") == "program":
    env.Replace(PROGNAME="firmware")

env.Append(
    BUILDERS=dict(
        ElfToBin=Builder(
            action=env.VerboseAction(" ".join([
                "$OBJCOPY",
                "-O",
                "binary",
                "$SOURCES",
                "$TARGET"
            ]), "Building $TARGET"),
            suffix=".bin"
        ),
        ElfToHex=Builder(
            action=env.VerboseAction(" ".join([
                "$OBJCOPY",
                "-O",
                "ihex",
                "$SOURCES",
                "$TARGET"
            ]), "Building $TARGET"),
            suffix=".hex"
        )
    )
)

if not env.get("PIOFRAMEWORK"):
    env.SConscript("frameworks/_bare.py")

#
# Target: Build executable and linkable firmware
#

target_elf = None
if "nobuild" in COMMAND_LINE_TARGETS:
    target_elf = join("$BUILD_DIR", "${PROGNAME}.elf")
    target_firm = join("$BUILD_DIR", "${PROGNAME}.bin")
else:
    target_elf = env.BuildProgram()
    target_firm = env.ElfToBin(join("$BUILD_DIR", "${PROGNAME}"), target_elf)
    env.Depends(target_firm, "checkprogsize")

AlwaysBuild(env.Alias("nobuild", target_firm))
target_buildprog = env.Alias("buildprog", target_firm, target_firm)

#
# Target: Print binary size
#

target_size = env.Alias(
    "size", target_elf,
    env.VerboseAction("$SIZEPRINTCMD", "Calculating size $SOURCE"))
AlwaysBuild(target_size)

#
# Target: Upload firmware
#

upload_protocol = env.subst("$UPLOAD_PROTOCOL")
upload_actions = []
upload_source = target_firm

debug_tools = board.get("debug.tools", {})


if upload_protocol.startswith("jlink"):
    jlink_device = board.get("debug.jlink_device")
    if not jlink_device:
        sys.stderr.write(
            "Warning! Missed J-Link Device ID, see `debug.jlink_device`\n")

    def _jlink_program(env, source):
        """Flash through J-Link.

        `loadfile` understands .hex/.elf and needs no flash address; for a
        plain .bin use the SoC specific address instead.
        """
        build_dir = env.subst("$BUILD_DIR")
        if not isdir(build_dir):
            makedirs(build_dir)
        target = source[0] if isinstance(source, list) else source
        if str(target).endswith(".bin"):
            cmd = "loadbin %s, %s" % (
                target, board.get("upload.offset_address", "0x0"))
        else:
            cmd = "loadfile %s" % target
        script_path = join(build_dir, "upload.jlink")
        with open(script_path, "w") as fp:
            fp.write("\n".join(["h", cmd, "r", "q"]))
        return script_path

    env.Replace(
        __jlink_cmd_script=_jlink_program,
        UPLOADER="JLink.exe" if os_system() == "Windows" else "JLinkExe",
        UPLOADERFLAGS=[
            "-device", jlink_device,
            "-speed", env.GetProjectOption("debug_speed", "4000"),
            "-if", ("jtag" if upload_protocol == "jlink-jtag" else "swd"),
            "-autoconnect", "1",
            "-NoGui", "1"
        ],
        UPLOADCMD='$UPLOADER $UPLOADERFLAGS -CommanderScript "${__jlink_cmd_script(__env__, SOURCE)}"'
    )
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

elif upload_protocol == "custom":
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

elif upload_protocol in debug_tools:
    server = debug_tools.get(upload_protocol).get("server", {})
    openocd_args = ["-d%d" % (2 if int(ARGUMENTS.get("PIOVERBOSE", 0)) else 1)]
    openocd_args.extend(server.get("arguments", []))
    if env.GetProjectOption("debug_speed", ""):
        openocd_args.extend(["-c", "adapter speed %s" %
                             env.GetProjectOption("debug_speed")])
    openocd_args.extend([
        "-c", "program {$SOURCE} %s verify reset; shutdown;" %
        board.get("upload.offset_address", "")
    ])
    openocd_args = [
        f.replace("$PACKAGE_DIR",
                  platform.get_package_dir(server["package"]) or "")
        for f in openocd_args
    ]
    env.Replace(
        UPLOADER=server.get("executable", "openocd"),
        UPLOADERFLAGS=openocd_args,
        UPLOADCMD="$UPLOADER $UPLOADERFLAGS"
    )
    upload_actions = [env.VerboseAction("$UPLOADCMD", "Uploading $SOURCE")]

else:
    sys.stderr.write(
        "Warning! Unknown upload protocol %s\n"
        "        Supported: jlink, custom (and any `debug.tools` entry).\n"
        "        Set `upload_protocol = custom` and `upload_command = ...`\n"
        "        to invoke the vendor tool (J-Flash / SWMTool / SynwitPRG).\n"
        % upload_protocol
    )
    upload_actions = [env.VerboseAction("echo %s" % upload_protocol)]

AlwaysBuild(env.Alias("upload", upload_source, upload_actions))

#
# Default targets
#

Default([target_buildprog, target_size])
