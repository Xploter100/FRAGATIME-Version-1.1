# FRAGATIME v1.1

Plugin VST3 granular para Windows 64 bits (FL Studio).

## Novedades de la v1.1
- **TABIE**: eliges el genero, el plugin detecta el BPM del audio cargado y una red neuronal
  pequena (19 -> 32 -> 15, ~1.1 mil pesos) propone **3 opciones de fragmentacion** (A tight, B groove, C wide).
- **LIKE / DISLIKE**: cada voto reentrena la red localmente (en tu PC). El modelo se guarda en
  `%APPDATA%\TapeLab\FRAGATIME\tabie_model_v1.bin`. Boton RESET para olvidar lo aprendido.
- **SYNC**: DENSITY y SIZE pasan a ser subdivisiones del tempo (1/16, 1/8T, etc.).
- **EXPORT**: renderiza 1/2/4/8 compases a WAV 24 bits.
  - *Save WAV to computer...*: lo guardas donde quieras.
  - *Send to FL Studio*: guarda en Documentos\FRAGATIME Exports y aparece el cuadro **DRAG TO FL**;
    arrastralo al playlist de FL Studio.
  - Funciona igual despues de Tabie o con tus ajustes manuales (exporta lo que suena con tus perillas).

## Compilar con GitHub (sin instalar nada)
1. Crea un repositorio nuevo y sube el contenido de esta carpeta
   (CMakeLists.txt, carpeta Source y carpeta .github).
2. Pestana **Actions**: la compilacion tarda unos 10-15 minutos.
3. Descarga **FRAGATIME-v1.1-VST3-Windows** (Artifacts).
4. Copia la carpeta FRAGATIME.vst3 a `C:\Program Files\Common Files\VST3`.
5. En FL Studio: Options > Manage plugins > Find installed plugins.

## Memoria
El audio cargado vive en RAM (maximo 4 min, ~90 MB). El modelo de Tabie pesa unos KB.
