# Cornell box demonstration plan

## Purpose

Add a deterministic diffuse Cornell box demonstration that exercises the M0
finite-system certificate on data assembled from explicit geometry. The
result must be recognizable as a Cornell box and useful for inspecting the
current solver. It is not an end-to-end image certificate.

The finite binary64 system assembled by this demonstration may be certified
by M0. Rectangle discretization, visibility decisions, angular quadrature,
camera rays, declared linear-sRGB coefficients, and display conversion remain
outside that proof.

## Acceptance criteria

- [x] Add a documented `lumenbound demo cornell-box` command.
- [x] Construct an open Cornell box, an area emitter, and two interior boxes
      from oriented rectangles.
- [x] Subdivide rectangles into positive piecewise-constant diffuse patches
      with fixed canonical ordering.
- [x] Assemble nonnegative form-factor estimates with deterministic tensor
      quadrature and exact tie-breaking rules.
- [x] Use no random, Monte Carlo, quasi-Monte Carlo, denoising, temporal
      filtering, or unreported image-space smoothing. The positive bilinear
      patch reconstruction is an explicit part of `P`.
- [x] Keep each assembled transport row contractive by construction and
      validate the resulting `q` through the existing M0 path.
- [x] Build a nonnegative pinhole-camera projection with fixed subpixel
      quadrature.
- [x] Produce raw linear pixels, coefficient bounds, a finite-system
      certificate, metrics, an assembly record, and a display PPM.
- [x] Label the display PPM as non-certifying and record its explicit
      linear-sRGB display mapping.
- [x] Make projection evaluation skip exact zero weights without changing
      logical matrix semantics or dense-constructor behavior.
- [x] Preserve the existing `certified-patches` command and its regression
      values.
- [x] Test geometry validation, determinism, contraction, projection,
      command-line behavior, generated files, and display labeling.
- [x] Inspect the rendered image and reject an empty, inverted, clipped, or
      structurally broken result.
- [x] Reproduce certificate, metrics, assembly record, raw pixels, and PPM
      byte for byte on repeated runs.

## Discrete model

Each patch stores one outgoing diffuse-radiance coefficient per band. For
receiver patch `i` and source patch `j`, the demonstration assembles

```text
T_band(i,j) = reflectance_band(i) F(i,j).
```

`F(i,j)` is the fraction of a fixed cosine-weighted tensor quadrature launched
from deterministic points on patch `i` whose first geometric hit is the front
of patch `j`. A first back-face hit blocks the sample without adding energy.
Every remaining sample contributes to one patch or escapes. Consequently each
estimated form-factor row has a sum no larger than one. Surface reflectances
are finite, nonnegative, and strictly below one, so the assembled finite
operator is positive and contractive.

This is a deterministic quadrature estimate, not an interval enclosure of the
continuous form factor. The M0 certificate begins after the resulting
binary64 `e`, `T`, and `P` have been assembled.

The camera projection averages a fixed subpixel grid. A visible sample maps
to nearby piecewise-constant patch coefficients with nonnegative bilinear
weights. Missed samples contribute black. The projection therefore remains
finite and nonnegative.

## Implemented modules

- `transport/diffuse_patch_assembly`: rectangle validation, patch ordering,
  ray/rectangle visibility, cosine tensor quadrature, transport assembly, and
  pinhole projection.
- `core/cornell_box_demo`: fixed scene description, command execution,
  assembly metadata, and proof-boundary reporting.
- `io/output`: selectable non-certifying preview mappings while raw linear
  data remains unchanged.
- `apps/lumenbound`: command selection and Cornell image dimensions.

The generic assembly API keeps coefficient bands runtime-sized. Only the
Cornell scene declares its three bands to be linear-sRGB coefficients.

## Validation commands

```text
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
build/dev/lumenbound.exe demo cornell-box \
  --output out/cornell-box \
  --peak 4 \
  --target-psnr 80
```

Release validation will use the corresponding release preset. Reproducibility
validation will render two separate directories and compare every
proof-bearing and image output byte for byte.

## Progress

- [x] Define the demonstration boundary and acceptance criteria.
- [x] Implement diffuse patch assembly.
- [x] Implement the Cornell scene, camera, and output record.
- [x] Extend the command line and display writer.
- [x] Add unit and integration coverage.
- [x] Update public documentation and limitations.
- [x] Build, test, render, inspect, and verify reproducibility.
- [x] Inspect the final diff and public repository contents.

## Decisions

- The first Cornell image uses positive piecewise-constant patches. Signed
  bases would not satisfy the M0 positivity proof.
- Visibility is a deterministic nearest-hit decision over oriented
  rectangles. It is deliberately not called certified.
- Structured midpoint and cosine-domain tensor quadrature is used instead of
  sampling sequences.
- The camera projection uses canonical CSR rows. The digest reconstructs the
  historical logical-dense binary64 stream, so sparse and dense construction
  of the same matrix retain the same problem identity.
- Display conversion is separate from the peak used by the raw-linear PSNR
  calculation.

## Discoveries

- The original dense projection representation makes image-sized camera
  matrices needlessly expensive. `Projection` now stores canonical CSR rows
  while preserving the dense constructor and the historical logical-dense
  digest stream.
- The original preview searched the full pixel-bound array for every channel
  of every pixel. Certification already emits strict band-major order, so the
  writer now validates and indexes that order directly.
- A `128 x 128` render uses 274 transport coefficients, 280576 deterministic
  transport rays, and 41591 stored projection weights.

## Validation evidence

- `cmake --preset dev` and `cmake --build --preset dev --parallel 2`
  configured and built the C++23 development tree with MSVC 19.44.35228.
- `ctest --preset dev --output-on-failure` passed 4 of 4 CTest tests.
- The dependency-free harness passed 27 of 27 checks with no skips.
- The release preset configured, built, and passed 4 of 4 CTest tests.
- `build/release/lumenbound.exe demo cornell-box --output
  out/cornell-box-256 --width 256 --height 256 --preview-exposure 1
  --peak 4 --target-psnr 80` returned `ProofStatus::Certified` and
  `TargetStatus::Reached` with `q = 0.75000000000000799`, zero affine
  iterations, maximum coefficient width `1.8651746813702633e-14`,
  `MSE_upper = 5.6177669069055332e-29`, and
  `PSNR_lower = 294.54556267217646 dB`. This run used the release preset on
  Windows x64, MSVC 19.44.35228, and an Intel Core i9-11900KF. Its
  `metrics.json` SHA-256 is
  `616D950D1C4EB435C52040D0A501B6B0476F516FB6EDD3ACE04FDE0E21613224`.
- Visual inspection found the ceiling emitter, both colored walls, both
  interior boxes, the open front, and consistent upright camera orientation.
- Two independent `256 x 256` release renders produced byte-identical
  assembly, coefficient, raw-pixel, preview, metric, and certificate files.

## Remaining work

The Cornell demonstration is complete. M1 still requires the analytic
two-patch reference, independent comparison, documented tolerances, and the
remaining assembly tests.
