#include "lumenbound/certification/certifier.hpp"

#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/certification/problem_digest.hpp"
#include "lumenbound/core/backend.hpp"
#include "lumenbound/math/interval.hpp"
#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

#ifndef LUMENBOUND_SOLVER_VERSION
#error "LUMENBOUND_SOLVER_VERSION must be defined by the build"
#endif

namespace lumenbound {
namespace {

[[nodiscard]] ProofFailureCode map_transport_failure(
    TransportValidationCode code) {
    switch (code) {
    case TransportValidationCode::Valid:
        return ProofFailureCode::None;
    case TransportValidationCode::InvalidDimensions:
        return ProofFailureCode::InvalidDimensions;
    case TransportValidationCode::NonFiniteInput:
        return ProofFailureCode::NonFiniteInput;
    case TransportValidationCode::NegativeEmission:
        return ProofFailureCode::NegativeEmission;
    case TransportValidationCode::NegativeTransport:
        return ProofFailureCode::NegativeTransport;
    case TransportValidationCode::NonContractive:
        return ProofFailureCode::NonContractive;
    case TransportValidationCode::NumericalFailure:
        return ProofFailureCode::NumericalFailure;
    }
    return ProofFailureCode::NumericalFailure;
}

[[nodiscard]] ProofFailureCode map_projection_failure(
    ProjectionValidationCode code) {
    switch (code) {
    case ProjectionValidationCode::Valid:
        return ProofFailureCode::None;
    case ProjectionValidationCode::InvalidDimensions:
        return ProofFailureCode::InvalidDimensions;
    case ProjectionValidationCode::NonFiniteInput:
        return ProofFailureCode::NonFiniteInput;
    case ProjectionValidationCode::NegativeProjection:
        return ProofFailureCode::NegativeProjection;
    }
    return ProofFailureCode::NumericalFailure;
}

void mark_bound_proof_status(Certificate& certificate) {
    for (BoundedValue& bound : certificate.coefficient_bounds) {
        bound.proof_status = certificate.proof_status;
    }
    for (BoundedValue& bound : certificate.pixel_bounds) {
        bound.proof_status = certificate.proof_status;
    }
}

void set_proof_failure(Certificate& certificate,
                       ProofFailureCode failure,
                       std::string reason) {
    certificate.proof_status = ProofStatus::Uncertified;
    certificate.proof_failure = failure;
    certificate.proof_reason = std::move(reason);
    if (certificate.target_status != TargetStatus::InvalidTarget) {
        certificate.target_status = TargetStatus::NotEvaluated;
        certificate.target_reason = "proof_not_established";
    }
}

[[nodiscard]] double interval_absolute_upper(const Interval& interval) {
    return std::max(std::abs(interval.lower()),
                    std::abs(interval.upper()));
}

void compute_residual_certificate(
    const TransportSystem& system, const CandidateSolution& candidate,
    double denominator_lower, Certificate& certificate) {
    double residual_upper = 0.0;

    for (std::size_t band = 0;
         band < system.spectral_coefficient_count(); ++band) {
        const DenseMatrix& transport = system.transport(band);
        const DenseVector& emission = system.emission(band);
        const DenseVector& values = candidate.values.at(band);

        for (std::size_t row = 0;
             row < system.transport_coefficient_count(); ++row) {
            Interval residual = Interval::point(emission[row]);
            for (std::size_t column = 0;
                 column < system.transport_coefficient_count(); ++column) {
                residual =
                    residual +
                    (Interval::point(transport(row, column)) *
                     Interval::point(values[column]));
            }
            residual = residual - Interval::point(values[row]);
            residual_upper =
                std::max(residual_upper,
                         interval_absolute_upper(residual));
        }
    }

    certificate.residual_upper_bound = residual_upper;
    certificate.candidate_error_upper_bound =
        math::divide_up(residual_upper, denominator_lower);
}

void intersect_with_residual_enclosure(
    const CandidateSolution& candidate, double error_upper_bound,
    std::vector<DenseVector>& lower,
    std::vector<DenseVector>& upper) {
    for (std::size_t band = 0; band < lower.size(); ++band) {
        for (std::size_t coefficient = 0;
             coefficient < lower[band].size(); ++coefficient) {
            const double candidate_value =
                candidate.values.at(band)[coefficient];
            const Interval residual_enclosure(
                math::subtract_down(candidate_value, error_upper_bound),
                math::add_up(candidate_value, error_upper_bound));
            const std::optional<Interval> refined =
                Interval(lower[band][coefficient],
                         upper[band][coefficient])
                    .intersection(residual_enclosure);
            if (!refined.has_value()) {
                throw std::runtime_error(
                    "residual enclosure intersection is empty");
            }
            lower[band][coefficient] = refined->lower();
            upper[band][coefficient] = refined->upper();
        }
    }
}

void retain_snapshot_if_requested(
    const CertificationOptions& options,
    const std::vector<DenseVector>& lower,
    const std::vector<DenseVector>& upper,
    CertificationResult& result) {
    if (options.retain_iteration_snapshots) {
        result.iterations.push_back(IterationSnapshot{lower, upper});
    }
}

void update_bounds_and_metrics(
    const Projection& projection, const CandidateSolution& candidate,
    const std::vector<DenseVector>& lower,
    const std::vector<DenseVector>& upper, Certificate& certificate) {
    certificate.coefficient_bounds.clear();
    certificate.pixel_bounds.clear();

    const std::size_t band_count = lower.size();
    for (std::size_t band = 0; band < band_count; ++band) {
        for (std::size_t coefficient = 0;
             coefficient < lower[band].size(); ++coefficient) {
            const double candidate_value =
                candidate.values.at(band)[coefficient];
            const double error_bound =
                conservative_absolute_error(
                    candidate_value,
                    Interval(lower[band][coefficient],
                             upper[band][coefficient]));
            certificate.coefficient_bounds.push_back(
                BoundedValue{band, coefficient, candidate_value,
                             lower[band][coefficient],
                             upper[band][coefficient], error_bound,
                             ProofStatus::Uncertified});
        }
    }

    std::vector<double> pixel_error_bounds;
    pixel_error_bounds.reserve(
        band_count * projection.pixel_count());
    for (std::size_t band = 0; band < band_count; ++band) {
        const DenseVector projected_candidate =
            projection.project(candidate.values.at(band));
        if (!projected_candidate.is_finite()) {
            throw std::runtime_error(
                "candidate projection produced a non-finite value");
        }
        const std::vector<Interval> projected_bounds =
            projection.project(lower[band], upper[band]);

        for (std::size_t pixel = 0; pixel < projection.pixel_count();
             ++pixel) {
            const double candidate_value = projected_candidate[pixel];
            const Interval& bounds = projected_bounds[pixel];
            const double error_bound =
                conservative_absolute_error(candidate_value, bounds);
            certificate.pixel_bounds.push_back(
                BoundedValue{band, pixel, candidate_value, bounds.lower(),
                             bounds.upper(), error_bound,
                             ProofStatus::Uncertified});
            pixel_error_bounds.push_back(error_bound);
        }
    }

    certificate.mse_upper_bound =
        compute_mse_upper_bound(pixel_error_bounds);
    certificate.psnr_lower_bound_kind = PsnrBoundKind::Unavailable;
    certificate.psnr_lower_bound.reset();
    if (std::isfinite(certificate.signal_peak) &&
        certificate.signal_peak > 0.0) {
        const PsnrLowerBound psnr = compute_psnr_lower_bound(
            *certificate.mse_upper_bound, certificate.signal_peak);
        certificate.psnr_lower_bound_kind = psnr.kind;
        certificate.psnr_lower_bound = psnr.value;
    }
}

[[nodiscard]] bool target_is_reached(const Certificate& certificate) {
    if (certificate.target_status == TargetStatus::InvalidTarget) {
        return false;
    }
    if (certificate.psnr_lower_bound_kind ==
        PsnrBoundKind::PositiveInfinity) {
        return true;
    }
    return certificate.psnr_lower_bound_kind == PsnrBoundKind::Finite &&
           certificate.psnr_lower_bound.has_value() &&
           *certificate.psnr_lower_bound >= certificate.target_psnr;
}

[[nodiscard]] std::vector<std::string> certificate_assumptions() {
    return {
        "finite_input_values_are_interpreted_as_exact_binary64_numbers",
        "coefficient_bands_are_independent_and_positionally_ordered",
        "certification_requires_componentwise_nonnegative_emission",
        "certification_requires_finite_componentwise_nonnegative_transport_and_projection",
        "certification_requires_a_conservative_transport_infinity_norm_bound_below_one",
        "certification_requires_preserved_binary64_subnormal_values",
        "certification_requires_source_precision_evaluation_without_operation_contraction",
        "certification_requires_fixed_order_outward_rounded_arithmetic",
        "the_residual_enclosure_is_intersected_with_the_positive_transport_enclosure",
        "binary64_bit_strings_are_authoritative_for_reported_bounds",
        "the_projection_targets_raw_linear_coefficients",
        "display_preview_conversion_is_outside_the_certificate",
    };
}

}  // namespace

const char* to_string(ProofStatus status) noexcept {
    switch (status) {
    case ProofStatus::Certified:
        return "Certified";
    case ProofStatus::Uncertified:
        return "Uncertified";
    }
    return "Uncertified";
}

const char* to_string(ProofFailureCode code) noexcept {
    switch (code) {
    case ProofFailureCode::None:
        return "None";
    case ProofFailureCode::InvalidDimensions:
        return "InvalidDimensions";
    case ProofFailureCode::NonFiniteInput:
        return "NonFiniteInput";
    case ProofFailureCode::NegativeEmission:
        return "NegativeEmission";
    case ProofFailureCode::NegativeTransport:
        return "NegativeTransport";
    case ProofFailureCode::NegativeProjection:
        return "NegativeProjection";
    case ProofFailureCode::NonContractive:
        return "NonContractive";
    case ProofFailureCode::NumericalFailure:
        return "NumericalFailure";
    }
    return "NumericalFailure";
}

const char* to_string(TargetStatus status) noexcept {
    switch (status) {
    case TargetStatus::NotEvaluated:
        return "NotEvaluated";
    case TargetStatus::Reached:
        return "Reached";
    case TargetStatus::IterationLimit:
        return "IterationLimit";
    case TargetStatus::Stagnated:
        return "Stagnated";
    case TargetStatus::InvalidTarget:
        return "InvalidTarget";
    }
    return "NotEvaluated";
}

CertificationResult certify(const TransportSystem& system,
                            const Projection& projection,
                            const CertificationOptions& options) {
    CertificationResult result;
    Certificate& certificate = result.certificate;
    certificate.solver_version = LUMENBOUND_SOLVER_VERSION;
    certificate.spectral_coefficient_count =
        system.spectral_coefficient_count();
    certificate.transport_coefficient_count =
        system.transport_coefficient_count();
    certificate.pixel_count = projection.pixel_count();
    certificate.signal_peak = options.signal_peak;
    certificate.target_psnr = options.target_psnr;
    certificate.maximum_iterations = options.maximum_iterations;
    certificate.iteration_snapshots_retained =
        options.retain_iteration_snapshots;
    certificate.assumptions = certificate_assumptions();

    if (!std::isfinite(options.signal_peak) ||
        options.signal_peak <= 0.0) {
        certificate.target_status = TargetStatus::InvalidTarget;
        certificate.target_reason =
            "signal_peak_must_be_finite_and_positive";
    } else if (!std::isfinite(options.target_psnr)) {
        certificate.target_status = TargetStatus::InvalidTarget;
        certificate.target_reason = "target_psnr_must_be_finite";
    }

    try {
        certificate.problem_digest =
            compute_problem_digest(system, projection, options);
    } catch (const std::exception&) {
        set_proof_failure(certificate,
                          ProofFailureCode::NumericalFailure,
                          "problem_digest_evaluation_failed");
        return result;
    }

    const TransportValidationReport system_validation = system.validate();
    if (system_validation.valid() ||
        system_validation.code ==
            TransportValidationCode::NonContractive) {
        certificate.contraction_upper_bound =
            system_validation.contraction_upper_bound;
    }
    if (!system_validation.valid()) {
        set_proof_failure(
            certificate, map_transport_failure(system_validation.code),
            system_validation.reason);
        return result;
    }

    const ProjectionValidationReport projection_validation =
        projection.validate(system.transport_coefficient_count());
    if (!projection_validation.valid()) {
        set_proof_failure(
            certificate, map_projection_failure(projection_validation.code),
            projection_validation.reason);
        return result;
    }

    double denominator_lower = 0.0;
    try {
        denominator_lower =
            math::subtract_down(
                1.0, *certificate.contraction_upper_bound);
    } catch (const std::exception&) {
        set_proof_failure(
            certificate, ProofFailureCode::NumericalFailure,
            "contraction_denominator_evaluation_failed");
        return result;
    }
    if (denominator_lower <= 0.0) {
        set_proof_failure(
            certificate, ProofFailureCode::NonContractive,
            "rounded_contraction_denominator_is_not_positive");
        return result;
    }

    try {
        result.candidate = solve_candidate(system);
    } catch (const std::exception&) {
        set_proof_failure(certificate,
                          ProofFailureCode::NumericalFailure,
                          "candidate_dense_solve_failed");
        return result;
    }

    std::string arithmetic_failure_reason =
        "residual_bound_evaluation_failed";
    try {
        compute_residual_certificate(
            system, result.candidate, denominator_lower, certificate);

        arithmetic_failure_reason =
            "initial_enclosure_construction_failed";
        std::vector<DenseVector> lower;
        std::vector<DenseVector> upper;
        lower.reserve(system.spectral_coefficient_count());
        upper.reserve(system.spectral_coefficient_count());

        for (std::size_t band = 0;
             band < system.spectral_coefficient_count(); ++band) {
            const DenseVector& emission = system.emission(band);
            const double maximum_emission =
                *std::max_element(emission.values().begin(),
                                  emission.values().end());
            const double uniform_upper =
                math::divide_up(maximum_emission, denominator_lower);
            lower.emplace_back(system.transport_coefficient_count(), 0.0);
            upper.emplace_back(system.transport_coefficient_count(),
                               uniform_upper);
        }

        arithmetic_failure_reason =
            "residual_enclosure_intersection_failed";
        intersect_with_residual_enclosure(
            result.candidate,
            *certificate.candidate_error_upper_bound, lower, upper);
        retain_snapshot_if_requested(options, lower, upper, result);

        arithmetic_failure_reason =
            "image_bound_evaluation_failed";
        update_bounds_and_metrics(projection, result.candidate, lower, upper,
                                  certificate);
        certificate.proof_status = ProofStatus::Certified;
        certificate.proof_failure = ProofFailureCode::None;
        certificate.proof_reason = "all_proof_obligations_satisfied";
        mark_bound_proof_status(certificate);

        if (certificate.target_status == TargetStatus::InvalidTarget) {
            return result;
        }
        if (target_is_reached(certificate)) {
            certificate.target_status = TargetStatus::Reached;
            certificate.target_reason =
                "requested_psnr_lower_bound_reached";
            return result;
        }

        if (options.maximum_iterations == 0) {
            certificate.target_status = TargetStatus::IterationLimit;
            certificate.target_reason =
                "zero_iteration_budget_before_target";
            return result;
        }

        for (std::size_t iteration_index = 0;
             iteration_index < options.maximum_iterations;
             ++iteration_index) {
            const std::size_t iteration = iteration_index + 1U;
            arithmetic_failure_reason =
                "interval_propagation_failed";
            std::vector<DenseVector> next_lower;
            std::vector<DenseVector> next_upper;
            next_lower.reserve(system.spectral_coefficient_count());
            next_upper.reserve(system.spectral_coefficient_count());
            bool changed = false;

            for (std::size_t band = 0;
                 band < system.spectral_coefficient_count(); ++band) {
                const DenseMatrix& transport = system.transport(band);
                const DenseVector& emission = system.emission(band);
                DenseVector band_lower(
                    system.transport_coefficient_count(), 0.0);
                DenseVector band_upper(
                    system.transport_coefficient_count(), 0.0);

                for (std::size_t row = 0;
                     row < system.transport_coefficient_count(); ++row) {
                    Interval evaluated = Interval::point(emission[row]);
                    for (std::size_t column = 0;
                         column <
                         system.transport_coefficient_count();
                         ++column) {
                        const Interval coefficient_interval(
                            lower[band][column],
                            upper[band][column]);
                        evaluated =
                            evaluated +
                            (Interval::point(
                                 transport(row, column)) *
                             coefficient_interval);
                    }

                    const double stored_lower =
                        std::max(lower[band][row],
                                 evaluated.lower());
                    const double stored_upper =
                        std::min(upper[band][row],
                                 evaluated.upper());
                    if (stored_lower > stored_upper) {
                        arithmetic_failure_reason =
                            "interval_intersection_was_empty";
                        throw std::runtime_error(
                            "interval intersection is empty");
                    }

                    band_lower[row] = stored_lower;
                    band_upper[row] = stored_upper;
                    changed =
                        changed ||
                        stored_lower != lower[band][row] ||
                        stored_upper != upper[band][row];
                }

                next_lower.push_back(std::move(band_lower));
                next_upper.push_back(std::move(band_upper));
            }

            lower = std::move(next_lower);
            upper = std::move(next_upper);
            retain_snapshot_if_requested(options, lower, upper, result);
            certificate.interval_iteration_count = iteration;
            arithmetic_failure_reason =
                "image_bound_evaluation_failed";
            update_bounds_and_metrics(projection, result.candidate, lower,
                                      upper, certificate);
            mark_bound_proof_status(certificate);

            if (target_is_reached(certificate)) {
                certificate.target_status = TargetStatus::Reached;
                certificate.target_reason =
                    "requested_psnr_lower_bound_reached";
                return result;
            }

            if (!changed) {
                certificate.target_status = TargetStatus::Stagnated;
                certificate.target_reason =
                    "interval_propagation_stagnated_before_target";
                return result;
            }

            // The last permitted iteration must return here. Incrementing a
            // size_t counter at its maximum would restart the loop at zero.
            if (iteration == options.maximum_iterations) {
                certificate.target_status = TargetStatus::IterationLimit;
                certificate.target_reason =
                    "maximum_interval_iterations_exhausted_before_target";
                return result;
            }
        }

        certificate.target_status = TargetStatus::IterationLimit;
        certificate.target_reason =
            "maximum_interval_iterations_exhausted_before_target";
        return result;
    } catch (const std::exception&) {
        set_proof_failure(certificate,
                          ProofFailureCode::NumericalFailure,
                          arithmetic_failure_reason);
        certificate.coefficient_bounds.clear();
        certificate.pixel_bounds.clear();
        certificate.residual_upper_bound.reset();
        certificate.candidate_error_upper_bound.reset();
        certificate.mse_upper_bound.reset();
        certificate.psnr_lower_bound_kind = PsnrBoundKind::Unavailable;
        certificate.psnr_lower_bound.reset();
        result.iterations.clear();
        return result;
    }
}

std::string_view CpuReferenceBackend::name() const noexcept {
    return "cpu-reference";
}

CertificationResult CpuReferenceBackend::run(
    const TransportSystem& system, const Projection& projection,
    const CertificationOptions& options) const {
    return certify(system, projection, options);
}

}  // namespace lumenbound
