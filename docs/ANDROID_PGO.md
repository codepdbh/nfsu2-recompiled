# Profile-guided builds (PGO) for Android

On a Xiaomi 15T (Dimensity 8400, Cortex-A720) building the lifted game code against a
profile of a real run is worth more than every hand optimisation around it: **39.0 against
31.1 frames per second**, and that at a slightly heavier scene (52.1k against 48.7k Direct3D 9
calls per frame). Those are medians of the two-second samples whose workload falls in one
band, 24 of them against 16, taken by `scripts/bench_compare.py` over several races of each
build; comparing whole runs is useless here because a race frame costs 33k calls on an empty
street and 50k in traffic. The render thread's wait for the machine lock drops from 7.3 to
5.1 ms per frame.

The reason is the shape of the generated code: 27k lifted functions, each branching on
every x86 condition and calling out through `nfs_icall_resolve`. Without a profile the
compiler cannot tell which side of those branches is hot and lays them out alike. On
2004 x86 code, where the hot D3DX state-apply loop (`sub_00669296`) runs 109 million times
in one race, that costs a lot.

The profile is **not kept in the repository**. Like the generated C itself it is derived
from SPEED2.EXE (lifted function names plus their block counters), so it is collected
again with the steps below.

## Collecting a profile

An instrumented build (`-fprofile-generate -fprofile-update=atomic`; atomic counters are
required because host threads — the AAudio callback, the hang watchdog, the timer worker —
run alongside the guest):

```
cd android
./gradlew assembleDebug -PnfsCpu=cortex-a720 -PnfsFast -PnfsPgo=generate
```

That build takes about 17 minutes, the APK grows to 194 MB and the frame rate drops to
roughly 30. All three are expected and none of them affect the result.

The run writes its counters on request: once a second the runtime looks for the file
`<app external files>/pgo/dump` and, when it appears, writes `default_<hash>.profraw` from
a separate thread, so the one or two seconds it takes never stall a frame. The `%m` in the
name merges every run into one file.

```
python scripts/android_bench.py --label pgo-gen \
  --apk <instrumented>.apk --menu "$(cat work/bench/menu.txt)" \
  --settle 4 --race-seconds 40 --pgo-dump work/pgo
```

Collect the frontend as well as a race (`--no-gas` through the menus and the garage),
otherwise the menu code is left without a profile. The set used here: two 40-second races
plus one pass through the menus.

## Building against the profile

```
<ndk>/toolchains/llvm/prebuilt/<host>/bin/llvm-profdata merge \
  -output=work/pgo/nfsu2.profdata work/pgo/*.profraw

cd android
./gradlew assembleDebug -PnfsCpu=cortex-a720 -PnfsFast \
  -PnfsPgo=use -PnfsPgoProfile=<absolute path>/work/pgo/nfsu2.profdata
```

The APK grows from 141 to 161 MB: given the profile, the compiler inlines hot calls more
aggressively.

## What has to match

A profile is keyed by function name and by a control-flow hash taken **before** ThinLTO.
When that hash does not match, the profile for the function is dropped silently and the
build looks successful without being optimised. So `generate` and `use` must agree on:

- `NFS_GUEST_FAST` (it changes `RECOMP_BACKEDGE` in `arm64_guest.h`, so every loop's graph);
- `NFS_GUEST_OPT` and the rest of the flags the lifted code is compiled with;
- the lifter output (`src/recomp/gen/`), meaning the SPEED2.EXE build and `config/`.

Changing `-mcpu` does not change the graph, but it is still not worth changing without
collecting the profile again. A portable build is made without `NFS_GUEST_FAST` and
therefore needs its **own** profile, collected from a portable instrumented build.

To check that a profile really covers the game code:

```
llvm-profdata show --all-functions work/pgo/nfsu2.profdata | grep -c "^  sub_"
```

That should print roughly 27k functions.
