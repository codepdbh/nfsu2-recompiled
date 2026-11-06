#!/usr/bin/env python3
"""Repeatable on-device race benchmark for the Android port.

Installs an APK (optional), cold-starts the game, walks the frontend menus with
key events (main.cpp maps DPAD/ENTER/ESCAPE to the game's keyboard scancodes),
holds the gas pedal through a touch hold, and collects the runtime's own
per-120-frame statistics from logcat over a fixed window:

  Guest D3D9 presentation <fps> FPS over <s> seconds; bridge ~<ms> ms/frame (present <ms>), <n> calls/frame
  Bridge ms/frame by kind.method: ...; render handovers <n>/frame, waiting <ms> ms/frame
  Audio health: xruns=<n> ...

Medians over the window go to work/bench/<label>-<stamp>.json and one line to
logs/bench.tsv. --perf runs the NDK's simpleperf over the same window and
leaves a symbolized top-N report next to the JSON.

Needs: wireless/USB adb, the Xiaomi "USB debugging (security settings)" switch
(or any device where `adb shell input` is allowed), the phone unlocked with a
long screen timeout. The game process is com.nfsu2.androidevolved:game.

Examples:
  python scripts/android_bench.py --label thunk-cache --apk release/x.apk
  python scripts/android_bench.py --label baseline --menu "sleep:6,key:ENTER,sleep:3" --shots
  python scripts/android_bench.py --label perf --perf --lib android/app/build/intermediates/cxx/Debug/<hash>/obj/arm64-v8a
"""
import argparse, json, os, re, statistics, subprocess, sys, threading, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADB = os.environ.get("ADB", "C:/Android/Sdk/platform-tools/adb.exe")
NDK = os.environ.get("ANDROID_NDK", "C:/Android/Sdk/ndk/28.2.13676358")
PACKAGE = "com.nfsu2.androidevolved"
GAME_PROCESS = PACKAGE + ":game"
ACTIVITY = PACKAGE + "/.GameActivity"

PRESENTATION = re.compile(r"Guest D3D9 presentation ([\d.]+) FPS over ([\d.]+) seconds; bridge ~([\d.]+) ms/frame \(present ([\d.]+)\), (\d+) calls/frame")
HANDOVERS = re.compile(r"render handovers ([\d.]+)/frame, waiting ([\d.]+) ms/frame")
SCHED = re.compile(r"sched (t\d+) hold ([\d.]+) acq ([\d.]+) wait ([\d.]+)")
AUDIO = re.compile(r"Audio health: xruns=(-?\d+)")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", default=os.environ.get("ANDROID_SERIAL", "192.168.3.44:45109"))
    ap.add_argument("--label", required=True, help="name of this configuration (goes into the TSV)")
    ap.add_argument("--apk", help="install this APK first (adb install -r)")
    ap.add_argument("--menu", default=os.environ.get("NFSU2_BENCH_MENU", ""),
                    help="comma list of steps after the frontend is up: key:<KEYCODE>, sleep:<s>, tap:<x>,<y>, shot:<name>")
    ap.add_argument("--boot-timeout", type=float, default=120, help="seconds to wait for the first presentation line")
    ap.add_argument("--settle", type=float, default=8, help="seconds after the last menu step before measuring")
    ap.add_argument("--race-seconds", type=float, default=30, help="measurement window; gas is held for settle+window")
    ap.add_argument("--gas", default="2536,998", help="screen x,y of the gas pedal in landscape (touch hold)")
    ap.add_argument("--no-gas", action="store_true", help="do not hold the gas pedal (menus, garage)")
    ap.add_argument("--no-launch", action="store_true", help="measure whatever is running now; skip install/launch/menu")
    ap.add_argument("--keep", action="store_true", help="leave the game running afterwards")
    ap.add_argument("--shots", action="store_true", help="screenshot after every menu step (calibration)")
    ap.add_argument("--perf", action="store_true", help="run simpleperf over the window")
    ap.add_argument("--lib", help="directory with the unstripped libnfsu2_android.so for simpleperf symbolization")
    ap.add_argument("--repeat", type=int, default=1)
    args = ap.parse_args()

    out = ROOT / "work" / "bench"
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for run in range(args.repeat):
        results.append(one_run(args, out, run))
    if args.repeat > 1:
        print("\n== medians over runs:", json.dumps({k: median([r[k] for r in results if r.get(k) is not None]) for k in ("fps", "waiting_ms", "bridge_ms", "calls")}))


