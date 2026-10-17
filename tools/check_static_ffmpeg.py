#!/usr/bin/env python3
"""Validate an explicit MSVC x86/x64 /MT FFmpeg prefix for both Windows builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

COMPONENTS = ("avformat", "avcodec", "swresample", "swscale", "avutil")
SYSTEM_LIBRARIES = ("bcrypt.lib", "ole32.lib", "user32.lib", "advapi32.lib",
                    "ws2_32.lib", "secur32.lib")
SYSTEM_DLLS = {name[:-4] + ".dll" for name in SYSTEM_LIBRARIES} | {"kernel32.dll"}
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--arch", choices=("x86", "x64"), default="x86")
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    record = {"success": False, "arch": args.arch, "commands": [], "inputs": {}}

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def remember(path):
        require(path.is_file(), "Missing static FFmpeg input: " + str(path))
        record["inputs"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()

    def run(command, label):
        result = subprocess.run([str(value) for value in command], cwd=output,
                                capture_output=True, text=True, errors="replace", timeout=120)
        log = output / (label + ".log")
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        record["commands"].append({"argv": [str(value) for value in command],
                                   "exit_code": result.returncode, "log": str(log)})
        require(result.returncode == 0, label + " failed; see " + str(log))
        return result.stdout + result.stderr

    try:
        require(os.name == "nt", "Static FFmpeg validation requires native Windows MSVC.")
        prefix = Path(args.prefix)
        require(args.prefix and prefix.is_absolute(), "FFmpeg prefix must be an explicit absolute path.")
        prefix = prefix.resolve()
        require(not any(char in str(prefix) for char in ";\r\n"), "FFmpeg prefix contains an unsupported list separator.")
        include = prefix / "include"
        libraries = [prefix / "lib" / (name + ".lib") for name in COMPONENTS]
        for name, library in zip(COMPONENTS, libraries):
            remember(include / ("lib" + name) / (name + ".h"))
            remember(include / ("lib" + name) / "version.h")
            major = include / ("lib" + name) / "version_major.h"
            if major.is_file():
                remember(major)
            for header in sorted((include / ("lib" + name)).glob("*.h")):
                remember(header)
            remember(library)
        compiler = shutil.which(args.compiler)
        require(compiler is not None, "MSVC compiler unavailable; use a matching Visual Studio developer prompt.")
        dumpbin = Path(compiler).with_name("dumpbin.exe")
        require(dumpbin.is_file() and os.environ.get("INCLUDE") and os.environ.get("LIB"),
                "Enabled codecs require a matching Visual Studio developer prompt (cl, dumpbin, INCLUDE and LIB).")
        expected_machine = {"x86": 0x14c, "x64": 0x8664}[args.arch]
        for library in libraries:
            text = run([dumpbin, "/nologo", "/headers", "/directives", library], library.stem)
            machines = re.findall(r"^\s*([0-9A-Fa-f]+) machine\b", text, re.M)
            require(machines and all(int(machine, 16) == expected_machine for machine in machines),
                    "FFmpeg archive is not entirely native " + args.arch + " COFF: " + str(library))
            require(not re.search(r"DLL name\s*:|IMPORT HEADER", text, re.I),
                    "FFmpeg DLL import library is unsupported: " + str(library))
            require(not re.search(r"DEFAULTLIB:\s*\"?(?:MSVCRT|MSVCRTD|LIBCMTD)\b|"
                                  r"RuntimeLibrary=(?:MD_|MTd_)", text, re.I),
                    "FFmpeg archive requires an incompatible CRT; native /MT is required: " + str(library))
        sources = [ROOT / "client" / name for name in ("snd_ogg.c", "snd_stream.c", "cl_media.c")]
        sources.append(ROOT / "tools/static_ffmpeg_probe.c")
        remember(Path(__file__).resolve())
        for name in ("snd_ogg.h", "snd_stream.h", "cl_media.h"):
            remember(ROOT / "client" / name)
        objects = []
        for source in sources:
            remember(source)
            obj = output / (source.stem + ".obj")
            run([compiler, "/nologo", "/TC", "/O2", "/W3", "/MT", "/fp:precise",
                 "/DUSE_AVCODEC", "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(include),
                 "/I" + str(ROOT / "client"), "/c", source, "/Fo" + str(obj)], source.stem + "-compile")
            objects.append(obj)
        executable = output / "static-ffmpeg-probe.exe"
        run([compiler, "/nologo", "/MT", *objects, "/Fe" + str(executable), "/link",
             "/MACHINE:" + args.arch.upper(), "/INCREMENTAL:NO", "/SUBSYSTEM:CONSOLE", *libraries,
             *SYSTEM_LIBRARIES], "link")
        imports = run([dumpbin, "/nologo", "/dependents", executable], "imports")
        dlls = {name.lower() for name in re.findall(r"^\s+([\w.-]+\.dll)\s*$", imports, re.M | re.I)}
        require(dlls and dlls <= SYSTEM_DLLS,
                "Static FFmpeg probe imports unexpected DLLs: " + ", ".join(sorted(dlls - SYSTEM_DLLS)))
        record["imports"] = sorted(dlls)
        record["capabilities"] = run([executable], "capabilities")
        record["success"] = True
        # Shared machine-readable result: include directory, then exact link arguments.
        print("\n".join([str(include), *map(str, libraries), *SYSTEM_LIBRARIES]))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        record["error"] = str(error)
        print("Static FFmpeg validation: " + str(error), file=sys.stderr)
    finally:
        (output / "report.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return 0 if record["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
