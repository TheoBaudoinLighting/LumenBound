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
6. For the supplied positive peak, the actual raw-linear candidate PSNR is no
   smaller than the reported PSNR lower bound, including an explicit
   zero-error representation.

The certificate records the assumptions needed for these statements. It does
not certify that the finite coefficients were assembled from a physical
scene, that they approximate the continuous rendering equation to a stated
tolerance, or that the preview represents a colorimetrically valid display
image.

## Runtime preconditions

Certification requires the implementation to establish all of these
conditions before returning `Certified`:

- vector and matrix dimensions are nonzero and mutually compatible;
- the number and ordering of coefficient bands are valid;
- all emission, transport, projection, peak, target, and iteration-control
  values required by the calculation are finite;
- emission is componentwise nonnegative;
- transport is componentwise nonnegative;
- projection is componentwise nonnegative;
- the supplied signal peak is strictly positive;
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
- residual, error, MSE, and PSNR bounds are evaluated in conservative
  directions;
- the requested stopping condition is established from certified bounds
  within the configured iteration limit.

No condition is inferred from a visually plausible preview or a small
nearest-rounded residual. Failure of a precondition prevents certified status.

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

## Candidate residual

The residual bound is evaluated independently of the candidate solve. The
certificate encloses every component of

```text
r = e + T candidate - candidate
```

and reduces those intervals to an upper bound on `||r||_infinity`. The
candidate-error upper bound then follows from contraction. The certificate
retains both the interval enclosure and the residual result; agreement between
them is diagnostic, while either computation may expose a numerical failure.

## Projection and metrics

Only a nonnegative finite projection is accepted in M0. Each projected
candidate, lower endpoint, upper endpoint, and maximum absolute error is
recorded per raw linear pixel coefficient. Each record also carries the
certificate status; an uncertified aggregate cannot leave a pixel marked
certified.

The MSE bound averages all reported pixel-coefficient error bounds using an
upward-rounded reduction. The pure `compute_image_metric_bounds` function owns
that reduction and the conservative logarithmic PSNR evaluation. The
iterative certifier supplies the ordered error bounds and user-supplied
positive peak. The certificate states the domain as raw linear coefficients.
It does not attach the bound to encoded PPM bytes.

If the MSE upper bound is exactly zero, the result kind is
`positive_infinity` and the numeric PSNR value is null. A finite bound uses
kind `finite` and includes its value. A path that did not establish a PSNR
bound uses `unavailable` with a null value. The record does not serialize
`NaN`, `Infinity`, or another non-JSON token.

## Status and failure behavior

The status enumeration preserves distinct causes:

- `Certified`: all preconditions and requested bounds were established;
- `UncertifiedInvalidDimensions`: one or more shapes or band counts are
  invalid;
- `UncertifiedNonFiniteInput`: an input is NaN or infinite;
- `UncertifiedNegativeEmission`: an emission coefficient is negative;
- `UncertifiedNegativeTransport`: a transport coefficient is negative;
- `UncertifiedNegativeProjection`: a projection coefficient is negative;
- `UncertifiedNonContractive`: the conservative bound is not strictly below
  one;
- `UncertifiedInvalidSignalPeak`: the supplied peak is nonpositive or
  otherwise outside the metric contract;
- `UncertifiedInvalidTarget`: the requested PSNR target is invalid;
- `UncertifiedIterationLimit`: the required enclosure calculation did not
  reach the target before exhausting its budget while at least one endpoint
  could still tighten;
- `UncertifiedTargetNotReached`: an iteration changed no stored endpoint and
  the resulting stagnant enclosure did not establish the requested target;
- `NumericalFailure`: arithmetic could not preserve the documented finite
  enclosure contract.

Each uncertified record contains an exact machine-readable reason in addition
to its status. Validation order is deterministic, so the same malformed input
selects the same primary status. Within the iteration loop, a satisfied target
is checked first, endpoint stagnation second, and budget exhaustion last. The
program returns nonzero when the requested certification is not obtained.

There is no silent substitution of another algorithm, precision, backend,
target, or projection. A valid candidate may be emitted for diagnostics after
a certification failure only if the output identifies it as uncertified and
does not imply that its errors are bounded.

## Machine-readable record

`certificate.json` uses schema version `lumenbound.certificate.v1` and
includes, as applicable:

- status and reason;
- coefficient-band and matrix dimensions;
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

The assumptions list defines the positive finite-system model, nonnegative
projection model, raw-linear metric domain, and required certified-arithmetic
environment. `signal_peak` records the supplied peak. Only `Certified` states
that all listed preconditions were established.

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

`metrics.json` uses schema version `lumenbound.metrics.v1`. It is a compact
deterministic view of the requested target and resulting raw-linear bounds. It
does not supersede the assumptions and status in `certificate.json`.

## What is not certified

M0 does not certify:

- geometry parsing or continuous scene representation;
- form-factor, basis, or transport-operator assembly;
- visibility, occlusion boundaries, or quadrature error;
- discretization error between the finite system and a continuous equation;
- glossy, delta, caustic, or volumetric path classes;
- spectral reconstruction or colorimetric accuracy;
- display transfer functions, clipping, quantization, or the PPM preview;
- CPU/GPU parity, GPU rounding, or production performance.

These exclusions remain in force even when the finite algebraic certificate
is narrow.

## Why the preview is outside the proof

The PPM preview is derived from raw linear candidate coefficients for visual
inspection. It selects the first three bands as display channels, divides each
candidate coefficient by the supplied peak, clamps to `[0, 1]`, multiplies by
`255`, and rounds to an integer. The three selected bands are not asserted to
be colorimetric channels. These operations change both the metric domain and
its error model.

M0 neither bounds those conversion errors nor validates a display or observer
model. The preview therefore has no certification status. The authoritative
values are the raw-linear arrays and bounds in the deterministic data and JSON
records.
