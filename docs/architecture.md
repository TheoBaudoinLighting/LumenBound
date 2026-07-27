# Architecture

## M0 boundary

M0 is a certificate engine for finite-dimensional positive, contractive
transport systems. It accepts explicit dense binary64 data and solves
independent systems

```text
x = e + T x
```

and, when every proof precondition is satisfied, encloses their solutions and
the result of a nonnegative projection. The certificate starts after `e`, `T`,
and `P` exist as binary64 arrays. The Cornell demonstration can construct
those arrays from rectangles and a pinhole camera, but that construction is
outside the M0 proof.

This is not a general Galerkin solver. M0 has no weak formulation, trial or
test functions, mass matrix, basis integration, or assembly. Positivity is
expressed directly in the supplied coefficient arrays. If those arrays are
later derived from basis functions, the current proof applies only when the
resulting emission, operator, and projection preserve the nonnegative
coefficient cone.

The architecture keeps the mathematical model independent of command-line
parsing, demonstration construction, file formats, display conversion, and
execution backends. It also keeps candidate estimation separate from
certification. These boundaries are proof boundaries rather than packaging
conventions.

## Modules

Public types are under `include/lumenbound`; implementations are under `src`.
All project code uses the `lumenbound` namespace.

### `math`

`Interval` represents a closed binary64 enclosure. Its certification
operations round lower endpoints downward and upper endpoints upward. It
validates finite endpoints and supplies addition, subtraction, multiplication,
valid division, intersection, containment, width, and a diagnostic midpoint.
The midpoint is never evidence for certification.

`DenseVector` owns finite coefficients in a fixed order. It provides
deterministic arithmetic, an infinity norm, and finite/nonnegative validation.

`DenseMatrix` owns deterministic row-major storage. It validates dimensions
and finite values, performs fixed-order matrix-vector multiplication, computes
a conservative infinity-norm row-sum bound, and supports the small dense
candidate solve. Equal-magnitude pivot candidates are resolved by the lowest
row index.

### Coefficient bands

The transport API stores a runtime-sized sequence of independent coefficient
bands. There is no parallel fixed-size `Spectrum<N>` representation. Band
order is positional and participates in canonical serialization and problem
identity. M0 makes no RGB or color-matching assumption.

The Cornell demonstration alone declares its three bands to be linear-sRGB
display coefficients. That declaration selects a preview mapping; it does not
turn the transport core into a spectral or colorimetric implementation.

### `transport`

`TransportSystem` groups one emission vector and one finite transport operator
per coefficient band. Validation reports distinguish malformed
dimensions, non-finite input, negative emission, negative transport, and a
non-contractive row-sum bound. The system exposes the conservative contraction
upper bound used by the proof. Identical band operators may be supplied, but
they remain explicit values rather than an implicit sharing rule.

### `transport/diffuse_patch_assembly`

The diffuse patch assembler is an experimental scene-to-matrix path used by
the Cornell demonstration. It accepts oriented rectangles with runtime-band
emission and reflectance, derives each front normal from the ordered rectangle
edges, and splits surfaces into constant rectangular patches in surface,
row, then column order. Degenerate geometry, inconsistent band counts,
non-finite coefficients, negative emission, and reflectance outside `[0, 1)`
are rejected before assembly.

Each receiver patch uses a fixed tensor quadrature: two by two midpoint
positions on the patch, eight radial cosine-domain steps, and 32 azimuth
steps. This gives 1024 rays per patch. A ray contributes to the first
front-facing rectangle patch it hits. A miss is recorded as escape; a first
back-face hit blocks the ray without adding a transport term. Exact distance
ties retain the lower surface index because surfaces are visited in canonical
order.

If `N_ij` is the number of rays from receiver `i` assigned to source patch
`j`, the assembler forms

```text
F_ij = N_ij / 1024
T^(b)_ij = rho^(b)_i F_ij.
```

No ray contributes to more than one source, so every form-factor row sum is at
most one. The fixed sample count is a power of two and the Cornell
reflectances are dyadic. The assembled binary64 matrices still pass through
the ordinary M0 positivity and contraction checks; the assembler cannot
bypass them.

