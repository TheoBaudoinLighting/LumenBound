#include "lumenbound/transport/transport_system.hpp"

#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cfenv>
#include <exception>
#include <utility>

namespace lumenbound {

TransportSystem::TransportSystem(
    std::vector<DenseVector> emissions,
    std::vector<DenseMatrix> transport_operators)
    : emissions_(std::move(emissions)),
      transport_operators_(std::move(transport_operators)) {}

std::size_t TransportSystem::spectral_coefficient_count() const noexcept {
    return emissions_.size();
}

std::size_t TransportSystem::transport_coefficient_count() const noexcept {
    return emissions_.empty() ? 0 : emissions_.front().size();
}

std::size_t TransportSystem::emission_band_count() const noexcept {
    return emissions_.size();
}

std::size_t TransportSystem::transport_operator_count() const noexcept {
    return transport_operators_.size();
}

const std::vector<DenseVector>& TransportSystem::emissions() const noexcept {
    return emissions_;
}

const std::vector<DenseMatrix>&
TransportSystem::transport_operators() const noexcept {
    return transport_operators_;
}

const DenseVector& TransportSystem::emission(std::size_t band) const {
    return emissions_.at(band);
}

const DenseMatrix& TransportSystem::transport(std::size_t band) const {
    return transport_operators_.at(band);
}

TransportValidationReport TransportSystem::validate() const {
    if (emissions_.empty() ||
        emissions_.size() != transport_operators_.size()) {
        return {TransportValidationCode::InvalidDimensions,
                "spectral_band_dimensions_are_invalid", 0.0};
    }

    const std::size_t coefficient_count = emissions_.front().size();
    if (coefficient_count == 0) {
        return {TransportValidationCode::InvalidDimensions,
                "transport_coefficient_count_must_be_positive", 0.0};
    }

    for (std::size_t band = 0; band < emissions_.size(); ++band) {
        if (emissions_[band].size() != coefficient_count ||
            transport_operators_[band].rows() != coefficient_count ||
            transport_operators_[band].columns() != coefficient_count) {
            return {TransportValidationCode::InvalidDimensions,
                    "transport_matrix_and_emission_dimensions_do_not_match",
                    0.0};
        }
    }

    for (const DenseVector& emission : emissions_) {
        if (!emission.is_finite()) {
            return {TransportValidationCode::NonFiniteInput,
                    "emission_contains_non_finite_value", 0.0};
        }
    }
    for (const DenseMatrix& transport : transport_operators_) {
        if (!transport.is_finite()) {
            return {TransportValidationCode::NonFiniteInput,
                    "transport_contains_non_finite_value", 0.0};
        }
    }

    for (const DenseVector& emission : emissions_) {
        if (!emission.is_nonnegative()) {
            return {TransportValidationCode::NegativeEmission,
                    "emission_contains_negative_value", 0.0};
        }
    }
    for (const DenseMatrix& transport : transport_operators_) {
        if (!transport.is_nonnegative()) {
            return {TransportValidationCode::NegativeTransport,
                    "transport_contains_negative_value", 0.0};
        }
    }

    if (std::fegetround() != FE_TONEAREST) {
        return {TransportValidationCode::NumericalFailure,
                "candidate_arithmetic_requires_round_to_nearest", 0.0};
    }
    if (!math::supports_certified_rounding()) {
        return {TransportValidationCode::NumericalFailure,
                "binary64_directed_rounding_is_unavailable", 0.0};
    }

    try {
        double contraction_upper_bound = 0.0;
        for (const DenseMatrix& transport : transport_operators_) {
            contraction_upper_bound =
                std::max(contraction_upper_bound,
                         transport.conservative_infinity_norm());
        }

        if (contraction_upper_bound >= 1.0) {
            return {TransportValidationCode::NonContractive,
                    "transport_infinity_norm_upper_bound_is_not_below_one",
                    contraction_upper_bound};
        }
        return {TransportValidationCode::Valid, "all_preconditions_satisfied",
                contraction_upper_bound};
    } catch (const std::exception&) {
        return {TransportValidationCode::NumericalFailure,
                "contraction_bound_evaluation_failed", 0.0};
    }
}

}  // namespace lumenbound
