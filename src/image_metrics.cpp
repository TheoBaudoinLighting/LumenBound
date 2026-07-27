#include "lumenbound/certification/image_metrics.hpp"

#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace lumenbound {
namespace {

[[nodiscard]] double absolute_difference_upper(double left, double right) {
    if (left >= right) {
        return math::subtract_up(left, right);
    }
    return math::subtract_up(right, left);
}

}  // namespace

const char* to_string(PsnrBoundKind kind) noexcept {
    switch (kind) {
    case PsnrBoundKind::Unavailable:
        return "unavailable";
    case PsnrBoundKind::Finite:
        return "finite";
    case PsnrBoundKind::PositiveInfinity:
        return "positive_infinity";
    }
    return "unavailable";
}

double conservative_absolute_error(double candidate,
                                   const Interval& enclosure) {
    if (!math::supports_certified_rounding()) {
        throw std::runtime_error(
            "absolute error bounds require supported binary64 arithmetic");
    }
    if (!std::isfinite(candidate)) {
        throw std::invalid_argument(
            "candidate value must be finite");
    }
    return std::max(
        absolute_difference_upper(candidate, enclosure.lower()),
        absolute_difference_upper(enclosure.upper(), candidate));
}

double compute_mse_upper_bound(
    std::span<const double> absolute_error_upper_bounds) {
    if (!math::supports_certified_rounding()) {
        throw std::runtime_error(
            "MSE bounds require supported binary64 arithmetic");
    }
    if (absolute_error_upper_bounds.empty()) {
        throw std::invalid_argument(
            "image metric evaluation requires at least one value");
    }
    double squared_error_sum = 0.0;
    for (const double error_bound : absolute_error_upper_bounds) {
        if (!std::isfinite(error_bound) || error_bound < 0.0) {
            throw std::invalid_argument(
                "image error bounds must be finite and nonnegative");
        }
        squared_error_sum =
            math::add_up(squared_error_sum,
                         math::multiply_up(error_bound, error_bound));
    }

    const std::size_t value_count =
        absolute_error_upper_bounds.size();
    constexpr std::uint64_t maximum_exact_integer =
        std::uint64_t{1} << 53U;
    if (static_cast<std::uint64_t>(value_count) >
        maximum_exact_integer) {
        throw std::runtime_error(
            "image value count is not exactly representable as double");
    }
    const double value_count_as_double =
        static_cast<double>(value_count);

    return math::divide_up(squared_error_sum, value_count_as_double);
}

PsnrLowerBound compute_psnr_lower_bound(double mse_upper_bound,
                                        double signal_peak) {
    if (!math::supports_certified_rounding()) {
        throw std::runtime_error(
            "PSNR bounds require supported binary64 arithmetic");
    }
    if (!std::isfinite(mse_upper_bound) || mse_upper_bound < 0.0) {
        throw std::invalid_argument(
            "PSNR evaluation requires a finite nonnegative MSE bound");
    }
    if (!std::isfinite(signal_peak) || signal_peak <= 0.0) {
        throw std::invalid_argument(
            "PSNR signal peak must be finite and positive");
    }
    if (mse_upper_bound == 0.0) {
        return {PsnrBoundKind::PositiveInfinity, std::nullopt};
    }

    const Interval peak_logarithm = certified_log10(signal_peak);
    const Interval mse_logarithm =
        certified_log10(mse_upper_bound);
    const Interval psnr =
        (Interval::point(20.0) * peak_logarithm) -
        (Interval::point(10.0) * mse_logarithm);
    return {PsnrBoundKind::Finite, psnr.lower()};
}

ImageMetricBounds compute_image_metric_bounds(
    std::span<const double> absolute_error_upper_bounds,
    double signal_peak) {
    const double mse_upper =
        compute_mse_upper_bound(absolute_error_upper_bounds);
    const PsnrLowerBound psnr =
        compute_psnr_lower_bound(mse_upper, signal_peak);
    return {mse_upper, psnr.kind, psnr.value};
}

}  // namespace lumenbound
