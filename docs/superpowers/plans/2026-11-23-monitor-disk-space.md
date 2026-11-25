# Monitor — Widget de espacio libre en disco Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mostrar el espacio libre del disco del sistema como una métrica más del strip y de la bandeja, con tres modos de texto (libre en GB, libre en %, usado en %), reusando el sampler, el overflow y la configuración existentes.

**Architecture:** Se agrega `Id::DiskSpace` al final del enum para no mover IDs existentes. El sampler lee `GetDiskFreeSpaceExW` cada 60 s y guarda porcentaje y GB en el `Snapshot`. Formato y severidad son funciones puras en `metrics_format.cpp`; el widget, la bandeja y el menú consumen el mismo `WidgetItem`/`MetricSpec` de siempre. Sin hilo, dependencia, servicio ni permiso nuevo.

**Tech Stack:** C++17/Win32, MinGW-w64 (`$env:MSYSTEM='MINGW64'` antes de `bash -lc`).

**Spec:** `docs/monitor-spec.md` (modelo de ítems, D7/D8/D10/D11) más las decisiones de este plan.

## Deltas de esta feature

- **Una sola unidad, la del sistema:** se usa la letra de `GetWindowsDirectoryW()`; no hay selector de unidad. Si el usuario quiere `D:`, es backlog.
- **Default visible en la barra** (`Where::Bar`) porque el objetivo es que un usuario casual lo vea sin configurar. Esto aplica también a configs viejas: el byte nuevo se rellena con el default, no con `Hidden`. `MigrateTraySense` (`src/main.cpp:447-461`) se acota a los primeros 7 ids y rellena `7..kCount-1` con `kDefaultConfig`, para que los migrados de TraySense también lo vean.
- **Bandeja siempre en % usado:** barra llena = disco lleno, rojo consistente con CPU/RAM/GPU (no `% libre`, que invertiría la barra). El strip sigue el modo elegido y `iconSig` incluye el modo y los GB redondeados para que el icono se redibuje al cambiarlos.
- **Glifo propio, distinto de SSD:** `0xEDA2` ya lo usa `SsdTemp` (`src/main.cpp:123`) y en `S2_Compact` no se dibuja la caption; usar un glifo de almacenamiento no usado (candidato `0xE770` Storage — verificar el renderizado antes del commit).
- **`SameValues` compara décimas de GB** (`llround(gb * 10)`), no el double crudo: sin repintados cada 60 s por cualquier byte cambiado.
- **`--metrics-dump` agrega `disk=<% libre>` y `disk_gb=<GB libres>`** y es la verificación automática del Task 3.
- **Umbrales de disco:** naranja con libre < 20 %, rojo con libre < 10 %. No toca D11 (esos son temperaturas).
- **Texto por modo:** `FreeGb` default (ej. `62.4 GB`), `FreePercent` (ej. `24%`), `UsedPercent` (ej. `76%`). El tooltip siempre muestra GB libres, GB totales y % libre.
- **Sin historial ni alertas de disco.** No se pidieron.

## Global Constraints

- Sin admin, sin servicios, sin drivers, sin dependencias nuevas, sin frameworks.
- Solo Windows: no tocar `src/linux/` ni `build-linux.sh`.
- `Id` existentes conservan su valor; el nuevo se agrega al final. `kTrayUidBase + id` sigue siendo único.
- `MetricsWhere` guardado por versiones previas (7 bytes) se migra sin resetear la configuración del usuario.
- Defaults en `metrics::kDefaultConfig` alineados con el enum; el nuevo índice queda `Where::Bar`.
- Inglés + español completos; el resto cae a inglés (`lang/_template.txt`, `lang/es.txt`).
- Reusar la raíz de registro `HKCU\Software\WirelessBattery` y el patrón `Load*/Save*` existente.
- `make test` verde; `build.bat` y `make` compilan sin warnings nuevos.

## Review Focus

1. **Disco ausente o ilegible:** `GetDiskFreeSpaceExW` falla o total = 0 → `--`, sin división por cero ni crash. Pin: Task 1 (`FormatGb(..., false)`, total 0 en Task 2).
2. **Config vieja:** un `MetricsWhere` de 7 bytes conserva exactamente las 7 posiciones y solo agrega el default del disco. Pin: Task 1 (test legacy).
3. **Redondeo:** 0 bytes, 0.04 GB, 99.95 GB, 1024 GB y > 2 TB no muestran negativos ni `-0.0 GB`. Pin: Task 1.
4. **Severidad:** 20.0 % libre = normal, 19.9 % = naranja, 10.0 % = naranja, 9.9 % = rojo. Pin: Task 1.
5. **UI:** el modo elegido no cambia el tamaño del strip de forma impredecible; el tooltip conserva los tres datos. Pin: Task 3 reviewer.
6. **Bandeja y dump:** el icono muestra siempre el % usado y se redibuja al cambiar de modo; `--metrics-dump` imprime `disk=` y `disk_gb=`. Pin: Task 3.

