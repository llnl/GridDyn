# ESAC5A implementation

GridDyn's `ExciterESAC5A` follows the [ANDES ESAC5A equations](https://docs.andes.app/en/latest/reference/models/Exciter.html#esac5a). The DYR reader uses the PSS/E ESAC5A `CON(J)` through `CON(J+14)` order: `TR, KA, TA, VRMAX, VRMIN, KE, TE, KF, TF1, TF2, TF3, E1, SE1, E2, SE2`. ANDES currently has no ESAC5A entry in its `psse-dyr.yaml`, so the field order comes from the PSS/E model sheet.

With sensed voltage `v_m`, regulator state `V_R`, lead lag state `x_L`, washout state `x_F`, and exciter output `E_fd`, the implemented equations are

```text
TR  v_m'  = V_t - v_m
TF2 x_L'  = V_R - x_L
LL        = x_L + TF3 (V_R - x_L) / TF2
TF1 x_F'  = LL - x_F
WF        = KF (LL - x_F) / TF1
TA  V_R'  = KA (V_ref + V_set - 1 + V_ss - v_m - WF) - V_R
TE  E_fd' = V_R - KE E_fd - S(E_fd)
```

The regulator has antiwindup limits `[VRMIN, VRMAX]`. When `TR=0`, terminal voltage feeds the regulator directly. When `TF2=0`, `LL=V_R` and `TF3` has no effect, matching the ANDES lead lag bypass. The saturation term is the ANDES cutoff quadratic with `S(E1)=E1*SE1` and `S(E2)=E2*SE2`; it is zero when both saturation values are zero. GridDyn sets the reference bias at initialization so the requested field output and all dynamic states start at equilibrium.

Local tests compare perturbed derivatives and residuals against these equations, finite difference every DAE Jacobian column (including input columns), exercise the limiter and the `TF2=0` path, and compare a voltage step with an independent closed form linear response. A DYR test checks all 15 fields and the full model DAE, while an IEEE 14 bus setpoint event checks dynamic integration.
