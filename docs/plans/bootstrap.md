# Bootstrap implementation plan

## Purpose

Establish the M0 finite-dimensional transport core as a buildable C++23
research prototype. The milestone separates a deterministic candidate solve
from a certification path that encloses the solution of a positive,
contractive linear transport system and projects that enclosure to raw linear
pixel coefficients.

This milestone does not assemble transport from continuous geometry and does
not certify visibility, quadrature, glossy or delta transport, caustics,
participating media, color conversion, or display output.

## Acceptance criteria

- [x] Configure and build with the documented `dev` preset.
- [x] Pass every unit and integration test without skips.
- [x] Solve at least three independent bands with at least six coefficients
      per band using deterministic dense partial pivoting.
- [x] Reject malformed, non-finite, negative-energy, negative-projection, and
      non-contractive inputs with distinct machine-readable proof failures.
- [x] Report proof validity independently from PSNR target attainment.
- [x] Preserve a componentwise enclosure of the manufactured exact solution at
      every certification iteration.
- [x] Demonstrate monotone lower bounds, monotone upper bounds, and contracting
      interval width.
- [x] Conservatively bound the candidate residual error in the infinity norm.
- [x] Intersect the positive enclosure with the residual-derived candidate
      enclosure using outward-rounded endpoints.
- [x] Project coefficient enclosures through a nonnegative matrix.
- [x] Report a conservative raw-linear image MSE upper bound and PSNR lower
      bound for an explicit positive signal peak.
- [x] Emit byte-identical `certificate.json` and `metrics.json` files on
      repeated runs.
- [x] Bind both records to the complete finite problem and certification
      options with a canonical SHA-256 digest.
- [x] Produce raw coefficient data, raw linear pixel data, a non-certifying PPM
      preview, and a concise console summary.
- [x] Fail with a nonzero exit status when the requested PSNR cannot be reached
      while preserving the established proof status.
- [x] Document the implemented guarantee, proof preconditions, failure
      behavior, architecture, and unsupported cases.

## Proof assumptions and numerical contract

For each independent coefficient band, the finite system is

`x = e + T x`.

Certification requires all of the following runtime-checked conditions:

1. Vector and matrix dimensions agree and are nonzero.
2. Every emission, transport, and projection value is finite.
3. Emission `e`, transport `T`, and projection `P` are componentwise
   nonnegative.
4. A conservatively evaluated infinity-norm row-sum bound `q` satisfies
   `0 <= q < 1`.
5. Every arithmetic result used to establish an enclosure remains finite.
6. Interval endpoints and certificate reductions are rounded outward with
   directed `double` successor or predecessor operations.
7. The compiled core uses source-precision evaluation, disabled operation
   contraction and fast-math, and no interprocedural optimization.

The initial enclosure is `lower_0 = 0` and
`upper_0 = max(e) / (1 - q)`, replicated componentwise and rounded upward.
Positive interval propagation then computes `e + T lower` downward and
`e + T upper` upward in fixed row-major order. Each valid update is
intersected with the previous enclosure. An empty or non-finite intersection
is a numerical failure.

The candidate solve is diagnostic. Certification reevaluates its residual
with a
componentwise outward enclosure of `e + T candidate - candidate`, followed by
an outward upper bound on
`||r||_infinity / (1 - q)`. The resulting bound `E` proves the enclosure
`[candidate - E, candidate + E]`. That enclosure is intersected with the
positive initial enclosure before optional interval propagation.

The projection is a positive linear map, so coefficient intervals are
projected with fixed-order outward arithmetic. Per-pixel error is the larger
distance from the candidate projection to either endpoint. Squared errors and
their mean are accumulated upward. PSNR is evaluated from the conservative MSE
bound for a finite positive peak; the zero-error case is represented
explicitly and without relying on an infinite JSON number. An invalid peak or
target changes the target status, not independently established coefficient,
pixel, residual, or MSE bounds.

## Planned structure

- `math`: intervals, dense vectors, dense matrices, and deterministic solve.
- runtime coefficient bands: independent positional storage and serialization
  through `TransportSystem`.
- `transport`: validated positive finite transport systems.
- `solver`: candidate estimation and numerical diagnostics.
- `certification`: residual intersection, interval propagation, independent
  proof and target states, problem identity, and certificate records.
- `projection`: nonnegative coefficient-to-pixel maps.
- `io`: deterministic JSON, raw data, and PPM output.
- `apps/lumenbound`: command-line parsing and the manufactured demonstration.
- `tests`: dependency-free unit and integration coverage through CTest.

