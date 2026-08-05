# AnimeHub Desktop

AnimeHub Desktop será la aplicación nativa para mantener una biblioteca personal, reproducir mediante aplicaciones externas y administrar descargas. No replica la web: comparte su API pública, pero ofrece una interfaz y responsabilidades propias del escritorio.

> **Estado:** scaffold ejecutable para Windows. Solo existe una ventana Qt Quick, configuración CMake y una prueba de identidad; todavía no hay red, biblioteca, persistencia ni motores de descarga.

## Relación con AnimeHub Web

- **AnimeHub Web** descubre catálogo públicamente, sin cuentas, y aloja el API y el scraper de AnimeAV1.
- **AnimeHub Desktop** consume ese API, mantiene estado exclusivamente local y administra tareas de larga duración.
- El proyecto anterior `Anime_downloader` es un antecedente conceptual. No se copia su código, historial ni interfaz.

## Stack

- C++26 obligatorio, sin extensiones del compilador
- Qt 6.11: Core, Gui, Qml, Quick, QuickControls2 y Qt Test
- CMake 4.0+, Ninja y MSVC 2022 x64 (19.44 en el entorno inicial)
- clang-format y clang-tidy
- Windows como primera plataforma de CI; la estructura evita APIs de Windows innecesarias

## Requisitos en Windows

1. Visual Studio 2022 o Build Tools con **Desktop development with C++**.
2. Qt 6.11.0, kit **MSVC 2022 64-bit**.
3. CMake y Ninja. La instalación de Qt puede suministrar ambos.

El script no modifica el `PATH` global. Localiza Visual Studio, activa MSVC x64 y añade Qt, CMake y Ninja únicamente a su propio proceso. Puedes usar otra ubicación de Qt definiendo `QT_ROOT` antes de invocarlo.

## Compilar y probar

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1
```

Para Release:

```powershell
.\scripts\build.ps1 -Configuration Release
```

Equivalente dentro de una sesión que ya tenga MSVC y `QT_ROOT` configurados:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

El ejecutable Debug queda bajo `out/build/windows-debug/AnimeHub.exe`.

## Arquitectura objetivo

La aplicación consumirá REST/OpenAPI de AnimeHub Web y conservará una caché offline. El scraper seguirá siendo exclusivo del servidor: no se implementará una segunda versión en C++.

La biblioteca se almacenará localmente en SQLite y será reconstruible. Visto, pendiente, favorito y estados similares serán colecciones de datos, no carpetas físicas. La reproducción se delegará a la aplicación externa elegida por el usuario.

Para v1, MEGA y Pixeldrain serán proveedores nativos. aria2 manejará descargas HTTP. JDownloader será una integración opcional para enviar trabajos a una instancia local o remota, no un requisito ni un backend embebido. Las notificaciones de nuevos episodios estarán activadas por defecto y la descarga automática será configurable por anime.

## Roadmap

1. Definir el cliente tipado para `/api/v1` y la estrategia de caché offline.
2. Diseñar el esquema SQLite de biblioteca y colecciones personales.
3. Integrar reproducción externa y selección segura de aplicaciones.
4. Implementar proveedores nativos MEGA/Pixeldrain y aria2.
5. Añadir JDownloader como salida opcional local/remota.
6. Incorporar notificaciones y automatización granular por anime.

No se integran todavía SQLite, red, bandeja del sistema, MEGA SDK, aria2, JDownloader, archivos ni módulos vacíos que simulen producto.

## Licencia

AnimeHub Desktop se distribuye bajo [GNU GPL v3 o posterior](LICENSE).
