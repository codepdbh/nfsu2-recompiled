# Разведка NFSU2 (SPEED2.EXE)

Создано `scripts/recon.py` по диапазонам кода из каталога disasm32
(не простым линейным проходом, поэтому данные внутри `.text` исключены).

## Код

- Инструкций в каталогизированном коде: **1,131,558**
- Функций: 27,742 (см. FUNCTION_RECOVERY.md)

| Вызовы | количество |
|---|---|
| прямые | 62,387 |
| через регистр | 432 |
| mem[reg+disp] (vtable/COM) | 4,961 |
| mem прочее | 409 |
| iat | 584 |

| Переходы | количество |
|---|---|
| прямые | 24,129 |
| через регистр | 10 |
| mem прочее | 183 |
| table [idx*4+disp] | 527 |
| iat thunk | 45 |

Таблицы переходов (`jmp [idx*4+table]`): 527. Косвенные вызовы через
`[reg+disp]` (vtables / COM): 4961.

## Возможности CPU

| Класс | инструкций |
|---|---|
| x87 | 111,375 |
| mmx | 16,752 |
| sse | 3,619 |
| sse2 | 2,506 |
| 3dnow | 2,709 |

| Префикс / сегмент | количество |
|---|---|
| rep/repe | 2,732 |
| opsize16 | 8,115 |
| repne | 3 |
| fs | 3,636 |
| gs | 33 |
| cs/ss/ds/es | 4 |

Редкие инструкции (большинство срабатываний `int3`/BCD/`arpl`/`bound`
приходится на одну ложную функцию по адресу 0x6D5610 и на padding):

`int3` 272, `femms` 133, `emms` 31, `cpuid` 27, `int` 20, `pushal` 13, `rdtsc` 12, `popal` 12, `bound` 11, `std` 9, `aaa` 6, `out` 5, `enter` 4, `iretd` 4, `aas` 4, `in` 3, `xlatb` 3, `hlt` 2, `arpl` 2, `into` 1, `aad` 1, `daa` 1, `das` 1

Мнемоники x87: `fld` 31455, `fstp` 21431, `fmul` 18395, `faddp` 6947, `fadd` 5936, `fnstsw` 5734, `fcomp` 4422, `fsub` 4163, `fild` 2525, `fxch` 2158, `fst` 1376, `fcom` 873, `fchs` 780, `fldz` 644, `fsubr` 628, `fdiv` 598, `fabs` 477, `fsubp` 435, `fsqrt` 315, `fdivr` 290, `fucompp` 259, `fmulp` 242, `fld1` 211, `fdivp` 195, `fcompp` 175, `femms` 133, `fistp` 127, `fldcw` 52, `fimul` 45, `fidiv` 35, `fnstcw` 35, `fyl2x` 23, `fldln2` 21, `fdivrp` 19, `fsubrp` 18, `fiadd` 15, `fist` 15, `fisub` 13, `ffree` 13, `fsin` 11, `fpatan` 11, `fsincos` 9, `fscale` 8, `fcos` 7, `frndint` 7, `fprem` 7, `fisubr` 6, `ftst` 4, `fxam` 3, `fnsave` 2, `fnclex` 2, `fldpi` 2, `fldl2e` 2, `f2xm1` 2, `fnstenv` 2, `fldenv` 2, `fprem1` 2, `fcmovb` 1, `fidivr` 1, `fptan` 1, `frstor` 1, `fldlg2` 1

