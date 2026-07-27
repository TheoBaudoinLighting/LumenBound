#include "lumenbound/certification/certifier.hpp"
#include "lumenbound/certification/image_metrics.hpp"
#include "lumenbound/certification/problem_digest.hpp"
#include "lumenbound/core/cornell_box_demo.hpp"
#include "lumenbound/core/demo.hpp"
#include "lumenbound/io/output.hpp"
#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/math/interval.hpp"
#include "lumenbound/math/rounding.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/solver/candidate_solver.hpp"
#include "lumenbound/transport/transport_system.hpp"
#include "lumenbound/transport/diffuse_patch_assembly.hpp"

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

void require_problem_digest(const lumenbound::Certificate& certificate,
                            std::string_view message) {
    require(certificate.problem_digest.starts_with("sha256:") &&
                certificate.problem_digest.size() == 71U,
            message);
    for (std::size_t index = 7;
         index < certificate.problem_digest.size(); ++index) {
        const char digit = certificate.problem_digest[index];
        require((digit >= '0' && digit <= '9') ||
                    (digit >= 'a' && digit <= 'f'),
                "a problem digest contains a non-canonical hex digit");
    }
}

void require_proof(
    const lumenbound::CertificationResult& result,
    lumenbound::ProofStatus expected_status,
    lumenbound::ProofFailureCode expected_failure,
    std::string_view message) {
    require(result.certificate.proof_status == expected_status, message);
    require(result.certificate.proof_failure == expected_failure, message);
    require(!result.certificate.proof_reason.empty(),
            "a proof result must include a reason");
    require_problem_digest(
        result.certificate,
        "a proof result must identify the supplied finite problem");
}

void require_target(const lumenbound::CertificationResult& result,
                    lumenbound::TargetStatus expected,
                    std::string_view message) {
    require(result.certificate.target_status == expected, message);
    require(!result.certificate.target_reason.empty(),
            "a target result must include a reason");
}

void require_certified_bounds(const lumenbound::Certificate& certificate) {
    require(!certificate.coefficient_bounds.empty(),
            "a certified proof omitted coefficient bounds");
    require(!certificate.pixel_bounds.empty(),
            "a certified proof omitted pixel bounds");
    for (const lumenbound::BoundedValue& bound :
         certificate.coefficient_bounds) {
        require(bound.proof_status == lumenbound::ProofStatus::Certified,
                "a coefficient bound did not retain certified proof status");
    }
    for (const lumenbound::BoundedValue& bound :
         certificate.pixel_bounds) {
        require(bound.proof_status == lumenbound::ProofStatus::Certified,
                "a pixel bound did not retain certified proof status");
    }
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

void require_exact_coefficients_enclosed(
    const lumenbound::ManufacturedProblem& problem,
    const lumenbound::CertificationResult& result) {
    for (std::size_t band = 0;
         band < problem.exact_coefficients.size(); ++band) {
        for (std::size_t coefficient = 0;
             coefficient < problem.exact_coefficients[band].size();
             ++coefficient) {
            const lumenbound::BoundedValue& bound =
                find_bound(result.certificate.coefficient_bounds,
                           band, coefficient);
            const double exact =
                problem.exact_coefficients[band][coefficient];
            require(bound.lower <= exact && exact <= bound.upper,
                    "a coefficient enclosure excluded the exact solution");
        }
    }
}

void require_bound_vectors_equal(
    const std::vector<lumenbound::BoundedValue>& first,
    const std::vector<lumenbound::BoundedValue>& second,
    std::string_view message) {
    require(first.size() == second.size(), message);
    for (std::size_t index = 0; index < first.size(); ++index) {
        require(first[index].band == second[index].band &&
                    first[index].index == second[index].index &&
                    first[index].candidate == second[index].candidate &&
                    first[index].lower == second[index].lower &&
                    first[index].upper == second[index].upper &&
                    first[index].error_bound == second[index].error_bound &&
                    first[index].proof_status ==
                        second[index].proof_status,
                message);
    }
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
    if (!stream) {
        fail("expected output file is missing: " + path.string());
    }
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

void test_dense_algebra() {
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
    require(candidate.residual_infinity_norm <= candidate_tolerance,
            "candidate residual exceeded its binary64 test tolerance");
}

void test_iteration_snapshots_are_monotone_and_contain_exact_solution() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult result =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 1000.0, 512, true});

    require_proof(result, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "the manufactured system was not certified");
    require_target(result, lumenbound::TargetStatus::Stagnated,
                   "the snapshot run did not reach arithmetic stagnation");
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
            lumenbound::CertificationOptions{1.0, 1000.0, 512, true});
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

    const double residual_error_bound =
        *result.certificate.candidate_error_upper_bound;
    for (std::size_t band = 0;
         band < problem.exact_coefficients.size(); ++band) {
        for (std::size_t coefficient = 0;
             coefficient < problem.exact_coefficients[band].size();
             ++coefficient) {
            const lumenbound::BoundedValue& bound =
                find_bound(result.certificate.coefficient_bounds,
                           band, coefficient);
            const double candidate =
                result.candidate.values[band][coefficient];
            const double residual_lower =
                lumenbound::math::subtract_down(
                    candidate, residual_error_bound);
            const double residual_upper =
                lumenbound::math::add_up(
                    candidate, residual_error_bound);
            require(bound.lower >= residual_lower &&
                        bound.upper <= residual_upper,
                    "the final enclosure escaped the residual enclosure");
            const double exact =
                problem.exact_coefficients[band][coefficient];
            require(bound.lower <= exact && exact <= bound.upper,
                    "the residual-refined enclosure excluded the exact value");
        }
    }
}

