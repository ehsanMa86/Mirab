# Engines and compatibility

## Bundled engines

| ID | Name | Library | Notes |
|---|---|---|---|
| `epanet-2.2-epa` | EPA EPANET 2.2 | `mirab_epanet22_epa` | USEPA/epanet-engine |
| `epanet-2.3-owa` | OWA EPANET 2.3 | `mirab_epanet23_owa` | OpenWaterAnalytics/EPANET v2.3.5, default for EPANET models |
| `epanet-msx-2.0-epa22` | EPANET-MSX 2.0 on EPANET 2.2 | `mirab_msx20_22_epa` | linked to `mirab_epanet22_epa` |
| `epanet-msx-2.0-owa23` | EPANET-MSX 2.0 on EPANET 2.3 | `mirab_msx20_23_owa` | linked to `mirab_epanet23_owa` |
| `swmm-5.2-epa` | EPA SWMM 5.2 | `mirab_swmm52_epa` | USEPA/Stormwater-Management-Model v5.2.4, default for SWMM models |
| `swmm5plus-1.0` | SWMM5+ 1.0 | `swmm5plus_capi` | optional, from the swmm5plus-capi project |

Every engine is compiled from unmodified upstream sources into a library with a unique name. Several versions of one solver can therefore be installed side by side, and MSX can run on either EPANET engine.

## Where engines are found

In this order:

1. folders listed in the `MIRAB_ENGINE_PATH` environment variable (`;` separated on Windows, `:` elsewhere),
2. `engines/` next to the executable (Windows and development builds),
3. `../lib/mirab/engines` (Linux),
4. `../Frameworks/engines` (macOS application bundle),
5. the per-user folder: `%LOCALAPPDATA%\Mirab\engines`, `~/Library/Application Support/Mirab/engines` or `~/.local/share/mirab/engines`.

`mirab-cli engines` lists each engine, whether it was found, and the folders searched.

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
| Kinematic wave or steady routing | | | yes | ignored (set to dynamic wave) |
| `INERTIAL_DAMPING` PARTIAL or FULL | | | yes | set to NONE (the only mode SWMM5+ supports) |

"Ignored" features are removed from the copy of the input file given to the engine; the project file itself does not change. The run log lists what was removed:

| Feature | Removed from the engine's copy |
|---|---|
| FAVAD pipe leakage | `[LEAKAGE]` |
| BAR or FEET pressure units | the `PRESSURE` option |
| `EMITTER BACKFLOW` | the option |
| Pollutants | `[POLLUTANTS]`, `[LANDUSES]`, `[COVERAGES]`, `[LOADINGS]`, `[BUILDUP]`, `[WASHOFF]`, `[TREATMENT]`, the pollutant rows of `[INFLOWS]` and `[DWF]` (flow rows stay), and the `REMOVALS` rows of `[LID_CONTROLS]` |
| LID controls | `[LID_CONTROLS]`, `[LID_USAGE]` |
| Streets and inlets | `[STREETS]`, `[INLETS]`, `[INLET_USAGE]` |
| Kinematic wave or steady routing | `FLOW_ROUTING` is set to `DYNWAVE` |
| `INERTIAL_DAMPING` | set to `NONE` |

"Cannot run" blocks the run until you change the engine or the model.

## Results during a run

