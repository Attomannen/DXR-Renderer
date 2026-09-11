# TextureCooker

Cooks loose source maps (PNG / TGA / JPG / BMP / TIFF / HDR / DDS) into the
**TGA texture-packing standard** DDS files the engine resolves automatically by
material name (`ModelFactory::AssignDefaultMaterials`).

| Output | Channels | Space | Format |
|--------|----------|-------|--------|
| `<mat>_C.dds`  | RGB = BaseColor, A = Opacity        | sRGB   | `BC7_UNORM_SRGB` |
| `<mat>_M.dds`  | R = AO, G = Roughness, B = Metalness | linear | `BC7_UNORM` |
| `<mat>_N.dds`  | R = Normal.X, G = Normal.Y           | linear | `BC5_UNORM` |
| `<mat>_FX.dds` | R = Emissive mask, G = Height/Displ. | linear | `BC7_UNORM` |

Missing inputs get sensible fills: no AO → white (unoccluded), no roughness → 0.5,
no metalness → 0 — or per-material constants from `cook.json` (below). `_FX` is
written when an emissive/height map **or** a `cook.json` emissive constant exists.
Full mip chains are generated (sRGB-aware for `_C`).

Cooking runs multi-threaded over materials (`--jobs N`, default = CPU count). The
GPU BC7 path is serialised internally; all decode / resize / mip / BC5 work
overlaps.

### Normal convention

The TGE model pixel shader builds its TBN with a **negated bitangent**, i.e. it
expects **DirectX-convention** (green-down) tangent-space normal maps. Most
authored sources (Substance, Khronos Sponza, …) are **OpenGL** (green-up), so the
cooker **flips green by default** (`--src-normals gl`). Pass `--src-normals dx` for
sources already in DX convention, or `--flip-green` / per-material `"flipGreen"`
as an extra XOR toggle for oddballs.

## cook.json (manifest)

Auto-loaded from `<srcDir>/cook.json` (or `--manifest <path>`). Per-material
overrides keyed by FBX material name, plus a `"*"` block for defaults:

```json
{
  "materials": {
    "*":          { "srcNormals": "gl" },
    "glass":      { "baseColor": [0.05,0.06,0.07,1.0], "roughness": 0.05, "metalness": 0.0 },
    "light_bulb": { "baseColor": [0.9,0.85,0.7,1.0], "emissive": [1.0,0.85,0.55], "emissiveStrength": 4.0 }
  }
}
```

Keys: `baseColor [r,g,b,a]`, `roughness`, `metalness`, `ao` (all linear 0..1),
`emissive [r,g,b]`, `emissiveStrength`, `flipGreen`, `srcNormals` (`gl`|`dx`).
A constant is used **only when the matching source map is absent**, so a material
listed here with *no* textures at all is still cooked from constants (fixes
texture-less FBX materials like `glass` / `light_bulb`). Editing `cook.json`
re-cooks everything it can affect.

## Build

Part of the `GameEditor` / `Game` solutions under the **Tools** group, or on its own:

```
generate_tools.bat
MSBuild TextureCooker.sln -p:Configuration=Release -p:Platform=x64
```

Links only `External` (bundles DirectXTex + ufbx). BC7 uses a headless D3D11
device when available; falls back to the CPU codec (`--cpu` forces it).

## Usage

```
TextureCooker --in <srcDir> --out <dstDir>
              [--fbx <model.fbx>] [--tgo <out.tgo>] [--tgm <out.tgm>]
              [--game-root <dir>] [--manifest <cook.json>]
              [--src-normals gl|dx] [--flip-green] [--cpu] [--jobs N]
              [--force] [--recursive] [--quiet]
```

- **`--in`**  directory of source maps. Files are grouped by material: the name
  minus a recognised suffix (`_BaseColor`/`_Albedo`/`_c`, `_Roughness`, `_Metalness`,
  `_AO`/`_Occlusion`, `_Normal`/`_n`, `_Emissive`, `_Height`/`_Displacement`,
  `_Opacity`/`_alpha`). A bare `_m` / `_fx` input is treated as already packed.
- **`--out`**  where the `.dds` are written (put them next to the `.fbx` for
  auto-resolution). Also writes `cook_report.json`.
- **`--fbx`**  read material names with ufbx and name outputs after the real FBX
  material (so name-based auto-resolution matches). Reports materials with no
  source textures.
- **`--tgm`**  write `{ "Fbx": "<rel>" }` import descriptor.
- **`--tgo`**  write an object-definition with the `Model` property + `textures`
  array (one `[c,n,m,fx]` row per FBX material, capped at engine
  `MAX_MESHES_PER_MODEL`, now 128). Name-based auto-resolution covers anything past
  the cap.
- **`--game-root`**  base for the relative paths written into `.tgo` / `.tgm`
  (defaults to `--out`).
- **`--flip-green`**  invert normal-map green (OpenGL ↔ DirectX handedness).
- **`--force`**  ignore up-to-date checks (outputs are otherwise skipped when
  newer than every input).

## Example — Sponza

```
TextureCooker --in  Source/Game/data/sponza/textures ^
              --out Source/Game/data/sponza ^
              --fbx Source/Game/data/sponza/Sponza.fbx ^
              --tgm Source/Game/data/sponza/Sponza.tgm ^
              --tgo Source/Game/data/sponza/Sponza.tgo ^
              --game-root Source/Game/data
```

`Source/Game/data/sponza/textures/cook.json` defines the 7 texture-less Sponza FBX
materials (`glass`, `light_bulb`, …) from constants.

## Notes / limitations

- Sponza ships no AO or per-texel emissive maps. `_M` red = white (unoccluded);
  real per-texel AO needs a mesh-space bake (future). `cook.json` can set a
  per-material constant AO in the meantime.
- `TEX_COMPRESS_PARALLEL` is unavailable (DirectXTex built without OpenMP) — the
  cooker parallelises at the material level instead (`--jobs`).
- On the very first cook, a couple of outputs can re-cook once on the next run due
  to filesystem timestamp granularity; it converges immediately after.
