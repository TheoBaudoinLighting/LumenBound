# Architecture

## M0 boundary

M0 is a finite-dimensional positive transport core. It accepts explicit dense
binary64 data, solves independent systems

```text
x = e + T x
```

and, when every proof precondition is satisfied, encloses their solutions and
the result of a nonnegative projection. It does not construct `e`, `T`, or the
projection from scene geometry.

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

### `spectrum`

`Spectrum<N>` provides fixed-count independent coefficient arithmetic. The
transport API stores a runtime sequence of coefficient bands in a stable
order. M0 makes no RGB or color-matching assumption. Serialization preserves
the stored order.

### `transport`

`TransportSystem` groups one emission vector and one finite transport operator
per coefficient band. Validation reports distinguish malformed
dimensions, non-finite input, negative emission, negative transport, and a
non-contractive row-sum bound. The system exposes the conservative contraction
upper bound used by the proof. Identical band operators may be supplied, but
they remain explicit values rather than an implicit sharing rule.

### `solver`

The candidate solver forms `I - T` and solves each band with deterministic
partial pivoting. It returns `CandidateSolution`, including candidate values,
`nearest_residuals`, `nearest_residual_infinity_norm`, and pivot diagnostics.
Those residual fields are nearest-rounded diagnostics. Candidate arithmetic is
not reused as interval evidence. The independently evaluated certified
residual appears only as `Certificate::residual_upper_bound`.

Candidate failure is reported. There is no fallback to iteration, a lower
precision, or another backend.

### `certification`

The certification module owns:

- runtime proof-precondition checks;
- construction and monotone tightening of coefficient enclosures;
- the outward-rounded candidate-residual bound;
- explicit certificate status and failure reason;
- iteration stopping based on image bounds returned by the metric module;
- the machine-readable `Certificate` data model.

An update is intersected with the prior enclosure. This retains both valid
enclosures while making lower endpoints nondecreasing and upper endpoints
nonincreasing even when an outward-rounded evaluation repeats an endpoint.
An empty intersection or non-finite arithmetic result is a numerical failure.

The module never upgrades a candidate to certified status. A result receives
`Certified` only after validation and completion of all requested proof
calculations.

### `projection`

`Projection` contains a finite coefficient-to-pixel matrix. M0 requires every
entry to be finite and nonnegative. This permits endpoint projection without
sign ambiguity:

```text
P lower <= P exact <= P upper.
```

The module projects both candidate values and interval endpoints. The
certification path derives a maximum absolute error about the candidate for
every raw linear pixel coefficient.

The projection is part of the finite input contract. It is not a validated
camera, reconstruction filter, color transform, or display model.

### `certification/image_metrics`

Image error aggregation is separate from the iterative certifier.
`compute_image_metric_bounds` is a pure function of a span of finite
nonnegative absolute-error upper bounds and a finite positive signal peak. It
returns an upward-rounded MSE bound and either a `finite` or
`positive_infinity` PSNR-bound kind. `unavailable` is the explicit certificate
state when metric evaluation did not produce a result.

The zero-error result has kind `positive_infinity` and no numeric PSNR value.
A finite result has kind `finite` and a lower-bound value. The JSON encoding
uses `null` for the missing value. The certifier uses this result for target
testing; it does not duplicate the MSE or logarithm calculation.

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

The PPM writer is isolated from certificate construction. Its display mapping
may clip, quantize, or apply a transfer function, so the preview carries no
proof status.

### `apps/lumenbound`

The executable parses the supported command, constructs the deterministic
manufactured problem, calls the library modules, writes outputs, and maps
statuses to process exit codes. Command-line code contains no transport proof
logic.

Unknown options, malformed numeric arguments, a nonpositive peak, and an
unreachable target are explicit errors. The executable does not silently
change the target, iteration budget, precision, solver, or output mapping.

## Data flow

The demonstration follows one fixed sequence:

```text
manufactured finite data
        |
        v
dimension, finiteness, positivity, and contraction validation
        |
        +----------------------+
        |                      |
        v                      v
deterministic candidate     initial interval enclosure
dense solve                and monotone propagation
        |                      |
        v                      v
nearest residual           coefficient bounds
(diagnostic)
        |                      |
        +----------+-----------+
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

Validation precedes every computation that relies on a proof assumption. If a
validation stage fails, later stages do not run under weakened assumptions.
The resulting status retains the exact cause.

## Candidate/certificate separation

The candidate and interval paths answer different questions.

The candidate path estimates the center value for a small dense system.
Gaussian elimination and `nearest_residuals` are subject to ordinary
floating-point error. Certification evaluates the residual again with
outward-rounded intervals and stores only its norm bound in
`Certificate::residual_upper_bound`.

The interval path proves inclusion. It starts without using the candidate and
maintains a componentwise lower and upper solution bound. Candidate values may
guide reporting or a future refinement policy, but they cannot replace a
proof step, narrow an endpoint without intersection with a proven enclosure,
or determine certified status.

Keeping the data structures and call boundaries separate makes accidental
promotion of an estimate visible in review and testing.

## Determinism contract

For identical supported inputs and build semantics, M0 fixes:

- row-major matrix storage and traversal;
- left-to-right accumulation order;
- lowest-row tie-breaking in partial pivoting;
- band, coefficient, pixel, and field serialization order;
- interval endpoint rounding direction;
- iteration update and stopping order;
- status precedence when more than one input defect is present;
- locale-independent numeric and JSON formatting.

The certified reference path contains no random or quasi-random sequence,
parallel reduction, learned component, denoiser, temporal state, or hidden
interpolation. Global mutable state is avoided. Floating-point environment
changes used by interval operations are scoped and restored.

Determinism does not by itself imply identical results across compilers or
floating-point libraries. Byte identity is tested for repeated runs within a
supported build configuration. Cross-toolchain equality must be established
before it is claimed.

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

The measurable gates for those additions are defined in
[the roadmap](roadmap.md).
