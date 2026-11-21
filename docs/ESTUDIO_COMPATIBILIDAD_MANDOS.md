# Estudio de compatibilidad de mandos — ApolloInput Nova 0.2.0

> Estado: **staging**. Esta versión vive primero en
> `ViGEmBus refunded\ApolloInput-Nova-0.2.0` y todavía **no** está integrada de
> forma definitiva en el repositorio de Apollo.

## 1. Objetivo

Ampliar la compatibilidad de mandos de `apollo-input` sin romper lo que ya
funciona y sin depender de un driver con licencia. El trabajo se centra en tres
capas:

1. **Modelo de perfiles**: más tipos de mando virtual y más capacidades.
2. **Selección automática**: elegir el mejor perfil a partir de lo que reporta el
   cliente (familia, touchpad, movimiento, palancas traseras, botones extra).
3. **Fallback**: si el backend no puede crear el perfil preferido, crear uno
   compatible en lugar de fallar.

## 2. Nombre nuevo

La versión se llama **ApolloInput Nova**. El paquete CMake y el espacio de
nombres C++ siguen siendo `apollo-input` / `apollo::input` para no romper a los
consumidores. El nombre Nova distingue esta generación mientras se valida.

## 3. Qué se ha añadido

### Perfiles nuevos

- `xbox_elite`
- `dualsense_edge`
- `steam_controller`
- `steam_deck`

### Capacidades nuevas

- `trigger_rumble`
- `player_leds`
- `battery`
- `paddles`
- `misc_buttons`
- Botón `Button::misc2`

### API nueva

- `profile_capabilities()`
- `profile_identity()`
- `recommended_profile()`
- `profile_compatibility_chain()`
- `Backend::create_compatible()`
- `controller_family_from_string()`
- Alias de perfiles y backends sin distinguir mayúsculas.

### Backends

- **ViGEmBus (Windows)**: sigue creando Xbox 360 y DualShock 4, pero ahora
  reporta capacidades correctas y `create_compatible()` permite que una petición
  de DualSense, Switch Pro o Steam acabe en un mando funcional.
- **uinput (Linux)**: ahora es consciente del perfil. Usa la identidad USB
  correcta (VID/PID/versión), expone todos los perfiles y añade palancas, Share,
  click de touchpad y `misc2` mediante `BTN_TRIGGER_HAPPY*`.
- **Null**: simula capacidades y datos opcionales para pruebas y previews.

### Precisión y anti-drift

- `sanitize_gamepad_state()` aplica una **deadzone radial** a los sticks, no por
  eje. Esto evita que un pequeño movimiento diagonal se convierta en drift en
  un solo eje.
- Los valores dentro de la deadzone se fuerzan a `0,0` exacto.
- Los valores fuera de la deadzone se reescalan para conservar todo el recorrido.
- Los gatillos tienen su propia deadzone y también se reescalan.
- `Device::set_deadzone()` permite ajustar el filtro por dispositivo.
- En el host Apollo, la misma lógica se aplica antes de enviar el estado al
  backend, por lo que cubre libvirtualhid y ViGEmBus.
- Al reanudar una sesión pausada se envía un estado neutro antes de reenganchar
  el mando, evitando que un stick o gatillo quede “pillado”.

### Tester real

La carpeta incluye `bin/apollo-input-controller-tester.exe`:

- enumera XInput, DirectInput e interfaces HID,
- muestra botones/ejes en vivo,
- mide drift en reposo,
- crea mandos virtuales con cualquier perfil o fallback,
- hace bridge físico → virtual,
- reenvía rumble al mando Xbox físico,
- ejecuta self-test visible y verificación XInput.

## 4. Matriz de perfiles

| Perfil | Rumble | Rumble gatillos | Lightbar | LEDs jugador | IMU | Touchpad | Gatillos adaptativos | Batería | Palancas | Botones misc |
|---|---|---|---|---|---|---|---|---|---|---|
| `xbox_360` | Sí | No | No | No | No | No | No | No | No | 0 |
| `xbox_one` | Sí | Sí | No | No | No | No | No | Sí | No | 0 |
| `xbox_series` | Sí | Sí | No | No | No | No | No | Sí | No | 1 |
| `xbox_elite` | Sí | Sí | No | No | No | No | No | Sí | Sí | 1 |
| `dualshock4` | Sí | No | Sí | No | Sí | Sí | No | Sí | No | 0 |
| `dualsense` | Sí | No | Sí | Sí | Sí | Sí | Sí | Sí | No | 1 |
| `dualsense_edge` | Sí | No | Sí | Sí | Sí | Sí | Sí | Sí | Sí | 1 |
| `switch_pro` | Sí | No | Sí | Sí | Sí | No | No | Sí | No | 1 |
| `steam_controller` | Sí | No | No | No | Sí | Sí | No | Sí | Sí | 0 |
| `steam_deck` | Sí | No | No | No | Sí | Sí | No | Sí | Sí | 1 |
| `generic` | Sí | No | No | No | No | No | No | No | No | 0 |