def one_run(args, out: Path, run: int) -> dict:
    stamp = time.strftime("%Y%m%d-%H%M%S")
    label = args.label if args.repeat == 1 else f"{args.label}-{run + 1}"
    if not args.no_launch:
        if args.apk:
            print(f"== installing {args.apk}")
            adb(args, "install", "-r", str(args.apk), check=True, timeout=1200)  # 140 MB over a slow Wi-Fi link
        adb(args, "shell", "am", "force-stop", PACKAGE)
        time.sleep(1.5)
        adb(args, "logcat", "-c")
        wake(args)
        print("== launching", ACTIVITY)
        adb(args, "shell", "am", "start", "-n", ACTIVITY, check=True)
        pid = wait_for_pid(args, 30)
        print("== game pid", pid, "- waiting for the frontend")
        if not wait_for_line(args, pid, PRESENTATION, args.boot_timeout):
            dump_crash(args)
            sys.exit("the game never presented a frame")
        run_menu(args, out, label)
        print(f"== settling {args.settle}s")
    pid = wait_for_pid(args, 10)
    gas_thread = None
    if not args.no_gas:
        gas_thread = threading.Thread(target=hold_gas, args=(args, args.settle + args.race_seconds + 2), daemon=True)
        gas_thread.start()
    time.sleep(args.settle)

    xruns_before = last_xruns(args, pid)
    start = device_epoch(args)
    perf_thread = None
    perf_data = out / f"perf-{label}-{stamp}.data"
    if args.perf:
        perf_thread = threading.Thread(target=run_simpleperf, args=(args, perf_data, args.race_seconds), daemon=True)
        perf_thread.start()
    print(f"== measuring {args.race_seconds}s")
    time.sleep(args.race_seconds)
    end = device_epoch(args)
    if perf_thread:
        perf_thread.join(timeout=args.race_seconds + 60)
    samples = collect(args, pid, start, end)
    xruns_after = last_xruns(args, pid)
    if gas_thread:
        gas_thread.join(timeout=5)
    if not args.keep and not args.no_launch:
        adb(args, "shell", "am", "force-stop", PACKAGE)

    summary = summarize(samples, xruns_before, xruns_after)
    summary.update(label=label, apk=args.apk or "", stamp=stamp, window_s=args.race_seconds)
    (out / f"{label}-{stamp}.json").write_text(json.dumps({"summary": summary, "samples": samples}, indent=1))
    append_tsv(summary)
    print("== result:", json.dumps(summary))
    if not samples:
        print("!! no presentation lines in the window - was the game in a race? check work/bench/*.png")
        dump_crash(args)
    if args.perf and perf_data.exists():
        report_simpleperf(args, perf_data, out / f"perf-{label}-{stamp}.txt")
    return summary


# --- device helpers --------------------------------------------------------

def adb(args, *cmd, check=False, capture=True, timeout=120):
    full = [ADB, "-s", args.serial, *cmd]
    r = subprocess.run(full, capture_output=capture, text=True, timeout=timeout, encoding="utf-8", errors="replace")
    if check and r.returncode:
        sys.exit(f"adb {' '.join(cmd)} failed: {r.stderr.strip() or r.stdout.strip()}")
    return r.stdout if capture else ""


def wake(args):
    adb(args, "shell", "input", "keyevent", "KEYCODE_WAKEUP")
    adb(args, "shell", "wm", "dismiss-keyguard")
    focus = adb(args, "shell", "dumpsys", "window")
    m = re.search(r"mCurrentFocus=Window\{[^ ]+ u0 ([^}]+)\}", focus)
    if m and ("NotificationShade" in m.group(1) or "Keyguard" in m.group(1)):
        sys.exit("the phone is locked: unlock it (and raise the screen timeout) before benchmarking")


