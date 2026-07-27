#include "lumenbound/solver/candidate_solver.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace lumenbound {

CandidateSolution solve_candidate(const TransportSystem& system) {
    const TransportValidationReport validation = system.validate();
    if (!validation.valid()) {
        throw std::invalid_argument(
            "candidate solve requires a valid transport system");
    }

    CandidateSolution result;
    result.residual_infinity_norm = 0.0;
    result.minimum_absolute_pivot =
        std::numeric_limits<double>::infinity();
    result.values.reserve(system.spectral_coefficient_count());
    result.residuals.reserve(system.spectral_coefficient_count());

    const std::size_t coefficient_count =
        system.transport_coefficient_count();
    for (std::size_t band = 0;
         band < system.spectral_coefficient_count(); ++band) {
        DenseMatrix system_matrix =
            DenseMatrix::identity(coefficient_count);
        const DenseMatrix& transport = system.transport(band);
        for (std::size_t row = 0; row < coefficient_count; ++row) {
            for (std::size_t column = 0; column < coefficient_count;
                 ++column) {
                system_matrix(row, column) -= transport(row, column);
            }
        }

        DenseSolveResult solve =
            system_matrix.solve(system.emission(band));
        const DenseVector residual =
            system.emission(band) +
            transport.multiply(solve.solution) - solve.solution;
        if (!residual.is_finite()) {
            throw std::runtime_error(
                "candidate residual produced a non-finite value");
        }

        result.residual_infinity_norm =
            std::max(result.residual_infinity_norm,
                     residual.infinity_norm());
        result.minimum_absolute_pivot =
            std::min(result.minimum_absolute_pivot,
                     solve.minimum_absolute_pivot);
        result.values.push_back(std::move(solve.solution));
        result.residuals.push_back(residual);
    }

    return result;
}

}  // namespace lumenbound
