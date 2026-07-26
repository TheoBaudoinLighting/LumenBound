#include "lumenbound/certification/certifier.hpp"
#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/core/demo.hpp"
#include "lumenbound/io/output.hpp"
#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/math/interval.hpp"
#include "lumenbound/math/rounding.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/solver/candidate_solver.hpp"
#include "lumenbound/spectrum/spectrum.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cmath>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

[[noreturn]] void fail(std::string_view message) {
    throw TestFailure(std::string(message));
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

template <typename ExceptionType, typename Callable>
void require_throws(Callable&& callable, std::string_view message) {
    try {
        std::invoke(std::forward<Callable>(callable));
    } catch (const ExceptionType&) {
        return;
    } catch (const std::exception&) {
        fail(message);
    } catch (...) {
        fail(message);
    }
    fail(message);
}

void require_contains_long_double(const lumenbound::Interval& interval,
                                  long double value,
                                  std::string_view message) {
    const long double lower =
        static_cast<long double>(interval.lower());
    const long double upper =
        static_cast<long double>(interval.upper());
    require(lower <= value && value <= upper, message);
}

void require_status(const lumenbound::CertificationResult& result,
                    lumenbound::CertificateStatus expected,
                    std::string_view message) {
    require(result.certificate.status == expected, message);
    require(!result.certificate.reason.empty(),
            "an uncertified result must include a reason");
}

[[nodiscard]] lumenbound::TransportSystem make_scalar_system(
    double emission, double transport) {
    return lumenbound::TransportSystem(
        std::vector<lumenbound::DenseVector>{
            lumenbound::DenseVector{emission}},
        std::vector<lumenbound::DenseMatrix>{
            lumenbound::DenseMatrix(1, 1, {transport})});
}

[[nodiscard]] lumenbound::Projection make_scalar_projection(double weight) {
    return lumenbound::Projection(
        lumenbound::DenseMatrix(1, 1, {weight}));
}

[[nodiscard]] const lumenbound::BoundedValue& find_bound(
    const std::vector<lumenbound::BoundedValue>& bounds,
    std::size_t band, std::size_t index) {
    const auto found = std::find_if(
        bounds.begin(), bounds.end(),
        [band, index](const lumenbound::BoundedValue& bound) {
            return bound.band == band && bound.index == index;
        });
    if (found == bounds.end()) {
        fail("expected bounded value is missing");
    }
    return *found;
}

[[nodiscard]] double maximum_snapshot_width(
    const lumenbound::IterationSnapshot& snapshot) {
    require(snapshot.lower.size() == snapshot.upper.size(),
            "snapshot band dimensions must match");
    double maximum_width = 0.0;
    for (std::size_t band = 0; band < snapshot.lower.size(); ++band) {
        require(snapshot.lower[band].size() ==
                    snapshot.upper[band].size(),
                "snapshot coefficient dimensions must match");
        for (std::size_t coefficient = 0;
             coefficient < snapshot.lower[band].size(); ++coefficient) {
            maximum_width = std::max(
                maximum_width,
                snapshot.upper[band][coefficient] -
                    snapshot.lower[band][coefficient]);
        }
    }
    return maximum_width;
}

[[nodiscard]] std::string read_binary_file(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "expected output file is missing");
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

class TemporaryDirectory final {
public:
    explicit TemporaryDirectory(std::filesystem::path path)
        : path_(std::move(path)) {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path_, error));
        require(!error, "failed to clear the temporary test directory");
        static_cast<void>(
            std::filesystem::create_directories(path_, error));
        require(!error, "failed to create the temporary test directory");
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code error;
        static_cast<void>(std::filesystem::remove_all(path_, error));
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class RoundingModeGuard final {
public:
    RoundingModeGuard()
        : original_(std::fegetround()) {
        require(original_ != -1,
                "the current floating-point rounding mode is unavailable");
    }

    RoundingModeGuard(const RoundingModeGuard&) = delete;
    RoundingModeGuard& operator=(const RoundingModeGuard&) = delete;

    ~RoundingModeGuard() {
        static_cast<void>(std::fesetround(original_));
    }

private:
    int original_;
};

void test_interval_operations_contain_scalar_references() {
    const double left_value = std::nextafter(1.0, 2.0);
    const double right_value = 0.1;
    const lumenbound::Interval left = lumenbound::Interval::point(left_value);
    const lumenbound::Interval right =
        lumenbound::Interval::point(right_value);

    require_contains_long_double(
        left + right,
        static_cast<long double>(left_value) +
            static_cast<long double>(right_value),
        "interval addition excluded the extended-precision reference");
    require_contains_long_double(
        left - right,
        static_cast<long double>(left_value) -
            static_cast<long double>(right_value),
        "interval subtraction excluded the extended-precision reference");
    require_contains_long_double(
        left * right,
        static_cast<long double>(left_value) *
            static_cast<long double>(right_value),
        "interval multiplication excluded the extended-precision reference");
    require_contains_long_double(
        left / right,
        static_cast<long double>(left_value) /
            static_cast<long double>(right_value),
        "interval division excluded the extended-precision reference");

    const lumenbound::Interval mixed(-1.25, 0.75);
    const lumenbound::Interval positive(0.2, 0.4);
    const lumenbound::Interval product = mixed * positive;
    require_contains_long_double(product, -0.5L,
                                 "interval product excluded its lower corner");
    require_contains_long_double(product, 0.3L,
                                 "interval product excluded its upper corner");

    const std::optional<lumenbound::Interval> overlap =
        mixed.intersection(lumenbound::Interval(-0.5, 1.0));
    require(overlap.has_value(), "overlapping intervals did not intersect");
    require(overlap->lower() == -0.5 && overlap->upper() == 0.75,
            "interval intersection returned incorrect endpoints");
    require(!mixed.intersection(lumenbound::Interval(1.0, 2.0)).has_value(),
            "disjoint intervals produced an intersection");
    require(mixed.contains(0.0) && mixed.contains(*overlap),
            "interval containment rejected enclosed values");
    require(mixed.is_finite() && mixed.width() >= 2.0,
            "interval diagnostics returned an invalid result");
    require(mixed.midpoint() == -0.25,
            "interval diagnostic midpoint is incorrect");
    require_throws<std::domain_error>(
        [&left]() {
            static_cast<void>(
                left / lumenbound::Interval(-1.0, 1.0));
        },
        "division by an interval containing zero was not rejected");
}

void test_outward_rounding_edge_cases() {
    const double half_ulp = 0x1p-53;
    const lumenbound::Interval halfway =
        lumenbound::Interval::point(1.0) +
        lumenbound::Interval::point(half_ulp);
    const long double halfway_reference =
        1.0L + static_cast<long double>(half_ulp);
    require_contains_long_double(
        halfway, halfway_reference,
        "half-ulp addition was rounded inward");
    require(halfway.lower() < halfway.upper(),
            "half-ulp addition did not produce a nonzero enclosure");

    const double source_lower = -0.25;
    const double source_upper = 0.75;
    const lumenbound::Interval widened =
        lumenbound::Interval::outward(source_lower, source_upper);
    require(widened.lower() < source_lower &&
                widened.upper() > source_upper,
            "outward construction did not widen both finite endpoints");

    const double numerator = std::numeric_limits<double>::min();
    const double denominator = 3.0;
    const lumenbound::Interval quotient =
        lumenbound::Interval::point(numerator) /
        lumenbound::Interval::point(denominator);
    require_contains_long_double(
        quotient,
        static_cast<long double>(numerator) /
            static_cast<long double>(denominator),
        "subnormal division edge case was rounded inward");

    require_throws<std::invalid_argument>(
        []() {
            static_cast<void>(lumenbound::Interval::outward(
                1.0, -1.0));
        },
        "reversed outward endpoints were not rejected");
}

void test_nonfinite_fast_paths_are_rejected() {
    const double infinity = std::numeric_limits<double>::infinity();
    const double not_a_number =
        std::numeric_limits<double>::quiet_NaN();

    require_throws<std::invalid_argument>(
        [infinity]() {
            static_cast<void>(
                lumenbound::math::add_up(0.0, infinity));
        },
        "zero-addition fast path accepted infinity");
    require_throws<std::invalid_argument>(
        [infinity]() {
            static_cast<void>(
                lumenbound::math::multiply_down(0.0, infinity));
        },
        "zero-multiplication fast path accepted infinity");
    require_throws<std::invalid_argument>(
        [not_a_number]() {
            static_cast<void>(
                lumenbound::math::multiply_up(1.0, not_a_number));
        },
        "identity-multiplication fast path accepted NaN");
    require_throws<std::invalid_argument>(
        [infinity]() {
            static_cast<void>(
                lumenbound::math::divide_down(0.0, infinity));
        },
        "zero-division fast path accepted infinity");
    require_throws<std::invalid_argument>(
        [not_a_number]() {
            static_cast<void>(
                lumenbound::Interval::point(not_a_number));
        },
        "interval point construction accepted NaN");
}

void test_certified_logarithm_known_values() {
    const lumenbound::Interval logarithm_one =
        lumenbound::certified_log10(1.0);
    const lumenbound::Interval logarithm_ten =
        lumenbound::certified_log10(10.0);
    require(logarithm_one.contains(0.0),
            "certified log10(1) excluded zero");
    require(logarithm_ten.contains(1.0),
            "certified log10(10) excluded one");
}

void test_rounding_mode_is_preserved() {
    RoundingModeGuard guard;
    require(std::fesetround(FE_DOWNWARD) == 0,
            "failed to select downward rounding for the preservation test");

    const lumenbound::Interval result =
        lumenbound::Interval::point(0.1) +
        lumenbound::Interval::point(0.2);
    require(result.contains(0.3),
            "directed interval addition returned an invalid enclosure");
    require(std::fegetround() == FE_DOWNWARD,
            "interval arithmetic changed the caller rounding mode");

    require_throws<std::domain_error>(
        []() {
            static_cast<void>(
                lumenbound::math::divide_up(1.0, 0.0));
        },
        "certified division by zero did not fail");
    require(std::fegetround() == FE_DOWNWARD,
            "failed certified arithmetic changed the caller rounding mode");
}

void test_dense_algebra_and_spectrum_order() {
    const lumenbound::DenseMatrix matrix(
        2, 3, {1.0, -2.0, 3.0, -0.5, 0.25, -0.125});
    require(matrix.infinity_norm() == 6.0,
            "matrix infinity norm has an incorrect row sum");
    require(matrix.conservative_infinity_norm() >= 6.0,
            "conservative matrix norm underestimated the exact row sum");

    const lumenbound::DenseVector vector{-2.0, 3.0, -4.0};
    require(vector.infinity_norm() == 4.0,
            "vector infinity norm is incorrect");
    require(vector.is_finite() && !vector.is_nonnegative(),
            "vector validation returned an incorrect result");
    const lumenbound::DenseVector product = matrix.multiply(vector);
    require(product[0] == -20.0 && product[1] == 2.25,
            "row-major matrix-vector multiplication is incorrect");

    const lumenbound::Spectrum<3> first{0.25, 0.5, 0.75};
    const lumenbound::Spectrum<3> second{1.0, 2.0, 3.0};
    const lumenbound::Spectrum<3> combined = first + (second * 0.5);
    require(combined.size() == 3 && combined[0] == 0.75 &&
                combined[1] == 1.5 && combined[2] == 2.25,
            "spectrum arithmetic changed coefficient order");
    require_throws<std::invalid_argument>(
        []() {
            static_cast<void>(
                lumenbound::Spectrum<3>{1.0, 2.0});
        },
        "spectrum accepted an incorrect coefficient count");
}

void test_candidate_solver_matches_manufactured_solution() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CandidateSolution candidate =
        lumenbound::solve_candidate(problem.system);
    constexpr double candidate_tolerance =
        128.0 * std::numeric_limits<double>::epsilon();

    require(candidate.values.size() == problem.exact_coefficients.size(),
            "candidate band count does not match the manufactured system");
    for (std::size_t band = 0; band < candidate.values.size(); ++band) {
        for (std::size_t coefficient = 0;
             coefficient < candidate.values[band].size(); ++coefficient) {
            require(std::abs(candidate.values[band][coefficient] -
                             problem.exact_coefficients[band][coefficient]) <=
                        candidate_tolerance,
                    "candidate solve exceeded its binary64 test tolerance");
        }
    }
    require(candidate.minimum_absolute_pivot > 0.0,
            "candidate solve did not report a positive pivot diagnostic");
    require(candidate.nearest_residual_infinity_norm <= candidate_tolerance,
            "candidate residual exceeded its binary64 test tolerance");
}

