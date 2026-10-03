# Optional font regeneration input

The checked-in firmware already contains generated glyph coverage arrays. Normal firmware and host builds do not need the original OTF.

To run `../prepare_ui_type.py`, provide `NotoSansCJKsc-Regular.otf` from the upstream [Noto CJK project](https://github.com/notofonts/noto-cjk). The generator records the source SHA-256 in `components/gl30_ui/assets/type-generation.json`; use that manifest to check the exact input before expecting byte-identical regeneration. The original local OTF is retained on the project owner's computer and is not redistributed in this Git snapshot.

The source and generated glyph material retain SIL Open Font License 1.1. See `LICENSE` here and `../../components/gl30_ui/assets/LICENSE_Noto_OFL.txt`.
