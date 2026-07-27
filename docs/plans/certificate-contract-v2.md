# Certificate contract v2 plan

## Purpose

Correct the M0 certificate boundary without changing the finite transport
model. A valid enclosure must remain certified when a requested PSNR target is
not reached. The residual proof must also participate directly in enclosure
tightening, and every machine-readable certificate must identify the exact
finite input and certification options it covers.

This work does not add scene assembly, a Galerkin weak formulation, visibility,
quadrature, hierarchy, path specialization, or colorimetry.

## Acceptance criteria

- [x] Report proof validity independently from target-refinement outcome.
- [x] Preserve `Certified` proof status after an iteration limit or arithmetic
      stagnation when the emitted bounds remain valid.
- [x] Retain distinct machine-readable causes for invalid proof inputs.
- [x] Intersect the positivity enclosure with the outward-rounded residual
      enclosure `[candidate - E, candidate + E]`.
- [x] Reach the documented 80 dB demonstration target without an affine
      interval iteration when the residual enclosure is sufficient.
- [x] Attach every certificate and metrics record to a deterministic SHA-256
      digest of the complete finite problem and certification options.
- [x] Record the certificate scope, solver version, and arithmetic policy.
- [x] Compile the certified core with strict floating-point semantics and
      interprocedural optimization disabled.
- [x] Reject build configurations whose compiler frontend has no defined
      strict floating-point policy.
- [x] Require `FLT_EVAL_METHOD == 0` in the certified arithmetic translation
      unit.
- [x] Label the PPM as a false-color coefficient preview.
- [x] Rename nearest-rounded candidate residual diagnostics without implying
      that they establish a proof.
- [x] Remove the unused fixed-size `Spectrum<N>` type in favor of the runtime
      band representation already used by `TransportSystem`.
- [x] Retain iteration snapshots only when explicitly requested.
- [x] Preserve deterministic, byte-identical certificate and metrics outputs.

## Proof assumptions

The algebraic assumptions remain those of M0: each supplied binary64 system is
finite, componentwise nonnegative, and contractive under a conservatively
evaluated infinity-norm bound `q_bar < 1`. Projection weights are finite and
nonnegative. Certified arithmetic passes the runtime binary64, directed
rounding, and subnormal-preservation preflight.

Let `c` be the deterministic candidate and let the interval residual
calculation establish

```text
||c - x||_infinity <= E.
```

Then every exact coefficient belongs to the outward-rounded interval
`[c_i - E, c_i + E]`. The positivity/contraction enclosure and this residual
enclosure are independent valid enclosures. Their componentwise intersection
therefore remains a valid enclosure. An empty intersection is a numerical
failure; the implementation does not fall back to either input interval.

`ProofStatus::Certified` states that the populated coefficient, pixel,
residual, candidate-error, and MSE bounds satisfy the finite-system contract.
A PSNR lower bound additionally requires a finite positive signal peak.
`TargetStatus` records whether that requested PSNR test was reached, could not
be evaluated, exhausted its iteration budget, or stagnated. Target failure
does not alter already established proof fields.

## Canonical problem identity

The digest preimage is a versioned binary stream. It contains raw binary64
bits, not formatted decimal values:

1. the format tag `lumenbound.problem-digest.v1` followed by a zero byte;
2. tagged emission data with band and vector sizes;
3. tagged transport data with band counts, dimensions, and row-major values;
4. tagged projection dimensions and row-major values;
5. tagged certification options: signal peak, target PSNR, maximum iteration
   count, and snapshot-retention policy;
6. a final `0xff` byte.

All counts use unsigned 64-bit big-endian encoding. Binary64 values use their
raw unsigned 64-bit representation in big-endian order. Signed zero and NaN
payloads are not canonicalized. The digest is emitted as `sha256:` followed by
64 lowercase hexadecimal digits.

The output path, PPM dimensions, and preview mapping are excluded because they
are not inputs to `certify`. A digest binds a certificate to known input. It
is not a signature and does not authenticate the producer.

## Implementation outline

- Replace the aggregate certificate status with `ProofStatus`,
  `ProofFailureCode`, and `TargetStatus`.
- Split MSE aggregation from peak-dependent PSNR conversion.
- Calculate the certified residual before refinement, build the residual box,
  and intersect it with the uniform positivity enclosure.
- Add a dependency-free incremental SHA-256 implementation local to the
  problem-identity module.
