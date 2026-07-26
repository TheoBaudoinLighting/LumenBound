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
blue.

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
If it cannot reach the requested criterion within the iteration budget, it
returns an explicit uncertified status.

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

This residual certificate is independent evidence about the candidate. It
does not replace the componentwise enclosure from Equation (10).

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

The implementation passes the ordered set of `delta_j` values and `peak` to
the pure `compute_image_metric_bounds` function. The iterative certifier does
not implement a second aggregation path.

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
been recorded.
