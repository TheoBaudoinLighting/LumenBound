#pragma once

#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/solver/candidate_solver.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lumenbound {

enum class CertificateStatus {
    Certified,
    UncertifiedInvalidDimensions,
    UncertifiedNonFiniteInput,
    UncertifiedNegativeEmission,
    UncertifiedNegativeTransport,
    UncertifiedNegativeProjection,
    UncertifiedNonContractive,
    UncertifiedInvalidSignalPeak,
    UncertifiedInvalidTarget,
    UncertifiedIterationLimit,
    UncertifiedTargetNotReached,
    NumericalFailure,
};

[[nodiscard]] const char* to_string(CertificateStatus status) noexcept;

struct BoundedValue {
    std::size_t band;
    std::size_t index;
    double candidate;
    double lower;
    double upper;
    double error_bound;
    CertificateStatus status;
};

struct IterationSnapshot {
    std::vector<DenseVector> lower;
    std::vector<DenseVector> upper;
};

struct Certificate {
    std::string schema_version{"lumenbound.certificate.v1"};
    CertificateStatus status{CertificateStatus::NumericalFailure};
    std::string reason;
    std::size_t spectral_coefficient_count{0};
    std::size_t transport_coefficient_count{0};
    std::size_t pixel_count{0};
    std::optional<double> contraction_upper_bound;
    std::size_t interval_iteration_count{0};
    std::vector<BoundedValue> coefficient_bounds;
    std::vector<BoundedValue> pixel_bounds;
    std::optional<double> residual_upper_bound;
    std::optional<double> candidate_error_upper_bound;
    std::optional<double> mse_upper_bound;
    PsnrBoundKind psnr_lower_bound_kind{PsnrBoundKind::Unavailable};
    std::optional<double> psnr_lower_bound;
    double signal_peak{0.0};
    double target_psnr{0.0};
    std::vector<std::string> assumptions;
};

struct CertificationOptions {
    double signal_peak{1.0};
    double target_psnr{80.0};
    std::size_t maximum_iterations{512};
};

struct CertificationResult {
    Certificate certificate;
    CandidateSolution candidate;
    std::vector<IterationSnapshot> iterations;
};

}  // namespace lumenbound
