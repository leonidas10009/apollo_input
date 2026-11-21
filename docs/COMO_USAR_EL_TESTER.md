# Cómo usar el tester de mandos

## Abrir la versión visual (recomendada)

Doble clic en:

`E:\ProyectoProgramacion\apolo_V2026\ApolloInput Nova Visual Tester.lnk`

O en:

`E:\ProyectoProgramacion\apolo_V2026\ViGEmBus refunded\ApolloInput-Nova-0.2.0\ApolloInput Nova Visual Tester.lnk`

También puedes ejecutar:

`E:\ProyectoProgramacion\apolo_V2026\ViGEmBus refunded\ApolloInput-Nova-0.2.0\visual-tester\ABRIR_TESTER_VISUAL.bat`

Se abre una página local en `http://127.0.0.1:8765/` con el mando dibujado.
No hace falta MSYS2 ni comandos.

> El servidor local escucha solo en `127.0.0.1`. Cierra la ventana del servidor
> para detenerlo.

## Antes de empezar

1. Conecta el mando físico por USB o Bluetooth.
2. Pulsa **un botón del mando** para que el navegador lo detecte.
3. Cierra Steam si quieres evitar que Steam Input capture el mando.
4. Para ver un mando virtual, créalo en la pestaña **Perfiles** y luego pulsa un
   botón en él.

## Pestañas de la aplicación visual

### Vivo

- Mando dibujado con botones, sticks, gatillos, D-pad, LB/RB y botones centrales.
- Sticks en vivo.
- Gatillos LT/RT en vivo.
- Lista de botones pulsados.
- Ejes brutos.
- **Probar rumble** (si el navegador y el mando lo permiten).
- **Test de drift** de 5 segundos.

### Botones

- Rejilla con todos los botones.
- Se marca en verde cada botón que hayas pulsado.
- Barra de progreso y contador.
- **Reiniciar** para repetir la prueba.

### Sticks

- Dos pads grandes con punto en vivo.
- Valor bruto y valor filtrado.
- Test anti-drift con máximo bruto, máximo filtrado y veredicto PASS/FAIL.
- Mueve los sliders de deadzone para ver el efecto en tiempo real.

### Perfiles

Permite lanzar el tester real `apollo-input-controller-tester.exe` desde la
página:

- Listar dispositivos.
- Pasar mando físico → virtual.
- Self-test virtual.
- Verificar Xbox 360 por XInput.
- Test de drift por CLI.
- Detener la prueba.

Configura:

- **Origen:** XInput (Xbox) o DirectInput (otros).
- **Índice / slot:** normalmente `0`.
- **Perfil:** xbox_360, xbox_series, dualshock4, dualsense, switch_pro, etc.
- **Segundos:** duración de la prueba.

Cuando crees un mando virtual, vuelve a **Vivo** o a **Perfiles**, pulsa un botón
en el mando virtual y selecciónalo en el desplegable **Mando detectado**.

ViGEmBus solo crea Xbox 360 y DualShock 4 de forma nativa. Los demás perfiles
usan fallback y la aplicación mostrará el perfil real.

### Registro

Historial de eventos, mandos conectados y salida del tester CLI.

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

## Versión de consola

Si prefieres el menú clásico:

`E:\ProyectoProgramacion\apolo_V2026\ApolloInput Nova Tester.lnk`

o

`E:\ProyectoProgramacion\apolo_V2026\ViGEmBus refunded\ApolloInput-Nova-0.2.0\ABRIR_TESTER.bat`

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

## Importante

Este tester **no está integrado en Apollo server**. Es una herramienta de
diagnóstico de la versión en staging. Apollo sigue limpio y sin cambios.
