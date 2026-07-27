# Mathematical model

## Finite transport equation

M0 considers `B` independent coefficient bands. For each band
`b in {1, ..., B}`, let

- `x^(b) in R^n` be the unknown radiance coefficient vector;
- `e^(b) in R^n` be the supplied emitted-radiance coefficient vector;
- `T^(b) in R^(n x n)` be the supplied finite transport matrix.

The algebraic transport equation is

```text
x^(b) = e^(b) + T^(b) x^(b).                     (1)
```

Each band has an explicit operator. Operators may have identical values, but
no sharing is assumed. No coefficient band is assumed to be red, green, or
blue. Bands are stored in one runtime-sized positional sequence; there is no
second fixed-size spectral type.

Equation (1) is a supplied finite algebraic system at the M0 proof boundary.
The Cornell demonstration can assemble one such system from rectangles, but
M0 does not derive or bound that assembly. There is no weak formulation,
trial or test function, mass matrix, or certified geometric quadrature. The
implementation is therefore not a general Galerkin solver. If coefficients
are interpreted as basis weights, the current proof requires that
representation and its assembly to preserve the componentwise nonnegative
cone in Equation (2) and the nonnegative projection condition below. Signed
bases are not covered.

For a vector `v`, the infinity norm is

```text
||v||_infinity = max_i |v_i|.
```

For a matrix `A`, the induced infinity norm is

```text
||A||_infinity = max_i sum_j |A_ij|.
```

M0 requires componentwise positivity:

```text
e^(b)_i >= 0
T^(b)_ij  >= 0.                                  (2)
```

It computes one conservative floating-point upper bound `q_bar` such that

```text
q_bar >= max_b ||T^(b)||_infinity
0 <= q_bar < 1.                                  (3)
```

Because each `T^(b)` is nonnegative, its infinity norm is its maximum row sum.

For each band, Equation (3) makes the affine map
`F^(b)(z) = e^(b) + T^(b)z` a contraction in the infinity norm. Therefore
Equation (1) has one finite solution:

```text
x^(b) = (I - T^(b))^(-1) e^(b)
      = sum_(k=0)^infinity (T^(b))^k e^(b).      (4)
```

Every term in the Neumann series is nonnegative. Thus the solution is
componentwise nonnegative. These statements apply to the supplied finite
system; they do not establish how accurately the system approximates a
continuous light-transport problem.

## Cornell diffuse collocation

The Cornell demonstration constructs `e` and `T` with a deterministic diffuse
patch collocation. This construction explains the finite matrix, but it is not
part of the certificate derivation.

Each oriented rectangle is split into constant rectangular patches. For a
receiver patch `i`, the assembler evaluates four midpoint positions arranged
as a two by two grid. At each position it evaluates a cosine-domain tensor
grid with eight radial indices and 32 azimuth indices. For radial index `a`
and azimuth index `k`, define

```text
u_a   = (a + 1/2) / 8
phi_k = 2 pi (k + 1/2) / 32.
```

With receiver tangent `t_i`, bitangent `s_i`, and front normal `n_i`, the ray
direction is

```text
omega_(a,k) =
    sqrt(u_a) cos(phi_k) t_i
  + sqrt(u_a) sin(phi_k) s_i
  + sqrt(1 - u_a) n_i.                          (4a)
```

The implementation normalizes the constructed binary64 direction before
tracing it. Four positions times 256 directions give

```text
N = 2 * 2 * 8 * 32 = 1024
```

rays per receiver patch.

Each ray has one of three outcomes:

1. its first rectangle hit is front-facing and belongs to source patch `j`;
2. it escapes through the open scene;
3. its first hit is a back face, which blocks the ray without contributing
   energy.

Let `N_ij` be the count of the first outcome for source `j`. The finite
collocation coefficient is

```text
F_ij = N_ij / N.                                (4b)
```

No ray is assigned twice, hence

```text
F_ij >= 0
sum_j F_ij <= 1.                                (4c)
```

For patch reflectance `rho_i^(b)` and emitted coefficient `e_i^(b)`, the
Cornell operator is

```text
T_ij^(b) = rho_i^(b) F_ij.                      (4d)
```

The assembler requires finite nonnegative emission and
`0 <= rho_i^(b) < 1`. In the intended real-valued collocation this gives a
nonnegative row sum no larger than `rho_i^(b)`. Certification does not rely on
that construction argument alone: it evaluates the actual assembled
binary64 matrix and rejects it unless the conservative bound in Equation (3)
is below one.

The `F_ij` values are fixed quadrature estimates. They are not interval
enclosures of continuous form factors and are not required to satisfy
reciprocity. Ray-origin offsets, first-hit decisions, trigonometric direction
construction, patch discretization, and missed quadrature remainder are not
bounded. A narrow enclosure for Equation (1) says nothing about those errors.

