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

    [[nodiscard]] std::size_t pixel_count() const noexcept;
    [[nodiscard]] std::size_t coefficient_count() const noexcept;
    [[nodiscard]] const DenseMatrix& weights() const noexcept;
    [[nodiscard]] ProjectionValidationReport validate(
        std::size_t expected_coefficient_count) const;
    [[nodiscard]] DenseVector project(const DenseVector& coefficients) const;
    [[nodiscard]] std::vector<Interval> project(
        const DenseVector& lower, const DenseVector& upper) const;

private:
    DenseMatrix weights_;
};

}  // namespace lumenbound