void test_iteration_snapshots_are_monotone_and_contain_exact_solution() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});

    require(result.certificate.status ==
                lumenbound::CertificateStatus::Certified,
            "the manufactured system was not certified");
    require(!result.iterations.empty(),
            "certification did not retain its interval snapshots");
    require(result.iterations.size() ==
                result.certificate.interval_iteration_count + 1U,
            "iteration count does not match the retained snapshots");

    for (std::size_t iteration = 0;
         iteration < result.iterations.size(); ++iteration) {
        const lumenbound::IterationSnapshot& snapshot =
            result.iterations[iteration];
        require(snapshot.lower.size() ==
                    problem.exact_coefficients.size(),
                "snapshot band count is incorrect");
        for (std::size_t band = 0;
             band < problem.exact_coefficients.size(); ++band) {
            for (std::size_t coefficient = 0;
                 coefficient <
                 problem.exact_coefficients[band].size();
                 ++coefficient) {
                const double exact =
                    problem.exact_coefficients[band][coefficient];
                require(snapshot.lower[band][coefficient] <= exact &&
                            exact <=
                                snapshot.upper[band][coefficient],
                        "a snapshot excluded the manufactured exact solution");
                require(snapshot.lower[band][coefficient] <=
                            snapshot.upper[band][coefficient],
                        "a snapshot contains reversed endpoints");

                if (iteration != 0) {
                    const lumenbound::IterationSnapshot& previous =
                        result.iterations[iteration - 1U];
                    require(snapshot.lower[band][coefficient] >=
                                previous.lower[band][coefficient],
                            "a certified lower endpoint decreased");
                    require(snapshot.upper[band][coefficient] <=
                                previous.upper[band][coefficient],
                            "a certified upper endpoint increased");
                }
            }
        }
    }
}