def device_epoch(args) -> float:
    return float(adb(args, "shell", "date", "+%s.%N").strip() or time.time())


def wait_for_pid(args, timeout: float) -> int:
    until = time.time() + timeout
    while time.time() < until:
        pid = adb(args, "shell", "pidof", GAME_PROCESS).strip()
        if pid:
            return int(pid.split()[0])
        time.sleep(0.5)
    sys.exit(f"{GAME_PROCESS} did not start")


def wait_for_line(args, pid: int, pattern: re.Pattern, timeout: float) -> bool:
    until = time.time() + timeout
    while time.time() < until:
        log = adb(args, "logcat", "-d", f"--pid={pid}", "-s", "NFSU2:*", "NFSMW:*")
        if pattern.search(log):
            return True
        if not adb(args, "shell", "pidof", GAME_PROCESS).strip():
            print("!! the game process died while booting")
            return False
        time.sleep(2)
    return False


def run_menu(args, out: Path, label: str):
    for i, step in enumerate(s for s in args.menu.split(",") if s.strip()):
        kind, _, value = step.strip().partition(":")
        if kind == "key":
            adb(args, "shell", "input", "keyevent", value)
        elif kind == "sleep":
            time.sleep(float(value))
        elif kind == "tap":
            x, y = value.split(",")
            adb(args, "shell", "input", "tap", x, y)
        elif kind == "hold":  # hold:x/y/ms - a timed touch (menus that need a held confirm)
            x, y, ms = value.split("/")
            adb(args, "shell", "input", "swipe", x, y, x, y, ms)
        elif kind == "shot":
            screenshot(args, out / f"{label}-{value}.png")
        else:
            sys.exit(f"unknown menu step {step!r}")
        if args.shots and kind != "shot":
            screenshot(args, out / f"{label}-step{i:02d}-{kind}-{value.replace(',', '_')}.png")


def screenshot(args, path: Path):
    r = subprocess.run([ADB, "-s", args.serial, "exec-out", "screencap", "-p"], capture_output=True, timeout=30)
    path.write_bytes(r.stdout)
    print("   screenshot", path.name, len(r.stdout), "bytes")


def hold_gas(args, seconds: float):
    # `input swipe` holds a touch at one point for its duration; the OS caps a
    # single injected event stream, so hold in chunks and re-press.
    x, y = args.gas.split(",")
    deadline = time.time() + seconds
    while time.time() < deadline:
        chunk = int(min(10, max(0.1, deadline - time.time())) * 1000)
        subprocess.run([ADB, "-s", args.serial, "shell", "input", "swipe", x, y, x, y, str(chunk)], capture_output=True, timeout=chunk / 1000 + 15)


def collect(args, pid: int, start: float, end: float) -> list:
    log = adb(args, "logcat", "-v", "epoch", "-d", f"--pid={pid}", "-s", "NFSU2:*", "NFSMW:*")
    samples, current = [], None
    for line in log.splitlines():
        m = re.match(r"\s*([\d.]+)", line)
        if not m:
            continue
        t = float(m.group(1))
        if t < start or t > end:
            continue
        p = PRESENTATION.search(line)
        if p:
            current = dict(t=t, fps=float(p.group(1)), bridge_ms=float(p.group(3)), present_ms=float(p.group(4)), calls=int(p.group(5)))
            samples.append(current)
            continue
        h = HANDOVERS.search(line)
        if h and current is not None:
            current.update(handovers=float(h.group(1)), waiting_ms=float(h.group(2)))
            current["sched"] = {s.group(1): dict(hold=float(s.group(2)), acq=float(s.group(3)), wait=float(s.group(4))) for s in SCHED.finditer(line)}
    return samples


def last_xruns(args, pid: int):
    log = adb(args, "logcat", "-d", f"--pid={pid}", "-s", "NFSMW:*")
    hits = AUDIO.findall(log)
    return int(hits[-1]) if hits else None


def dump_crash(args):
    crash = adb(args, "logcat", "-b", "crash", "-d")
    tail = crash.strip().splitlines()[-25:]
    if tail:
        print("!! crash buffer tail:\n   " + "\n   ".join(tail))