- Serialize the new contract as certificate and metrics schema v2.
- Make snapshot capture an explicit option disabled by default.
- Update the executable summary and exit condition to require both a valid
  proof and a reached target.
- Tighten compiler frontend selection and disable interprocedural
  optimization for the certified core.
- Update tests, public documentation, limitations, and recorded validation
  evidence.

## Validation commands

```text
cmake --preset dev
cmake --build --preset dev --parallel 2
ctest --preset dev --output-on-failure
build/dev/lumenbound_tests.exe
build/dev/lumenbound.exe demo certified-patches --output out/certified-patches --peak 1.0 --target-psnr 80
```

The deterministic output check will run the demonstration twice in separate
directories and compare `certificate.json` and `metrics.json` byte for byte.
Negative runs will cover an iteration-limited target, a stagnant target,
invalid metric inputs, invalid finite systems, and digest sensitivity.

## Progress

- [x] Audit the existing API, output schema, build flags, and tests.
- [x] Define the v2 acceptance criteria and proof assumptions.
- [x] Implement the certificate contract.
- [x] Extend tests and documentation.
- [x] Complete build, test, demonstration, and reproducibility validation.
- [x] Inspect the final diff and public repository contents.

## Decisions

- Schema v1 is not reinterpreted. The incompatible status and identity changes
  require schema v2.
- The target status includes `NotEvaluated`; algebraic proof failure otherwise
  has no honest target state.
- A non-finite target or invalid signal peak does not erase finite coefficient,
  pixel, residual, or MSE bounds. An invalid peak leaves PSNR unavailable. An
  invalid target does not erase an independently available PSNR bound.
- The residual intersection uses only the independently evaluated interval
  residual. Nearest-rounded candidate diagnostics remain diagnostics.
- One global `q_bar` and one global residual error bound remain in v2.
  Per-band bounds are deferred until they can be introduced with a versioned
  schema and focused tests.

## Discoveries

- The requested compiler flags were already present for MSVC and GNU-style
  GCC/Clang builds. The remaining build hazards are unknown compiler
  frontends, clang-cl frontend selection, implicit LTO enablement, and an
  unchecked excess-precision policy.
- The current transport API hides unmatched transport operators from generic
  traversal. Raw read-only collection access is required so invalid systems
  can still receive a digest covering every supplied value.
- The manufactured candidate residual is much narrower than the uniform
  positivity enclosure. Tests that require several affine iterations must use
  an explicit high target and opt in to snapshots.
- The final certificate audit found that nonnegative emission was validated
  but not named in the machine-readable assumptions. The assumption is now
  explicit. The same pass made the last permitted iteration return before the
  loop increment, so a maximum `size_t` budget cannot wrap.

## Validation evidence

- `cmake --preset dev`: configured with the Visual Studio 2022 generator and
  MSVC 19.44.35228.
- `cmake --build --preset dev --parallel 2`: built the core, executable, and
  tests with strict warnings, `/fp:strict`, and `/GL-`.
- `ctest --preset dev --output-on-failure`: 3 of 3 tests passed.
- `build/dev/lumenbound_tests.exe`: 24 of 24 checks passed with no skips.
- The documented demonstration returned `ProofStatus::Certified` and
  `TargetStatus::Reached` at zero affine interval iterations with
  `q = 0.093750000000000042`, maximum coefficient width
  `2.4424906541753448e-15`, `MSE_upper = 2.0445672289614941e-30`, and
  `PSNR_lower = 296.89398604509091`.
- A 1000 dB target with no iteration budget retained a certified proof and
  returned `TargetStatus::IterationLimit`. With the full budget it retained
  the proof and returned `TargetStatus::Stagnated` after three updates.
- Two independent output directories produced byte-identical
  `certificate.json` and `metrics.json`.
- An independent check of the generated JSON found all manufactured
  coefficients and projected pixels inside their reported intervals. The
  measured binary64 candidate MSE was
  `2.5679065925163143e-34`, below `MSE_upper`; the corresponding measured
  PSNR was `335.90420777757595 dB`, above `PSNR_lower`.
- An independent reconstruction of the canonical manufactured preimage
  produced
  `sha256:b9823914271361f1d8dcda2787ea4cec665b0e94b4a87a40bf53a86b0ce4e27d`.

## Remaining work

The contract correction is complete. A later Cornell demonstration uses it on
an assembled finite system, while the analytic two-patch work required to
close M1 remains outstanding.
