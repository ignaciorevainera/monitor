# Monitor — Widget de uso de OpenCode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mostrar en un solo widget el porcentaje gastado de los límites de OpenCode Go (5 horas / semanal / mensual) con el endpoint oficial, configurando una API key una sola vez.

**Architecture:** Modelo puro `AiUsage` (parseo del JSON oficial, formato `60/20/80`, severidad) + token cifrado con DPAPI en la raíz de registro existente + fetch WinHTTP cada 5 minutos dentro del sampler actual. El widget, la bandeja y el menú reusan `WidgetItem`/`MetricSpec`/i18n; no hay hilo, servicio ni dependencia nueva. Un diálogo modal pide la key.

**Tech Stack:** C++17/Win32, WinHTTP, DPAPI (`crypt32`), MinGW-w64 (`$env:MSYSTEM='MINGW64'` antes de `bash -lc`).

**Spec:** `docs/monitor-spec.md` (modelo de ítems, D10/D14) más las decisiones de este plan.

**Dependencia:** este plan se aplica **después** de `docs/superpowers/plans/2026-11-23-monitor-disk-space.md`. Asume `Id::DiskSpace` en 7 y `kCount = 8`; agrega `Id::OpenCodeUsage` en 8 y deja `kCount = 9`.

## Deltas de esta feature

- **Un solo widget para los tres límites**, texto `60/20/80` en orden fijo 5h/semanal/mensual. El tooltip detalla cada uno con `resetsAt`.
- **Endpoint oficial:** `GET https://opencode.ai/inference/go/v1/usage` con `Authorization: Bearer <key>`. Nada de scraping ni del endpoint legacy `/zen/...`.
- **Sin OAuth:** OpenCode Go se conecta con API key personal. El diálogo ofrece “Get an API key...” que abre `https://opencode.ai/console/api` en el navegador; el usuario pega la key.
- **Default oculto** (`Where::Hidden`): sin key el widget no debe ensuciar el strip de un usuario casual. Al guardar una key válida, si sigue oculto, se pasa a `Where::Bar` y se persiste.
- **Poll cada 5 minutos.** El sampler sigue a 1/2/5 s para el resto; el fetch de OpenCode usa cache propia.
- **Sin lectura de la credencial de OpenCode.** En V2 vive en SQLite y las docs piden no manipularla; la key se pide y se guarda aparte.
- **Hilo propio para el fetch** (patrón de `src/update_win.cpp:109-165`): cancelable al salir, caché protegida con SRWLOCK, `TakeSnapshot` sólo lee la caché y `--metrics-dump` no dispara fetch. Un fetch síncrono en el sampler congelaría CPU/RAM/GPU hasta ~30 s y excedería los 5 s de `WM_DESTROY` (`src/main.cpp:1844`).
- **Poll detenido tras 401/403** hasta que el usuario cambie la key (`RequestAiRefresh` lo reanuda); 429 se reporta como red ocupada.
- **Tip de bandeja compacto (≤ 120 chars):** `szTip` es `WCHAR[128]` (`shellapi.h`) y el detalle multilínea se truncaría; el detalle completo queda sólo en el tooltip del widget.
- **Transporte endurecido:** `WINHTTP_OPTION_REDIRECT_POLICY_NEVER` (un redirect jamás reenvía el Bearer), key validada sin caracteres < 0x20, UA `Monitor/<WB_VERSION_WSTRING>`, buffers de la key limpiados con `SecureZeroMemory`, fin-de-respuesta verificado (patrón `src/update_win.cpp:146-154`).
- **Sin historial, sin alertas de cuota, sin Gemini.** Backlog.

## Global Constraints

- Sin admin, sin servicios, sin drivers, sin dependencias nuevas, sin frameworks.
- Solo Windows: no tocar `src/linux/` ni `build-linux.sh`.
- `Id` existentes conservan su valor; `OpenCodeUsage` se agrega al final. `kTrayUidBase + id` sigue siendo único.
- `MetricsWhere` de 8 bytes (post-disco) se migra sin resetear la configuración del usuario.
- La API key nunca se escribe en claro: solo blob DPAPI (`REG_BINARY` `OpenCodeToken` en `HKCU\Software\WirelessBattery`). Nunca en logs, `--json`, reportes, tooltips ni títulos de ventana.
- Sin key, con 401/403 o sin red: `--` y tooltip con el motivo; nunca crash ni reintento en loop.
- El fetch corre en un hilo propio de baja prioridad, cancelable al salir (`WM_DESTROY` no debe esperar más de sus 5 s); `TakeSnapshot` sólo lee una caché protegida y `--metrics-dump` no dispara fetch.
- Inglés + español completos; el resto cae a inglés (`lang/_template.txt`, `lang/es.txt`).
- `make test` verde; `build.bat` y `make` compilan sin warnings nuevos.

