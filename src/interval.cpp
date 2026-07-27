#include "lumenbound/math/interval.hpp"

#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cfloat>
#include <cfenv>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "Certified arithmetic cannot be compiled with fast-math semantics"
#endif

static_assert(
    FLT_EVAL_METHOD == 0,
    "Certified arithmetic requires source-precision evaluation");

#if defined(_MSC_VER)
#pragma fenv_access(on)
#elif defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif

namespace lumenbound::math {
namespace {

enum class BinaryOperation {
    Add,
    Subtract,
    Multiply,
    Divide,
};

[[nodiscard]] double evaluate(double left, double right,
                              BinaryOperation operation, int mode,
                              double direction) {
    if (!std::isfinite(left) || !std::isfinite(right)) {
        throw std::invalid_argument(
            "certified arithmetic requires finite operands");
    }
    if (operation == BinaryOperation::Divide && right == 0.0) {
        throw std::domain_error("division by zero in certified arithmetic");
    }

    volatile double volatile_left = left;
    volatile double volatile_right = right;
    double result = 0.0;
    std::fenv_t environment{};
    if (std::feholdexcept(&environment) != 0) {
        throw std::runtime_error("directed rounding mode is unavailable");
    }
    if (std::fesetround(mode) != 0) {
        static_cast<void>(std::fesetenv(&environment));
        throw std::runtime_error("directed rounding mode is unavailable");
    }
    switch (operation) {
    case BinaryOperation::Add:
        result = volatile_left + volatile_right;
        break;
    case BinaryOperation::Subtract:
        result = volatile_left - volatile_right;
        break;
    case BinaryOperation::Multiply:
        result = volatile_left * volatile_right;
        break;
    case BinaryOperation::Divide:
        result = volatile_left / volatile_right;
        break;
    }

    double widened = result;
    if (std::isfinite(result)) {
        widened = std::nextafter(result, direction);
    }
    if (std::fesetenv(&environment) != 0) {
        throw std::runtime_error(
            "floating-point environment restoration failed");
    }

    if (!std::isfinite(result)) {
        throw std::overflow_error(
            "certified arithmetic produced a non-finite result");
    }

    if (!std::isfinite(widened)) {
        throw std::overflow_error(
            "outward rounding produced a non-finite endpoint");
    }
    return widened;
}

void validate_finite_operands(double left, double right) {
    if (!std::isfinite(left) || !std::isfinite(right)) {
        throw std::invalid_argument(
            "certified arithmetic requires finite operands");
    }
}

}  // namespace

bool supports_certified_rounding() noexcept {
    volatile bool binary64_format_supported =
        std::numeric_limits<double>::is_iec559 &&
        std::numeric_limits<double>::radix == 2 &&
        std::numeric_limits<double>::digits == 53;
    if (!binary64_format_supported) {
        return false;
    }

    std::fenv_t environment{};
    if (std::feholdexcept(&environment) != 0) {
        return false;
    }
    if (std::fesetround(FE_TONEAREST) != 0) {
        static_cast<void>(std::fesetenv(&environment));
        return false;
    }

    volatile double smallest_normal = std::numeric_limits<double>::min();
    volatile double smallest_subnormal =
        std::numeric_limits<double>::denorm_min();
    volatile double half = 0.5;
    volatile double subnormal_amplifier = 0x1p52;
    volatile double subnormal_denominator = smallest_subnormal;
    volatile double produced_subnormal = smallest_normal * half;
    volatile double amplified_subnormal =
        smallest_subnormal * subnormal_amplifier;
    volatile double subnormal_ratio =
        smallest_subnormal / subnormal_denominator;

    if (produced_subnormal <= 0.0 ||
        produced_subnormal >= smallest_normal ||
        amplified_subnormal != smallest_normal ||
        subnormal_ratio != 1.0) {
        static_cast<void>(std::fesetenv(&environment));
        return false;
    }

    if (std::fesetround(FE_DOWNWARD) != 0) {
        static_cast<void>(std::fesetenv(&environment));
        return false;
    }
    volatile double one = 1.0;
    volatile double half_ulp = 0x1p-53;
    volatile double quarter_ulp = 0x1p-54;
    volatile double ten = 10.0;
    volatile double next_above_one =
        std::nextafter(1.0,
                       std::numeric_limits<double>::infinity());
    volatile double downward_add = one + half_ulp;
    volatile double downward_subtract = one - quarter_ulp;
    volatile double downward_multiply =
        next_above_one * next_above_one;
    volatile double downward_divide = one / ten;

    if (std::fesetround(FE_UPWARD) != 0) {
        static_cast<void>(std::fesetenv(&environment));
        return false;
    }
    volatile double upward_add = one + half_ulp;
    volatile double upward_subtract = one - quarter_ulp;
    volatile double upward_multiply =
        next_above_one * next_above_one;
    volatile double upward_divide = one / ten;

    const bool restored = std::fesetenv(&environment) == 0;
    return restored && downward_add < upward_add &&
           downward_subtract < upward_subtract &&
           downward_multiply < upward_multiply &&
           downward_divide < upward_divide;
}

double add_down(double left, double right) {
    validate_finite_operands(left, right);
    if (right == 0.0) {
        return left;
    }
    if (left == 0.0) {
        return right;
    }
    return evaluate(left, right, BinaryOperation::Add, FE_DOWNWARD,
                    -std::numeric_limits<double>::infinity());
}

double add_up(double left, double right) {
    validate_finite_operands(left, right);
    if (right == 0.0) {
        return left;
    }
    if (left == 0.0) {
        return right;
    }
    return evaluate(left, right, BinaryOperation::Add, FE_UPWARD,
                    std::numeric_limits<double>::infinity());
}

double subtract_down(double left, double right) {
    validate_finite_operands(left, right);
    if (right == 0.0) {
        return left;
    }
    if (left == right) {
        return 0.0;
    }
    return evaluate(left, right, BinaryOperation::Subtract, FE_DOWNWARD,
                    -std::numeric_limits<double>::infinity());
}

double subtract_up(double left, double right) {
    validate_finite_operands(left, right);
    if (right == 0.0) {
        return left;
    }
    if (left == right) {
        return 0.0;
    }
    return evaluate(left, right, BinaryOperation::Subtract, FE_UPWARD,
                    std::numeric_limits<double>::infinity());
}

double multiply_down(double left, double right) {
    validate_finite_operands(left, right);
    if (left == 0.0 || right == 0.0) {
        return 0.0;
    }
    if (left == 1.0) {
        return right;
    }
    if (right == 1.0) {
        return left;
    }
    return evaluate(left, right, BinaryOperation::Multiply, FE_DOWNWARD,
                    -std::numeric_limits<double>::infinity());
}

double multiply_up(double left, double right) {
    validate_finite_operands(left, right);
    if (left == 0.0 || right == 0.0) {
        return 0.0;
    }
    if (left == 1.0) {
        return right;
    }
    if (right == 1.0) {
        return left;
    }
    return evaluate(left, right, BinaryOperation::Multiply, FE_UPWARD,
                    std::numeric_limits<double>::infinity());
}

double divide_down(double numerator, double denominator) {
    validate_finite_operands(numerator, denominator);
    if (denominator == 0.0) {
        throw std::domain_error("division by zero in certified arithmetic");
    }
    if (numerator == 0.0) {
        return 0.0;
    }
    if (denominator == 1.0) {
        return numerator;
    }
    return evaluate(numerator, denominator, BinaryOperation::Divide,
                    FE_DOWNWARD,
                    -std::numeric_limits<double>::infinity());
}

double divide_up(double numerator, double denominator) {
    validate_finite_operands(numerator, denominator);
    if (denominator == 0.0) {
        throw std::domain_error("division by zero in certified arithmetic");
    }
    if (numerator == 0.0) {
        return 0.0;
    }
    if (denominator == 1.0) {
        return numerator;
    }
    return evaluate(numerator, denominator, BinaryOperation::Divide,
                    FE_UPWARD, std::numeric_limits<double>::infinity());
}

}  // namespace lumenbound::math

