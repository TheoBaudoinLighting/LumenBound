#include "lumenbound/projection/projection.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lumenbound {
namespace {

[[nodiscard]] std::size_t checked_row_offset_count(std::size_t rows) {
    if (rows == std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("projection row count is too large");
    }
    return rows + 1U;
}

[[nodiscard]] std::size_t checked_element_count(std::size_t rows,
                                                std::size_t columns) {
    if (rows != 0 &&
        columns > (std::numeric_limits<std::size_t>::max() / rows)) {
        throw std::invalid_argument("projection dimensions overflow");
    }
    return rows * columns;
}

[[nodiscard]] bool is_omitted_positive_zero(double value) noexcept {
    return value == 0.0 && !std::signbit(value);
}

void validate_sparse_structure(
    std::size_t rows, std::size_t columns,
    const std::vector<std::size_t>& row_offsets,
    const std::vector<std::size_t>& column_indices,
    const std::vector<double>& values) {
    static_cast<void>(checked_element_count(rows, columns));
    const std::size_t expected_offset_count =
        checked_row_offset_count(rows);
    if (row_offsets.empty() ||
        row_offsets.size() != expected_offset_count ||
        row_offsets.front() != 0U) {
        throw std::invalid_argument(
            "projection row offsets do not match row count");
    }
    if (column_indices.size() != values.size() ||
        row_offsets.back() != values.size()) {
        throw std::invalid_argument(
            "projection sparse array sizes do not match");
    }

    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t begin = row_offsets[row];
        const std::size_t end = row_offsets[row + 1U];
        if (begin > end || end > values.size()) {
            throw std::invalid_argument(
                "projection row offsets are not monotone");
        }

        bool has_previous_column = false;
        std::size_t previous_column = 0;
        for (std::size_t entry = begin; entry < end; ++entry) {
            const std::size_t column = column_indices[entry];
            if (column >= columns) {
                throw std::invalid_argument(
                    "projection column index is out of range");
            }
            if (has_previous_column && column <= previous_column) {
                throw std::invalid_argument(
                    "projection columns must be sorted and unique");
            }
            previous_column = column;
            has_previous_column = true;
        }
    }
}

}  // namespace

Projection::Projection(DenseMatrix weights)
    : rows_(weights.rows()), columns_(weights.columns()) {
    static_cast<void>(checked_element_count(rows_, columns_));
    row_offsets_.reserve(checked_row_offset_count(rows_));
    row_offsets_.push_back(0U);

    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t column = 0; column < columns_; ++column) {
            const double value = weights(row, column);
            // Positive zero is the sole implicit CSR value. Keeping negative
            // zero and non-finite values is necessary because validation and
            // the canonical problem digest must still observe the supplied
            // binary64 input.
            if (!is_omitted_positive_zero(value)) {
                column_indices_.push_back(column);
                values_.push_back(value);
            }
        }
        row_offsets_.push_back(values_.size());
    }
}

Projection::Projection(std::size_t rows, std::size_t columns,
                       std::vector<std::size_t> row_offsets,
                       std::vector<std::size_t> column_indices,
                       std::vector<double> values)
    : rows_(rows), columns_(columns) {
    validate_sparse_structure(rows, columns, row_offsets, column_indices,
                              values);

    row_offsets_.reserve(checked_row_offset_count(rows_));
    row_offsets_.push_back(0U);
    column_indices_.reserve(column_indices.size());
    values_.reserve(values.size());
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t entry = row_offsets[row];
             entry < row_offsets[row + 1U]; ++entry) {
            const double value = values[entry];
            if (!is_omitted_positive_zero(value)) {
                column_indices_.push_back(column_indices[entry]);
                values_.push_back(value);
            }
        }
        row_offsets_.push_back(values_.size());
    }
}

std::size_t Projection::pixel_count() const noexcept {
    return rows_;
}

std::size_t Projection::coefficient_count() const noexcept {
    return columns_;
}

std::size_t Projection::stored_entry_count() const noexcept {
    return values_.size();
}

const std::vector<std::size_t>& Projection::row_offsets() const noexcept {
    return row_offsets_;
}

const std::vector<std::size_t>& Projection::column_indices() const noexcept {
    return column_indices_;
}

const std::vector<double>& Projection::values() const noexcept {
    return values_;
}

ProjectionValidationReport Projection::validate(
    std::size_t expected_coefficient_count) const {
    if (rows_ == 0 || columns_ == 0 ||
        columns_ != expected_coefficient_count) {
        return {ProjectionValidationCode::InvalidDimensions,
                "projection_dimensions_do_not_match_transport"};
    }
    if (!std::all_of(values_.begin(), values_.end(),
                     [](double value) { return std::isfinite(value); })) {
        return {ProjectionValidationCode::NonFiniteInput,
                "projection_contains_non_finite_value"};
    }
    if (!std::all_of(values_.begin(), values_.end(),
                     [](double value) { return value >= 0.0; })) {
        return {ProjectionValidationCode::NegativeProjection,
                "projection_contains_negative_value"};
    }
    return {ProjectionValidationCode::Valid, "projection_is_valid"};
}

DenseVector Projection::project(const DenseVector& coefficients) const {
    if (coefficients.size() != coefficient_count()) {
        throw std::invalid_argument(
            "projection matrix-vector dimensions do not match");
    }

    DenseVector result(pixel_count(), 0.0);
    for (std::size_t pixel = 0; pixel < pixel_count(); ++pixel) {
        double sum = 0.0;
        for (std::size_t entry = row_offsets_[pixel];
             entry < row_offsets_[pixel + 1U]; ++entry) {
            sum += values_[entry] *
                   coefficients[column_indices_[entry]];
        }
        result[pixel] = sum;
    }
    return result;
}

std::vector<Interval> Projection::project(
    const DenseVector& lower, const DenseVector& upper) const {
    if (lower.size() != coefficient_count() ||
        upper.size() != coefficient_count()) {
        throw std::invalid_argument(
            "projection interval dimensions do not match");
    }

    for (std::size_t coefficient = 0;
         coefficient < coefficient_count(); ++coefficient) {
        if (lower[coefficient] > upper[coefficient]) {
            throw std::invalid_argument(
                "coefficient interval endpoints are reversed");
        }
        static_cast<void>(
            Interval(lower[coefficient], upper[coefficient]));
    }

    std::vector<Interval> result;
    result.reserve(pixel_count());
    for (std::size_t pixel = 0; pixel < pixel_count(); ++pixel) {
        Interval sum = Interval::point(0.0);
        for (std::size_t entry = row_offsets_[pixel];
             entry < row_offsets_[pixel + 1U]; ++entry) {
            const std::size_t coefficient = column_indices_[entry];
            const Interval weight = Interval::point(values_[entry]);
            const Interval coefficient_interval(
                lower[coefficient], upper[coefficient]);
            sum = sum + (weight * coefficient_interval);
        }
        result.push_back(sum);
    }
    return result;
}

}  // namespace lumenbound
