#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>

namespace lumenbound {

template <std::size_t CoefficientCount>
class Spectrum {
public:
    Spectrum() = default;

    Spectrum(std::initializer_list<double> coefficients) {
        if (coefficients.size() != CoefficientCount) {
            throw std::invalid_argument("spectrum coefficient count mismatch");
        }
        std::copy(coefficients.begin(), coefficients.end(),
                  coefficients_.begin());
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return CoefficientCount;
    }

    [[nodiscard]] double& operator[](std::size_t index) {
        return coefficients_.at(index);
    }

    [[nodiscard]] const double& operator[](std::size_t index) const {
        return coefficients_.at(index);
    }

    [[nodiscard]] const std::array<double, CoefficientCount>& coefficients()
        const noexcept {
        return coefficients_;
    }

    friend Spectrum operator+(const Spectrum& left, const Spectrum& right) {
        Spectrum result;
        for (std::size_t index = 0; index < CoefficientCount; ++index) {
            result[index] = left[index] + right[index];
        }
        return result;
    }

    friend Spectrum operator-(const Spectrum& left, const Spectrum& right) {
        Spectrum result;
        for (std::size_t index = 0; index < CoefficientCount; ++index) {
            result[index] = left[index] - right[index];
        }
        return result;
    }

    friend Spectrum operator*(const Spectrum& spectrum, double scalar) {
        Spectrum result;
        for (std::size_t index = 0; index < CoefficientCount; ++index) {
            result[index] = spectrum[index] * scalar;
        }
        return result;
    }

private:
    std::array<double, CoefficientCount> coefficients_{};
};

}  // namespace lumenbound
