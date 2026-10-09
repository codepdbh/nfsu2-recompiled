#!/usr/bin/env python3
"""Differential test of REAL SPEED2.EXE functions: Unicorn vs the lifted C.

  difftest_real.py <analysis.exe> <catalog.json> [--n 400] [--seed 1] [--fp-only] [--sse-only]
                   [--func 0xVA ...] [--cases 3] [--out work/difftest]

For each selected leaf function (no calls, no indirect jumps, no fs:, no
3DNow!, no tail jumps out of its body):

  * the original bytes run in Unicorn with the real image mapped at 0x400000;
  * the same function, lifted by this project's lift driver (lift32 + simd32,
    precise_carry), is compiled for an x64 host with ADDR(va) = va + g_mem_base
    (the portable, non-flat memory model) and run from identical state.

Initial state per case: random registers (small ints, pointers into a random
scratch region, large values), a stack whose slots point into scratch, the
image's .data as in the file. Compared afterwards: eax ecx edx ebx esi edi ebp
esp, x87 depth and st(0) (relative 1e-9), and every byte changed in .data,
scratch and stack. A case where either machine faults is skipped unless only
one of them faults.

Output: work/difftest/report.json and a summary on stdout.
"""
import argparse, json, os, random, struct, subprocess, sys, math

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run_lift  # noqa: E402  (sets up pcrecomp paths)
from capstone import Cs, CS_ARCH_X86, CS_MODE_32  # noqa: E402
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UcError  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

BASE = 0x400000
SCRATCH, SCRATCH_SZ = 0x10000000, 0x100000
STACK_LO, STACK_SZ = 0x20000000, 0x10000
ESP0 = STACK_LO + 0x8000
RET = 0xDEAD0000
REGS = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi")
UCR = {"eax": X.UC_X86_REG_EAX, "ecx": X.UC_X86_REG_ECX, "edx": X.UC_X86_REG_EDX, "ebx": X.UC_X86_REG_EBX,
       "esp": X.UC_X86_REG_ESP, "ebp": X.UC_X86_REG_EBP, "esi": X.UC_X86_REG_ESI, "edi": X.UC_X86_REG_EDI}


def f80(m, se):
    """80-bit extended (mantissa with explicit integer bit, sign|exponent) -> float."""
    sign = -1.0 if se & 0x8000 else 1.0
    e = se & 0x7FFF
    if e == 0x7FFF:
        return float("nan") if (m << 1) & 0xFFFFFFFFFFFFFFFF else sign * float("inf")
    if m == 0:
        return sign * 0.0
    try:
        return sign * math.ldexp(m, e - 16383 - 63)
    except OverflowError:
        return sign * float("inf")


def select(cat, img, args):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    out = []
    for f in cat["functions"]:
        if f["entry_kind"] != "start" or f["calls_to"] or not (6 <= f["size"] <= 3000):
            continue
        lo, hi = f["address"], f["end"]
        ok, nfp, nsse = True, 0, 0
        for s, e, *_ in f["blocks"]:
            for i in md.disasm(img[s - BASE:e - BASE], s):
                m = i.mnemonic
                if m == "call" or "fs:" in i.op_str or m.startswith("pf") or m in (
                        "int", "int3", "rdtsc", "cpuid", "femms", "in", "out", "hlt", "pswapd", "pi2fd", "pf2id"):
                    ok = False
                if m == "jmp" or (m.startswith("j") and i.op_str.startswith("0x")):
                    if not i.op_str.startswith("0x"):
                        ok = False
                    elif not (lo <= int(i.op_str, 16) < hi):
                        ok = False
                if m.startswith("f"): nfp += 1
                if "xmm" in i.op_str: nsse += 1
        if not ok:
            continue
        if args.fp_only and not nfp: continue
        if args.sse_only and not nsse: continue
        out.append(f)
    return out


