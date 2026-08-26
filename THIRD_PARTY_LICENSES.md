# Third-party licenses

This project is original work licensed under GPL-3.0 (see LICENSE). No
Equalizer APO source code is used or copied.

## Dependencies

### VST3 SDK interfaces (pluginterfaces)

- Source: https://github.com/steinbergmedia/vst3sdk (fetched at build time
  from the `pluginterfaces` part; not vendored in this repository)
- License: GPL-3.0 (Steinberg) — see the SDK's LICENSE file at
  https://github.com/steinbergmedia/vst3sdk
- Usage: the VST3 host in `src/plugins/vst3_host.cpp` compiles against the
  public interface headers (`pluginterfaces/base`, `pluginterfaces/vst`).
  The `STGRAudioServer` binary that links this host is GPL-3.0, consistent
  with the project license.
- Steinberg additionally offers a commercial license for the VST3 SDK;
  see https://www.steinberg.net/sdklicenses for the terms applicable to
  plugin developers.

### VST 2.4 ABI

- The `src/plugins/vst2_host.cpp` file declares the minimal published VST
  2.4 ABI (the `VstEffect` structure layout and opcodes) required to
  interoperate with third-party plugins.
- No Steinberg VST 2.4 SDK source code is included in this repository.
- Steinberg's VST 2.4 SDK (and its license terms) is obtained by plugin
  developers directly from Steinberg; hosting against the published plugin
  ABI does not redistribute SDK material.

### Windows SDK

- Microsoft Windows SDK headers (`audioenginebaseapo.h`, `audiomediatype.h`,
  `mmdeviceapi.h`, etc.) are used under the Microsoft SDK license
  (development-only license terms shipped with Visual Studio).

## GPL notice

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version. This program is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details: https://www.gnu.org/licenses/
