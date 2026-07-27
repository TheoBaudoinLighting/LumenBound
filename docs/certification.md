# Certification contract

## Meaning of `Certified`

For M0, `Certified` means that the emitted record establishes all of the
following for the supplied finite binary64 data:

1. Each validated positive, contractive system `x = e + T x` has a unique
   nonnegative real solution.
2. Every reported coefficient lower and upper endpoint encloses that solution
   componentwise.
3. Every reported raw linear pixel interval encloses the nonnegative
   projection of that solution.
4. The candidate residual norm and candidate infinity-norm error are no larger
   than their reported upper bounds.
5. The actual raw-linear candidate MSE is no larger than the reported MSE
   upper bound.
6. When the supplied peak is finite and positive, the actual raw-linear
   candidate PSNR is no smaller than the reported PSNR lower bound, including
   an explicit zero-error representation.

The certificate records the assumptions needed for these statements. It does
not certify that the finite coefficients were assembled from a physical
scene, that they approximate the continuous rendering equation to a stated
tolerance, or that the preview represents a colorimetrically valid display
image.

This boundary is unchanged for `demo cornell-box`. That command assembles
binary64 `e`, `T`, and `P` from rectangles, fixed ray decisions, and fixed
quadrature before invoking M0. `Certified` applies to those three finite
arrays. It does not certify the Cornell geometry, visibility classifications,
quadrature, patch discretization, continuous pinhole camera, declared
linear-sRGB interpretation, or display conversion.

`Certified` does not mean that the requested PSNR threshold was reached.
`ProofStatus` records the validity of the finite-system bounds.
`TargetStatus` independently records whether the requested stopping condition
was reached, exhausted its iteration budget, stagnated, or was invalid.

## Runtime preconditions

Certification requires the implementation to establish all of these
conditions before returning `Certified`:

- vector and matrix dimensions are nonzero and mutually compatible;
- the number and ordering of coefficient bands are valid;
- all emission, transport, and projection values are finite;
- emission is componentwise nonnegative;
- transport is componentwise nonnegative;
- projection is componentwise nonnegative;
- the certified-arithmetic preflight establishes IEC 559 binary64 semantics,
  working upward and downward rounding modes, and non-flushing subnormal input
  and output;
- the transport infinity norm is accumulated upward to a finite bound
  `q_bar`;
- `0 <= q_bar < 1`;
- the initial upper enclosure is finite;
- every interval operation returns ordered finite endpoints;
- every interval intersection remains nonempty;
- coefficient and pixel enclosure invariants hold through the final
  iteration;
- residual, error, and MSE bounds are evaluated in conservative directions;
- when PSNR is available, its logarithmic bound is evaluated conservatively.

No condition is inferred from a visually plausible preview or a small
nearest-rounded residual. Failure of a precondition prevents certified status.

The Cornell assembler performs additional input checks on rectangles,
materials, quadrature counts, and camera parameters. Those checks make the
demonstration's finite construction well-defined; they do not prove that its
binary64 ray classifications or quadrature enclose a continuous scene.

Target evaluation has its own input checks. A finite target PSNR and a finite,
strictly positive signal peak are required for a finite target comparison.
Failure sets `TargetStatus::InvalidTarget`; it does not erase independently
established coefficient, pixel, residual, or MSE bounds. Reaching the target
within `maximum_iterations` is a command success condition, not a proof
precondition.

## Floating-point model

The CPU reference implementation requires `double` to report IEC 559
semantics with radix two, 53 significand bits, and subnormal support. Input
values are treated as the exact real numbers denoted by their accepted
binary64 encodings.

Before certification, an arithmetic preflight requires the incoming mode to
be round-to-nearest. It changes the active mode to downward and upward in turn
and evaluates a halfway addition whose results must differ in the required
directions, then restores round-to-nearest. It also verifies a
subnormal-producing operation and a subnormal-consuming operation, so an
environment with flush-to-zero or denormals-are-zero behavior cannot pass. The
certificate assumptions state that this preflight passed. Failure returns
`NumericalFailure` before any result receives certified status.

Nearest-rounded arithmetic may be used for the candidate solve because that
path is an estimate. Any operation that can narrow or aggregate a certified
bound uses an interval operation with a downward-rounded lower endpoint and an
upward-rounded upper endpoint. The interval layer scopes and restores any
floating-point environment changes. Compiler options for certified code must
preserve the required rounding semantics and must not reassociate expressions
or contract operations in a way that bypasses them.

