# Android UI localization

The Android launcher and in-game Android overlays use standard Android string resources. English is the complete fallback locale. Each translation lives in its own `values-<language>` directory.

Current Android UI locales:

- English: `app/src/main/res/values/strings.xml`
- Spanish: `app/src/main/res/values-es/strings.xml`
- Russian: `app/src/main/res/values-ru/strings.xml`

Supported per-app locales are declared in `app/src/main/res/xml/locales_config.xml` and referenced by `android:localeConfig` in the manifest.

## Add another Android UI language

1. Copy `app/src/main/res/values/strings.xml` to a new Android locale directory, for example `app/src/main/res/values-de/strings.xml`.
2. Translate string values only. Keep every `name` unchanged.
3. Preserve format placeholders such as `%1$s` and `%2$s` exactly. Strings containing a literal percent sign use `formatted="false"` where needed.
4. Add the locale tag to `app/src/main/res/xml/locales_config.xml`, for example `<locale android:name="de" />`.
5. Build and run Android lint:

   ```text
   gradlew.bat :app:assembleDebug
   gradlew.bat :app:lintDebug
   ```

Android selects the best matching locale automatically and falls back to the English `values/strings.xml` resources when a translation is missing. On Android 13 and newer, the supported locales are also exposed in the system per-app language settings. The launcher’s **Interface language** row opens that system picker. On Android 12 and earlier, the launcher follows the device locale.

## Interface language vs. game language

Android UI localization and the original game's language are independent.

The **Interface language** setting controls the launcher and Android in-game overlays. The **Game language** option selects an installed `LANGUAGES/<name>.bin` resource used by NFS Underground 2. Do not add UI translations to the `LANGUAGES` table in `LauncherActivity`; that table describes game data, not Android UI locales.

Some native runtime and driver components still emit stable internal Spanish diagnostic phrases. `GameActivity` and `LauncherActivity` translate those known phrases at the Java boundary before they reach the UI. A new UI locale therefore only needs resource translations; it does not require changes to native C++ code.