## Review Focus

1. **Fuga de la API key:** solo existe en el blob DPAPI; “Clear API key” la borra; no aparece en logs, reportes ni `--json`. Pin: Task 2 (roundtrip + clear) y reviewer.
2. **Fallo de red/HTTP:** 401, 403, timeout y body basura dejan `--` con motivo, sin colgar el sampler ni la UI. Pin: Task 1 (parse inválido) + Task 3 (timeouts) + reviewer.
3. **Config vieja:** `MetricsWhere` de 8 bytes conserva todas las posiciones y agrega OpenCode oculto. Pin: Task 1 (test legacy).
4. **Formato:** orden fijo `5h/semanal/mensual`; `rate-limited` o 100 % → rojo; ≥ 80 % → naranja; sin datos → `--`. Pin: Task 1.
5. **Un solo icono de bandeja:** usa el peor porcentaje para barra/color y el tooltip lista los tres; no tres iconos. Pin: Task 4 reviewer.

---

### Task 1: Modelo puro de uso de IA (TDD)

**Files:**
- Create: `src/metrics/ai_usage.h`
- Create: `src/metrics/ai_usage.cpp`
- Modify: `src/metrics/metrics.h`
- Modify: `src/metrics/metrics_format.cpp`
- Test: `tests/ai_usage_test.cpp`
- Test: `tests/metrics_test.cpp`

**Interfaces:**
- Produces:
```cpp
namespace metrics {
enum class AiError : uint8_t { None = 0, NoToken, Unauthorized, Forbidden, Network, Parse };
struct AiMeter { int percent = -1; bool limited = false; std::string resetsAt; };
struct AiUsage { AiMeter meter[3]; bool ok = false; AiError error = AiError::NoToken; };

bool ParseAiUsage(const std::string& json, AiUsage& out);
std::wstring FormatAiUsage(const AiUsage& ai);          // "60/20/80" o "--"
std::wstring FormatAiReset(const std::string& iso);     // "2026-11-21 15:00 UTC"
int AiSeverity(const AiUsage& ai);                      // 0/1/2
}
```
- Consumes: nada nuevo.

- [ ] **Step 1: Escribir el test rojo**

Crear `tests/ai_usage_test.cpp`:

```cpp
#include "../src/metrics/ai_usage.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace metrics;

    const std::string ok =
        R"({"usage":{"rolling":{"status":"ok","percent":30,"resetsAt":"2026-11-21T15:00:00.000Z"},)"
        R"("weekly":{"status":"rate-limited","percent":100,"resetsAt":"2026-11-23T00:00:00.000Z"},)"
        R"("monthly":{"status":"ok","percent":50,"resetsAt":"2026-12-01T00:00:00.000Z"}}})";

    AiUsage a{};
    assert(ParseAiUsage(ok, a));
    assert(a.ok);
    assert(a.error == AiError::None);
    assert(a.meter[0].percent == 30 && !a.meter[0].limited);
    assert(a.meter[1].percent == 100 && a.meter[1].limited);
    assert(a.meter[2].percent == 50 && !a.meter[2].limited);
    assert(FormatAiUsage(a) == L"30/100/50");
    assert(AiSeverity(a) == 2);

    AiUsage warn = a;
    warn.meter[1] = { 80, false, "" };
    warn.meter[2] = { 20, false, "" };
    assert(AiSeverity(warn) == 1);

    AiUsage normal = a;
    normal.meter[0] = { 10, false, "" };
    normal.meter[1] = { 20, false, "" };
    normal.meter[2] = { 30, false, "" };
    assert(AiSeverity(normal) == 0);

    AiUsage bad{};
    assert(!ParseAiUsage("{}", bad));
    assert(bad.error == AiError::Parse);
    assert(!bad.ok);
    assert(FormatAiUsage(bad) == L"--");
    assert(AiSeverity(bad) == 0);

    assert(FormatAiReset("2026-11-21T15:00:00.000Z") == L"2026-11-21 15:00 UTC");
    assert(FormatAiReset("") == L"");
    assert(FormatAiReset("basura") == L"");

    std::puts("ai_usage_test: all passed");
    return 0;
}
```

