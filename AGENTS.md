# Agent Instructions

## Propósito

AnimeHub Desktop es una aplicación nativa C++26 con Qt Quick para Windows y Linux: biblioteca local, catálogo vía AnimeHub API, descargas nativas, reproducción externa, bandeja y avisos. La documentación se escribe en español; código, nombres de archivos, identificadores y commits, en inglés.

## Límites del producto

- No replica AnimeHub Web. Consume `animehub-api` bajo `/api/v1`, con caché local de respuestas para uso sin conexión.
- Sin scraping en C++: AnimeAV1 se procesa solo en el servidor.
- La biblioteca es local, privada y reconstruible (SQLite). Visto y favorito son datos, no carpetas.
- Descargas nativas solo de MEGA (API pública, descifrado AES-CTR y verificación del MAC) y Pixeldrain, siempre con aria2 por JSON-RPC. Otros servidores, solo a través de JDownloader (API local o MyJDownloader), como tareas seguidas de la misma cola: nada se envía sin seguimiento.
- La cola nunca guarda URLs firmadas, claves de enlace ni credenciales; una tarea reanudada vuelve a resolver su origen.
- Motores: en Windows, aria2 y mpv van incluidos; en Linux, los del sistema (el .deb los declara como dependencias; si faltan, la app dice qué paquete instalar).
- Reproducción externa: mpv por defecto (seguimiento por IPC) o la aplicación predeterminada del sistema (abrir marca visto). Nada de reproductor embebido sin una nueva decisión. Cerrar AnimeHub no detiene un vídeo.
- Avisos de episodios nuevos activados por defecto; la descarga automática se activa por anime y con consentimiento explícito (destino y forma de desactivarla).
- Credenciales de servicios opcionales solo en el almacén del sistema (Administrador de credenciales en Windows, Secret Service por libsecret en Linux); de MyJDownloader, solo las claves derivadas.

## Interfaz

- Referencia: la biblioteca de Steam. Pestañas arriba (Biblioteca · Catálogo · Descargas) y Ajustes en un diálogo; en Biblioteca, lista a la izquierda y a la derecha el inicio con portadas o la ficha. Barra de descarga abajo mientras quede algo sin terminar, también en pausa o con error.
- Controles del estilo FluentWinUI3 de Qt sin re-estilar, tema oscuro, acento de marca `#2f81f7`, iconos Segoe Fluent Icons, y en Linux Uno Fluent Icons con los mismos códigos (`image://glyph/<código>` para `icon.source`; un código nuevo se comprueba en ambas fuentes). Tokens en `qml/Theme.qml`.
- Menús con `ActionMenu` (con `entries`, solo las entradas cuyo `wanted` aplica) y `MenuEntry`, diálogos con `ModalDialog` y portadas con los tokens `poster*` de `Theme`: corrigen el ancho fijo de los menús de Fluent, la alineación de sus textos, la barra de título bajo el velo modal y los tamaños entre filas y rejillas.
- Teclado, foco visible, nombres accesibles y la acción "pulsar" de accesibilidad en todo control; la ventana debe funcionar desde su ancho mínimo.
- Las acciones destructivas o automáticas comunican destino, alcance y recuperación (los archivos van a la papelera).

## Arquitectura y C++

- `CMAKE_CXX_STANDARD 26`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`.
- `src/core` no depende de la interfaz; lo específico de cada sistema (cifrado, procesos auxiliares, credenciales, papelera, inicio con la sesión) va tras interfaces pequeñas en `src/core/platform`, con `*_win.cpp` y `*_linux.cpp`. En `src/app`, lo poco que difiere (avisos por D-Bus, «Mostrar en carpeta», textos de Ajustes) se separa por plataforma en el sitio.
- QML solo presenta; reglas, persistencia y red en C++. QML ve modelos e interfaces mínimas (`src/app`), nunca objetos del núcleo.
- RAII, tipos de valor, ownership explícito, `std::expected` para errores. Sin singletons globales.
- Dependencias nuevas: licencia compatible con GPL-3.0-or-later y razón concreta. Lo que se descarga al compilar (motores de Windows, la fuente de iconos de Linux, linuxdeploy) se fija por URL y SHA-256 (`cmake/Pinned.cmake`; linuxdeploy, en `scripts/package-linux.sh`).

## Toolchain y calidad

- MSVC 2022 x64 y Qt 6.11 MSVC con Qt Image Formats, más un `strip` de MinGW (el de Qt Tools) para la información de depuración de mpv; `scripts/build.ps1` prepara el entorno solo para su proceso (`-Fresh`, `-Configuration Release`, `-SkipTests`).
- Linux: Ubuntu 24.04, GCC 14, Qt 6.11 `linux_gcc_64`, presets `linux-debug` y `linux-release` (el ejecutable se llama `animehub`). Desde Windows, en el contenedor de `scripts/linux-build.Dockerfile`; la prueba de credenciales necesita el llavero desechable que monta el CI.
- La carpeta Release es la instalación: `cmake/Deploy.cmake` despliega Qt y el runtime de MSVC y quita lo que la app no carga (comprobado con los módulos cargados en un recorrido completo). `scripts/package.ps1` la empaqueta (zip portable e instalador por usuario de Inno Setup, `installer/AnimeHub.iss`); `scripts/package-linux.sh`, el AppImage (con Qt y lo que un escritorio puede no tener) y el .deb (Qt en `/opt/animehub`, el resto de paquetes del sistema). Un tag anotado `vX.Y.Z` igual a la versión de `CMakeLists.txt` publica los cuatro con un único `SHA256SUMS.txt` desde `.github/workflows/release.yml`; el mensaje del tag son las notas.
- Formato con `.clang-format` (`clang-format` de Qt Creator); pruebas con Qt Test. Las de descargas y reproducción usan aria2 y mpv reales contra un servidor local: no requieren red.
- Antes de entregar cambios transversales: formato, `.\scripts\build.ps1` (Debug con `ctest`) y la Release arrancada sin Qt en el `PATH`.
- Capturas y recorridos sin tocar la pantalla: `--data-dir <perfil> --smoke-screenshot <png>` con `--smoke-hidden-native` en Windows (ventana nativa oculta) o `QT_QPA_PLATFORM=offscreen` en Linux. Sirven `--smoke-page`, `--smoke-settings-page`, `--smoke-anime`, `--smoke-franchise`, `--smoke-library-view` y `--smoke-archive`; `--smoke-flow create|archive|settings` recorre esos diálogos con teclado y deja `Flow PASS/FAIL` en el `.log` de la captura. Offscreen dibuja con el renderizador por software (los diálogos salen transparentes): el aspecto real de Linux se mira con Xvfb y OpenGL.

## Git

- Conventional Commits en inglés, cambios pequeños y enfocados. Ramas: `dev` para trabajar, `main` para lo publicado.
- No confirmes `out`, binarios, bases SQLite ni rutas o datos personales.
