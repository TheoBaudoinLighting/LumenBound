#include "lumenbound/projection/projection.hpp"

#include <stdexcept>
#include <utility>

namespace lumenbound {

Projection::Projection(DenseMatrix weights)
    : weights_(std::move(weights)) {}

std::size_t Projection::pixel_count() const noexcept {
    return weights_.rows();
}

std::size_t Projection::coefficient_count() const noexcept {
    return weights_.columns();
}

const DenseMatrix& Projection::weights() const noexcept {
    return weights_;
}

ProjectionValidationReport Projection::validate(
    std::size_t expected_coefficient_count) const {
    if (weights_.rows() == 0 || weights_.columns() == 0 ||
        weights_.columns() != expected_coefficient_count) {
        return {ProjectionValidationCode::InvalidDimensions,
                "projection_dimensions_do_not_match_transport"};
    }
    if (!weights_.is_finite()) {
        return {ProjectionValidationCode::NonFiniteInput,
                "projection_contains_non_finite_value"};
    }
    if (!weights_.is_nonnegative()) {
        return {ProjectionValidationCode::NegativeProjection,
                "projection_contains_negative_value"};
    }
    return {ProjectionValidationCode::Valid, "projection_is_valid"};
}

DenseVector Projection::project(const DenseVector& coefficients) const {
    return weights_.multiply(coefficients);
}

std::vector<Interval> Projection::project(
    const DenseVector& lower, const DenseVector& upper) const {
    if (lower.size() != coefficient_count() ||
        upper.size() != coefficient_count()) {
        throw std::invalid_argument(
            "projection interval dimensions do not match");
    }

    std::vector<Interval> result;
    result.reserve(pixel_count());
    for (std::size_t pixel = 0; pixel < pixel_count(); ++pixel) {
        Interval sum = Interval::point(0.0);
        for (std::size_t coefficient = 0;
             coefficient < coefficient_count(); ++coefficient) {
            if (lower[coefficient] > upper[coefficient]) {
                throw std::invalid_argument(
                    "coefficient interval endpoints are reversed");
            }
            const Interval weight =
                Interval::point(weights_(pixel, coefficient));
            const Interval coefficient_interval(lower[coefficient],
                                                upper[coefficient]);
            sum = sum + (weight * coefficient_interval);
        }
        result.push_back(sum);
    }
    return result;
}

}  // namespace lumenbound