## Candidate solve

The candidate path rewrites Equation (1) as

```text
(I - T^(b)) c^(b) = e^(b)                        (5)
```

and solves the small dense system in `double` with deterministic partial
pivoting. Pivot candidates are compared by magnitude; equal candidates select
the lowest row index. Arithmetic and traversal order are fixed.

The resulting `c` is an estimate. Ordinary floating-point Gaussian elimination
does not establish a componentwise enclosure, even when the observed residual
is small. Candidate values therefore have no certification status by
themselves.

## Initial componentwise enclosure

Fix one band `b`. The iteration symbols `lower_k`, `upper_k`, and `x` below
refer to that band when the superscript is omitted.

Let

```text
e_max^(b) = max_i e^(b)_i
U^(b)     = e_max^(b) / (1 - q_bar).             (6)
```

All quantities used to construct the implemented `U` are rounded so that the
stored value is an upper bound on the real expression. Define

```text
lower_0^(b) = 0
upper_0^(b) = U^(b) 1,                           (7)
```

where `1` is the vector of ones.

The lower bound follows from `x >= 0`. For every row `i`,

```text
(e^(b) + T^(b) upper_0^(b))_i
    <= e_max^(b) + ||T^(b)||_infinity U^(b)
    <= e_max^(b) + q_bar U^(b)
    = U^(b).
```

Thus `upper_0` is a supersolution. Monotonicity of `F` and the fixed-point
equation imply

```text
lower_0^(b) <= x^(b) <= upper_0^(b).             (8)
```

If the denominator in Equation (6) cannot be proven positive, if construction
overflows, or if an endpoint is non-finite, the initial enclosure is not
available and the result is not certified.

## Monotone interval propagation

Before propagation, the implementation intersects the enclosure in Equation
(8) with the residual enclosure derived in Equations (15a) and (15b) below.
For this section, `lower_0` and `upper_0` denote those refined endpoints. They
still satisfy Equation (8).

Assume at iteration `k` that

```text
lower_k <= x <= upper_k.
```

Positivity of the band operator makes `F^(b)` monotone, so in exact arithmetic

```text
F^(b)(lower_k) <= F^(b)(x) = x
    <= F^(b)(upper_k).                           (9)
```

The implementation evaluates the left side downward and the right side
upward. Denote those endpoint vectors by `F_down^(b)(lower_k)` and
`F_up^(b)(upper_k)`. It then intersects the new enclosure with the previous
one:

```text
lower_(k+1) = max(lower_k, F_down^(b)(lower_k))
upper_(k+1) = min(upper_k, F_up^(b)(upper_k)),    (10)
```

with componentwise `max` and `min`. Both operands on each side are proven
bounds, so intersection preserves inclusion. It also makes the stored
sequences monotone despite outward-rounding slack:

```text
lower_(k+1) >= lower_k
upper_(k+1) <= upper_k
lower_(k+1) <= x <= upper_(k+1).                 (11)
```

In exact arithmetic, the distance between propagated endpoints satisfies

```text
||F^(b)(upper_k) - F^(b)(lower_k)||_infinity
    <= ||T^(b)||_infinity
       ||upper_k - lower_k||_infinity
    <= q_bar ||upper_k - lower_k||_infinity.     (12)
```

Finite-precision endpoint expansion can eventually limit observed width
reduction. The implementation must not infer target attainment from the
expected factor in Equation (12); it checks the actual outward-rounded bounds.
If it cannot reach the requested criterion within the iteration budget, the
target status becomes `IterationLimit`. If no endpoint changes first, it
becomes `Stagnated`. Neither state invalidates the enclosure already
established by the proof.

## Independent residual bound

For a finite candidate `c`, define its residual using the sign convention

```text
r^(b) = e^(b) + T^(b) c^(b) - c^(b).             (13)
```

Subtracting Equation (1) gives

```text
r^(b) = (I - T^(b))(x^(b) - c^(b)).
```

The Neumann-series bound in Equation (4) implies

```text
||(I - T^(b))^(-1)||_infinity
    <= sum_(k=0)^infinity ||T^(b)||_infinity^k
    <= 1 / (1 - q_bar).
```

Consequently,

```text
||c^(b) - x^(b)||_infinity
    <= ||r^(b)||_infinity / (1 - q_bar).          (14)
```

Certification does not insert a nearest-rounded residual into Equation (14).
It computes an interval `R_i^(b)` enclosing each residual component. Write its
endpoints as `R_i^(b,lower)` and `R_i^(b,upper)`. A conservative residual norm
across all bands is

```text
rho = max_b max_i max(
          |R_i^(b,lower)|,
          |R_i^(b,upper)|).
```

The reported candidate-error upper bound is an upward-rounded enclosure of

```text
epsilon_candidate = rho / (1 - q_bar).           (15)
```

