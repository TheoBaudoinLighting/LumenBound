#pragma once

#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/math/interval.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lumenbound {

enum class ProjectionValidationCode {
    Valid,
    InvalidDimensions,
    NonFiniteInput,
    NegativeProjection,
};

struct ProjectionValidationReport {
    ProjectionValidationCode code;
    std::string reason;

    [[nodiscard]] bool valid() const noexcept {
        return code == ProjectionValidationCode::Valid;
    }
};

class Projection {
public:
    explicit Projection(DenseMatrix weights);
    Projection(std::size_t rows, std::size_t columns,
               std::vector<std::size_t> row_offsets,
               std::vector<std::size_t> column_indices,
               std::vector<double> values);

    [[nodiscard]] std::size_t pixel_count() const noexcept;
    [[nodiscard]] std::size_t coefficient_count() const noexcept;
    [[nodiscard]] std::size_t stored_entry_count() const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& row_offsets()
        const noexcept;
    [[nodiscard]] const std::vector<std::size_t>& column_indices()
        const noexcept;
    [[nodiscard]] const std::vector<double>& values() const noexcept;
    [[nodiscard]] ProjectionValidationReport validate(
        std::size_t expected_coefficient_count) const;
    [[nodiscard]] DenseVector project(const DenseVector& coefficients) const;
    [[nodiscard]] std::vector<Interval> project(
        const DenseVector& lower, const DenseVector& upper) const;

private:
    std::size_t rows_{0};
    std::size_t columns_{0};
    std::vector<std::size_t> row_offsets_;
    std::vector<std::size_t> column_indices_;
    std::vector<double> values_;
};

}  // namespace lumenbound
