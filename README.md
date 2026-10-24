# Need for Speed Underground 2 — Recompiled


### Android downloads / Descargas Android

[Underground 2 v0.14.9](https://github.com/codepdbh/nfsu2-recompiled/releases/tag/v0.14.9)

| APK | Architecture |
|---|---|
| [ARM64 / 64 bits](https://github.com/codepdbh/nfsu2-recompiled/releases/download/v0.14.9/NFSU2-AndroidEvolved-v0.14.9-arm64.apk) | arm64-v8a, рекомендуется |
| [ARMv7 / 32 bits](https://github.com/codepdbh/nfsu2-recompiled/releases/download/v0.14.9/NFSU2-AndroidEvolved-v0.14.9-armeabi-v7a.apk) | armeabi-v7a; требуется Android с поддержкой 32-битных приложений |

Android 10+ и Vulkan 1.1+. Оба APK используют один и тот же пакет и ключ подписи;
устанавливайте архитектуру, поддерживаемую вашим устройством. Требуются оригинальные файлы игры.
Подробности про аудио, частоту кадров и валидацию: [отчёт по runtime](docs/AUDIO_FRAME_PACING.md).
Изменения по issue и оставшиеся ограничения: [обзор от 16 октября](docs/ISSUE_REVIEW_2026_10_16.md).

<div align="center">

<img src="docs/images/logo.png" alt="NFSU2 Android Evolved Logo" width="220" />

### **Статическая рекомпиляция SPEED2.EXE в нативный C (x86 и ARM64)**

[![Platform](https://img.shields.io/badge/Platform-Windows%20x86%20%7C%20Android%20ARM64-blue.svg)](#status)
[![Graphics](https://img.shields.io/badge/Graphics-Direct3D%209%20%7C%20Vulkan%20(DXVK)-orange.svg)](#graphics--vulkan-runtime)
[![Audio](https://img.shields.io/badge/Audio-AAudio%20%7C%20DirectSound-green.svg)](#audio--multimedia)
[![Status](https://img.shields.io/badge/Status-Playable%20Race%20Prototype-brightgreen.svg)](#status)
[![License](https://img.shields.io/badge/License-MIT%20(Tooling)-lightgrey.svg)](#legal)

*Полная статическая бинарная рекомпиляция **Need for Speed: Underground 2** (`SPEED2.EXE`, x86-32) в чистый переносимый код на C с помощью доработанного форка [pcrecomp](https://github.com/sp00nznet/pcrecomp).*

[🎮 Витрина Android](#gameplay-showcase) • [⚡ Возможности](#features) • [📊 Статус](#status) • [🛠️ Пайплайн сборки](#pipeline) • [📱 Гайд по Android](#android-port) • [⚖️ Правовая информация](#legal)

---

</div>

## Витрина геймплея

> Реальные скриншоты, снятые при нативном запуске на **Android (ARM64)** через DXVK Native поверх Vulkan 1.3:

<div align="center">

|                        **Ночная гонка (обгон и HUD)**                         |                           **Кастомизация и тюнинг-гараж**                            |
|:--------------------------------------------------------------------------------------:|:------------------------------------------------------------------------------------------:|
| <img src="docs/images/android_race_action.png" alt="Android Race Action" width="460"/> | <img src="docs/images/android_garage_tuning.png" alt="Android Garage Tuning" width="460"/> |
|           *Гонка в реальном времени против ИИ с активным сенсорным HUD и аналоговыми приборами*            |                  *Полный 3D-рендеринг виниловых наклеек, дисков и кузова в гараже*                  |

|                            **Вид от водителя и телеметрия**                             |                               **Настраиваемое сенсорное управление и стили**                                |
|:------------------------------------------------------------------------------------------:|:-----------------------------------------------------------------------------------------------------:|
| <img src="docs/images/android_race_gameplay.png" alt="Android Race Gameplay" width="460"/> | <img src="docs/images/android_controls_styles.png" alt="Android Controls Customization" width="460"/> |
|            *Плавный геймплей на 35–51+ FPS на Samsung Galaxy S24 Ultra (SM-S938B)*             |                 *Живой редактор раскладки, масштабирование кнопок, наклон по гироскопу и готовые пресеты*                 |

|                          **Интерактивный редактор раскладки кнопок**                          |                       **Нативный лаунчер игры и настройки**                        |
|:--------------------------------------------------------------------------------------:|:--------------------------------------------------------------------------------:|
| <img src="docs/images/android_controls_editor.png" alt="Controls Editor" width="460"/> | <img src="docs/images/android_launcher.png" alt="Android Launcher" width="460"/> |
|                 *Перетаскивание, изменение размера и сохранение персональных профилей управления*                  |          *Автоматическое определение внутренней памяти, выбор языка и лимитов FPS*           |

</div>

---

## Возможности

- **⚡ Нативная бинарная рекомпиляция**: превращает более 27 700 функций x86 в стандартный код на C. Никакого JIT-компилятора, виртуализации или накладных расходов на динамическую трансляцию во время выполнения.
- **📱 Нативный рантайм Android ARM64**: выполнение на современных мобильных устройствах с переносимым управлением виртуальной памятью, структурной обработкой исключений и синхронизацией потоков через POSIX.
- **🌋 Графика на Vulkan**: полная трансляция Direct3D 9 в Vulkan на базе встроенного порта DXVK Native.
- **🎮 Полностью настраиваемое сенсорное управление**:
  - Редактор управления с перетаскиванием, масштабированием кнопок и сохранением именованных раскладок.
  - Готовые темы управления: *Классика*, *Xbox* и *PlayStation*.
  - Управление по гироскопу / наклону для интуитивного вождения.
  - Отдельные кнопки: руль, ручной тормоз, нитро (N₂O), тормоз/задний ход и газ.
- **🖥️ Масштабирование разрешения и FPS в игре**:
  - Динамическое масштабирование разрешения рендера (25%, 50%, 75%, нативное разрешение экрана, 720p).
  - Настраиваемые лимиты частоты кадров (без ограничений, 30, 60, 120 FPS) для баланса нагрева и автономности.
- **🔊 Аудио с низкой задержкой**: нативный PCM-стриминг через бэкенд AAudio в Android с автоматической защитой от underrun буфера.
- **🌐 Поддержка нескольких языков**: определяет и загружает языковые файлы игры (испанский, английский, французский, немецкий, итальянский и другие) прямо с памяти телефона.

---

## Обзор архитектуры

```
                          ┌────────────────────────┐
                          │   SPEED2.EXE (x86)     │
                          └───────────┬────────────┘
                                      │
                         [ pcrecomp disasm32 ]
                                      │
                         [ lift32 (+ simd32) ]
                                      │
                          ┌───────────▼────────────┐
                          │  27,742 C Functions    │
                          │   (~8.9M lines C99)    │
                          └─────┬────────────┬─────┘
                                │            │
           ┌────────────────────┘            └────────────────────┐
           ▼                                                      ▼
 ┌──────────────────────┐                               ┌──────────────────────┐
 │     Windows Host     │                               │    Android ARM64     │
 │  MSVC x86 Toolchain  │                               │   Android NDK / C++  │
 │  Direct3D 9 / Win32  │                               │   DXVK Native/Vulkan │
 └──────────────────────┘                               └──────────────────────┘
```

> [!IMPORTANT]
> **Этот репозиторий не содержит защищённого авторским правом кода игры или ресурсов.** Вам нужна лицензионная копия Need for Speed Underground 2 и собственные файлы игры. Весь сгенерированный код и артефакты анализа создаются локально на вашем компьютере.

---

## Статус

Текущее состояние на **октябрь 2026**:

| Этап / компонент | Платформа | Статус | Детали |
|---|---|:---:|---|
| **M0: генерация C** | Любая | **ГОТОВО** | 27 742 функции, 8,9 млн строк C, 0 ошибок лифтинга |
| **M1: компиляция / M2: линковка** | Windows | **ГОТОВО** | MSVC x86, 0 ошибок (~4,5 мин на 16 потоках) |
| **M3: CRT / M4: точка входа / M5: WinMain** | Windows | **ГОТОВО** | Запуск игры полностью работает на рекомпилированном коде (реестр, таймеры, потоки) |
| **M6: окно / M7: D3D9 / M8: первый кадр** | Windows | **ГОТОВО** | Инициализация устройства, управление окном и рендеринг |
| **M9: интро / M10: меню** | Windows | **РАБОТАЕТ** | Воспроизводятся ролики, рендерятся меню, полный ввод с клавиатуры и геймпада |
| **Рендеринг машин и визуал** | Win / Android | **РАБОТАЕТ** | Исправлен баг со знаковым 16-битным флагом; машины, винил и фары рендерятся корректно |
| **Загрузка трасс и геймплей** | Win / Android | **ИГРАБЕЛЬНО** | Загружаются Resort Loop и быстрые гонки; работают разгон, руление, нитро, ИИ-трафик |
| **Порт на Android ARM64** | Android | **ИГРАБЕЛЬНО** | 35–51+ FPS при 1170×540 на Snapdragon 8 Gen 3; активны сенсорное управление и звук |
| **Восстановление поверхности и пауза** | Android | **ПРОВЕРЕНО** | Чистый переход в фон и обратно без потери процесса или состояния графики |
| **Лимитер частоты кадров и скейлер** | Android | **ГОТОВО** | Реализованы лимиты 30/60/120 FPS и режимы динамического разрешения |

---

## Протестированная сборка игры

| Свойство | Детали |
|---|---|
| **Файл бинарника** | `SPEED2.EXE` (4 800 512 байт) |
| **Контрольная сумма SHA-256** | `f9dd86c054878ce6276beb07c1fd61874f7a1e4bf1f241b084c65b73e24168a7` |
| **Встроенная дата** | `Feb 9 2005` (поле `VERS`) |
| **Формат бинарника** | Распакованный образ (чистая секция `.text`, восстановленные импорты, сохранены релокации) |

*Подробности о бинарнике см. в [docs/NFSU2_BINARY_REPORT.md](docs/NFSU2_BINARY_REPORT.md).*

**Other SPEED2.EXE builds.** Widescreen hooks are tied to the exact code of the lifted executable. The default table (`config/hooks.txt`) is for the build above. The build with SHA-1 `f4bb0d99…` used by the R36S port has its own table (`config/hooks-f4bb0d99.txt`): set `NFSU2_EXE_BUILD=f4bb0d99` both when lifting (`scripts/run_lift.py`) and when building (CMake reads the same variable). A different build needs its own `config/hooks-<build>.txt` and table in `src/runtime/widescreen.c`.

---

## Требования

### Хост на Windows
- Windows 10 / 11 64-бит
- Visual Studio 2022 / 2026 с набором инструментов C++ x86 desktop development (MSVC)
- LLVM / Clang (для дифференциального тестирования)
- Python 3.10+ со следующими зависимостями:
  ```bash
  pip install capstone pefile unicorn keystone-engine
  ```
- Доработанный сабмодуль pcrecomp с применёнными патчами 0001–0011:
  ```bash
  git clone https://github.com/sp00nznet/pcrecomp.git ../pcrecomp
  cd ../pcrecomp
  git checkout 35548b8
  git am ../nfsu2-recompiled/patches/pcrecomp/*.patch
  ```

### Сборка под Android
- Android SDK Platform 35 / Min SDK 26 (Android 8.0+)
- Android NDK (r25c или новее с Clang ARM64)
- Устройство на Android с поддержкой Vulkan 1.3 (поддержка Vulkan 1.1 в разработке)

---

## Пайплайн сборки

Шаги для генерации кода на C и сборки исполняемого файла:

```bash
# 1. Инициализировать окружение MSVC x86 в Git Bash
source scripts/vsenv.sh x86
GAME="/path/to/Need for Speed Underground 2"

# 2. Нормализовать исполняемый файл (восстанавливает таблицу релокаций, не меняя оригинал)
python scripts/normalize_exe.py "$GAME/SPEED2.EXE" work/SPEED2.analysis.exe

# 3. Найти сиды: vtable и RTTI
python ../pcrecomp/tools/cpp/vtable_scan.py work/SPEED2.analysis.exe --seeds work/vtable_seeds.json
python ../pcrecomp/tools/cpp/rtti.py        work/SPEED2.analysis.exe --seeds work/rtti_seeds.json

# 4. Сгенерировать каталог функций с базовыми блоками (~5 мин)
python scripts/run_lift.py catalog work/SPEED2.analysis.exe work/seeds_all.json work/catalog.json

# 5. Поднять ассемблер в переносимый C (~90 с) -> src/recomp/gen/
python scripts/run_lift.py lift work/SPEED2.analysis.exe work/catalog.json src/recomp/gen

# 6. Собрать и запустить нативный хост под Windows
scripts/build.sh
scripts/run.sh [--fullscreen]
```

---

## Порт на Android

Каталог `android/` содержит полный проект Gradle для сборки APK:

1. **Сборка APK**:
   ```bash
   cd android
   ./gradlew assembleDebug
   ```
2. **Установка на устройство**:
   - Установите собранный APK на свое Android-устройство.
   - Скопируйте файлы игры с ПК (`Need for Speed Underground 2`) во внутреннюю память устройства, в папку `/sdcard/nfsu2/`.
   - Откройте **NFSU2 Android Evolved**, разрешите доступ к файлам, выберите язык, разрешение и лимит FPS — и запускайте игру!

Дополнительная документация:
- [Статус Android-порта и заметки по сборке](docs/ANDROID_PORT.md)
- [Android-порт DXVK Native](ports/dxvk_android/README.md)
- [Проверка геймплея и управления](docs/ANDROID_GAMEPLAY_CONTROLS_TEST.md)
- [Тестирование меню и профилей сохранений](docs/ANDROID_MENU_PROFILE_TEST.md)

---

## Патчи, применённые к pcrecomp

Проект использует набор патчей поверх апстрима pcrecomp, расположенных в `patches/pcrecomp/`:

| Патч | Название / функциональность |
|:---:|---|
| **0001** | `pe_analyze`: диапазон кода ограничен смежными исполняемыми секциями, содержащими точку входа |
| **0002** | `lift32`: полная поддержка MMX, SSE и SSE2 через `simd32.py`; +40 тестовых случаев SIMD для дифтеста |
| **0003** | `RECOMP_STRICT_ICALL`: хуки хост-диспетчеризации для неразрешённых косвенных вызовов |
| **0004** | Поддержка ленивых флагов `lahf` и сохранение/восстановление окружения x87 |
| **0005** | Уступки (yield) на обратных переходах для прогресса гостевых потоков и кооперативной многозадачности |
| **0006** | Хуки инструментации после вызова и трассировка API |
| **0007** | Хуки перед вызовом для проверки нативных аргументов и перезаписи памяти |
| **0008** | Отчёт по TOP x87, честная FIFO-блокировка машины и поиск символов через хеш |
| **0009** | Явная обработка знака для знаковых условий (исправлен краш при загрузке гонки и рендеринг машин) |
| **0010** | `RECOMP_NATIVE_RANGE`: выборочное гибридное бисекционное выполнение относительно оригинального кода |
| **0011** | Потокобезопасное сохранение xmm/mxcsr и гостевых флагов CPU на каждый гостевой поток |

---

## Контрибьюторы

| Контрибьютор | Вклад |
|---|---|
| [codepdbh](https://github.com/codepdbh) | Статическая рекомпиляция, хост под Windows, порт на Android и лаунчер |
| [Manuel Peña (manuelpllull)](https://github.com/manuelpllull) | Порт под портативку R36S ([nfsu2-recompiled-r36s](https://github.com/manuelpllull/nfsu2-recompiled-r36s)) и доработка хуков широкоэкранного режима |

Пул-реквесты с вкладом приветствуются.

---

## Правовая информация и отказ от ответственности

- Need for Speed, Underground, EA и все связанные материалы, логотипы и торговые марки принадлежат **Electronic Arts Inc.**
- Этот проект — исследовательская инициатива по реверс-инжинирингу и статической рекомпиляции, созданная в образовательных и архивных целях.
- Весь код, написанный в этом репозитории (инструменты лифтера, мост для Android, обёртки WSI, лаунчер и скрипты), распространяется под лицензией **MIT**.
- **В репозитории не распространяются проприетарные бинарники игры, защищённая авторским правом музыка, видео или 3D-модели.**
