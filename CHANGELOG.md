# Changelog

All notable changes to this project after the 0.9.0 release will be documented here

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [unreleased]

## [0.12.0] - 2026-09-25

### Added

- Added a compiled Python package with APIs for loading networks, power flow,
  dynamic simulation, parameter edits, and MATPOWER/PYPOWER export, plus runnable
  examples.
- Added continuous DC optimal power flow for supported polynomial costs, using
  the native solver or the optional HiGHS backend.
- Added `GENSAE` and expanded generator controls, exciters, and governors, along
  with broader PSS/E DYR mappings and EPC+DYD input support.
- Added native PSS/E `GENROE` and `IEEEX1` dynamic models with exact DYR-reader
  mappings, initialization/residual/Jacobian coverage, and component tests.
  `GENROE` uses its exponential saturation curve, while `IEEEX1` supports both
  the zero-`TB` direct path and the lead-lag path. Independent external
  trajectory comparisons remain follow-up validation work.

### Improved

- Expanded PSS/E RAW and MATPOWER compatibility, including transformer records,
  remote voltage control, tap data, and generator cost handling.
- Improved CVODE/SUNDIALS integration and expanded transient simulation
  validation for larger networks and dynamic model behavior.

Major changes in cmake and build system
addition of Braid solvers
upgrade to sundials 3.1
inclusion of HELICS
