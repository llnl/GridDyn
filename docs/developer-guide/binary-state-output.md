# HDF5 state, diagnostic and recorder output

GridDyn recorder samples, state snapshots, solver callback diagnostics, sparse Jacobians and the
convergence diagnostic use one HDF5 implementation in
[Hdf5DataFile](../../src/utilities/Hdf5DataFile.h). The custom `GDSTATE1`
encoding and reader have been removed. File extensions do not select another
state format; `.h5` is recommended. `saveState` and `loadState` use HDF5.

## Build and ownership

The build uses the existing `ThirdParty/hdf5` submodule checkout. It initializes
a missing checkout through the top-level `submod_update` mechanism,
subject to `ENABLE_SUBMODULE_UPDATE`, and links through `GridDyn::hdf5`, defined
by `config/cmake/addHDF5.cmake`. It uses the recorded submodule revision rather
than fetching a branch's latest commit during configuration. Dependency options
are hidden from the main CMake cache view like the other third-party libraries.
The integration uses the C library, built statically with position-independent
code, and reuses GridDyn's existing zlib. It does not build HDF5's
C++/Fortran/Java wrappers,
examples, tools, MPI, thread-safety or plugin configurations. GridDyn serializes
its HDF5 calls, including destruction. The HDF5 license is installed with the
runtime notices. Updating the upstream submodule does not change the GridDyn
schema version automatically.

High-frequency solver diagnostics retain file sessions and batch the related
state, derivative/RHS and residual vectors into one frame. Sessions with the
same normalized absolute path share one append position. Explicit truncation
of an active session is rejected. Setting `filecapture=false` closes that
solver's sessions; the final owner closes the file. These are ordinary HDF5
sessions, not SWMR or independent-process multi-writer access.

The standalone `saveStateBinary`, `writeVector` and `writeArray` entry points
remain convenience adapters over the same storage implementation. They close
their own file handles on return. Required writes throw `FileOperationError`;
optional solver callback capture catches storage failures, logs a warning and
disables file capture without changing the numerical callback return value.

## Solver snapshots and loading

`saveStateBinary(sim, path, mode, append)` records the selected initialized
solver's state and, for differential modes, its derivatives. When the solver
has a paired mode, the initialized paired solver is recorded in the same frame.
The adapter reads the solver's current mode rather than relying on an earlier
copy of the pairing metadata. Initial periodic partitioned capture occurs after
partitioned startup conditions have populated both vectors.

`readStateBinary(path)` selects the last published snapshot frame, skipping
trial diagnostics. It reads the index in bounded blocks and materializes only
that frame's state arrays. `BinaryStateData` exposes the primary time, mode,
state and derivatives, plus `paired` blocks when present.

`loadStateBinary(sim, path, mode)` checks the mode properties, initialized sizes,
and availability of every destination block before copying any arrays. Stored
offset numbers identify file blocks; the destination's current pairing selects
the paired solver. Load is a solver-vector operation: it does not rewind the
simulation clock, commit component state, restore parameters/events/limit flags,
or recover solver integration history. This remains separate from a complete
[checkpoint/restore operation](checkpoint-restore.md). No physical-consistency reconciliation of user edits
is performed.

## File schema, version 1

Root attributes are `format="griddyn-data"` and `schema_version=uint32(1)`.
All payloads are ordinary typed, extendible one-dimensional datasets:

| Dataset              | Physical type               | Meaning                                                          |
| -------------------- | --------------------------- | ---------------------------------------------------------------- |
| `/records`           | Packed compound table       | Capture metadata and offsets; fields below.                      |
| `/values`            | Little-endian IEEE binary64 | Vector and sparse-matrix values.                                 |
| `/indices`           | Little-endian uint64        | Sparse rows followed by columns, or relative UTF-8 name offsets. |
| `/bytes`             | uint8                       | UTF-8 name bytes.                                                |
| `/committed_records` | Scalar little-endian uint64 | Number of published index rows.                                  |

`/records` has these named fields:

| Fields                              | Type / convention                                                                                                                                          |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `time_ns`                           | Signed int64 elapsed simulation nanoseconds.                                                                                                               |
| `frame`, `index`                    | uint64 frame identity and producer evaluation/block index.                                                                                                 |
| `key`, `paired_key`, `parent_index` | uint64 producer keys; absent values are `UINT64_MAX`. Solver keys are offset indices. `parent_index` is reserved when no enclosing evaluation is supplied. |
| `source`                            | uint32: 0 snapshot, 1 diagnostic, 2 checkpoint runtime, 3 recorder samples/metadata.                                                                       |
| `type`                              | uint32: 0 vector, 1 sparse COUP matrix, 2 names.                                                                                                           |
| `code`                              | uint32 vector role: 0 state, 1 derivative/RHS, 2 residual; matrix role 1 Jacobian.                                                                         |
| `flags`, `approx`                   | uint32 solver mode bits and approximation mask. Mode bits 0-5 mean dynamic, differential, algebraic, local, extended state and parameters.                 |
| `value_offset`, `count`             | uint64 zero-based offset into `/values` and element count; for names, count is number of names.                                                            |
| `index_offset`                      | uint64 offset into `/indices`. A matrix stores `count` rows then `count` columns. Names store `count+1` nondecreasing offsets beginning at zero.           |
| `byte_offset`, `byte_count`         | uint64 range in `/bytes` for names.                                                                                                                        |
| `rows`, `columns`                   | uint64 explicit matrix dimensions; indices are zero based.                                                                                                 |
| `cj`                                | binary64 Jacobian integration coefficient; NaN means unspecified.                                                                                          |

