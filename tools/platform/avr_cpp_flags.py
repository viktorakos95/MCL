import os
import zlib

Import("env")

env.AppendUnique(
    CXXFLAGS=[
        "-fno-threadsafe-statics",
        "-fno-rtti",
    ]
)

# Size options, checked against the timing-critical code (every function an
# interrupt reaches, the sequencer, clock and MIDI code) by instruction count:
# -fmerge-all-constants changes none of it; -fno-move-loop-invariants
# shortens the sequencer tick, the clock callbacks and the timer interrupt,
# but makes the loops in the sources below longer, so those keep the default.
env.Append(CCFLAGS=["-fno-move-loop-invariants", "-fmerge-all-constants"])
_KEEP_LOOP_INVARIANTS = ("*/src/mcl/Midi/Midi.cpp",
                         "*/Sequencer/ArpSeqTrack.cpp",
                         "*/Sequencer/ExtSeqTrack.cpp")


def _keep_loop_invariants(env, node):
    return env.Object(node, CCFLAGS=env["CCFLAGS"] + ["-fmove-loop-invariants"])


for _pattern in _KEEP_LOOP_INVARIANTS:
    env.AddBuildMiddleware(_keep_loop_invariants, _pattern)

# Link layout (see tools/platform/avr_lowtext.py): one section per function
# from the LTO link, relocations kept in the ELF (the image is unchanged), and
# avr6_mcl.x, which links the functions in avr_lowtext.ld below 128 KB so
# they need no trampoline. MCL_AVR_LOWTEXT_FREE=1 links in the compiler's own
# order, as input for avr_lowtext.py.
platform_dir = os.path.join(env.subst("$PROJECT_DIR"), "tools", "platform")
script = os.path.join(platform_dir, "avr6_mcl.x")
listing = os.path.join(platform_dir, "avr_lowtext.ld")
search = [platform_dir]
if os.environ.get("MCL_AVR_LOWTEXT_FREE"):
    free_dir = os.path.join(env.subst("$BUILD_DIR"), "lowtext_free")
    os.makedirs(free_dir, exist_ok=True)
    with open(os.path.join(free_dir, "avr_lowtext.ld"), "w") as empty:
        empty.write("/* MCL_AVR_LOWTEXT_FREE: compiler order */\n")
    search.insert(0, free_dir)
# The checksum symbol puts the script and list contents into the link command,
# so editing either relinks (PROGNAME is not final yet for env.Depends here).
crc = 0
for path in (script, listing):
    with open(path, "rb") as source:
        crc = zlib.crc32(source.read(), crc)
env.Append(LINKFLAGS=["-ffunction-sections", "-Wl,-q", "-flto-partition=one"] +
           ["-Wl,-L," + d for d in search] + ["-Wl,-T," + script,
           "-Wl,--defsym=__mcl_lowtext_crc=0x%08x" % (crc & 0xFFFFFFFF)])
