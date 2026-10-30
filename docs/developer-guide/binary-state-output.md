# Binary solver-state output

The [architecture review](binary-state-architecture-review.md) records the
remaining work for complete partitioned captures, optimization integration,
shared buffering/readers and future parameter/checkpoint manifests. The format
below describes the current implementation, not that proposed extension.

`saveStateBinary(sim, path, mode, append)` writes a compact snapshot of an
initialized solver's state vector. Differential modes also include a derivative
vector. With `append=true`, snapshots are appended to a compatible state file;
`append=false` replaces the file. `readStateBinary(path)` reads the last
snapshot and exposes its recorded time, solver mode and vectors.
`loadStateBinary(sim, path, mode)` copies the last snapshot's vectors into an
initialized solver with a matching mode and state size.

This is solver-state output, not a simulation checkpoint. Loading does not
rewind the simulation clock, apply component state, restore events or solver
integration history, or rebuild an operating point. The caller must use a
compatible model and state layout. The stored solver offset is informational;
loading matches the mode properties and vector size, not the offset number.

The version 1 file starts with the eight ASCII bytes `GDSTATE1` and contains
one or more records. Snapshots, solver diagnostic vectors, sparse Jacobians,
and state names use the same format. Integers and IEEE-754 binary64 values use
little-endian byte order. Each record contains, in order:

| Field | Encoding |
| --- | --- |
| Record marker | 32-bit unsigned integer `0x31544153` |
| Source | 32-bit unsigned integer: 0 for a saved snapshot, 1 for diagnostics |
| Record type | 32-bit unsigned integer: 0 snapshot, 1 vector, 2 sparse matrix, 3 names |
| Data code | 32-bit unsigned integer: 0 state, 1 derivative, 2 residual; matrix code 1 is Jacobian |
| Index | 32-bit unsigned integer: diagnostic iteration index, or 0 for a snapshot |
| Solver mode flags | 32-bit unsigned integer; bits 0–5 are dynamic, differential, algebraic, local, extended state and parameters |
| Solver approximations | 32-bit unsigned bit mask |
| Solver offset index | 32-bit unsigned integer |
| Simulation time | 64-bit signed nanoseconds |
| Length | 64-bit unsigned integer: number of values or matrix entries, or byte count for names |
| Payload | Snapshot: state values followed by derivatives if differential; vector: binary64 values; matrix: `(uint32 row, uint32 column, binary64 value)` entries; names: a `uint32` count followed by `(uint32 byte length, UTF-8 bytes)` entries |
| End marker | 32-bit unsigned integer `0x31444e45` |

The reader rejects malformed or truncated records and unsupported versions.
Appending rejects a file without the version 1 header or a complete final
record trailer. Ordinary snapshots omit checksums, stable component identities,
state names and model parameters. Named records are currently emitted by the
explicit Jacobian capture helpers; they are optional format content.

MATLAB can read all record kinds with `matlab/readStateFile.m`. It returns the
same vector-oriented `time`, `index`, `code`, `typecode`, `key`, and `data`
fields used by the existing MATLAB scripts, plus the exact `time_ns`, solver
`flags`, `approx`, and `source` metadata. State and derivative vectors have
codes 0 and 1 respectively; `index` is the zero-based snapshot number for
snapshots or the solver's iteration index for diagnostics. MATLAB exposes
matrices as zero-based row and column arrays plus values, and names as a cell
array. `matlab/loadJacState.m` reads a state and Jacobian capture into a state
vector, sparse MATLAB matrix, and optional names; `matlab/readJacFile.m` reads
the last Jacobian record and any state names.

`readStateBinary` finds the last snapshot in a file and skips diagnostic
records; it rejects a file containing diagnostics only. Diagnostic trial states
are not implicitly treated as restorable snapshots.

To exercise the MATLAB reader against files produced by GridDyn, set
`GRIDDYN_MATLAB_STATE_FIXTURES` to an existing writable directory and run the
`PowerflowSystemTests.BinaryState*` tests. Then add `matlab` and `matlab/tests`
to the MATLAB or Octave path and call `testReadStateFile` with that directory.
