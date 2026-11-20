# Monitor — Etapa 1 (Métricas nativas sin admin) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CPU %, RAM %, GPU % y VRAM como ítems nativos del strip de Monitor (a la izquierda de las baterías, gap uniforme), con ubicación por métrica Barra/Bandeja/Oculta, menú, bandeja por métrica, compacto+`…` para overflow, sin hover highlight, todo sin admin.

**Architecture:** Se extiende `WidgetItem` y `UpdateWidget()` — cero renderers nuevos. Datos en `src/metrics/` (Windows-only): funciones puras testeables + sampler en hilo propio (5 s default) con pausa por pantalla apagada. Overflow via el hilo UIA **existente** (nunca un segundo escáner: `widget.cpp:52-61` documenta el deadlock). Sin temps (E2), sin PawnIO, sin admin.

**Tech Stack:** C++17/Win32, PDH (`PdhAddEnglishCounterW`), DXGI, MinGW-w64 vía MSYS2 (`$env:MSYSTEM='MINGW64'` antes de `bash -lc`), tests estilo `tests/*_test.cpp` compilados por `make test`.

**Spec:** `docs/monitor-spec.md` — implement su **Etapa 1**. Decisiones citadas como (D#).

## Deltas desde la Etapa 0 (aplicados a este plan)

- **Correr `make test` completo después de cada tarea** — el fixture del updater falló en CI por no re-correar tras un renombre.
- **El mutex heredado impide coexistir**: `Monitor.exe` corre ahora (PID 30316). Todo smoke local debe hacer `Monitor.exe --quit` antes de lanzar una instancia nueva.
- **Toolchain sano**: `C:\msys64\mingw64\bin` en PATH + MSYSTEM en el padre.
- **El viejo binario instalado no responde a `--quit`** — irrelevante aquí, no tocar el install viejo.
- Orquestación hc: briefs de una sola línea sin comillas dobles; cada reporte del hijo pide permiso (aprobar con `hc keys <t> enter`); CI ~10 min por push, la API anónima lee runs.

## Global Constraints

- Sin admin, sin PawnIO, sin temps: solo CPU %, RAM %, GPU %, VRAM. El enum reserva los 7 ids (temps entran en E2 sin renumerar).
- Internos heredados intactos (D3): clase `WirelessBatteryWnd`, mutex `Local\WirelessBatteryTray`, raíz `Software\WirelessBattery` (las config nuevas de métricas van bajo esa misma raíz), Run value, carpetas de datos.
- uID de bandeja por métrica: `0x2000 + id` (D5/`main.cpp:43-44`: 1 y 2 están reservados; baterías usan `StableId` ≥ 0x100).
- Orden del strip: métricas a la izquierda, `…` (si hay ocultos), baterías a la derecha; **un único gap inter-ítem `Px(14)`** (D7). Dentro del par glifo→texto se mantiene el `Px(5)` actual.
- Refresh métricas: default **5000 ms** (D8), menú 1/2/5 s.
- Sin hover highlight (D9) — se elimina también en los espejos.
- Iconos solo Segoe Fluent (D10): CPU `EEA1`, RAM `EEA0`, GPU `E950` (Component; fallback si confunde visualmente: `E7F4` TVMonitor), VRAM = RAM + caption `V`. Validación: humano al smoke final; si un glifo sale tofu, se cambia el code point.
- Idiomas: frases nuevas en inglés como claves + `lang/es.txt`; resto cae a inglés (D13).
- Los archivos de `src/metrics/` son Windows-only: **no** se agregan a `build-linux.sh`.
- Código y comentarios en inglés; texto de UI vía lang files.

## Review Focus

1. **Colisión de uID de bandeja** (una métrica con uID 1/2 borraría el placeholder o el globo de alerta). Test: Task 2 — `kTrayUidBase` y `base+id` nunca caen en 1..2 ni en el rango de `StableId`.
2. **Repintado por tick a 1 s** (churn de GDI/fonts en todo el strip). Test: Task 2 — `SameValues` (solo se repinta al cambiar un valor visible).
3. **Overflow: el strip tapa botones de apps** (regresión del motivo de este diseño). Test: Task 7 — `PickHidden` con anchos que no entran; nunca se oculta una batería en low; métricas primero.
4. **Config corrupta/versión futura del blob REG_BINARY** (7 bytes hoy, 10 mañana). Test: Task 2 — `ParseConfig` con buffer corto/garbage → defaults sin crash.
5. **Segundo consumidor de UIA** (deadlock Explorer). Constraint + revisión: Task 7 usa el hilo lookup existente; no se crea ningún otro hilo con UIA.

---

### Task 1: Eliminar el hover highlight

**Files:**
- Modify: `src/widget.cpp` — borrar `g_hover` (línea 14), el parámetro `hover` y el `roundRect` de `PaintStrip` (~521-535), `SetHover` + `Mirror::hover` + `WM_MOUSEMOVE`/`WM_MOUSELEAVE` de `WidgetProc` (~796-821), y los usos en `Paint`/`PaintMirror`/`Destroy` (~590, 597, 906). Quedan tooltip y clic intactos.

**Interfaces:** nada nuevo; produce el widget sin estado hover para todos los demás tasks.

- [ ] **Step 1: Aplicar las eliminaciones de arriba** (el input del mouse sigue llegando: el fondo casi transparente de `PaintStrip` no se toca).
- [ ] **Step 2: Build + tests**

```powershell
$env:Path = "C:\msys64\mingw64\bin;$env:Path"; .\build.bat
$env:MSYSTEM = "MINGW64"; C:\msys64\usr\bin\bash.exe -lc "cd '/c/Users/Ignacio Revainera/dev/battery-monitor' && make test"
```
Expected: `Built Monitor.exe`, todos los tests pasan.

- [ ] **Step 3: Commit** — `refactor: remove taskbar widget hover highlight (spec D9)`

### Task 2: Núcleo puro de métricas (TDD)

**Files:**
- Create: `src/metrics/metrics.h`
- Create: `src/metrics/metrics_format.cpp` (portable: `<string>`/`<cstdint>` solamente)
- Create: `tests/metrics_test.cpp`
- Modify: `Makefile` (wildcard `src/metrics/*.cpp` en SRC; target `test-metrics` + agregarlo a `test:`)
- Modify: `build.bat` (agregar `src\metrics\*.cpp` al loop de SOURCES)

**Interfaces (los tasks 3-8 dependen de esto exacto):**
```cpp
enum class Id : int { CpuUsage = 0, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp };  // Count = 7, orden de TraySense
enum class Where : uint8_t { Bar = 0, Tray = 1, Hidden = 2 };
struct Snapshot { double val[7]; bool ok[7]; };
struct Config { Where where[7]; };
constexpr Config kDefaultConfig = { Bar, Hidden, Bar, Bar, Hidden, Bar, Hidden };  // temps off hasta E2
std::wstring FormatPercent(double v, bool ok);        // ok=false => L"--"; clamp 0..100, entero redondeado + L"%"
bool ParseConfig(const uint8_t* data, size_t bytes, Config& out);  // buffer corto/garbage => kDefaultConfig y false
bool SameValues(const Snapshot& a, const Snapshot& b);            // compara solo pares (val, ok)
constexpr uint32_t kTrayUidBase = 0x2000;
```

- [ ] **Step 1: Escribir el test que falla** — `tests/metrics_test.cpp` con asserts: `FormatPercent(12.4,true)=="12%"`, `(99.6,true)=="100%"`, `(0,true)=="0%"`, `(_,false)=="--"`; `ParseConfig` con 7 bytes válidos → parsed, con buffer corto/garbage → `kDefaultConfig` + false; `SameValues` distingue ok y val; `kTrayUidBase > 0x1000` y `kTrayUidBase + 7` no pisa ningún rango conocido.
- [ ] **Step 2: Correr y verlo fallar** — `make test` → `metrics_test` no compila (headers inexistentes).
- [ ] **Step 3: Implementar** `metrics.h` + `metrics_format.cpp` según las firmas de arriba.
- [ ] **Step 4: Correr y verlo pasar** — `make test` verde completo (delta de la E0: siempre la suite entera).
- [ ] **Step 5: Commit** — `feat: metrics core - ids, config, formatting, tray uid base (pure, tested)`

### Task 3: Sampler — hilo, 4 fuentes, pausa, `--metrics-dump`

**Files:**
- Create: `src/metrics/metrics_sampler.cpp`
- Modify: `src/main.cpp` — `WM_METRICS = WM_APP + 4`, globals (`metrics::Config g_metricCfg`, snapshot último), thread start/stop junto al battery worker, case `WM_METRICS`, extensión del `WM_POWERBROADCAST` existente (1081-1083) con `RegisterPowerSettingNotification(g_hwnd, GUID_CONSOLE_DISPLAY_STATE)` + flag pausa
- Modify: `src/main.cpp` — modo CLI `--metrics-dump` (junto a los modos diagnósticos, **antes** del mutex: una toma, imprime `cpu= ram= gpu= vram=` solo los ok, sale 0)

**Interfaces:**
```cpp
// metrics_sampler.cpp
bool TakeSnapshot(metrics::Snapshot& out);   // directo: lo usa --metrics-dump y el hilo
DWORD WINAPI MetricsWorker(void* unused);     // loop: TakeSnapshot -> PostMessage(g_hwnd, WM_METRICS, 0, new Snapshot)
```
Fuentes (port verbatim de `vendor/traysense/traysense.c`): CPU % `GetSystemTimes` (206-217), RAM % `GlobalMemoryStatusEx` (219-227), GPU % PDH `\GPU Engine(*)\Utilization Percentage` máx por engine filtrado por LUID (271-287, 578-590), VRAM PDH `\GPU Adapter Memory(*)\Dedicated Usage` (289-301) + LUID/vramTotal de DXGI (init_gpu 321-367 **sin** nvapi/temps). Pausa: patrón `g_paused`+`g_wake` de `traysense.c:1468-1473` sobre el wait del hilo.

- [ ] **Step 1: Implementar TakeSnapshot + fuentes + hilo + pausa + `--metrics-dump`.**
- [ ] **Step 2: Build + `make test`.**
- [ ] **Step 3: Verificación runtime** (con Monitor corriendo → primero `Monitor.exe --quit`):

```powershell
.\Monitor.exe --metrics-dump; Start-Sleep 6; .\Monitor.exe --metrics-dump
```
Expected: dos corridas con valores razonables (cpu/gpu varían entre corridas; ram estable ±2). El dump **no** debe levantar widget/tray.

- [ ] **Step 4: Commit** — `feat: metrics sampler thread with display-off pause and --metrics-dump`

### Task 4: Ítems de métrica en el strip

**Files:**
- Modify: `src/widget.h` — `WidgetItem` gana `int metric = -1;` y `wchar_t glyphOverride = 0;` (`metric >= 0` = ítem de métrica)
- Modify: `src/widget.cpp` — `Glyph()` respeta `glyphOverride`; métricas emiten las mismas pieces que los dispositivos (glifo `Px(5)`→texto, `gapAfter Px(14)` entre ítems — gap uniforme D7)
- Modify: `src/main.cpp` — `UpdateWidget()`: al cambiar el snapshot (gate `SameValues` vs cache) re-ensambla ítems: métricas con `where == Bar` en orden fijo (Cpu, Ram, Gpu, Vram), luego dispositivos existentes; texto = `FormatPercent`; VRAM con `label = L"V"`; tooltip con nombre + valor

**Interfaces:** produce el strip final E1 (barra). El `…` y compacto llegan en Task 7.

- [ ] **Step 1: Implementar campos + assembly + glifos.**
- [ ] **Step 2: Build + `make test`.**
- [ ] **Step 3: Smoke scriptable** — `Monitor.exe --quit` de la instancia viva, lanzar la nueva, verificar proceso vivo 10 s, `--metrics-dump` sigue funcionando, `--quit` la cierra. (Visual: humano al final.)
- [ ] **Step 4: Commit** — `feat: show cpu/ram/gpu/vram as widget items left of devices`

### Task 5: Bandeja por métrica

**Files:**
- Modify: `src/main.cpp` — `ApplyMetricTray(id)`: uID `kTrayUidBase + id`, icono via el `RenderIcon` existente (glifo + valor corto sin `%` si no entra), tooltip nombre+valor, regeneración solo al cambiar (patrón iconSig de `ApplyEntry`), NIM_ADD/MODIFY/DELETE al cambiar `where`; solo para `where == Tray`; Placeholder y alertas (uID 1/2) intactos.

**Interfaces:** produce bandeja E1. `UpdateWidget` ya separa por `where`; `UpdatePlaceholder` no cambia.

- [ ] **Step 1: Implementar tray por métrica.**
- [ ] **Step 2: Build + `make test` + smoke scriptable** (mismos pasos que Task 4 Step 3; nota: en Win11 los iconos nuevos caen al overflow oculto — esperado, D5).
- [ ] **Step 3: Commit** — `feat: per-metric tray icons with explicit Tray location`

### Task 6: Menú Métricas + settings + i18n

**Files:**
- Modify: `src/main.cpp` — submenú "System metrics": por métrica un popup radio (Bar/Tray/Hidden), luego radio de frecuencia (1/2/5 s); en el menú principal: "Move left"/"Move right"/"Reset position" (escriben `TaskbarOffset` que `widget.cpp:984,1037` ya lee) y "Open Task Manager" (`taskmgr.exe` vía `ShellExecute`, junto al clic izquierdo actual D12)
- Modify: `src/main.cpp` — persistencia: `MetricsWhere` (REG_BINARY, 7 bytes) y `MetricIntervalMs` (REG_DWORD, default 5000) bajo `Software\WirelessBattery`; lectura con `ParseConfig` (Task 2)
- Modify: `lang/_template.txt`, `lang/es.txt` — claves nuevas: `System metrics`, `CPU usage`, `RAM usage`, `GPU usage`, `VRAM`, `Taskbar`, `Notification area`, `Hidden`, `Refresh rate`, `{seconds} s` (reusar patrón del menú de batería), `Move left`, `Move right`, `Reset position`, `Open Task Manager`

**Interfaces:** produce la superficie de control completa E1.

- [ ] **Step 1: Implementar menú + persistencia + strings (inglés + es).**
- [ ] **Step 2: Build + `make test` + smoke scriptable** + verificación de que un cambio de ubicación persiste (reiniciar la app y releer).
- [ ] **Step 3: Commit** — `feat: metrics menu with per-metric location, refresh rate, offset and task manager entry`

### Task 7: Overflow — appsR, compacto, `…`

**Files:**
- Create: `src/metrics/overflow.h` (puro) — `PickHidden(const std::vector<int>& widths, int available, const std::vector<uint8_t>& isMetric, int lowBatteryIdx)` → índices a ocultar; regla: primero métricas de último a primero (Vram→Cpu), baterías solo si no queda opción, **nunca** la batería en low
- Create: `tests/overflow_test.cpp` + registro en `Makefile`
- Modify: `src/widget.cpp` — el **hilo UIA existente** mide además el borde derecho de los botones de apps de la taskbar primaria (técnica de `traysense.c:1024-1068`: bounds del elemento de task list vía UIA); `Reposition` usa `minLeft = appsR + Px(4)`; `Layout` gana flag `compact` (gap inter-ítem `Px(6)`, sin captions); si compacto aún no entra → `PickHidden` + glifo `E712` ("More") al borde izquierdo con tooltip de nombres ocultos

**Interfaces:** produce el strip que nunca tapa botones.

- [ ] **Step 1: Test que falla** — `overflow_test.cpp`: entra todo → nada oculto; no entra → se ocultan métricas de última a primera; extremo: solo una batería en low → nunca oculta.
- [ ] **Step 2: Implementar `PickHidden` + appsR + compacto + `…`.**
- [ ] **Step 3: `make test` completo + smoke scriptable.**
- [ ] **Step 4: Commit** — `feat: overflow handling - appsR scan, compact layout, hidden-items indicator`

### Task 8: Cierre de etapa

- [ ] **Step 1: `make test` completo + build limpio desde cero** (`Remove-Item -Recurse build` + `.\build.bat`).
- [ ] **Step 2: Comparación contra TraySense en vivo** — lanzar `C:\Program Files\TraySense\TraySense.exe` (solo para comparar), `Monitor.exe --metrics-dump` tres veces espaciadas 6 s; CPU/RAM/GPU/VRAM dentro de ±2 del strip de TraySense (fases de muestreo distintas: tolerancia, no igualdad). Cerrar TraySense después.
- [ ] **Step 3: Smoke visual humano** — lista para el usuario: métricas a la izquierda de las baterías, mismo gap, hover sin highlight, menú Métricas completo, bandeja si se elige Tray, `…` si se ocupa el strip (ocultar métricas y achicar la taskbar para forzarlo), idioma español.
- [ ] **Step 4: Commit + push** — `git push origin main` (sin tags) y verificar CI verde (API anónima de runs).
- [ ] **Step 5: Reporte de etapa** con evidencias + deltas para el plan de la E2 (temperatura vía Thermal Zone se verá en `--metrics-dump` antes del trabajo de E2).

## Notas de ejecución

- Tareas 1-7 en un único hijo Antigravity (effort medium), steer por tarea desde orquestación; Task 8 comparación + smoke con el mismo hijo, visual humano al final.
- No tocar: drivers, installer, updater, `build-linux.sh`, `vendor/`, el install viejo de BatteryHub, `Software\TraySense`.
- Fuera de alcance E1 (E2+): temperaturas, severidad de color, PawnIO, migración de Mask2, alertas de temp, historial de métricas, opciones de orden/gap del menú.
