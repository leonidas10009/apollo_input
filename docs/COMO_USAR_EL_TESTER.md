# Cómo usar el tester de mandos

## 1. Compilar el tester C++

Desde la raíz del repositorio:

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
```

El ejecutable queda en:

```text
cmake-build-apollo-input/tools/apollo-input-controller-tester.exe
```

## 2. Abrir la versión visual

```bash
cd visual-tester
python server.py
```

O en Windows, doble clic en:

```text
visual-tester/ABRIR_TESTER_VISUAL.bat
```

Se abre `http://127.0.0.1:8765/` con el mando dibujado. El servidor escucha solo
en localhost. Cierra la ventana de consola para detenerlo.

Para automatizaciones:

```bash
python server.py --no-browser
```

## 3. Antes de empezar

1. Conecta el mando físico por USB o Bluetooth.
2. Pulsa un botón del mando para que el navegador lo detecte.
3. Cierra Steam si quieres evitar que Steam Input capture el mando.
4. Para ver un mando virtual, créalo en la pestaña **Perfiles** y luego pulsa un
   botón en él.

## Pestañas de la aplicación visual

### Vivo

- Mando dibujado con botones, sticks, gatillos, D-pad, LB/RB y botones centrales.
- Sticks y gatillos en vivo.
- Botones pulsados y ejes brutos.
- **Probar rumble** cuando el navegador lo soporta.
- **Test de drift** de 5 segundos.

### Botones

- Rejilla con todos los botones.
- Cada botón pulsado se marca en verde.
- Progreso y reinicio de la prueba.

### Sticks

- Dos pads grandes con punto en vivo.
- Valor bruto y filtrado.
- Test anti-drift con máximo bruto, máximo filtrado y PASS/FAIL.
- Deadzone ajustable en tiempo real.

### Perfiles

- Listar dispositivos.
- Pasar mando físico → virtual.
- Self-test virtual.
- Verificar Xbox 360 por XInput.
- Test de drift por CLI.
- Detener la prueba.

Configura origen (`xinput`/`dinput`), índice, perfil y segundos.

ViGEmBus solo crea Xbox 360 y DualShock 4 de forma nativa. Los demás perfiles
usan fallback y el tester muestra el perfil real.

### Registro

Historial de eventos, mandos conectados y salida del tester CLI.

## 4. Tester de consola

Uso directo:

```bash
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe list
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe monitor xinput 0
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe drift xinput 0 --seconds 10
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe bridge xinput 0 --profile ds5 --seconds 120
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe bridge dinput 0 --profile switch --seconds 120
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe selftest --profile xseries --seconds 20
./cmake-build-apollo-input/tools/apollo-input-controller-tester.exe verify
```

## Perfiles

| Perfil | Alias |
|---|---|
| `xbox_360` | `x360` |
| `xbox_one` | `xone` |
| `xbox_series` | `xseries` |
| `xbox_elite` | `elite` |
| `dualshock4` | `ds4`, `ps4` |
| `dualsense` | `ds5`, `ps5` |
| `dualsense_edge` | `ds5_edge` |
| `switch_pro` | `switch` |
| `steam_controller` | `steam` |
| `steam_deck` | `deck` |
| `generic` | `hid` |

## Resultado esperado del drift

Sin tocar los sticks:

```text
PASS: no ghost drift after filtering.
```

En la versión visual:

```text
PASS · drift filtrado 0.0000 · sin movimiento fantasma.
```

Si sale FAIL:

- sube la deadzone de sticks a 5-8 %,
- revisa el mando físico,
- prueba otro puerto/cable,
- compara el valor bruto con el filtrado en la pestaña **Sticks**.

## Problemas comunes

- **No aparece ningún mando:** pulsa un botón, revisa `joy.cpl` y prueba otro
  cable/puerto.
- **XInput no lo ve:** algunos mandos necesitan activar el modo XInput.
- **El mando virtual no aparece:** instala ViGEmBus 1.22.0.
- **El navegador no lo detecta:** usa `http://127.0.0.1:8765/`, no `file://`.
- **Python no se encuentra:** usa `python server.py` desde una consola con Python
  3.10+ en PATH o define `APOLLO_TESTER_EXE` para el ejecutable.
