# Native schematic symbol provenance

These Altium sheets are derived from the project's reviewed KiCad A01-A07 circuit sheets. The conversion preserves circuit symbols, pin numbers, electrical types and connections; it excludes the test-point sheet and its 66 parts. Three otherwise isolated U1 pins are explicitly unused after test-point removal.

Resistor, capacitor, polarized capacitor, TVS and crystal symbol geometry originates from the official KiCad 10.0.6 libraries. This adapted library material remains under CC BY-SA 4.0 with the KiCad electronic-design exception; see the included `KICAD_LIBRARY_LICENSE.md` and https://www.kicad.org/libraries/license/. The conversion changes the storage format and placement/text representation for native Altium schematic objects. No KiCad footprint geometry is included in this AD package.

Custom IC and interface symbols originate from the GL30 project, drawn from the reviewed component pin tables and manufacturer documents. Datasheet URLs are embedded as symbol parameters. All three paired DRV8316 output pins are separate physical pins in the AD files. A schematic symbol is not a manufacturer-qualified PCB land pattern.

Native file generation uses the existing local `altium-monkey` package, version 2026.8.21; that package's implementation is not redistributed. Altium Designer 26.10.1.5 was used to read the final sheets and independently extract compiled pin/net connections. No OSHWHub board design or third-party SmartKnob implementation was copied.

## User-provided PCB libraries

The user explicitly requested reuse of `G:\dontdel\AD\_Lib` and `G:\dontdel\AD_Lib`. The `User_*.PcbLib` files accompanying this private project are byte-identical copies of the actually referenced user-provided libraries. Source paths, SHA-256 hashes, footprint names and per-reference matching evidence are recorded in `user_footprint_mapping.json` and `verification/user_library_link_check.json`. Original library files were not modified. No separate license to publish or redistribute those user-supplied libraries is asserted.

The linked PCB models do not change schematic pin functions, purchased component part numbers, or fitted/DNP status. Generic package reuse is distinguished from exact manufacturer-part matches in `USER_LIBRARY_CN.md`. The existing library symbols are used as matching evidence; their unrelated part-number parameters and inaccurate datasheet hyperlinks are not copied into the schematic.


## R5 project derivative

`GL30_Adjusted_User.PcbLib` contains one footprint derived from the user-supplied SamacSys B2B-PH-K-S footprint. Only the name, description, plated-hole diameter (0.85 mm), and copper pad size (1.40 mm) were adjusted. Source libraries are unchanged; embedded 3D payloads are preserved. This project copy retains the source library licensing conditions.


## R6 J4 project derivative

`GL30_J4_Header.PcbLib` derives one footprint from the user-supplied `STM32F407_Ctrl.PcbLib` / `HDR2.54-LI-5P`. Only the footprint name, description and plated-hole diameter (1.02 mm) were adjusted. The 1.70 mm lands, pad numbers, positions, shapes and embedded 3D payloads are preserved. The original library is unchanged; source licensing conditions remain applicable.

## Public snapshot distribution

The public Git snapshot intentionally omits all five user-supplied or derived PcbLib files. The original local project is unchanged. The document-list-only change in the public PrjPcb is documented in README_CN.md; embedded native schematic model links are retained. No additional redistribution rights for personal libraries are claimed.