namespace lumenbound {
namespace {

[[nodiscard]] Interval logarithm_from_atanh_argument(const Interval& z) {
    constexpr std::size_t term_count = 24;

    const Interval z_squared = z * z;
    Interval term = z;
    Interval sum = Interval::point(0.0);

    for (std::size_t term_index = 0; term_index < term_count; ++term_index) {
        if (term_index != 0) {
            term = term * z_squared;
        }
        const double denominator =
            static_cast<double>((2U * term_index) + 1U);
        sum = sum + (term / Interval::point(denominator));
    }

    const double next_denominator =
        static_cast<double>((2U * term_count) + 1U);
    const Interval next_term = term * z_squared;
    const Interval geometric_tail =
        (next_term / Interval::point(next_denominator)) /
        (Interval::point(1.0) - z_squared);

    const Interval partial = Interval::point(2.0) * sum;
    const Interval total_upper =
        Interval::point(2.0) * (sum + geometric_tail);
    return Interval(partial.lower(), total_upper.upper());
}

[[nodiscard]] Interval logarithm_two() {
    const Interval one_third =
        Interval::point(1.0) / Interval::point(3.0);
    return logarithm_from_atanh_argument(one_third);
}

[[nodiscard]] Interval natural_logarithm(double value) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::domain_error(
            "certified logarithm requires a finite positive value");
    }

