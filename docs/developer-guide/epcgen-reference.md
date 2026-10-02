# EPCGEN reference and validation basis

GridDyn's EPCGEN implementation is based on the ASU reference supplied with the
case-development materials:

> Deepak Ramasubramanian, *Impact of Converter Interfaced Generation and Load on
> Grid Performance*, doctoral dissertation, Arizona State University, May 2017.

The source PDF is not vendored in this repository. The working source artifact is
currently:

`C:\Users\top1\Downloads\97830527.pdf`

For provenance checking, the SHA-256 of the supplied 7,591,471-byte file is:

`941ADC403E5A2F152BBFBE1443F43678E6E083C9D0D985CD473343CBC1107DEB`

## Relevant source material

- Chapter 4, §4.3.1, printed pages 44–46: outer voltage/reactive-power and
  active-power control equations (4.1)–(4.12).
- Chapter 4, §4.3.3, printed pages 47–50: controlled-voltage-source equations
  (4.13)–(4.15), modulation-ratio limits, and protection behavior.
- Chapter 5, §5.1.2, printed pages 74–81: three-machine/nine-bus validation and
  comparison of the `epcgen` model with PLECS.
- Table 5.1, printed page 76: controller values used for the small validation
  case.
- Appendix A, printed pages 193–195: three-machine equivalent power-flow and
  dynamic data.
- Appendix D, printed pages 214–221: the complete EPCL implementation of the
  `epcgen` model.

The PDF page numbers are offset by the dissertation front matter. For example,
the EPCL listing is PDF pages 249–255, while its printed pages are 215–221.

## Implementation boundary

The supplied EPCL model is a positional user-written model with parameters such as
`Tr`, `Kp`, `Ki`, `Kip`, `Kiq`, `Rq`, `Rp`, `TQ`, `TG`, `T1`, `T2`, `TD`, `Ted`,
`Teq`, `MWcap`, `Pmax`, `Qmax`, `Qmin`, `dV`, `dt`, `Imax`, and `Tfl`.

The `base080626.dyd` records instead identify `epsbes.p` version 7 and expose
named fields such as `rsrc`, `xsrc`, `tfrq`, `ofpdb`, `ufpdb`, `ofpdroop`,
`ufpdroop`, `vbreak`, `imax`, `pmax`, `pmin`, and `pref`. The DYD adapter maps
those fields explicitly. Parameters that the v7 record does not expose remain
model defaults; they must not be described as independently validated against the
EPCL source until the matching `epsbes.p` v7 definition is available.

The regression tests use the dissertation's documented inner-loop and coupling
values where those values are available, and test the invariants that are
independent of a particular network solution: ten-state initialization, preservation
of the requested initial P/Q, finite DAE residual/Jacobian behavior, and acceptance
of the named v7 DYD schema.
