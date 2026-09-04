# SCRX and ESAC6A derived model specification

This note turns the locally available model descriptions into a GridDyn
implementation contract. It is intentionally explicit about source
differences: a similarly named block or an ANDES reader conversion must not be
treated as exact support.

## Sources and authority

| Model  | Primary local source                                         | Cross-check                                                                     | ANDES status                                                  |
| ------ | ------------------------------------------------------------ | ------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| SCRX   | `OpenIPSL/.../ES/SCRX.mo`                                    | `PowerDynamics.jl/.../PSSE_SCRX.jl` and GridKit `.../Exciter/SCRX/README.md`    | Parsed but mapped to `SEXS`; not exact                        |
| ESAC6A | GridKit `.../Exciter/ESAC6A/README.md` and its block diagram | Existing GridDyn `ExciterEXAC1` supplies the same PSS/E rectifier-loading curve | Parsed but mapped to `SEXS`; explicitly marked TODO/not exact |

OpenIPSL includes and validates SCRX. It has no `ESAC6A` class. Its related
`EXNI` model is a different AC6A-era SCR bridge model and must not be used as
an ESAC6A substitute. ANDES has neither exact model: its DYR YAML lists both
records but routes them to `SEXS`, losing model-specific dynamics and limits.

## SCRX

### PSS/E record and interface

```text
BUS, ID, TATB, TB, K, TE, EMIN, EMAX, CSWITCH, RCRFD
```

Use the existing GridDyn exciter signals: terminal-voltage magnitude for the
SCRX `E_C`/`E_T` voltage inputs, field current `I_fd` (GridDyn's `XadIfd`
signal), voltage-setpoint input, and PSS input `V_S`. The source equations
also include UEL/OEL summing inputs; GridDyn currently treats those as zero
until a dedicated limiter interface is wired. Convert `TATB` to the lead time
constant once on input:

\[
T_A = TATB\,T_B.
\]

The output is field voltage `E_fd`. `CSWITCH=0` means bus-fed and `=1` means
solid-fed. Reject any other value.

### Equations

Define the voltage-regulator input and the lead-lag output as

\[
\begin{aligned}
e &= V_{ref}+V_S+V_{UEL}-V_{OEL}-E_C,\\
T_B\dot x_L &= e-x_L,\\
v_L &= x_L+\frac{T_A}{T_B}(e-x_L),\\
u &= \operatorname{lim}_{[E_{MIN},E_{MAX}]}(x_E),\\
T_E\dot x_E &= K v_L-x_E,\\
M &= (1-CSWITCH)E_T+CSWITCH,\\
v_d &= M u.
\end{aligned}
\]

For `TB=0`, require `TATB=0`, omit `x_L`, and use `v_L=e`. For `TE=0`,
the limited amplifier is algebraic: `u = clamp(K*v_L, EMIN, EMAX)`.

The last step is the negative-field-current crowbar, which is present in
OpenIPSL and in its independently validated PowerDynamics port:

\[
E_{fd}=\begin{cases}
-RCRFD\,I_{fd}, & I_{fd}<0,\\
v_d, & I_{fd}\ge0.
\end{cases}
\]

`RCRFD=0` therefore gives zero output while the negative-current crowbar is
active. GridKit's SCRX README describes `RCRFD` as inactive and omits this
equation. It also adds `VOEL` to the error, whereas OpenIPSL and its
PowerDynamics validation port subtract it. GridDyn should implement the
OpenIPSL/PowerDynamics behavior; keep explicit negative-field-current and OEL
sign regressions because these are the material source disagreements. The
normal positive-current paths are otherwise the same.

The limiter is a stateful hard limiter with outward integration disabled while
clamped. Root detection places the state on the active bound, while the local
explicit-timestep path clamps overshoot directly. This preserves prompt
release when the driving signal turns back toward the admissible range.

### Initialization

For a normal, unsaturated start with `I_fd >= 0`:

\[
\begin{aligned}
M_0 &= (1-CSWITCH)E_{T,0}+CSWITCH,\\
u_0 &= E_{fd,0}/M_0,\\
x_{E,0} &= u_0,\\
v_{L,0}=x_{L,0}=e_0 &= u_0/K,\\
V_{ref,0} &= e_0+E_{C,0}-V_{S,0}-V_{UEL,0}+V_{OEL,0}.
\end{aligned}
\]

Reject this closed-form branch if `M0` or `K` is zero, or if `u0` is outside
the amplifier limits. A negative-current start is a separate crowbar branch
and must satisfy `Efd0 = -RCRFD*Ifd0`.

## ESAC6A

### PSS/E record and interface

```text
BUS, ID, TR, KA, TA, TK, TB, TC, VAMAX, VAMIN, VRMAX, VRMIN,
TE, VFELIM, KH, VHMAX, TH, TJ, KC, KD, KE, E1, SE1, E2, SE2
```

The implemented GridDyn inputs are terminal voltage for `E_C`, voltage
setpoint, `V_S`, field current `I_fd`, and optional absolute per-unit speed
`omega`. The source equation also includes `V_UEL`; GridDyn currently treats it
as zero until a dedicated limiter interface is wired. The speed multiplier is
a model option, not a DYR field; default it to disabled. No direct OpenIPSL or
ANDES implementation establishes an alternate ESAC6A input convention.

### Saturation and rectifier functions