---

### Task 1: Modelo puro de disco (TDD)

**Files:**
- Modify: `src/metrics/metrics.h:9-33`
- Modify: `src/metrics/metrics_format.cpp:9-67`
- Test: `tests/metrics_test.cpp`

**Interfaces:**
- Produces: `metrics::Id::DiskSpace` (valor 7), `metrics::kCount = 8`, `metrics::DiskDisplay`, `metrics::Snapshot::diskFreeGb`, `metrics::Snapshot::diskTotalGb`, `metrics::FormatGb(double, bool)`, `metrics::DiskSeverity(double)`, `metrics::ParseConfig` aceptando 7..8 bytes.
- Consumes: nada nuevo.

- [x] **Step 1: Escribir tests rojos**

Agregar en `tests/metrics_test.cpp`, antes del `puts` final:

```cpp
assert(FormatGb(62.44, true) == L"62.4 GB");
assert(FormatGb(0.04, true) == L"0.0 GB");
assert(FormatGb(99.96, true) == L"100.0 GB");
assert(FormatGb(128.0, true) == L"128 GB");
assert(FormatGb(1024.0, true) == L"1024 GB");
assert(FormatGb(-3.0, true) == L"0.0 GB");
assert(FormatGb(1.0, false) == L"--");

assert(DiskSeverity(20.0) == 0);
assert(DiskSeverity(19.9) == 1);
assert(DiskSeverity(10.0) == 1);
assert(DiskSeverity(9.9) == 2);
assert(DiskSeverity(0.0) == 2);

assert(kDefaultConfig.where[static_cast<int>(Id::DiskSpace)] == Where::Bar);

Config legacy{};
const uint8_t old7[7] = { 1, 1, 1, 1, 1, 1, 2 };
assert(ParseConfig(old7, sizeof(old7), legacy));
assert(legacy.where[0] == Where::Tray);
assert(legacy.where[6] == Where::Hidden);
assert(legacy.where[static_cast<int>(Id::DiskSpace)] == Where::Bar);

Snapshot d1{}, d2{};
d1.ok[static_cast<int>(Id::DiskSpace)] = true;
d1.val[static_cast<int>(Id::DiskSpace)] = 24.0;
d1.diskFreeGb = 62.4;
d1.diskTotalGb = 256.0;
d2 = d1;
assert(SameValues(d1, d2));
d2.diskFreeGb = 62.3;
assert(!SameValues(d1, d2));
Snapshot d3 = d1;
d3.diskFreeGb = d1.diskFreeGb + 0.02;  // menos de una décima visible: mismo snapshot, sin repintado
assert(SameValues(d1, d3));

for (int i = 0; i < kCount; i++) {
    uint32_t uid = kTrayUidBase + static_cast<uint32_t>(i);
    assert(uid != 1 && uid != 2 && uid >= 0x2000);
    static_assert((kTrayUidBase & 0x100) == 0, "kTrayUidBase..+kCount must keep bit 8 clear");
}
```

- [x] **Step 2: Correr y ver fallar**

Run: `make test-metrics`
Expected: errores de compilación por `FormatGb`, `DiskSeverity` y `Id::DiskSpace` inexistentes.

- [x] **Step 3: Implementar el modelo**

En `metrics.h`:

```cpp
enum class Id : int { CpuUsage = 0, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp, DiskSpace };
inline constexpr int kCount = 8;

enum class DiskDisplay : uint8_t { FreeGb = 0, FreePercent = 1, UsedPercent = 2 };

struct Snapshot {
    double val[kCount]{};
    bool ok[kCount]{};
    double diskFreeGb{};
    double diskTotalGb{};
};
```

`kDefaultConfig` agrega `Where::Bar` al final. Declarar:

```cpp
std::wstring FormatGb(double gb, bool ok);
int DiskSeverity(double freePercent);
```

