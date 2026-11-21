# Monitor — especificación de integración

Estado: v1, 19-11-2026. Fuente de verdad de las decisiones; el roadmap y la aceptación se definen acá.

Proyecto: **Monitor**, unión de dos apps de monitoreo de Windows:

- **Base:** BatteryHub — fork de `krisgfx9/Wireless-Device-Battery-Monitor` (GPL-3.0), repo local `battery-monitor`. Aporta shell: widget de taskbar multi-monitor, drivers HID de batería, bandeja, menú, i18n, historial, alertas, updater firmado, instalador Inno per-user.
- **Se porta:** solo la capa de datos de TraySense — fork de `DMTWEAKS/TraySense` (AGPL-3.0), repo local `usage-monitor`, `traysense.c` (C99, un archivo). Aporta métricas del sistema y sus fuentes de lectura. Su UI (strip propio, UIA, tray, menú, settings, autostart, instalación) **no se porta**.
- Licencia resultante: **AGPL-3.0** (la combinación GPL-3.0 + AGPL-3.0 la permite AGPL §13; las partes de BatteryHub quedan bajo GPL-3.0). El nombre y logo "TraySense" no se usan (fuera de su licencia).

## Objetivo

Una sola app nativa, sin admin, que muestra en la taskbar: métricas del sistema (CPU %, temp, RAM %, GPU %, temp, VRAM, temp SSD) **a la izquierda** y baterías de dispositivos inalámbricos **a la derecha**, con el mismo espaciado entre todos los ítems. Uso personal + compartir con un par de amigos. Repo público propio `ignaciorevainera/monitor`, historial git del fork conservado (remote `upstream` para drivers futuros).

## No-objetivos

- Sin admin, sin servicios, sin drivers: **PawnIO se descarta** (era la única métrica que exigía elevación: CPU-die-temp).
- Sin reescritura del widget ni del updater; sin frameworks nuevos; sin plugins.
- Sin métricas en Linux (el lado Linux queda solo-baterías, como hoy).
- Sin firma Authenticode ni activación del updater en este hito (backlog).
- No mantener compatibilidad de marca con upstream; sí mantener upgrade in-place del install existente.

## Decisiones cerradas

| # | Decisión | Detalle |
|---|---|---|
| D1 | Repo público | `ignaciorevainera/monitor`, historia del fork, upstream remoto |
| D2 | Sin admin | CPU temp = PDH Thermal Zone (fallback nativo de TraySense); GPU temp = KMT (+ NVML si NVIDIA); SSD temp = IOCTL access-0. Sin PawnIO |
| D3 | Upgrade in-place | Mismo Inno AppId `{7E0B5C2A-3F4D-4C8E-9B61-2D5A8F1C4E93}`; internos (clase de ventana, mutex, raíz de registro, Run value, carpetas de datos) se conservan — ver "Identidad" |
| D4 | Sin firma de release | CHANNEL dev ⇒ updater inerte; `Repository`/asset/user-agent se repuntan igualmente (cableado listo); activación en backlog |
| D5 | Bandeja | Iconos **por métrica** (uID `0x2000 + m`); en Win11 caen al overflow oculto hasta promoción manual — documentado en README |
| D6 | Ubicación por métrica | Barra / Bandeja / Oculta; default Barra |
| D7 | Orden del strip | Métricas a la izquierda, baterías a la derecha, **un único gap** (`Px(14)`) entre ítems; opciones de orden/gap → backlog |
| D8 | Refresh métricas | Default 5 s; menú 1/2/5 s; baterías siguen con su poll propio (30–3600 s) |
| D9 | Hover | El resaltado de fondo al pasar el mouse se **mantiene** con animación fade-in/out estilo Windows 11 (100 ms in / 180 ms out, ease-out cúbico, alfa 20/26, respeta `SPI_GETCLIENTAREAANIMATION`); tooltip y clic intactos |
| D10 | Iconos | Solo Segoe Fluent Icons, mismo renderer que baterías (CPU `EEA1`, RAM `EEA0`, GPU `E950`, temp `E957`, SSD `EDA2`, VRAM = RAM + caption `V`). Opción global de etiqueta por métrica (Icono default / Palabra corta / Ambos); dispositivos inalámbricos sin cambios |
| D11 | Umbrales de temp | CPU/GPU 80/90 °C, SSD 65/75 °C; naranja/rojo como TraySense |
| D12 | Clic | Clic derecho abre el menú (conserva "Abrir Administrador de tareas"); clic izquierdo abre el Administrador de tareas |
| D13 | Idiomas | Inglés + español completos; el resto cae a inglés |
| D14 | Métricas default | Heredan `Mask2` de TraySense (63: todo menos SSD temp) al migrar; en installs nuevos, todo menos SSD temp |
| D15 | Autostart | Un único mecanismo: HKCU Run (sin admin) |