Fit the existing GridDyn quadratic saturation representation from `(E1,SE1)`
and `(E2,SE2)`. When both saturation factors are zero, use `S_E=0`.
Otherwise:

\[
C=\sqrt{SE2/SE1},\qquad S_A=\frac{CE1-E2}{C-1},\qquad
S_B=\frac{SE1}{(E1-S_A)^2},
\]

and use the hard PSS/E curve

\[
S_E(V_E)=S_B\max(V_E-S_A,0)^2.
\]

`ExciterEXAC1` already implements the applicable PSS/E rectifier-loading
curve. Reuse it, including its derivative:

\[
F_{EX}(I_N)=\begin{cases}
1,&I_N\le0,\\
1-0.577I_N,&0<I_N\le0.433,\\
\sqrt{0.75-I_N^2},&0.433<I_N\le0.75,\\
1.732(1-I_N),&0.75<I_N\le1,\\
0,&I_N>1.
\end{cases}
\]

### Equations

The five potential differential states are sensed voltage `V_C`, first
lead-lag state `x_A`, second lead-lag state `x_L`, exciter voltage `V_E`, and
feedback lead-lag state `x_F`. Omit `V_C` when `TR=0` and set `V_C=E_C`;
omit `x_F` when `TH=TJ=0` and set `V_F=U_H` algebraically.

\[
\begin{aligned}
M_\omega &= \begin{cases}\omega,&s_{spd}=1,\\1,&s_{spd}=0,\end{cases}\\
T_R\dot V_C &= E_C-V_C,\\
e &= V_{ref}+V_{UEL}+V_S-V_C,\\
T_A\dot x_A &= K_Ae-x_A,\\
V_A &= \operatorname{clamp}\!\left(x_A+\frac{T_K}{T_A}(K_Ae-x_A),
V_{AMIN},V_{AMAX}\right),\\
T_B\dot x_L &= V_A-x_L,\\
V_L &= x_L+\frac{T_C}{T_B}(V_A-x_L),\\
U_H &= \operatorname{clamp}(K_H(V_{FE}-VFELIM),0,V_{HMAX}),\\
T_H\dot x_F &= U_H-x_F,\\
V_F &= x_F+\frac{T_J}{T_H}(U_H-x_F),\\
V_R &= \operatorname{clamp}(V_L-V_F,V_TV_{RMIN},V_TV_{RMAX}),\\
T_E\dot V_E &= \operatorname{antiwindup}(V_E,V_R-V_{FE},0,+\infty),\\
I_N &= K_C I_{fd}/V_E,\\
V_{FE} &= (K_E+S_E(V_E))V_E+K_D I_{fd},\\
E_{fd} &= M_\omega F_{EX}(I_N)V_E.
\end{aligned}
\]

For `TB=TC=0`, bypass the second lead-lag (`VL=VA`). For `TH=TJ=0`, bypass
the feedback lead-lag and use `VF=UH`. The source diagram places `VFELIM`
at the negative input of the field-limit feedback summation, applies `KH` and
the `0..VHMAX` clamp before the `TH/TJ` lead-lag, and scales the `VR` limits by
terminal voltage. These details supersede the earlier prose transcription of
the diagram.

`VA`, `VH`, and `VR` are output clamps, not extra differential states. Their
derivatives must be zero through the active clamp. This maps naturally to
GridDyn's existing transfer-function/limiter and root infrastructure.

### Initialization

Given machine-provided `Efd0` and `Ifd0`, solve the scalar nonlinear output
equation for positive `VE0`:

\[
E_{fd,0}=M_{\omega,0}
F_{EX}(K_C I_{fd,0}/V_{E,0})V_{E,0}.
\]

Then, for an interior-limit start:

\[
\begin{aligned}
V_{FE,0}&=(K_E+S_E(V_{E,0}))V_{E,0}+K_DI_{fd,0},\\
U_{H,0}&=\operatorname{clamp}(K_H(V_{FE,0}-VFELIM),0,V_{HMAX}),\\
V_{F,0}&=U_{H,0},\quad V_{R,0}=V_{FE,0},\quad V_{L,0}=V_{R,0}+V_{F,0},\\
V_{A,0}&=x_{A,0}=x_{L,0}=V_{L,0},\quad V_{C,0}=E_{C,0},\\
e_0&=x_{A,0}/K_A,\\
V_{ref,0}&=e_0+V_{C,0}-V_{UEL,0}-V_{S,0}.
\end{aligned}
\]

Treat starts outside the `VA` or voltage-scaled `VR` limits, with non-positive
`VE`, or with a zero speed multiplier as initialization failures. The `UH`
clamp is included directly in the closed-form initialization.

## GridDyn implementation and test boundary

GridDyn now has native `ExciterSCRX` and `ExciterESAC6A` classes plus exact
PSS/E DYR dispatch. Component tests cover the SCRX lead-lag, bus-fed/solid-fed
selector, negative-field-current crowbar and amplifier-limit entry/release
behavior; ESAC6A diagram topology, voltage-scaled regulator limits, bounded
exciter-state entry/release, saturation, and speed multiplier; plus factory
registration, clone, and parameter validation. ANDES DYR tests cover class
selection, field ordering, initialization, residuals, and analytic Jacobians.

Remaining validation work is external trajectory parity: the OpenIPSL SCRX SMIB
case is a suitable oracle, while ESAC6A still needs an independent captured
reference because GridKit provides equations/diagrams but no runnable model.