void test_iteration_width_contracts() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});
    require(result.iterations.size() > 1,
            "the manufactured enclosure did not perform an update");

    double previous_width =
        maximum_snapshot_width(result.iterations.front());
    for (std::size_t iteration = 1;
         iteration < result.iterations.size(); ++iteration) {
        const double current_width =
            maximum_snapshot_width(result.iterations[iteration]);
        require(current_width <= previous_width,
                "maximum interval width increased during propagation");
        previous_width = current_width;
    }
    require(maximum_snapshot_width(result.iterations.back()) <
                maximum_snapshot_width(result.iterations.front()),
            "the manufactured interval width did not contract");
}

void test_residual_certificate_contains_measured_errors() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});

    long double maximum_candidate_error = 0.0L;
    long double maximum_residual = 0.0L;
    for (std::size_t band = 0;
         band < problem.exact_coefficients.size(); ++band) {
        const lumenbound::DenseVector& values =
            result.candidate.values[band];
        const lumenbound::DenseVector& exact =
            problem.exact_coefficients[band];
        const lumenbound::DenseVector& emission =
            problem.system.emission(band);
        const lumenbound::DenseMatrix& transport =
            problem.system.transport(band);

        for (std::size_t row = 0; row < values.size(); ++row) {
            maximum_candidate_error = std::max(
                maximum_candidate_error,
                std::abs(static_cast<long double>(values[row]) -
                         static_cast<long double>(exact[row])));

            long double residual =
                static_cast<long double>(emission[row]);
            for (std::size_t column = 0;
                 column < values.size(); ++column) {
                residual +=
                    static_cast<long double>(transport(row, column)) *
                    static_cast<long double>(values[column]);
            }
            residual -= static_cast<long double>(values[row]);
            maximum_residual =
                std::max(maximum_residual, std::abs(residual));
        }
    }

    require(static_cast<long double>(
                *result.certificate.residual_upper_bound) >=
                maximum_residual,
            "residual certificate underestimated the measured residual");
    require(static_cast<long double>(
                *result.certificate.candidate_error_upper_bound) >=
                maximum_candidate_error,
            "residual certificate excluded the measured candidate error");
}