En `metrics_format.cpp`:
- `FormatGb`: `!ok` → `"--"`; calcular `long t = std::llround(std::max(0.0, gb) * 10.0)` una sola vez y formatear desde ese entero: `t >= 1000` → `std::to_wstring(t / 10) + " GB"`; si no, `std::to_wstring(t / 10) + "." + std::to_wstring(t % 10) + " GB"`. Evita `-0.0` y el doble redondeo (`99.96` → `100.0 GB`, `128.0` → `128 GB`).
- `DiskSeverity`: `free >= 20` → 0; `free >= 10` → 1; si no → 2.
- `ParseConfig`: aceptar `bytes >= 7`; parsear `min(bytes, kCount)` posiciones; rellenar las faltantes con `kDefaultConfig`; validar solo las presentes. Menos de 7 bytes o byte inválido → defaults y `false`.
- `SameValues`: cuando `ok[DiskSpace]`, comparar `llround(diskFreeGb * 10)` y `llround(diskTotalGb * 10)` (décimas visibles, no el double crudo); el resto como hoy.

- [x] **Step 4: Correr tests**

Run: `make test-metrics`
Expected: `metrics_test: all passed`.

- [x] **Step 5: Commit**

```bash
git add src/metrics/metrics.h src/metrics/metrics_format.cpp tests/metrics_test.cpp
git commit -m "feat: disk space metric model (pure, tested)"
```

---

### Task 2: Leer el disco en el sampler

**Files:**
- Modify: `src/metrics/metrics_sampler.cpp:336-411`

**Interfaces:**
- Consumes: `Id::DiskSpace`, `Snapshot::diskFreeGb`, `Snapshot::diskTotalGb` (Task 1).
- Produces: `TakeSnapshot` llena `val[DiskSpace]` (porcentaje libre), `diskFreeGb`, `diskTotalGb`, `ok[DiskSpace]`; sin API pública nueva.

- [x] **Step 1: Implementar `ReadDiskSpace`**

Agregar junto a `ReadDisk` (que ya existe para la temperatura SSD; no renombrar esa):

```cpp
void ReadDiskSpace(Snapshot& out) {
    static ULONGLONG next = 0;
    static bool cachedOk = false;
    static double freeGb = 0.0, totalGb = 0.0, freePct = 0.0;

    const ULONGLONG now = GetTickCount64();
    if (now >= next) {
        next = now + 60 * 1000;
        wchar_t win[MAX_PATH]{};
        wchar_t root[4] = { L'C', L':', L'\\', 0 };
        if (GetWindowsDirectoryW(win, MAX_PATH) >= 2) root[0] = win[0];

        ULARGE_INTEGER avail{}, total{}, totalFree{};
        cachedOk = GetDiskFreeSpaceExW(root, &avail, &total, &totalFree) != 0 && total.QuadPart > 0;
        if (cachedOk) {
            freeGb = static_cast<double>(avail.QuadPart) / (1024.0 * 1024.0 * 1024.0);
            totalGb = static_cast<double>(total.QuadPart) / (1024.0 * 1024.0 * 1024.0);
            freePct = 100.0 * static_cast<double>(avail.QuadPart) / static_cast<double>(total.QuadPart);
        }
    }
    if (!cachedOk) return;
    out.diskFreeGb = freeGb;
    out.diskTotalGb = totalGb;
    out.val[static_cast<int>(Id::DiskSpace)] = freePct;
    out.ok[static_cast<int>(Id::DiskSpace)] = true;
}
```

Llamarla en `TakeSnapshot` después de `ReadDisk(out)`.

- [x] **Step 2: Compilar y verificar a mano**

Run: `build.bat` (o `make`)
Expected: `Built Monitor.exe`.

Abrir el Administrador de archivos en la unidad del sistema y comparar “espacio disponible” con el tooltip del widget: diferencia ≤ 0.1 GB. Con el widget oculto, el valor no debe romper nada.

- [x] **Step 3: Commit**

```bash
git add src/metrics/metrics_sampler.cpp
git commit -m "feat: sample free space of the system drive"
```

---

### Task 3: Menú, strip, bandeja y tooltip

**Files:**
- Modify: `src/main.cpp:54-72` (IDs), `:109-124` (`kMetrics`), `:382-423` (load/save), `:447-461` (migración TraySense), `:871-916` (bandeja), `:956-1028` (widget), `:1958-1986` (`--metrics-dump`), `:1487-1531` (menú), `:1633-1698` (comandos), `:2055` (arranque)
- Test: manual + `make test`