Frames group related records; a differential snapshot has separate state and
derivative records. A partitioned snapshot adds its paired block. The snapshot
producer index is 0 for the primary block and 1 for its partner. Diagnostic
indices retain callback evaluation identity. Sparse output preserves producer
entries, including zeros and duplicate coordinates; it does not densify,
coalesce or change the solver's pattern. Live solver Jacobians record their
actual `cj`. Explicit `captureJacState`/`saveJacobian` captures record the
coefficient used by those helpers (currently 10000).

The convergence experiment uses diagnostic vector codes 3 for a solved state,
4 for a solver return code, 10 for increment/limit metadata and 11 for its
voltage-state mask. Trial state code 0 and outcome share the sample index.
This replaces its separate raw binary writer.

The reader validates the schema, physical datatypes, dataset dimensions and
selected ranges. A frame is published only after all of its payload and index
rows have been written and flushed; `/committed_records` is then advanced.
Readers ignore unpublished index rows. This is logical completeness, not a
transaction or power-failure durability guarantee. Ordinary HDF5 files can
still require recovery after abrupt termination. A failed writer cannot be
reused. Successfully published earlier frames remain the readable prefix.

Large vectors are written directly from their borrowed arrays, synchronously.
Sparse producers fill bounded 8192-entry chunks. `Hdf5DataFile::readRecords` and
`readValues` support range selections. Compression defaults to off; generic
storage callers can select lossless shuffle/deflate levels 1-9. Numeric types
and exact timestamp values are unchanged by compression. No lossy filter is used.

## MATLAB

`readStateFile(filename)` uses MATLAB's HDF5 functions and returns `data` cells
plus `time`, exact `time_ns`, `frame`, `index`, `block_index`, `source`, `code`,
`typecode`, `key`, `paired_key`, `parent_index`, `flags`, `approx` and `cj`.
Snapshot `index` is the frame number, while `block_index` identifies its primary
or paired block. Identity fields retain uint64 precision.

```matlab
records = readStateFile('state.h5');
frame = readStateFile('state.h5', 'Frame', uint64(3));
trial = readStateFile('diagnostics.h5', 'Source', 1, 'Code', 2);
metadata = readStateFile('state.h5', 'MetadataOnly', true);
```

`Frame`, `Source`, `Key` and `Code` accept selectors. The index is scanned in
512-row blocks; unselected payloads are not loaded. `MetadataOnly` reads no
payload arrays. Matrix cells contain `row`, `col`, `val`, `rows` and `columns`.
Names are UTF-8 cell arrays.

`loadJacState` selects a state, matrix and names associated by frame and key,
using the stored matrix shape. Its docgen entry point delegates to the same
implementation. `readJacFile` selects the last matrix and associated names.
`processCVODEStatefile(path, false, key)` selects a solver key; by default it
selects the first differential key so paired algebraic blocks do not become
extra differential state rows. MATLAB sparse reconstruction can combine
repeated coordinates/drop zero entries; inspect the triplet data when the
stored structure matters.

## Verification

Set `GRIDDYN_MATLAB_STATE_FIXTURES` to an existing directory and run
`PowerflowSystemTests.BinaryState*` and `PowerflowSystemTests.Hdf5*`. These
produce `.h5` fixtures for power flow, appended/periodic DAE state, live
KINSOL/IDA/CVODE/ARKODE diagnostics, matrices/names, paired snapshots, exact
64-bit metadata, special values, empty matrices and chunked compressed data.
Run `EventComponentTests --gtest_filter=MeasurementFiles.Hdf5XmlRecorderCapturesIteratedPowerFlow`
with the same environment setting to add the recorder fixture.
Then add `matlab` and `matlab/tests` to the MATLAB path and run
`testReadStateFile(fixtureDirectory)`.

The C++ tests also exercise malformed/truncated files, mode mismatch, a failed
unpublished frame, selected reads, and contained diagnostic I/O failure.
Iterated power-flow checkpointing is described in [checkpoint and restore](checkpoint-restore.md).
[Recorder HDF5 output](../recorders-and-grabbers.md#saving-and-resetting) also uses this schema.
Other applications in the [integration assessment](hdf5-integration-assessment.md)
remain separate work. LUT integration is deferred and would be last, if pursued.