void test_signed_candidate_residual_arithmetic() {
    const lumenbound::DenseMatrix transport(
        2, 2, {0.25, 0.125, 0.0, 0.5});
    const lumenbound::DenseVector emission{0.1, 0.2};
    const lumenbound::DenseVector candidate{-0.75, 0.25};
    require(candidate[0] < 0.0,
            "signed residual fixture does not contain a negative candidate");

    for (std::size_t row = 0; row < candidate.size(); ++row) {
        lumenbound::Interval residual =
            lumenbound::Interval::point(emission[row]);
        long double reference =
            static_cast<long double>(emission[row]);
        for (std::size_t column = 0;
             column < candidate.size(); ++column) {
            residual =
                residual +
                (lumenbound::Interval::point(transport(row, column)) *
                 lumenbound::Interval::point(candidate[column]));
            reference +=
                static_cast<long double>(transport(row, column)) *
                static_cast<long double>(candidate[column]);
        }
        residual =
            residual - lumenbound::Interval::point(candidate[row]);
        reference -= static_cast<long double>(candidate[row]);
        require_contains_long_double(
            residual, reference,
            "residual arithmetic excluded a signed-candidate reference");
    }

    const double error = lumenbound::conservative_absolute_error(
        candidate[0], lumenbound::Interval(0.0, 0.5));
    require(error >= 1.25,
            "signed candidate error bound was underestimated");
}

void test_projection_contains_exact_pixels() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});

    for (std::size_t band = 0;
         band < problem.exact_coefficients.size(); ++band) {
        const lumenbound::DenseVector exact_pixels =
            problem.projection.project(
                problem.exact_coefficients[band]);
        for (std::size_t pixel = 0;
             pixel < exact_pixels.size(); ++pixel) {
            const lumenbound::BoundedValue& bound =
                find_bound(result.certificate.pixel_bounds, band, pixel);
            require(bound.lower <= exact_pixels[pixel] &&
                        exact_pixels[pixel] <= bound.upper,
                    "projected interval excluded an exact pixel coefficient");
            require(bound.status ==
                        lumenbound::CertificateStatus::Certified,
                    "projected bound did not inherit certified status");
            require(bound.error_bound >=
                        std::abs(bound.candidate - exact_pixels[pixel]),
                    "projected absolute error bound was underestimated");
        }
    }
}