En `tests/metrics_test.cpp`, agregar:

```cpp
assert(kDefaultConfig.where[static_cast<int>(Id::OpenCodeUsage)] == Where::Hidden);

Config old8{};
const uint8_t cfg8[8] = { 1, 1, 1, 1, 1, 1, 2, 0 };
assert(ParseConfig(cfg8, sizeof(cfg8), old8));
assert(old8.where[7] == Where::Bar);
assert(old8.where[static_cast<int>(Id::OpenCodeUsage)] == Where::Hidden);

Snapshot a1{}, b1{};
a1.ai.ok = true;
a1.ai.meter[0] = { 60, false, "2026-11-21T15:00:00.000Z" };
b1 = a1;
assert(SameValues(a1, b1));
b1.ai.meter[0].percent = 61;
assert(!SameValues(a1, b1));
```

- [ ] **Step 2: Correr y ver fallar**

Run: `make test-ai-usage` (después de agregar el target) o compilar a mano:

```bash
g++ -std=c++17 -O2 -Wall -Wextra tests/ai_usage_test.cpp src/metrics/ai_usage.cpp -o build/ai_usage_test.exe
```

Expected: falla por `ai_usage.h` inexistente.

- [ ] **Step 3: Implementar el modelo**

`ai_usage.h` exactamente con las firmas del bloque Interfaces, más `<cstdint>` y `<string>`.

`ai_usage.cpp`:
- `ExtractObject(json, "\"rolling\"", obj)` busca la clave, el `{` siguiente y el `}` de cierre con profundidad (ignorando strings y escapes). Error si falta.
- `ExtractInt(obj, "\"percent\"", int&)`: dígitos; rango 0..100; error si falta o no es número.
- `ExtractString(obj, "\"status\"", std::string&)` y `ExtractString(obj, "\"resetsAt\"", ...)`; `resetsAt` puede faltar (queda vacío).
- `ParseAiUsage`: exigir `rolling`, `weekly`, `monthly`, cada uno seguido de `:` + `{` (un valor `null` es error de parseo, no un salto al siguiente objeto); exigir `percent` y `status` en cada uno; `limited = status == "rate-limited"`; `ok = true`, `error = None`. Cualquier fallo: `out = AiUsage{}`, `out.error = AiError::Parse`, `false`.
- `FormatAiUsage`: `!ok` → `"--"`; si no, `percent[0]/percent[1]/percent[2]` con `std::to_wstring`.
- `FormatAiReset`: aceptar sólo si tiene ≥ 16 chars, el char 10 es `T` y termina en `Z`; si no, `L""` (nunca imprimir un `resets` vacío).
- `AiSeverity`: `!ok` → 0; `limited` o `percent >= 100` → 2; `percent >= 80` → 1; si no, 0.

En `metrics.h`:
```cpp
#include "ai_usage.h"
enum class Id : int { CpuUsage = 0, CpuTemp, RamUsage, GpuUsage, GpuTemp, Vram, SsdTemp, DiskSpace, OpenCodeUsage };
inline constexpr int kCount = 9;
struct Snapshot { /* ...campos de disco... */ AiUsage ai; };
```
`kDefaultConfig` agrega `Where::Hidden` al final. `SameValues` compara `ai.ok`, `ai.error`, cada `percent`, `limited` y `resetsAt`.

`ParseConfig` ya acepta un prefijo de 7..`kCount` bytes por el plan de disco; no cambiar esa lógica.

- [ ] **Step 4: Correr tests**

Agregar el target al `Makefile` (y a `test:` / `.PHONY`):

```make
test-ai-usage:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra tests/ai_usage_test.cpp src/metrics/ai_usage.cpp \
	    -o build/ai_usage_test.exe
	./build/ai_usage_test.exe
```

Run: `make test-ai-usage test-metrics`
Expected: `ai_usage_test: all passed` y `metrics_test: all passed`.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/ai_usage.h src/metrics/ai_usage.cpp src/metrics/metrics.h \
        src/metrics/metrics_format.cpp tests/ai_usage_test.cpp tests/metrics_test.cpp Makefile
