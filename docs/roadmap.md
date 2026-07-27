# Roadmap

Each milestone is a proof gate. Later work may begin experimentally before a
gate closes, but it must not inherit certified status until its own proof
obligation and exit criterion are met. Tests listed here are minimum evidence,
not substitutes for derivations or runtime checks.

The Cornell demonstration is such experimental work. Its deterministic
rectangle collocation and sparse camera projection feed binary64 `e`, `T`, and
`P` to M0, but geometry, visibility, quadrature, discretization, continuous
camera formation, declared linear-sRGB semantics, and display conversion
remain outside the certificate.

## M0: Certified finite-dimensional positive transport core

**Scope.** Implement the CPU binary64 reference path for independent finite
systems `x = e + T x`, deterministic dense candidate solving, monotone
interval enclosure, residual-derived enclosure intersection, nonnegative
projection, raw-linear MSE and PSNR bounds, separate proof and target states,
canonical SHA-256 problem identity, and deterministic schema v2 output.

**Proof obligation.** Runtime checks establish finite compatible dimensions,
componentwise nonnegative `e`, `T`, and `P`, and a conservative
`q_bar >= ||T||_infinity` with `q_bar < 1`. Outward arithmetic preserves
coefficient and projected-pixel inclusion and conservatively aggregates all
reported bounds. A target budget or stagnation event must not invalidate an
established proof. The compiled core must enforce source-precision binary64
evaluation, dynamic rounding, disabled contraction and fast-math, and no LTO.

**Tests.** Cover interval edge cases, matrix norms, manufactured candidate
accuracy, monotone endpoint sequences, exact-solution containment at every
iteration, residual containment, projected containment, conservative MSE and
PSNR, negative and non-finite inputs, non-contraction, deterministic
serialization and digest coverage, proof/target state independence, opt-in
snapshot retention, zero-iteration target success, iteration-limit target
failure, and stagnation.

**Unsupported cases.** Continuous geometry, operator assembly, visibility and
quadrature error, weak formulations, mass matrices, general or signed Galerkin
bases, glossy and delta transport, caustics, media, validated colorimetry, GPU
execution, and production-scale systems.

**Exit criterion.** A clean checkout configures and builds with the documented
C++23 CMake presets; all tests pass without skips; the manufactured command
produces byte-identical certificate and metrics records on repeated runs; all
reported inequalities hold against the known finite solution; and
documentation states the finite proof boundary. The manufactured target is
reached from the residual intersection with zero affine interval iterations,
while explicit limit and stagnation cases preserve `ProofStatus::Certified`.

## M1: Deterministic diffuse patch operator assembly

**Scope.** Assemble finite diffuse transport coefficients from a documented
static patch scene representation with fixed traversal, patch orientation,
emission, and reflectance conventions. Feed the resulting operator to M0
without weakening its validation. Begin with a two-patch geometry whose finite
radiosity solution and form-factor relations can be checked analytically.

The current Cornell path is an implementation step toward this gate. It uses
constant rectangle patches, two by two receiver points, an 8 by 32
cosine-domain direction grid, first-hit or escape classification,
`T^(b)_ij = rho^(b)_i F_ij`, and a positive sparse two by two camera
projection with bilinear reconstruction. These features do not close M1.

**Proof obligation.** Define the discrete diffuse form-factor model, units,
normal conventions, reciprocity expectations, self-interaction policy, and
energy-conservation conditions. Assembly must be deterministic and must not
claim bounds for unresolved integration or visibility error. The initial patch
representation is deliberately positive: assembled emission, transport, and
projection coefficients must remain nonnegative.

**Tests.** Include analytic or high-precision patch configurations,
starting with the two-patch case; orientation and unit tests; row-energy
checks; permutation tests with a specified canonical ordering;
degenerate-geometry rejection; and regression scenes preserving raw assembled
matrices. Cornell regression and determinism tests supplement, but do not
replace, the analytic two-patch evidence.

**Unsupported cases.** Certified visibility, bounded quadrature error,
hierarchical basis functions, glossy or specular scattering, media, motion,
and a claim that the discrete matrix encloses continuous transport.

**Exit criterion.** Supported diffuse scenes produce reproducible finite
operators whose entries and conventions match independent analytic or
high-precision references within documented non-certified assembly tolerances;
all unsupported geometry is rejected explicitly; and the output can be
certified only as an M0 finite system. The analytic two-patch comparison and
its documented tolerances remain required before this criterion is met.

## M2: Interval-bounded visibility and quadrature

**Scope.** Add conservative geometric predicates, visibility classification,
and interval-bounded integration for the supported diffuse patch kernel.

**Proof obligation.** Enclose geometric input, distances, cosines, visibility,
singular or near-singular regions, and quadrature remainders. Every operator
entry must contain the corresponding continuous integral under stated scene
regularity assumptions. Unresolved visibility must widen a bound or fail; it
must never be guessed.

**Tests.** Exercise occlusion tangencies, shared edges, near-contact patches,
distance extremes, interval predicate boundaries, analytic integrals,
subdivision convergence, adversarial geometry, and preservation of failing
cases.