void test_mse_and_psnr_bounds_are_conservative() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});

    long double squared_error_sum = 0.0L;
    std::size_t value_count = 0;
    for (std::size_t band = 0;
         band < problem.exact_coefficients.size(); ++band) {
        const lumenbound::DenseVector exact_pixels =
            problem.projection.project(
                problem.exact_coefficients[band]);
        for (std::size_t pixel = 0;
             pixel < exact_pixels.size(); ++pixel) {
            const lumenbound::BoundedValue& bound =
                find_bound(result.certificate.pixel_bounds, band, pixel);
            const long double difference =
                static_cast<long double>(bound.candidate) -
                static_cast<long double>(exact_pixels[pixel]);
            squared_error_sum += difference * difference;
            ++value_count;
        }
    }
    require(value_count != 0, "measured image has no coefficients");
    const long double measured_mse =
        squared_error_sum / static_cast<long double>(value_count);
    require(static_cast<long double>(
                *result.certificate.mse_upper_bound) >= measured_mse,
            "reported MSE upper bound is below the measured MSE");

    if (measured_mse > 0.0L) {
        require(result.certificate.psnr_lower_bound_kind ==
                    lumenbound::PsnrBoundKind::Finite &&
                    result.certificate.psnr_lower_bound.has_value(),
                "positive measured MSE lacks a finite PSNR lower bound");
        const long double measured_psnr =
            10.0L * std::log10(1.0L / measured_mse);
        require(static_cast<long double>(
                    *result.certificate.psnr_lower_bound) <=
                    measured_psnr,
                "reported PSNR lower bound exceeds measured PSNR");
    } else {
        require(
            result.certificate.psnr_lower_bound_kind ==
                    lumenbound::PsnrBoundKind::Finite ||
                result.certificate.psnr_lower_bound_kind ==
                    lumenbound::PsnrBoundKind::PositiveInfinity,
            "zero measured MSE lacks a valid PSNR lower bound");
    }

    const std::array<double, 2> synthetic_errors{0.125, 0.25};
    const lumenbound::ImageMetricBounds synthetic =
        lumenbound::compute_image_metric_bounds(synthetic_errors, 1.0);
    constexpr long double synthetic_mse = 0.0390625L;
    const long double synthetic_psnr =
        10.0L * std::log10(1.0L / synthetic_mse);
    require(static_cast<long double>(synthetic.mse_upper_bound) >=
                synthetic_mse,
            "metric aggregation underestimated a known MSE");
    require(synthetic.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::Finite &&
                synthetic.psnr_lower_bound.has_value() &&
                static_cast<long double>(*synthetic.psnr_lower_bound) <=
                    synthetic_psnr,
            "metric aggregation overestimated a known PSNR");
}

void test_zero_error_metrics_report_positive_infinity() {
    const std::array<double, 3> errors{0.0, 0.0, 0.0};
    const lumenbound::ImageMetricBounds metrics =
        lumenbound::compute_image_metric_bounds(errors, 1.0);
    require(metrics.mse_upper_bound == 0.0,
            "zero error produced a positive MSE upper bound");
    require(metrics.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::PositiveInfinity,
            "zero error did not produce positive-infinity PSNR");
    require(!metrics.psnr_lower_bound.has_value(),
            "positive-infinity PSNR carried a finite numeric value");
}

void test_noncontractive_system_is_rejected() {
    const lumenbound::TransportSystem system =
        make_scalar_system(0.0, 1.0);
    const lumenbound::TransportValidationReport validation =
        system.validate();
    require(validation.code ==
                lumenbound::TransportValidationCode::NonContractive,
            "row-sum bound equal to one was not rejected");
    require(validation.contraction_upper_bound >= 1.0,
            "non-contractive validation reported a bound below one");

    const lumenbound::CertificationResult result =
        lumenbound::certify(
            system, make_scalar_projection(1.0),
            lumenbound::CertificationOptions{1.0, 0.0, 8});
    require_status(
        result,
        lumenbound::CertificateStatus::UncertifiedNonContractive,
        "non-contractive system received an incorrect certificate status");
}