def mk_cases(rng, ncases):
    cases = []
    for _ in range(ncases):
        regs = {}
        for r in REGS:
            k = rng.random()
            if r == "esp":
                regs[r] = ESP0
            elif k < 0.45:
                regs[r] = SCRATCH + rng.randrange(0, SCRATCH_SZ - 0x2000, 16)
            elif k < 0.8:
                regs[r] = rng.randrange(0, 64)
            else:
                regs[r] = rng.getrandbits(32)
        cases.append(regs)
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe"); ap.add_argument("catalog")
    ap.add_argument("--n", type=int, default=400); ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--cases", type=int, default=3)
    ap.add_argument("--fp-only", action="store_true"); ap.add_argument("--sse-only", action="store_true")
    ap.add_argument("--func", nargs="*", default=[])
    ap.add_argument("--local-regs", action="store_true", help="compile with RECOMP_LOCAL_REGS")
    ap.add_argument("--x87-window", action="store_true", help="compile with RECOMP_X87_WINDOW")
    ap.add_argument("--cc", default="msvc", choices=("msvc", "clang-o2"),
                    help="msvc: cl /O1 (the Windows build); clang-o2: clang-cl -O2 -fwrapv "
                         "-fno-strict-aliasing with non-volatile guest memory (the Android build's flags)")
    ap.add_argument("--out", default=os.path.join(HERE, "..", "..", "work", "difftest"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    import pefile
    pe = pefile.PE(args.exe, fast_load=True)
    img = bytearray(pe.get_memory_mapped_image())
    cat = json.load(open(args.catalog))
    rng = random.Random(args.seed)
    pool = select(cat, img, args)
    if args.func:
        want = {int(x, 16) for x in args.func}
        funcs = [f for f in cat["functions"] if f["address"] in want]
    else:
        rng.shuffle(pool)
        funcs = sorted(pool[:args.n], key=lambda f: f["address"])
    print(f"[*] {len(pool)} eligible leaf functions, testing {len(funcs)} x {args.cases} cases")

    # --- memory: image (.data writable), scratch, stack ---
    scratch = bytearray(rng.getrandbits(8) for _ in range(SCRATCH_SZ))
    # make some scratch dwords valid pointers into scratch, and some small floats
    for k in range(0, SCRATCH_SZ, 64):
        struct.pack_into("<I", scratch, k, SCRATCH + rng.randrange(0, SCRATCH_SZ - 0x2000, 16))
        struct.pack_into("<f", scratch, k + 4, rng.uniform(-100, 100))
        struct.pack_into("<d", scratch, k + 8, rng.uniform(-1000, 1000))
    stack = bytearray(STACK_SZ)
    for k in range(0, STACK_SZ, 4):
        struct.pack_into("<I", stack, k, SCRATCH + rng.randrange(0, SCRATCH_SZ - 0x2000, 16) if k % 8 == 0
                         else rng.randrange(0, 256))
    open(os.path.join(args.out, "image.bin"), "wb").write(img)
    open(os.path.join(args.out, "scratch.bin"), "wb").write(scratch)
    open(os.path.join(args.out, "stack.bin"), "wb").write(stack)

    # --- lift ---
    from lift32 import Lifter
    from pe_analyze import analyze_pe, build_iat_map
    info = analyze_pe(args.exe)
    lifter = Lifter(iat_map=build_iat_map(info), func_names={}, lifted={f["address"] for f in funcs},
                    precise_carry=True)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    bodies = []
    for f in funcs:
        fn = run_lift.rebuild(md, img, BASE, f)
        bodies.append(lifter.lift_function(fn))
    cases = []
    for fi, f in enumerate(funcs):
        for regs in mk_cases(rng, args.cases):
            cases.append((fi, regs))

    # --- C harness (x64 host, non-flat memory) ---
    rt = os.path.normpath(os.path.join(run_lift.PCRECOMP, "runtime", "recomp32"))
    c = [r'''/* generated by difftest_real.py */
#define RECOMP_GENERATED_CODE
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "recomp_types.h"
uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
#ifndef RECOMP_X87_WINDOW
double g_st[8];
#endif
int g_fp_top; uint16_t g_fpu_cw = 0x027F;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint64_t g_mm[8]; V128 g_xmm[8]; uint32_t g_mxcsr = 0x1F80;
uint32_t g_fs_base, g_gs_base, g_cur_func; ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE], g_icall_trace_idx, g_icall_count;
const char *g_cur_import;
void recomp_dump_trace(const char *w) { (void)w; }
void recomp_hook(uint32_t va) { (void)va; }
void recomp_unimpl(uint32_t va, const char *w) { RaiseException(0xE0000001, 0, 0, 0); }
recomp_func_t recomp_lookup(uint32_t va) { return 0; }
recomp_func_t recomp_lookup_manual(uint32_t va) { return 0; }
recomp_func_t recomp_lookup_import(uint32_t va) { return 0; }
const recomp_dispatch_entry_t recomp_dispatch_table[1]; const uint32_t recomp_dispatch_count = 0;
/* loop budget: RECOMP_BACKEDGE calls this every RECOMP_YIELD_EVERY back-edges */
static int g_budget;
static void budget_hook(void) { if (++g_budget > 40) RaiseException(0xE0000002, 0, 0, 0); }
''']
    c += [b.replace("void sub_", "static void sub_", 1) for b in bodies]
    c.append("typedef void (*fn_t)(void);\nstatic const fn_t k_funcs[] = {" +
             ",".join(f"sub_{f['address']:08X}" for f in funcs) + "};\n")
    c.append(r'''
static uint8_t *load(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); *n = ftell(f);
  fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
#define M(va) ((uint8_t *)(g_mem_base + (uintptr_t)(va)))
static void commit(uint32_t va, uint32_t n) { VirtualAlloc(M(va), n, MEM_COMMIT, PAGE_READWRITE); }
int main(int argc, char **argv) {
  uint8_t *base = VirtualAlloc(NULL, 0x100000000ull, MEM_RESERVE, PAGE_NOACCESS);
  g_mem_base = (ptrdiff_t)base;
  recomp_yield_hook = budget_hook;
  size_t ni, ns, nk; uint8_t *img = load(argv[1], &ni), *scr = load(argv[2], &ns), *stk = load(argv[3], &nk);
  commit(0x400000, (uint32_t)ni); commit(0x10000000, (uint32_t)ns); commit(0x20000000, (uint32_t)nk);
  FILE *cf = fopen(argv[4], "r"); int fi; unsigned r[8];
  while (fscanf(cf, "%d %x %x %x %x %x %x %x %x", &fi, &r[0], &r[1], &r[2], &r[3], &r[4], &r[5], &r[6], &r[7]) == 9) {
    memcpy(M(0x400000), img, ni); memcpy(M(0x10000000), scr, ns); memcpy(M(0x20000000), stk, nk);
    g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3]; g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
#ifdef RECOMP_X87_WINDOW
    g_fp_sp = RECOMP_X87_SLOTS / 2;
#endif
    memset(g_st, 0, 8 * sizeof(double)); g_fp_top = 0; g_fpu_cw = 0x027F; memset(g_xmm, 0, sizeof g_xmm); memset(g_mm, 0, sizeof g_mm);
    g_flag_k = g_flag_a = g_flag_b = g_flag_cf = 0;
    g_esp -= 4; *(uint32_t *)M(g_esp) = 0xDEAD0000u;
    int fault = 0; g_budget = 0; g_backedges = 0;
    __try { k_funcs[fi](); } __except (1) { fault = 1; }
    printf("C %d %d %08X %08X %08X %08X %08X %08X %08X %08X %d %.17g\n", fi, fault, g_eax, g_ecx, g_edx, g_ebx,
           g_esp, g_ebp, g_esi, g_edi, g_fp_top & 7, g_st[0]);
    if (!fault) {
      struct { uint32_t va; uint8_t *ref; size_t n; } rg[3] = {{0x400000, img, ni}, {0x10000000, scr, ns}, {0x20000000, stk, nk}};
      int nd = 0;
      for (int k = 0; k < 3; k++) for (size_t o = 0; o < rg[k].n; o++)
        if (M(rg[k].va)[o] != rg[k].ref[o] && !(rg[k].va == 0x20000000 && rg[k].va + o >= r[4] - 4 - 0x2000 && rg[k].va + o < r[4]) && nd < 64) {
          printf("D %08X %02X\n", (unsigned)(rg[k].va + o), M(rg[k].va)[o]); nd++; }
    }
    printf("E\n");
  }
  return 0;
}
''')
    cpath = os.path.join(args.out, "difftest_real.c")
    open(cpath, "w").write("\n".join(c))
    with open(os.path.join(args.out, "cases.txt"), "w") as f:
        for fi, regs in cases:
            f.write("%d %s\n" % (fi, " ".join("%x" % regs[r] for r in ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"))))
    exe = os.path.join(args.out, "difftest_real.exe")
    if args.cc == "clang-o2":
        cmd = ["clang-cl", "-nologo", "/O2", "-w", "/clang:-fwrapv", "/clang:-fno-strict-aliasing",
               "/clang:-ffp-contract=off", "-DRECOMP_MEM_QUAL=", "-I" + rt, cpath, "-Fe" + exe, "-Fo" + args.out + os.sep]
    else:
        cmd = ["cl", "-nologo", "-O1", "-w", "-bigobj", "-I" + rt, cpath, "-Fe" + exe, "-Fo" + args.out + os.sep]
    if args.local_regs:
        cmd.insert(2, "-DRECOMP_LOCAL_REGS")
    if args.x87_window:
        cmd.insert(2, "-DRECOMP_X87_WINDOW")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-3000:]); sys.exit("compile failed (run from an x64 MSVC environment: source scripts/vsenv.sh x64)")
    out = subprocess.run([exe, os.path.join(args.out, "image.bin"), os.path.join(args.out, "scratch.bin"),
                          os.path.join(args.out, "stack.bin"), os.path.join(args.out, "cases.txt")],
                         capture_output=True, text=True).stdout.splitlines()
    lifted, cur = [], None
    for line in out:
        p = line.split()
        if p[0] == "C":
            cur = {"fault": p[2] == "1", "regs": [int(x, 16) for x in p[3:11]], "fpd": int(p[11]),
                   "st0": float("nan") if "nan" in p[12].lower() else float(p[12]), "mem": {}}
        elif p[0] == "D":
            cur["mem"][int(p[1], 16)] = int(p[2], 16)
        elif p[0] == "E":
            lifted.append(cur)

    # --- Unicorn ---
    results = {"tested": 0, "passed": 0, "failed": 0, "skipped": 0, "failures": []}
    per_func = {}
    for (fi, regs), lc in zip(cases, lifted):
        f = funcs[fi]
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(BASE, (len(img) + 0xFFF) & ~0xFFF); mu.mem_write(BASE, bytes(img))
        mu.mem_map(SCRATCH, SCRATCH_SZ); mu.mem_write(SCRATCH, bytes(scratch))
        mu.mem_map(STACK_LO, STACK_SZ); mu.mem_write(STACK_LO, bytes(stack))
        mu.mem_map(RET, 0x1000)
        for rn in REGS: mu.reg_write(UCR[rn], regs[rn])
        mu.reg_write(X.UC_X86_REG_ESP, ESP0 - 4); mu.mem_write(ESP0 - 4, struct.pack("<I", RET))
        mu.reg_write(X.UC_X86_REG_FPCW, 0x027F)
        ufault = False
        try:
            mu.emu_start(f["address"], RET, count=3_000_000)
            if mu.reg_read(X.UC_X86_REG_EIP) != RET:
                results["skipped"] += 1; per_func.setdefault(f["address"], {"pass": 0, "fail": 0, "skip": 0})["skip"] += 1
                continue                                    # instruction budget: inconclusive
        except UcError:
            ufault = True
        st = per_func.setdefault(f["address"], {"pass": 0, "fail": 0, "skip": 0})
        if ufault and lc["fault"]:
            results["skipped"] += 1; st["skip"] += 1; continue
        results["tested"] += 1
        why = []
        if ufault != lc["fault"]:
            why.append(f"fault: unicorn={ufault} lifted={lc['fault']}")
        else:
            ur = [mu.reg_read(UCR[rn]) for rn in REGS]
            for k, rn in enumerate(REGS):
                if ur[k] != lc["regs"][k]: why.append(f"{rn}: cpu {ur[k]:08X} lifted {lc['regs'][k]:08X}")
            sw = mu.reg_read(X.UC_X86_REG_FPSW); top = (sw >> 11) & 7; depth = (8 - top) & 7
            if depth != lc["fpd"]: why.append(f"x87 depth: cpu {depth} lifted {lc['fpd']}")
            elif depth:
                st0 = mu.reg_read(X.UC_X86_REG_FP0 + top)   # FPn is the PHYSICAL register Rn; st(0) is R[TOP]
                v = f80(st0[0], st0[1]) if isinstance(st0, tuple) else None
                if v is not None and not (math.isclose(v, lc["st0"], rel_tol=1e-9, abs_tol=1e-300) or (v != v and lc["st0"] != lc["st0"])):
                    why.append(f"st0: cpu {v!r} lifted {lc['st0']!r}")
            # memory: compare every byte either side changed
            changed = {}
            for va, ref, n in ((BASE, img, len(img)), (SCRATCH, scratch, SCRATCH_SZ), (STACK_LO, stack, STACK_SZ)):
                cur_m = mu.mem_read(va, n)
                if va == STACK_LO:
                    lo_ = ESP0 - 4 - 0x2000 - va; cur_m[lo_:ESP0 - va] = ref[lo_:ESP0 - va]
                if cur_m != ref:
                    for o in range(n):
                        if cur_m[o] != ref[o]: changed[va + o] = cur_m[o]
            for a in sorted(set(changed) | set(lc["mem"]))[:8]:
                if changed.get(a) != lc["mem"].get(a):
                    why.append(f"mem {a:08X}: cpu {changed.get(a)} lifted {lc['mem'].get(a)}")
        if why:
            results["failed"] += 1; st["fail"] += 1
            if len(results["failures"]) < 300:
                results["failures"].append({"func": f"0x{f['address']:08X}", "regs": {k: f"{v:08X}" for k, v in regs.items()},
                                            "why": why[:6]})
        else:
            results["passed"] += 1; st["pass"] += 1
    results["functions"] = len(funcs)
    results["functions_all_pass"] = sum(1 for v in per_func.values() if v["fail"] == 0 and v["pass"])
    results["functions_with_failures"] = sorted(f"0x{k:08X}" for k, v in per_func.items() if v["fail"])
    json.dump(results, open(os.path.join(args.out, "report.json"), "w"), indent=1)
    print(json.dumps({k: v for k, v in results.items() if k != "failures"}, indent=1)[:3000])
    for fl in results["failures"][:25]:
        print(fl["func"], "; ".join(fl["why"][:3]))


if __name__ == "__main__":
    main()
