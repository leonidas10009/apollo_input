# Notas sobre ViGEmBus

## Contexto

Este repositorio **no incluye** el código fuente de ViGEmBus. El driver oficial
1.22.0 (BSD-3-Clause, EOL) solo emula:

- Xbox 360
- DualShock 4

No puede crear Xbox One/Series, Xbox Elite, DualSense, DualSense Edge, Switch Pro,
Steam Controller ni Steam Deck. Tampoco expone gatillos adaptativos ni palancas.

## Qué hace ApolloInput Nova

ApolloInput Nova **no modifica ViGEmBus**. Usa el driver oficial para Xbox 360 y
DS4 y añade:

- `create_compatible()` para que una petición de DualSense/Switch/Steam caiga a
  DS4 o Xbox 360 en lugar de fallar.
- capacidades correctas por perfil.
- un backend uinput Linux con todos los perfiles.
- un modelo de perfiles preparado para futuros backends HID.

## Si se quiere ampliar ViGEmBus

Un fork real de ViGEmBus implica:

1. Nuevos PDOs USB (DualSense, Switch Pro, etc.) en `sys/`.
2. Cambios en el protocolo IOCTL del bus y en `ViGEmClient`.
3. INF/instalador y pruebas de compatibilidad.
4. Firma de driver (WDK, certificado EV/atestación).

No es necesario para ApolloInput Nova 0.2.0. Si se decide hacerlo, el nombre de
trabajo propuesto es **ViGEmBus Reforged** y debe mantenerse en una carpeta de
staging separada hasta validarse.

## Alternativa recomendada para Windows

Un backend **VHF/UMDF2** en modo usuario:

- crea dispositivos HID reales visibles para juegos,
- soporta DualSense, DualSense Edge, Switch Pro, Steam Controller y Xbox,
- permite IMU, touchpad, batería, LEDs y gatillos adaptativos,
- requiere firma de driver y pruebas en Windows 10/11.

Es el camino que sigue libvirtualhid, pero ApolloInput Nova mantiene su propio
backend MIT para no depender de LB-SAL 1.0.
