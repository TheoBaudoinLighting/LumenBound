#pragma once

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace lumenbound {

class DenseVector {
public:
    DenseVector() = default;
    explicit DenseVector(std::size_t size, double value = 0.0);
    DenseVector(std::initializer_list<double> values);
    explicit DenseVector(std::vector<double> values);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] double infinity_norm() const noexcept;
    [[nodiscard]] bool is_finite() const noexcept;
    [[nodiscard]] bool is_nonnegative() const noexcept;

    [[nodiscard]] double& operator[](std::size_t index);
    [[nodiscard]] const double& operator[](std::size_t index) const;
    [[nodiscard]] const std::vector<double>& values() const noexcept;

    friend DenseVector operator+(const DenseVector& left,
                                 const DenseVector& right);
    friend DenseVector operator-(const DenseVector& left,
                                 const DenseVector& right);

private:
    std::vector<double> values_;
};

}  // namespace lumenbound