Мнемоники SIMD: `movq` 5659, `paddw` 1643, `pfmul` 1327, `movaps` 915, `pfadd` 758, `psubw` 684, `punpckhdq` 593, `movd` 577, `punpckldq` 570, `movdqa` 566, `mulps` 551, `pmullw` 547, `punpcklbw` 507, `psraw` 499, `paddsw` 449, `shufps` 418, `pand` 386, `movss` 382, `pxor` 377, `addps` 367, `por` 278, `paddd` 275, `psubsw` 262, `pmaddwd` 257, `punpckhbw` 239, `pmulhw` 236, `packuswb` 215, `movlps` 198, `psrlq` 180, `movhps` 179, `pfsub` 148, `psubusw` 142, `punpcklwd` 141, `mulss` 133, `punpckhwd` 103, `psrlw` 100, `pfacc` 99, `psubusb` 96, `subps` 72, `movups` 69, `pandn` 68, `movlpd` 66, `psllq` 65, `psrad` 65, `addss` 53, `psrld` 48, `unpcklps` 47, `movhlps` 44, `pfrcpit2` 43, `mulsd` 43, `paddb` 42, `psllw` 41, `addsd` 41, `packssdw` 36, `paddusb` 33, `unpckhps` 32, `emms` 31, `pcmpgtb` 30, `pfsubr` 28, `movapd` 28, `xorps` 27, `movlhps` 27, `movdqu` 27, `pfrcp` 26, `movntq` 25, `movq2dq` 25, `subss` 24, `pfrcpit1` 24, `pslld` 23, `psubb` 22, `andpd` 22, `pfcmpge` 21, `movdq2q` 21, `subsd` 21, `pextrw` 21, `mulpd` 21, `andps` 20, `paddsb` 20, `psubsb` 20, `pfrsqrt` 19, `pfrsqit1` 19, `pswapd` 19, `psrldq` 16, `pcmpeqd` 15, `pcmpeqw` 13, `pfcmpgt` 13, `rsqrtss` 12, `cvtpi2ps` 11, `psubd` 11, `punpcklqdq` 10, `pshufd` 10, `pfpnacc` 9, `pi2fd` 9, `xorpd` 9, `rcpps` 8, `comiss` 8, `pmovmskb` 8, `packsswb` 8, `punpckhqdq` 8, `orpd` 8, `addpd` 8, `pfnacc` 7, `pinsrw` 5, `pmaxsw` 4, `andnps` 4, `orps` 4, `pcmpgtd` 4, `movntps` 4, `stmxcsr` 4, `unpcklpd` 4, `rcpss` 3, `pfcmpeq` 3, `subpd` 3, `cvtdq2ps` 2, `cmpneqps` 2, `pf2id` 2, `ucomisd` 2, `cmpltpd` 2, `cmpnlepd` 2, `unpckhpd` 2, `prefetchw` 1, `pfmax` 1, `pfmin` 1, `cvtsi2ss` 1, `psubq` 1, `cvtdq2pd` 1, `cvtsd2si` 1, `divsd` 1, `paddq` 1

3DNow! (`pf*`, `femms`, `pi2fd`, `pswapd`...) встречается только в путях с
диспетчеризацией по CPU; он не моделируется.

## C++

- MSVC RTTI: всего 5 дескрипторов типов / 5 vtable (RTTI фактически отключён).
- vtable_scan (структурный): 518 участков, похожих на vtable, 4,432 уникальных метода.
- C++ EH: disasm32 распознал 2,379 EH-заглушек/funclet'ов → C++-исключения скомпилированы в бинарник; диспетчеризация через нативный `RaiseException` остаётся открытым вопросом.
- SEH: 3,636 обращений к `fs:` (SEH-фреймы, `__SEH_prolog`).

## Поверхность взаимодействия с Windows (измеренные импорты: 252 функции)

