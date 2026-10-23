#!/usr/bin/env python3
"""Project reference scenarios mined from tsgo's own baselines (plan 41; thirdparty/typescript, the 7.1 oracle): tsbuild/*
and tsc/{projectReferences,composite}, and tttt's own in test/scenarios (`own/NAME`). Each baseline holds a virtual filesystem (`Input::`), a tsc command and its
`Output::`. Only the first command counts (later ones are incremental edits). The tree is written under a temporary
directory, release/tttt runs the command there (`-b`/`-p` and the options tttt knows; output-only flags dropped), and
the (file, line, code) of the diagnostics are compared with tsc's.

    scripts/scenarios.py               pass/fail summary, failing scenarios with their differences
    scripts/scenarios.py -f REGEX      only scenarios whose name matches
    scripts/scenarios.py -v            show the commands and tttt's output of failing ones
    scripts/scenarios.py --keep DIR    write the trees under DIR (and keep them) to poke at them

Known divergences are listed in SKIP with why (plan 41: under -b tttt reads referenced projects' sources, never
outputs; no emit nor incremental state). tsc's tiny fake lib is replaced by tttt's real libraries.
"""
import argparse, os, re, shutil, subprocess, sys, tempfile
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, "thirdparty/typescript/tsc/testdata/baselines/reference")
# tttt's own scenarios in the same format (named own/...): divergences found on real solutions, cut down, with tsgo's
# output (`--builders 1` where tsgo's concurrent builders would race on outputs another project reads)
OWN = os.path.join(ROOT, "test/scenarios")
DIRS = ["tsbuild", "tsc/projectReferences", "tsc/composite"]
TSC = "tsgo"
ANSI = re.compile(r"\x1b\[[0-9;]*m")

# Name regex -> why it's skipped
SKIP = {
    r"^tsbuild/clean/": "--clean (tttt doesn't emit)",
    r"tsbuild/.*-discrepancies$": "incremental discrepancy reports, not a build",
    r"getNextInvalidatedProject|invalidates-projects-correctly|building-using-buildReferencedProject":
        "builder API, not a command",
    r"synthesized-module-specifiers-resolve-correctly|loads-js-based-projects-and-emits-them-correctly$":
        "tsc's fake lib declares Symbol.species, tttt's default libs don't",
    r"^tsbuild/dependencyUpdate/rebuilds-when-absolute-non-root-dependency-is-updated":
        "Windows drive-letter paths (`D:/work/...` in paths: rooted to tsgo's virtual fs, a relative name here)",
    r"^tsbuild/libraryResolution/with-config$":
        "tsc's fake lib.dom/lib.webworker stubs; the real ones conflict (tsc -b on the tree: TS2300/TS2403/TS2374 too)",
    r"^tsbuild/projectReferenceWithRootDirInParent/reports-error-for-same-tsbuildinfo-file":
        "TS6377 (two projects writing one .tsbuildinfo; tttt writes no build info)",
}

# Flags dropped from the command (output only), and flags with a value
DROP = {"--verbose", "-v", "--v", "--explainFiles", "--listEmittedFiles", "--traceResolution", "--listFiles",
        "--extendedDiagnostics", "--diagnostics", "-i", "--i", "--incremental", "--force", "-f", "--generateTrace"}
DROP_VALUE = {"--pretty"}


