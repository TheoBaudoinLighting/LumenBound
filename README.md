# LumenBound

LumenBound is a deterministic, hierarchical spectral light-transport solver
designed to produce conservative residual bounds instead of Monte Carlo noise.

The repository currently contains an M0 research prototype. M0 implements a
small finite-dimensional algebraic core; it is not a complete renderer. It
solves independent positive transport systems of the form

```text
x = e + T x
```

using binary64 arithmetic on the CPU. For a result to receive `Certified`
status, the implementation checks that the emission vector `e` and transport
matrix `T` are finite and componentwise nonnegative, and that a conservatively
evaluated bound `q` satisfies

```text
q >= ||T||_infinity
q < 1
```

It then constructs componentwise lower and upper bounds for the unique
solution, projects those bounds through a finite nonnegative matrix, and
reports conservative error metrics for the resulting raw linear pixel
coefficients. A certificate applies only to the supplied finite matrices and
vectors. It does not establish that those inputs accurately represent a
continuous scene or the rendering equation.

## Candidate and certificate

M0 deliberately keeps two computations separate.

The candidate solver applies deterministic dense Gaussian elimination to
`(I - T)x = e`. Its result is an estimate suitable for output and
diagnostics. The candidate does not establish a bound.

The certification solver starts from a proven enclosure and propagates it with
outward-rounded interval arithmetic. It also encloses the candidate residual
and applies the contraction estimate

```text
||candidate - exact_solution||_infinity
    <= ||e + T candidate - candidate||_infinity / (1 - q)
```

Only the certification path can produce `Certified`. Invalid dimensions,
non-finite values, negative energy, a negative projection, a non-contractive
operator, an exhausted iteration limit, or a missed requested target produce
an explicit uncertified or numerical-failure status. There is no stochastic,
precision, device, or algorithmic fallback.

The derivation and the floating-point proof obligations are described in
[the mathematical model](docs/mathematical-model.md) and
[the certification contract](docs/certification.md).

## Build

LumenBound requires a C++23 compiler and CMake 3.25 or later.

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

A release preset is also provided:

```text
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

The continuous-integration configuration builds the project with GCC and
Clang on Linux. Platform coverage beyond the configurations present in the CI
workflow is not claimed.

## Deterministic demonstration

The `certified-patches` demonstration manufactures a positive transport
problem from a known finite solution. It contains at least six transport
coefficients and three independent coefficient bands, then projects them to a
small raw linear image.

After building, run:

```text
./build/dev/lumenbound demo certified-patches \
  --output out/certified-patches \
  --peak 1.0 \
  --target-psnr 80
```

On Windows, invoke `.\build\dev\lumenbound.exe` and use PowerShell backticks
for line continuation if entering the command on several lines.

The default propagation budget is 512 iterations. Use
`--max-iterations <count>` to set it explicitly.

The command writes:

- `candidate-coefficients.csv`, containing candidate transport coefficients
  and their binary64 encodings;
- `coefficient-bounds.csv`, containing candidate values, raw lower and upper
  bounds, absolute-error upper bounds, and status;
- `linear-pixels.csv`, containing the same fields for raw linear pixel
  coefficients;
- `preview.ppm`, a non-certifying display preview;
- `certificate.json`, containing the proof status, assumptions, contraction
  bound, residual and interval bounds, and raw-linear image bounds;
- `metrics.json`, containing the requested peak and conservative image-error
  metrics.

The machine-readable records use a fixed schema version and contain no
timestamps. Repeated successful runs with identical arguments are required to
produce byte-identical certificate and metrics files.

Representative certificate fields include:

```text
schema_version
status
reason
assumptions
contraction_upper_bound
interval_iteration_count
coefficient_bounds
pixel_bounds
residual_upper_bound
candidate_error_upper_bound
mse_upper_bound
psnr_lower_bound.kind
psnr_lower_bound.value
signal_peak
target_psnr
```

`metrics.json` also records `maximum_coefficient_interval_width`.

For a finite PSNR lower bound, `psnr_lower_bound.kind` is `finite` and
`value` contains the encoded bound. Exact zero error uses
`positive_infinity` and a null value. `unavailable` is explicit on paths that
did not produce a PSNR bound.

Each proof-bearing scalar in the JSON records contains a decimal
`max_digits10` value and the canonical binary64 bit pattern as 16 hexadecimal
digits. Zero is canonicalized to positive zero. The bit pattern fixes the
certificate value independently of decimal display formatting.
If a peak or target is rejected because it is non-finite, its decimal member
is null and a `classification` member records `nan`, `positive_infinity`, or
`negative_infinity`. The submitted binary64 bit pattern remains available for
diagnosis.
Proof quantities that were not established on an uncertified path are null,
not zero-valued substitutes.

The command exits successfully only if it establishes the requested
conservative PSNR lower bound. It records `UncertifiedIterationLimit` when it
exhausts the iteration budget while endpoints can still tighten. It records
`UncertifiedTargetNotReached` when an iteration changes no endpoint and the
stagnant outward-rounded enclosure still misses the target. The target check
has priority over both failures. The stagnation check has priority over the
iteration-limit check. The console summary reports the same raw-linear proof
quantities without replacing the JSON certificate.

Validation failures that occur before candidate or interval construction still
write `certificate.json` and `metrics.json`. They do not create raw coefficient
tables or a preview from unavailable values.

The PPM preview takes the first three candidate bands as display channels,
divides each by the supplied peak, clamps the result to `[0, 1]`, and rounds
`255` times that value to an integer. This mapping is not colorimetry.
Clipping and quantization are outside the certificate.

## Current limitations

M0 assumes that finite emission, transport, and projection data have already
been supplied. It does not implement or certify continuous geometry assembly,
visibility, quadrature, a general Galerkin basis, glossy or delta transport,
caustics, participating media, motion blur, depth of field, colorimetry, GPU
execution, or production-scale performance. Its independent coefficient bands
must not be interpreted as display RGB unless an independently validated
colorimetric mapping is supplied.

See [limitations](docs/limitations.md) for the full proof boundary and
[the roadmap](docs/roadmap.md) for the measurable gates required to extend it.

## Contributing and license

Changes must preserve deterministic behavior, candidate/certificate
separation, explicit failure handling, and outward-rounded bounds. See
[CONTRIBUTING.md](CONTRIBUTING.md) before preparing a change.

LumenBound is licensed under the
[Apache License 2.0](LICENSE).