**Unsupported cases.** Geometry outside the documented primitive and
regularity set, glossy or delta kernels, motion, deforming geometry, and
unbounded singular configurations.

**Exit criterion.** For every accepted scene, assembled interval entries
contain independently evaluated references and all visibility states are
resolved or represented conservatively; an end-to-end diffuse operator bound
is emitted with its geometric and quadrature assumptions.

## M3: Hierarchical spatial basis and residual-driven refinement

**Scope.** Introduce nested spatial basis functions, hierarchical coefficient
storage, and deterministic refinement driven by certified residual bounds.

**Proof obligation.** Bound basis truncation, restriction, prolongation, and
operator-application error. Refinement must preserve positivity or use a
separately proven signed-interval formulation. The stopping rule must include
all retained and discarded residual contributions.

**Tests.** Verify partition and nesting properties, refinement-order
determinism, coarse/fine enclosure compatibility, manufactured spatial
solutions, localized residual cases, and comparison with an uncompressed
fine reference.

**Unsupported cases.** Angular hierarchy, spectral reconstruction, glossy or
delta transport, unbounded visibility, and heuristic refinement presented as
certified.

**Exit criterion.** Refinement terminates deterministically on supported
diffuse scenes and returns bounds that contain a fully expanded reference,
with an explicit budget for spatial truncation and no hidden interpolation.

## M4: Angular Galerkin basis for regular glossy transport

**Scope.** Add a finite angular basis and transport assembly for a restricted
class of regular, bounded, non-delta glossy scattering functions.

**Proof obligation.** Enclose basis projection, BRDF evaluation, angular
quadrature, positivity or signed-basis effects, and energy conservation.
Regular glossy transport must remain distinct from delta/specular chains. A
signed basis cannot reuse the M0 positivity proof; it needs an explicit
signed-operator enclosure and validation contract.

**Tests.** Cover constant and low-order analytic lobes, reciprocity where
required, grazing angles, roughness boundaries, basis truncation, nonnegative
energy, and containment against high-precision angular integration.

**Unsupported cases.** Dirac distributions, perfect mirrors or refraction,
singular microfacet limits, caustics, polarization, fluorescence, and
unvalidated material models.

**Exit criterion.** The supported regular-glossy class has explicit parameter
limits, bounded angular projection and integration errors, and end-to-end
coefficient enclosures that remain valid under spatial and angular
refinement.

## M5: Spectral basis and validated colorimetry

**Scope.** Represent wavelength-dependent emission, transport, and sensor
response in a documented spectral basis, then add a validated conversion from
bounded spectra to selected linear colorimetric quantities.

**Proof obligation.** Bound spectral interpolation, basis truncation,
wavelength quadrature, product terms, and color-matching integration. State
the wavelength domain, units, observer, illuminant assumptions, and handling
of out-of-gamut values.

**Tests.** Include analytic spectra, narrow-band stress cases, standard
reference datasets with recorded provenance, basis convergence, interval
color conversion, and independent high-precision integration.

**Unsupported cases.** Wavelength-changing effects unless explicitly added,
polarization, perceptual tone mapping, display calibration, and spectra
outside the validated domain.

**Exit criterion.** Accepted spectra and observer mappings produce
reproducible enclosures containing independent reference integrations, with
spectral and colorimetric error budgets separated from transport error.

## M6: Adjoint camera importance and view-adaptive refinement

**Scope.** Add an explicit adjoint sensor model and use bounded camera
importance to prioritize deterministic spatial, angular, and spectral
refinement for requested views.

**Proof obligation.** Prove the primal/adjoint pairing for the discrete bases,
bound sensor projection and importance transport, and ensure prioritization
changes work ordering without discarding unresolved error. All image-domain
stopping decisions must remain conservative.

**Tests.** Check adjoint identities, multi-view consistency, zero-importance
regions, narrow sensor footprints, refinement-order determinism, and equality
of final enclosures under alternative valid schedules.

**Unsupported cases.** Unvalidated lens optics, depth of field, motion blur,
rolling shutter, adaptive heuristics that omit residual terms, and sensor
models outside the supported finite projection.

**Exit criterion.** For supported cameras, view-adaptive runs meet the same
proof contract as exhaustive refinement while emitting a complete accounting
of residual terms and deterministic scheduling decisions.

## M7: Sparse, block-low-rank, or wavelet operator compression

**Scope.** Add one or more explicitly selected operator representations that
reduce storage or application work for validated hierarchical systems.

**Proof obligation.** Enclose truncation, factorization, quantization, and
application errors for each representation. Compression decisions must be
deterministic, and the accumulated compression residual must enter the final
certificate.

**Tests.** Compare every compressed operation with the dense reference,
exercise rank and threshold boundaries, check deterministic serialization,
verify monotone error budgets, and preserve adversarial non-compressible
operators.

**Unsupported cases.** Unbounded approximate factorizations, learned
compression, performance claims without reproducible evidence, and silent
fallback between representations.