This module implements deterministic collocation, not certified visibility or
bounded quadrature. Its ray offsets, intersection tolerances, trigonometric
direction construction, first-hit classification, and rectangle
discretization are ordinary binary64 computations. M0 certifies the resulting
finite arrays, not these construction steps.

The same module builds the Cornell camera projection. It traces a fixed two by
two subpixel grid through a pinhole camera. A front-facing hit is reconstructed
from nearby constant patch coefficients with nonnegative bilinear weights;
misses and first back-face hits contribute black. Sorted duplicate patch
weights are merged before the row is stored.

### `solver`

The candidate solver forms `I - T` and solves each band with deterministic
partial pivoting. It returns `CandidateSolution`, including candidate values,
`residuals`, `residual_infinity_norm`, and pivot diagnostics. Those residual
fields are nearest-rounded diagnostics despite their shorter names. Candidate
arithmetic is not reused as interval evidence. Certification reevaluates the
residual with outward-rounded intervals and stores its norm bound in
`Certificate::residual_upper_bound`.

Candidate failure is reported. There is no fallback to iteration, a lower
precision, or another backend.

### `certification`

The certification module owns:

- runtime proof-precondition checks;
- construction and monotone tightening of coefficient enclosures;
- the outward-rounded candidate-residual bound;
- intersection of the positive enclosure with the residual enclosure;
- independent proof and target statuses with exact reasons;
- iteration stopping based on image bounds returned by the metric module;
- optional retention of full iteration snapshots;
- the machine-readable `Certificate` data model.

The certifier first builds the positive enclosure, computes the residual error
bound `E`, and intersects every coefficient with the outward-rounded interval
`[candidate - E, candidate + E]`. Both operands enclose the same exact finite
solution, so this intersection is a proof step. It is not a candidate-based
guess. Subsequent affine updates are intersected with the prior enclosure.
This keeps lower endpoints nondecreasing and upper endpoints nonincreasing
even when an outward-rounded evaluation repeats an endpoint. An empty
intersection or non-finite arithmetic result is a numerical failure.

The module never upgrades a candidate by numerical accuracy alone. A result
receives `ProofStatus::Certified` only after validation and construction of
all reported proof bounds. Target attainment is then evaluated separately.
`IterationLimit` and `Stagnated` leave the established proof certified.

Full `IterationSnapshot` storage is disabled by default. API callers opt in
with `retain_iteration_snapshots`; otherwise the result retains only final
bounds and `interval_iteration_count`. The command-line demonstration does not
retain snapshots.

### `projection`

`Projection` contains a finite coefficient-to-pixel matrix in ordered
compressed sparse row form. A dense constructor remains available and
converts positive-zero entries to the same sparse representation. Negative
zero and invalid values are retained so validation and canonical hashing can
observe the supplied input. M0 requires every logical entry to be finite and
nonnegative. This permits endpoint projection without sign ambiguity:

```text
P lower <= P exact <= P upper.
```

The module projects both candidate values and interval endpoints. The
certification path derives a maximum absolute error about the candidate for
every raw linear pixel coefficient.

The projection is part of the finite input contract. It is not a validated
camera, reconstruction filter, color transform, or display model.

For the Cornell path, the stored matrix is the finite result of two by two
subpixel sampling and positive bilinear patch reconstruction. That matrix is
certified as `P`. The pinhole model, ray classification, pixel-footprint
approximation, and relationship to a continuous camera are not.

### `certification/image_metrics`

Image error aggregation is separate from the iterative certifier.
`compute_mse_upper_bound` consumes finite nonnegative absolute-error bounds.
`compute_psnr_lower_bound` consumes that MSE bound and a finite positive signal
peak. `compute_image_metric_bounds` remains a convenience composition of the
two; the certifier calls the stages separately so an invalid peak does not
discard an already established MSE bound.

The zero-error result has kind `positive_infinity` and no numeric PSNR value.
A finite result has kind `finite` and a lower-bound value. The JSON encoding
uses `null` for the missing value. The certifier uses this result for target
testing; it does not duplicate the MSE or logarithm calculation.

### `certification/problem_digest`