def parse(path):
    text = open(path, encoding="utf-8").read()
    lines = text.split("\n")
    m = re.match(r"currentDirectory::\s*(\S+)", lines[0])
    if not m:
        return None
    cwd = m.group(1)
    files, symlinks = {}, {}
    i = 0
    while i < len(lines) and lines[i] != "Input::":
        i += 1
    i += 1
    current = None
    command = None
    while i < len(lines):
        line = lines[i]
        if line.startswith(TSC + " ") or line == TSC:
            command = line[len(TSC):].split()
            break
        # (`//// [PATH] *new* `, or a symlink `//// [PATH] -> TARGET *new*`)
        m = re.match(r"^//// \[(.+?)\]( -> (\S+))?( \*[^*]+\*)?\s*$", line)
        if m:
            if m.group(3):
                symlinks[m.group(1)] = m.group(3)
                current = None
            else:
                current = m.group(1)
                files[current] = []
        elif current is not None:
            files[current].append(line)
        i += 1
    if command is None:
        return None
    i += 1
    status = None
    while i < len(lines) and (not lines[i].strip() or lines[i].startswith("ExitStatus::")):
        if lines[i].startswith("ExitStatus::"):
            status = EXIT_STATUS.get(lines[i].split("::", 1)[1].strip())
        i += 1
    if i >= len(lines) or lines[i] != "Output::":
        return None  # (A builder API scenario: an exit code, no output)
    i += 1
    output = []
    while i < len(lines) and not lines[i].startswith("//// [") and not lines[i].startswith(TSC) and \
            not re.match(r"^(Change|exitCode|Program root files|Edit \[)", lines[i]):
        output.append(ANSI.sub("", lines[i]))
        i += 1
    files = {k: "\n".join(v).rstrip("\n") + "\n" for k, v in files.items()}
    return cwd, files, symlinks, command, output, status


# tsgo's ExitStatus, as the baselines name it, to the process's exit code
EXIT_STATUS = {"Success": 0, "DiagnosticsPresent_OutputsSkipped": 1, "DiagnosticsPresent_OutputsGenerated": 2,
               "InvalidProject_OutputsSkipped": 3, "ProjectReferenceCycle_OutputsSkipped": 4}




def expected_diagnostics(output):
    ret = Counter()
    for line in output:
        m = re.match(r"^(\S.*?):(\d+):\d+ - error TS(\d+):", line) or re.match(r"^(\S.*?)\((\d+),\d+\): error TS(\d+):", line)
        if m:
            ret[(m.group(1), int(m.group(2)), int(m.group(3)))] += 1
            continue
        m = re.match(r"^error TS(\d+):", line)
        if m:
            ret[("", 0, int(m.group(1)))] += 1
    return ret


def actual_diagnostics(out, root):
    ret = Counter()
    for (name, line, code), n in expected_diagnostics(ANSI.sub("", out).split("\n")).items():
        ret[(name[len(root):] if name.startswith(root) else name, line, code)] += n
    return ret


# Build-only flags: dropped (tttt prints nothing about builds) only where tsc takes them, with --build first
BUILD_ONLY = {"--verbose", "-v", "--v", "--force", "-f"}


def translate(command, root):
    args = []
    build_first = bool(command) and command[0] in ("--b", "-b", "--build")
    i = 0
    while i < len(command):
        a = command[i]
        if a in BUILD_ONLY and not build_first and a.startswith("--"):
            args.append(a)  # (tsc's TS5093 without a leading --build)
        elif a in DROP:
            pass
        elif a in DROP_VALUE:
            i += 1
        elif a in ("--b", "-b", "--build"):
            args.append("-b" if i == 0 else a)  # (TS6369 anywhere else)
        elif a in ("--p", "-p", "--project"):
            args.append("-p")
        elif a.startswith("/"):
            args.append(root + a)
        else:
            args.append(a)
        i += 1
    return args