    int exponent = 0;
    double mantissa = std::frexp(value, &exponent);
    mantissa *= 2.0;
    --exponent;

    const Interval mantissa_interval = Interval::point(mantissa);
    const Interval z =
        (mantissa_interval - Interval::point(1.0)) /
        (mantissa_interval + Interval::point(1.0));
    const Interval logarithm_mantissa =
        logarithm_from_atanh_argument(z);
    const Interval exponent_term =
        Interval::point(static_cast<double>(exponent)) * logarithm_two();
    return logarithm_mantissa + exponent_term;
}

}  // namespace

Interval::Interval(double lower, double upper)
    : lower_(lower), upper_(upper) {
    if (!std::isfinite(lower_) || !std::isfinite(upper_)) {
        throw std::invalid_argument("interval endpoints must be finite");
    }
    if (lower_ > upper_) {
        throw std::invalid_argument("interval endpoints are reversed");
    }
}

Interval Interval::point(double value) {
    return Interval(value, value);
}

Interval Interval::outward(double lower, double upper) {
    if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper) {
        throw std::invalid_argument(
            "outward interval construction requires ordered finite values");
    }
    const double widened_lower =
        std::nextafter(lower, -std::numeric_limits<double>::infinity());
    const double widened_upper =
        std::nextafter(upper, std::numeric_limits<double>::infinity());
    return Interval(widened_lower, widened_upper);
}

double Interval::lower() const noexcept {
    return lower_;
}

double Interval::upper() const noexcept {
    return upper_;
}

double Interval::width() const {
    return math::subtract_up(upper_, lower_);
}

double Interval::midpoint() const noexcept {
    return std::midpoint(lower_, upper_);
}

bool Interval::contains(double value) const noexcept {
    return lower_ <= value && value <= upper_;
}

bool Interval::contains(const Interval& other) const noexcept {
    return lower_ <= other.lower_ && other.upper_ <= upper_;
}

bool Interval::is_finite() const noexcept {
    return std::isfinite(lower_) && std::isfinite(upper_);
}

std::optional<Interval> Interval::intersection(
    const Interval& other) const {
    const double lower = std::max(lower_, other.lower_);
    const double upper = std::min(upper_, other.upper_);
    if (lower > upper) {
        return std::nullopt;
    }
    return Interval(lower, upper);
}

Interval operator+(const Interval& left, const Interval& right) {
    return Interval(math::add_down(left.lower_, right.lower_),
                    math::add_up(left.upper_, right.upper_));
}

Interval operator-(const Interval& left, const Interval& right) {
    return Interval(math::subtract_down(left.lower_, right.upper_),
                    math::subtract_up(left.upper_, right.lower_));
}

Interval operator*(const Interval& left, const Interval& right) {
    const double downward_products[] = {
        math::multiply_down(left.lower_, right.lower_),
        math::multiply_down(left.lower_, right.upper_),
        math::multiply_down(left.upper_, right.lower_),
        math::multiply_down(left.upper_, right.upper_),
    };
    const double upward_products[] = {
        math::multiply_up(left.lower_, right.lower_),
        math::multiply_up(left.lower_, right.upper_),
        math::multiply_up(left.upper_, right.lower_),
        math::multiply_up(left.upper_, right.upper_),
    };

    return Interval(*std::min_element(std::begin(downward_products),
                                      std::end(downward_products)),
                    *std::max_element(std::begin(upward_products),
                                      std::end(upward_products)));
}

Interval operator/(const Interval& left, const Interval& right) {
    if (right.lower_ <= 0.0 && right.upper_ >= 0.0) {
        throw std::domain_error("interval divisor contains zero");
    }

    const double downward_quotients[] = {
        math::divide_down(left.lower_, right.lower_),
        math::divide_down(left.lower_, right.upper_),
        math::divide_down(left.upper_, right.lower_),
        math::divide_down(left.upper_, right.upper_),
    };
    const double upward_quotients[] = {
        math::divide_up(left.lower_, right.lower_),
        math::divide_up(left.lower_, right.upper_),
        math::divide_up(left.upper_, right.lower_),
        math::divide_up(left.upper_, right.upper_),
    };

    return Interval(*std::min_element(std::begin(downward_quotients),
                                      std::end(downward_quotients)),
                    *std::max_element(std::begin(upward_quotients),
                                      std::end(upward_quotients)));
}

Interval certified_log10(double value) {
    if (!math::supports_certified_rounding()) {
        throw std::runtime_error(
            "certified logarithm requires supported binary64 arithmetic");
    }
    return natural_logarithm(value) / natural_logarithm(10.0);
}

}  // namespace lumenbound