The digest module hashes a canonical byte stream with SHA-256. Tagged sections
cover ordered emission bands, ordered transport operators, the projection
matrix, and all certification options. Sizes are unsigned 64-bit big-endian
integers. Floating-point values are their exact binary64 bit patterns, also in
big-endian order; signed zero and NaN payloads are retained. The stream begins
with the domain string `lumenbound.problem-digest.v1`.

The resulting `sha256:` value binds a record to the supplied finite problem
and options. It is not a signature and provides no authentication, ownership,
or physical-scene provenance.

### `io`

The I/O layer serializes certificate records, metrics, coefficient tables,
linear pixel tables, and a PPM preview. Certification data is written in a
locale-independent, fixed field order. Each proof-bearing JSON scalar contains
a `max_digits10` decimal number and a 16-digit hexadecimal binary64 bit
pattern. Zero is canonicalized to positive zero. No timestamp, host
identifier, thread-dependent ordering, or transient path is included.
Rejected non-finite peak or target inputs use a null decimal member and an
explicit classification while retaining the submitted binary64 bit pattern.
Early validation failures write the two machine-readable records but do not
manufacture candidate tables or a preview.

Certificate and metric schema v2 records also include
`certificate_scope`, `problem_digest`, `solver_version`,
`arithmetic_policy`, `proof_status`, `proof_failure`, `proof_reason`,
`target_status`, and `target_reason`.

The PPM writer is isolated from certificate construction. The manufactured
demonstration uses a false-color coefficient clamp. The Cornell demonstration
selects the declared-linear-sRGB preview: multiply by a user-visible exposure,
clamp to `[0, 1]`, apply the sRGB opto-electronic transfer function, and
quantize to eight bits. Both mappings are labeled non-certifying. Preview
exposure does not alter raw pixels, `P`, the problem digest, MSE, or PSNR.

### `apps/lumenbound`

The executable parses the supported command, dispatches either the
manufactured problem or the Cornell assembly, calls the library modules,
writes outputs, and maps statuses to process exit codes. Command-line code
contains no transport proof logic.

Unknown options and malformed numeric arguments are explicit errors. A run
also exits nonzero when `TargetStatus` is not `Reached`, even if
`ProofStatus` is `Certified`. The executable does not silently change the
target, iteration budget, precision, solver, or output mapping.

## Data flow

The demonstration follows one fixed sequence:

```text
manufactured finite data
        |
        v
canonical problem digest
        |
        v
dimension, finiteness, positivity, and contraction validation
        |
        +-------------------------------+
        |                               |
        v                               v
deterministic candidate          positive enclosure
dense solve                     from contraction
        |                               |
        v                               |
outward-rounded residual bound          |
and [candidate - E, candidate + E]      |
        |                               |
        +---------------+---------------+
                        |
                        v
            enclosure intersection
                        |
                        v
         optional monotone propagation
                        |
                        v
                coefficient bounds
                   |
                   v
        nonnegative raw-linear projection
                   |
                   v
       pixel bounds and absolute-error bounds
                   |
                   v
          pure image metric aggregation
                   |
                   v
      certificate and deterministic data files
                   |
                   +----> non-certifying preview
```

The Cornell path prepends an explicitly non-certifying assembly stage:

```text
oriented rectangles and declared demo bands
                   |
                   v
fixed patch collocation and first-hit quadrature
                   |
                   +----> e and T as binary64 arrays
                   |
pinhole rays and positive bilinear reconstruction
                   |
                   +----> sparse P as a binary64 matrix
                                |
                                v
                       ordinary M0 data flow
                                |
                                +----> assembly.json
                                |
                                +----> exposure, clamp, sRGB OETF,
                                      and 8-bit non-certifying preview
```

`assembly.json` records the assembly method, quadrature dimensions, declared
band semantics, diagnostics, and the M0 problem digest. It also states that
continuous scene, geometry, visibility, quadrature, discretization, and
display mapping are not certified. It is a boundary record, not an extension
of `certificate.json`.

Validation precedes every computation that relies on a proof assumption. If a
validation stage fails, later stages do not run under weakened assumptions.
The proof failure retains the exact cause. Target evaluation does not replace
that proof result.

## Candidate/certificate separation

The candidate and interval paths answer different questions.

