"""
Generate the .S files produced by ESP-IDF's `target_add_binary_data()`.

PlatformIO does not build with CMake/ninja. It runs CMake only to *configure*
the project, scrapes the resulting metadata, and then rebuilds everything with
SCons. CMake `add_custom_command()` rules are dropped in that translation.

`target_add_binary_data()` (tools/cmake/utilities.cmake) is implemented as an
add_custom_command that turns a data file into `${BUILD_DIR}/<name>.S`, then
adds that .S as a component source. Under PlatformIO the source is registered
but nothing ever generates it, so the build dies with:

    Source `.pio/build/<env>/https_server.crt.S' not found, needed by target
    `.../https_server.crt.S.o'

Building with `idf.py` directly is unaffected -- real CMake runs the custom
command. This script only matters for the PlatformIO path.

This script scans component CMakeLists.txt files for target_add_binary_data()
calls and invokes the same helper CMake script the custom command would have,
writing the .S into $BUILD_DIR. Generating a .S that the active config does
not use (e.g. the MQTT cert) is harmless: it is only ever compiled if CMake
listed it as a component source.
"""

import os
import re
from pathlib import Path

Import("env")

platform = env.PioPlatform()

FRAMEWORK_DIR = Path(platform.get_package_dir("framework-espidf"))
CMAKE_EXE = Path(platform.get_package_dir("tool-cmake")) / "bin" / "cmake"
EMBED_SCRIPT = FRAMEWORK_DIR / "tools" / "cmake" / "scripts" / "data_file_embed_asm.cmake"

BUILD_DIR = Path(env.subst("$BUILD_DIR"))
PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))

# Where user/managed components live. Mirrors what ends up in COMPONENT_DIRS.
COMPONENT_ROOTS = ("managed_components", "components")

CALL_RE = re.compile(r"target_add_binary_data\s*\(([^)]*)\)", re.MULTILINE)


def _tokenize(arglist):
    """Split a CMake argument list, dropping quotes. Good enough for this call."""
    return [tok.strip('"') for tok in arglist.split()]


def _find_calls():
    """Yield (data_file, file_type, rename_to) for every embeddable call found."""
    for root in COMPONENT_ROOTS:
        root_dir = PROJECT_DIR / root
        if not root_dir.is_dir():
            continue
        for cmakelists in root_dir.glob("*/CMakeLists.txt"):
            try:
                text = cmakelists.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for match in CALL_RE.finditer(text):
                tokens = _tokenize(match.group(1))
                # Signature: target_add_binary_data(<target> <file> <type> [RENAME_TO <n>] [DEPENDS ...])
                if len(tokens) < 3:
                    continue
                _target, data_file, file_type = tokens[0], tokens[1], tokens[2]
                # Unexpanded CMake variables can't be resolved outside CMake.
                if "${" in data_file or "$<" in data_file:
                    continue
                rename_to = None
                if "RENAME_TO" in tokens:
                    idx = tokens.index("RENAME_TO")
                    if idx + 1 < len(tokens) and "${" not in tokens[idx + 1]:
                        rename_to = tokens[idx + 1]
                if file_type not in ("TEXT", "BINARY"):
                    file_type = "BINARY"
                resolved = (cmakelists.parent / data_file).resolve()
                if resolved.is_file():
                    yield resolved, file_type, rename_to


def _generate(data_file, file_type, rename_to):
    # target_add_binary_data() always writes to ${BUILD_DIR}/<basename>.S
    source_file = BUILD_DIR / (data_file.name + ".S")
    if source_file.is_file() and source_file.stat().st_mtime >= data_file.stat().st_mtime:
        return

    cmd = [
        str(CMAKE_EXE),
        "-DDATA_FILE=" + str(data_file),
        "-DSOURCE_FILE=" + str(source_file),
        "-DFILE_TYPE=" + file_type,
    ]
    if rename_to:
        cmd.append("-DVARIABLE_BASENAME=" + rename_to)
    cmd += ["-P", str(EMBED_SCRIPT)]

    env.Execute(
        env.VerboseAction(
            " ".join('"%s"' % part if " " in part else part for part in cmd),
            "Generating assembly for %s..." % data_file.name,
        )
    )


if not EMBED_SCRIPT.is_file():
    print("warning: %s missing; skipping binary data embedding" % EMBED_SCRIPT)
else:
    os.makedirs(BUILD_DIR, exist_ok=True)
    for data_file, file_type, rename_to in _find_calls():
        _generate(data_file, file_type, rename_to)