git commit -m "feat: OpenCode usage model (pure, tested)"
```

---

### Task 2: Guardar la API key con DPAPI (TDD)

**Files:**
- Create: `src/metrics/opencode_token.h`
- Create: `src/metrics/opencode_token.cpp`
- Test: `tests/opencode_token_test.cpp`
- Modify: `Makefile:10` (`LIBS`), `Makefile:42` (`test:`), `Makefile` targets
- Modify: `build.bat:21` (link)

**Interfaces:**
- Produces:
```cpp
bool ProtectToken(const std::wstring& token, std::vector<BYTE>& out);
bool UnprotectToken(const std::vector<BYTE>& in, std::wstring& token);
bool SaveOpenCodeToken(const std::wstring& token);
bool LoadOpenCodeToken(std::wstring& token);
bool HasOpenCodeToken();
void ClearOpenCodeToken();
```
- Consumes: nada.

- [ ] **Step 1: Test rojo**

Crear `tests/opencode_token_test.cpp`:

```cpp
#include "../src/metrics/opencode_token.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    std::vector<BYTE> blob;
    std::wstring back;
    assert(ProtectToken(L"sk-test-123", blob));
    assert(!blob.empty());
    assert(UnprotectToken(blob, back));
    assert(back == L"sk-test-123");

    std::vector<BYTE> broken(blob.begin(), blob.end() - 1);
    assert(!UnprotectToken(broken, back));

    std::vector<BYTE> odd{ 0x01, 0x02, 0x03 };
    std::wstring wide;
    assert(!UnprotectToken(odd, wide));  // no múltiplo de sizeof(wchar_t)

    std::vector<BYTE> huge(70 * 1024, 0x41);
    assert(!UnprotectToken(huge, wide));  // por encima del límite de 64 KB

    std::puts("opencode_token_test: all passed");
    return 0;
}
```

- [ ] **Step 2: Correr y ver fallar**

Run:

```bash
g++ -std=c++17 -O2 -Wall -Wextra tests/opencode_token_test.cpp src/metrics/opencode_token.cpp -o build/opencode_token_test.exe -lcrypt32
```

Expected: falla por header/funciones inexistentes.

- [ ] **Step 3: Implementar**

`opencode_token.cpp`:
```cpp
constexpr wchar_t kTokenValue[] = L"OpenCodeToken";
constexpr wchar_t kDescription[] = L"Monitor OpenCode API key";

bool ProtectToken(const std::wstring& token, std::vector<BYTE>& out) {
    if (token.empty()) return false;
    DATA_BLOB in{ static_cast<DWORD>(token.size() * sizeof(wchar_t)),
                  reinterpret_cast<BYTE*>(const_cast<wchar_t*>(token.data())) };
    DATA_BLOB enc{};
    if (!CryptProtectData(&in, kDescription, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &enc)) return false;
    out.assign(enc.pbData, enc.pbData + enc.cbData);
    LocalFree(enc.pbData);
    return true;
}
```
`UnprotectToken` hace lo inverso con `CryptUnprotectData` y valida que el tamaño sea múltiplo de `sizeof(wchar_t)`. `SaveOpenCodeToken` protege y escribe `REG_BINARY` con `RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", kTokenValue, ...)`. `LoadOpenCodeToken` lee tipo/tamaño, protege contra > 64 KB y desprotege. `HasOpenCodeToken` consulta tamaño con `RegGetValueW(..., RRF_RT_REG_BINARY, ...)`. `ClearOpenCodeToken` usa `RegDeleteKeyValueW` (ignorar `ERROR_FILE_NOT_FOUND`).

Agregar `#include <dpapi.h>` y `<vector>`. En `Makefile`, `LIBS` suma `-lcrypt32`; en `build.bat`, la línea de link suma `-lcrypt32`. El test es sólo in-memory (no toca HKCU). Riesgo CI: `CryptProtectData` en windows-latest no está verificado; si falla en Actions, aislar `test-opencode-token` en un smoke local y sacarlo de `test:` con un comentario que explique por qué.

- [ ] **Step 4: Suite verde**

Agregar target:

```make
test-opencode-token:
	@mkdir -p build
	$(CXX) -std=c++17 -O2 -Wall -Wextra tests/opencode_token_test.cpp src/metrics/opencode_token.cpp \
	    -o build/opencode_token_test.exe -lcrypt32
	./build/opencode_token_test.exe
```

Registrarlo en `test:` y `.PHONY`. Run: `make test-opencode-token`
Expected: `opencode_token_test: all passed`.

- [ ] **Step 5: Commit**

```bash
git add src/metrics/opencode_token.h src/metrics/opencode_token.cpp tests/opencode_token_test.cpp \
        Makefile build.bat
git commit -m "feat: store the OpenCode API key with DPAPI"
```

---

### Task 3: Fetch en hilo propio y sampler

