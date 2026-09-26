# Two-bus renewable fault comparison

This fixture compares GridDyn against ANDES commit
`eda5163c9ee8d19945a1dd5d1771fec5da608c27`. The network has a 1 pu
infinite source, a 0.6 pu renewable generator, a 0.4 + j0.1 pu load, and a
0.01 + j0.1 pu line on a 100 MVA base. A shunt fault at the renewable bus
starts at 0.1 s and clears at 0.2 s. The ANDES `Fault` uses `xf=0.2`; the
GridDyn event uses the corresponding shunt susceptance `yq=5`. Both run to
0.9 s. The checked-in ANDES samples use a fixed 0.005 s integration step.

The four CSV files cover these model assemblies:

| CSV                         | ANDES / GridDyn submodels                           |
| --------------------------- | --------------------------------------------------- |
| `andes_reference.csv`       | REGCA1 + REECA1                                     |
| `andes_plant_reference.csv` | REGCA1 + REECA1 + REPCA1                            |
| `andes_wind_reference.csv`  | REGCA1 + REECA1 + WTDTA1 + WTARA1 + WTPTA1 + WTTQA1 |
| `andes_reecb_reference.csv` | REGCA1 + REECB1                                     |

The C++ test `RenewableModels.TwoBusRenewableFaultMatchesAndesReference`
loads `two_bus.xml`, adds each model assembly, and compares bus voltage,
converter P/Q, and electrical-controller Ip/Iq at eight times before, during,
and after the fault. The wind profile also compares generator/turbine speed,
pitch, and mechanical power. The P/Q/current/voltage tolerance is 0.012 pu;
wind-state tolerances are tighter. The test is part of the regular
`GeneratorComponentTests` target and needs no Python or ANDES installation.

To regenerate the CSV files, place an ANDES checkout beside the GridDyn
checkout, then run from the GridDyn root:

```powershell
python test/reference/renewable_fault/generate_andes_reference.py
python test/reference/renewable_fault/generate_andes_reference.py --plant
python test/reference/renewable_fault/generate_andes_reference.py --wind
python test/reference/renewable_fault/generate_andes_reference.py --reecb
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
