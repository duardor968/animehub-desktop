# AnimeHub Desktop

Aplicación nativa de Windows para seguir anime: una biblioteca personal, el catálogo de AnimeHub, descargas que se gestionan solas y reproducción con el minuto exacto donde te quedaste. Comparte la API pública de AnimeHub con la web, pero no la replica: la biblioteca y el progreso viven solo en tu equipo.

## Qué hace

- **Biblioteca**, al estilo de la de Steam: la lista a la izquierda (buscar y filtrar: viendo, sin empezar, vistos, favoritos o descargados) y, a la derecha, un inicio con lo último que viste en grande, *Continuar viendo*, *Episodios nuevos*, *Próximos episodios* y *Toda la biblioteca*, o la ficha del anime elegido: puntuación, tráiler, tu progreso, relacionados y los episodios en páginas de 50.
- **Catálogo**: episodios recientes, añadidos recientemente y todo el catálogo con búsqueda, estado, género y orden.
- **Descargas nativas** de MEGA y Pixeldrain: la serie entera, lo que falta, los no vistos, un tramo o los episodios que elijas. La página Descargas las agrupa por serie, como Steam, con la velocidad de los últimos minutos y la cola reordenable. Cola persistente, pausa y reanudación (también tras cerrar la app), reintentos y espera automática cuando se agota la cuota de MEGA. Los archivos de MEGA se descifran y se verifica su integridad antes de aparecer en la carpeta.
- **Reproducción**: con el reproductor incluido (mpv) AnimeHub sabe en qué minuto te quedaste y marca el episodio como visto al terminarlo; con la aplicación predeterminada de Windows, abrirlo lo marca como visto. Cualquier episodio se puede marcar a mano como visto, no visto o en curso desde un minuto concreto.
- **Episodios nuevos**: AnimeHub comprueba la biblioteca mientras está abierto (también en la bandeja), avisa y, si lo activas en un anime, los descarga solo.
- **JDownloader** (opcional) para MP4Upload y 1Fichier: el de este equipo por Click'n'Load o cualquiera de tus equipos por MyJDownloader. De la cuenta solo se guardan las claves derivadas, en el Administrador de credenciales de Windows.

## Compilar

Requisitos: Visual Studio 2022 (o Build Tools) con *Desktop development with C++*, Qt 6.11 para MSVC 2022 64-bit con el módulo **Qt Image Formats** (las portadas son WebP), el MinGW de Qt (en *Build Tools* del instalador de Qt; basta cualquier `strip` de binutils en el `PATH`) y CMake 4.0 o posterior.

```powershell
.\scripts\build.ps1                         # Debug y pruebas
.\scripts\build.ps1 -Configuration Release  # carpeta lista para usar
```

El script prepara MSVC, Qt y CMake solo para su proceso; no toca el `PATH` global. `-Fresh` reconfigura desde cero. Si Qt está en otra ruta, define `QT_ROOT`.

La primera configuración descarga **aria2 1.37.0** y **mpv 0.41.0** desde sus versiones oficiales y comprueba su SHA-256 (fijado en [`cmake/Engines.cmake`](cmake/Engines.cmake)); quedan en `out/engine-cache` para las siguientes. A mpv se le quita la información de depuración que trae incrustada, más de un tercio de su tamaño.

## Instalación

Cada versión está en [Releases](https://github.com/duardor968/animehub-desktop/releases), en dos formas:

- `AnimeHub-<versión>-setup.exe`: instalador para tu usuario, sin permisos de administrador, en `%LOCALAPPDATA%\Programs\AnimeHub`. Añade AnimeHub al menú Inicio y se desinstala desde Configuración → Aplicaciones; desinstalar pregunta antes de borrar la biblioteca y nunca toca los vídeos descargados.
- `AnimeHub-<versión>-win64.zip`: la misma carpeta para usar desde cualquier sitio, sin instalar.

Las dos funcionan en Windows 10 (1809) o posterior sin Qt ni Visual C++ instalados; `SHA256SUMS.txt` trae sus sumas. Al no estar firmadas, Windows avisa la primera vez que se abren.

Los datos (biblioteca, ajustes y cachés) van en `%LOCALAPPDATA%\AnimeHub`; las descargas, por defecto, en `Descargas\AnimeHub`. Con *Ajustes → General → Al iniciar Windows*, AnimeHub se abre en la bandeja al iniciar sesión.

Compilada aquí, la carpeta `out/build/windows-release` es la aplicación y `.\scripts\package.ps1` la empaqueta como una release. Para probar sin tocar tu biblioteca:

```powershell
.\out\build\windows-release\AnimeHub.exe --data-dir "$env:TEMP\animehub-prueba"
```

## Pruebas

`build.ps1` en Debug ejecuta `ctest`: lectura estricta del contrato de la API, caché sin conexión, base de datos, cifrado de MEGA contra vectores calculados con OpenSSL, protocolo de MyJDownloader contra la librería de la web, la cola completa con aria2 real contra un servidor local (pausa, cancelación, cuota, reanudación tras reiniciar, archivos dañados) y el reproductor con mpv real sin ventana.

## Estructura

- `src/core`: dominio sin interfaz (API, biblioteca SQLite, descargas, reproducción y lo específico de Windows tras interfaces pequeñas).
- `src/app`: modelos que ve QML, bandeja, avisos y arranque.
- `qml`: la interfaz (Qt Quick con el estilo Fluent de Qt).
- `tests`: Qt Test, con fixtures reales de la API.

## Relación con el resto de AnimeHub

- [AnimeHub API](https://github.com/duardor968/animehub-api): contrato REST/OpenAPI y procesamiento de AnimeAV1 (el escritorio no hace scraping).
- [AnimeHub Frontend](https://github.com/duardor968/animehub-frontend): la web pública.

## Licencia

[GNU GPL v3 o posterior](LICENSE). aria2 (GPL-2.0-or-later) y mpv (GPL-2.0-or-later) se descargan en la compilación y se incluyen con sus licencias en `engines/`.