# --- results ---------------------------------------------------------------

def median(values):
    values = [v for v in values if v is not None]
    return round(statistics.median(values), 2) if values else None


def summarize(samples, xruns_before, xruns_after) -> dict:
    s = dict(samples=len(samples))
    for key in ("fps", "waiting_ms", "bridge_ms", "present_ms", "calls", "handovers"):
        s[key] = median([x.get(key) for x in samples])
    s["fps_min"] = min((x["fps"] for x in samples), default=None)
    s["fps_max"] = max((x["fps"] for x in samples), default=None)
    s["xruns"] = (xruns_after - xruns_before) if xruns_before is not None and xruns_after is not None else None
    threads = {}
    for x in samples:
        for tid, v in x.get("sched", {}).items():
            threads.setdefault(tid, {"hold": [], "acq": [], "wait": []})
            for k in ("hold", "acq", "wait"):
                threads[tid][k].append(v[k])
    s["sched"] = {tid: {k: median(v[k]) for k in v} for tid, v in threads.items()}
    return s


def append_tsv(s: dict):
    tsv = ROOT / "logs" / "bench.tsv"
    tsv.parent.mkdir(exist_ok=True)
    header = "stamp\tlabel\tfps\tfps_min\tfps_max\twaiting_ms\thandovers\tbridge_ms\tcalls\txruns\tsamples\tapk\n"
    if not tsv.exists():
        tsv.write_text(header)
    with tsv.open("a") as f:
        f.write("\t".join(str(s.get(k, "")) for k in ("stamp", "label", "fps", "fps_min", "fps_max", "waiting_ms", "handovers", "bridge_ms", "calls", "xruns", "samples", "apk")) + "\n")


# --- simpleperf --------------------------------------------------------------
# Driven by hand rather than through app_profiler.py: that script pulls every
# library of the process into a binary_cache over adb (minutes over Wi-Fi) and
# hung outright here. `record --app` on a debuggable app needs no root.

DEVICE_SIMPLEPERF = "/data/local/tmp/simpleperf"
DEVICE_DATA = "/data/local/tmp/nfsu2-perf.data"


def run_simpleperf(args, data: Path, seconds: float):
    if "pushed" not in adb(args, "shell", "ls", DEVICE_SIMPLEPERF):
        adb(args, "push", f"{NDK}/simpleperf/bin/android/arm64/simpleperf", DEVICE_SIMPLEPERF, timeout=300)
        adb(args, "shell", "chmod", "755", DEVICE_SIMPLEPERF)
    adb(args, "shell", "rm", "-f", DEVICE_DATA)
    out = adb(args, "shell", DEVICE_SIMPLEPERF, "record", "--app", PACKAGE, "-e", "task-clock:u", "-f", "1000",
              "--duration", str(int(seconds)), "-g", "-o", DEVICE_DATA, timeout=seconds + 120)
    if "Samples recorded" not in out and "samples" not in out.lower():
        print("!! simpleperf record said:", out.strip()[-600:])
    adb(args, "pull", DEVICE_DATA, str(data), timeout=600)


def report_simpleperf(args, data: Path, text: Path):
    exe = f"{NDK}/simpleperf/bin/windows/x86_64/simpleperf.exe"
    sym = ["--symdir", args.lib] if args.lib else []
    sections = []
    for title, sort, limit in (("threads", "comm", 40), ("thread,dso", "comm,dso", 60), ("thread,symbol", "comm,symbol", 400), ("symbol", "symbol", 200)):
        r = subprocess.run([exe, "report", "-i", str(data), "--sort", sort, "-n", *sym], capture_output=True, text=True, timeout=900, encoding="utf-8", errors="replace")
        body = (r.stdout or r.stderr).splitlines()
        sections.append(f"=== by {title} ===\n" + "\n".join(body[:limit]))
    text.write_text("\n\n".join(sections))
    print("== simpleperf report:", text)
    for line in sections[0].splitlines()[:14] + ["..."] + sections[3].splitlines()[:40]:
        print("   " + line)


if __name__ == "__main__":
    main()
