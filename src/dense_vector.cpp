#include "lumenbound/math/dense_vector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace lumenbound {

DenseVector::DenseVector(std::size_t size, double value)
    : values_(size, value) {}

DenseVector::DenseVector(std::initializer_list<double> values)
    : values_(values) {}

DenseVector::DenseVector(std::vector<double> values)
    : values_(std::move(values)) {}

std::size_t DenseVector::size() const noexcept {
    return values_.size();
}

bool DenseVector::empty() const noexcept {
    return values_.empty();
}

double DenseVector::infinity_norm() const noexcept {
    double result = 0.0;
    for (const double value : values_) {
        result = std::max(result, std::abs(value));
    }
    return result;
}

bool DenseVector::is_finite() const noexcept {
    return std::all_of(values_.begin(), values_.end(),
                       [](double value) { return std::isfinite(value); });
}

bool DenseVector::is_nonnegative() const noexcept {
    return std::all_of(values_.begin(), values_.end(),
                       [](double value) { return value >= 0.0; });
}

double& DenseVector::operator[](std::size_t index) {
    return values_.at(index);
}

const double& DenseVector::operator[](std::size_t index) const {
    return values_.at(index);
}

const std::vector<double>& DenseVector::values() const noexcept {
    return values_;
}

DenseVector operator+(const DenseVector& left, const DenseVector& right) {
    if (left.size() != right.size()) {
        throw std::invalid_argument("vector addition dimension mismatch");
    }

    DenseVector result(left.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        result[index] = left[index] + right[index];
    }
    return result;
}

DenseVector operator-(const DenseVector& left, const DenseVector& right) {
    if (left.size() != right.size()) {
        throw std::invalid_argument("vector subtraction dimension mismatch");
    }

    DenseVector result(left.size());
    for (std::size_t index = 0; index < left.size(); ++index) {
        result[index] = left[index] - right[index];
    }
    return result;
}

}  // namespace lumenbound
