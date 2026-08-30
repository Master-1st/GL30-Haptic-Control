# TI official reference manifest

The public repository keeps only this source manifest. Vendor PDF files are downloaded locally from TI and remain ignored; a source file is reference evidence, not proof that TI reviewed this board or that an official EVM was physically tested.

| Expected local file | Official source | SHA-256 of reviewed copy | Purpose |
| --- | --- | --- | --- |
| `DRV8316_Datasheet_SLVSF16B.pdf` | <https://www.ti.com/lit/ds/symlink/drv8316.pdf> | `39F1977BF713D0697B12967D818C2150BFB697A80FD34BAF4A13E322CB50C6B0` | Pinout, ratings, external components, registers, protection and layout guidance |
| `DRV8316REVM_User_Guide_SLVUBZ9.pdf` | <https://www.ti.com/lit/pdf/SLVUBZ9> | `E96B6659C6E0110D4515BD0A0A15DB56AD13F7055A3DD287ECF2D383C33EF366` | Official EVM interface, protection, test-point and schematic reference |
| `TLV1704_Datasheet_SBOS589.pdf` | <https://www.ti.com/lit/ds/symlink/tlv1704.pdf> | `B971C8CA9AE7DA43D67FA3486F202E6E688D8F5CC4B220268473FC0B4904C42B` | Supply range, input/output behavior and comparator timing |

The TI hardware-source package `SLOC372A` is not part of this repository and this validation board must not be described as a copy of the official EVM PCB. Contributors may download vendor material under its original terms for local comparison, but should not commit it without confirmed redistribution permission.
