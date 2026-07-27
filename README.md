# LumenBound

LumenBound is a deterministic, hierarchical spectral light-transport solver
designed to produce conservative residual bounds instead of Monte Carlo noise.

The repository currently contains an M0 research prototype. What exists today
is a deterministic certificate engine for supplied finite-dimensional,
positive, contractive linear systems. It is not a complete renderer and it is
not a general Galerkin solver. It solves independent coefficient-band systems
of the form

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

The repository also contains a deterministic diffuse Cornell box
demonstration. It assembles a positive finite system from oriented rectangles,
fixed visibility rays, and fixed tensor quadrature, then passes that system to
the same M0 core. This is useful rendering evidence, but it does not extend the
certificate over geometry, visibility, quadrature, or spatial discretization.

## Candidate and certificate

M0 deliberately keeps two computations separate.

The candidate solver applies deterministic dense Gaussian elimination to
`(I - T)x = e`. Its result is an estimate suitable for output and
diagnostics. The candidate does not establish a bound.

The certification solver starts from a proven positive-transport enclosure.
It independently encloses the candidate residual and applies the contraction
estimate

```text
||candidate - exact_solution||_infinity
    <= ||e + T candidate - candidate||_infinity / (1 - q)
```

The resulting error bound `E` defines another proven enclosure,
`[candidate - E, candidate + E]`, with outward-rounded endpoints. M0
intersects it with the positive-transport enclosure before optional fixed-point
propagation. The current manufactured problem reaches its requested image
bound from this intersection alone.

Proof validity and target attainment are separate. `ProofStatus::Certified`
means that the reported finite-system bounds are valid.
`TargetStatus::Reached`, `IterationLimit`, `Stagnated`, or `InvalidTarget`
describes only the requested PSNR stopping condition. Exhausting the iteration
budget or stagnating before the requested target does not invalidate an
already established enclosure. Invalid dimensions, non-finite system data,
negative energy, a negative projection, a non-contractive operator, or a
numerical failure produce `ProofStatus::Uncertified` with a distinct
`ProofFailureCode`. There is no stochastic, precision, device, or algorithmic
fallback.

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

The certified core is compiled with dynamic-rounding semantics and without
operation contraction or fast-math. GCC and Clang builds use
`-frounding-math`, `-fno-fast-math`, and `-ffp-contract=off`; MSVC and
clang-cl builds use `/fp:strict`. The translation unit rejects fast-math and
requires `FLT_EVAL_METHOD == 0`. Interprocedural optimization and LTO are
disabled for the certified core.

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
  bounds, absolute-error upper bounds, and proof status;
- `linear-pixels.csv`, containing the same fields for raw linear pixel
  coefficients;
- `preview.ppm`, a non-certifying false-color coefficient preview;
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
certificate_scope
problem_digest
solver_version
arithmetic_policy
proof_status
proof_failure
proof_reason
target_status
target_reason
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
The schema versions are `lumenbound.certificate.v2` and
`lumenbound.metrics.v2`.

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

`problem_digest` starts with `sha256:` and identifies the exact ordered
binary64 problem. Its canonical input covers every emission vector, every
transport matrix, the projection matrix, the signal peak, target PSNR,
iteration budget, snapshot-retention option, dimensions, band order, and
coefficient order. It is a deterministic association check, not a digital
signature, authentication mechanism, or statement about the physical origin
of the matrices. `certificate_scope`, `solver_version`, and
`arithmetic_policy` state which contract produced the record.

Iteration snapshots are an opt-in in-memory API diagnostic. They are disabled
for the command-line demonstration, so normal runs retain only the final
bounds and iteration count.

The command exits successfully only when `target_status` is `Reached`. It
returns nonzero for `IterationLimit`, `Stagnated`, or `InvalidTarget`, while
preserving `proof_status: Certified` when the finite-system proof remains
valid. A reached target is checked before stagnation; stagnation is checked
before budget exhaustion. The console summary reports both statuses and the
same raw-linear proof quantities without replacing the JSON certificate.

Validation failures that occur before candidate or interval construction still
write `certificate.json` and `metrics.json`. They do not create raw coefficient
tables or a preview from unavailable values.