La matriz describe lo que **puede representar el perfil**. Cada backend publica
en `DeviceInfo::capabilities` el subconjunto que realmente puede exponer.

## 5. Matriz de backends

| Backend | Plataforma | Perfiles | Capacidades reales | Requisitos |
|---|---|---|---|---|
| ViGEmBus | Windows | Xbox 360 y DS4; el resto por fallback | Rumble; DS4 con lightbar, IMU y touchpad | ViGEmBus 1.22 instalado |
| uinput | Linux | Todos los integrados | Botones, ejes, D-pad, palancas, Share, click de touchpad | `/dev/uinput` escribible |
| Null | Todas | Todos | Simulación para tests | Ninguno |

ViGEmBus está EOL y no puede crear Xbox One/Series, Elite, DualSense, Edge,
Switch Pro ni Steam. El fallback mantiene esos mandos utilizables como DS4 o
Xbox 360, pero se pierden las características exclusivas. Para tenerlas en
Windows hace falta un backend HID (VHF/UMDF2 o un fork de ViGEmBus).

uinput no puede exponer IMU, touchpad, batería, LEDs ni force feedback porque
Linux input event no es un HID arbitrario. UHID sería el siguiente paso.

## 6. Selección automática

`recommended_profile()` aplica esta lógica:

| Mando del cliente | Perfil recomendado |
|---|---|
| Xbox sin palancas | `xbox_series` |
| Xbox con palancas | `xbox_elite` |
| PlayStation sin touchpad/movimiento | `dualshock4` |
| PlayStation con touchpad/movimiento | `dualsense` |
| PlayStation con palancas | `dualsense_edge` |
| Nintendo | `switch_pro` |
| Steam Controller con touchpad/movimiento | `steam_controller` |
| Steam Deck con touchpad/movimiento/palancas | `steam_deck` |
| Desconocido con touchpad/movimiento | `dualsense` |
| Desconocido con palancas | `xbox_elite` |
| Desconocido sin extras | `xbox_series` |

## 7. Cadenas de fallback

```text
dualsense_edge -> dualsense -> dualshock4 -> xbox_360 -> generic
xbox_elite     -> xbox_series -> xbox_one -> xbox_360 -> generic
steam_deck     -> steam_controller -> dualsense -> dualshock4 -> xbox_360 -> generic
switch_pro     -> xbox_360 -> generic
```

`create_compatible()` devuelve un dispositivo y el perfil real queda en
`DeviceInfo::profile`. Los logs y la UI deben mostrar ese perfil real.

## 8. Mandos físicos

### Xbox

- **Xbox 360**: familia `xbox`; perfil `xbox_series` o `xbox_360`.
- **Xbox One**: perfil `xbox_series`; ViGEm pierde rumble de gatillos y batería.
- **Xbox Series**: perfil `xbox_series`; Share existe en el modelo, pero ViGEm no
  lo expone. uinput lo expone como botón extra.
- **Xbox Elite Series 2**: perfil `xbox_elite`; uinput expone las 4 palancas;
  ViGEm cae a Xbox 360 y las pierde.

### PlayStation

- **DualShock 3**: familia `playstation`; se aproxima con `dualshock4`.
- **DualShock 4**: perfil `dualsense` por touchpad/movimiento o `dualshock4` si
  el juego exige identidad DS4.
- **DualSense**: perfil `dualsense`; ViGEm cae a DS4 y pierde gatillos
  adaptativos y LEDs de jugador.
- **DualSense Edge**: perfil `dualsense_edge`; las dos funciones y las dos
  palancas se modelan como `paddle1..paddle4`.

### Nintendo

- **Switch Pro**: perfil `switch_pro`; rumble, IMU, LEDs de jugador, batería y
  Capture.
