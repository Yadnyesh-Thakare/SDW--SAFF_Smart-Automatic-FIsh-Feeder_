# SAFF - Smart Automatic Fish Feeder

An automated smart fish feeder built around an ESP8266 microcontroller, a Real-Time Clock (RTC) module, and a servo. Feeding times and portion sizes are scheduled precisely, and the ESP8266's built-in Wi-Fi hosts a secure, encrypted local web server so schedules and light settings can be controlled from a phone or computer. The feeder also indicates when food in the storage container is running low, and a battery keeps the system running through a power loss.

## Repository layout

```
.
├── .github/workflows/kicad.yml   CI that exports PDFs and fabrication files
├── docs/                         Generated PDFs (schematic and PCB views)
├── hardware/                     KiCad 10 project
│   ├── SDW.kicad_pro
│   ├── SDW.kicad_sch
│   ├── SDW.kicad_pcb
│   ├── fp-lib-table
│   ├── sym-lib-table
│   └── libs/kicad-ESP8266/        Bundled ESP8266 symbols, footprints and 3D shapes
├── mechanical/
│   └── SDW_all.stl               Printable enclosure / assembly
└── README.md
```

## Opening the design

Open `hardware/SDW.kicad_pro` with KiCad 10 or newer. The project is a single-sheet schematic paired with a 2-layer, 40 × 60 mm PCB. The ESP8266 library is bundled and registered through the project's `sym-lib-table` and `fp-lib-table`, so it resolves without any extra setup.

The NodeMCU dev-board 3D model is not bundled. Add it through Footprint Properties → 3D Models if you want it in the 3D viewer.

## Generated outputs

The `KiCad` GitHub Actions workflow runs on every push or pull request that touches `hardware/`:

- `docs/SDW-schematic.pdf`: schematic
- `docs/SDW-pcb-top.pdf`: top view (copper, silkscreen, board outline)
- `docs/SDW-pcb-fab.pdf`: black-and-white fabrication print (both copper layers, silkscreen, outline)
- `kicad-outputs` artifact: Gerbers, Excellon drill file, and CSV bill of materials

PDFs are committed to `docs/` on pushes to `main`; the Gerber/drill/BOM bundle is attached to each workflow run as an artifact.

## 3D design

Printable mechanical design: [https://a360.co/4jwVF6T](https://a360.co/4jwVF6T)
