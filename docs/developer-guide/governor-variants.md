# Governor variants in GridDyn

GridDyn supports six additional governors from the [ANDES TurbineGov reference](https://docs.andes.app/en/latest/reference/models/TurbineGov.html): `TG2`, `TGOV1DB`, `TGOV1N`, `TGOV1NDB`, `HYGOVDB`, and `HYGOV4`.

`GovernorTgov1Variant` contains the shared valve and turbine states for the three TGOV1 variants. The normalized variants add the auxiliary input after the speed droop; the original variant adds it before droop. The two deadband variants apply ANDES's `DeadBand1` to both the controller speed input and turbine damping. The existing `GovernorTgov1` retains its original GridDyn equations.

`GovernorHygovDB` is in the `GovernorHygov` family and reuses its hydro parameters and initialization, but implements the ANDES HYGOVDB filter, gate position, servo, and water column equations. Its turbine damping uses raw speed deviation. ANDES's published HYGOVDB equations do not use `Tr` in the state derivatives, though it remains a model parameter.

`GovernorTG2` and `GovernorHygov4` derive directly from `Governor` because their states differ from the existing steam and hydro classes. `TG2` has a single lead-lag state and optional input deadband and output hard limit. Its default is deadband off and hard limit on. The current [ANDES `DeadBandRT` implementation](https://github.com/CURRENT/andes/blob/master/andes/core/discrete.py) passes the input outside the deadband and returns zero inside; its documented return-direction tracking is not implemented. The `dbc` parameter is retained for input compatibility. `HYGOV4` follows the four ANDES states and the simplified equations documented there; ANDES explicitly ignores the PSS/E HYGOV4 input deadband, valve deadband, and nonlinear valve curve.

## DYR records

ANDES's [published `psse-dyr.yaml`](https://github.com/CURRENT/andes/blob/master/andes/io/psse-dyr.yaml) does not map these six names. GridDyn accepts them as extensions with the format `bus 'MODEL' machine-id parameters /`. Parameters are in the order listed below. These layouts are specific to GridDyn; standard `TGOV1` and `HYGOV` records retain their existing PSS/E order.

| Model      | Parameters after machine ID                                 |
| ---------- | ----------------------------------------------------------- |
| `TG2`      | `R pmax pmin dbl dbu dbc T1 T2`                             |
| `TGOV1DB`  | `R VMAX VMIN T1 T2 T3 Dt dbL dbU`                           |
| `TGOV1N`   | `R VMAX VMIN T1 T2 T3 Dt`                                   |
| `TGOV1NDB` | `R VMAX VMIN T1 T2 T3 Dt dbL dbU`                           |
| `HYGOVDB`  | `R r GMAX GMIN VELM Tf Tr Tg Dt qNL Tw At dbL dbU`          |
| `HYGOV4`   | `Rperm Rtemp UO UC PMAX PMIN Tp Tg Tr Tw At Dturb Hdam qNL` |

The existing ANDES JSON reader imports static network data. ANDES's `psse-dyr.yaml` is a mapping used by ANDES's own DYR reader, not a dynamic model data file, so there is no YAML file to update in GridDyn for these extensions.