**Files:**
- Create: `src/metrics/opencode_usage.h`
- Create: `src/metrics/opencode_usage.cpp`
- Modify: `src/metrics/metrics.h`
- Modify: `src/metrics/metrics_sampler.cpp` (sólo `TakeSnapshot` + arranque/parada)

**Interfaces:**
- Consumes: `AiUsage`, `ParseAiUsage` (Task 1); `LoadOpenCodeToken` (Task 2).
- Produces:
```cpp
namespace metrics {
bool FetchOpenCodeUsage(const std::wstring& token, AiUsage& out);  // sólo red
bool AiSnapshot(AiUsage& out);        // copia la caché bajo SRWLOCK
void StartAiSampler();                // lanza el hilo (idempotente)
void StopAiSampler();                 // cancela y une (< 1 s tras el fetch en curso)
void RequestAiRefresh();              // menú: reanuda tras 401/403 y fuerza fetch
}
```

- [ ] **Step 1: Implementar el fetch**

`opencode_usage.h`:
```cpp
#pragma once
#include <string>
#include "ai_usage.h"
namespace metrics { bool FetchOpenCodeUsage(const std::wstring& token, AiUsage& out); }
```

`opencode_usage.cpp`:
- Convertir el token a UTF-8 con `WideCharToMultiByte(CP_UTF8, ...)` y `SecureZeroMemory` del buffer al terminar.
- Validar la key antes de usarla: rechazar cualquier carácter < 0x20 o espacio (defensa contra inyección de headers).
- `WinHttpOpen(L"Monitor/" WB_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, ...)`; timeouts `WinHttpSetTimeouts(s, 5000, 5000, 10000, 10000)`.
- `WinHttpConnect(s, L"opencode.ai", INTERNET_DEFAULT_HTTPS_PORT, 0)`; `WinHttpOpenRequest(c, L"GET", L"/inference/go/v1/usage", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)`; fijar `WINHTTP_OPTION_REDIRECT_POLICY_NEVER` en el request (un redirect jamás reenvía el Bearer).
- Header: sólo `Authorization: Bearer <utf8>\r\n` (el UA ya vive en `WinHttpOpen`).
- Status: 200 → leer hasta 16 KB verificando fin de respuesta (patrón de `src/update_win.cpp:146-154`) y `ParseAiUsage`; 401 → `Unauthorized`; 403 → `Forbidden`; 429 → `Network` con mensaje propio de "rate limited"; otro/0 → `Network`. En cualquier fallo `out.ok = false`.
- Cerrar todos los handles en el orden inverso, en todos los caminos.

- [ ] **Step 2: Hilo propio con caché**

En `opencode_usage.cpp`, además del fetch:
- Estado: `std::atomic<bool> g_aiStop`, `std::atomic<bool> g_aiHalted` (401/403), `SRWLOCK g_aiLock`, `AiUsage g_aiCache`, `HANDLE g_aiWake`, handle del hilo creado por `StartAiSampler` (idempotente).
- Worker (below normal): loop `WaitForSingleObject(g_aiWake, 5 * 60 * 1000)`; al despertar (y una vez al arrancar), si `!g_aiHalted` → `LoadOpenCodeToken` + `FetchOpenCodeUsage` → guardar bajo lock; si el resultado es `Unauthorized` o `Forbidden` → `g_aiHalted = true` (no más polls hasta `RequestAiRefresh` tras cambiar la key); `SecureZeroMemory` del token tras usarlo.
- Sin key: el worker escribe `AiUsage{}` (error `NoToken`) y sigue durmiendo; nunca fetch.
- `AiSnapshot(out)`: copia bajo SRWLOCK shared.
- `RequestAiRefresh()`: `g_aiHalted = false; SetEvent(g_aiWake);`.
- `StopAiSampler()`: `g_aiStop = true; SetEvent(g_aiWake);` join. El fetch en curso termina solo dentro de sus timeouts; `WM_DESTROY` nunca espera más de sus 5 s (`src/main.cpp:1844`).

- [ ] **Step 3: Integrar en `TakeSnapshot`**

En `metrics_sampler.cpp`, al final de `TakeSnapshot`:
```cpp
metrics::AiUsage ai{};
if (metrics::AiSnapshot(ai)) {
    out.ai = ai;
    out.ok[static_cast<int>(Id::OpenCodeUsage)] = ai.ok;
    out.val[static_cast<int>(Id::OpenCodeUsage)] = ai.ok
        ? std::max({ ai.meter[0].percent, ai.meter[1].percent, ai.meter[2].percent })
        : 0.0;
}
```
`StartAiSampler()` junto al arranque del worker de métricas y `StopAiSampler()` junto a su join. `--metrics-dump` no dispara fetch: sólo la caché (sin key no imprime nada nuevo).