For `certified-patches`, the PPM is explicitly a false-color coefficient
preview. It assigns the first three candidate bands to the file channels,
divides each by the supplied peak, clamps to `[0, 1]`, and rounds `255` times
that value to an integer. The assignment is not RGB colorimetry. Clipping and
quantization are outside the certificate.

For the command above, the current regression output is:

```text
proof_status: Certified
target_status: Reached
q_upper: 0.093750000000000042
interval_iterations: 0
maximum_coefficient_interval_width: 2.4424906541753448e-15
mse_upper: 2.0445672289614941e-30
psnr_lower: 296.89398604509091
```

These values were observed with the development preset on Windows x64, MSVC
19.44.35228, and an Intel Core i9-11900KF. They are regression values for the
manufactured finite input, not a benchmark or an accuracy result for a
physical scene.

## Cornell box demonstration

The Cornell box path is a deterministic diffuse patch renderer built on the
same finite algebra:

```text
./build/dev/lumenbound demo cornell-box \
  --output out/cornell-box \
  --peak 4.0 \
  --target-psnr 80
```

The default image is `128 x 128`. Certified output accepts dimensions from 16
through 256. `--preview-exposure` controls only the display PPM and defaults to
`1.0`; it does not change the raw pixels, certificate, problem digest, MSE, or
PSNR.

Larger inspection renders, up to `1024 x 1024`, require the explicit
`--preview-only` flag:

```text
./build/dev/lumenbound demo cornell-box \
  --output out/cornell-box-800 \
  --width 800 --height 800 \
  --preview-only
```

That mode solves and projects the deterministic candidate but writes only
`preview.ppm`. It does not run the interval certificate or emit proof and
metric files.

The scene contains five inward-facing room surfaces, a ceiling emitter, and
two rotated diffuse boxes. Rectangles are split into 274 positive constant
patches. For every receiver patch, the assembler evaluates two by two fixed
surface points and an 8 by 32 tensor grid over the cosine hemisphere. Each of
the resulting 1024 rays contributes to one first front-facing patch or to no
transport term. There is no random sequence.

The command writes the standard coefficient, raw-pixel, certificate, and
metric files. It also writes `assembly.json`. That record identifies the
assembly method, camera and quadrature counts, declared band semantics, and
the exact M0 `problem_digest`. Its proof boundary is explicit:

```text
finite binary64 system: certified when all M0 checks pass
continuous scene: not certified
geometry and visibility: not certified
quadrature and discretization: not certified
display mapping: not certified
```

The three demo bands are declared approximate linear-sRGB display
coefficients. They are not measured spectra and do not establish validated
colorimetry. `preview.ppm` applies the requested exposure, clamps to `[0,1]`,
applies the sRGB opto-electronic transfer function, and quantizes to eight
bits. `linear-pixels.csv` remains the authoritative raw-linear result.

## Current limitations

The M0 proof assumes that finite emission, transport, and projection data have
already been supplied. The Cornell demonstration can construct such data, but
its construction is not enclosed by M0. There is still no weak formulation,
trial or test functions, mass matrix, certified geometric assembly, or
bounded spatial or angular quadrature. LumenBound therefore does not
implement a general Galerkin method. The proof requires
componentwise nonnegative `e`, `T`, and `P`; a future basis interpretation must
preserve that positive coefficient cone. Classical signed bases are outside
the current contract and require a different enclosure argument.

M0 also does not certify visibility, hierarchy, view adaptation, compression,
glossy or delta transport, caustics, participating media, motion blur, depth
of field, colorimetry, GPU execution, or production-scale performance. The
Cornell path implements only fixed binary64 rectangle visibility and diffuse
collocation. Runtime coefficient bands remain positionally ordered; only the
Cornell scene gives its three bands an explicit, unvalidated display
interpretation.

See [limitations](docs/limitations.md) for the full proof boundary and
[the roadmap](docs/roadmap.md) for the measurable gates required to extend it.

## Contributing and license

Changes must preserve deterministic behavior, candidate/certificate
separation, explicit failure handling, and outward-rounded bounds. See
[CONTRIBUTING.md](CONTRIBUTING.md) before preparing a change.

LumenBound is licensed under the
[Apache License 2.0](LICENSE).
