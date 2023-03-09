# Copyright 2014-present PlatformIO <contact@platformio.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""
SPL

The ST Standard Peripheral Library provides a set of functions for
handling the peripherals on the synwit ARM chip family.
The idea is to save the user (the new user, in particular) having to deal
directly with the registers.
"""
import os
from os.path import isdir, isfile, join, dirname, realpath
from string import Template

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
platform = env.PioPlatform()
board = env.BoardConfig()

env.SConscript("_bare.py")

def get_flag_value(flag_name:str, default_val:bool):
    flag_val = board.get("build.%s" % flag_name, default_val)
    flag_val = str(flag_val).lower() in ("1", "yes", "true")
    return flag_val


env.Append(
    LINKFLAGS=["--specs=nosys.specs", "--specs=nano.specs"]
)

if not board.get("build.mcu").startswith("swm"):
    print("""Error! The specified is a warehouse of non-synwit chips in synwit,
        Unable to continue working""")
    env.Exit(-1)

FRAMEWORK_DIR = platform.get_package_dir("framework-spl-synwit")
assert isdir(FRAMEWORK_DIR)

ALL_PACK_PATH = join(FRAMEWORK_DIR, "synwit")
series = board.get("build.series").lower()
mcu = board.get('build.mcu').lower() # swm34sret6
PACK_DIR = ""

# print("mcu series:  %s" % series)
# find we need std path
for dir in os.listdir(ALL_PACK_PATH):
    if dir.lower().startswith(series):
        PACK_DIR = join(ALL_PACK_PATH, dir)
        break

if len(PACK_DIR) == 0:
    print("No standard library directory found")
    exit(-1)

CMSIS_DIR = join(PACK_DIR, "CMSIS")
assert isdir(CMSIS_DIR, )

# standard library directory
STD_PERIPH_PATH = join(PACK_DIR, "%s_StdPeriph_Driver" % series.upper())

env.Append(
    CPPDEFINES=[
        "USE_STDPERIPH_DRIVER"
    ],
    CPPPATH=[
        join(CMSIS_DIR, "CoreSupport"),
        join(CMSIS_DIR, "DeviceSupport"),
        # join(CMSIS_DIR, "startup", "gcc"),
        join(STD_PERIPH_PATH)
    ]
)

# cmsis source files
env.BuildSources(
    join("$BUILD_DIR", "CMSIS"),
    join(CMSIS_DIR, "DeviceSupport"),
    src_filter=[
        "-<*>",
        f"+<system_{series.upper()}.c>",
        f"+<startup/gcc/startup_{series.upper()}.S>"
    ]
)

# std periph files
env.BuildSources(
    join("$BUILD_DIR", "StdPD"),
    join(STD_PERIPH_PATH),
    src_filter=[
        "+<*.c>"
    ]
)

def get_linker_script(mcu):
    if not board.get("build.ldscript", ""):
        ldscript =  join(FRAMEWORK_DIR, "platformio", "ldscripts", mcu + ".ld")

        # update always
        # if isfile(ldscript):
        #     return ldscript
        stack_size = board.get('upload.stack_size', 0x400)
        rom_size = board.get('upload.maximum_size', 0x80000)
        ram_size = board.get('upload.maximum_ram_size', 0x10000)

        template_file = join(FRAMEWORK_DIR, 'platformio', 'ldscripts', 'tpl', 'linker.tpl')
        print("template_file:", template_file)
        content = ""
        with open(template_file) as fp:
            data = Template(fp.read())
            content = data.substitute(
                stack=hex(stack_size), # 0x20000000 - start address for RAM
                ram=str(int(ram_size/1024)) + "K",
                rom=str(int(rom_size/1024)) + "K",
            )

        with open(ldscript, "w") as fp:
            fp.write(content)
    
        return ldscript
    else:
        default_ld = join(FRAMEWORK_DIR, "platformio", "ldscripts", series) + ".ld"
        return default_ld

env.Replace(
    LDSCRIPT_PATH=get_linker_script(mcu)
)

def configure_printf_lib():
    # allows wrapping of printf functions though, e.g., printf-minimal library.
    if get_flag_value("use_minimal_printf", False):
        env.Append(LINKFLAGS=[
            "-Wl,--wrap,printf",
            ])
    if get_flag_value("use_printf_float", False):
         env.Append(LINKFLAGS=[
             "-u_printf_float",
             ])

configure_printf_lib()


