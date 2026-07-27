#pragma once

#include "lumenbound/math/interval.hpp"

#include <optional>
#include <span>

namespace lumenbound {

enum class PsnrBoundKind {
    Unavailable,
    Finite,
    PositiveInfinity,
};

[[nodiscard]] const char* to_string(PsnrBoundKind kind) noexcept;

struct ImageMetricBounds {
    double mse_upper_bound;
    PsnrBoundKind psnr_lower_bound_kind;
    std::optional<double> psnr_lower_bound;
};

struct PsnrLowerBound {
    PsnrBoundKind kind;
    std::optional<double> value;
};

[[nodiscard]] double conservative_absolute_error(
    double candidate, const Interval& enclosure);

[[nodiscard]] double compute_mse_upper_bound(
    std::span<const double> absolute_error_upper_bounds);

[[nodiscard]] PsnrLowerBound compute_psnr_lower_bound(
    double mse_upper_bound, double signal_peak);

[[nodiscard]] ImageMetricBounds compute_image_metric_bounds(
    std::span<const double> absolute_error_upper_bounds,
    double signal_peak);

}  // namespace lumenbound
