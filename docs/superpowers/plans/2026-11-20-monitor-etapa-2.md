# Monitor — Etapa 2 (Temperaturas sin admin) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CPU temp (Thermal Zone), GPU temp (KMT, NVML fallback) y SSD temp (IOCTL) como métricas nativas sin admin, con severidad naranja/rojo en el strip, menú y bandeja completos para las 7 métricas.

**Architecture:** Los 3 ids de temp ya están reservados en el enum (`Id::CpuTemp/GpuTemp/SsdTemp`) — cero renumeración. Fuentes porteadas verbatim de `vendor/traysense/traysense.c`. La severidad es estado visual nuevo: entra a `WidgetItem` y a `SameItems` (clave del `LayoutCache`) o el strip no repinta.

**Tech Stack:** C++17/Win32, PDH (`\Thermal Zone Information(*)\Temperature`), D3DKMT vía `gdi32` GetProcAddress, IOCTL storage, MinGW-w64 (`$env:MSYSTEM='MINGW64'` antes de `bash -lc`).

**Spec:** `docs/monitor-spec.md` — implement su **Etapa 2**. Decisiones citadas como (D#).

## Deltas desde etapas anteriores (aplicados a este plan)

- **CRÍTICO (de E1.5)**: todo estado visual nuevo debe entrar a la clave del `LayoutCache`. La severidad va como campo de `WidgetItem` y se agrega a `SameItems` (`widget.cpp:654-667`, hoy 13 campos).
- **Defaults vs config guardada**: cambiar `kDefaultConfig` NO reescribe la config ya persistida en `MetricsWhere` (REG_BINARY). En la máquina del usuario la config E1 guardada manda; activar las temps es por menú. No borrar la config guardada.
- `make && make test` tarda ~8 min; `build.bat` solo la app es rápido. Cerrar toda instancia viva (`Monitor.exe --quit`) antes de linkear.
- Reviewer fresco por tarea (en E1/E1.5 encontró bugs reales). Fallback si `agy` no está disponible: presets `cavecrew` (investigator/builder/reviewer).

## Global Constraints

- **Sin admin, sin PawnIO, sin nvapi.** NVML (`nvml.dll`, NVIDIA) es solo el fallback del KMT; el 7800 XT usa KMT.
- Umbrales (D11): CPU/GPU warn 80 / crit 90 °C; SSD warn 65 / crit 75 °C.
- **Guardas anti-valores-bogus, porteadas verbatim**: Thermal Zone solo si `mx > 200.0` (Kelvin, `traysense.c:268`); KMT falla si `Temperature == 0` (`:314`); SSD solo si `0 < t < 150` (`:575`). Sin sensor ⇒ `ok=false` ⇒ `--`, nunca un número inventado.
- Iconos (D10): temps `E957` (Sensor), SSD `EDA2` (HardDrive). CPU temp es **temperatura de zona/placa**, no de die — el tooltip lo aclara.
- Ids del enum ya reservados: prohibido renumerar. `kE1Metrics` se renombra a `kMetrics` y pasa de 4 a 7 entradas.
- i18n: claves nuevas en inglés + `lang/es.txt` (D13). Código/comentarios en inglés.
- Internos heredados intactos; los settings nuevos van bajo `Software\WirelessBattery` (ya existentes: `MetricsWhere`, `MetricIntervalMs`).

## Review Focus

1. **Cache-key regression**: severidad comparada en `SameItems`; probar que un cambio de severidad con valores congelados repinta el strip. Pin: Task 3 + reviewer.
2. **Valores bogus**: las tres guardas (Kelvin >200, KMT zero, rango SSD) presentes y en el camino del dato. Pin: Task 2 verificación + reviewer.
3. **Ports fieles**: KMT/NVML vía `GetProcAddress` (cero dependencias nuevas de linkeo); SSD IOCTL con access mask 0. Pin: reviewer contra `vendor/traysense/traysense.c:303-366,540-576`.
4. **Defaults no pisan la config guardada**: `LoadMetricsWhere` sigue ganando a `kDefaultConfig`. Pin: Task 1 test + reviewer.
5. **i18n**: cada clave nueva existe verbatim en `_template.txt` y `es.txt`. Pin: reviewer, listado con líneas.

---

### Task 1: Núcleo puro — FormatTemp, SeverityFor, defaults (TDD)

**Files:**
- Modify: `src/metrics/metrics.h` + `src/metrics/metrics_format.cpp`
- Modify: `tests/metrics_test.cpp`

**Interfaces (Task 3-4 dependen de esto):**
```cpp
std::wstring FormatTemp(double c, bool ok);   // ok=false => L"--"; entero redondeado + L"\u00B0" (grado)
int SeverityFor(int metric, double c);        // 0 normal | 1 warn | 2 crit; no-temp => 0
// kDefaultConfig pasa a: CpuUsage Bar, CpuTemp Bar, RamUsage Bar, GpuUsage Bar, GpuTemp Bar, Vram Bar, SsdTemp Hidden (D14: todo menos SSD)
```

- [ ] **Step 1: Test rojo** — metrics_test.cpp: FormatTemp normal/`--`/redondeo; SeverityFor CPU 79.9→0, 80→1, 90→2; GPU igual; SSD 64.9→0, 65→1, 75→2; métricas no-temp→0; kDefaultConfig nuevo valor exacto.
- [ ] **Step 2: `make test-metrics` → falla.** Implementar; **Step 3: suite completa → verde.**
- [ ] **Step 4: Commit** — `feat: temperature formatting, severity thresholds and new metric defaults (pure, tested)`

### Task 2: Fuentes de temperatura (port verbatim)

**Files:**
- Modify: `src/metrics/metrics_sampler.cpp` (+ declaraciones en `metrics.h` si hacen falta)

**Interfaces:** `TakeSnapshot` llena `val/ok` de `Id::CpuTemp` (1), `Id::GpuTemp` (4), `Id::SsdTemp` (6). `--metrics-dump` imprime `cpu_temp=/gpu_temp=/ssd_temp=` solo los ok.

**Ports (líneas de `vendor/traysense/traysense.c`):**
- CPU: PDH `\Thermal Zone Information(*)\Temperature` agregado al query existente; max de zonas; `mx - 273.15` solo si `mx > 200.0` (260-269, 584-585).
- GPU: KMT `D3DKMTOpenAdapterFromLuid` + `D3DKMTQueryAdapterInfo` tipo 62 con `KMT_PERF.Temperature / 10.0`, fail si `Temperature == 0` (303-345, 194-195); fallback NVML: `nvml.dll` (system32 + NVSMI), `nvmlInit_v2`/`nvmlDeviceGetHandleByIndex_v2`/`nvmlDeviceGetTemperature` (348-366). Todo vía `GetProcAddress`; cero libs nuevas.
- SSD: extents del disco del sistema (`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS`) → `\\.\PhysicalDriveN` con access mask 0 → `IOCTL_STORAGE_QUERY_PROPERTY` con `StorageDeviceTemperatureProperty`, max de infos solo si `0 < t < 150` (540-576).

- [ ] **Step 1: Implementar los tres ports + wiring + dump.**
- [ ] **Step 2: Build + suite completa.**
- [ ] **Step 3: Verificación runtime** — `Monitor.exe --metrics-dump` dos veces espaciadas 6 s (sin admin, sin instancia viva): cpu_temp y gpu_temp con valores plausibles (30-60 °C en idle); ssd_temp valor o `--`; cerrar todo después.
- [ ] **Step 4: Commit** — `feat: cpu gpu and ssd temperature sources without admin`

### Task 3: Ítems de temperatura con severidad en el strip

**Files:**
- Modify: `src/widget.h` — `WidgetItem` gana `int severity = 0;`
- Modify: `src/widget.cpp` — `SameItems` compara `severity` (cache-key); color del texto por severidad: warn naranja / crit rojo (RGBs porteados de `heat()` en `traysense.c:702-716`, con variantes light/dark como hace `Layout` con las baterías)
- Modify: `src/main.cpp` — `kE1Metrics` → `kMetrics` (7 entradas: + `{CpuTemp, 0xE957, L"CPU temperature"}, {GpuTemp, 0xE957, L"GPU temperature"}, {SsdTemp, 0xEDA2, L"SSD temperature"}`); `UpdateWidget` emite los items temp Bar con `FormatTemp`, `severity = SeverityFor(id, val)`; tooltip con nombre+valor y la aclaración de zona para CPU

**Interfaces:** el strip muestra temps entre usages y baterías según `where`; severidad congelada con valores cambiando debe repintar (gate del Review Focus 1).

- [ ] **Step 1: Implementar campo+cache+items+colores.** Cambiar la config guardada NO; el usuario activa temps desde el menú.
- [ ] **Step 2: Build + suite completa + smoke scriptable** (lanzar, vivo 10 s, `--quit`).
- [ ] **Step 3: Commit** — `feat: temperature items with severity colors in the widget strip`

### Task 4: Menú completo + i18n/es

**Files:**
- Modify: `src/main.cpp` — el submenú "System metrics" itera los 7 de `kMetrics` (radios Bar/Tray/Hidden); tooltips
- Modify: `lang/_template.txt` + `lang/es.txt` — claves: `CPU temperature`, `GPU temperature`, `SSD temperature`, y `Thermal zone reading, not a CPU die sensor` (tooltip CPU)

- [ ] **Step 1: Implementar menú + strings.** **Step 2: Build + suite completa + smoke con cambio de ubicación persistente tras reinicio.**
- [ ] **Step 3: Commit** — `feat: temperature metrics in the system menu with es translations`

### Task 5: Cierre

- [ ] **Step 1: Clean rebuild + suite completa.**
- [ ] **Step 2: Push `origin main` sin tags; el orquestador verifica CI.**
- [ ] **Step 3: Chequeos humanos** — (a) comparar cpu_temp/gpu_temp/ssd_temp del `--metrics-dump` contra HWiNFO (sensores nombrados; tolerancia amplia por Tctl/offset en AMD); (b) activar las temps en el menú y ver el strip: glifo Sensor + `54°`, tooltip con aclaración; (c) severidad visual solo alcanzable calentando — confiar en los tests puros del Task 1; (d) bandeja por temp si se manda a Notification area.

## Notas de ejecución

- Orquestación idéntica a E1: builder medium por tarea, reviewer high fresco por tarea, reportes vía `hc send t38`. Si `agy` no está disponible, usar los presets `cavecrew` (investigator/builder/reviewer) como reemplazo.
- Fuera de alcance: alertas de temperatura (E4), migración de `Mask2` de TraySense (E3), historial de métricas (E6), PawnIO/nvapi.
