# Library provenance

The project-local `GL30.kicad_sym` contains GL30 custom symbols and copies of the official KiCad symbols R, C, C_Polarized, D_TVS, Crystal_GND24, PWR_FLAG and TestPoint. Selected official KiCad footprints are copied into `GL30.pretty`.

Official library source: KiCad 10.0.6 Windows distribution, signed by KiCad Services Corporation; downloaded from the Tsinghua mirror listed by the official KiCad Windows download page. Maintainers: the KiCad library community. See https://www.kicad.org/libraries/license/ and the included `KICAD_LIBRARY_LICENSE.md`.

The redistributed library material remains under CC BY-SA 4.0 with the KiCad electronic-design exception. Project symbols are renamed into the GL30 namespace. The copied footprints retain their original pad geometry; external 3D-model links were removed so opening the schematic does not require a separate 3D-model download. Embedded symbols have been flattened where necessary. `generation_manifest.json` records original footprint paths and hashes.

Custom IC symbols were drawn from the component pin table and checked against manufacturer documents; component datasheet URLs are embedded in the schematic. U2 uses KiCad 10 stacked pin numbers for duplicated phase-output pads. A symbol is not a manufacturer-qualified land pattern.

No OSHWHub project files or SmartKnob implementation code were copied into this schematic. Those projects are comparison references listed in `开源项目对照.md`.