void test_target_outcomes_preserve_certified_proof() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();

    const lumenbound::CertificationResult reached =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 80.0, 512});
    require_proof(reached, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "the reachable target lost its certified proof");
    require_target(reached, lumenbound::TargetStatus::Reached,
                   "the 80 dB target was not reached");
    require(reached.certificate.interval_iteration_count == 0U,
            "the residual enclosure did not reach 80 dB at initialization");
    require_certified_bounds(reached.certificate);
    require_exact_coefficients_enclosed(problem, reached);

    const lumenbound::CertificationResult limited =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 1000.0, 0});
    require_proof(limited, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "the iteration limit invalidated an established proof");
    require_target(limited, lumenbound::TargetStatus::IterationLimit,
                   "a zero iteration budget received the wrong target status");
    require_certified_bounds(limited.certificate);
    require_exact_coefficients_enclosed(problem, limited);

    const lumenbound::CertificationResult stagnated =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 1000.0, 512});
    require_proof(stagnated, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "arithmetic stagnation invalidated an established proof");
    require_target(stagnated, lumenbound::TargetStatus::Stagnated,
                   "the unreachable target did not report stagnation");
    require_certified_bounds(stagnated.certificate);
    require_exact_coefficients_enclosed(problem, stagnated);
}

void test_iteration_snapshots_are_opt_in_and_proof_neutral() {
    const lumenbound::ManufacturedProblem problem =
        lumenbound::make_certified_patches_problem();
    const lumenbound::CertificationResult without_snapshots =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 1000.0, 4, false});
    const lumenbound::CertificationResult with_snapshots =
        lumenbound::certify(
            problem.system, problem.projection,
            lumenbound::CertificationOptions{1.0, 1000.0, 4, true});

    require(without_snapshots.iterations.empty(),
            "default certification retained diagnostic snapshots");
    require(!without_snapshots.certificate.iteration_snapshots_retained,
            "certificate metadata incorrectly reports retained snapshots");
    require(with_snapshots.certificate.iteration_snapshots_retained,
            "opt-in snapshot capture was not recorded");
    require(with_snapshots.iterations.size() ==
                with_snapshots.certificate.interval_iteration_count + 1U,
            "opt-in snapshots do not match the affine iteration count");
    require(without_snapshots.certificate.problem_digest !=
                with_snapshots.certificate.problem_digest,
            "snapshot policy was omitted from the problem digest");
    require(
        std::find(without_snapshots.certificate.assumptions.begin(),
                  without_snapshots.certificate.assumptions.end(),
                  "certification_requires_componentwise_nonnegative_emission") !=
            without_snapshots.certificate.assumptions.end(),
        "certificate assumptions omit nonnegative emission");

    const lumenbound::Certificate& first =
        without_snapshots.certificate;
    const lumenbound::Certificate& second =
        with_snapshots.certificate;
    require(first.proof_status == second.proof_status &&
                first.proof_failure == second.proof_failure &&
                first.proof_reason == second.proof_reason &&
                first.target_status == second.target_status &&
                first.target_reason == second.target_reason &&
                first.contraction_upper_bound ==
                    second.contraction_upper_bound &&
                first.interval_iteration_count ==
                    second.interval_iteration_count &&
                first.residual_upper_bound ==
                    second.residual_upper_bound &&
                first.candidate_error_upper_bound ==
                    second.candidate_error_upper_bound &&
                first.mse_upper_bound == second.mse_upper_bound &&
                first.psnr_lower_bound_kind ==
                    second.psnr_lower_bound_kind &&
                first.psnr_lower_bound == second.psnr_lower_bound,
            "snapshot capture changed a proof or target field");
    require_bound_vectors_equal(
        first.coefficient_bounds, second.coefficient_bounds,
        "snapshot capture changed coefficient bounds");
    require_bound_vectors_equal(
        first.pixel_bounds, second.pixel_bounds,
        "snapshot capture changed pixel bounds");
}