def run(name, scenario, tttt, keep, verbose):
    cwd, files, symlinks, command, output, _ = scenario
    root = keep and os.path.join(keep, name.replace("/", "__")) or tempfile.mkdtemp(prefix="tttt-scenario-")
    if os.path.exists(root):
        shutil.rmtree(root)
    try:
        for path, content in files.items():
            full = root + path
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as f:
                f.write(content)
        for link, target in symlinks.items():
            full = root + link
            os.makedirs(os.path.dirname(full), exist_ok=True)
            os.makedirs(root + target, exist_ok=True) if not os.path.exists(root + target) else None
            os.symlink(root + target, full)
        os.makedirs(root + cwd, exist_ok=True)
        args = translate(command, root)
        proc = subprocess.run([tttt] + args, cwd=root + cwd, capture_output=True, text=True,
                              timeout=60, env=dict(os.environ,
                                  # (tsgo's harness serves its Go content mappers in process: test/oracle, plan 56)
                                  TTTT_CONTENT_MAPPER_LAUNCHER=os.path.join(ROOT, "thirdparty/typescript/built/tttt-test-mapper")))
        out = proc.stdout + proc.stderr
        # Paths tttt prints relative to the current directory, tsc too
        expected = expected_diagnostics(output)
        actual = actual_diagnostics(out, root)
        unknown = "Unknown option" in out or "isn't supported" in out
        return expected, actual, args, out, unknown, proc.returncode
    finally:
        if not keep:
            shutil.rmtree(root, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-f", default=None)
    ap.add_argument("-v", action="store_true")
    ap.add_argument("--keep", default=None)
    ap.add_argument("--names", action="store_true", help="only list failing names")
    a = ap.parse_args()
    tttt = os.path.join(ROOT, "release/tttt")
    subprocess.run(["ninja", "-C", os.path.join(ROOT, "release")], check=True, capture_output=True)
    names = []
    for d in DIRS:
        for dirpath, _, fs in os.walk(os.path.join(REF, d)):
            for f in fs:
                if f.endswith(".js"):
                    names.append(os.path.relpath(os.path.join(dirpath, f), REF)[:-3])
    names += ["own/" + f[:-3] for f in os.listdir(OWN) if f.endswith(".js")]
    names.sort()
    passed, failed, skipped = [], [], Counter()
    for name in names:
        if a.f and not re.search(a.f, name):
            continue
        why = next((w for r, w in SKIP.items() if re.search(r, name)), None)
        if why:
            skipped[why] += 1
            continue
        scenario = parse(os.path.join(OWN, name[4:] + ".js") if name.startswith("own/") else os.path.join(REF, name + ".js"))
        if not scenario:
            skipped["no tsc command or output"] += 1
            continue
        expected, actual, args, out, unknown, code = run(name, scenario, tttt, a.keep, a.v)
        if unknown:
            skipped["an option tttt doesn't know"] += 1
            if a.v:
                print(f"SKIP {name}: {' '.join(args)}: {out.strip().splitlines()[-1] if out.strip() else ''}")
            continue
        # (The exit code: tsc's command emits and tttt's doesn't, but tsc 7's codes don't depend on that, plan 54)
        want_code = scenario[5]
        if expected == actual and code >= 0 and (want_code is None or code == want_code):
            passed.append(name)
            continue
        failed.append(name)
        if code < 0:
            print(f"CRASH {name}   (tttt {' '.join(args)}: signal {-code})")
        if a.names:
            print(name)
            continue
        print(f"FAIL {name}   (tttt {' '.join(args)})")
        if want_code is not None and code >= 0 and code != want_code:
            print(f"   exit code {code}, tsc's {want_code}")
        for k in sorted(expected - actual, key=str):
            print(f"   missing  {k[0]}:{k[1]} TS{k[2]}" + (f" x{(expected - actual)[k]}" if (expected - actual)[k] > 1 else ""))
        for k in sorted(actual - expected, key=str):
            print(f"   spurious {k[0]}:{k[1]} TS{k[2]}" + (f" x{(actual - expected)[k]}" if (actual - expected)[k] > 1 else ""))
        if a.v:
            print("   " + "\n   ".join(ANSI.sub("", out).strip().splitlines()[-40:]))
    print(f"\n{len(passed)} passed, {len(failed)} failed, {sum(skipped.values())} skipped " +
          "(" + ", ".join(f"{v} {k}" for k, v in skipped.most_common()) + ")")


if __name__ == "__main__":
    main()