CMake applies `/fp:strict` to MSVC and clang-cl frontends. GCC, Clang, and
AppleClang use `-frounding-math`, `-fno-fast-math`, and
`-ffp-contract=off`. A compiler without a defined policy is rejected during
configuration. The interval translation unit rejects `__FAST_MATH__` and
`_M_FP_FAST`, requires `FLT_EVAL_METHOD == 0`, and enables floating-point
environment access with the supported compiler pragma.

Interprocedural optimization is disabled on the certified core for the
generic target and every configured build type. `/GL-`,
`/clang:-fno-lto`, or `-fno-lto` also blocks compiler LTO for that target.
No certificate relies on unvalidated cross-translation-unit reassociation.

The following quantities specifically require conservative evaluation:

- each transport row sum and the maximum row sum;
- `1 - q_bar` and the initial upper endpoint;
- every multiply and add in interval propagation and projection;
- residual subtraction and norm reduction;
- division by `1 - q_bar`;
- candidate-to-pixel endpoint distances;
- squared error accumulation and the MSE mean;
- the logarithmic conversion from MSE upper bound to PSNR lower bound.

The implementation may widen an endpoint beyond the closest representable
bound. It may not return an inward endpoint for convenience. A standard
nearest-rounded `log10` result, even followed by an undocumented tolerance,
does not establish Equation (22).

The logarithm implementation decomposes a positive binary64 argument as
`m * 2^k`, where `1 <= m < 2`, and evaluates the nonnegative atanh series for
`ln(m)`. It uses 24 terms and adds the geometric upper remainder

```text
2 z^49 / (49 (1 - z^2)),    z = (m - 1) / (m + 1).
```

The same bounded series encloses `ln(2)` and `ln(10)`. Interval division
encloses `log10`. The PSNR calculation evaluates
`20 log10(peak) - 10 log10(MSE_upper)` with interval operations and stores the
lower endpoint. It does not form the squared peak or quotient directly. A
nonpositive logarithm argument or any non-finite intermediate is
`NumericalFailure`.

Subnormal values are not silently flushed to zero in certified arithmetic.
Overflow, a non-finite endpoint, loss of the active rounding contract, or an
empty interval is a numerical failure. Clamping is permitted only in the
non-certifying preview mapping.

## Enclosure invariant

The initial interval uses zero as the lower endpoint and an outward-rounded
uniform supersolution as the upper endpoint. Positivity and contraction prove
that it encloses the fixed point.

The certifier then computes the outward-rounded global candidate error bound
`E` and intersects each coefficient with

```text
[
    subtract_down(candidate_i, E),
    add_up(candidate_i, E)
].
```

The residual theorem proves that this second interval contains the same exact
finite solution. Intersecting it with the positive enclosure therefore
preserves inclusion. An empty intersection is a numerical failure.

Each subsequent affine interval evaluation also encloses the fixed point. The
implementation intersects that result with the previous interval:

```text
lower_next = max(lower_previous, evaluated_lower)
upper_next = min(upper_previous, evaluated_upper).
```

This intersection makes the stored lower sequence nondecreasing and the upper
sequence nonincreasing without discarding the solution. Tests check
monotonicity and exact manufactured-solution containment at every iteration.
The stopping decision uses the stored outward-rounded endpoints, not an
asymptotic estimate based only on `q_bar`.

`interval_iteration_count` counts only subsequent affine propagation. Initial
positive-enclosure construction and residual intersection are proof steps but
not interval iterations. A zero-iteration certificate is valid when the
intersected enclosure already establishes the requested target.

Full `IterationSnapshot` retention is optional and disabled by default. API
callers request it with `retain_iteration_snapshots`; the command-line
demonstration leaves it off.

## Candidate residual

The residual bound is evaluated independently of the candidate solve. The
certificate encloses every component of

```text
r = e + T candidate - candidate
```

and reduces those intervals to an upper bound on `||r||_infinity`. The
candidate-error upper bound then follows from contraction. The certificate
uses that bound to construct `[candidate - E, candidate + E]` and intersects
it with the positive enclosure. The nearest-rounded
`CandidateSolution::residuals` fields remain diagnostics and do not enter the
proof.