void test_problem_digest_is_canonical_and_sensitive() {
    const lumenbound::TransportSystem system =
        make_scalar_system(0.25, 0.5);
    const lumenbound::Projection projection =
        make_scalar_projection(1.0);
    const lumenbound::CertificationOptions options{
        1.0, 80.0, 512, false};
    const std::string digest =
        lumenbound::compute_problem_digest(system, projection, options);
    require(
        digest ==
            "sha256:f0a57997694460607bad06e2047785955ded9c2093962958cdcc2ef337dc44e3",
        "canonical scalar problem digest changed");

    require(
        lumenbound::compute_problem_digest(
            make_scalar_system(0.5, 0.5), projection, options) != digest,
        "emission mutation did not change the problem digest");
    require(
        lumenbound::compute_problem_digest(
            make_scalar_system(0.25, 0.25), projection, options) != digest,
        "transport mutation did not change the problem digest");
    require(
        lumenbound::compute_problem_digest(
            system, make_scalar_projection(0.5), options) != digest,
        "projection mutation did not change the problem digest");

    lumenbound::CertificationOptions changed = options;
    changed.signal_peak = 2.0;
    require(lumenbound::compute_problem_digest(
                system, projection, changed) != digest,
            "signal peak mutation did not change the problem digest");
    changed = options;
    changed.target_psnr = 81.0;
    require(lumenbound::compute_problem_digest(
                system, projection, changed) != digest,
            "target mutation did not change the problem digest");
    changed = options;
    changed.maximum_iterations = 511;
    require(lumenbound::compute_problem_digest(
                system, projection, changed) != digest,
            "iteration budget mutation did not change the problem digest");
    changed = options;
    changed.retain_iteration_snapshots = true;
    require(lumenbound::compute_problem_digest(
                system, projection, changed) != digest,
            "snapshot policy mutation did not change the problem digest");

    const lumenbound::TransportSystem ordered_bands(
        std::vector<lumenbound::DenseVector>{
            lumenbound::DenseVector{0.25},
            lumenbound::DenseVector{0.5}},
        std::vector<lumenbound::DenseMatrix>{
            lumenbound::DenseMatrix(1, 1, {0.125}),
            lumenbound::DenseMatrix(1, 1, {0.25})});
    const lumenbound::TransportSystem reversed_bands(
        std::vector<lumenbound::DenseVector>{
            lumenbound::DenseVector{0.5},
            lumenbound::DenseVector{0.25}},
        std::vector<lumenbound::DenseMatrix>{
            lumenbound::DenseMatrix(1, 1, {0.25}),
            lumenbound::DenseMatrix(1, 1, {0.125})});
    require(
        lumenbound::compute_problem_digest(
            ordered_bands, projection, options) !=
            lumenbound::compute_problem_digest(
                reversed_bands, projection, options),
        "band permutation did not change the problem digest");

    const lumenbound::TransportSystem extra_operator(
        std::vector<lumenbound::DenseVector>{
            lumenbound::DenseVector{0.25}},
        std::vector<lumenbound::DenseMatrix>{
            lumenbound::DenseMatrix(1, 1, {0.5}),
            lumenbound::DenseMatrix(1, 1, {0.125})});
    require(
        lumenbound::compute_problem_digest(
            extra_operator, projection, options) != digest,
        "an unmatched transport operator was omitted from the digest");

    const std::string positive_zero_digest =
        lumenbound::compute_problem_digest(
            make_scalar_system(0.0, 0.5), projection, options);
    const std::string negative_zero_digest =
        lumenbound::compute_problem_digest(
            make_scalar_system(-0.0, 0.5), projection, options);
    require(positive_zero_digest != negative_zero_digest,
            "the problem digest canonicalized signed zero");
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
            require(bound.proof_status ==
                        lumenbound::ProofStatus::Certified,
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
    require_proof(
        result, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NonContractive,
        "non-contractive system received an incorrect certificate status");
    require_target(result, lumenbound::TargetStatus::NotEvaluated,
                   "a rejected system evaluated the requested target");
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
    require_proof(
        nonfinite_emission, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NonFiniteInput,
        "non-finite emission received an incorrect status");
    require(nonfinite_emission.certificate.proof_reason ==
                "emission_contains_non_finite_value",
            "non-finite emission received an imprecise reason");

    const lumenbound::CertificationResult nonfinite_transport =
        lumenbound::certify(
            make_scalar_system(0.5, infinity),
            make_scalar_projection(1.0), options);
    require_proof(
        nonfinite_transport, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NonFiniteInput,
        "non-finite transport received an incorrect status");
    require(nonfinite_transport.certificate.proof_reason ==
                "transport_contains_non_finite_value",
            "non-finite transport received an imprecise reason");

    const lumenbound::CertificationResult negative_emission =
        lumenbound::certify(
            make_scalar_system(-0.5, 0.25),
            make_scalar_projection(1.0), options);
    require_proof(
        negative_emission, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NegativeEmission,
        "negative emission received an incorrect status");

    const lumenbound::CertificationResult negative_transport =
        lumenbound::certify(
            make_scalar_system(0.5, -0.25),
            make_scalar_projection(1.0), options);
    require_proof(
        negative_transport, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NegativeTransport,
        "negative transport received an incorrect status");

    const lumenbound::CertificationResult negative_projection =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(-1.0), options);
    require_proof(
        negative_projection, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NegativeProjection,
        "negative projection received an incorrect status");

    const lumenbound::CertificationResult nonfinite_projection =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(infinity), options);
    require_proof(
        nonfinite_projection, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::NonFiniteInput,
        "non-finite projection received an incorrect status");
    require(nonfinite_projection.certificate.proof_reason ==
                "projection_contains_non_finite_value",
            "non-finite projection received an imprecise reason");

    require(negative_emission.certificate.proof_failure !=
                negative_transport.certificate.proof_failure &&
                negative_transport.certificate.proof_failure !=
                    negative_projection.certificate.proof_failure,
            "negative input causes were collapsed into one failure code");
    require_target(nonfinite_emission,
                   lumenbound::TargetStatus::NotEvaluated,
                   "an invalid emission evaluated the target");
    require_target(nonfinite_transport,
                   lumenbound::TargetStatus::NotEvaluated,
                   "an invalid transport evaluated the target");
    require_target(negative_emission,
                   lumenbound::TargetStatus::NotEvaluated,
                   "a negative emission evaluated the target");
    require_target(negative_transport,
                   lumenbound::TargetStatus::NotEvaluated,
                   "a negative transport evaluated the target");
    require_target(negative_projection,
                   lumenbound::TargetStatus::NotEvaluated,
                   "a negative projection evaluated the target");
    require_target(nonfinite_projection,
                   lumenbound::TargetStatus::NotEvaluated,
                   "an invalid projection evaluated the target");
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
    require_proof(
        invalid_dimensions, lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::InvalidDimensions,
        "invalid transport dimensions received an incorrect status");
    require_target(invalid_dimensions,
                   lumenbound::TargetStatus::NotEvaluated,
                   "invalid transport dimensions evaluated the target");

    const lumenbound::CertificationResult invalid_projection_dimensions =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            lumenbound::Projection(
                lumenbound::DenseMatrix(1, 2, {1.0, 0.0})),
            lumenbound::CertificationOptions{1.0, 0.0, 8});
    require_proof(
        invalid_projection_dimensions,
        lumenbound::ProofStatus::Uncertified,
        lumenbound::ProofFailureCode::InvalidDimensions,
        "invalid projection dimensions received an incorrect status");
    require_target(invalid_projection_dimensions,
                   lumenbound::TargetStatus::NotEvaluated,
                   "invalid projection dimensions evaluated the target");

    const lumenbound::CertificationResult invalid_peak =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(1.0),
            lumenbound::CertificationOptions{0.0, 0.0, 8});
    require_proof(invalid_peak, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "an invalid peak erased a valid finite-system proof");
    require_target(invalid_peak, lumenbound::TargetStatus::InvalidTarget,
                   "a nonpositive peak received the wrong target status");
    require(invalid_peak.certificate.mse_upper_bound.has_value(),
            "an invalid peak erased the MSE upper bound");
    require(invalid_peak.certificate.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::Unavailable &&
                !invalid_peak.certificate.psnr_lower_bound.has_value(),
            "an invalid peak produced a PSNR bound");
    require_certified_bounds(invalid_peak.certificate);

    const lumenbound::CertificationResult invalid_target =
        lumenbound::certify(
            make_scalar_system(0.5, 0.25),
            make_scalar_projection(1.0),
            lumenbound::CertificationOptions{
                1.0, std::numeric_limits<double>::infinity(), 8});
    require_proof(invalid_target, lumenbound::ProofStatus::Certified,
                  lumenbound::ProofFailureCode::None,
                  "an invalid target erased a valid finite-system proof");
    require_target(invalid_target, lumenbound::TargetStatus::InvalidTarget,
                   "a non-finite PSNR target received an incorrect status");
    require(invalid_target.certificate.mse_upper_bound.has_value(),
            "an invalid target erased the MSE upper bound");
    require(invalid_target.certificate.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::Finite &&
                invalid_target.certificate.psnr_lower_bound.has_value(),
            "an invalid target erased an independently valid PSNR bound");
    require_certified_bounds(invalid_target.certificate);

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
    require_proof(
        invalid_peak.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "invalid demo peak erased the finite-system proof");
    require_target(
        invalid_peak.certification,
        lumenbound::TargetStatus::InvalidTarget,
        "invalid demo peak lost its target status");
    require(invalid_peak.certification.certificate.target_reason ==
                "signal_peak_must_be_finite_and_positive",
            "invalid demo peak lost its machine-readable reason");
    require(invalid_peak.certification.certificate.mse_upper_bound.has_value(),
            "invalid demo peak erased the MSE upper bound");
    require(invalid_peak.certification.certificate.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::Unavailable,
            "invalid demo peak produced a PSNR bound");
    const std::string peak_certificate =
        read_binary_file(peak_output / "certificate.json");
    require(
        peak_certificate.find("\"proof_status\":\"Certified\"") !=
                std::string::npos &&
            peak_certificate.find(
                "\"target_status\":\"InvalidTarget\"") !=
                std::string::npos &&
            peak_certificate.find("\"classification\":\"nan\"") !=
                std::string::npos &&
            peak_certificate.find(
                "\"psnr_lower_bound\":{\"kind\":\"unavailable\"") !=
                std::string::npos &&
            peak_certificate.find("\"problem_digest\":\"sha256:") !=
                std::string::npos &&
            peak_certificate.find("\"mse_upper_bound\":null") ==
                std::string::npos,
        "invalid demo peak was not serialized explicitly");
    require(!std::filesystem::exists(peak_output / "preview.ppm"),
            "invalid demo peak retained a stale preview");
    require(std::filesystem::exists(
                peak_output / "candidate-coefficients.csv"),
            "invalid demo peak discarded the valid candidate");
    const std::string peak_metrics =
        read_binary_file(peak_output / "metrics.json");
    require(
        peak_metrics.find(
            "\"maximum_coefficient_interval_width\":null") ==
            std::string::npos,
        "invalid demo peak erased an established interval width");

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
    require_proof(
        invalid_target.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "invalid demo target erased the finite-system proof");
    require_target(
        invalid_target.certification,
        lumenbound::TargetStatus::InvalidTarget,
        "invalid demo target lost its target status");
    require(invalid_target.certification.certificate.mse_upper_bound
                .has_value(),
            "invalid demo target erased the MSE upper bound");
    require(invalid_target.certification.certificate.psnr_lower_bound_kind ==
                lumenbound::PsnrBoundKind::Finite &&
                invalid_target.certification.certificate.psnr_lower_bound
                    .has_value(),
            "invalid demo target erased an independently valid PSNR bound");
    const std::string target_certificate =
        read_binary_file(target_output / "certificate.json");
    require(
        target_certificate.find(
            "\"proof_status\":\"Certified\"") !=
                std::string::npos &&
            target_certificate.find(
                "\"target_status\":\"InvalidTarget\"") !=
                std::string::npos &&
            target_certificate.find(
                "\"classification\":\"positive_infinity\"") !=
                std::string::npos &&
            target_certificate.find(
                "\"psnr_lower_bound\":{\"kind\":\"finite\"") !=
                std::string::npos &&
            target_certificate.find("\"problem_digest\":\"sha256:") !=
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
    require_proof(
        first_run.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "first demonstration did not return a certified proof");
    require_target(first_run.certification,
                   lumenbound::TargetStatus::Reached,
                   "first demonstration did not reach its target");
    require_proof(
        second_run.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "second demonstration did not return a certified proof");
    require_target(second_run.certification,
                   lumenbound::TargetStatus::Reached,
                   "second demonstration did not reach its target");
    require(first_run.certification.certificate.interval_iteration_count ==
                0U &&
                second_run.certification.certificate
                        .interval_iteration_count == 0U,
            "the documented target required an affine interval iteration");
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

    const std::string first_certificate =
        read_binary_file(first / "certificate.json");
    const std::string second_certificate =
        read_binary_file(second / "certificate.json");
    const std::string first_metrics =
        read_binary_file(first / "metrics.json");
    const std::string second_metrics =
        read_binary_file(second / "metrics.json");
    require(first_certificate == second_certificate,
            "certificate files are not byte-identical");
    require(first_metrics == second_metrics,
            "metric files are not byte-identical");
    require(
        first_certificate.find(
            "\"schema_version\":\"lumenbound.certificate.v2\"") !=
                std::string::npos &&
            first_certificate.find(
                "\"certificate_scope\":"
                "\"finite_dimensional_positive_binary64_transport\"") !=
                std::string::npos &&
            first_certificate.find("\"solver_version\":\"0.3.0\"") !=
                std::string::npos &&
            first_certificate.find(
                "\"arithmetic_policy\":"
                "\"binary64-outward-rounded-v1\"") !=
                std::string::npos &&
            first_certificate.find("\"proof_status\":\"Certified\"") !=
                std::string::npos &&
            first_certificate.find("\"target_status\":\"Reached\"") !=
                std::string::npos,
        "certificate output omitted a v2 contract identity field");
    require(
        first_metrics.find(
            "\"schema_version\":\"lumenbound.metrics.v2\"") !=
                std::string::npos &&
            first_metrics.find("\"problem_digest\":\"sha256:") !=
                std::string::npos &&
            first_metrics.find("\"proof_status\":\"Certified\"") !=
                std::string::npos &&
            first_metrics.find("\"target_status\":\"Reached\"") !=
                std::string::npos,
        "metrics output omitted a v2 contract identity field");
    require(
        read_binary_file(first / "preview.ppm")
                .find("False-color coefficient preview; not certified") !=
            std::string::npos,
        "preview output was not labeled as false color");
}

