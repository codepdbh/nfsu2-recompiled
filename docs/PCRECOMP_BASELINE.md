# Базовое состояние pcrecomp

pcrecomp `35548b8` (upstream HEAD на 2026-10-06), до каких-либо изменений.
Окружение: Windows 11, Python 3.14, capstone 5.0.7, unicorn, MSVC 14.51 x86,
LLVM/clang 22.1.8 (`--target=i686-pc-windows-msvc`, через `scripts/clang32.bat`).

| Тест | Результат |
|---|---|
| `--selftest` для rtti, vtable_scan, disasm32, score_recovery, seed_from_log, emu_unpack, steamstub, generate, lift32_cpu, lift64_cpu, recover, catalog, debug_symbols, map_names, merge_names, pe_analyze, rsrc | всё OK |
| `stdcall_argc --selftest` | **FAIL**: подхватился неполный SDK 10.0.28000.0 (нет `kernel32.lib`) → исправлено в патче 0001, теперь 53/53 |
| test_unlzexe, test_bswap, test_fpu_operands | OK |
| `sse_selftest.py` (нужен C-компилятор) | 59/59 |
| `difftest.py` (lift32 vs Unicorn) | 202/202 |
| C-селфтесты: recomp32_cpu/cpu_selftest, recomp32/flags_selftest, mmx_selftest, hybrid_selftest, native32_selftest | все проходят |
| win32hle_selftest | не слинкован (в моём запуске не хватало остальных исходников); это не сбой тулкита |

После патчей этого проекта: difftest **242/242** (40 новых SIMD-кейсов), все
перечисленные выше селфтесты по-прежнему проходят.

Заметки:
- `difftest.py` покрывает только `lift32.py`; у `lift32_cpu.py` дифференциального покрытия нет.
- На Windows clang по умолчанию собирает под x64; тестам нужны `RECOMP_CC=scripts/clang32.bat` и окружение MSVC x86 (`source scripts/vsenv.sh x86`).