void test_invalid_values_have_distinct_statuses() {
    const double not_a_number =
        std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const lumenbound::CertificationOptions options{1.0, 0.0, 8};

    const lumenbound::CertificationResult nonfinite_emission =
        lumenbound::certify(
            make_scalar_system(not_a_number, 0.25),
            make_scalar_projection(1.0), options);
    require_status(
        nonfinite_emission,
        lumenbound::CertificateStatus::UncertifiedNonFiniteInput,
        "non-finite emission received an incorrect status");
    require(nonfinite_emission.certificate.reason ==
                "emission_contains_non_finite_value",
            "non-finite emission received an imprecise reason");

    const lumenbound::CertificationResult nonfinite_transport =
        lumenbound::certify(
            make_scalar_system(0.5, infinity),
            make_scalar_projection(1.0), options);
    require_status(
        nonfinite_transport,
        lumenbound::CertificateStatus::UncertifiedNonFiniteInput,
        "non-finite transport received an incorrect status");
    require(nonfinite_transport.certificate.reason ==
                "transport_contains_non_finite_value",
            "non-finite transport received an imprecise reason");

    const lumenbound::CertificationResult negative_emission =
        lumenbound::certify(
            make_scalar_system(-0.5, 0.25),
            make_scalar_projection(1.0), options);
    require_status(
        negative_emission,
        lumenbound::CertificateStatus::UncertifiedNegativeEmission,
        "negative emission received an incorrect status");

    const lumenbound::CertificationResult negative_transport =
        lumenbound::certify(
            make_scalar_system(0.5, -0.25),
            make_scalar_projection(1.0), options);
    require_status(
        negative_transport,
        lumenbound::CertificateStatus::UncertifiedNegativeTransport,
        "negative transport received an incorrect status");

    const lumenbound::CertificationResult negative_projection =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(-1.0), options);
    require_status(
        negative_projection,
        lumenbound::CertificateStatus::UncertifiedNegativeProjection,
        "negative projection received an incorrect status");

    const lumenbound::CertificationResult nonfinite_projection =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(infinity), options);
    require_status(
        nonfinite_projection,
        lumenbound::CertificateStatus::UncertifiedNonFiniteInput,
        "non-finite projection received an incorrect status");
    require(nonfinite_projection.certificate.reason ==
                "projection_contains_non_finite_value",
            "non-finite projection received an imprecise reason");

    require(negative_emission.certificate.status !=
                negative_transport.certificate.status &&
                negative_transport.certificate.status !=
                    negative_projection.certificate.status,
            "negative input causes were collapsed into one status");
}

void test_invalid_dimensions_and_metric_options_are_rejected() {
    const lumenbound::TransportSystem invalid_system(
        std::vector<lumenbound::DenseVector>{
            lumenbound::DenseVector{0.5, 0.25}},
        std::vector<lumenbound::DenseMatrix>{
            lumenbound::DenseMatrix(1, 1, {0.25})});
    const lumenbound::CertificationResult invalid_dimensions =
        lumenbound::certify(
            invalid_system,
            lumenbound::Projection(
                lumenbound::DenseMatrix(1, 2, {1.0, 0.0})),
            lumenbound::CertificationOptions{1.0, 0.0, 8});
    require_status(
        invalid_dimensions,
        lumenbound::CertificateStatus::UncertifiedInvalidDimensions,
        "invalid transport dimensions received an incorrect status");

    const lumenbound::CertificationResult invalid_projection_dimensions =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            lumenbound::Projection(
                lumenbound::DenseMatrix(1, 2, {1.0, 0.0})),
            lumenbound::CertificationOptions{1.0, 0.0, 8});
    require_status(
        invalid_projection_dimensions,
        lumenbound::CertificateStatus::UncertifiedInvalidDimensions,
        "invalid projection dimensions received an incorrect status");

    const lumenbound::CertificationResult invalid_peak =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(1.0),
            lumenbound::CertificationOptions{0.0, 0.0, 8});
    require_status(
        invalid_peak,
        lumenbound::CertificateStatus::UncertifiedInvalidSignalPeak,
        "nonpositive signal peak received an incorrect status");

    const lumenbound::CertificationResult invalid_target =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(1.0),
            lumenbound::CertificationOptions{
                1.0, std::numeric_limits<double>::infinity(), 8});
    require_status(
        invalid_target,
        lumenbound::CertificateStatus::UncertifiedInvalidTarget,
        "non-finite PSNR target received an incorrect status");

    require_throws<std::invalid_argument>(
        []() {
            const std::array<double, 1> errors{0.0};
            static_cast<void>(
                lumenbound::compute_image_metric_bounds(errors, 0.0));
        },
        "metric module accepted a nonpositive signal peak");
    require_throws<std::invalid_argument>(
        []() {
            const std::array<double, 1> errors{-0.5};
            static_cast<void>(
                lumenbound::compute_image_metric_bounds(errors, 1.0));
        },
        "metric module accepted a negative error bound");
}

