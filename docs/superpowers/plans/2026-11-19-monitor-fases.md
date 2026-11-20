# Monitor — Planes por fase

Estado: 19-11-2026. Spec: `docs/monitor-spec.md` (decisiones D1–D15).

## Política de planes

- **Un plan detallado por etapa**, guardado como `docs/superpowers/plans/<fecha>-monitor-etapa-<N>.md`, escrito **cuando la etapa arranca**, no antes.
- Cada plan de una etapa futura debe abrir con una sección **"Deltas desde etapas anteriores"**: qué aprendizajes, desvíos o decisiones nuevas de etapas ya implementadas lo cambian (ej.: "el IOCTL de SSD funcionó sin admin", "el gap uniforme quedó en `Px(12)`, no `Px(14)`").
- Si una etapa revela que una etapa posterior de la spec está mal planteada, **se corrige la spec primero** (commit `docs:`) y el plan de esa etapa se escribe contra la spec corregida. La spec es la fuente de verdad; los planes la implementan.
- Solo la etapa activa tiene plan detallado. Las demás viven acá como skeletons.

| Etapa | Plan detallado | Estado |
|---|---|---|
| 0 — Fundación | `2026-11-19-monitor-etapa-0.md` | **Detallado** |
| 1 — Métricas nativas sin admin | pendiente (`<fecha>-monitor-etapa-1.md`) | Skeleton |
| 2 — Temperaturas sin admin | pendiente | Skeleton |
| 3 — Una sola app | pendiente | Skeleton |
| 4 — Pulido | pendiente | Skeleton |
| 5 — Distribución | pendiente | Skeleton |

## Deltas aprendidos en la Etapa 0 (para los planes de etapas futuras)

- **Re-correr `make test` tras cualquier renombre**: el fixture `Asset()` de `tests/update_core_test.cpp:15,17` referenciaba `BatteryHub-Setup-1.0.10.exe`; falló en CI (no local — la suite no se re-corrió tras el renombre). Corregido en `c0af8cd`; la regla quedó en el plan de la etapa 0.
- **El binario viejo instalado NO responde a `--quit`** (build anterior a ese argumento). La Etapa 3 no puede depender de `--quit` del viejo para el upgrade in-place: cerrarlo con Stop-Process o el diálogo de Inno.
- **`make test` local** requiere `$env:MSYSTEM='MINGW64'` en el proceso padre antes de `bash -lc` (login sourcing de /etc/profile).
- **Toolchain local reparado**: pacman tenía lock stale + entrada fantasma en la DB local + gcc half-installed; reinstalado con `--overwrite` (transacción gcc+make). Quedó sano y reproducible.
- **Mutex heredado verificado en runtime**: Monitor y el BatteryHub viejo no coexisten — exactamente lo que el upgrade in-place necesita.
- **Orquestación hc/Antigravity**: prompts van con `-i` (posicional no funciona); briefs de una línea sin comillas dobles; cada `hc send` del hijo pide permiso (el orquestador aprueba con `keys enter`); CI ~10 min por push; la API anónima lee runs, los logs requieren auth — reproducir local es más rápido.

## Deltas aprendidos en la Etapa 1 (para los planes de etapas futuras)

- **D9 retractada por el usuario**: el hover se mantiene. La eliminación de la E0 se revirtió y se fusionó con el `maxW` del overflow (`1e2957f`); `spec D9` actualizada.
- **El reviewer fresco por tarea paga**: encontró un bug real que el builder no vio (compacto roto en layouts stacked/vertical: gaps hardcodeados, indicador `…` solapado, anchos mal contados) → REJECT → fix `3c25d12` → APPROVE. Mantener el gate.
- **appsR medido en el hilo UIA existente** funcionó; sin lookup, `Reposition` cae a ancho infinito (nunca tapa botones).
- **`--metrics-dump` no imprime desde shells sin consola** (exe `-mwindows` + `AttachConsole`): verificar siempre desde una consola real; path con espacio se rompe en la cadena `hc exec cmd /c` → usar el path corto 8.3.
- **El strip de TraySense no es scripable** (GDI layered): la comparación válida es contra contadores de Windows (RAM Δ0.1 en la verificación final) + ojo humano.
- **uIDs de bandeja**: disjuntos de `StableId` por el bit 8 que `StableId` siempre setea (`| 0x100`), no por rango. Tenerlo en cuenta si se agregan más uIDs.

## Deltas aprendidos en la E1.5 (interacción del widget)

- **Clics**: derecho = menú, izquierdo = Administrador de tareas, diferenciados por `wParam` en el `WM_WIDGET_CLICK` existente; los espejos heredan por compartir clase y proc. Entrada del menú conservada.
- **Hover animado**: 100 ms in / 180 ms out, ease-out cúbico, alfa 20/26, respeta `SPI_GETCLIENTAREAANIMATION`. El `LayoutCache` se llavea por TODOS los campos de `WidgetItem` (13 vía `SameItems`) + tema, DPI, stacked, vertical, tamaño de barra y `maxW`, y memoiza el resultado post-overflow.
- **CRÍTICO para la E2**: cualquier estado visual nuevo que no viva en `WidgetItem` (p. ej. colores de severidad calculados desde métricas) **debe sumarse a la clave del cache** o el strip no repinta. Si la severidad entra como campo de `WidgetItem`, el `SameItems` la cubre.
- **Guarda anti-reinicio**: `StartHover` no-op cuando la dirección ya coincide — sin eso, `WM_MOUSEMOVE` continuo reinicia el fade-in y la animación nunca termina.
- **Tiempos de build**: `make && make test` tarda ~8 min (50+ tests compilados); `build.bat` solo la app es rápido. Planificar los waits del orquestador en consecuencia.

