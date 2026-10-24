# AnimeHub Desktop

Aplicación nativa de Windows y Linux para seguir anime: una biblioteca personal, el catálogo de AnimeHub, descargas que se gestionan solas y reproducción con el minuto exacto donde te quedaste. Comparte la API pública de AnimeHub con la web, pero no la replica: la biblioteca y el progreso viven solo en tu equipo.

## Qué hace

- **Biblioteca**, al estilo de la de Steam: la lista a la izquierda (buscar, filtrar por estado o solo lo que está en disco, y agrupar por franquicia) y, a la derecha, un inicio por tareas: *Para ver* (el siguiente episodio ya está en disco), *Para descargar* (lo que falta de lo que sigues, en un clic) y los *Próximos episodios* por día. La lista entera, en portadas, a un botón; la ficha de cada anime o franquicia con tu progreso, sus relacionados y los episodios en páginas de 50.
- **Catálogo**: episodios recientes, añadidos recientemente y todo el catálogo con búsqueda, estado, género y orden.
- **Descargas nativas** de MEGA y Pixeldrain: la serie entera, lo que falta, los no vistos, un tramo o los episodios que elijas. La página Descargas las agrupa por serie, como Steam, con la velocidad de los últimos minutos y la cola reordenable. Cola persistente, pausa y reanudación (también tras cerrar la app), reintentos y espera automática cuando se agota la cuota de MEGA. Los archivos de MEGA se descifran y se verifica su integridad antes de aparecer en la carpeta.
- **Reproducción**: con mpv (incluido en Windows; en Linux, el del sistema) AnimeHub sabe en qué minuto te quedaste y marca el episodio como visto al terminarlo; con la aplicación predeterminada del sistema, abrirlo lo marca como visto. Cualquier episodio se puede marcar a mano como visto, no visto o en curso desde un minuto concreto.
- **Episodios nuevos**: AnimeHub comprueba la biblioteca mientras está abierto (también en la bandeja), avisa y, si lo activas en un anime, los descarga solo.
- **JDownloader** (opcional): descarga lo que solo está en MP4Upload o 1Fichier, o todo si lo eliges como motor, en la carpeta de AnimeHub y con su progreso en Descargas. El de este equipo por su API local o cualquiera de tus equipos por MyJDownloader. De la cuenta solo se guardan las claves derivadas, en el almacén del sistema: el Administrador de credenciales en Windows, el llavero (Secret Service) en Linux.

## Compilar

Requisitos: Visual Studio 2022 (o Build Tools) con *Desktop development with C++*, Qt 6.11 para MSVC 2022 64-bit con el módulo **Qt Image Formats** (las portadas son WebP), el MinGW de Qt (en *Build Tools* del instalador de Qt; basta cualquier `strip` de binutils en el `PATH`) y CMake 4.0 o posterior.

```powershell
.\scripts\build.ps1                         # Debug y pruebas
.\scripts\build.ps1 -Configuration Release  # carpeta lista para usar
```

El script prepara MSVC, Qt y CMake solo para su proceso; no toca el `PATH` global. `-Fresh` reconfigura desde cero. Si Qt está en otra ruta, define `QT_ROOT`.

La primera configuración descarga **aria2 1.37.0** y **mpv 0.41.0** desde sus versiones oficiales y comprueba su SHA-256 (fijado en [`cmake/Engines.cmake`](cmake/Engines.cmake)); quedan en `out/engine-cache` para las siguientes. A mpv se le quita la información de depuración que trae incrustada, más de un tercio de su tamaño.

En Linux (Ubuntu 24.04 o equivalente): GCC 14, CMake 4.0 o posterior, Qt 6.11 `linux_gcc_64` con **Qt Image Formats**, `libssl-dev`, `libsecret-1-dev`, y `mpv` y `aria2` para usarla y para las pruebas. Con `QT_ROOT` apuntando al kit:

```bash
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
cmake --preset linux-release && cmake --build --preset linux-release && ./scripts/package-linux.sh
```

La primera configuración descarga la fuente de iconos (Uno Fluent Icons) y comprueba su SHA-256. Desde Windows, [`scripts/linux-build.Dockerfile`](scripts/linux-build.Dockerfile) prepara el mismo entorno en un contenedor.

## Instalación

Cada versión está en [Releases](https://github.com/duardor968/animehub-desktop/releases), con `SHA256SUMS.txt` para comprobar cada archivo.

**Windows** 10 (1809) o posterior, sin Qt ni Visual C++ instalados:

- `AnimeHub-<versión>-setup.exe`: instalador para tu usuario, sin permisos de administrador, en `%LOCALAPPDATA%\Programs\AnimeHub`. Añade AnimeHub al menú Inicio y se desinstala desde Configuración → Aplicaciones; desinstalar pregunta antes de borrar la biblioteca y nunca toca los vídeos descargados.
- `AnimeHub-<versión>-win64.zip`: la misma carpeta para usar desde cualquier sitio, sin instalar.

Al no estar firmadas, Windows avisa la primera vez que se abren.

**Linux** x86-64:

- `animehub_<versión>_amd64.deb`, para Ubuntu 24.04 o posterior y sus derivadas: `sudo apt install ./animehub_<versión>_amd64.deb` instala también mpv y aria2.
- `AnimeHub-<versión>-x86_64.AppImage`, para cualquier distribución con glibc 2.39 o posterior: `chmod +x` y ábrelo. Usa el mpv y el aria2 del sistema; si falta alguno, AnimeHub dice qué paquete instalar.

Los datos (biblioteca, ajustes y cachés) van en `%LOCALAPPDATA%\AnimeHub` en Windows y en `~/.local/share/AnimeHub` en Linux; las descargas, por defecto, en `Descargas/AnimeHub`. Con *Ajustes → General → Al iniciar Windows* (*Al iniciar sesión* en Linux), AnimeHub se abre al iniciar sesión, en la bandeja si el escritorio tiene una.

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
