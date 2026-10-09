# Third-party notices

Anahita is Copyright (C) 2026 Ehsan Madadi and licensed under GPL-3.0-or-later. It builds on the projects below. Their source code is downloaded unmodified at the pinned commits listed here and compiled into separate engine libraries. Each project keeps its own copyright and license, reproduced or referenced below.

## EPANET 2.2 (U.S. EPA)

- Source: https://github.com/USEPA/epanet-engine, commit `6aff01d7d9be7c9eee64246c6588b486776b3a75`
- Library: `anahita_epanet22_epa`
- License: material original to EPANET 2.0.12 and new material prepared by the United States Government is in the public domain (17 USC 105). Material from Open Water Analytics is under the MIT License:

```
MIT License

Copyright (c) 2016 - 2019 see AUTHORS, excluding material in the public domain.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## EPANET 2.3 (Open Water Analytics)

- Source: https://github.com/OpenWaterAnalytics/EPANET, tag v2.3.5, commit `444725fbf16ab28b0470ef0ed281dc4849d49583`
- Library: `anahita_epanet23_owa`
- License: MIT License, Copyright (c) 2019 (see the AUTHORS file of the project). The full text is the MIT License reproduced above, with "list of authors" added to the notice that must be retained.

## EPANET-MSX 2.0 (U.S. EPA)

- Source: https://github.com/USEPA/EPANETMSX, tag EPANET-MSX-2.0, commit `84c057e0a62b7fb3a89ef555a86fa0e3e0ed87ad`
- Libraries: `anahita_msx20_22_epa` and `anahita_msx20_23_owa` (the same sources linked against each EPANET engine)
- License: MIT License (see the project repository).

## SWMM 5.2 (U.S. EPA)

- Source: https://github.com/USEPA/Stormwater-Management-Model, tag v5.2.4, commit `7952ca837988b1c32f791812eccc9fd64547e093`
- Library: `anahita_swmm52_epa`
- License: public domain.

## SWMM5+ 1.0 (CIMM, University of Texas at Austin), optional

- Source: https://github.com/CIMM-ORG/SWMM5plus
- Distributed separately through the swmm5plus-capi project; not included in Anahita packages unless installed by the user.
- License: The Unlicense (public domain dedication). SWMM5+ was developed under Cooperative Agreement No. 83595001 awarded by the U.S. EPA to the University of Texas at Austin.

## SQLite

- Source: https://www.sqlite.org, amalgamation 3.53.4
- Used for GeoPackage export.
- License: public domain.

## Qt 6

- Source: https://www.qt.io
- Used by the desktop application under the GNU Lesser General Public License version 3. Qt is linked dynamically and its libraries are shipped unmodified. Qt source code is available from https://download.qt.io/official_releases/qt/.

## MinGW-w64 runtime (Windows packages only)

- `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll` are shipped with the Windows build. They are covered by the GCC Runtime Library Exception (GPL-3.0 with exception) and the MinGW-w64 license (ZPL/MIT-style).

## References used for the resilience metrics

The resilience tools are original implementations of published methods and contain no third-party code:

- Todini, E. (2000). Looped water distribution networks design using a resilience index based heuristic approach. Urban Water, 2(2), 115-122.
- Buhl, J., et al. (2004). Efficiency and robustness in ant networks of galleries. The European Physical Journal B, 42, 123-129 (meshedness coefficient).
- The water service availability definition and the JSON network layout follow the Water Network Tool for Resilience (WNTR), https://github.com/USEPA/WNTR, Copyright 2024 National Technology & Engineering Solutions of Sandia, LLC, Revised BSD License. No WNTR code is included in Anahita.
