# GreenMind plant pixel assets

This directory contains deterministic plant sprites for the STM32 TFT.

## Resource contract

- Size: `48 x 48`
- Preview: RGB PNG
- Device payload: raw `RGB565_BE`
- Raw size: `48 x 48 x 2 = 4608` bytes
- Byte order: most-significant byte first
- Integrity: CRC32 and SHA-256 in `manifest.json`
- Logical artwork grid: `16 x 16`, enlarged by an integer scale of 3

Each species has three presentation states:

- `NORMAL`: the current readings fit the selected plant profile;
- `ATTENTION`: one or more readings need attention;
- `DANGER`: a severe deviation or sensor failure is present.

These states visualize system assessment. They do not diagnose plant disease.

The generated `catalog_preview.png` is arranged as:

- columns: `pothos`, `mint`, `succulent`, `cactus`, `orchid`, `tomato`;
- rows: `NORMAL`, `ATTENTION`, `DANGER`.

Regenerate all assets from the gateway directory:

```powershell
python .\tools\generate_plant_assets.py
```

Do not hand-edit generated PNG or RGB565 files. Change the generator so the
preview, device payload and manifest remain consistent.
