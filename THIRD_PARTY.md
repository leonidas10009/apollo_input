# Third-party notices

ApolloInput Nova is MIT-licensed. It depends on or interoperates with:

## ViGEmClient

- Project: <https://github.com/LizardByte/Virtual-Gamepad-Emulation-Client>
- License: MIT
- Included as the `third-party/ViGEmClient` git submodule for the Windows
  ViGEmBus backend.

## ViGEmBus

- Project: <https://github.com/nefarius/ViGEmBus>
- License: BSD-3-Clause
- Separate signed kernel driver. Not bundled and not required to build the
  library.

## Linux uinput

- Kernel user-space API. No additional library license.

## References used for compatibility research

- [WujekFoliarz/duaLib](https://github.com/WujekFoliarz/duaLib) (MIT)
- [Meorge/SwitchKit](https://github.com/Meorge/SwitchKit) (MIT)
- [adamczykpiotr/simple-xpad](https://github.com/adamczykpiotr/simple-xpad)
- [mdqinc/SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) (zlib)

These are references for controller identities, report formats and mappings.
No code from LizardByte's Source-Available License 1.0 components is used by
ApolloInput Nova.
