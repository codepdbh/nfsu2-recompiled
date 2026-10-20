import os
import re

TRANS_MAP = {
    # Buttons & core UI
    '"JUGAR"': '"PLAY"',
    '"Permitir acceso a memoria interna/nfsu2"': '"Allow access to internal storage/nfsu2"',
    '"ARCHIVOS DEL JUEGO"': '"GAME FILES"',
    '"OPCIONES"': '"OPTIONS"',
    '"Se aplican al pulsar JUGAR."': '"Applied when pressing PLAY."',
    '"Permite el acceso a archivos para leer memoria interna/nfsu2."': '"Grant file access to read internal storage/nfsu2."',
    '"Memoria interna/nfsu2"': '"Internal storage/nfsu2"',
    '"Copia los archivos de tu juego en memoria interna/nfsu2."': '"Copy your game files to internal storage/nfsu2."',
    '"Carpetas del juego"': '"Game folders"',
    '"Faltan carpetas: "': '"Missing folders: "',
    '"HUD panor\u00e1mico (scripts/\u2026WidescreenFix.dat)"': '"Widescreen HUD (scripts/…WidescreenFix.dat)"',
    '"HUD panorámico (scripts/NFSUnderground2.WidescreenFix.dat)"': '"Widescreen HUD (scripts/NFSUnderground2.WidescreenFix.dat)"',
    '"PC \u00b7 recompilado de forma nativa para Android"': '"PC \u00b7 Natively recompiled for Android"',
    '"PC · recompilado de forma nativa para Android"': '"PC · Natively recompiled for Android"',

    # Options menu rows
    '"Driver Vulkan"': '"Vulkan Driver"',
    '"Del sistema"': '"System default"',
    '"Driver del sistema"': '"System driver"',
    '"Probar driver"': '"Test driver"',
    '"GPU y funciones Vulkan"': '"GPU and Vulkan features"',
    '"Idioma"': '"Language"',
    '"Reiniciar cach\u00e9 gr\u00e1fica"': '"Reset graphics cache"',
    '"Reiniciar caché gráfica"': '"Reset graphics cache"',
    '"Regenerar shaders"': '"Recompile shaders"',
    '"Servidor online"': '"Online server"',
    '"Buscar actualizaciones"': '"Check for updates"',
    '"Comprobando GitHub\u2026"': '"Checking GitHub\u2026"',
    '"Comprobando GitHub."': '"Checking GitHub."',
    '"Reportar un problema"': '"Report an issue"',
    '"Log completo a GitHub"': '"Full log to GitHub"',

    # GameOptions
    '"Resoluci\u00f3n interna"': '"Internal resolution"',
    '"Resolución interna"': '"Internal resolution"',
    '"Nativa \u00b7 m\u00e1xima calidad"': '"Native \u00b7 maximum quality"',
    '"Nativa · máxima calidad"': '"Native · maximum quality"',
    '"75% \u00b7 equilibrada"': '"75% \u00b7 balanced"',
    '"75% · equilibrada"': '"75% · balanced"',
    '"50% \u00b7 m\u00e1ximo rendimiento"': '"50% \u00b7 maximum performance"',
    '"50% · máximo rendimiento"': '"50% · maximum performance"',
    '"1280\u00d7720 \u00b7 16:9"': '"1280\u00d7720 \u00b7 16:9"',
    '"1280x720 · 16:9"': '"1280x720 · 16:9"',

    '"L\u00edmite de FPS"': '"FPS limit"',
    '"Límite de FPS"': '"FPS limit"',
    '"30 FPS \u00b7 ahorra bater\u00eda"': '"30 FPS \u00b7 saves battery"',
    '"30 FPS · ahorra batería"': '"30 FPS · saves battery"',
    '"Sin l\u00edmite"': '"Uncapped"',
    '"Sin límite"': '"Uncapped"',

    '"Pantalla panor\u00e1mica"': '"Widescreen"',
    '"Pantalla panorámica"': '"Widescreen"',
    '"HUD y c\u00e1mara corregidos"': '"HUD and camera corrected"',
    '"HUD y cámara corregidos"': '"HUD and camera corrected"',
    '"Original 4:3 estirado"': '"Original 4:3 stretched"',

    '"Minimapa"': '"Minimap"',
    '"Arriba a la izquierda"': '"Top-left"',
    '"Abajo a la izquierda (original)"': '"Bottom-left (original)"',

    '"Sincron\u00eda CPU/GPU"': '"CPU/GPU sync"',
    '"Sincronía CPU/GPU"': '"CPU/GPU sync"',
    '"Rendimiento \u00b7 CPU y GPU en paralelo"': '"Performance \u00b7 CPU and GPU in parallel"',
    '"Rendimiento · CPU y GPU en paralelo"': '"Performance · CPU and GPU in parallel"',
    '"Baja latencia \u00b7 menos FPS"': '"Low latency \u00b7 lower FPS"',
    '"Baja latencia · menos FPS"': '"Low latency · lower FPS"',

    '"Texturas comprimidas"': '"Compressed textures"',
    '"Autom\u00e1tico \u00b7 GPU si las soporta"': '"Automatic \u00b7 GPU if supported"',
    '"Automático · GPU si las soporta"': '"Automatic · GPU if supported"',
    '"Convertir en CPU \u00b7 compatibilidad"': '"Convert on CPU \u00b7 compatibility"',
    '"Convertir en CPU · compatibilidad"': '"Convert on CPU · compatibility"',

    # Language dialog
    '"No se encontraron idiomas en memoria interna/nfsu2/LANGUAGES."': '"No language files found in internal storage/nfsu2/LANGUAGES."',

    # Driver dialogs
    '"Prueba del driver Vulkan"': '"Vulkan driver test"',
    '"Importar ZIP"': '"Import ZIP"',
    '"Eliminar seleccionado"': '"Delete selected"',
    '"Importando driver Vulkan\u2026"': '"Importing Vulkan driver\u2026"',
    '"Importando driver Vulkan."': '"Importing Vulkan driver."',
    '"No se pudo importar el driver: "': '"Could not import driver: "',
    '"Compatible con el juego."': '"Compatible with the game."',
    '"Faltan funciones: "': '"Missing features: "',
    '"Usar driver del sistema"': '"Use system driver"',
    '"Comprobando el driver Vulkan\u2026"': '"Testing Vulkan driver\u2026"',
    '"El driver debe ser una biblioteca Android ARM64 (.so), no de PC ni de 32 bits."': '"Driver must be an Android ARM64 (.so) library, not PC or 32-bit."',

    # Graphics Cache dialogs
    '"Cach\u00e9 gr\u00e1fica"': '"Graphics cache"',
    '"Caché gráfica"': '"Graphics cache"',
    '"Cach\u00e9 gr\u00e1fica preparada"': '"Graphics cache ready"',
    '"Caché gráfica preparada"': '"Graphics cache ready"',
    '"Se cerrar\u00e1 la sesi\u00f3n actual del juego. Los shaders se volver\u00e1n a compilar al jugar; las partidas y los ajustes se conservan."': '"The game session will close. Shaders will recompile next launch; saves and settings are kept."',
    '"Se cerrará la sesión actual del juego. Los shaders se volverán a compilar al jugar; las partidas y los ajustes se conservan."': '"The game session will close. Shaders will recompile next launch; saves and settings are kept."',
    '"El pr\u00f3ximo arranque generar\u00e1 una cach\u00e9 nueva. La anterior queda respaldada en la cach\u00e9 privada de la app."': '"Next launch will generate a fresh cache. Previous cache backed up in app private storage."',
    '"El próximo arranque generará una caché nueva. La anterior queda respaldada en la caché privada de la app."': '"Next launch will generate a fresh cache. Previous cache backed up in app private storage."',
    '"Reiniciar"': '"Reset"',

    # Online server dialog
    '"El modo online del juego (EA Nation) se conecta a este servidor de la comunidad en lugar de los servidores de EA, ya cerrados."': '"Online multiplayer (EA Nation) connects to this community server instead of defunct EA servers."',
    '"Guardar"': '"Save"',
    '"Restaurar"': '"Restore"',
    '" no es un nombre de servidor v\u00e1lido."': '" is not a valid server name."',
    '" no es un nombre de servidor válido."': '" is not a valid server name."',

    # Controls view
    '"Controles del juego"': '"Game controls"',
    '"Opciones de controles"': '"Control options"',
    '"Estilo de conducci\u00f3n"': '"Driving style"',
    '"Estilo de conducción"': '"Driving style"',
    '"Cl\u00e1sico \u00b7 flechas y pedales"': '"Classic \u00b7 arrows and pedals"',
    '"Clásico · flechas y pedales"': '"Classic · arrows and pedals"',
    '"PlayStation \u00b7 volante y \u00d7 / \u25cb / \u25a1 / \u25b3"': '"PlayStation \u00b7 wheel and \u00d7 / \u25cb / \u25a1 / \u25b3"',
    '"PlayStation · volante y × / ○ / □ / △"': '"PlayStation · wheel and × / ○ / □ / △"',
    '"Xbox \u00b7 volante y A / B / X / Y"': '"Xbox \u00b7 wheel and A / B / X / Y"',
    '"Xbox · volante y A / B / X / Y"': '"Xbox · wheel and A / B / X / Y"',
    '"Conducci\u00f3n por inclinaci\u00f3n"': '"Tilt steering"',
    '"Conducción por inclinación"': '"Tilt steering"',
    '"Girar inclinando el celular"': '"Steer by tilting phone"',
    '"Sensibilidad: "': '"Sensitivity: "',
    '"Invertir direcci\u00f3n"': '"Invert steering"',
    '"Invertir dirección"': '"Invert steering"',
    '"Sost\u00e9n el celular en tu posici\u00f3n de juego al aplicar. La inclinaci\u00f3n activa izquierda/derecha; los pedales siguen siendo t\u00e1ctiles."': '"Hold your phone in your playing position when applying. Tilt steers left/right; pedals remain on screen."',
    '"Sostén el celular en tu posición de juego al aplicar. La inclinación activa izquierda/derecha; los pedales siguen siendo táctiles."': '"Hold your phone in your playing position when applying. Tilt steers left/right; pedals remain on screen."',
    '"Aplicar y calibrar"': '"Apply and calibrate"',
    '"Guardar layout con nombre"': '"Save layout as..."',
    '"Cargar layout personalizado"': '"Load custom layout"',
    '"Cargar layout"': '"Load layout"',
    '"Escribe un nombre"': '"Enter a name"',
    '"Nombre del layout"': '"Layout name"',
    '"Escribir en el juego"': '"Type in-game"',
    '"Layout guardado: "': '"Layout saved: "',
    '"Todav\u00eda no hay layouts guardados para este modo"': '"No saved layouts for this mode yet"',
    '"Todavía no hay layouts guardados para este modo"': '"No saved layouts for this mode yet"',

    # Dialog general buttons
    '"Aceptar"': '"OK"',
    '"Cancelar"': '"Cancel"',
    '"M\u00e1s tarde"': '"Later"',
    '"Más tarde"': '"Later"',
    '"Ahora no"': '"Not now"',
    '"Actualizar"': '"Update"',
    '"Ver en GitHub"': '"View on GitHub"',
    '"Reportar en GitHub"': '"Report on GitHub"',
    '"Abrir GitHub"': '"Open GitHub"',

    # Updates & Crash
    '"Cargando las novedades\u2026"': '"Loading what\'s new\u2026"',
    '"Cargando las novedades."': '"Loading what\'s new."',
    '"No se pudieron cargar las novedades (sin conexi\u00f3n o a\u00fan no publicadas). Puedes verlas en GitHub."': '"Could not load what\'s new (offline or not published). You can view on GitHub."',
    '"No se pudieron cargar las novedades (sin conexión o aún no publicadas). Puedes verlas en GitHub."': '"Could not load what\'s new (offline or not published). You can view on GitHub."',
    '"El juego se cerr\u00f3"': '"The game crashed"',
    '"El juego se cerró"': '"The game crashed"',
    '".\n\n\u00bfQuieres enviar el registro completo a GitHub para que se pueda corregir?"': '".\n\nWould you like to send the log to GitHub so it can be fixed?"',
    '".\n\n¿Quieres enviar el registro completo a GitHub para que se pueda corregir?"': '".\n\nWould you like to send the log to GitHub so it can be fixed?"',
    '"El juego dej\u00f3 de responder"': '"The game stopped responding"',
    '"El juego dejó de responder"': '"The game stopped responding"',
    '"El juego se cerr\u00f3 inesperadamente"': '"The game closed unexpectedly"',
    '"El juego se cerró inesperadamente"': '"The game closed unexpectedly"',
    '"Nueva versi\u00f3n disponible: "': '"New version available: "',
    '"Nueva versión disponible: "': '"New version available: "',
    '"Puedes descargarla desde GitHub o seguir jugando con esta versi\u00f3n. Instala el APK encima del actual para conservar tus partidas y ajustes."': '"You can download from GitHub or keep playing this version. Install over existing APK to preserve saves."',
    '"Puedes descargarla desde GitHub o seguir jugando con esta versión. Instala el APK encima del actual para conservar tus partidas y ajustes."': '"You can download from GitHub or keep playing this version. Install over existing APK to preserve saves."',
    '". No hay una versi\u00f3n m\u00e1s reciente en GitHub."': '". You have the latest version from GitHub."',
    '". No hay una versión más reciente en GitHub."': '". You have the latest version from GitHub."',
    '"No se pudo consultar GitHub. Comprueba tu conexi\u00f3n e int\u00e9ntalo despu\u00e9s."': '"Could not reach GitHub. Check connection and try again later."',
    '"No se pudo consultar GitHub. Comprueba tu conexión e inténtalo después."': '"Could not reach GitHub. Check connection and try again later."',
    '"Actualizaciones"': '"Updates"',
    '"Tienes la versi\u00f3n "': '"You are on version "',
    '"Tienes la versión "': '"You are on version "',
    '"\u00a1Actualizado a la versi\u00f3n "': '"Updated to version "',
    '"¡Actualizado a la versión "': '"Updated to version "',
}

def patch_smali_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    modified = False

    # Special case: Default language in GameOptions.smali
    if 'GameOptions.smali' in filepath:
        # replace default language "Spanish" with "English UK"
        pattern = r'(const-string [vp]\d+, )"Spanish"'
        def repl_lang(m):
            return m.group(1) + '"English UK"'
        new_content = re.sub(pattern, repl_lang, content)
        if new_content != content:
            content = new_content
            modified = True
            print(f'Set default language to English UK in {os.path.basename(filepath)}')

    for spanish, english in TRANS_MAP.items():
        if spanish in content:
            content = content.replace(spanish, english)
            modified = True

    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f'Patched: {os.path.basename(filepath)}')

def main(root_dir):
    count = 0
    for root, dirs, files in os.walk(root_dir):
        for f in files:
            if f.endswith('.smali'):
                patch_smali_file(os.path.join(root, f))
                count += 1
    print(f'Scanned {count} smali files.')

if __name__ == '__main__':
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else '.'
    main(target)