The CPU implementation is the only backend in M0. Backend-neutral interfaces
will identify the observable contract without adding an inactive CUDA
implementation.

## Progress

- [x] Record acceptance criteria and proof assumptions.
- [x] Create the C++23/CMake project structure.
- [x] Implement deterministic algebra and interval primitives.
- [x] Implement candidate solve and validation.
- [x] Implement certification, projection, and metrics.
- [x] Implement deterministic output and the manufactured demonstration.
- [x] Add unit and integration tests.
- [x] Complete public documentation and CI.
- [x] Configure, build, test, and run deterministic demonstrations.
- [x] Inspect numerical inequalities and repeated output bytes.
- [x] Inspect the final tracked diff and repository hygiene.

## Decisions

- Use `double` throughout the M0 reference path.
- Use row-major dense storage and fixed left-to-right accumulation.
- Break equal-magnitude pivot ties by the lowest row index.
- Require a runtime-validated binary64 environment with directed rounding and
  preserved subnormal values. Reject the calculation if the probe fails.
- Keep serialization field order fixed. Store each proof-bearing scalar as a
  locale-independent decimal diagnostic and an authoritative 16-digit
  binary64 bit string.
- Hash every finite-system input and certification option in a tagged,
  endian-independent canonical byte stream. The digest associates inputs and
  records; it is not a signature.
- Retain full iteration snapshots only when an API caller opts in.
- Disable LTO on the certified core until cross-translation-unit optimization
  has evidence for the rounding contract.
- Treat preview conversion as display-only and exclude it from all
  certification claims. The current PPM is explicitly false color.
- Keep generated demonstration output outside the tracked source tree by
  default.

## Discoveries

- An outward-rounded affine update does not by itself make the stored bounds
  monotone. Intersecting each valid update with the prior enclosure preserves
  inclusion and enforces the required ordering.
- The C++ library does not specify a usable error bound for `std::log10`.
  Certified PSNR therefore uses binary range reduction, an interval atanh
  series, and an explicit geometric remainder bound.
- A type-level subnormal capability flag is insufficient because runtime
  flush-to-zero modes can still discard values. Certification now probes
  subnormal production and preservation before accepting the arithmetic
  environment.
- A valid enclosure and a requested stopping target are different results.
  Iteration exhaustion and stagnation no longer erase a proven enclosure.
- The manufactured dense solve has a residual error bound near binary64
  precision. Intersecting its residual enclosure with the positive enclosure
  proves the 80 dB target without an affine propagation step.
- Compiler-family checks were insufficient for clang-cl because its option
  syntax follows the MSVC frontend. The build now selects floating-point
  options by compiler frontend and rejects unknown policies.

## Commands

Planned validation commands:

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
build/dev/lumenbound demo certified-patches --output out/certified-patches --peak 1.0 --target-psnr 80
```

Negative-path behavior is also exercised by CTest and the unit/integration
test executable.

## Validation evidence

- `cmake --preset dev`: configured with MSVC 19.44 and the Visual Studio 2022
  generator.
- `cmake --build --preset dev --parallel 2`: built the core, executable, and
  tests with strict warnings.
- `ctest --preset dev --output-on-failure`: 3 of 3 CTest tests passed.
- `build/dev/lumenbound_tests.exe`: 24 of 24 checks passed with no skips.
- The repeated-output test produced byte-identical `certificate.json` and
  `metrics.json` records in two independent output directories.
- The documented demonstration returned `ProofStatus::Certified` and
  `TargetStatus::Reached` with zero affine interval iterations,
  `q = 0.093750000000000042`, maximum coefficient interval width
  `2.4424906541753448e-15`,
  `MSE_upper = 2.0445672289614941e-30`, and
  `PSNR_lower = 296.89398604509091 dB` for peak 1.
- A zero-iteration budget with a 1000 dB target returned
  `TargetStatus::IterationLimit` while retaining `ProofStatus::Certified`.
  Continuing the same target reached `TargetStatus::Stagnated` after three
  affine interval updates and also retained the proof.
- The documented problem digest was independently reconstructed as
  `sha256:b9823914271361f1d8dcda2787ea4cec665b0e94b4a87a40bf53a86b0ce4e27d`.

## Remaining work

M0 is implemented. A Cornell box demonstration now exercises an explicitly
unbounded deterministic diffuse assembly path, but it does not close M1. The
next proof-gate work remains an analytic two-patch geometry with independently
checked form factors and the assembly boundary defined in the roadmap.
