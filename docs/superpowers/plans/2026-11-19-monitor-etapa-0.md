# Monitor — Etapa 0 (Fundación) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Renombrar la identidad visible del fork (BatteryHub → Monitor), cablear el repo y el updater propios, y dejar un build reproducible — sin romper el upgrade in-place del install existente.

**Architecture:** Se conserva todo identificador interno heredado (clase de ventana, mutex, raíz de registro, Run value, carpetas de datos — spec D3) y se renombra solo lo visible: exe, strings de UI, installer, CI, README, repo destino. El updater queda apuntando al repo propio pero inerte (CHANNEL dev, sin firma — spec D4).

**Tech Stack:** C++17/Win32, MinGW-w64 (g++/windres, vía MSYS2), Inno Setup (solo CI en esta etapa), PowerShell, GitHub Actions, GitHub MCP (creación del repo).

**Spec:** `docs/monitor-spec.md` — este plan implementa su **Etapa 0**. Las decisiones se citan como (D#).

## Global Constraints

- Sin admin, sin PawnIO, sin firma de release: todo build de esta etapa es CHANNEL dev — `build/version.h` debe contener `#define WB_OFFICIAL_RELEASE 0` (D2, D4).
- **NO renombrar internos heredados** (D3): clase de ventana `WirelessBatteryWnd` (`src/main.cpp:1226`), mutex `Local\WirelessBatteryTray` (`main.cpp:1241`), raíz de registro `Software\WirelessBattery` (`main.cpp:64`, `i18n.cpp:15`, `update_win.cpp:18-19`, `devices.cpp`, `widget.cpp`), Run value name `WirelessBattery` (`main.cpp:62`), carpetas `%LOCALAPPDATA%\WirelessBattery` (`battery_history.cpp:35`, `update_win.cpp:326`, `update_apply.cpp:62`) y `%APPDATA%\WirelessBattery\lang` (`i18n.cpp:122`).
- `Repository` pasa a `ignaciorevainera/monitor` (D1); asset esperado `Monitor-Setup-<v>.exe`.
- `.github/workflows/build.yml:33` (gate de canal `stable` al repo de krisgfx9) **se deja como está**: en el repo nuevo todo build es dev ⇒ updater inerte, exactamente lo querido (D4).
- Cero dependencias nuevas. Código y comentarios en inglés (convención del repo); texto de UI vía `lang/*.txt`.
- Licencia resultante AGPL-3.0, con créditos a krisgfx9 (GPL-3.0) y DMTWEAKS (AGPL-3.0).

## Review Focus

1. **Updater mirando repo/asset de un tercero** → un build oficial se auto-reemplazaría por releases ajenas (crítico con upgrade in-place). Test: Task 2 greps — `ignaciorevainera/monitor` y `Monitor-Setup-` presentes, `krisgfx9` ausente en `src/`.
2. **Renombrar un interno heredado** → el install viejo no se cierra con `--quit` y el Run value deja de apuntar a un exe válido. Test: Task 2 grep — el conteo de `WirelessBattery` en `src/` no baja; Task 6 smoke (`--quit` funciona, el viejo arranca de nuevo).
3. **iss renombrado pero scripts/CI no** → pipeline roto o artefactos con nombre viejo. Test: Task 3 greps (`Monitor.iss` ↔ `build-installer.ps1` coherentes, cero `BatteryHub` en `.github/` salvo lo listado) + Task 5 CI verde con artefacto `Monitor`.
4. **Tag accidental publica release con body de donación de upstream y llama webhook ausente** (`build.yml` job `release`). Test: Task 3 elimina Discord/Ko-fi del yml; Task 5 no empuja tags.
5. **Build local marcado oficial** → consultaría releases firmadas inexistentes. Test: Task 2 verifica `build/version.h` con `WB_OFFICIAL_RELEASE 0`.

---

### Task 1: Baseline — docs, toolchain y build antes de tocar nada

**Files:**
- Commit (sin crear/modificar código): `docs/monitor-spec.md`, `docs/superpowers/plans/*` ya escritos.

**Interfaces:**
- Produces: toolchain MinGW-w64 operativo local (`C:\msys64\mingw64\bin` en PATH del shell de trabajo), build reproducible con `build.bat`, suite `make test` verde. Tasks 2+ dependen de esto para poder verificar.

- [ ] **Step 1: Commitear la documentación ya escrita**

```bash
git add docs/monitor-spec.md docs/superpowers/plans/
git commit -m "docs: Monitor spec and per-phase implementation plans"
```

- [ ] **Step 2: Instalar MSYS2 si falta**

Run: `winget install --id MSYS2.MSYS2 -e` (acepta el UAC; si `C:\msys64` ya existe, saltar).
Expected: `C:\msys64` presente.

- [ ] **Step 3: Instalar gcc y make**

Run: `C:\msys64\usr\bin\pacman.exe -S --needed --noconfirm mingw-w64-x86_64-gcc make`
Expected: `C:\msys64\mingw64\bin\g++.exe` y `windres.exe` existen.

- [ ] **Step 4: Build baseline con el código sin modificar**

Run (PowerShell, desde el repo):
```powershell
$env:Path = "C:\msys64\mingw64\bin;$env:Path"
.\build.bat
```
Expected: última línea `Built BatteryHub.exe`. Si falla, parar: el baseline no compila y ningún cambio posterior es evaluable.

- [ ] **Step 5: Suite de tests baseline**

Run:
```powershell
$env:MSYSTEM = "MINGW64"   # before bash starts: /etc/profile builds PATH from it
C:\msys64\usr\bin\bash.exe -lc "cd '/c/Users/Ignacio Revainera/dev/battery-monitor' && make test"
```
Expected: todos los tests compilan y pasan. Guardar el nombre de cualquier test que falle ANTES de modificar algo — esos fallos no son culpa de las etapas.

### Task 2: Renombrar identidad visible en código y build

**Files:**
- Modify: `Makefile` (líneas 6 y 15: target `BatteryHub.exe` → `Monitor.exe`; ídem `clean:`)
- Modify: `build.bat` (línea 20: `-o BatteryHub.exe` → `-o Monitor.exe`; echo final)
- Modify: `scripts/verify-windows-version.ps1` (`BatteryHub.exe` → `Monitor.exe`, mensaje)
- Modify: `res/app.rc` (FileDescription, ProductName, OriginalFilename: `BatteryHub` → `Monitor`)
- Modify: `res/app.manifest` (assemblyIdentity name + description → `Monitor`)
- Modify: `src/update_core.h:7` (`Repository = "ignaciorevainera/monitor"`)
- Modify: `src/update_core.cpp:247` y `src/update_win.cpp:440` (asset `BatteryHub-Setup-` → `Monitor-Setup-`)
- Modify: `src/update_win.cpp:114,371` (user-agent `L"BatteryHub/"` → `L"Monitor/"`)
- Modify: `src/*.cpp` — **regla, no lista**: reemplazar todo literal de cadena visible al usuario que contenga `BatteryHub` (títulos de diálogo, placeholder de tray, textos de update) por `Monitor`. No tocar identificadores, macros `WB_*`, nombres de registro/valores, ni `WirelessBattery`.
- Modify: `lang/*.txt` y `lang/_template.txt` — mismas claves de frase renombradas (`BatteryHub update` → `Monitor update`, etc.), valores de traducción intactos.

**Interfaces:**
- Produces: build `Monitor.exe` (Task 3 lo empaqueta; Task 5 lo sube; Task 6 lo prueba). `build/version.h` generado con CHANNEL dev.

- [ ] **Step 1: Aplicar los edits de arriba (regla: solo literales visibles)**

Encontrarlos con: `Select-String -Path Makefile,build.bat,scripts/*.ps1,src/*,res/app.rc,res/app.manifest,lang/*.txt -Pattern 'BatteryHub'` — cada hit es un edit o una ocurrencia intencional a conservar; en `src/` y `res/` no queda ninguna permitida.

- [ ] **Step 2: Rebuild**

```powershell
$env:Path = "C:\msys64\mingw64\bin;$env:Path"
Remove-Item .\BatteryHub.exe -ErrorAction SilentlyContinue
.\build.bat
```
Expected: `Built Monitor.exe`.

- [ ] **Step 3: Verificar identidad del exe y canal dev**

```powershell
(Get-Item .\Monitor.exe).VersionInfo.ProductName    # -> Monitor
Select-String -Path .\build\version.h -Pattern 'WB_OFFICIAL_RELEASE 0'   # -> hit
```

- [ ] **Step 4: Gates de grep**

```powershell
Select-String -Path Makefile,build.bat,scripts/*.ps1,src/*,res/app.rc,res/app.manifest,lang/*.txt -Pattern 'BatteryHub'   # -> 0 resultados
Select-String -Path src/*.cpp,src/*.h -Pattern 'WirelessBattery'          # -> mismo conteo que antes de los edits (internos intactos)
Select-String -Path src/update_core.h,src/update_core.cpp,src/update_win.cpp -Pattern 'ignaciorevainera/monitor','Monitor-Setup-'   # -> presentes; 'krisgfx9' ausente en src/
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: rename visible identity to Monitor, point updater at ignaciorevainera/monitor"
```

### Task 3: Installer + CI + README

**Files:**
- Rename: `installer/BatteryHub.iss` → `installer/Monitor.iss` (`git mv`)
- Modify: `installer/Monitor.iss` — AppName/AppVerName/AppPublisher/DefaultDirName `{autopf}\Monitor`/`OutputBaseFilename=Monitor-Setup-{#AppVersion}`/`UninstallDisplayIcon` y `UninstallDisplayName` → Monitor/Icons/Tasks/[Run] filename → `{app}\Monitor.exe`/`[InstallDelete]` agrega `Type: files; Name: "{app}\BatteryHub.exe"` (conservando las líneas de `WirelessBattery.exe`)/`[UninstallRun]` → Monitor.exe/sección `[Code]`: `InstalledRunValue()` devuelve `"...Monitor.exe"`, y la comparación de upgrade (iss:104-115) reconoce los paths viejos de `WirelessBattery.exe` **y** `BatteryHub.exe`
- Modify: `installer/build-installer.ps1` — `BatteryHub.iss` → `Monitor.iss`, `BatteryHub.exe` → `Monitor.exe`, zip `BatteryHub-<v>-portable.zip` → `Monitor-<v>-portable.zip`, nombres en `SHA256SUMS.txt` y mensaje final
- Modify: `.github/workflows/build.yml` — name de step "Build Monitor.exe" (cosmético), `upload-artifact`/`download-artifact` name `BatteryHub` → `Monitor`, job `release`: body sin links Ko-fi/PayPal/Sponsors, **eliminar** el step "Announce on Discord" + el permiso `pull-requests: read` (queda sin uso). **No tocar la línea 33** (gate de canal). Agregar una línea de comentario sobre esa línea: `# shortcut: channel gate left on the upstream repo on purpose; flip when signing is activated (spec D4)`.
- Modify: `README.md` y `CONTRIBUTING.md` — reemplazo mínimo de título/marca y links de repo; la reescritura completa es Etapa 4.

**Interfaces:**
- Consumes: `Monitor.exe` del Task 2 (el `.iss` lo empaqueta; no se puede probar local sin Inno Setup — se verifica por CI en Task 5).
- Produces: pipeline CI que builda/testea/empaqueta `Monitor-Setup-<v>.exe` + portable + `SHA256SUMS.txt` con artefacto `Monitor`.

- [ ] **Step 1: git mv del iss + edits del iss**

- [ ] **Step 2: Edits de build-installer.ps1 y build.yml (reglas de arriba; la línea 33 intacta)**

- [ ] **Step 3: Gates de grep**

```powershell
Select-String -Path installer/*.ps1,installer/Monitor.iss -Pattern 'BatteryHub'   # -> solo las líneas históricas de [InstallDelete]/[Code] que refieren exes viejos
Select-String -Path .github/workflows/build.yml -Pattern 'BatteryHub|Ko-fi|Discord'   # -> 0 resultados (el gate línea 33 usa el nombre de repo de krisgfx9, no la marca)
Select-String -Path README.md,CONTRIBUTING.md -Pattern 'BatteryHub'   # -> 0 resultados tras el swap mínimo
```

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: rebrand installer and CI to Monitor; drop upstream release announcements"
```

### Task 4: vendor de referencia + licencia AGPL + atribución

**Files:**
- Create: `vendor/traysense/traysense.c` (copiar de `C:\Users\Ignacio Revainera\dev\usage-monitor\traysense.c`, sin editar)
- Create: `vendor/traysense/LICENSE` (copiar `usage-monitor/LICENSE` — texto AGPL-3.0)
- Create: `vendor/traysense/README.md` — origen (`ignaciorevainera/usage-monitor`, fork de `DMTWEAKS/TraySense`), copyright DMTWEAKS, AGPL-3.0, y una línea: "Reference for the metrics port; not compiled, not distributed."
- Modify: `LICENSE` (raíz) — reemplazar texto GPL-3.0 por AGPL-3.0 (mismo texto de `usage-monitor/LICENSE`)
- Modify: `installer/THIRD_PARTY_NOTICES.txt` — agregar bloque de créditos: BatteryHub/krisgfx9 (GPL-3.0, base del código) y TraySense/DMTWEAKS (AGPL-3.0, origen del código de métricas que se portará)
- Modify: `README.md` — sección License: AGPL-3.0 + atribución a ambos upstreams

**Interfaces:**
- Produces: cumplimiento de licencia para distribución futura (spec: Licencias y atribución). `vendor/traysense/` es la referencia que leerán las etapas 1–2.

- [ ] **Step 1: Copiar los tres archivos de vendor y escribir el README del vendor**

- [ ] **Step 2: LICENSE raíz → AGPL-3.0 + THIRD_PARTY_NOTICES + README license**

- [ ] **Step 3: Verificación**

`Test-Path vendor/traysense/traysense.c, vendor/traysense/LICENSE` → True; `Select-String -Path LICENSE -Pattern 'AFFERO'` → hit.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: switch to AGPL-3.0, vendor TraySense source as port reference, add credits"
```

### Task 5: Repo `monitor` + push + CI verde

**Files:** ninguno (operación de git/remotes).

**Interfaces:**
- Consumes: historial completo + todos los commits de Tasks 1–4.
- Produces: repo público `ignaciorevainera/monitor` con `main` empujado, `origin` = repo nuevo, `upstream` = krisgfx9; pipeline Actions verde.

- [ ] **Step 1: Crear el repo público**

Con GitHub MCP (buscar la herramienta con `search()` dentro de `execute`, algo como `create_repository`) o a mano en la web: nombre `monitor`, público, sin README/licencia iniciales (se empuja la historia del fork). Sin tags.

- [ ] **Step 2: Repuntar remotes**

```bash
git remote set-url origin https://github.com/ignaciorevainera/monitor.git
git remote add upstream https://github.com/krisgfx9/Wireless-Device-Battery-Monitor.git
git push -u origin main
```
Expected: push exitoso con el historial completo. `git ls-remote --heads origin` muestra `main`.

- [ ] **Step 3: Verificar CI**

En Actions del repo nuevo: el run de push sobre `main` debe terminar verde (build `make`, `make test`, installer + zip + sums con `Monitor-Setup-0.0.0.exe`). Duración típica ~10 min.
Expected: artefacto `Monitor` con `Monitor-Setup-0.0.0.exe` + `Monitor-0.0.0-portable.zip` + `SHA256SUMS.txt`. Job `release` no corre (no hay tags).

### Task 6: Smoke local + prueba de rollback

**Files:** ninguno (verificación manual, sin commit).

- [ ] **Step 1: Con el BatteryHub viejo aún corriendo, ejecutar `.\Monitor.exe`** — Expected: la instancia nueva sale de inmediato (el mutex heredado `Local\WirelessBatteryTray` impide dos strips: no hay coexistencia, exactamente lo que exige la spec).

- [ ] **Step 2: Salir del BatteryHub instalado** (tray → Exit; es la copia vieja de `%LOCALAPPDATA%\Programs\Wireless Battery`, sigue intacta).

- [ ] **Step 3: Correr `.\Monitor.exe`** — Expected: widget de taskbar con baterías (sin métricas todavía), tooltip y menú funcionales, placeholder de tray dice "Monitor".

- [ ] **Step 4: `.\Monitor.exe --quit` desde otra consola** — Expected: la instancia cierra (la clase de ventana heredada sigue respondiendo).

- [ ] **Step 5: Rollback: relanzar el BatteryHub viejo** (`C:\Users\Ignacio Revainera\AppData\Local\Programs\Wireless Battery\WirelessBattery.exe`) — Expected: arranca y funciona igual que antes de la etapa. Salir de nuevo y dejar la máquina como estaba.

---

## Notas de ejecución

- Los Steps de PowerShell asumen el repo en `C:\Users\Ignacio Revainera\dev\battery-monitor` hasta que Task 5 repunta remotes (la carpeta local no se renombra en esta etapa).
- El primer push al repo nuevo dispara CI: es la única verificación real del installer (local no hay Inno Setup). Si falla el job de build por el iss, el fix es local + push de nuevo.
- El BatteryHub viejo instalado NO se desinstala en esta etapa (rollback). Su desinstalación/upgrade in-place es Etapa 3.
