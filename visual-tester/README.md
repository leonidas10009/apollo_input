# ApolloInput Nova Visual Tester

Aplicación local de diagnóstico con interfaz visual similar a un gamepad tester
web, adaptada a ApolloInput Nova.

## Arranque

Primero compila el tester C++ desde la raíz del repositorio:

```bash
cmake -S . -B cmake-build-apollo-input -G Ninja
cmake --build cmake-build-apollo-input --parallel
```

Después doble clic en `ABRIR_TESTER_VISUAL.bat`.

El lanzador:

1. busca `apollo-input-controller-tester.exe` en `../bin`, `../cmake-build-apollo-input/tools` o `../build/tools`,
2. inicia `server.py` en `127.0.0.1:8765`,
3. abre el navegador una sola vez,
4. mantiene el servidor en la ventana de consola hasta cerrarla.

Si el ejecutable está en otra ruta:

```powershell
$env:APOLLO_TESTER_EXE = "C:\ruta\apollo-input-controller-tester.exe"
python server.py
```

Para automatizaciones o capturas:

```powershell
python server.py --no-browser
```

## Qué incluye

- Vista SVG del mando, centrada y adaptativa.
- Detección de mandos con la Gamepad API del navegador.
- Botones, sticks, gatillos, D-pad, LB/RB y botones centrales en vivo.
- Prueba guiada de todos los botones con progreso.
- Test anti-drift de 5 segundos con deadzone configurable.
- Comparación entre valores brutos y filtrados.
- Rumble mediante `vibrationActuator` cuando está disponible.
- Perfiles ApolloInput Nova y fallback.
- Lanzamiento del tester C++ desde la página:
  - listar dispositivos,
  - bridge físico → virtual,
  - self-test virtual,
  - verificación Xbox 360,
  - drift CLI,
  - detener.
- Registro de eventos y salida del tester.

## API local

El servidor solo escucha en localhost:

| Ruta | Método | Uso |
|---|---|---|
| `/api/health` | GET | Estado del servidor y ruta del tester |
| `/api/devices` | GET | Ejecuta `controller-tester list` |
| `/api/start` | POST | Inicia bridge/selftest/verify/drift |
| `/api/stop` | POST | Detiene la prueba actual |
| `/api/status` | GET | Estado y salida de la prueba |
| `/api/clear` | POST | Limpia el log del servidor |

## Limitaciones

- La Gamepad API solo expone el mapeo estándar: no touchpad, IMU, batería ni
  gatillos adaptativos.
- El navegador puede ocultar un mando hasta que se pulsa un botón.
- En Windows, ViGEmBus solo crea Xbox 360 y DualShock 4 de forma nativa.
- Los perfiles DualSense/Switch/Steam usan fallback hasta que exista un backend
  HID completo.
- El servidor es de diagnóstico local; no exponerlo en la red.

## Archivos

| Archivo | Uso |
|---|---|
| `index.html` | Estructura de la interfaz y SVG del mando |
| `styles.css` | Tema visual y responsive |
| `app.js` | Gamepad API, drift, perfiles y llamadas a la API |
| `server.py` | Servidor local y puente al tester C++ |
| `ABRIR_TESTER_VISUAL.bat` | Lanzador para doble clic |