- **advapi32.dll** (6): `RegSetValueExA`, `RegQueryValueExA`, `RegOpenKeyExA`, `RegOpenKeyA`, `RegCreateKeyA`, `RegCloseKey`
- **ddraw.dll** (1): `DirectDrawCreate`
- **dinput8.dll** (1): `DirectInput8Create`
- **dsound.dll** (1): `#1`
- **gdi32.dll** (12): `SetTextColor`, `CreateCompatibleDC`, `CreateFontA`, `CreateBitmap`, `SetBkMode`, `DeleteDC`, `ExtTextOutA`, `SetBkColor`, `GetPixel`, `DeleteObject`, `SelectObject`, `BitBlt`
- **kernel32.dll** (152): `VirtualQuery`, `IsBadCodePtr`, `GetCurrentProcessId`, `SetEndOfFile`, `SetStdHandle`, `FlushFileBuffers`, `GetStringTypeW`, `GetStringTypeA`, `IsValidCodePage`, `IsValidLocale`, `EnumSystemLocalesA`, `CompareStringA`, `CompareStringW`, `SetEnvironmentVariableA`, `IsDebuggerPresent`, `DebugBreak`, `GetLocaleInfoW`, `GetExitCodeThread`, `GetLocaleInfoA`, `GetUserDefaultLCID`, `LCMapStringW`, `LCMapStringA`, `SetUnhandledExceptionFilter`, `GetCPInfo`, `GetOEMCP`, `GetACP`, `HeapSize`, `HeapReAlloc`, `HeapCreate`, `HeapDestroy`, `TlsAlloc`, `SetLastError`, `TlsFree`, `GetStartupInfoA`, `SetHandleCount`, `GetEnvironmentStringsW`, `FreeEnvironmentStringsW`, `GetEnvironmentStrings`, `FreeEnvironmentStringsA`, `UnhandledExceptionFilter`, `GetStdHandle`, `IsBadWritePtr`, `IsBadReadPtr`, `GetLastError`, `QueryPerformanceFrequency`, `QueryPerformanceCounter`, `FindClose`, `FindNextFileA`, `FindFirstFileA`, `CreateDirectoryA`, `MultiByteToWideChar`, `GetModuleFileNameA`, `GetModuleHandleA`, `GetLongPathNameA`, `SetErrorMode`, `CloseHandle`, `Process32Next`, `Process32First`, `Sleep`, `CreateToolhelp32Snapshot`, `SuspendThread`, `ResumeThread`, `GetDriveTypeA`, `GetLogicalDrives`, `GetVersionExA`, `OutputDebugStringA`, `SetProcessAffinityMask`, `SetThreadPriority`, `SetPriorityClass`, `GetProcessAffinityMask`, `GetThreadPriority`, `GetPriorityClass`, `GetCurrentThread`, `GetCurrentProcess`, `FreeLibrary`, `GetProcAddress`, `LoadLibraryA`, `WideCharToMultiByte`, `GetSystemInfo`, `IsProcessorFeaturePresent`, `EnterCriticalSection`, `InterlockedExchange`, `InitializeCriticalSection`, `InterlockedCompareExchange`, `DeleteCriticalSection`, `LeaveCriticalSection`, `GetFullPathNameA`, `MapViewOfFile`, `GetFileSize`, `CreateFileMappingA`, `CreateFileA`, `CreateFileW`, `UnmapViewOfFile`, `LockResource`, `LoadResource`, `SizeofResource`, `FindResourceA`, `FindResourceW`, `VirtualFree`, `VirtualAlloc`, `lstrcmpiA`, `DuplicateHandle`, `GetCurrentThreadId`, `TerminateThread`, `SleepEx`, `GetTickCount`, `CreateEventA`, `SetEvent`, `WaitForSingleObject`, `ResetEvent`, `GetCurrentDirectoryA`, `WriteFile`, `SetFilePointer`, `DeleteFileA`, `MoveFileA`, `ReadFile`, `GetDiskFreeSpaceExA`, `GlobalAlloc`, `GlobalFree`, `VirtualProtect`, `CreateThread`, `CreateMutexA`, `ReleaseMutex`, `HeapFree`, `GetProcessHeap`, `HeapAlloc`, `WaitCommEvent`, `GetOverlappedResult`, `SetCommMask`, `WaitForMultipleObjects`, `PurgeComm`, `SetCommState`, `GetCommState`, `SetCommConfig`, `GetCommConfig`, `SetCommTimeouts`, `SetupComm`, `GetCommandLineA`, `RtlUnwind`, `ExitProcess`, `TerminateProcess`, `GetTimeZoneInformation`, `GetSystemTimeAsFileTime`, `RaiseException`, `ExitThread`, `TlsSetValue`, `TlsGetValue`, `FileTimeToSystemTime`, `FileTimeToLocalFileTime`, `GetDiskFreeSpaceA`, `GetFileType`, `RemoveDirectoryA`
- **netapi32.dll** (1): `Netbios`
- **shell32.dll** (1): `ShellExecuteA`
- **shfolder.dll** (1): `SHGetFolderPathA`
- **tapi32.dll** (9): `lineAnswer`, `lineMakeCall`, `lineGetID`, `lineClose`, `lineNegotiateAPIVersion`, `lineOpen`, `lineGetDevCaps`, `lineInitialize`, `lineShutdown`
- **user32.dll** (38): `ShowCursor`, `GetCursorPos`, `wvsprintfA`, `MessageBoxA`, `GetSystemMetrics`, `DefWindowProcA`, `ReleaseCapture`, `GetMessageA`, `DispatchMessageA`, `PeekMessageA`, `TranslateMessage`, `LoadIconA`, `LoadCursorA`, `RegisterClassExA`, `AdjustWindowRect`, `GetDesktopWindow`, `CreateWindowExA`, `UpdateWindow`, `SetCursor`, `SetFocus`, `SetForegroundWindow`, `ShowWindow`, `SetWindowLongA`, `DestroyWindow`, `BeginPaint`, `EndPaint`, `PostMessageA`, `PostThreadMessageA`, `PostQuitMessage`, `SetCapture`, `GetClientRect`, `GetWindowRect`, `SetRect`, `UnregisterClassA`, `SetActiveWindow`, `GetForegroundWindow`, `wsprintfA`, `IsIconic`
- **winmm.dll** (6): `timeSetEvent`, `timeKillEvent`, `timeBeginPeriod`, `timeGetDevCaps`, `timeGetTime`, `timeEndPeriod`
- **ws2_32.dll** (22): `WSAGetLastError`, `WSAStartup`, `setsockopt`, `ioctlsocket`, `socket`, `closesocket`, `shutdown`, `bind`, `connect`, `listen`, `accept`, `sendto`, `send`, `recvfrom`, `recv`, `select`, `getpeername`, `getsockname`, `gethostbyname`, `gethostname`, `WSAIoctl`, `WSACleanup`
- **d3d9.dll** (1): `Direct3DCreate9`