## Projection and metrics

Only a nonnegative finite projection is accepted in M0. Each projected
candidate, lower endpoint, upper endpoint, and maximum absolute error is
recorded per raw linear pixel coefficient. Each record also carries the proof
status; an uncertified proof cannot leave a pixel marked certified.

`Projection` stores sorted sparse rows. Omitted entries are canonical positive
zero; the canonical problem digest expands the logical matrix in row-major
order, so sparse and dense construction of the same binary64 `P` have the same
identity. Projection evaluation visits stored columns in increasing order and
uses outward arithmetic for interval endpoints.

The Cornell `P` is assembled from two by two pinhole-camera samples per pixel.
Each front-facing hit uses nonnegative bilinear reconstruction over nearby
constant patches; misses and first back-face hits add zero. M0 certifies the
resulting finite nonnegative matrix. It does not certify the rays,
reconstruction as an approximation of a continuous image, antialiasing error,
or the camera model.

The MSE bound averages all reported pixel-coefficient error bounds using an
upward-rounded reduction in `compute_mse_upper_bound`.
`compute_psnr_lower_bound` separately converts that established MSE bound and
a positive peak into a conservative PSNR lower bound.
`compute_image_metric_bounds` is a convenience composition. The certificate
states the domain as raw linear coefficients. It does not attach the bound to
encoded PPM bytes.

If the MSE upper bound is exactly zero, the result kind is
`positive_infinity` and the numeric PSNR value is null. A finite bound uses
kind `finite` and includes its value. A path that did not establish a PSNR
bound uses `unavailable` with a null value. The record does not serialize
`NaN`, `Infinity`, or another non-JSON token.

## Proof and target behavior

Proof validity has two states:

- `ProofStatus::Certified`: the finite-system bounds were established;
- `ProofStatus::Uncertified`: no proof claim is made for unavailable bounds.

`ProofFailureCode` retains the exact class of failure:

- `None`;
- `InvalidDimensions`;
- `NonFiniteInput`;
- `NegativeEmission`;
- `NegativeTransport`;
- `NegativeProjection`;
- `NonContractive`;
- `NumericalFailure`.

The separate target states are:

- `NotEvaluated`: no target conclusion is available because the proof was not
  established;
- `Reached`: the certified PSNR lower bound meets the requested threshold;
- `IterationLimit`: valid bounds exist, but the configured propagation budget
  ended before the target was reached;
- `Stagnated`: valid bounds exist, no endpoint changed, and the target remains
  unmet;
- `InvalidTarget`: the peak or target is outside the target-evaluation
  contract.

Every state has a deterministic machine-readable reason. Within the iteration
loop, a satisfied target is checked first, endpoint stagnation second, and
budget exhaustion last. The command returns nonzero unless the proof is
`Certified` and the target is `Reached`. A nonzero command result does not
change `Certified` to `Uncertified`.

There is no silent substitution of another algorithm, precision, backend,
target, or projection. A valid candidate may be emitted for diagnostics after
a proof failure only if the output identifies it as uncertified and does not
imply that its errors are bounded.

## Machine-readable record

`certificate.json` uses schema version `lumenbound.certificate.v2` and
includes, as applicable:

- certificate scope, problem digest, solver version, and arithmetic policy;
- proof status, failure code, and reason;
- target status and reason;
- coefficient-band and matrix dimensions;
- maximum iterations and snapshot-retention option;
- contraction upper bound;
- interval iteration count and stopping criterion;
- coefficient candidate values and bounds;
- raw linear pixel candidate values, bounds, and error bounds;
- residual upper bound;
- candidate error upper bound;
- MSE upper bound;
- PSNR lower-bound kind and optional finite value;
- signal peak;
- explicit assumptions and output domain.

The fixed scope is
`finite_dimensional_positive_binary64_transport`. The arithmetic policy is
`binary64-outward-rounded-v1`; `solver_version` records the project version
compiled into the core. The assumptions list defines the positive
finite-system model, nonnegative projection model, raw-linear metric domain,
residual intersection, false-color preview boundary, and required arithmetic
environment. Only `proof_status: Certified` states that the proof
preconditions were established.