void test_invalid_demo_inputs_remain_machine_readable() {
    const std::filesystem::path root =
        std::filesystem::current_path() /
        "lumenbound-test-output-invalid-input";
    const TemporaryDirectory temporary(root);

    std::ostringstream peak_summary;
    std::ostringstream peak_errors;
    const std::filesystem::path peak_output =
        temporary.path() / "invalid-peak";
    std::ostringstream valid_summary;
    std::ostringstream valid_errors;
    const lumenbound::DemoRunResult valid_run =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{peak_output, 1.0, 80.0, 16},
            valid_summary, valid_errors);
    require(valid_run.exit_code == 0 &&
                std::filesystem::exists(peak_output / "preview.ppm"),
            "valid setup run did not create preview output");

    const lumenbound::DemoRunResult invalid_peak =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{
                peak_output,
                std::numeric_limits<double>::quiet_NaN(),
                80.0,
                16},
            peak_summary, peak_errors);
    require_status(
        invalid_peak.certification,
        lumenbound::CertificateStatus::UncertifiedInvalidSignalPeak,
        "invalid demo peak lost its validation status");
    require(invalid_peak.certification.certificate.reason ==
                "signal_peak_must_be_finite_and_positive",
            "invalid demo peak lost its machine-readable reason");
    const std::string peak_certificate =
        read_binary_file(peak_output / "certificate.json");
    require(
        peak_certificate.find("\"status\":\""
                              "UncertifiedInvalidSignalPeak\"") !=
                std::string::npos &&
            peak_certificate.find("\"classification\":\"nan\"") !=
                std::string::npos &&
            peak_certificate.find("\"mse_upper_bound\":null") !=
                std::string::npos,
        "invalid demo peak was not serialized explicitly");
    require(!std::filesystem::exists(peak_output / "preview.ppm"),
            "invalid demo peak retained a stale preview");
    require(!std::filesystem::exists(
                peak_output / "candidate-coefficients.csv"),
            "invalid demo peak retained stale candidate data");
    const std::string peak_metrics =
        read_binary_file(peak_output / "metrics.json");
    require(
        peak_metrics.find(
            "\"maximum_coefficient_interval_width\":null") !=
            std::string::npos,
        "invalid demo peak reported an unestablished interval width");

    std::ostringstream target_summary;
    std::ostringstream target_errors;
    const std::filesystem::path target_output =
        temporary.path() / "invalid-target";
    const lumenbound::DemoRunResult invalid_target =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{
                target_output,
                1.0,
                std::numeric_limits<double>::infinity(),
                16},
            target_summary, target_errors);
    require_status(
        invalid_target.certification,
        lumenbound::CertificateStatus::UncertifiedInvalidTarget,
        "invalid demo target lost its validation status");
    const std::string target_certificate =
        read_binary_file(target_output / "certificate.json");
    require(
        target_certificate.find(
            "\"status\":\"UncertifiedInvalidTarget\"") !=
                std::string::npos &&
            target_certificate.find(
                "\"classification\":\"positive_infinity\"") !=
                std::string::npos,
        "invalid demo target was not serialized explicitly");
}

void test_demo_is_deterministic_and_emits_all_outputs() {
    const std::filesystem::path root =
        std::filesystem::current_path() /
        "lumenbound-test-output-determinism";
    const TemporaryDirectory temporary(root);
    const std::filesystem::path first = temporary.path() / "first";
    const std::filesystem::path second = temporary.path() / "second";

    std::ostringstream first_summary;
    std::ostringstream first_errors;
    const lumenbound::DemoRunResult first_run =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{first, 1.0, 80.0, 512},
            first_summary, first_errors);
    std::ostringstream second_summary;
    std::ostringstream second_errors;
    const lumenbound::DemoRunResult second_run =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{second, 1.0, 80.0, 512},
            second_summary, second_errors);

    require(first_run.exit_code == 0 && second_run.exit_code == 0,
            "documented demonstration command did not succeed");
    require(first_run.certification.certificate.status ==
                lumenbound::CertificateStatus::Certified &&
                second_run.certification.certificate.status ==
                    lumenbound::CertificateStatus::Certified,
            "demonstration did not return certified status");
    require(first_errors.str().empty() && second_errors.str().empty(),
            "successful demonstration wrote an error");
    require(first_summary.str() == second_summary.str(),
            "repeated demonstration summaries differ");
    require(first_summary.str().find("q_upper:") != std::string::npos &&
                first_summary.str().find("mse_upper:") !=
                    std::string::npos &&
                first_summary.str().find("psnr_lower:") !=
                    std::string::npos,
            "demonstration summary omits proof metrics");

    constexpr std::array<std::string_view, 6> expected_files{
        "candidate-coefficients.csv",
        "coefficient-bounds.csv",
        "linear-pixels.csv",
        "preview.ppm",
        "certificate.json",
        "metrics.json",
    };
    for (const std::string_view filename : expected_files) {
        const std::filesystem::path first_path =
            first / std::filesystem::path(filename);
        const std::filesystem::path second_path =
            second / std::filesystem::path(filename);
        require(std::filesystem::is_regular_file(first_path) &&
                    std::filesystem::is_regular_file(second_path),
                "demonstration omitted a required output file");
        require(std::filesystem::file_size(first_path) > 0U &&
                    std::filesystem::file_size(second_path) > 0U,
                "demonstration produced an empty output file");
    }

    require(read_binary_file(first / "certificate.json") ==
                read_binary_file(second / "certificate.json"),
            "certificate files are not byte-identical");
    require(read_binary_file(first / "metrics.json") ==
                read_binary_file(second / "metrics.json"),
            "metric files are not byte-identical");
}