void test_sparse_projection_preserves_dense_contract() {
    const lumenbound::Projection dense(lumenbound::DenseMatrix(
        2, 3,
        {
            0.25, 0.0, 0.75,
            0.0, 1.0, 0.0,
        }));
    const lumenbound::Projection sparse(
        2, 3,
        std::vector<std::size_t>{0, 2, 3},
        std::vector<std::size_t>{0, 2, 1},
        std::vector<double>{0.25, 0.75, 1.0});

    require(dense.stored_entry_count() == 3 &&
                sparse.stored_entry_count() == 3,
            "projection CSR retained implicit positive zeros");
    require(dense.validate(3).valid() &&
                sparse.validate(3).valid(),
            "equivalent dense and sparse projections did not validate");

    const lumenbound::DenseVector coefficients{2.0, 3.0, 4.0};
    const lumenbound::DenseVector dense_values =
        dense.project(coefficients);
    const lumenbound::DenseVector sparse_values =
        sparse.project(coefficients);
    require(dense_values.values() == sparse_values.values() &&
                dense_values[0] == 3.5 && dense_values[1] == 3.0,
            "sparse projection changed the dense projection result");

    const lumenbound::DenseVector lower{1.0, 2.0, 3.0};
    const lumenbound::DenseVector upper{2.0, 3.0, 4.0};
    const std::vector<lumenbound::Interval> dense_intervals =
        dense.project(lower, upper);
    const std::vector<lumenbound::Interval> sparse_intervals =
        sparse.project(lower, upper);
    require(dense_intervals.size() == sparse_intervals.size(),
            "sparse interval projection changed row count");
    for (std::size_t row = 0; row < dense_intervals.size(); ++row) {
        require(dense_intervals[row].lower() ==
                    sparse_intervals[row].lower() &&
                    dense_intervals[row].upper() ==
                    sparse_intervals[row].upper(),
                "sparse interval projection changed an enclosure");
    }

    require_throws<std::invalid_argument>(
        []() {
            static_cast<void>(lumenbound::Projection(
                1, 2,
                std::vector<std::size_t>{0, 2},
                std::vector<std::size_t>{1, 1},
                std::vector<double>{0.5, 0.5}));
        },
        "sparse projection accepted duplicate columns");
    require_throws<std::invalid_argument>(
        []() {
            static_cast<void>(lumenbound::Projection(
                1, 2,
                std::vector<std::size_t>{0, 1},
                std::vector<std::size_t>{2},
                std::vector<double>{1.0}));
        },
        "sparse projection accepted an out-of-range column");

    const double negative_zero = -0.0;
    const lumenbound::Projection signed_zero(lumenbound::DenseMatrix(
        1, 1, {negative_zero}));
    require(signed_zero.stored_entry_count() == 1 &&
                std::signbit(signed_zero.values().front()),
            "dense-to-CSR conversion erased supplied negative zero");
    const lumenbound::Projection nonfinite(lumenbound::DenseMatrix(
        1, 1,
        {std::numeric_limits<double>::quiet_NaN()}));
    require(nonfinite.stored_entry_count() == 1 &&
                nonfinite.validate(1).code ==
                    lumenbound::ProjectionValidationCode::NonFiniteInput,
            "dense-to-CSR conversion hid a non-finite projection value");
}

