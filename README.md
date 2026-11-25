# EC Audio — DAW para grabar y editar voces (macOS)

DAW en C++17 con [JUCE 7](https://juce.com), pensado para grabación y edición de voz.
Estética inspirada en tu captura (azul noche + turquesa) y flujo tipo Logic Pro.

## Qué incluye

| Área | Detalle |
|---|---|
| **Grabación** | WAV mono 24-bit directo a disco (hilo dedicado, sin cortes), compensación automática de latencia |
| **Monitor de voz en tiempo real** | Botón **I** por pista; se activa solo al armar (R). Buffer por defecto: 128 muestras @ 48 kHz |
| **Edición** | Importar audio (WAV, AIFF, FLAC, MP3, M4A…), mover/arrastrar regiones entre pistas, dividir, duplicar, eliminar |
| **Plugins nativos** | Ecualizador de 3 bandas (graves / medios con frecuencia variable / agudos) y compresor (umbral, ratio, ataque, relajación, ganancia) |
| **Plugins externos** | **10 inserts VST3 por pista** (también carga AudioUnit), con escaneo, editor, bypass y guardado de estado |
| **Mezcla** | Volumen, pan, mute, solo, vúmetros, master con limitador de seguridad |
| **Exportación** | WAV 16 (dither TPDF) / 24 / 32-float, AIFF 24, FLAC 24 — render offline a la frecuencia de la sesión |
| **Sesiones** | Guardar / abrir `.ecaudio` (incluye plugins y su estado) |
| **Compatibilidad** | Binario universal **Intel + Apple Silicon**; destino mínimo **macOS 10.13** |

## Compilar

Requisitos: Xcode (con Command Line Tools) y CMake ≥ 3.22 (`brew install cmake`).

```bash
cd ECAudio
./build_mac.sh            # genera build/ECAudio_artefacts/Release/EC Audio.app
# o, para abrir en Xcode:
cmake -B build -G Xcode && open build/ECAudio.xcodeproj
```

La primera compilación descarga JUCE 7.0.12 automáticamente (requiere internet).

## Uso rápido

1. **EC Audio ▸ Audio ▸ Preferencias de audio** → elige tu interfaz y entrada de micrófono.
2. Pulsa **R** en la pista para armarla (activa también el monitor). Usa **audífonos** para evitar acoples.
3. **Espacio** reproduce/detiene · **R** graba · **Return** va al inicio · **K** metrónomo.
4. **Audio ▸ Administrar plugins…** → *Scan* para encontrar tus VST3, luego haz clic en un slot de **Inserts**.

### Atajos
`Espacio` play/stop · `R` grabar · `⌘E` dividir en el cursor · `⌘D` duplicar · `⌫` eliminar región ·
`⌘T` nueva pista · `⌘I` importar · `⌘⇧E` exportar · `⌘S` guardar · `⌘+rueda` zoom · `Shift+rueda` desplazar.

## Distribución

Para firmar y notarizar, el proyecto ya activa *Hardened Runtime* con los permisos
`audio-input` y `disable-library-validation` (necesario para cargar plugins de terceros).
Firma con tu Developer ID desde Xcode.

## Notas honestas

* Este código **no se compiló aquí** (no hay macOS/Xcode en este entorno). Es un proyecto completo y coherente,
  pero espera algún ajuste menor de compilación la primera vez; pásame los errores y los corrijo.
* "Cualquier versión de macOS": el mínimo real con Xcode actual es 10.13 (High Sierra). Versiones anteriores no son posibles con herramientas modernas.
* Las pistas de grabación son mono (una entrada por pista). Los plugins se procesan en estéreo.
* No hay compensación de latencia de plugins ni deshacer/rehacer todavía; son los siguientes pasos naturales.

## Obtener el DMG desde GitHub (sin Xcode local)

1. Sube esta carpeta a un repositorio de GitHub (rama `main`).
2. Ve a la pestaña **Actions ▸ Build macOS DMG** (se ejecuta solo con cada push; o pulsa *Run workflow*).
3. Al terminar, descarga **EC-Audio-dmg** desde *Artifacts* (viene dentro de un zip con `EC-Audio.dmg`).
4. Para una release pública: `git tag v1.0.0 && git push --tags` y el DMG aparece en **Releases**.

La app va firmada *ad-hoc* (sin cuenta de Apple Developer). La primera vez, macOS la bloqueará:
clic derecho sobre **EC Audio.app ▸ Abrir**, o ejecuta `xattr -cr "/Applications/EC Audio.app"`.
Para distribuirla sin avisos hace falta firmar y notarizar con un Developer ID.