- [ ] **Step 4: Verificar sin key y con red**

Run: `build.bat`
Expected: `Built Monitor.exe`.

Sin key: `--` en el widget (si está visible), tooltip “No API key set”, sin demora perceptible. Con una key inválida: tooltip “Invalid API key” y **no más polls hasta cambiar la key**. Con red desconectada: tooltip “Could not reach OpenCode” y CPU/RAM siguen actualizando en cada tick (el fetch nunca corre en el hilo del sampler). Salir de la app durante un fetch: el proceso termina en < 5 s.

- [ ] **Step 4: Commit**

```bash
git add src/metrics/opencode_usage.h src/metrics/opencode_usage.cpp \
        src/metrics/metrics.h src/metrics/metrics_sampler.cpp
git commit -m "feat: OpenCode Go usage fetch on its own cancellable thread"
```

---

### Task 4: Diálogo, menú, widget, bandeja e i18n

**Files:**
- Create: `src/opencode_dialog.h`
- Create: `src/opencode_dialog.cpp`
- Modify: `src/main.cpp:54-72` (IDs), `:109-124` (`kMetrics`), `:871-916` (bandeja), `:956-1028` (widget), `:1487-1531` (menú), `:1633-1698` (comandos)
- Modify: `lang/_template.txt`
- Modify: `lang/es.txt`

**Interfaces:**
- Consumes: `SaveOpenCodeToken`, `ClearOpenCodeToken`, `HasOpenCodeToken` (Task 2); `AiUsage`, `FormatAiUsage`, `AiSeverity`, `RequestAiRefresh` (Tasks 1/3).
- Produces: `bool PromptOpenCodeKey(HWND owner, std::wstring& key);`, `AiTip(const metrics::AiUsage&)` (widget, detalle completo), `AiTrayTip(const metrics::AiUsage&)` (bandeja, ≤ 120 chars), `AiTrayPercent(const metrics::AiUsage&)`.

- [ ] **Step 1: Diálogo**

`opencode_dialog.h`:
```cpp
#pragma once
#include <windows.h>
#include <string>
bool PromptOpenCodeKey(HWND owner, std::wstring& key);
```

`opencode_dialog.cpp`: copiar el patrón de `src/rename_dialog.cpp` (clase propia `WirelessBatteryOpenCodeDialog`, `WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW`, `IsDialogMessageW`). Título de la ventana y del `MessageBoxW`: `T(L"OpenCode")` (clave ya agregada al i18n). Controles:
- Label `OpenCode API key:`.
- `EDIT` con `ES_PASSWORD | ES_AUTOHSCROLL`, límite 512.
- Botón `Get an API key...` → `ShellExecuteW(nullptr, L"open", L"https://opencode.ai/console/api", nullptr, nullptr, SW_SHOWNORMAL)`.
- `Save` (IDOK) valida no vacío después de recortar espacios; si está vacío, `MessageBoxW` con `Enter the API key.` y foco al edit.
- `Cancel` (IDCANCEL).
- `WM_CLOSE` destruye sin aceptar.

- [ ] **Step 2: Métricas y tooltip**

En `kMetrics`, después de disco:
```cpp
{ metrics::Id::OpenCodeUsage, 0xE943, L"OpenCode usage", L"OC", L"AI" },
```
`0xE943` es el glifo “Code” de Segoe Fluent; la caption `OC` lo distingue. Si más adelante se agrega el logo PNG, `glyphOverride`/`label` ya están aislados en `MetricSpec`.

