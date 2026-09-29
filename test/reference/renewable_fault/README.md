# Two-bus renewable fault comparison

This fixture compares GridDyn against ANDES commit
`eda5163c9ee8d19945a1dd5d1771fec5da608c27`. The network has a 1 pu
infinite source, a 0.6 pu renewable generator, a 0.4 + j0.1 pu load, and a
0.01 + j0.1 pu line on a 100 MVA base. A shunt fault at the renewable bus
starts at 0.1 s and clears at 0.2 s. The ANDES `Fault` uses `xf=0.2`; the
GridDyn event uses the corresponding shunt susceptance `yq=5`. Both run to
0.9 s. The checked-in ANDES samples use a fixed 0.005 s integration step.

The fault CSV files cover these model assemblies:

| CSV                           | ANDES / GridDyn submodels                           |
| ----------------------------- | --------------------------------------------------- |
| `andes_reference.csv`         | REGCA1 + REECA1                                     |
| `andes_plant_reference.csv`   | REGCA1 + REECA1 + REPCA1                            |
| `andes_wind_reference.csv`    | REGCA1 + REECA1 + WTDTA1 + WTARA1 + WTPTA1 + WTTQA1 |
| `andes_reecb_reference.csv`   | REGCA1 + REECB1                                     |
| `andes_wtds_reference.csv`    | REGCP1 (no PLL) + REECA1 (`PFLAG=1`) + WTDS         |
| `andes_reeca1e_reference.csv` | REGCA1 + REECA1E + area-owned BusROCOF sensor       |

The C++ test `RenewableModels.TwoBusRenewableFaultMatchesAndesReference`
loads `two_bus.xml`, adds each model assembly, and compares bus voltage,
converter P/Q, and electrical-controller Ip/Iq at eight times before, during,
and after the fault. The two-mass wind profile also compares generator/turbine
speed, pitch, and mechanical power; the one-mass WTDS profile compares shaft
speed. The P/Q/current/voltage tolerance is 0.012 pu; wind-state tolerances
are tighter. The test is part of the regular
`GeneratorComponentTests` target and needs no Python or ANDES installation.

The REECA1E profile also compares frequency deviation and ROCOF. The fault
trajectories have different bus angles in the two simulators, so this network
comparison uses 0.0006 pu for frequency deviation and 0.005 pu/s for ROCOF.
`andes_busrocof_angle_reference.csv` contains the full ANDES bus-angle
waveform. `BusROCOFMatchesAndesForIdenticalAngleInput` feeds that same angle
waveform to GridDyn's measurement model and checks the filtered outputs to
0.0001 pu and 0.001 pu/s. This isolates measurement equations from network
angle differences.

To regenerate the CSV files, place an ANDES checkout beside the GridDyn
checkout, then run from the GridDyn root:

```powershell
python test/reference/renewable_fault/generate_andes_reference.py
python test/reference/renewable_fault/generate_andes_reference.py --plant
python test/reference/renewable_fault/generate_andes_reference.py --wind
python test/reference/renewable_fault/generate_andes_reference.py --reecb
python test/reference/renewable_fault/generate_andes_reference.py --wtds
python test/reference/renewable_fault/generate_andes_reference.py --reeca1e
```

This is a comparison of shared control branches, not a claim of complete
model equivalence. The fixture uses `Lvplsw=0` and nonbinding REGCA1 reactive
current ramp limits because the two implementations differ in their enabled
LVPL and zero-rate-limit conventions. REECA1 has `Thld2=0.5`, `Tpord=0`,
constant current-limit curves, and constant-Q control. REPCA1 uses local
voltage control (`RefFlag=1`). The wind fault changes shaft speed slightly but
barely excites pitch; stronger mechanical disturbances and the other control
flags need separate comparisons. The ACTIVSg25k-specific `Lvplsw=1`, zero
reactive recovery limits, and empty VDL tables still need a reference from a
compatible implementation.

The WTDS profile uses `REGCP1` without a PLL, `REECA1 PFLAG=1` with
`Tpord=0.02`, and one-mass `WTDS` with `H=3`, `D=1`, and `w0=1`. The fault
raises shaft speed by about 0.003 pu before it relaxes; this checks the speed
feedback branch and one-mass equation together. ANDES fails initialization
for this fixture with `w0=0.9` because its REECA1 speed algebraic variable
starts at 1.0, so the checked-in trajectory uses nominal initial speed.

The REECA1E profile uses `Kf=4`, `Kdf=0.5` and an area-owned `BusROCOF` sensor
measurement with ANDES-equivalent angle lag, angle washout, and frequency
washout. GridDyn's local timestep uses RK4 with a linear angle interpolation
between samples. The continuous DAE model has analytic Jacobians. The current
DYR extension accepts a local `BUSROCOF` record with name, `Tf`, `Tw`, `Tr`,
and `fn`; `REECA1E` appends `Kf`, `Kdf`, and the quoted measurement name to
the existing REECA1 fields. A nonzero remote `BUSR` is rejected until an
external bus measurement binding is available.

## Grid-forming converter references

`generate_andes_grid_forming_reference.py` builds two load profiles for each
of `REGCV1`, `REGCV2`, `REGF1`, `REGF2`, and `REGF3`: constant P/Q
(`two_bus.xml`) and constant impedance (`two_bus_impedance.xml`). Each profile
has a checked-in CSV with terminal voltage, P, Q, and virtual rotor angle at
the eight sample times above. Both start with 0.4 pu P and 0.1 pu Q at 1 pu
voltage, then the impedance load varies as voltage squared. Regenerate with:

```powershell
python test/reference/renewable_fault/generate_andes_grid_forming_reference.py
```

`GridFormingTwoBusFaultAndAndesReference` compares all eight samples for both
load profiles and all five models. The ANDES run uses a 0.2 ms fixed step:
its default 5 ms step substantially under-resolves the fast `REGF*` current
loops. The reference generator explicitly selects ANDES PQ's dynamic load
fractions for each profile. The earlier 0.08 pu `REGF2` Q discrepancy at
0.21 s came from comparing GridDyn's constant P/Q demand against ANDES's
default constant impedance demand. With constant P/Q on both sides, GridDyn
and ANDES give 2.203 and 2.210 pu, respectively. The test uses 0.02 pu for
voltage, 0.04 pu for P and Q, and 0.1 rad for virtual rotor angle across all
models and both load profiles.

GridDyn accepts a local positional DYR schema for these models; ANDES has no
`psse-dyr.yaml` definitions for them. After `BUS 'MODEL' 'ID'`, the fields are:

| Model            | Fields                                                                                                     |
| ---------------- | ---------------------------------------------------------------------------------------------------------- |
| `REGCV1`         | `fn Tc kw kv M D ra xs Kpvd Kivd Kpvq Kivq KpId KiId KpIq KiIq`                                            |
| `REGCV2`         | `fn kw kv M D ra xs Kpvd Kivd Kpvq Kivq Tid Tiq`                                                           |
| `REGF1`, `REGF3` | `fn rf xf dwmax dwmin wdrp Qdrp Tr Te KPi KIi KPv KIv Pmax Pmin KPplim KIplim Qmax Qmin KPqlim KIqlim Tpm` |
| `REGF2`          | The `REGF1` fields, then `mf dd 'PLL_NAME'`                                                                |

`REGF2` requires a named local sensor that provides frequency deviation;
the reference case uses `PLL2`. These terminal models own their
voltage/frequency controls and reject an attached `REECA1` electrical control
because they expose no current-command inputs.
