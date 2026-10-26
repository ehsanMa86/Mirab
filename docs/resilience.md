# Resilience tools

Mirab implements five analyses natively in C++. They are available in *Analysis > Resilience tools* and through `mirab-cli resilience`. All pressures and flows are in the units of the model. With a SWMM model open, only the network topology analysis is offered (*Analysis > Network topology*); the others need EPANET hydraulics.

## Todini resilience index

For each reporting period:

```
Ir = sum_i q_i (h_i - h*_i) / (sum_k Q_k H_k + sum_p Q_p dH_p - sum_i q_i h*_i)
```

- `q_i`, `h_i`: demand and head at junction `i`,
- `h*_i`: elevation plus the required service pressure,
- `Q_k`, `H_k`: outflow and head of reservoirs and tanks that are supplying water,
- `Q_p`, `dH_p`: flow and head gain of each pump.

The index is the share of the energy entering the network that remains as surplus above the required heads. Values close to 1 indicate a robust network; values near or below 0 indicate one that barely meets demand. It uses the results of the last simulation.

Reference: Todini, E. (2000), Urban Water 2(2), 115-122.

## Water service availability

Delivered demand divided by expected demand, per period, per junction and overall. Expected demand comes from the *Full Demand* result of OWA EPANET 2.3, or from *Demand* plus *Demand Deficit* in EPANET 2.2. Both need a pressure-driven run (`DEMAND MODEL PDA`); with demand-driven runs every demand is met by definition and the tool says so.

## Topology

Graph measures of the network as drawn, independent of hydraulics:

- connected components, average node degree, link density,
- meshedness coefficient `(m - n + 1) / (2n - 5)` (Buhl et al., 2004),
- articulation points: nodes whose loss splits the network,
- bridges: links whose loss splits the network,
- dead ends and isolated nodes.

Bridges and articulation points are computed with an iterative Tarjan search that handles parallel links correctly.

## Pipe criticality

Each pipe is closed in turn and the full extended-period hydraulics are re-run. For each pipe the tool reports:

- how many junctions drop below the pressure threshold that were above it in the base run,
- the share of demand not delivered (pressure-driven runs),
- the lowest pressure anywhere and where it occurs.

By default the runs are pressure-driven so that isolating part of the network shows up as unmet demand instead of negative pressures. Pipes are ranked by impact.

## Fire-flow capacity

For each hydrant node a constant fire demand is added and a single-period hydraulic snapshot is solved at the chosen time of day (a clock time of the model, so its START CLOCKTIME and PATTERN START apply). The tool reports the pressure at the hydrant, the lowest junction pressure and where it occurs, whether every junction stays above the residual pressure, and the largest flow that keeps every junction above it (found by bisection).

## Command line examples

```
mirab-cli resilience topology model.inp
mirab-cli run model.inp --out model.anr
mirab-cli resilience todini model.inp --results model.anr --required-pressure 20
mirab-cli resilience service --results model.anr
mirab-cli resilience criticality model.inp --threshold 20
mirab-cli resilience fireflow model.inp --flow 30 --residual 14 --time 8 --nodes J5,J9
```

Add `--json` for machine-readable output.