void test_iteration_budget_failure_is_explicit() {
    const std::filesystem::path root =
        std::filesystem::current_path() /
        "lumenbound-test-output-iteration-limit";
    const TemporaryDirectory temporary(root);
    std::ostringstream summary;
    std::ostringstream errors;
    const lumenbound::DemoRunResult run =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{temporary.path() / "run",
                                    1.0, 80.0, 0},
            summary, errors);

    require(run.exit_code != 0,
            "zero iteration budget returned a successful exit code");
    require_status(
        run.certification,
        lumenbound::CertificateStatus::UncertifiedIterationLimit,
        "zero iteration budget received an incorrect status");
    require(run.certification.certificate.reason ==
                "zero_iteration_budget_before_target",
            "zero iteration budget received an imprecise reason");
    require(errors.str().find("UncertifiedIterationLimit") !=
                std::string::npos &&
                errors.str().find(
                    "zero_iteration_budget_before_target") !=
                    std::string::npos,
            "iteration-limit failure was not explained on stderr");
}

void test_impossible_target_failure_is_explicit() {
    const std::filesystem::path root =
        std::filesystem::current_path() /
        "lumenbound-test-output-impossible-target";
    const TemporaryDirectory temporary(root);
    std::ostringstream summary;
    std::ostringstream errors;
    const lumenbound::DemoRunResult run =
        lumenbound::run_certified_patches(
            lumenbound::DemoOptions{temporary.path() / "run",
                                    1.0, 1000.0, 512},
            summary, errors);

    require(run.exit_code != 0,
            "unreachable PSNR target returned a successful exit code");
    require_status(
        run.certification,
        lumenbound::CertificateStatus::UncertifiedTargetNotReached,
        "unreachable PSNR target received an incorrect status");
    require(run.certification.certificate.reason ==
                "interval_propagation_stagnated_before_target",
            "unreachable PSNR target received an imprecise reason");
    require(errors.str().find("UncertifiedTargetNotReached") !=
                std::string::npos,
            "unreachable-target failure was not explained on stderr");
}

struct TestCase {
    std::string_view name;
    void (*function)();
};

}  // namespace

int main() {
    const std::vector<TestCase> tests{
        {"interval operations contain scalar references",
         test_interval_operations_contain_scalar_references},
        {"outward rounding covers edge cases",
         test_outward_rounding_edge_cases},
        {"non-finite fast paths are rejected",
         test_nonfinite_fast_paths_are_rejected},
        {"certified logarithm contains known values",
         test_certified_logarithm_known_values},
        {"rounding mode is preserved",
         test_rounding_mode_is_preserved},
        {"dense algebra and spectrum order",
         test_dense_algebra_and_spectrum_order},
        {"candidate solver matches manufactured solution",
         test_candidate_solver_matches_manufactured_solution},
        {"iteration snapshots are monotone and enclosing",
         test_iteration_snapshots_are_monotone_and_contain_exact_solution},
        {"iteration width contracts",
         test_iteration_width_contracts},
        {"residual certificate contains measured errors",
         test_residual_certificate_contains_measured_errors},
        {"signed candidate residual arithmetic",
         test_signed_candidate_residual_arithmetic},
        {"projection contains exact pixels",
         test_projection_contains_exact_pixels},
        {"MSE and PSNR bounds are conservative",
         test_mse_and_psnr_bounds_are_conservative},
        {"zero error reports positive-infinity PSNR",
         test_zero_error_metrics_report_positive_infinity},
        {"non-contractive system is rejected",
         test_noncontractive_system_is_rejected},
        {"invalid values have distinct statuses",
         test_invalid_values_have_distinct_statuses},
        {"invalid dimensions and options are rejected",
         test_invalid_dimensions_and_metric_options_are_rejected},
        {"invalid demo inputs remain machine-readable",
         test_invalid_demo_inputs_remain_machine_readable},
        {"demo is deterministic and complete",
         test_demo_is_deterministic_and_emits_all_outputs},
        {"iteration budget failure is explicit",
         test_iteration_budget_failure_is_explicit},
        {"impossible target failure is explicit",
         test_impossible_target_failure_is_explicit},
    };

    std::size_t passed = 0;
    for (const TestCase& test : tests) {
        try {
            test.function();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[FAIL] " << test.name << ": "
                      << exception.what() << '\n';
        } catch (...) {
            std::cerr << "[FAIL] " << test.name
                      << ": unknown exception\n";
        }
    }

    std::cout << passed << '/' << tests.size() << " tests passed\n";
    return passed == tests.size() ? 0 : 1;
}