The candidate path estimates the center value for a small dense system.
Gaussian elimination and `CandidateSolution::residuals` are subject to
ordinary floating-point error. Certification evaluates the residual again
with outward-rounded intervals, derives a contraction error bound, and only
then intersects `[candidate - E, candidate + E]` with the independently proven
positive enclosure. The candidate is useful because the residual calculation
turns it into a theorem-backed enclosure. Its nearest-rounded diagnostics
alone remain non-certifying.

Keeping the data structures and call boundaries separate makes accidental
promotion of an estimate visible in review and testing.

## Proof and target state

`ProofStatus` has two values: `Certified` and `Uncertified`. A separate
`ProofFailureCode` identifies invalid dimensions, non-finite input, negative
emission, negative transport, negative projection, non-contraction, or a
numerical failure.

`TargetStatus` has `NotEvaluated`, `Reached`, `IterationLimit`, `Stagnated`,
and `InvalidTarget`. A valid enclosure remains valid when the target is not
reached. This split is reflected in the API, JSON, CSV rows, console summary,
and process-exit mapping.

## Determinism contract

For identical supported inputs and build semantics, M0 fixes:

- row-major matrix storage and traversal;
- left-to-right accumulation order;
- lowest-row tie-breaking in partial pivoting;
- band, coefficient, pixel, and field serialization order;
- canonical problem-digest section and byte ordering;
- interval endpoint rounding direction;
- iteration update and stopping order;
- status precedence when more than one input defect is present;
- locale-independent numeric and JSON formatting.

The Cornell path additionally fixes surface and patch insertion order,
surface-position and angular quadrature order, first-hit traversal, camera
subpixel order, sparse-column sorting, and duplicate-weight reduction order.
Those rules make the assembly repeatable within a supported build. They do
not make its geometric decisions certified.

The certified reference path contains no random or quasi-random sequence,
parallel reduction, learned component, denoiser, temporal state, or hidden
interpolation. Global mutable state is avoided. Floating-point environment
changes used by interval operations are scoped and restored.

Determinism does not by itself imply identical results across compilers or
floating-point libraries. Byte identity is tested for repeated runs within a
supported build configuration. Cross-toolchain equality must be established
before it is claimed.

## Certified build contract

The build applies `/fp:strict` to MSVC and clang-cl frontends. GCC, Clang, and
AppleClang use `-frounding-math`, `-fno-fast-math`, and
`-ffp-contract=off`. Unknown compiler families are rejected at configuration
time. The interval translation unit rejects fast-math macros, requires
`FLT_EVAL_METHOD == 0`, and enables floating-point environment access with
the supported compiler pragma.

The certified core explicitly disables interprocedural optimization for every
configured build type. It also passes `/GL-`, `/clang:-fno-lto`, or
`-fno-lto` as appropriate. This is deliberate. Cross-translation-unit
optimization remains disabled until its treatment of rounding-mode changes,
volatile operands, and operation ordering has separate evidence.

## Backend boundary

M0 has one CPU reference backend using `double`. The observable backend
contract consists of:

- the same input validation and status taxonomy;
- the same mathematical operator and coefficient ordering;
- outward-rounded enclosures that satisfy the same containment tests;
- deterministic behavior under the backend's documented execution model;
- no silent precision, algorithm, device, or CPU fallback.

A future CUDA backend must be selected explicitly and must pass parity and
containment tests against the reference path. Device-specific arithmetic may
produce wider valid bounds, but it may not weaken precondition checks or claim
certification after losing outward rounding. M0 contains no inactive CUDA
implementation that could appear functional.

## Future transport classes

Diffuse, regular glossy, delta/specular-chain, caustic, volumetric, and
sensor-domain transport have different analytical assumptions. Future
milestones give them explicit assemblers and subsolvers. They will not be
folded into the M0 matrix through hidden conditionals that obscure visibility,
quadrature, basis, or contraction obligations.

The current diffuse patch module is enough to render the Cornell
demonstration, but it does not close M1. In particular, the roadmap still
requires the analytic two-patch reference, documented comparison tolerances,
and the remaining M1 tests before diffuse operator assembly reaches that
gate.

Signed Galerkin bases are not a hidden future extension of the positive core.
They require either a positivity-preserving representation or a separately
derived signed-operator certificate.

The measurable gates for those additions are defined in
[the roadmap](roadmap.md).