Helpers en `main.cpp`:
```cpp
int AiTrayPercent(const metrics::AiUsage& ai) {
    return std::max({ ai.meter[0].percent, ai.meter[1].percent, ai.meter[2].percent });
}
std::wstring AiTip(const metrics::AiUsage& ai) {
    if (!ai.ok) {
        const wchar_t* reason = ai.error == metrics::AiError::NoToken     ? L"No API key set"
                              : ai.error == metrics::AiError::Unauthorized ? L"Invalid API key"
                              : ai.error == metrics::AiError::Forbidden    ? L"This key has no OpenCode Go subscription"
                              : ai.error == metrics::AiError::Parse        ? L"Could not read OpenCode usage"
                                                                           : L"Could not reach OpenCode";
        return std::wstring(T(L"OpenCode usage")) + L": " + T(reason);
    }
    const wchar_t* names[3] = { L"5-hour", L"Weekly", L"Monthly" };
    std::wstring tip = T(L"OpenCode usage");
    for (int i = 0; i < 3; i++) {
        tip += L"\n" + std::wstring(T(names[i])) + L": " +
               metrics::FormatPercent(ai.meter[i].percent, true);
        const std::wstring reset = metrics::FormatAiReset(ai.meter[i].resetsAt);
        if (!reset.empty()) {
            tip += L" (" + TF(L"resets {time}", { { L"time", reset } }) + L")";
        }
    }
    return tip;
}
std::wstring AiTrayTip(const metrics::AiUsage& ai) {  // szTip cabe en 128 wchar: compacto
    if (!ai.ok) {
        const wchar_t* reason = ai.error == metrics::AiError::NoToken ? L"No API key set"
                              : ai.error == metrics::AiError::Unauthorized ? L"Invalid API key"
                              : ai.error == metrics::AiError::Forbidden ? L"This key has no OpenCode Go subscription"
                                                                        : L"Could not reach OpenCode";
        return std::wstring(T(L"OpenCode usage")) + L": " + T(reason);
    }
    std::wstring tip = T(L"OpenCode usage") + L" " + metrics::FormatAiUsage(ai);
    const wchar_t* tag[3] = { L"5h", L"W", L"M" };
    for (int i = 0; i < 3; i++) {
        const std::wstring reset = metrics::FormatAiReset(ai.meter[i].resetsAt);
        if (!reset.empty()) tip += std::wstring(L" \u00B7 ") + tag[i] + L" " + reset.substr(11, 5);
    }
    return tip;
}
```

En `UpdateWidget`, para `m.id == Id::OpenCodeUsage`:
```cpp
valText = metrics::FormatAiUsage(g_metricSnap.ai);
sev = metrics::AiSeverity(g_metricSnap.ai);
```
El tooltip del strip usa `AiTip(g_metricSnap.ai)`; no repetir las tres líneas.

En `ApplyMetricTray`, para OpenCode (usa `g_metricSnap`, no `snap`):
- `val = ok ? AiTrayPercent(g_metricSnap.ai) : -1`.
- `sev = metrics::AiSeverity(g_metricSnap.ai)`.
- `barPct = val` (el icono representa el peor límite).
- `tip = AiTrayTip(g_metricSnap.ai)` (≤ 120 chars: `szTip` es `WCHAR[128]`; el detalle completo va sólo en el tooltip del widget).
- Sigue siendo **un solo** icono de bandeja con el uID existente `kTrayUidBase + id`.

- [ ] **Step 3: Menú y comandos**

IDs nuevos:
```cpp
ID_OPENCODE_KEY = 166, ID_OPENCODE_CLEAR, ID_OPENCODE_GET,
```

Dentro del submenú `mm` de System metrics, después de “Component label”:
```cpp
HMENU oc = CreatePopupMenu();
AppendMenuW(oc, MF_STRING, ID_OPENCODE_KEY, T(L"Set API key..."));
AppendMenuW(oc, MF_STRING | (HasOpenCodeToken() ? 0 : MF_GRAYED), ID_OPENCODE_CLEAR, T(L"Clear API key"));
AppendMenuW(oc, MF_STRING, ID_OPENCODE_GET, T(L"Get an API key..."));
AppendMenuW(mm, MF_POPUP, (UINT_PTR)oc, T(L"OpenCode"));
```

Casos en el `switch`:
```cpp
case ID_OPENCODE_GET:
    ShellExecuteW(nullptr, L"open", L"https://opencode.ai/console/api", nullptr, nullptr, SW_SHOWNORMAL);
    break;
case ID_OPENCODE_KEY: {
    std::wstring key;
    if (PromptOpenCodeKey(g_hwnd, key) && !key.empty() && SaveOpenCodeToken(key)) {
        if (g_metricCfg.where[static_cast<int>(metrics::Id::OpenCodeUsage)] == metrics::Where::Hidden) {
            g_metricCfg.where[static_cast<int>(metrics::Id::OpenCodeUsage)] = metrics::Where::Bar;
            SaveMetricsWhere();
        }
        metrics::RequestAiRefresh();
        UpdateWidget();
        ApplyAllMetricTray();
    }
    break;
}
case ID_OPENCODE_CLEAR:
    ClearOpenCodeToken();
    g_metricSnap.ai = metrics::AiUsage{};  // la UI refleja el borrado al instante, no el valor viejo
    g_metricSnap.ok[static_cast<int>(metrics::Id::OpenCodeUsage)] = false;
    UpdateWidget();
    ApplyAllMetricTray();
    break;
```
El submenú de ubicación por métrica ya incluye “OpenCode usage” por recorrer `kMetrics`.

