# Agent Instructions

## Propósito y estado

AnimeHub Desktop es una aplicación nativa C++26 con Qt Quick. El repositorio actual es solo un scaffold ejecutable. La ventana inicial valida toolchain y tokens visuales; no constituye el diseño final ni funcionalidad de producto.

La documentación se escribe en español. Código, nombres de archivos, identificadores y commits se escriben en inglés.

## Límites del producto

- Desktop no replica AnimeHub Web. Debe aprovechar patrones nativos para biblioteca, cola, notificaciones y tareas persistentes.
- Consume el API REST/OpenAPI alojado de AnimeHub Web bajo `/api/v1` y podrá mantener una caché offline.
- No implementes scraping en C++. AnimeAV1 se procesa exclusivamente en el servidor.
- La biblioteca es local, privada y reconstruible. SQLite será su almacenamiento cuando se diseñe el dominio.
- Visto, pendiente, favorito y otros estados son colecciones de datos, no carpetas físicas.
- La reproducción se delega a una aplicación externa; no incorpores un reproductor multimedia embebido sin una nueva decisión.
- MEGA y Pixeldrain serán proveedores nativos de v1 y aria2 administrará HTTP.
- JDownloader será una integración opcional local o remota. No intentes embeber, distribuir ni controlar internamente componentes no diseñados como librería pública.
- Las notificaciones de episodios estarán activadas por defecto. La descarga automática se configura por anime y requiere consentimiento explícito.
- No añadas módulos vacíos de red, SQLite, descargas, bandeja o archivos antes de implementar una capacidad vertical real.

## Arquitectura y C++

- Mantén `CMAKE_CXX_STANDARD 26`, `CMAKE_CXX_STANDARD_REQUIRED ON` y `CMAKE_CXX_EXTENSIONS OFF`.
- Prefiere C++ estándar y Qt multiplataforma. Aísla cualquier API específica de Windows detrás de una interfaz pequeña.
- Usa RAII, tipos de valor, ownership explícito y señales/slots tipadas. Evita singletons globales y estado mutable compartido.
- Mantén QML para presentación e interacción, y C++ para dominio, persistencia, red y coordinación. No coloques reglas de negocio en QML.
- No expongas objetos C++ completos a QML; publica interfaces mínimas y modelos adecuados.
- Usa únicamente módulos Qt necesarios. Cualquier dependencia nueva necesita licencia compatible con GPL-3.0-or-later y una razón concreta.

## UX y accesibilidad

- Conserva una identidad propia del escritorio y el nombre AnimeHub; no copies la interfaz web ni el proyecto antecedente.
- Usa tokens de color, espacio y tipografía consistentes. La dirección inicial es oscura, precisa y orientada a tareas, no una cuadrícula genérica de tarjetas.
- Soporta teclado, foco visible, lectores de pantalla, escalado de texto y pantallas HiDPI.
- Las acciones destructivas o automáticas deben comunicar destino, alcance y posibilidad de recuperación.

## Seguridad y privacidad

- No confirmes secretos, URLs firmadas, cookies, historiales personales, bases SQLite ni rutas privadas del usuario.
- Valida nombres y rutas antes de escribir; evita traversal, sobrescrituras silenciosas y ejecución de comandos construidos con texto no confiable.
- Las credenciales de servicios opcionales deben usar mecanismos seguros del sistema operativo cuando exista la implementación real.
- No envíes biblioteca, estado de reproducción o credenciales al servidor salvo una decisión explícita y documentada.

## Toolchain y calidad

- Windows usa MSVC 2022 x64 y Qt 6.11 MSVC. No alteres el `PATH` global ni reemplaces el kit MinGW instalado.
- `scripts/build.ps1` prepara MSVC, CMake, Ninja y Qt solo para su proceso.
- Formatea C++ con `.clang-format` y conserva `.clang-tidy` limpio para código tocado.
- Añade Qt Test para lógica C++ y pruebas QML cuando exista interacción real.

Antes de entregar cambios transversales:

```powershell
clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp
.\scripts\build.ps1
```

## Git

- Usa Conventional Commits en inglés y cambios pequeños y enfocados.
- No confirmes `out`, `build`, cachés CMake, binarios, DLL, PDB ni bases de usuario.
- No copies código, historial o artefactos de `Anime_downloader`; es solo antecedente conceptual.
- Mantén README y decisiones arquitectónicas sincronizados cuando cambien interfaces o requisitos.