`problem_digest` is a SHA-256 digest of a canonical tagged byte stream. It
covers every ordered emission vector, every ordered transport matrix, the
projection, signal peak, target PSNR, maximum iterations, and the
snapshot-retention flag. Dimensions and exact binary64 bits are included.
Signed zero and NaN payloads remain distinct inputs. The digest associates the
record with that canonical problem; it is not a digital signature and does
not authenticate the producer or the physical meaning of the data.

For the Cornell demonstration, this digest binds the certificate to the
assembled finite `e`, `T`, and logical `P`. It does not hash or certify the
source rectangles, ray-intersection tolerances, visibility decisions,
quadrature construction, declared band semantics, or preview exposure.
`assembly.json` records those demonstration choices and repeats the finite
problem digest so the files can be associated. That record is not a proof of
the assembly.

Fields have fixed ordering. Every proof-bearing scalar has a `decimal` member
written with `max_digits10` precision and a `binary64` member containing the
canonical 16-digit hexadecimal bit pattern. Zero is canonicalized to positive
zero. A rejected non-finite peak or target has a null decimal member and an
explicit `nan`, `positive_infinity`, or `negative_infinity` classification;
its binary64 member still records the submitted bits. The record has no
timestamp. Unknown future schema versions must not be
interpreted as the current contract without version-aware validation.
Proof quantities that were not established before a failure are encoded as
null rather than as numeric zero.

`metrics.json` uses schema version `lumenbound.metrics.v2`. It is a compact
deterministic view of the requested target and resulting raw-linear bounds. It
does not supersede the assumptions and statuses in `certificate.json`.

## Cornell assembly boundary

`transport/diffuse_patch_assembly` divides oriented rectangles into constant
patches in fixed order. For each receiver it traces 1024 quadrature rays:
two by two midpoint positions times eight radial and 32 azimuth directions in
the cosine hemisphere. A ray is assigned to its first front-facing patch or
to no transport term when it escapes or first meets a back face. The finite
coefficients are

```text
F_ij = hit_count_ij / 1024
T^(b)_ij = rho^(b)_i F_ij.
```

This construction is positive and assigns each ray at most once. The
certifier still checks the actual binary64 `T` and its outward-evaluated
contraction bound. It does not accept the assembly argument as a replacement
for runtime validation.

The ray-origin offset, rectangle intersection tolerance, first-hit result,
cosine-direction evaluation, and omitted integration remainder are not
interval quantities. The assembly record calls this path
`DeterministicUnbounded` for that reason. Determinism is useful for regression;
it is not a visibility or quadrature certificate.

## What is not certified

M0 does not certify:

- geometry parsing or continuous scene representation;
- weak formulation, trial or test functions, mass matrices, or general
  Galerkin assembly;
- form-factor, basis, or transport-operator assembly;
- visibility, occlusion boundaries, or quadrature error;
- discretization error between the finite system and a continuous equation;
- glossy, delta, caustic, or volumetric path classes;
- spectral reconstruction or colorimetric accuracy;
- display transfer functions, clipping, quantization, or the PPM preview;
- CPU/GPU parity, GPU rounding, or production performance.

These exclusions remain in force even when the finite algebraic certificate
is narrow.

The requirement `e >= 0`, `T >= 0`, and `P >= 0` means that any future basis
used without changing the proof must preserve a positive coefficient
representation. General signed Galerkin bases and matrices require a separate
certificate derivation.

## Why the preview is outside the proof

The manufactured PPM is a false-color coefficient preview. It assigns the
first three bands to file channels, divides by the supplied peak, clamps to
`[0, 1]`, and quantizes to eight bits. Those bands have no RGB assertion.

The Cornell scene separately declares its three bands as linear-sRGB demo
coefficients. Its preview multiplies the candidate by an explicit exposure,
clamps to `[0, 1]`, applies the sRGB opto-electronic transfer function, and
quantizes to eight bits. Preview exposure changes no proof-bearing output.
The declaration does not establish spectral integration, standard-observer
colorimetry, calibrated primaries, or the physical accuracy of the material
coefficients.

M0 bounds neither preview conversion. It does not propagate raw pixel
intervals through exposure, clamp, the transfer function, or quantization.
Both PPM modes therefore have no certification status. The authoritative
values are the raw-linear arrays and bounds in the deterministic data and JSON
records.
