#include "lumenbound/certification/certifier.hpp"

#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/core/backend.hpp"
#include "lumenbound/math/interval.hpp"
#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace lumenbound {
namespace {

[[nodiscard]] CertificateStatus map_transport_status(
    TransportValidationCode code) {
    switch (code) {
    case TransportValidationCode::Valid:
        return CertificateStatus::Certified;
    case TransportValidationCode::InvalidDimensions:
        return CertificateStatus::UncertifiedInvalidDimensions;
    case TransportValidationCode::NonFiniteInput:
        return CertificateStatus::UncertifiedNonFiniteInput;
    case TransportValidationCode::NegativeEmission:
        return CertificateStatus::UncertifiedNegativeEmission;
    case TransportValidationCode::NegativeTransport:
        return CertificateStatus::UncertifiedNegativeTransport;
    case TransportValidationCode::NonContractive:
        return CertificateStatus::UncertifiedNonContractive;
    case TransportValidationCode::NumericalFailure:
        return CertificateStatus::NumericalFailure;
    }
    return CertificateStatus::NumericalFailure;
}

[[nodiscard]] CertificateStatus map_projection_status(
    ProjectionValidationCode code) {
    switch (code) {
    case ProjectionValidationCode::Valid:
        return CertificateStatus::Certified;
    case ProjectionValidationCode::InvalidDimensions:
        return CertificateStatus::UncertifiedInvalidDimensions;
    case ProjectionValidationCode::NonFiniteInput:
        return CertificateStatus::UncertifiedNonFiniteInput;
    case ProjectionValidationCode::NegativeProjection:
        return CertificateStatus::UncertifiedNegativeProjection;
    }
    return CertificateStatus::NumericalFailure;
}

void set_bound_status(Certificate& certificate) {
    for (BoundedValue& bound : certificate.coefficient_bounds) {
        bound.status = certificate.status;
    }
    for (BoundedValue& bound : certificate.pixel_bounds) {
        bound.status = certificate.status;
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
                             CertificateStatus::NumericalFailure});
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
                             CertificateStatus::NumericalFailure});
            pixel_error_bounds.push_back(error_bound);
        }
    }

    const ImageMetricBounds metrics =
        compute_image_metric_bounds(pixel_error_bounds,
                                    certificate.signal_peak);
    certificate.mse_upper_bound = metrics.mse_upper_bound;
    certificate.psnr_lower_bound_kind =
        metrics.psnr_lower_bound_kind;
    certificate.psnr_lower_bound = metrics.psnr_lower_bound;
}

[[nodiscard]] bool target_is_reached(const Certificate& certificate) {
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
        "spectral_coefficients_are_independent",
        "certification_requires_finite_componentwise_nonnegative_transport_and_projection",
        "certification_requires_a_conservative_transport_infinity_norm_bound_below_one",
        "certification_requires_preserved_binary64_subnormal_values",
        "certification_requires_fixed_order_outward_rounded_arithmetic",
        "binary64_bit_strings_are_authoritative_for_reported_bounds",
        "the_projection_targets_raw_linear_coefficients",
        "the_preview_display_mapping_is_not_certified",
    };
}

}  // namespace

const char* to_string(CertificateStatus status) noexcept {
    switch (status) {
    case CertificateStatus::Certified:
        return "Certified";
    case CertificateStatus::UncertifiedInvalidDimensions:
        return "UncertifiedInvalidDimensions";
    case CertificateStatus::UncertifiedNonFiniteInput:
        return "UncertifiedNonFiniteInput";
    case CertificateStatus::UncertifiedNegativeEmission:
        return "UncertifiedNegativeEmission";
    case CertificateStatus::UncertifiedNegativeTransport:
        return "UncertifiedNegativeTransport";
    case CertificateStatus::UncertifiedNegativeProjection:
        return "UncertifiedNegativeProjection";
    case CertificateStatus::UncertifiedNonContractive:
        return "UncertifiedNonContractive";
    case CertificateStatus::UncertifiedInvalidSignalPeak:
        return "UncertifiedInvalidSignalPeak";
    case CertificateStatus::UncertifiedInvalidTarget:
        return "UncertifiedInvalidTarget";
    case CertificateStatus::UncertifiedIterationLimit:
        return "UncertifiedIterationLimit";
    case CertificateStatus::UncertifiedTargetNotReached:
        return "UncertifiedTargetNotReached";
    case CertificateStatus::NumericalFailure:
        return "NumericalFailure";
    }
    return "NumericalFailure";
}