void test_diffuse_patch_assembly_is_positive_and_contractive() {
    const lumenbound::CornellBoxProblem problem =
        lumenbound::make_cornell_box_problem(16, 16);
    const lumenbound::DiffusePatchAssemblyDiagnostics& diagnostics =
        problem.assembly.diagnostics;
    require(diagnostics.surface_count == 16 &&
                diagnostics.patch_count == 274 &&
                diagnostics.coefficient_band_count == 3,
            "Cornell patch ordering or dimensions changed unexpectedly");
    require(diagnostics.transport_ray_count ==
                diagnostics.patch_count * 1024U,
            "Cornell transport quadrature ray count is incorrect");
    require(diagnostics.maximum_form_factor_row_sum <= 1.0 &&
                diagnostics.maximum_reflectance == 0.75 &&
                diagnostics.ray_origin_offset == 1.0e-7 &&
                diagnostics.intersection_epsilon == 1.0e-10,
            "Cornell quadrature violated its row-energy construction");

    const lumenbound::TransportValidationReport transport_report =
        problem.assembly.system.validate();
    const lumenbound::ProjectionValidationReport projection_report =
        problem.assembly.projection.validate(
            problem.assembly.system.transport_coefficient_count());
    require(transport_report.valid() &&
                transport_report.contraction_upper_bound < 1.0,
            "Cornell finite transport system is not contractive");
    require(transport_report.contraction_upper_bound <=
                0.7500000000001,
            "Cornell contraction bound is inconsistent with reflectance");
    require(projection_report.valid(),
            "Cornell camera projection is invalid");
    require(problem.assembly.projection.stored_entry_count() ==
                diagnostics.projection_nonzero_count,
            "Cornell projection diagnostics report the wrong CSR size");

    const std::vector<std::size_t>& row_offsets =
        problem.assembly.projection.row_offsets();
    const std::vector<double>& weights =
        problem.assembly.projection.values();
    for (std::size_t pixel = 0;
         pixel < problem.assembly.projection.pixel_count(); ++pixel) {
        double row_sum = 0.0;
        for (std::size_t entry = row_offsets[pixel];
             entry < row_offsets[pixel + 1U]; ++entry) {
            require(std::isfinite(weights[entry]) &&
                        weights[entry] >= 0.0,
                    "Cornell projection contains an invalid weight");
            row_sum += weights[entry];
        }
        require(row_sum <= 1.000000000000001,
                "Cornell projection row exceeds unit reconstruction weight");
    }

    const lumenbound::OrientedRectangle invalid_surface{
        "invalid",
        {0.0, 0.0, 0.0},
        {1.0, 0.0, 0.0},
        {0.0, 0.0, 1.0},
        1,
        1,
        lumenbound::DiffusePatchMaterial{{1.0}, {0.0}},
    };
    const lumenbound::PinholeCamera camera{
        {0.0, 1.0, -1.0},
        {0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        40.0,
    };
    require_throws<std::invalid_argument>(
        [&]() {
            static_cast<void>(
                lumenbound::assemble_diffuse_patch_problem(
                    {invalid_surface}, camera,
                    lumenbound::DiffusePatchAssemblyOptions{
                        16, 16, 2, 8, 32, 2}));
        },
        "diffuse assembly accepted unit reflectance");
    require_throws<std::invalid_argument>(
        [&]() {
            lumenbound::OrientedRectangle degenerate = invalid_surface;
            degenerate.material.reflectance[0] = 0.5;
            degenerate.edge_v = {2.0, 0.0, 0.0};
            static_cast<void>(
                lumenbound::assemble_diffuse_patch_problem(
                    {degenerate}, camera,
                    lumenbound::DiffusePatchAssemblyOptions{
                        16, 16, 2, 8, 32, 2}));
        },
        "diffuse assembly accepted a degenerate rectangle");
    require_throws<std::invalid_argument>(
        [&]() {
            lumenbound::OrientedRectangle valid = invalid_surface;
            valid.material.reflectance[0] = 0.5;
            static_cast<void>(
                lumenbound::assemble_diffuse_patch_problem(
                    {valid}, camera,
                    lumenbound::DiffusePatchAssemblyOptions{
                        16, 16, 3, 8, 32, 2}));
        },
        "diffuse assembly accepted a non-dyadic quadrature count");
}

void test_cornell_demo_is_deterministic_and_visually_structured() {
    const std::filesystem::path root =
        std::filesystem::current_path() /
        "lumenbound-test-output-cornell";
    const TemporaryDirectory temporary(root);
    const std::filesystem::path first = temporary.path() / "first";
    const std::filesystem::path second = temporary.path() / "second";
    const lumenbound::CornellBoxDemoOptions options{
        first, 4.0, 80.0, 32, 32, 32, 0.7};

    std::ostringstream first_summary;
    std::ostringstream first_errors;
    const lumenbound::CornellBoxDemoRunResult first_run =
        lumenbound::run_cornell_box(
            options, first_summary, first_errors);
    lumenbound::CornellBoxDemoOptions second_options = options;
    second_options.output_directory = second;
    std::ostringstream second_summary;
    std::ostringstream second_errors;
    const lumenbound::CornellBoxDemoRunResult second_run =
        lumenbound::run_cornell_box(
            second_options, second_summary, second_errors);

    require(first_run.exit_code == 0 && second_run.exit_code == 0,
            "Cornell demonstration did not reach its finite-system target");
    require_proof(
        first_run.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "Cornell finite system was not certified");
    require_target(
        first_run.certification, lumenbound::TargetStatus::Reached,
        "Cornell finite-system target was not reached");
    require(first_summary.str().find(
                "continuous_scene_certified: false") !=
                std::string::npos &&
                first_summary.str().find(
                    "assembly_status: DeterministicUnbounded") !=
                std::string::npos,
            "Cornell console summary obscures the assembly proof boundary");
    require(first_errors.str().empty() &&
                second_errors.str().empty(),
            "Cornell demonstration emitted an unexpected error");

    const std::array<std::string_view, 7> output_names{
        "candidate-coefficients.csv",
        "coefficient-bounds.csv",
        "linear-pixels.csv",
        "preview.ppm",
        "metrics.json",
        "certificate.json",
        "assembly.json",
    };
    for (const std::string_view name : output_names) {
        require(read_binary_file(first / name) ==
                    read_binary_file(second / name),
                "Cornell output is not byte-identical: " +
                    std::string(name));
    }

    const std::string preview =
        read_binary_file(first / "preview.ppm");
    require(preview.starts_with("P3\n") &&
                preview.find(
                    "Declared linear-sRGB preview; non-certifying") !=
                    std::string::npos &&
                preview.find("\n32 32\n255\n") != std::string::npos,
            "Cornell preview has an invalid format or proof label");
    const std::string assembly =
        read_binary_file(first / "assembly.json");
    require(assembly.find(
                "\"continuous_scene\":false") !=
                std::string::npos &&
                assembly.find(
                    "\"geometry\":false") != std::string::npos &&
                assembly.find(
                    "\"visibility\":false") != std::string::npos &&
                assembly.find(
                    "\"finite_system_proof_status\":\"Certified\"") !=
                    std::string::npos &&
                assembly.find(
                    "\"problem_digest\":\"sha256:") !=
                    std::string::npos,
            "Cornell assembly record omits its proof boundary");

    const lumenbound::Certificate& certificate =
        first_run.certification.certificate;
    const auto pixel_value =
        [&certificate](std::size_t band, std::size_t x,
                       std::size_t y) {
            const std::size_t pixel = (y * 32U) + x;
            return find_bound(
                       certificate.pixel_bounds, band, pixel)
                .candidate;
        };
    require(pixel_value(0, 3, 16) >
                5.0 * pixel_value(1, 3, 16),
            "Cornell left wall is not red-dominant");
    require(pixel_value(1, 28, 16) >
                5.0 * pixel_value(0, 28, 16),
            "Cornell right wall is not green-dominant");
    require(pixel_value(0, 16, 4) > 3.0 &&
                pixel_value(1, 16, 4) > 3.0 &&
                pixel_value(2, 16, 4) > 2.5,
            "Cornell ceiling emitter is not visible");

    const std::filesystem::path alternate_preview =
        temporary.path() / "alternate-preview";
    lumenbound::write_demo_outputs(
        alternate_preview, first_run.certification, 32, 32,
        lumenbound::PreviewSettings{
            lumenbound::PreviewMapping::DeclaredLinearSrgb, 0.5});
    require(read_binary_file(first / "certificate.json") ==
                read_binary_file(alternate_preview / "certificate.json") &&
                read_binary_file(first / "linear-pixels.csv") ==
                read_binary_file(
                    alternate_preview / "linear-pixels.csv"),
            "preview exposure changed a proof-bearing Cornell output");
    require(read_binary_file(first / "preview.ppm") !=
                read_binary_file(alternate_preview / "preview.ppm"),
            "preview exposure did not change the display-only output");

    lumenbound::CornellBoxDemoOptions preview_only_options = options;
    preview_only_options.output_directory =
        temporary.path() / "preview-only";
    preview_only_options.preview_only = true;
    std::ostringstream preview_only_summary;
    std::ostringstream preview_only_errors;
    const lumenbound::CornellBoxDemoRunResult preview_only_run =
        lumenbound::run_cornell_box(
            preview_only_options, preview_only_summary,
            preview_only_errors);
    require(preview_only_run.exit_code == 0 &&
                preview_only_errors.str().empty(),
            "Cornell preview-only render failed");
    require(preview_only_summary.str().find(
                "render_mode: PreviewOnly") != std::string::npos &&
                preview_only_summary.str().find(
                    "finite_system_proof_status: NotRun") !=
                    std::string::npos,
            "Cornell preview-only summary implies certification");
    const std::filesystem::path preview_only_directory =
        preview_only_options.output_directory;
    const std::string preview_only =
        read_binary_file(preview_only_directory / "preview.ppm");
    require(preview_only.find(
                "Preview-only candidate render; no certificate generated") !=
                std::string::npos &&
                !std::filesystem::exists(
                    preview_only_directory / "certificate.json") &&
                !std::filesystem::exists(
                    preview_only_directory / "metrics.json") &&
                !std::filesystem::exists(
                    preview_only_directory / "assembly.json"),
            "Cornell preview-only output obscures its proof boundary");

    lumenbound::CornellBoxDemoOptions invalid = options;
    invalid.image_width = 8;
    std::ostringstream invalid_summary;
    std::ostringstream invalid_errors;
    const lumenbound::CornellBoxDemoRunResult invalid_run =
        lumenbound::run_cornell_box(
            invalid, invalid_summary, invalid_errors);
    require(invalid_run.exit_code != 0 &&
                invalid_errors.str().find(
                    "cornell_image_dimensions_must_be_16_to_256") !=
                    std::string::npos,
            "Cornell demo did not reject an unsupported image size");
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
                                    1.0, 1000.0, 0},
            summary, errors);

    require(run.exit_code != 0,
            "zero iteration budget returned a successful exit code");
    require_proof(
        run.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "zero iteration budget invalidated an established proof");
    require_target(
        run.certification, lumenbound::TargetStatus::IterationLimit,
        "zero iteration budget received an incorrect target status");
    require_certified_bounds(run.certification.certificate);
    require(run.certification.certificate.target_reason ==
                "zero_iteration_budget_before_target",
            "zero iteration budget received an imprecise reason");
    require(errors.str().find("IterationLimit") !=
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
    require_proof(
        run.certification, lumenbound::ProofStatus::Certified,
        lumenbound::ProofFailureCode::None,
        "target stagnation invalidated an established proof");
    require_target(
        run.certification, lumenbound::TargetStatus::Stagnated,
        "unreachable PSNR target received an incorrect status");
    require_certified_bounds(run.certification.certificate);
    require(run.certification.certificate.target_reason ==
                "interval_propagation_stagnated_before_target",
            "unreachable PSNR target received an imprecise reason");
    require(errors.str().find("Stagnated") !=
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
        {"dense algebra", test_dense_algebra},
        {"candidate solver matches manufactured solution",
         test_candidate_solver_matches_manufactured_solution},
        {"iteration snapshots are monotone and enclosing",
         test_iteration_snapshots_are_monotone_and_contain_exact_solution},
        {"iteration width contracts",
         test_iteration_width_contracts},
        {"residual certificate contains measured errors",
         test_residual_certificate_contains_measured_errors},
        {"target outcomes preserve certified proof",
         test_target_outcomes_preserve_certified_proof},
        {"iteration snapshots are opt-in and proof-neutral",
         test_iteration_snapshots_are_opt_in_and_proof_neutral},
        {"problem digest is canonical and sensitive",
         test_problem_digest_is_canonical_and_sensitive},
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
        {"sparse projection preserves dense contract",
         test_sparse_projection_preserves_dense_contract},
        {"diffuse patch assembly is positive and contractive",
         test_diffuse_patch_assembly_is_positive_and_contractive},
        {"Cornell demo is deterministic and visually structured",
         test_cornell_demo_is_deterministic_and_visually_structured},
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
