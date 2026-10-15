# Agent Instructions

## Propósito

AnimeHub Desktop es una aplicación nativa C++26 con Qt Quick para Windows: biblioteca local, catálogo vía AnimeHub API, descargas nativas, reproducción externa, bandeja y avisos. La documentación se escribe en español; código, nombres de archivos, identificadores y commits, en inglés.

## Límites del producto

- No replica AnimeHub Web. Consume `animehub-api` bajo `/api/v1`, con caché local de respuestas para uso sin conexión.
- Sin scraping en C++: AnimeAV1 se procesa solo en el servidor.
- La biblioteca es local, privada y reconstruible (SQLite). Visto y favorito son datos, no carpetas.
- Descargas nativas solo de MEGA (API pública, descifrado AES-CTR y verificación del MAC) y Pixeldrain, siempre con aria2 por JSON-RPC. Otros servidores, solo a través de JDownloader (Click'n'Load local o MyJDownloader).
- La cola nunca guarda URLs firmadas, claves de enlace ni credenciales; una tarea reanudada vuelve a resolver su origen.
- Reproducción externa: mpv incluido por defecto (seguimiento por IPC) o la aplicación predeterminada del sistema (abrir marca visto). Nada de reproductor embebido sin una nueva decisión. Cerrar AnimeHub no detiene un vídeo.
- Avisos de episodios nuevos activados por defecto; la descarga automática se activa por anime y con consentimiento explícito (destino y forma de desactivarla).
- Credenciales de servicios opcionales solo en el almacén del sistema (Administrador de credenciales); de MyJDownloader, solo las claves derivadas.

## Interfaz

- Referencia: la biblioteca de Steam. Pestañas arriba (Biblioteca · Catálogo · Descargas) y Ajustes en un diálogo; en Biblioteca, lista a la izquierda y a la derecha el inicio con portadas o la ficha. Barra de descarga abajo mientras quede algo sin terminar, también en pausa o con error.
- Controles del estilo FluentWinUI3 de Qt sin re-estilar, tema oscuro, acento de marca `#2f81f7`, iconos Segoe Fluent Icons (`image://glyph/<código>` para `icon.source`). Tokens en `qml/Theme.qml`.
- Menús con `ActionMenu` (con `entries`, solo las entradas cuyo `wanted` aplica) y `MenuEntry`, diálogos con `ModalDialog` y portadas con los tokens `poster*` de `Theme`: corrigen el ancho fijo de los menús de Fluent, la alineación de sus textos, la barra de título bajo el velo modal y los tamaños entre filas y rejillas.
- Teclado, foco visible, nombres accesibles y la acción "pulsar" de accesibilidad en todo control; la ventana debe funcionar desde su ancho mínimo.
- Las acciones destructivas o automáticas comunican destino, alcance y recuperación (los archivos van a la papelera).

## Arquitectura y C++

- `CMAKE_CXX_STANDARD 26`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`.
- `src/core` no depende de la interfaz; lo específico de Windows (CNG, Job Objects, credenciales) va tras interfaces pequeñas en `src/core/platform`.
- QML solo presenta; reglas, persistencia y red en C++. QML ve modelos e interfaces mínimas (`src/app`), nunca objetos del núcleo.
- RAII, tipos de valor, ownership explícito, `std::expected` para errores. Sin singletons globales.
- Dependencias nuevas: licencia compatible con GPL-3.0-or-later y razón concreta. Los motores se fijan por URL y SHA-256 en `cmake/Engines.cmake`.

## Toolchain y calidad

- MSVC 2022 x64 y Qt 6.11 MSVC con Qt Image Formats, más un `strip` de MinGW (el de Qt Tools) para la información de depuración de mpv; `scripts/build.ps1` prepara el entorno solo para su proceso (`-Fresh`, `-Configuration Release`, `-SkipTests`).
- La carpeta Release es la instalación: `cmake/Deploy.cmake` despliega Qt y el runtime de MSVC y quita lo que la app no carga (comprobado con los módulos cargados en un recorrido completo). `scripts/package.ps1` la empaqueta (zip portable e instalador por usuario de Inno Setup, `installer/AnimeHub.iss`) y un tag anotado `vX.Y.Z` igual a la versión de `CMakeLists.txt` la publica desde `.github/workflows/release.yml`; el mensaje del tag son las notas.
- Formato con `.clang-format` (`clang-format` de Qt Creator); pruebas con Qt Test. Las de descargas y reproducción usan aria2 y mpv reales contra un servidor local: no requieren red.
- Antes de entregar cambios transversales: formato, `.\scripts\build.ps1` (Debug con `ctest`) y la Release arrancada sin Qt en el `PATH`.

## Git

- Conventional Commits en inglés, cambios pequeños y enfocados. Ramas: `dev` para trabajar, `main` para lo publicado.
- No confirmes `out`, binarios, bases SQLite ni rutas o datos personales.
