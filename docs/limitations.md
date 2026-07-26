# Limitations

## Proof boundary

M0 certifies only the solution and nonnegative projection of a supplied
finite-dimensional positive, contractive transport system. A narrow
coefficient interval does not imply that the finite system is a close
approximation of a physical scene. Discretization, quadrature, visibility,
basis truncation, spectral reconstruction, and display conversion errors are
not included in the M0 bounds.

The implementation is a research prototype. It is not a production renderer
and does not expose a general scene-description pipeline.

## Missing scene and operator construction

M0 has no continuous geometry assembly. It does not parse meshes, materials,
lights, cameras, or acceleration structures into a certified operator. The
manufactured demonstration defines its matrix and exact finite solution
directly.

There are no bounds for:

- geometric approximation or tessellation;
- patch integration or general Galerkin basis integration;
- near-field or singular-kernel treatment;
- quadrature truncation and roundoff during operator assembly;
- unresolved occlusion boundaries;
- general visibility or shadow classification.

Consequently, the certificate must not be described as an end-to-end rendering
equation certificate.

## Supported transport class

The finite matrix is componentwise nonnegative and strictly contractive in a
conservatively bounded infinity norm. This is an algebraic input condition,
not a general result for light transport.

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
solver. M0 does not build a hierarchical spatial basis, angular Galerkin
basis, validated spectral basis, or compressed operator. It does not perform
residual-driven refinement, view-adaptive refinement, wavelet truncation, or
low-rank approximation.

Any interpretation of a coefficient as a spatial, angular, or wavelength
basis weight belongs to the caller and is outside the certificate.

## Camera and image formation

The projection is an explicit nonnegative matrix. M0 does not establish that
it represents a physical camera, pixel footprint, reconstruction filter, or
sensor response. It has no motion blur, depth of field, rolling shutter,
lens system, or validated antialiasing.

The reported MSE and PSNR bounds apply to raw linear projected coefficients and
the user-supplied signal peak. They do not apply to:

- the PPM preview;
- tone mapping or exposure selection;
- clipping and integer quantization;
- gamma or another display transfer function;
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

Three bands in the demonstration must not be interpreted as proof of RGB
colorimetry.

## Numerical limitations

The CPU reference path uses `double`. Its interval layer must preserve outward
rounding, but finite precision can stop interval widths from shrinking further.
A requested target may therefore remain unproven even for a mathematically
contractive system.

The infinity-norm condition `q_bar < 1` is sufficient, not necessary. M0
rejects a matrix when this check fails even if another norm or a spectral
argument could prove contraction. It does not silently switch proofs.

The dense candidate solver is intended for small systems. M0 contains no
sparse factorization, preconditioner, hierarchical matrix operation, or
production-scale memory strategy. No speed, memory, or scaling claim is made.

Deterministic traversal and serialization are specified within supported build
configurations. Byte-identical output across different compilers, standard
libraries, processors, or floating-point environments is not claimed without
specific parity evidence.

## Execution backends

M0 provides only a CPU reference implementation. There is no CUDA code, GPU
certificate, heterogeneous scheduler, or device fallback. A later backend must
match the observable validation and enclosure contract and pass parity tests
before it can produce certified status.

## Demonstration scope

The manufactured problem is designed to exercise finite algebra, interval
propagation, projection, serialization, and failure behavior. Because its
exact finite solution is constructed in advance, it is suitable for regression
tests. It is not evidence for accuracy on an external scene, general
convergence rates, production performance, or research novelty.

The next roadmap gate is M1, deterministic diffuse patch operator assembly.
M1 must add an explicit discretization model and tests, but end-to-end
certification still requires the later visibility and quadrature obligations.