- **Joy-Con**: se aproxima con `switch_pro` cuando el cliente los presenta
  combinados.
- **Switch 2 / Joy-Con 2**: sin perfil todavía; SwitchKit tampoco los soporta.

### Valve

- **Steam Controller**: perfil `steam_controller`; se modela un touchpad y un IMU,
  no los dos touchpads por separado. Las palancas se exponen como `paddle1..4`.
- **Steam Deck**: perfil `steam_deck`; igual que el Steam Controller más un botón
  misc.

### Genéricos

- **8BitDo y similares**: en modo XInput usar `xbox_series`; en modo Switch usar
  `switch_pro`; en HID genérico usar `generic`.
- **Modo USB/Bluetooth**: cambia las capacidades que reporta el cliente. El host
  debe confiar en las capacidades reportadas, no en el modelo comercial.

## 9. Repositorios de referencia

| Repo | Licencia | Uso |
|---|---|---|
| [WujekFoliarz/duaLib](https://github.com/WujekFoliarz/duaLib) | MIT | Reportes DS4/DualSense, gatillos adaptativos y botones extra de DualSense Edge |
| [Meorge/SwitchKit](https://github.com/Meorge/SwitchKit) | MIT | Reportes de Switch Pro/Joy-Con, HD Rumble, IMU y LEDs |
| [adamczykpiotr/simple-xpad](https://github.com/adamczykpiotr/simple-xpad) | Ver repo | Comportamiento Xbox One USB/Bluetooth y límites de rumble |
| [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | zlib | GUIDs, mappings estándar y convención de palancas/misc |

Son referencias de protocolo y mapeo. ApolloInput Nova no las enlaza ni copia
código de LizardByte LB-SAL 1.0.

## 10. Limitaciones actuales

1. uinput no expone IMU, touchpad, batería ni LEDs.
2. ViGEmBus no crea DualSense, DualSense Edge, Switch Pro, Xbox Elite ni Steam.
3. Los dos touchpads del Steam Controller se unifican en uno.
4. Switch 2 y Joy-Con 2 no están modelados.
5. No hay backend macOS en esta librería.
6. uinput no tiene force feedback.

## 11. Validación realizada

- [x] Compilación MSYS2/UCRT64 de ApolloInput Nova 0.2.0.
- [x] `apollo-input-core`: perfiles, alias, capacidades, identidades,
      recomendaciones, cadenas de fallback, anti-drift y backend null.
- [x] `apollo-input-vigem-xinput`: creación de mando Xbox 360, lectura por
      XInput, pulsación/liberación de A y movimiento de stick.
- [x] Fallback DualSense -> DualShock 4 en ViGEmBus.
- [x] Tester: `list`, `monitor`, `selftest` y `verify` contra mandos virtuales
      Xbox 360 y DualShock 4.
- [x] Host Apollo: pruebas específicas de deadzone, botones, perfiles Steam y
      reanudación de sesión.
- [ ] Suite completa de Apollo después de aplicar el parche de streaming.
- [ ] Prueba con los mandos físicos del usuario mediante el tester.

## 12. Validación pendiente

- [ ] uinput en una máquina Linux real con `evtest`, `jstest` y SDL3
      `testcontroller`.
- [ ] Reconocimiento de cada perfil uinput por Steam Input.
- [ ] Gatillos adaptativos con un backend HID futuro.
- [ ] Movimiento y rumble de Switch Pro con un backend HID futuro.
- [ ] Palancas de Xbox Elite y DualSense Edge con un backend HID futuro.

## 13. Próximos pasos recomendados

1. Validar con los mandos físicos del usuario usando
   `bin/apollo-input-controller-tester.exe`.
2. Aplicar `apollo-streaming-precision-0.2.0.patch` en Apollo y ejecutar la
   suite completa.
3. Aplicar `apollo-input-0.2.0.patch` en `Apollo-git/libs/apollo-input` solo
   después de la validación.
4. Si se quieren perfiles completos en Windows sin licencia, crear un backend
   **VHF/UMDF2** (no un fork de ViGEmBus) y firmarlo.
5. Si se decide revivir ViGEmBus como fork, llamarlo **ViGEmBus Reforged** y
   mantenerlo en esta misma carpeta de staging.

> Importante: ahora mismo **no hay nada integrado en Apollo server**. El repo
> `Apollo-git` está limpio y `Release\apollo.exe` no se ha tocado.