Mientras `PromptOpenCodeKey` esté abierto, `ShowMenu()` debe ignorar nuevos clics del widget (guard estático, p.ej. `g_dialogOpen`): el widget cuelga de la taskbar, no de `g_hwnd`, y un clic durante el modal reabriría el menú encima del diálogo.

- [ ] **Step 4: i18n**

Agregar a `_template.txt`:
```text
OpenCode usage = 
OpenCode = 
Set API key... = 
Clear API key = 
Get an API key... = 
OpenCode API key: = 
Enter the API key. = 
5-hour = 
Weekly = 
Monthly = 
resets {time} = 
No API key set = 
Invalid API key = 
This key has no OpenCode Go subscription = 
Could not read OpenCode usage = 
Could not reach OpenCode = 
```
Traducir las mismas claves en `es.txt`:
```text
OpenCode usage = Uso de OpenCode
OpenCode = OpenCode
Set API key... = Configurar API key...
Clear API key = Borrar API key
Get an API key... = Obtener una API key...
OpenCode API key: = API key de OpenCode:
Enter the API key. = Ingresa la API key.
5-hour = 5 horas
Weekly = Semanal
Monthly = Mensual
resets {time} = se reinicia {time}
No API key set = Sin API key configurada
Invalid API key = API key inválida
This key has no OpenCode Go subscription = Esta key no tiene suscripción a OpenCode Go
Could not read OpenCode usage = No se pudo leer el uso de OpenCode
Could not reach OpenCode = No se pudo conectar con OpenCode
```

- [ ] **Step 5: Verificar a mano**

Run: `build.bat`
Expected: `Built Monitor.exe`.

1. Sin key: el ítem está oculto por default; en `System metrics > OpenCode usage > Taskbar` aparece `--` y tooltip “Sin API key configurada”.
2. `Set API key...` con una key real: guarda, fuerza refresh, pasa a la barra y muestra `x/y/z` en ≤ 5 s.
3. Reiniciar Monitor: sigue mostrando el valor sin volver a pedir la key.
4. `Clear API key`: vuelve a `--`; el valor `OpenCodeToken` desaparece del registro.
5. Key inválida: `--` y “API key inválida”.
6. Desconectar red: `--` y “No se pudo conectar con OpenCode”; CPU/RAM siguen actualizando.
7. `make test` verde.

- [ ] **Step 6: Commit**

```bash
git add src/opencode_dialog.h src/opencode_dialog.cpp src/main.cpp lang/_template.txt lang/es.txt
git commit -m "feat: OpenCode usage widget, key dialog and menu"
```

---

### Task 5: README, spec y cierre

**Files:**
- Modify: `README.md`
- Modify: `docs/monitor-spec.md`
- Modify: `docs/releasing.md` (si la release menciona la lista de métricas)

**Interfaces:**
- Consumes: feature completa.
- Produces: documentación y verificación final.

- [ ] **Step 1: Documentar**

README: agregar “OpenCode Go usage (5-hour / weekly / monthly)” a la lista de métricas; explicar que requiere una API key personal de `https://opencode.ai/console/api`, que se guarda cifrada con DPAPI para el usuario de Windows, que se refresca cada 5 minutos y que sin suscripción Go la API responde 403. Aclarar que no lee la sesión de OpenCode ni Gemini.

Spec: agregar `OpenCodeUsage` a la lista de `MetricId`, `AiUsage` al `Snapshot`, y anotar en backlog el logo PNG y Gemini (sin endpoint oficial).

- [ ] **Step 2: Verificación final**

Run:

```powershell
$env:MSYSTEM='MINGW64'
bash -lc "make test"
```

Expected: todos los targets verdes, incluidos `test-metrics`, `test-ai-usage` y `test-opencode-token`. Después `build.bat` y una ejecución de 10 minutos con una key real: el valor se refresca a los 5 minutos, el resto de métricas no se congela y no aparecen warnings nuevos.

- [ ] **Step 3: Commit**

```bash
git add README.md docs/monitor-spec.md docs/releasing.md
git commit -m "docs: OpenCode usage widget"
```