Let the stored upper bound in Equation (15) be `E`. For every band and
coefficient, outward-rounded subtraction and addition give

```text
C_i^(b) = [
    subtract_down(c_i^(b), E),
    add_up(c_i^(b), E)
].
                                                       (15a)
```

The infinity-norm result proves `x_i^(b) in C_i^(b)`. The implementation
therefore intersects this residual enclosure with the positive enclosure:

```text
lower_0,i^(b) = max(0, C_i^(b,lower))
upper_0,i^(b) = min(U^(b), C_i^(b,upper)).        (15b)
```

Both intervals contain the same exact finite solution, so their nonempty
intersection also contains it. The candidate does not establish this
intersection by proximity; the outward residual evaluation and contraction
bound do. Fixed-point propagation in Equation (10) starts only after this
step. `interval_iteration_count` counts those propagation updates, not
construction of the two initial enclosures. A certified result with zero
interval iterations is therefore expected when the residual intersection
already proves the requested image bound.

## Nonnegative projection

Let `P in R^(m x n)` map transport coefficients to `m` raw linear pixel
coefficients:

```text
y^(b) = P x^(b).                                 (16)
```

M0 requires

```text
P_pi >= 0
```

and finite, compatible dimensions. For a coefficient enclosure,
nonnegativity gives

```text
P lower^(b) <= y^(b) <= P upper^(b).             (17)
```

Each endpoint in Equation (17) is evaluated outward in fixed row-major order.
The projected candidate is

```text
y_candidate^(b) = P c^(b).
```

For projected interval `[L_j, U_j]` and candidate value `a_j`, where `j`
indexes every pixel-band pair, define

```text
delta_j = max(|a_j - L_j|, |U_j - a_j|).         (18)
```

With outward-rounded subtraction and maximum operations, `delta_j` bounds
`|a_j - y_j|` even if the candidate lies outside the interval.

### Cornell camera projection

The Cornell camera constructs a particular finite `P` with a two by two
subpixel grid. For subpixel sample `s`, a miss or first back-face hit
contributes zero. A front-facing hit at rectangle coordinates `(u, v)` is
reconstructed from the neighboring constant patch coefficients with
bilinear weights `w_sj`. Boundary neighbors are folded onto the nearest valid
patch cell. In real arithmetic, the ideal bilinear weights are nonnegative
and satisfy

```text
sum_j w_sj = 1
```

for a visible sample. The pixel row is

```text
P_pj = (1 / 4) sum_(s=1)^4 w_sj.               (18a)
```

The stored binary64 weights can differ from that ideal sum by a few ulps.
Rows are stored sparsely with sorted unique column indices. Certification
validates the actual stored `P_pj >= 0`; it does not replace that matrix with
the ideal real-valued weights. The generic certification path treats omitted
entries as positive zero and applies the same outward projection proof in
Equation (17).

This establishes positivity of the finite reconstruction matrix. It does not
bound pinhole-camera approximation, pixel-footprint quadrature, ray
intersection, visibility, aliasing against the continuous scene, or a sensor
response.

## Image MSE upper bound

Let `M = m B` be the number of raw linear pixel coefficients being compared.
The actual candidate error against the exact solution of the finite system is

```text
MSE = (1 / M) sum_(j=1)^M (a_j - y_j)^2.         (19)
```

Equation (18) yields

```text
MSE
    <= (1 / M) sum_(j=1)^M delta_j^2
    = MSE_upper.                                 (20)
```

Squaring, summation, and division contributing to the stored `MSE_upper` are
rounded upward. The metric concerns raw linear coefficients only. It excludes
preview transfer functions, clipping, quantization, display conversion, and
any unsupplied physical image.

The implementation passes the ordered set of `delta_j` values to
`compute_mse_upper_bound`. It then passes that result and `peak` to
`compute_psnr_lower_bound`. `compute_image_metric_bounds` is a convenience
composition of the same two functions. The iterative certifier does not
duplicate either calculation.

## PSNR lower bound

For a finite supplied signal peak `peak > 0` and positive MSE, define

```text
PSNR = 10 log10(peak^2 / MSE).                   (21)
```

The expression is monotonically decreasing in MSE. Therefore Equation (20)
implies

```text
PSNR
    >= 10 log10(peak^2 / MSE_upper)
    = PSNR_lower.                                (22)
```

The reference logarithm does not use a library `log10` result as a bound. For
positive `v`, it writes

```text
v = m 2^k,    1 <= m < 2
z = (m - 1) / (m + 1),    0 <= z <= 1/3
ln(v) = k ln(2)
      + 2 sum_(n=0)^infinity z^(2n+1) / (2n+1). (23)
```

The binary decomposition is exact for a finite positive binary64 value. The
implementation evaluates 24 nonnegative series terms with interval
arithmetic. If `N = 24`, the omitted tail satisfies

