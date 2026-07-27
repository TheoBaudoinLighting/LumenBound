#pragma once

#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lumenbound {

enum class TransportValidationCode {
    Valid,
    InvalidDimensions,
    NonFiniteInput,
    NegativeEmission,
    NegativeTransport,
    NonContractive,
    NumericalFailure,
};

struct TransportValidationReport {
    TransportValidationCode code;
    std::string reason;
    double contraction_upper_bound;

    [[nodiscard]] bool valid() const noexcept {
        return code == TransportValidationCode::Valid;
    }
};

class TransportSystem {
public:
    TransportSystem(std::vector<DenseVector> emissions,
                    std::vector<DenseMatrix> transport_operators);

    [[nodiscard]] std::size_t spectral_coefficient_count() const noexcept;
    [[nodiscard]] std::size_t transport_coefficient_count() const noexcept;
    [[nodiscard]] std::size_t emission_band_count() const noexcept;
    [[nodiscard]] std::size_t transport_operator_count() const noexcept;
    [[nodiscard]] const std::vector<DenseVector>& emissions() const noexcept;
    [[nodiscard]] const std::vector<DenseMatrix>& transport_operators()
        const noexcept;
    [[nodiscard]] const DenseVector& emission(std::size_t band) const;
    [[nodiscard]] const DenseMatrix& transport(std::size_t band) const;
    [[nodiscard]] TransportValidationReport validate() const;

private:
    std::vector<DenseVector> emissions_;
    std::vector<DenseMatrix> transport_operators_;
};

}  // namespace lumenbound
