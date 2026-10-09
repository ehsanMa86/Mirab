# Engines and compatibility

## Bundled engines

| ID | Name | Library | Notes |
|---|---|---|---|
| `epanet-2.2-epa` | EPA EPANET 2.2 | `anahita_epanet22_epa` | USEPA/epanet-engine |
| `epanet-2.3-owa` | OWA EPANET 2.3 | `anahita_epanet23_owa` | OpenWaterAnalytics/EPANET v2.3.5, default for EPANET models |
| `epanet-msx-2.0-epa22` | EPANET-MSX 2.0 on EPANET 2.2 | `anahita_msx20_22_epa` | linked to `anahita_epanet22_epa` |
| `epanet-msx-2.0-owa23` | EPANET-MSX 2.0 on EPANET 2.3 | `anahita_msx20_23_owa` | linked to `anahita_epanet23_owa` |
| `swmm-5.2-epa` | EPA SWMM 5.2 | `anahita_swmm52_epa` | USEPA/Stormwater-Management-Model v5.2.4, default for SWMM models |
| `swmm5plus-1.0` | SWMM5+ 1.0 | `swmm5plus_capi` | optional, from the swmm5plus-capi project |

Every engine is compiled from unmodified upstream sources into a library with a unique name. Several versions of one solver can therefore be installed side by side, and MSX can run on either EPANET engine.

## Where engines are found

In this order:

1. folders listed in the `ANAHITA_ENGINE_PATH` environment variable (`;` separated on Windows, `:` elsewhere),
2. `engines/` next to the executable (Windows and development builds),
3. `../lib/anahita/engines` (Linux),
4. `../Frameworks/engines` (macOS application bundle),
5. the per-user folder: `%LOCALAPPDATA%\Anahita\engines`, `~/Library/Application Support/Anahita/engines` or `~/.local/share/anahita/engines`.

`anahita-cli engines` lists each engine, whether it was found, and the folders searched.

## Capability manifest

Each engine declares how it handles features whose support differs between engines. Anything not listed is fully supported.

| Feature | EPA 2.2 | OWA 2.3 | SWMM 5.2 | SWMM5+ |
|---|---|---|---|---|
| Pressure-driven demand | yes | yes | | |
| FAVAD pipe leakage `[LEAKAGE]` | ignored | yes | | |
| PCV valves | cannot run | yes | | |
| CMS flow units | cannot run | yes | | |
| BAR or FEET pressure units | ignored | yes | | |
| `EMITTER BACKFLOW` option | ignored | yes | | |
| Pollutants | | | yes | ignored |
| LID controls | | | yes | ignored |
| Streets and inlets | | | yes | ignored |
| Kinematic wave or steady routing | | | yes | ignored (always dynamic wave) |
| `INERTIAL_DAMPING` PARTIAL or FULL | | | yes | set to NONE (the only mode SWMM5+ supports) |

"Ignored" features are removed from the copy of the input file given to the engine, and the run reports what was removed. "Cannot run" blocks the run until you change the engine or the model.

## Why engines run in a separate process

The desktop application never loads an engine. It starts `anahita-cli run ... --machine`, which loads the engine, runs it and streams progress as JSON lines (see [file-formats.md](file-formats.md)). This gives:

- **crash isolation**: an access violation in C code, or a Fortran `STOP` inside SWMM5+, ends the solver process only,
- **reliable cancellation**: the process is told to stop and is terminated if it does not,
- **clean state**: EPANET-MSX and SWMM keep global state that is not safe to reuse within one process.

## SWMM5+

SWMM5+ is written in modern Fortran and uses coarrays. It is built separately by the [swmm5plus-capi](https://github.com/ehsanMa86/swmm5plus-capi) project, which adds a C interface (`bind(C)`) to SWMM5+ and produces two libraries:

- `swmm5plus_capi` (or `libswmm5plus_capi`), the SWMM5+ solver with the C interface,
- the EPA SWMM 5.1 library that SWMM5+ uses for input parsing and hydrology.

Copy both into any engine folder above. Anahita checks the interface version (`s5p_api_version`) before using it. The C interface is documented in the swmm5plus-capi repository.