Every engine delivers the results of each reporting period while it runs, and every engine can be paused between two time steps (see [file-formats.md](file-formats.md#live-results)):

- **EPANET 2.2 and 2.3:** all variables, exactly as in the final results.
- **EPANET-MSX:** the hydraulic pass first, then the species pass with all variables.
- **EPA SWMM 5.2:** read with the SWMM 5.2 `swmm_getValue` functions at the routing step that reaches each reporting time. Node depth, head, volume, inflows and flooding, link flow, depth and velocity, and subcatchment rainfall, evaporation, infiltration and runoff are available; link capacity is approximated from the depth; link volume, snow, groundwater, soil moisture, pollutants and most system values appear when the run ends. The final results are always read from the binary output file.
- **SWMM5+:** node depth and head, link flow, depth and velocity, as in its final results.

## Why engines run in a separate process

The desktop application never loads an engine. It starts `mirab-cli run ... --machine`, which loads the engine, runs it and streams progress as JSON lines (see [file-formats.md](file-formats.md)). This gives:

- **crash isolation**: an access violation in C code, or a Fortran `STOP` inside SWMM5+, ends the solver process only,
- **reliable cancellation**: the process is told to stop and is terminated if it does not,
- **clean state**: EPANET-MSX and SWMM keep global state that is not safe to reuse within one process.

## SWMM5+

SWMM5+ is written in modern Fortran and uses coarrays. It is built separately by the [swmm5plus-capi](https://github.com/ehsanMa86/swmm5plus-capi) project, which adds a C interface (`bind(C)`) to SWMM5+ and produces two libraries:

- `swmm5plus_capi` (or `libswmm5plus_capi`), the SWMM5+ solver with the C interface,
- the EPA SWMM 5.1 library that SWMM5+ uses for input parsing and hydrology.

### Installing SWMM5+

Prebuilt SWMM5+ libraries for Windows (x64), Linux (x86_64) and macOS (Apple silicon and Intel) are published with each [swmm5plus-capi release](https://github.com/ehsanMa86/swmm5plus-capi/releases). Each platform has a manifest, `swmm5plus-capi-<platform>.json`, that lists the files with their sizes and SHA-256 checksums.

- **From the application:** *Project > Simulation engines > Install SWMM5+*. Mirab downloads the manifest for this computer and every file it lists, checks each checksum, installs the files into the per-user engine folder, and then loads the engine once in the solver process to confirm that it works. The same button reads *Update SWMM5+* once it is installed, and *Remove SWMM5+* deletes the installed files.
- **Offline:** download the manifest and the files of one platform from the release page into one folder, then choose the manifest with *Install from file...*, or run:

  ```
  mirab-cli engines install swmm5plus-capi-windows-x86_64.json
  mirab-cli engines verify swmm5plus
  ```

- **Without a window** (scripts and managed installations): `mirab --install-engine swmm5plus`, or pass a manifest URL or file instead of `swmm5plus`. The command returns 0 on success, so it can follow the Mirab installer in a deployment script, for example `mirab.exe --install-engine swmm5plus` on Windows.

Nothing is installed unless every file matches its checksum. A package for another platform, or one with a C interface version other than 1, is refused. The files are placed in the per-user folder (item 5 above); `mirab-cli engines where` prints it, and `--target DIR` installs elsewhere, for example into a shared folder listed in `MIRAB_ENGINE_PATH`.

To use a different download location, such as a mirror on an internal network, set the `MIRAB_SWMM5PLUS_MANIFEST_URL` environment variable to the manifest URL.

`mirab-cli` commands accept `swmm5plus` (or `swmm5+`) as a short name for `swmm5plus-1.0`.

### Building SWMM5+ yourself

Build swmm5plus-capi as described in its README and copy both libraries, with the Fortran runtime if it is not installed system-wide (Windows builds link it into the DLLs), into any engine folder above. Mirab checks the interface version (`s5p_api_version`) before using it. The C interface is documented in the swmm5plus-capi repository.

Status: SWMM5+ upstream is developed with Intel Fortran. swmm5plus-capi builds it with GNU gfortran on Windows (MinGW-w64), Linux and macOS, and with Intel ifx on Linux. It replaces the Intel-only extensions with standard Fortran, and the shell commands SWMM5+ runs (`mkdir`, `cp`, `rm`) with portable code, so no shell is started. Check the swmm5plus-capi README for the current state of each compiler.

SWMM5+ also has modeling rules of its own, which Mirab applies or reports:

- only `INERTIAL_DAMPING NONE` is supported; Mirab sets it for the run,
- routing is always dynamic wave; Mirab sets `FLOW_ROUTING DYNWAVE` for the run,
- pollutants and LID controls are not modeled; Mirab removes them from the engine's copy of the input,
- a subcatchment must drain to a storage node or to a junction next to a conduit; SWMM5+ stops with a configuration error otherwise.
