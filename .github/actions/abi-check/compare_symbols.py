# Copyright (c) 2026 UXL Foundation Contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import argparse
import pathlib
import re
import sys

import lief

# The job summary of a workflow run is capped, so only the beginning of a long
# list of symbols is reported
MAX_REPORTED_SYMBOLS = 51

OPERATING_SYSTEMS = {
    lief.Binary.FORMATS.ELF: "linux",
    lief.Binary.FORMATS.PE: "windows",
    lief.Binary.FORMATS.MACHO: "macos",
}

def parse_arguments():
    parser = argparse.ArgumentParser(
        description="Compare the exported symbols of the built shared libraries "
                    "against the checked-in ABI baselines."
    )
    parser.add_argument(
        "--binary",
        type=pathlib.Path,
        action="append",
        required=True,
        default=[],
        metavar="PATH",
        dest="binaries",
        help="Shared library to check. Repeat to check more than one"
    )
    parser.add_argument(
        "--baseline-dir",
        type=pathlib.Path,
        required=True,
        help="Directory holding the <platform>/<library>.txt baselines."
    )
    parser.add_argument(
        "--project",
        required=True,
        help="Name of the checked project, used as the heading of the report."
    )
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        required=True,
        help="Directory to write delta.md and the regenerated baselines to."
    )

    arguments = parser.parse_args()

    for binary in arguments.binaries:
        if not binary.is_file():
            parser.error(f"--binary '{binary}' is not a file")

    # The baselines are regenerated under this path inside the output directory,
    # which an absolute path would silently escape.
    if arguments.baseline_dir.is_absolute():
        parser.error(f"--baseline-dir '{arguments.baseline_dir}' has to be relative")

    return arguments


def binary_platform(binary):
    operating_system = OPERATING_SYSTEMS.get(binary.format)
    if operating_system is None:
        raise RuntimeError(f"unsupported binary format '{binary.format.name}'")
    return f"{operating_system}-{64 if binary.abstract.header.is_64 else 32}"


def exported_symbol_names(binary):
    if binary.format in (lief.Binary.FORMATS.ELF, lief.Binary.FORMATS.MACHO):
        return {symbol.name for symbol in binary.exported_symbols}

    if binary.format == lief.Binary.FORMATS.PE:
        # PE has no exported_symbols property
        export_dir = binary.get_export()
        if export_dir is None:
            return set()
        return {entry.name for entry in export_dir.entries if entry.name}

    raise RuntimeError(f"unsupported binary format '{binary.format.name}'")


def library_name(path):
    # Remove the lib prefix if any and strip the version from the name.
    # E.g. libtbb -> tbb, tbb12 -> tbb, libtbbbind_2_5 -> tbbbind.
    name = path.name.removeprefix("lib").partition(".")[0]
    return re.sub(r"_?\d+(_\d+)*$", "", name)


def parse_binaries(paths):
    binaries = {}
    parsed = set()
    for path in paths:
        # Resolve if the path is a symlink
        real_path = path.resolve()
        if real_path in parsed:
            continue
        parsed.add(real_path)

        binary = lief.parse(str(path))
        if binary is None:
            raise RuntimeError(f"'{path}' is not a supported binary")
        platform = binary_platform(binary)
        binaries.setdefault(platform, {})
        binaries[platform][library_name(path)] = sorted(exported_symbol_names(binary))
    return binaries


def get_baselines(baseline_dir, platform):
    baselines = {}
    platform_dir = baseline_dir / platform
    if not platform_dir.is_dir():
        return baselines

    for path in sorted(platform_dir.glob("*.txt")):
        symbols = set()
        for line in path.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#"):
                symbols.add(line)
        baselines[path.stem] = sorted(symbols)
    return baselines


def baseline_text(name, symbols, platform):
    lines = [f"# Exported symbols of {name} on {platform}.",
             "# Generated automatically, do not edit by hand.",
             ""]
    lines.extend(symbols)
    return "\n".join(lines) + "\n"


def write_new_baselines(binaries, destination, platform):
    for name, symbols in binaries.items():
        if not symbols:
            continue

        destination.mkdir(parents=True, exist_ok=True)
        (destination / f"{name}.txt").write_text(baseline_text(name, symbols, platform),
                                                 newline="\n")


def compare_with_baseline(binaries, baselines):
    deltas = {}
    for name in sorted(set(binaries) | set(baselines)):
        current = set(binaries.get(name, []))
        baseline = set(baselines.get(name, []))
        added = sorted(current - baseline)
        removed = sorted(baseline - current)

        if added or removed:
            deltas[name] = {
                "added": added,
                "removed": removed,
                "missing_binary": name not in binaries,
                "missing_baseline": name not in baselines
            }
    return deltas


def format_symbols(title, symbols):
    if not symbols:
        return []

    lines = [f"{title}:", "", "```"]
    lines.extend(symbols[:MAX_REPORTED_SYMBOLS])
    if len(symbols) > MAX_REPORTED_SYMBOLS:
        lines.append(f"... and {len(symbols) - MAX_REPORTED_SYMBOLS} more")
    lines.extend(["```", ""])
    return lines


def format_delta(deltas, platform):
    if not deltas:
        return ""

    lines = [f"#### {platform}", ""]
    for name, delta in deltas.items():
        lines.extend([f"<details><summary><code>{name}</code>: "
                      f"{len(delta['added'])} added, {len(delta['removed'])} removed"
                      f"</summary>", ""])

        if delta["missing_binary"]:
            lines.extend(["No binary of the library was given for the check.", ""])
        if delta["missing_baseline"]:
            lines.extend(["The library has no baseline yet.", ""])

        # The removed entry points come first, they are the breaking ones.
        lines.extend(format_symbols("Removed", delta["removed"]))
        lines.extend(format_symbols("Added", delta["added"]))
        lines.extend(["</details>", ""])

    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    arguments = parse_arguments()

    binaries = parse_binaries(arguments.binaries)

    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    report = ""
    for platform, libraries in sorted(binaries.items()):
        baselines = get_baselines(arguments.baseline_dir, platform)
        write_new_baselines(libraries,
                        arguments.output_dir / "baseline" / arguments.baseline_dir / platform,
                        platform)
        report += format_delta(compare_with_baseline(libraries, baselines), platform)

    (arguments.output_dir / "delta.md").write_text(
        f"### {arguments.project}\n\n{report}" if report else "", newline="\n")

    checked = ", ".join(sorted(binaries))
    if report:
        print(report)
        print(f"The exported symbols differ from the baselines of {checked}. "
            f"Removing an entry point breaks backward compatibility, adding one has "
            f"to be recorded by updating the baselines.")
        sys.exit(1)
    else:
        print(f"The exported symbols match the baselines of {checked}.")