## Diseño

### Modelo de ítems del widget

`WidgetItem` (`src/widget.h:29`) ya existe; se **extiende**, no se crea:

```
enum class MetricId { CpuUsage, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp };  // orden de TraySense
struct WidgetItem {
    // ...campos existentes (baterías)...
    MetricId metric;                     // item de métrica cuando kind == Other y metric != -1
    wchar_t glyph;                       // glifo explícito para métricas (Segoe Fluent)
    int severity;                        // 0 normal, 1 naranja, 2 rojo (temperaturas)
    int priority;                        // para ocultar por ancho: métricas antes que baterías
};
```

`UpdateWidget()` (`src/main.cpp:604`) ensambla en orden: ítems de métricas (Barra) → glifo `…` (si hubo ocultos) → dispositivos. Gap único entre ítems: hoy el layout usa `Px(5)` ícono→texto y `Px(14)` entre dispositivos (`src/widget.cpp:424-438`); se normaliza a un solo gap inter-ítem.

El strip se repinta solo si cambió algún valor visible (patrón firma de `main.cpp:528-534`, aplicado también a las métricas).

### Sampler

`src/metrics/` (solo Windows; se agrega a los wildcards de `Makefile:12` y `build.bat:16`, **no** a `build-linux.sh`):

- `metrics.h` — enum, `MetricsSnapshot { double val[7]; bool ok[7]; }`, funciones puras de formato/severidad/umbrales (testeables).
- `metrics_sampler.cpp` — hilo propio, 5 s default, `PostMessage` del snapshot a la ventana principal. Prioridad `THREAD_PRIORITY_BELOW_NORMAL`. Pausa cuando la pantalla está apagada: `RegisterPowerSettingNotification(g_hwnd, GUID_CONSOLE_DISPLAY_STATE)` extendiendo el `WM_POWERBROADCAST` existente (`src/main.cpp:1081-1083`).
- `metrics_sources.cpp` — lecturas portadas verbatim de `traysense.c`: CPU % `GetSystemTimes` (206-217), RAM % `GlobalMemoryStatusEx` (219-227), GPU % PDH `\GPU Engine(*)\Utilization Percentage` + VRAM `\GPU Adapter Memory(*)\Dedicated Usage` con `PdhAddEnglishCounterW` (578-590), GPU temp KMT `D3DKMTQueryAdapterInfo` tipo 62 + fallback NVML `nvml.dll` (348-366), CPU temp PDH `\Thermal Zone Information(*)\Temperature` con Kelvin→°C (260-269, 584-585), SSD temp IOCTL `StorageDeviceTemperatureProperty` sobre `\\.\PhysicalDriveN` access-0 (541-576).

Selección de GPU: la de TraySense (primera LUID activa). CPU temp via Thermal Zone es temperatura de zona/placa, no de die: el tooltip lo aclara; sin sensor ⇒ `--`.

Sin PawnIO: no se embeben módulos, no hay descarga pineada, no hay código de elevación, no hay tarea programada. El servicio PawnIO preexistente queda huérfano (remoción manual opcional, en el checklist de retiro).

### Posicionamiento y overflow (etapa 1, no después)

`Reposition` (`src/widget.cpp:959-1029`) no mide los botones de apps de Win11 (XAML); con 6 métricas + dispositivos el strip los solaparía. Se extiende el **hilo UIA existente** (`src/widget.cpp:52-61`, que ya evita el deadlock Explorer/UIA) para que, además del botón Widgets, mida el borde derecho de los botones de apps (`appsR`, técnica de `traysense.c:1058-1068`). Con eso:

1. **Compacto**: si `anchoStrip > espacioLibre`, labels cortos y gaps reducidos (como `traysense.c:1117-1123`).
2. **Ocultamiento por prioridad**: si aún no entra, se ocultan ítems de menor prioridad (métricas antes que baterías; never la batería en low) y aparece un glifo `…` cuyo tooltip lista los ocultos. Nunca se tapa un botón.