CertificationResult certify(const TransportSystem& system,
                            const Projection& projection,
                            const CertificationOptions& options) {
    CertificationResult result;
    Certificate& certificate = result.certificate;
    certificate.spectral_coefficient_count =
        system.spectral_coefficient_count();
    certificate.transport_coefficient_count =
        system.transport_coefficient_count();
    certificate.pixel_count = projection.pixel_count();
    certificate.signal_peak = options.signal_peak;
    certificate.target_psnr = options.target_psnr;
    certificate.assumptions = certificate_assumptions();

    const TransportValidationReport system_validation = system.validate();
    if (system_validation.valid() ||
        system_validation.code ==
            TransportValidationCode::NonContractive) {
        certificate.contraction_upper_bound =
            system_validation.contraction_upper_bound;
    }
    if (!system_validation.valid()) {
        certificate.status = map_transport_status(system_validation.code);
        certificate.reason = system_validation.reason;
        return result;
    }

    const ProjectionValidationReport projection_validation =
        projection.validate(system.transport_coefficient_count());
    if (!projection_validation.valid()) {
        certificate.status =
            map_projection_status(projection_validation.code);
        certificate.reason = projection_validation.reason;
        return result;
    }

    if (!std::isfinite(options.signal_peak) ||
        options.signal_peak <= 0.0) {
        certificate.status =
            CertificateStatus::UncertifiedInvalidSignalPeak;
        certificate.reason = "signal_peak_must_be_finite_and_positive";
        return result;
    }
    if (!std::isfinite(options.target_psnr)) {
        certificate.status = CertificateStatus::UncertifiedInvalidTarget;
        certificate.reason = "target_psnr_must_be_finite";
        return result;
    }

    double denominator_lower = 0.0;
    try {
        denominator_lower =
            math::subtract_down(
                1.0, *certificate.contraction_upper_bound);
    } catch (const std::exception&) {
        certificate.status = CertificateStatus::NumericalFailure;
        certificate.reason =
            "contraction_denominator_evaluation_failed";
        return result;
    }
    if (denominator_lower <= 0.0) {
        certificate.status =
            CertificateStatus::UncertifiedNonContractive;
        certificate.reason =
            "rounded_contraction_denominator_is_not_positive";
        return result;
    }

    try {
        result.candidate = solve_candidate(system);
    } catch (const std::exception&) {
        certificate.status = CertificateStatus::NumericalFailure;
        certificate.reason = "candidate_dense_solve_failed";
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

        result.iterations.push_back(IterationSnapshot{lower, upper});
        arithmetic_failure_reason =
            "image_bound_evaluation_failed";
        update_bounds_and_metrics(projection, result.candidate, lower, upper,
                                  certificate);
        if (target_is_reached(certificate)) {
            certificate.status = CertificateStatus::Certified;
            certificate.reason =
                "requested_psnr_lower_bound_reached";
            set_bound_status(certificate);
            return result;
        }

        if (options.maximum_iterations == 0) {
            certificate.status =
                CertificateStatus::UncertifiedIterationLimit;
            certificate.reason =
                "zero_iteration_budget_before_target";
            set_bound_status(certificate);
            return result;
        }

        for (std::size_t iteration = 1;
             iteration <= options.maximum_iterations; ++iteration) {
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
            result.iterations.push_back(
                IterationSnapshot{lower, upper});
            certificate.interval_iteration_count = iteration;
            arithmetic_failure_reason =
                "image_bound_evaluation_failed";
            update_bounds_and_metrics(projection, result.candidate, lower,
                                      upper, certificate);

            if (target_is_reached(certificate)) {
                certificate.status = CertificateStatus::Certified;
                certificate.reason =
                    "requested_psnr_lower_bound_reached";
                set_bound_status(certificate);
                return result;
            }

            if (!changed) {
                certificate.status =
                    CertificateStatus::UncertifiedTargetNotReached;
                certificate.reason =
                    "interval_propagation_stagnated_before_target";
                set_bound_status(certificate);
                return result;
            }
        }

        certificate.status =
            CertificateStatus::UncertifiedIterationLimit;
        certificate.reason =
            "maximum_interval_iterations_exhausted_before_target";
        set_bound_status(certificate);
        return result;
    } catch (const std::exception&) {
        certificate.status = CertificateStatus::NumericalFailure;
        certificate.reason = arithmetic_failure_reason;
        certificate.coefficient_bounds.clear();
        certificate.pixel_bounds.clear();
        certificate.residual_upper_bound.reset();
        certificate.candidate_error_upper_bound.reset();
        certificate.mse_upper_bound.reset();
        certificate.psnr_lower_bound_kind = PsnrBoundKind::Unavailable;
        certificate.psnr_lower_bound.reset();
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
