#include "lumenbound/math/dense_matrix.hpp"

#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lumenbound {
namespace {

[[nodiscard]] std::size_t checked_element_count(std::size_t rows,
                                                std::size_t columns) {
    if (rows != 0 &&
        columns > (std::numeric_limits<std::size_t>::max() / rows)) {
        throw std::invalid_argument("matrix dimensions overflow");
    }
    return rows * columns;
}

}  // namespace

DenseMatrix::DenseMatrix(std::size_t rows, std::size_t columns, double value)
    : rows_(rows),
      columns_(columns),
      values_(checked_element_count(rows, columns), value) {}

DenseMatrix::DenseMatrix(std::size_t rows, std::size_t columns,
                         std::initializer_list<double> values)
    : DenseMatrix(rows, columns, std::vector<double>(values)) {}

DenseMatrix::DenseMatrix(std::size_t rows, std::size_t columns,
                         std::vector<double> values)
    : rows_(rows), columns_(columns), values_(std::move(values)) {
    if (values_.size() != checked_element_count(rows_, columns_)) {
        throw std::invalid_argument("matrix value count mismatch");
    }
}

std::size_t DenseMatrix::rows() const noexcept {
    return rows_;
}

std::size_t DenseMatrix::columns() const noexcept {
    return columns_;
}

bool DenseMatrix::empty() const noexcept {
    return rows_ == 0 || columns_ == 0;
}

bool DenseMatrix::is_finite() const noexcept {
    return std::all_of(values_.begin(), values_.end(),
                       [](double value) { return std::isfinite(value); });
}

bool DenseMatrix::is_nonnegative() const noexcept {
    return std::all_of(values_.begin(), values_.end(),
                       [](double value) { return value >= 0.0; });
}

double& DenseMatrix::operator()(std::size_t row, std::size_t column) {
    if (row >= rows_ || column >= columns_) {
        throw std::out_of_range("matrix index out of range");
    }
    return values_[(row * columns_) + column];
}

const double& DenseMatrix::operator()(std::size_t row,
                                      std::size_t column) const {
    if (row >= rows_ || column >= columns_) {
        throw std::out_of_range("matrix index out of range");
    }
    return values_[(row * columns_) + column];
}

const std::vector<double>& DenseMatrix::values() const noexcept {
    return values_;
}

DenseVector DenseMatrix::multiply(const DenseVector& vector) const {
    if (columns_ != vector.size()) {
        throw std::invalid_argument(
            "matrix-vector multiplication dimension mismatch");
    }

    DenseVector result(rows_, 0.0);
    for (std::size_t row = 0; row < rows_; ++row) {
        double sum = 0.0;
        for (std::size_t column = 0; column < columns_; ++column) {
            sum += (*this)(row, column) * vector[column];
        }
        result[row] = sum;
    }
    return result;
}

double DenseMatrix::infinity_norm() const {
    double maximum_row_sum = 0.0;
    for (std::size_t row = 0; row < rows_; ++row) {
        double row_sum = 0.0;
        for (std::size_t column = 0; column < columns_; ++column) {
            row_sum += std::abs((*this)(row, column));
        }
        maximum_row_sum = std::max(maximum_row_sum, row_sum);
    }
    return maximum_row_sum;
}

double DenseMatrix::conservative_infinity_norm() const {
    if (!is_finite()) {
        throw std::invalid_argument(
            "matrix norm requires finite coefficients");
    }

    double maximum_row_sum = 0.0;
    for (std::size_t row = 0; row < rows_; ++row) {
        double row_sum = 0.0;
        for (std::size_t column = 0; column < columns_; ++column) {
            row_sum =
                math::add_up(row_sum, std::abs((*this)(row, column)));
        }
        maximum_row_sum = std::max(maximum_row_sum, row_sum);
    }
    return maximum_row_sum;
}

DenseSolveResult DenseMatrix::solve(
    const DenseVector& right_hand_side) const {
    if (rows_ == 0 || rows_ != columns_ ||
        right_hand_side.size() != rows_) {
        throw std::invalid_argument(
            "dense solve requires compatible nonempty square dimensions");
    }
    if (!is_finite() || !right_hand_side.is_finite()) {
        throw std::invalid_argument("dense solve requires finite input");
    }

    DenseMatrix factors = *this;
    DenseVector right = right_hand_side;
    double minimum_absolute_pivot =
        std::numeric_limits<double>::infinity();

    for (std::size_t pivot_column = 0; pivot_column < rows_;
         ++pivot_column) {
        std::size_t pivot_row = pivot_column;
        double pivot_magnitude =
            std::abs(factors(pivot_column, pivot_column));

        for (std::size_t row = pivot_column + 1; row < rows_; ++row) {
            const double candidate =
                std::abs(factors(row, pivot_column));
            if (candidate > pivot_magnitude) {
                pivot_magnitude = candidate;
                pivot_row = row;
            }
        }

        if (pivot_magnitude == 0.0 || !std::isfinite(pivot_magnitude)) {
            throw std::runtime_error(
                "dense solve encountered a zero or non-finite pivot");
        }

        if (pivot_row != pivot_column) {
            for (std::size_t column = 0; column < columns_; ++column) {
                std::swap(factors(pivot_column, column),
                          factors(pivot_row, column));
            }
            std::swap(right[pivot_column], right[pivot_row]);
        }

        const double pivot = factors(pivot_column, pivot_column);
        minimum_absolute_pivot =
            std::min(minimum_absolute_pivot, std::abs(pivot));

        for (std::size_t row = pivot_column + 1; row < rows_; ++row) {
            const double factor = factors(row, pivot_column) / pivot;
            factors(row, pivot_column) = 0.0;
            for (std::size_t column = pivot_column + 1; column < columns_;
                 ++column) {
                factors(row, column) -=
                    factor * factors(pivot_column, column);
            }
            right[row] -= factor * right[pivot_column];
        }
    }

    if (!factors.is_finite() || !right.is_finite()) {
        throw std::runtime_error(
            "dense elimination produced a non-finite value");
    }

    DenseVector solution(rows_, 0.0);
    for (std::size_t reverse_index = rows_; reverse_index > 0;
         --reverse_index) {
        const std::size_t row = reverse_index - 1;
        double value = right[row];
        for (std::size_t column = row + 1; column < columns_; ++column) {
            value -= factors(row, column) * solution[column];
        }
        const double pivot = factors(row, row);
        if (pivot == 0.0 || !std::isfinite(pivot)) {
            throw std::runtime_error(
                "dense back substitution encountered an invalid pivot");
        }
        solution[row] = value / pivot;
    }

    if (!solution.is_finite()) {
        throw std::runtime_error(
            "dense back substitution produced a non-finite value");
    }

    return DenseSolveResult{std::move(solution), minimum_absolute_pivot};
}

DenseMatrix DenseMatrix::identity(std::size_t size) {
    DenseMatrix result(size, size, 0.0);
    for (std::size_t index = 0; index < size; ++index) {
        result(index, index) = 1.0;
    }
    return result;
}

}  // namespace lumenbound