**Exit criterion.** At least one compressed representation passes parity and
containment tests on its documented domain, exposes its full approximation
budget, and can be disabled to reproduce the finite reference calculation.

## M8: Explicit delta and specular-chain subsolvers

**Scope.** Introduce separate representations and deterministic solvers for
perfect reflection/refraction and finite specular chains, coupled explicitly
to the regular transport system.

**Proof obligation.** Bound geometric constraints, branch selection, Fresnel
terms, throughput, termination, and coupling residuals. Distributional delta
transport must not be approximated by narrowing a regular glossy lobe without
a separate convergence proof.

**Tests.** Cover mirrors, planar refraction, total internal reflection,
multiple-interface chains, branch degeneracies, analytic path throughput, and
coupling to diffuse/regular-glossy components.

**Unsupported cases.** Caustic families requiring manifold search, rough
singular limits outside the regular-glossy domain, participating media, and
unbounded chain enumeration.

**Exit criterion.** Every accepted delta path belongs to an explicitly
enumerated and validated chain class, its contribution and missed-path bound
are emitted, and coupling preserves the global enclosure.

## M9: Caustic manifold or constraint subsolver

**Scope.** Add a dedicated solver for a restricted family of caustic paths
defined by specular constraints or manifolds.

**Proof obligation.** Prove root isolation or conservative coverage of all
supported solutions, bound Jacobians and throughput integration, handle
degeneracy, and account for every unresolved or pruned branch.

**Tests.** Use analytic lens and reflector configurations, multiple-solution
cases, grazing and focal degeneracies, interval root-isolation tests,
topological branch changes, and independent high-precision path references.

**Unsupported cases.** Constraint families without completeness guarantees,
arbitrary many-bounce caustics, diffraction, wave optics, and heuristic path
search presented as exhaustive.

**Exit criterion.** The documented caustic class has a deterministic coverage
argument, all isolated contributions are bounded, unresolved domains prevent
certification or widen a stated remainder, and regression cases cover known
degeneracies.

## M10: Participating media

**Scope.** Extend transport to a restricted class of absorbing, emitting, and
scattering media with explicit spatial, angular, and spectral representations.

**Proof obligation.** Bound transmittance, free-path integration, phase
functions, volume quadrature, boundary coupling, and multiple scattering.
State coefficient regularity and optical-depth limits required for
contraction or another convergence proof.

**Tests.** Include homogeneous analytic slabs, pure absorption and emission,
isotropic scattering, interface coupling, optical-depth extremes, spatial
refinement, and independent deterministic reference integration.

**Unsupported cases.** Media outside the validated regularity and coefficient
ranges, stochastic tracking as certificate evidence, fluorescence unless
added explicitly, polarization, and unresolved heterogeneous discontinuities.

**Exit criterion.** Supported media produce bounded transport and image
contributions with separated transmittance, quadrature, and solve errors, and
all surface-volume coupling terms participate in the global residual.

## M11: CPU/CUDA backend parity without fallback

**Scope.** Implement an explicitly selected CUDA backend for supported
operators while retaining the CPU binary64 reference path.

**Proof obligation.** Establish outward-rounded device operations, reduction
ordering, status equivalence, precision policy, and enclosure parity. A
device result may be wider than the CPU result but must contain the exact
finite result under the same assumptions.

**Tests.** Run shared conformance suites on CPU and CUDA, compare validation
statuses, check interval containment and deterministic repeatability, stress
subnormals and reduction order, inject device failures, and verify that no CPU
or lower-precision fallback occurs.

**Unsupported cases.** Hardware lacking the documented arithmetic semantics,
mixed precision without a separate proven policy, cross-device byte identity
without evidence, and automatic backend substitution.

**Exit criterion.** The CUDA backend passes the full mathematical contract and
failure suite on named CI or reproducible test hardware; backend selection is
explicit; and unavailable or failed CUDA execution terminates without
silently changing the algorithm.

## M12: End-to-end image certificates and target-PSNR stopping

**Scope.** Combine validated scene input, transport-class subsolvers,
hierarchical refinement, compression, spectral/colorimetric projection, and
backend execution into an end-to-end certificate for a documented scene and
camera domain.

**Proof obligation.** Aggregate geometry, visibility, quadrature, basis,
linear-solve, interval-rounding, path-class coupling, spectral, sensor,
compression, and display-domain errors without omission or double counting.
Target-PSNR stopping must be derived from the complete image error budget.
Proof validity and target attainment remain separate states at this level.

**Tests.** Include end-to-end manufactured scenes, analytic scenes, independent
high-precision references, every supported transport-class combination,
failure injection for each error source, deterministic target-boundary cases,
and reproducible multi-backend certificate verification.

**Unsupported cases.** Any scene feature, material, transport class, camera,
spectral domain, backend, or display conversion not explicitly admitted by
the certificate schema and runtime validation.

**Exit criterion.** A clean documented command either emits a machine-readable
end-to-end certificate whose assumptions cover every accepted input and whose
image bounds pass independent validation, or exits with a precise
machine-readable proof or target reason. Repeated runs in each supported
configuration reproduce all proof-bearing outputs.
