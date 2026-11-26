# UEL1, UEL2C, OEL3C, and OEL4C

Implemented 2026-11-25 from the published IEEE 421.5 block diagrams and
parameter descriptions linked below. This extends the previous native
restricted profiles; it is not a claim of WECC certification or vendor
trajectory parity.

## Equation and signal sources

- [PowerWorld UEL1](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Under%20Excitation%20Limiter%20UEL1.htm)
- [PowerWorld UEL2C](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Under%20Excitation%20Limiter%20UEL2C.htm)
- [PowerWorld OEL3C](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Over%20Excitation%20Limiter%20OEL3C.htm)
- [PowerWorld OEL4C](https://www.powerworld.com/WebHelp/Content/TransientModels_HTML/Over%20Excitation%20Limiter%20OEL4C.htm)
- [Dynawo OEL3C](https://github.com/dynawo/dynawo/blob/master/dynawo/sources/Models/Modelica/Dynawo/Electrical/Controls/Machines/OverExcitationLimiters/Standard/Oel3c.mo)
  independently confirms the powered reference, signed error, limited
  integrator, and integrator-dependent proportional switch.

The C++ equations were written for GridDyn; no Dynawo source was incorporated.
The OEL4C recovery behavior follows the IEEE control-logic footnote in the
PowerWorld diagram: positive reactive-power error passes immediately. Dynawo's
OEL4C instead holds zero PI input whenever Q is below its reference; that
behavior is not used as the recovery reference here.

## Generator interface

The first five limiter inputs remain `XadIfd, Id, Iq, Vd, Vq`. Four additional
inputs carry `Efd`, excitation-system stabilizing feedback `VF`, exciter field
current `VFE`, and the special UEL2C feedback `VFB`. Existing five-input
standalone configurations continue to work when they do not require an
additional signal.

Machine currents, P/Q, and related limits use the machine MVA base. Native OEL
models negate the standard signed correction when exposing GridDyn's `VOEL`;
an exciter subtracts that route at its summing point. `VUEL` remains additive.
Signed limiter bounds are supported. A generic instantaneous limiter remains
an independent model.

Internal exciter signals are exported with analytic state/input derivatives:

| Exciter | VF | VFE | External limiter summing inputs |
| --- | --- | --- | --- |
| EXAC1 | Existing excitation washout output | Existing field feedback including saturation and Ifd loading | UEL and OEL |
| EXST1 | Existing regulator washout output | Unavailable | UEL |
| ESST1A | Existing field-drive derivative feedback | Unavailable | UEL with UEL=1 |
| SCRX | Zero, because this exciter has no excitation-stabilizer block | Unavailable | UEL and OEL |
| ESAC6A | Unavailable | Existing exciter-field feedback including saturation and Ifd loading | UEL and OEL |

Other exciter subclasses may inherit a signal export, but their external
limiter input support is still checked separately. EXAC2 does not gain a
limiter summing input from EXAC1's new route.

The host rejects a required signal that its exciter does not supply. In
particular, nonzero UEL2C `KFB` needs the ST7C-specific `VFB` signal described
in the reference. The native limiter implements this input and its filter;
GridDyn does not yet have an ST7C exciter or a host exporter for that signal.
It is not replaced by Efd, VFE, or VF. Standalone UEL2C can receive VFB through
its input vector. Adding ST7C is separate excitation-model work.

Initialization establishes the exciter's internal signals before initializing
a dependent limiter, then initializes the final exciter reference with the
limiter outputs. DAE and paired algebraic/differential evaluations use the
same values and chain-rule derivatives.

## UEL1

The characteristic is
`VUC=min(abs(KUC*Vt-j*It), VUCmax)`,
`VUR=min(KUR*abs(Vt), VURmax)`, and
`error=VUC-VUR-KUF*VF` in GridDyn's dq convention.

The combined PI output `KUL*error+integral` is limited by VUImin/VUImax;
conditional integration permits recovery at either bound. Two lead-lags
follow the PI, then the VULmin/VULmax output limit. Previously only the
integral was limited. Nonzero KUF and signed output minima are now accepted.
Both zero-denominator lead-lags require zero numerators and bypass cleanly.
A zero terminal voltage has a finite circular characteristic.

## UEL2C

The model supports up to ten complete, strictly increasing P/Q curve pairs.
It implements these stages in the published diagram's order:

1. Filter terminal voltage by TUV, then apply voltage-bias logic. For filtered
   V greater than 1, u=V; otherwise u=1 when V>Vbias, and u=V/Vbias below it.
2. Multiply terminal P by 1/u^K1 **before** the TUP filter. Use the filtered P
   in the table; multiply table Q by u^K2 **before** the TQref filter.
3. Filter terminal Q by TUQ. Form `Qref-Qfiltered-KUF*VF`.
4. SW1=1 selects KFIX. SW1=2 selects the TADJ-filtered adjustable gain,
   limited to [0.1,1]. Its raw input is
   `(Vt^2/Xq+Qt)/hypot(Vt^2/Xq+Qt,Pt)` using unfiltered terminal quantities.
5. Apply the selected gain and limited PI. Subtract the TUL-filtered KFB*VFB
   signal after the PI.
6. Apply the first lead-lag and VUELmin2/VUELmax2 limit, then the second
   lead-lag and VUELmin1/VUELmax1 output limit.

All zero-time-constant filters bypass their current input. K1/K2 accept
0, 1, or 2. The inverse-voltage divisor is floored at 1e-6 pu at a collapsed
terminal; the numerator and Q-reference voltage scaling retain actual u.
This is a numerical regularization and needs validation in severe faults.
The prior unfiltered fixed-profile XML configurations retain zero defaults
for newly added time constants and feedback gains.

## OEL3C

OELInput=0 selects machine Ifd; 1 selects generator Efd; 2 selects exciter
VFE. The selected quantity is scaled by KSCALE and filtered by TF (or
bypassed when TF=0). The signed drive is
`KOEL*(ITFpu^K1-filteredInput^K1)`, for K1=1 or 2. The previous normalized
positive-only pickup was removed.

A limited integrator uses this drive divided by TOEL. SW1 includes
`KPOEL*drive` only when the integral is negative. Thus falling below pickup
allows the integral to recover, and the proportional branch switches off
when the integral returns to zero. A solver root rearms across a 1e-8 band
around zero and holds the selected branch during each solver step so the
discontinuous proportional output can be reinitialized at the event. Explicit
stepping selects the branch from the updated integral sign. An inactive
initial condition starts the integrator at its upper bound so its initial
residual is zero. An initially overloaded condition starts at zero and
begins integrating the violation.

## OEL4C

A continuous Q violation starts the TDELAY timer; returning below Qref resets
it. Negative error is held at zero before the delay expires. Positive error
passes immediately, releasing limitation through the PI. The output is
limited to [VMIN,0], with conditional integration. A released output may
still have a retained integral hidden at the upper PI limit; it must not be
unconditionally cleared on a threshold crossing.

Native XML Qref is an explicit machine-base value. In the positional reader,
Qref=0 selects the generator's physical power-flow Qmax divided by machine
MVA base, following the published PowerWorld parameter convention. Nonzero
Qref is preserved. Native Qref can also be changed by a GridDyn parameter event.

## Positional DYR and DYD input

The supported **GridDyn positional schemas** follow the linked parameter-table
order. Vendor-specific ICON/CON ordering, default/trailer layouts, and
independent PSS/E/PSLF export parity have not been established; these mappings
must not be described as universal vendor-format support.

DYR shape: `BUS 'MODEL' ID parameters /`.
Compact DYD shape: `model BUS "BUS NAME" BASE_KV "ID" : parameters /`.
DYD names are case insensitive and payloads may span lines. The payload
layouts are:

```text
OEL3C: OELInput ITFpu KSCALE TF K1 KOEL TOEL KPOEL
       VOELmax1 VOELmin1 VOELmax2 VOELmin2
OEL4C: Qref TDelay KP KI VMIN
UEL1:  KUR KUC KUF VURmax VUCmax KUI KUL VUImax VUImin
       TU1 TU2 TU3 TU4 VULmax VULmin
UEL2C: TUP TUQ TUV Vbias K1 K2 KUF TQref KFIX TADJ SW1
       KUI KUL VUImax VUImin TU1 TU2 TU3 TU4
       VUELmax1 VUELmin1 VUELmax2 VUELmin2 KFB TUL Xq
       P0 Q0 P1 Q1 ... (2 through 10 complete pairs)
```

Parameter counts, complete numeric consumption, finite values (including
conversion overflow), selector values, curve ordering, and model parameters
are checked before attachment. Missing buses/machine IDs and static-generator
targets are rejected by the existing contextual generator binding. Actual
exciter-signal compatibility is checked during generator initialization.

## Validation

Component tests cover pickup and recovery, input selection, feedback signs,
PI/output bounds, ten-pair curves, filter placement, fixed/adjustable gain,
zero-time bypasses, and analytic derivatives of all active paths. Exciter
exports are checked against finite differences. Reader fixtures check all
four native classes, nondefault machine-base Qmax conversion, initialization,
DAE/paired Jacobians, dynamic stepping, and malformed inputs. An implicit
network run exercises OEL3C pickup and recovery across its proportional-switch
root for each of Ifd, Efd, and VFE.

Local Windows Debug validation on 2026-11-25 passed 433 generator component
tests, 109 reader tests, 92 network component tests, and 113 comparison tests.
Two existing reader tests were skipped. Changed C++ sources passed cpplint
and clang-tidy; the new shared limiter header was also checked explicitly.
These are local results; GitHub CI has not been run for this change.

Independent PSS/E/PSLF/Modelica trajectories, exact vendor record layouts,
severe-fault voltage regularization, and limit-transition trajectory parity
remain validation work. Unit and equation checks do not establish those results.