### Bandeja

- uID métricas: `0x2000 + m` — `traysense.c:827` usa `m + 1` que **choca** con `PLACEHOLDER_ID=1` y `ALERT_ICON_ID=2` (`src/main.cpp:43-44`); baterías siguen en `StableId` (≥ 0x100).
- Iconos con el mismo `RenderIcon` de baterías (glifo Fluent + valor corto, sin `%` si no entra) y regeneración solo al cambiar el valor (patrón iconSig).
- Tooltip con valor completo (RAM usada/total, GPU name, temp + umbral).
- Nota UX: en Win11 los iconos nuevos empiezan en el overflow oculto; el README indica cómo promoverlos (D5).

### Menú

Nuevo submenú **"Métricas"**: por métrica, ubicación radio (Barra/Bandeja/Oculta) + frecuencia (1/2/5 s). Además: "Mover a la izquierda/derecha" y "Restablecer posición" (escriben el `TaskbarOffset` existente que `widget.cpp` ya lee), y "Abrir Administrador de tareas". Persistencia en la raíz de registro actual (claves nuevas tipo `MetricMask`, `MetricLocation`, `MetricInterval`).

### Identidad y upgrade in-place (D3)

**Se renombra (visible):** exe `Monitor.exe` (targets de `Makefile:6` y `build.bat:20`), `AppName`/strings del `.iss` + `OutputBaseFilename=Monitor-Setup-{#AppVersion}`, `res/app.rc` (FileDescription/ProductName/OriginalFilename), `res/app.manifest` (assemblyIdentity), `kAppName` en `main.cpp`, frases con "BatteryHub" en `lang/*.txt` y `_template.txt`, README/docs, user-agent del updater (`update_win.cpp:114,371`), asset esperado `Monitor-Setup-…` (`update_core.cpp:247` + `update_win.cpp:440`), `Repository` → `ignaciorevainera/monitor` (`update_core.h:7`).

**Se conserva (interno, a propósito):** clase de ventana `WirelessBatteryWnd` (así el `--quit` del install viejo y nuevo siguen cerrando la app; `main.cpp:1226`), mutex `Local\WirelessBatteryTray` (impide que Monitor y el BatteryHub viejo corran a la vez: dos strips no compiten), raíz de registro `Software\WirelessBattery`, Run value `WirelessBattery` (el upgrade mantiene el autostart sin migrar nada), carpetas `%LOCALAPPDATA%\WirelessBattery` (historial/updates) y `%APPDATA%\WirelessBattery\lang`. Todo marcado con un comentario de una línea: `shortcut: identidad interna heredada para upgrade in-place, renombrar tras el hito 2`. Renombrado interno + migración de datos → backlog.

**Consecuencias aceptadas:** cero migración de settings/historial/lenguajes en este hito (D3 los hace innecesarios); el uninstall del `.iss` (`iss:186`) sigue borrando la raíz correcta; `[InstallDelete]` agrega `BatteryHub.exe` al lado del `WirelessBattery.exe` que ya limpia hoy.

**Gate de renombre:** al cerrar la etapa 0, `grep -ri "BatteryHub" --exclude-dir=.git` debe devolver solo: historial de upstream (CHANGELOG-type docs), menciones legales/de atribución y frases ya migradas — ninguna cadena visible al usuario.

### Build y CI

- `Makefile` + `build.bat`: wildcard `src/metrics/*.cpp`; tests nuevos registrados en el target `test` (patrón de `Makefile:118-126`); `build-linux.sh` sin cambios.
- `.github/workflows/build.yml:33` se deja como está: al no ser el repo de krisgfx9, todo build es CHANNEL dev ⇒ updater inerte (D4). La activación (clave + `stable` + gate) es backlog y se hace en un solo commit.

### Licencias y atribución

- `LICENSE` → AGPL-3.0; las cabeceras GPL-3.0 de BatteryHub se mantienen (AGPL §13 permite la combinación).
- `installer/THIRD_PARTY_NOTICES.txt`: créditos BatteryHub/krisgfx9 (GPL-3.0) y TraySense/DMTWEAKS (AGPL-3.0, origen del código de métricas). TraySense ya no redistribuye PawnIO (lo descartamos), así que no hay avisos nuevos de módulos.
- Nombre y logo "TraySense" no se usan.

## Etapas