Не импортируются статически (нужно проверить в рантайме через
LoadLibrary/GetProcAddress): D3DX9 (слинкован статически; есть строки
`D3DX8 Shader Assembler Version 0.91`), Bink/Miles/аудио-DLL EA (отсутствуют
в таблице импорта).

## Места вызовов импортов (топ)

| импорт | мест прямого вызова |
|---|---|
| kernel32.dll!GetLastError | 31 |
| kernel32.dll!CloseHandle | 21 |
| kernel32.dll!WaitForSingleObject | 19 |
| kernel32.dll!LeaveCriticalSection | 16 |
| kernel32.dll!EnterCriticalSection | 15 |
| kernel32.dll!MultiByteToWideChar | 14 |
| kernel32.dll!ReleaseMutex | 14 |
| kernel32.dll!GetModuleHandleA | 13 |
| kernel32.dll!Sleep | 11 |
| kernel32.dll!GetCurrentThreadId | 11 |
| kernel32.dll!CreateFileA | 9 |
| advapi32.dll!RegCloseKey | 8 |
| kernel32.dll!WriteFile | 8 |
| kernel32.dll!WideCharToMultiByte | 7 |
| kernel32.dll!GetTickCount | 7 |
| kernel32.dll!SetThreadPriority | 6 |
| kernel32.dll!InterlockedExchange | 6 |
| kernel32.dll!VirtualAlloc | 6 |
| kernel32.dll!CreateThread | 6 |
| kernel32.dll!HeapAlloc | 6 |
| kernel32.dll!QueryPerformanceCounter | 5 |
| kernel32.dll!GetModuleFileNameA | 5 |
| kernel32.dll!GetCurrentThread | 5 |
| kernel32.dll!InitializeCriticalSection | 5 |
| kernel32.dll!GetFileSize | 5 |
| kernel32.dll!GetProcAddress | 5 |
| kernel32.dll!SetFilePointer | 5 |
| kernel32.dll!DeleteFileA | 5 |
| user32.dll!wsprintfA | 5 |
| kernel32.dll!HeapFree | 5 |
| kernel32.dll!SetCommMask | 5 |
| kernel32.dll!GetLocaleInfoA | 5 |
| kernel32.dll!GetLocaleInfoW | 5 |
| kernel32.dll!QueryPerformanceFrequency | 4 |
| user32.dll!DestroyWindow | 4 |
| kernel32.dll!LoadLibraryA | 4 |
| kernel32.dll!GetSystemInfo | 4 |
| kernel32.dll!DeleteCriticalSection | 4 |
| kernel32.dll!GetFullPathNameA | 4 |
| kernel32.dll!GetVersionExA | 4 |
