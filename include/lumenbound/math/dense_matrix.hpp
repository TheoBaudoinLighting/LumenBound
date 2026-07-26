#pragma once

#include "lumenbound/math/dense_vector.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace lumenbound {

struct DenseSolveResult {
    DenseVector solution;
    double minimum_absolute_pivot;
};

class DenseMatrix {
public:
    DenseMatrix() = default;
    DenseMatrix(std::size_t rows, std::size_t columns, double value = 0.0);
    DenseMatrix(std::size_t rows, std::size_t columns,
                std::initializer_list<double> values);
    DenseMatrix(std::size_t rows, std::size_t columns,
                std::vector<double> values);

    [[nodiscard]] std::size_t rows() const noexcept;
    [[nodiscard]] std::size_t columns() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool is_finite() const noexcept;
    [[nodiscard]] bool is_nonnegative() const noexcept;

    [[nodiscard]] double& operator()(std::size_t row, std::size_t column);
    [[nodiscard]] const double& operator()(std::size_t row,
                                           std::size_t column) const;
    [[nodiscard]] const std::vector<double>& values() const noexcept;

    [[nodiscard]] DenseVector multiply(const DenseVector& vector) const;
    [[nodiscard]] double infinity_norm() const;
    [[nodiscard]] double conservative_infinity_norm() const;
    [[nodiscard]] DenseSolveResult solve(const DenseVector& right_hand_side)
        const;

    [[nodiscard]] static DenseMatrix identity(std::size_t size);

private:
    std::size_t rows_{0};
    std::size_t columns_{0};
    std::vector<double> values_;
};

}  // namespace lumenbound