### Etapa 0 — Fundación
Repo `monitor` desde la historia del fork; renombre visible (D3) con gate grep; cableado del updater (Repository/asset/UA, CHANNEL dev); `vendor/traysense/` con `traysense.c` como referencia + aviso AGPL; checklist de retiro documentado.
**Aceptación:** build reproducible (`build.bat` y `make`); Monitor.exe (dev) corre junto al BatteryHub viejo **sin** coexistir (mutex) y reemplaza su función widget+baterías; el BatteryHub viejo sigue instalado e intacto (rollback).

### Etapa 1 — Métricas nativas sin admin
`src/metrics/` (CPU %, RAM %, GPU %, VRAM), WidgetItem extendido, UIDs `0x2000+`, appsR + compacto + `…` en el hilo UIA, gap uniforme, sin hover, menú Métricas (ubicación/frecuencia), bandeja por métrica, tooltips, wildcards de build + tests de las funciones puras.
**Aceptación:** con 6 métricas + dispositivos no se solapa nada (medido contra appsR); valores dentro de ±2 de TraySense corriendo simultáneo; sin cambio visual al pasar el mouse; `make test` verde en CI.

### Etapa 2 — Temperaturas sin admin
CPU temp (Thermal Zone), GPU temp (KMT; NVML solo NVIDIA), SSD temp (IOCTL access-0), severidad naranja/rojo (D11), tooltips con sensor y umbral.
**Aceptación:** temps vs HWiNFO con sensor nombrado y tolerancia (AMD: Tctl/Tdie); IOCTL de SSD verificado sin elevación; sin sensor ⇒ `--` sin romper nada.

### Etapa 3 — Una sola app
Upgrade in-place con el instalador (mismo AppId; `[InstallDelete]` limpia BatteryHub.exe); migración única de `HKCU\Software\TraySense` (lectura una vez: `Mask2`→toggles, `Interval`, `Offset`→`TaskbarOffset`; `Mode`/`ShowOn` se descartan — la ubicación por métrica los reemplaza); checklist manual de retiro de TraySense: borrar tarea `TraySense` (elevado, una vez), borrar `C:\Program Files\TraySense\TraySense.exe`, opcional desinstalar PawnIO desde Configuración > Aplicaciones.
**Aceptación:** tras reinicio de sesión hay un solo proceso (Monitor), cero tareas/valores de autostart ajenos, sin strips duplicados.

### Etapa 4 — Pulido
Alertas de temperatura (patrón `alert_rules.h`, umbrales D11), frases nuevas en `_template.txt` + `es.txt` (el resto cae a inglés), README (instalación, overflow de bandeja, retiro de TraySense), ajuste fino de overflow.
**Aceptación:** una semana de uso diario sin tocar configuración.

### Etapa 5 — Distribución
Release de GitHub (sin firma): `Monitor-Setup-x.y.z.exe` + portable zip; README con aviso SmartScreen para amigos.
**Aceptación:** un amigo instala desde la release y todo funciona con defaults sensatos.

### Etapa 6 — Futuro (backlog corto)
Opciones de orden y gap en menú (D7); renombrado interno + migración de datos (identidad heredada, D3); `MetricProvider` para métricas nuevas (red/energía/uptime); historial genérico reusando `battery_history`; activación del updater (clave nueva + CHANNEL stable + `build.yml:33` + `sign-release.ps1`); Authenticode si se justifica.

## Riesgos y rollback

- **UIA/XAML (mayor):** cada build grande de Win11 puede mover el layout; mitigación: modo bandeja siempre disponible; el scan vive confinado al hilo UIA existente.
- **Overflow (medio):** si appsR falla en un monitor, ese espejo se oculta (regla actual: nunca tapar a ciegas).
- **Rollback:** el BatteryHub viejo no se desinstala hasta la aceptación de la etapa 3; cualquier etapa abortada deja el status quo funcionando. TraySense se retira recién al final; sus fuentes quedan clonadas.
- **Updater inerte** hasta backlog: sin riesgo de auto-reemplazo por releases ajenas (CHANNEL dev).

## Preguntas resueltas durante la revisión (contexto)

Admin solo lo exigía PawnIO/CPU-die-temp ⇒ descartado (D2). Upgrade in-place ⇒ internos conservados (D3). Sin firma ⇒ updater inerte por ahora (D4). Bandeja por métrica pese al overflow de Win11 (D5).