## Deltas aprendidos en la Etapa 2 (para los planes de etapas futuras)

- **Thermal Zone ausente en esta placa** (AMD desktop): `cpu_temp` queda honestamente en `--` (ok=false). Sin admin no hay fuente de die-temp;PawnIO quedó descartado por decisión del usuario. Opciones futuras: nada sin admin — dejar así.
- **SSD real a ~79-80 °C en esta máquina**: número genuino del sensor NVMe (probablemente bajo la GPU), no un bug — el humano lo valida contra HWiNFO. Ya por encima del umbral warn 65 → el ítem se ve naranja de entrada.
- **El delta del cache E1.5 aplicado y verificado**: `severity` entró a `WidgetItem` y a `SameItems`; una severidad que cambia con texto congelado repinta (probado por el reviewer con el camino completo).
- **Segundo REJECT del ciclo de review** (T4): bandeja sin severidad y `shortVal` de temps clampado a 100 — el gate de reviewer fresco se sigue pagando; mantenerlo en E3+.
- **Iconos de bandeja de métricas son value-only** (estilo de la casa: los dispositivos también dibujan solo número); `widget::SeverityColor` es la fuente única de colores de severidad compartida entre strip y bandeja, y `iconSig` incluye severidad.
- **Defaults nuevos no pisan config guardada** (`LoadMetricsWhere` gana): en la máquina del usuario las temps se activan desde el menú, no por reset.

## Skeletons

### Etapa 1 — Métricas nativas sin admin
Cerrado por la spec: `src/metrics/` (sampler 5 s default, hilo propio, pausa por `GUID_CONSOLE_DISPLAY_STATE` sobre el `WM_POWERBROADCAST` existente); CPU %/RAM %/GPU %/VRAM; `WidgetItem` extendido (métrica, glifo, severidad, prioridad); orden métricas→`…`→baterías con gap único; sin hover highlight; menú "Métricas" (ubicación Barra/Bandeja/Oculta + frecuencia 1/2/5); bandeja con uID `0x2000+m` y `RenderIcon`; appsR + compacto + `…` en el hilo UIA existente; wildcards de build + tests de funciones puras; iconos Fluent (validar sprite 16 px).
Aceptación: sin solape con 6 métricas + dispositivos; ±2 vs TraySense simultáneo; sin hover; `make test` verde.
Puntos que pueden moverse al detallar: valor exacto del gap uniforme; cómo se modela `MetricId` vs `DeviceKind` (heredar de lo que la etapa 0 deje tras el renombre); prioridad de ocultamiento fino (batería en low nunca se oculta).

### Etapa 2 — Temperaturas sin admin
Cerrado: CPU temp = PDH Thermal Zone (aclarar "zona, no die" en tooltip); GPU temp = KMT (NVML solo NVIDIA); SSD temp = IOCTL access-0; severidad naranja/rojo 80/90 y 65/75.
Aceptación: vs HWiNFO con sensor nombrado y tolerancia; sin sensor ⇒ `--`.
Verificación embebida: IOCTL de SSD sin elevación (se espera OK; si falla, la métrica queda `--` sin admin — decisión ya tomada en spec).

### Etapa 3 — Una sola app
Cerrado: upgrade in-place con `Monitor-Setup` (mismo AppId, `[InstallDelete]` ya limpa BatteryHub.exe desde etapa 0); lectura única de `HKCU\Software\TraySense` (`Mask2`→toggles, `Interval`, `Offset`→`TaskbarOffset`; `Mode`/`ShowOn` descartados); checklist manual de retiro de TraySense (tarea programada, `C:\Program Files\TraySense\TraySense.exe`, PawnIO opcional desde Configuración).
Aceptación: tras reinicio de sesión hay un solo proceso (Monitor), sin strips ni autostarts ajenos.
Depende de: que la etapa 0 haya dejado el install en build verificable y CI verde — si el instalador local no se pudo probar (sin Inno local), acá se instala Inno y se prueba el upgrade de verdad.

### Etapa 4 — Pulido
Cerrado: alertas de temperatura (patrón `alert_rules.h`); frases nuevas en `lang/_template.txt` + `es.txt`; README completo (instalación, overflow de bandeja Win11, retiro de TraySense); ajuste fino de overflow.
Aceptación: una semana de uso diario sin tocar configuración.

### Etapa 5 — Distribución
Cerrado: release de GitHub sin firmar (`Monitor-Setup-x.y.z.exe` + portable + SHA256SUMS.txt), README con aviso SmartScreen para amigos.
Aceptación: un amigo instala desde la release y funciona con defaults.
Backlog que NO entra: activación del updater (clave + CHANNEL stable + `build.yml:33` + `sign-release.ps1`), Authenticode, renombrado interno + migración de datos, `MetricProvider`, historial genérico, opciones de orden/gap.