```text
0 <= tail
  <= 2 z^(2N+1) / ((2N+1)(1-z^2)).               (24)
```

The same series with `z = 1/3` encloses `ln(2)`. Decomposing `10` by the same
method encloses `ln(10)`, and interval division gives an enclosure of
`log10(v)`.

For positive `MSE_upper`, the implementation avoids forming `peak^2` or its
quotient directly. It evaluates the equivalent expression

```text
20 log10(peak) - 10 log10(MSE_upper)              (25)
```

with interval operations and stores the lower endpoint. This form avoids
avoidable overflow or underflow in the squared peak and quotient. A
nonpositive logarithm argument, a non-finite endpoint, or failure of a
directed operation produces `NumericalFailure`; it does not produce a PSNR
bound.

If `MSE_upper` is exactly zero, every `delta_j` is zero and the finite-system
candidate error is exactly zero under the established enclosures. The
mathematical PSNR lower bound is then positive infinity. The certificate
represents this with kind `positive_infinity` and a null numeric value. A
finite bound uses kind `finite` and carries its value. A failed calculation
uses `unavailable`; it is never represented by a non-standard infinite JSON
number.

The signal peak is an input to the metric, not a measured or inferred property
of the scene. A certificate always records the supplied value and the
raw-linear domain to which it applies.

## Proof validity and target attainment

Proof state and stopping state answer different questions.

`ProofStatus::Certified` means that validation succeeded and the reported
coefficient, projection, residual, MSE, and any available PSNR bounds satisfy
this finite-system derivation. `ProofStatus::Uncertified` is accompanied by a
`ProofFailureCode` and exact reason.

`TargetStatus` reports whether the requested PSNR threshold was reached. Its
values are `NotEvaluated`, `Reached`, `IterationLimit`, `Stagnated`, and
`InvalidTarget`. A finite positive peak is required to derive PSNR, but an
invalid peak or target does not retroactively invalidate coefficient, pixel,
residual, or MSE bounds that were otherwise established. Similarly,
`IterationLimit` and `Stagnated` leave `ProofStatus::Certified`.

## Canonical problem identity

The certificate associates its bounds with the exact supplied problem through
a SHA-256 digest. The input byte stream begins with
`lumenbound.problem-digest.v1`, uses tagged sections, encodes sizes as
big-endian unsigned 64-bit integers, and encodes every floating-point value by
its exact big-endian binary64 bit pattern. It includes, in order:

1. every emission band;
2. every transport operator;
3. the projection matrix;
4. signal peak, target PSNR, maximum iterations, and snapshot-retention flag.

Dimensions and sequence lengths are included, so band and coefficient order
are part of the identity. The `sha256:` digest detects accidental association
with different canonical input. It is not a digital signature and does not
authenticate the record or establish where the finite matrices came from.

## Manufactured demonstration

The demonstration constructs an exact finite solution first, chooses a
nontrivial nonnegative matrix with a contraction bound below one, and computes

```text
e = (I - T) exact.
```

The chosen data make `e` nonnegative. The known solution is used by tests to
check containment and conservativeness; the general certification algorithm
does not use it. A nonnegative matrix projects the coefficients to a small
linear image. The preview is derived only after all certified raw values have
been recorded and is labeled as false color.

For the documented peak `1.0` and target `80`, the residual intersection
reaches the target before any affine propagation. The regression record has
`q_bar = 0.093750000000000042`, `interval_iteration_count = 0`, maximum
coefficient width
`2.4424906541753448e-15`, `MSE_upper =
2.0445672289614941e-30`, and `PSNR_lower =
296.89398604509091`. These numbers describe only the manufactured finite
system.

## Cornell demonstration

The Cornell scene declares three bands, in order, as demo
`linear-sRGB R`, `linear-sRGB G`, and `linear-sRGB B` coefficients. This
declaration is local to the scene and supplies an explicit display-channel
interpretation. It is not a spectral derivation, standard-observer
integration, or validation of the chosen emission and reflectance values.

The authoritative image remains the raw finite projection in Equation (16).
For inspection only, the Cornell preview maps each candidate channel `v` as

```text
v_exposed = clamp(exposure * v, 0, 1)
v_display = sRGB_OETF(v_exposed),               (26)
```

then rounds `255 v_display` to an eight-bit PPM channel. Exposure, clamp, the
sRGB opto-electronic transfer function, and quantization are not present in
`P` and do not enter the MSE or PSNR certificate.

The Cornell certificate therefore has the same exact meaning as any other M0
certificate: its bounds apply to the binary64 `e`, `T`, and `P` that reached
the core. Geometry, rectangle discretization, ray visibility, transport
quadrature, continuous-camera behavior, colorimetry, and display conversion
remain outside the proof. This demonstration does not complete the M1
diffuse-assembly gate; an analytic two-patch reference and the remaining M1
evidence are still required.
