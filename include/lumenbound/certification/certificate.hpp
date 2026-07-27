#pragma once

#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/solver/candidate_solver.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace lumenbound {

enum class ProofStatus {
    Certified,
    Uncertified,
};

enum class ProofFailureCode {
    None,
    InvalidDimensions,
    NonFiniteInput,
    NegativeEmission,
    NegativeTransport,
    NegativeProjection,
    NonContractive,
    NumericalFailure,
};

enum class TargetStatus {
    NotEvaluated,
    Reached,
    IterationLimit,
    Stagnated,
    InvalidTarget,
};

[[nodiscard]] const char* to_string(ProofStatus status) noexcept;
[[nodiscard]] const char* to_string(ProofFailureCode code) noexcept;
[[nodiscard]] const char* to_string(TargetStatus status) noexcept;

struct BoundedValue {
    std::size_t band;
    std::size_t index;
    double candidate;
    double lower;
    double upper;
    double error_bound;
    ProofStatus proof_status;
};

struct IterationSnapshot {
    std::vector<DenseVector> lower;
    std::vector<DenseVector> upper;
};

struct Certificate {
    std::string schema_version{"lumenbound.certificate.v2"};
    std::string certificate_scope{
        "finite_dimensional_positive_binary64_transport"};
    std::string problem_digest;
    std::string solver_version;
    std::string arithmetic_policy{"binary64-outward-rounded-v1"};
    ProofStatus proof_status{ProofStatus::Uncertified};
    ProofFailureCode proof_failure{ProofFailureCode::NumericalFailure};
    std::string proof_reason{"proof_not_established"};
    TargetStatus target_status{TargetStatus::NotEvaluated};
    std::string target_reason{"proof_not_established"};
    std::size_t spectral_coefficient_count{0};
    std::size_t transport_coefficient_count{0};
    std::size_t pixel_count{0};
    std::optional<double> contraction_upper_bound;
    std::size_t interval_iteration_count{0};
    std::size_t maximum_iterations{0};
    bool iteration_snapshots_retained{false};
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
    bool retain_iteration_snapshots{false};
};

struct CertificationResult {
    Certificate certificate;
    CandidateSolution candidate;
    std::vector<IterationSnapshot> iterations;
};

}  // namespace lumenbound
