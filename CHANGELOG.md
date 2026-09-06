# Changelog

All notable changes to this project after the 0.9.0 release will be documented here

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/).
This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [unreleased]

- Added native PSS/E `GENROE` and `IEEEX1` dynamic models with exact DYR-reader
  mappings, initialization/residual/Jacobian coverage, and component tests.
  `GENROE` uses its exponential saturation curve, while `IEEEX1` supports both
  the zero-`TB` direct path and the lead-lag path. Independent external
  trajectory comparisons remain follow-up validation work.

Major changes in cmake and build system
addition of Braid solvers
upgrade to sundials 3.1
inclusion of HELICS
