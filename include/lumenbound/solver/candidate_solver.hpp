#pragma once

#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <vector>

namespace lumenbound {

struct CandidateSolution {
    std::vector<DenseVector> values;
    std::vector<DenseVector> residuals;
    double residual_infinity_norm{0.0};
    double minimum_absolute_pivot{0.0};
};

[[nodiscard]] CandidateSolution solve_candidate(
    const TransportSystem& system);

}  // namespace lumenbound
