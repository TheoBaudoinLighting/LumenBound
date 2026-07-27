# Limitations

## Proof boundary

M0 is a deterministic certificate engine for supplied finite-dimensional,
positive, contractive transport systems. It certifies their solutions and a
nonnegative projection. It is not a general Galerkin solver. A narrow
coefficient interval does not imply that the finite system is a close
approximation of a physical scene. Discretization, quadrature, visibility,
basis truncation, spectral reconstruction, and display conversion errors are
not included in the M0 bounds.

The implementation is a research prototype. It is not a production renderer
and does not expose a general scene-description pipeline. The Cornell
demonstration adds one fixed rectangle-based construction path, but its
geometry-to-matrix step is not certified.

## Scene and operator construction

M0 still has no certified continuous geometry assembly. It does not parse a
general mesh, material, light, camera, or acceleration-structure format into a
bounded operator. The manufactured demonstration defines its matrix and exact
finite solution directly.

`transport/diffuse_patch_assembly` is narrower. It accepts oriented
rectangles, constant runtime-band diffuse materials, a pinhole camera, and
fixed quadrature dimensions. The Cornell scene uses 274 constant patches.
Every receiver patch traces a two by two spatial grid and an 8 by 32
cosine-domain direction grid, for 1024 rays. A first front-facing hit
contributes to one source patch; escape and a first back-face hit contribute
no transport term. This produces a deterministic positive finite matrix, not
an interval enclosure of a continuous diffuse operator.

There is no weak formulation, trial space, test space, mass matrix, or
assembled bilinear form. There are no bounds for:

- geometric approximation or tessellation;
- patch integration or general Galerkin basis integration;
- near-field or singular-kernel treatment;
- quadrature truncation and roundoff during operator assembly;
- unresolved occlusion boundaries;
- general visibility or shadow classification.

Consequently, the certificate must not be described as an end-to-end rendering
equation certificate. For Cornell output it starts only after the assembler
has produced exact binary64 `e`, `T`, and `P`.

## Supported transport class

The finite matrix is componentwise nonnegative and strictly contractive in a
conservatively bounded infinity norm. This is an algebraic input condition,
not a general result for light transport.

The proof uses positivity directly. If coefficients are interpreted as basis
weights, the basis and assembly must preserve nonnegative emission, transport,
and projection coefficients. Many classical Galerkin bases and transformed
operators contain signed entries. They are outside M0; accepting them requires
a separately derived signed enclosure rather than removal of the validation
check.

M0 does not implement specialized treatment for:

- regular glossy or anisotropic scattering;
- delta reflection or refraction;
- explicit specular chains;
- caustic paths or manifold constraints;
- participating media, transmittance, or volume scattering;
- wavelength-changing transport;
- sensor-domain or adjoint importance transport.

Those classes require distinct proof assumptions and must not be represented
as already covered by the positive dense core.

## Basis and adaptivity

The current coefficients are independent finite values supplied to the
solver as runtime-sized, positionally ordered bands. There is no fixed-size
`Spectrum<N>` type in the core. M0 does not build a hierarchical spatial
basis, angular Galerkin basis, validated spectral basis, or compressed
operator. It does not perform residual-driven refinement, view-adaptive
refinement, wavelet truncation, or low-rank approximation.

Any interpretation of a coefficient as a spatial, angular, or wavelength
basis weight belongs to the caller and is outside the certificate.

## Camera and image formation

The projection is an explicit nonnegative sparse matrix. The Cornell builder
constructs it from a pinhole camera with a two by two subpixel grid. Each
front-facing hit uses nonnegative bilinear reconstruction over neighboring
constant patch coefficients; misses and first back-face hits add black. M0
certifies the resulting binary64 matrix as `P`. It does not establish that
the ray construction, bilinear reconstruction, or two by two quadrature
matches a continuous physical camera or bounds aliasing.

There is no motion blur, depth of field, rolling shutter, lens system,
diffraction, sensor response, or validated antialiasing.

The reported MSE and PSNR bounds apply to raw linear projected coefficients and
the user-supplied signal peak. They do not apply to:

- the false-color PPM coefficient preview;
- tone mapping or exposure selection;
- clipping and integer quantization;
- the sRGB opto-electronic transfer function or another display transfer;
- monitor behavior or perceptual quality.

No denoiser, temporal filter, learned reconstruction, or hidden image-space
smoothing is used to meet a target.

