#pragma once

#include <optional>

namespace lumenbound {

class Interval {
public:
    Interval(double lower, double upper);

    [[nodiscard]] static Interval point(double value);
    [[nodiscard]] static Interval outward(double lower, double upper);

    [[nodiscard]] double lower() const noexcept;
    [[nodiscard]] double upper() const noexcept;
    [[nodiscard]] double width() const;
    [[nodiscard]] double midpoint() const noexcept;
    [[nodiscard]] bool contains(double value) const noexcept;
    [[nodiscard]] bool contains(const Interval& other) const noexcept;
    [[nodiscard]] bool is_finite() const noexcept;

    [[nodiscard]] std::optional<Interval> intersection(
        const Interval& other) const;

    friend Interval operator+(const Interval& left, const Interval& right);
    friend Interval operator-(const Interval& left, const Interval& right);
    friend Interval operator*(const Interval& left, const Interval& right);
    friend Interval operator/(const Interval& left, const Interval& right);

private:
    double lower_;
    double upper_;
};

[[nodiscard]] Interval certified_log10(double value);

}  // namespace lumenbound
