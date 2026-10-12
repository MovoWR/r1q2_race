#!/usr/bin/env python3
"""Validate an explicit MSVC x86 /MT static OpenAL prefix without running it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

SYSTEM_LIBRARIES = ("winmm.lib", "ole32.lib", "shell32.lib", "user32.lib")
SYSTEM_DLLS = {"kernel32.dll", "winmm.dll", "ole32.dll", "shell32.dll",
               "user32.dll"}
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    record = {"success": False, "commands": [], "inputs": {}, "probe_executed": False}

    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def remember(path):
        require(path.is_file(), "Missing static OpenAL input: " + str(path))
        record["inputs"][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()

    def run(command, label):
        command = [str(value) for value in command]
        result = subprocess.run(command, cwd=output, capture_output=True,
                                text=True, errors="replace", timeout=120)
        log = output / (label + ".log")
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        record["commands"].append({"argv": command, "exit_code": result.returncode, "log": str(log)})
        require(result.returncode == 0, label + " failed; see " + str(log))
        return result.stdout + result.stderr

    try:
        require(os.name == "nt", "Static OpenAL validation requires native Windows MSVC.")
        prefix = Path(args.prefix)
        require(args.prefix and prefix.is_absolute(), "OpenAL prefix must be an explicit absolute path.")
        prefix = prefix.resolve()
        require(not any(char in str(prefix) for char in ";\r\n"), "OpenAL prefix contains an unsupported list separator.")
        include = prefix / "include"
        library = prefix / "lib/OpenAL32.lib"
        for name in ("al.h", "alc.h", "alext.h"):
            remember(include / "AL" / name)
        remember(library)
        compiler = shutil.which(args.compiler)
        require(compiler is not None, "MSVC compiler unavailable; use an x86 Visual Studio developer prompt.")
        dumpbin = Path(compiler).with_name("dumpbin.exe")
        require(dumpbin.is_file() and os.environ.get("INCLUDE") and os.environ.get("LIB"),
                "Static OpenAL requires an x86 Visual Studio developer prompt (cl, dumpbin, INCLUDE and LIB).")
        text = run([dumpbin, "/nologo", "/headers", "/directives", library], "archive")
        machines = re.findall(r"^\s*([0-9A-Fa-f]+) machine\b", text, re.M)
        require(machines and all(int(machine, 16) == 0x14c for machine in machines),
                "OpenAL archive is not entirely native x86 COFF.")
        require(not re.search(r"DLL name\s*:|IMPORT HEADER", text, re.I),
                "OpenAL DLL import libraries are unsupported; provide a static OpenAL32.lib.")
        require(not re.search(r"DEFAULTLIB:\s*\"?(?:MSVCRT|MSVCRTD|MSVCPRT|MSVCPRTD|LIBCMTD|LIBCPMTD)\b|"
                              r"RuntimeLibrary=(?:MD_|MTd_)", text, re.I),
                "OpenAL archive requires an incompatible CRT; native /MT is required.")
        source = ROOT / "tools/static_openal_probe.c"
        remember(source)
        remember(Path(__file__).resolve())
        obj = output / "static-openal-probe.obj"
        executable = output / "static-openal-probe.exe"
        run([compiler, "/nologo", "/TC", "/O2", "/W3", "/MT", "/DAL_LIBTYPE_STATIC",
             "/I" + str(include), "/c", source, "/Fo" + str(obj)], "compile")
        run([compiler, "/nologo", "/MT", obj, "/Fe" + str(executable), "/link",
             "/MACHINE:X86", "/INCREMENTAL:NO", "/SUBSYSTEM:CONSOLE", library,
             *SYSTEM_LIBRARIES], "link")
        imports = run([dumpbin, "/nologo", "/dependents", executable], "imports")
        dlls = {name.lower() for name in re.findall(r"^\s+([\w.-]+\.dll)\s*$", imports, re.M | re.I)}
        require(dlls and dlls <= SYSTEM_DLLS,
                "Static OpenAL probe imports unexpected DLLs: " + ", ".join(sorted(dlls - SYSTEM_DLLS)))
        record["imports"] = sorted(dlls)
        record["success"] = True
        print("\n".join([str(include), str(library), *SYSTEM_LIBRARIES]))
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        record["error"] = str(error)
        print("Static OpenAL validation: " + str(error), file=sys.stderr)
    finally:
        (output / "report.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return 0 if record["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