**Interfaces:**
- Consumes: `DiskDisplay`, `FormatGb`, `DiskSeverity`, `Snapshot::diskFreeGb/diskTotalGb/val[DiskSpace]` (Tasks 1-2).
- Produces: `g_diskDisplay`, `LoadDiskDisplay()`, `SaveDiskDisplay()`, `DiskText(const metrics::Snapshot&, metrics::DiskDisplay, bool)`, `DiskTip(const metrics::Snapshot&)`.

- [x] **Step 1: Agregar config persistida**

Global: `metrics::DiskDisplay g_diskDisplay = metrics::DiskDisplay::FreeGb;`

Agregar junto a `LoadMetricLabelStyle`/`SaveMetricLabelStyle`:

```cpp
metrics::DiskDisplay LoadDiskDisplay() {
    DWORD v = 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"DiskDisplay", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v <= 2 ? static_cast<metrics::DiskDisplay>(v) : metrics::DiskDisplay::FreeGb;
}
void SaveDiskDisplay(metrics::DiskDisplay mode) {
    DWORD v = static_cast<DWORD>(mode);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"DiskDisplay", REG_DWORD, &v, sizeof(v));
}
```

En el arranque, después de `g_metricIntervalMs = LoadMetricIntervalMs();`:

```cpp
g_diskDisplay = LoadDiskDisplay();
```

- [x] **Step 2: Agregar el ítem y su texto**

En `kMetrics`, al final:

```cpp
{ metrics::Id::DiskSpace, 0xE770, L"Disk space", L"D", L"DISK" },  // Storage; verificar que no colisione y se lea a 16 px (0xEDA2 ya lo usa SSD)
```

Agregar un helper en `main.cpp`:

```cpp
std::wstring DiskText(const metrics::Snapshot& s, metrics::DiskDisplay mode, bool ok) {
    if (!ok) return L"--";
    switch (mode) {
        case metrics::DiskDisplay::FreePercent:
            return metrics::FormatPercent(s.val[static_cast<int>(metrics::Id::DiskSpace)], true);
        case metrics::DiskDisplay::UsedPercent:
            return metrics::FormatPercent(100.0 - s.val[static_cast<int>(metrics::Id::DiskSpace)], true);
        default:
            return metrics::FormatGb(s.diskFreeGb, true);
    }
}
std::wstring DiskTip(const metrics::Snapshot& s) {
    return TF(L"{name}: {free} free of {total} ({percent})",
              { { L"name", T(L"Disk space") },
                { L"free", metrics::FormatGb(s.diskFreeGb, s.ok[static_cast<int>(metrics::Id::DiskSpace)]) },
                { L"total", metrics::FormatGb(s.diskTotalGb, s.ok[static_cast<int>(metrics::Id::DiskSpace)]) },
                { L"percent", metrics::FormatPercent(s.val[static_cast<int>(metrics::Id::DiskSpace)],
                                                     s.ok[static_cast<int>(metrics::Id::DiskSpace)]) } });
}
```

En `UpdateWidget`, antes del bloque de temperaturas:

```cpp
bool isDisk = m.id == metrics::Id::DiskSpace;
std::wstring valText = isDisk ? DiskText(g_metricSnap, g_diskDisplay, ok)
                    : isTemp ? metrics::FormatTemp(g_metricSnap.val[idx], ok)
                             : metrics::FormatPercent(g_metricSnap.val[idx], ok);
int sev = !ok ? 0 : isDisk ? metrics::DiskSeverity(g_metricSnap.val[idx])
          : isTemp ? metrics::SeverityFor(idx, g_metricSnap.val[idx]) : 0;
```

El tooltip del widget usa `DiskTip(g_metricSnap)` cuando `isDisk`; no duplicar el texto genérico.

- [x] **Step 3: Bandeja**

En `ApplyMetricTray`, agregar `const bool isDisk = id == static_cast<int>(metrics::Id::DiskSpace);`.
- `sev`: `isDisk && ok ? metrics::DiskSeverity(g_metricSnap.val[id]) : ...` (dejar temps como están).
- `val`: para disco usar el porcentaje **usado** (`100 - libre`) redondeado, clampeado 0..100: barra llena = disco lleno, rojo consistente con el resto.
- `shortVal`: `isDisk ? std::to_wstring(val) : ...` (mismo patrón que hoy).
- `barPct`: `val`.
- `tip`: `isDisk ? DiskTip(g_metricSnap) : ...` y no agregar la nota de “thermal zone”.
- `iconSig`: para disco incluir `g_diskDisplay` y `llround(diskFreeGb * 10)` además de `val/sev`, para que el icono se redibuje al cambiar de modo aunque el porcentaje no cambie.

- [x] **Step 4: Menú**

