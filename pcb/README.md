# Scope Hub PCB

KiCad 10 project for the Scope Hub board: an RP2350A controller with six
switched 12 V outputs, six USB-C ports behind two TUSB hubs, per-port current
monitors, a GPS receiver and environment sensors.

- `scope_hub.kicad_pro` opens the project. `scope_hub.kicad_sch` is the root
  schematic; the other `.kicad_sch` files are its sheets.
- `scope_hub.kicad_pcb` is the layout, with custom rules in `scope_hub.kicad_dru`.
- `scope_hub.kicad_sym`, `scope_hub.pretty` and `scope_hub.3dshapes` hold the
  project's own symbols, footprints and 3D models. The library tables point at
  them relative to the project, so the folder opens as is on another machine.
- `fabrication-toolkit-options.json` configures the Fabrication Toolkit plugin,
  which writes the JLCPCB Gerbers, BOM and placement files.

The firmware is in `../firmware` and the host driver in `../driver`.

## License

Copyright (c) 2026 mikefsq

This source describes Open Hardware and is licensed under the CERN-OHL-S v2.
You may redistribute and modify this source and make products using it under
the terms of the [CERN-OHL-S v2](LICENSE). This source is distributed WITHOUT
ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING OF MERCHANTABILITY, SATISFACTORY
QUALITY AND FITNESS FOR A PARTICULAR PURPOSE. Please see the CERN-OHL-S v2
for applicable conditions.