## Spectral and color limitations

Multiple coefficient bands are solved independently, but they are abstract
unless accompanied by a separately validated basis. M0 does not supply:

- spectral emission or reflectance integration;
- bounded wavelength interpolation;
- standard-observer color matching;
- illuminant adaptation;
- conversion to a color space;
- gamut mapping or display calibration.

The manufactured demonstration maps three abstract bands to PPM channels only
for false-color inspection. The Cornell scene instead declares its three bands
as linear-sRGB demo coefficients and assigns them to the corresponding file
channels. That declaration is not derived from spectra or measured
colorimetry. The material and emitter coefficients remain unvalidated display
approximations.

The Cornell PPM applies an explicit exposure, clamps to `[0, 1]`, applies the
sRGB opto-electronic transfer function, and quantizes to eight bits. Those
operations are display-only and are absent from the raw-linear certificate.

## Numerical limitations

The CPU reference path uses `double`. Its interval layer must preserve outward
rounding, but finite precision can stop interval widths from shrinking further.
A requested target may therefore remain unmet even for a mathematically
contractive system. `TargetStatus::IterationLimit` or `Stagnated` does not
invalidate finite-system bounds already carrying
`ProofStatus::Certified`.

The current certificate uses one maximum contraction bound and one residual
error bound across all bands. Per-band bounds could be narrower, but are not
implemented.

Cornell assembly uses ordinary binary64 trigonometry and ray/rectangle
intersection. A fixed ray-origin offset, parallel-intersection epsilon,
boundary clamp, and nearest-hit ordering resolve the implemented finite
calculation. They do not enclose exact geometry. Rays near an edge, tangent,
or coincident surface can therefore expose toolchain or representation
sensitivity that the M0 interval layer cannot repair after assembly.

The transport quadrature counts are required to be exact powers of two. This
makes the `hit_count / sample_count` weights exactly representable for the
current counts, but it does not bound quadrature truncation. Likewise, sparse
camera rows reduce storage and arithmetic over zero entries; they do not
improve the continuous image model.

The infinity-norm condition `q_bar < 1` is sufficient, not necessary. M0
rejects a matrix when this check fails even if another norm or a spectral
argument could prove contraction. It does not silently switch proofs.

The candidate transport solver and transport operators remain dense and are
intended for small systems. Camera projection is sparse, but M0 contains no
sparse transport factorization, preconditioner, hierarchical matrix
operation, or production-scale memory strategy. No speed, memory, or scaling
claim is made.

Deterministic traversal and serialization are specified within supported build
configurations. Byte-identical output across different compilers, standard
libraries, processors, or floating-point environments is not claimed without
specific parity evidence.

The certified core requires source-precision binary64 evaluation, strict
rounding semantics, disabled operation contraction, and no LTO. Builds using
an unrecognized compiler policy are rejected. These restrictions are part of
the current proof implementation, not general portability claims.

## Execution backends

M0 provides only a CPU reference implementation. There is no CUDA code, GPU
certificate, heterogeneous scheduler, or device fallback. A later backend must
match the observable validation and enclosure contract and pass parity tests
before it can produce certified status.

## Demonstration scope

The manufactured problem is designed to exercise finite algebra, interval
propagation, residual intersection, projection, problem identity,
serialization, and failure behavior. The residual enclosure is narrow enough
to meet the documented target with zero affine interval iterations. Because
the exact finite solution is constructed in advance, it is suitable for
regression tests. It is not evidence for accuracy on an external scene,
general convergence rates, production performance, or research novelty.

The Cornell demonstration adds oriented rectangles, constant diffuse patches,
fixed first-hit quadrature, sparse camera projection, and a declared
linear-sRGB preview. Its `assembly.json` status is
`DeterministicUnbounded`. Geometry, visibility, quadrature, discretization,
continuous camera formation, colorimetry, and display conversion remain
outside the proof.

The SHA-256 problem digest does not prevent someone from replacing both a
problem and its record. It is an exact-input association mechanism, not a
signature, trust anchor, or scene provenance record.

The next roadmap gate remains M1, deterministic diffuse patch operator
assembly. The Cornell path does not close it. An analytic two-patch reference,
independent comparison, documented tolerances, and the remaining M1 tests are
still required. End-to-end certification also requires the later visibility
and quadrature obligations.
