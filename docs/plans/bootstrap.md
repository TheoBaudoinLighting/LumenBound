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
      non-contractive inputs with distinct machine-readable statuses.
- [x] Preserve a componentwise enclosure of the manufactured exact solution at
      every certification iteration.
- [x] Demonstrate monotone lower bounds, monotone upper bounds, and contracting
      interval width.
- [x] Conservatively bound the candidate residual error in the infinity norm.
- [x] Project coefficient enclosures through a nonnegative matrix.
- [x] Report a conservative raw-linear image MSE upper bound and PSNR lower
      bound for an explicit positive signal peak.
- [x] Emit byte-identical `certificate.json` and `metrics.json` files on
      repeated runs.
- [x] Produce raw coefficient data, raw linear pixel data, a non-certifying PPM
      preview, and a concise console summary.
- [x] Fail with a nonzero exit status when the requested PSNR cannot be reached
      within the iteration limit.
- [x] Document the implemented guarantee, proof preconditions, failure
      behavior, architecture, and unsupported cases.

## Proof assumptions and numerical contract

For each independent coefficient band, the finite system is

`x = e + T x`.

Certification requires all of the following runtime-checked conditions:

1. Vector and matrix dimensions agree and are nonzero.
2. Every input value and the supplied signal peak are finite.
3. Emission `e`, transport `T`, and projection `P` are componentwise
   nonnegative.
4. A conservatively evaluated infinity-norm row-sum bound `q` satisfies
   `0 <= q < 1`.
5. Every arithmetic result used to establish an enclosure remains finite.
6. Interval endpoints and certificate reductions are rounded outward with
   directed `double` successor or predecessor operations.

The initial enclosure is `lower_0 = 0` and
`upper_0 = max(e) / (1 - q)`, replicated componentwise and rounded upward.
Positive interval propagation then computes `e + T lower` downward and
`e + T upper` upward in fixed row-major order. Each valid update is
intersected with the previous enclosure. An empty or non-finite intersection
is a numerical failure.

The candidate solve is diagnostic. Its residual certificate uses a
componentwise outward enclosure of `e + T candidate - candidate`, followed by
an outward upper bound on
`||r||_infinity / (1 - q)`. It does not replace interval propagation.

The projection is a positive linear map, so coefficient intervals are
projected with fixed-order outward arithmetic. Per-pixel error is the larger
distance from the candidate projection to either endpoint. Squared errors and
their mean are accumulated upward. PSNR is evaluated from the conservative MSE
bound; the zero-error case is represented explicitly and without relying on an
infinite JSON number.

## Planned structure

- `math`: intervals, dense vectors, dense matrices, and deterministic solve.
- `spectrum`: independent coefficient-band storage and serialization order.
- `transport`: validated positive finite transport systems.
- `solver`: candidate estimation and numerical diagnostics.
- `certification`: interval propagation, residual bounds, statuses, and
  certificate records.
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
- Treat preview conversion as display-only and exclude it from all
  certification claims.
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
- `build/dev/lumenbound_tests.exe`: 21 of 21 checks passed with no skips.
- The repeated-output test produced byte-identical `certificate.json` and
  `metrics.json` records in two independent output directories.
- The documented demonstration returned `Certified` after 4 interval
  iterations with `q = 0.093750000000000042`, maximum coefficient interval
  width `4.5724726957296909e-05`,
  `MSE_upper = 2.7313292363907636e-10`, and
  `PSNR_lower = 95.636259465354854 dB` for peak 1.
- A one-iteration run returned `UncertifiedIterationLimit`. A target of
  1000 dB reached arithmetic stagnation and returned
  `UncertifiedTargetNotReached`.

## Remaining work

M0 is implemented. The next work is M1 deterministic diffuse patch operator
assembly under the proof boundary defined in the roadmap.