Agregar IDs al enum:

```cpp
ID_DISK_MODE_FIRST = 163,  // 163..165: FreeGb / FreePercent / UsedPercent
```

Dentro del submenú `mm` de System metrics, después de “Refresh rate”:

```cpp
HMENU disk = CreatePopupMenu();
AppendMenuW(disk, MF_STRING, ID_DISK_MODE_FIRST + 0, T(L"Free space (GB)"));
AppendMenuW(disk, MF_STRING, ID_DISK_MODE_FIRST + 1, T(L"Free space (%)"));
AppendMenuW(disk, MF_STRING, ID_DISK_MODE_FIRST + 2, T(L"Used space (%)"));
CheckMenuRadioItem(disk, ID_DISK_MODE_FIRST, ID_DISK_MODE_FIRST + 2,
                   ID_DISK_MODE_FIRST + static_cast<UINT>(g_diskDisplay), MF_BYCOMMAND);
AppendMenuW(mm, MF_POPUP, (UINT_PTR)disk, T(L"Disk space shows"));
```

En el `switch`, caso `cmd` en `ID_DISK_MODE_FIRST..+2`:

```cpp
g_diskDisplay = static_cast<metrics::DiskDisplay>(cmd - ID_DISK_MODE_FIRST);
SaveDiskDisplay(g_diskDisplay);
UpdateWidget();
ApplyAllMetricTray();
```

El submenú de ubicación por métrica ya se genera recorriendo `kMetrics`, así que “Disk space” aparece sin código extra.

- [x] **Step 4b: Migración y `--metrics-dump`**

`MigrateTraySense` (`src/main.cpp:447-461`): recorrer sólo los primeros 7 ids contra `Mask2` y rellenar `7..kCount-1` con `kDefaultConfig.where[i]`, de modo que un migrado de TraySense también reciba el disco en `Where::Bar`.

En el bloque de `--metrics-dump` (`src/main.cpp:1958-1986`), agregar al final de la línea `disk=<% libre>` (mismo patrón `add(...)`) y `disk_gb=<GB libres>` con `snprintf("%.1f")` sobre `diskFreeGb` cuando `ok[DiskSpace]`.

- [x] **Step 5: Verificar**

Run: `make test`
Expected: suite completa verde.

Manual: alternar los tres modos y confirmar que el strip muestra el modo elegido, la bandeja siempre el % usado y el tooltip GB/%; cambiar ubicación a bandeja/oculta; reiniciar y confirmar persistencia. Automático: `Monitor.exe --metrics-dump` imprime `disk=` y `disk_gb=` coherentes con la unidad del sistema.

- [x] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: disk space widget, tray icon and display mode menu"
```

---

### Task 4: i18n, README y cierre

**Files:**
- Modify: `lang/_template.txt`
- Modify: `lang/es.txt`
- Modify: `README.md`
- Modify: `docs/monitor-spec.md` (lista de métricas y backlog de unidades múltiples)

**Interfaces:**
- Consumes: todas las frases nuevas de Task 3.
- Produces: traducciones y documentación.

- [ ] **Step 1: Agregar frases**

En `_template.txt` (inglés, dejando el valor vacío como el resto):

```text
Disk space = 
Disk space shows = 
Free space (GB) = 
Free space (%) = 
Used space (%) = 
{name}: {free} free of {total} ({percent}) = 
```

En `es.txt`, traducir:

```text
Disk space = Espacio en disco
Disk space shows = Espacio en disco muestra
Free space (GB) = Espacio libre (GB)
Free space (%) = Espacio libre (%)
Used space (%) = Espacio usado (%)
{name}: {free} free of {total} ({percent}) = {name}: {free} libres de {total} ({percent})
```

- [ ] **Step 2: README y spec**

README: agregar “Disk space (free GB / free % / used %)” a la lista de métricas y una línea sobre que usa la unidad de Windows y se refresca cada 60 s. Spec: agregar `DiskSpace` a la lista de `MetricId` y anotar “selector de unidad” como backlog.

- [ ] **Step 3: Verificación final**

Run:

```powershell
$env:MSYSTEM='MINGW64'
bash -lc "make test"
```

Expected: todos los targets verdes. Después `build.bat` y una ejecución de 5 minutos con el widget visible: sin crash, sin warnings nuevos, valor coherente con Explorer.

- [ ] **Step 4: Commit**

```bash
git add lang/_template.txt lang/es.txt README.md docs/monitor-spec.md
git commit -m "docs: disk space widget copy and metric list"
```
