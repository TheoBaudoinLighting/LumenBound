#include "lumenbound/certification/problem_digest.hpp"

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lumenbound {
namespace {

constexpr std::array<std::uint32_t, 64> sha256_round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] constexpr std::uint32_t rotate_right(
    std::uint32_t value, unsigned int amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

class Sha256 {
public:
    void update(std::span<const std::uint8_t> bytes) {
        constexpr std::uint64_t maximum_byte_count =
            std::numeric_limits<std::uint64_t>::max() / 8U;
        static_assert(
            std::numeric_limits<std::size_t>::digits <=
            std::numeric_limits<std::uint64_t>::digits);
        if (static_cast<std::uint64_t>(bytes.size()) >
            maximum_byte_count - byte_count_) {
            throw std::length_error("SHA-256 input is too long");
        }
        byte_count_ += static_cast<std::uint64_t>(bytes.size());

        std::size_t offset = 0;
        if (buffer_size_ != 0) {
            const std::size_t copied =
                std::min(bytes.size(), buffer_.size() - buffer_size_);
            for (std::size_t index = 0; index < copied; ++index) {
                buffer_[buffer_size_ + index] = bytes[index];
            }
            buffer_size_ += copied;
            offset += copied;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_);
                buffer_size_ = 0;
            } else {
                return;
            }
        }

        while ((bytes.size() - offset) >= buffer_.size()) {
            transform(std::span<const std::uint8_t, 64>(
                bytes.data() + offset, buffer_.size()));
            offset += buffer_.size();
        }

        const std::size_t remainder = bytes.size() - offset;
        for (std::size_t index = 0; index < remainder; ++index) {
            buffer_[index] = bytes[offset + index];
        }
        buffer_size_ = remainder;
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bit_count = byte_count_ * 8U;

        buffer_[buffer_size_] = 0x80U;
        ++buffer_size_;
        if (buffer_size_ > 56U) {
            while (buffer_size_ < buffer_.size()) {
                buffer_[buffer_size_] = 0U;
                ++buffer_size_;
            }
            transform(buffer_);
            buffer_size_ = 0;
        }
        while (buffer_size_ < 56U) {
            buffer_[buffer_size_] = 0U;
            ++buffer_size_;
        }
        for (std::size_t byte = 0; byte < 8U; ++byte) {
            const unsigned int shift =
                static_cast<unsigned int>((7U - byte) * 8U);
            buffer_[56U + byte] = static_cast<std::uint8_t>(
                (bit_count >> shift) & 0xffU);
        }
        transform(buffer_);

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                const unsigned int shift =
                    static_cast<unsigned int>((3U - byte) * 8U);
                digest[(word * 4U) + byte] =
                    static_cast<std::uint8_t>(
                        (state_[word] >> shift) & 0xffU);
            }
        }
        return digest;
    }

private:
    void transform(std::span<const std::uint8_t, 64> block) {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t word = 0; word < 16U; ++word) {
            const std::size_t offset = word * 4U;
            schedule[word] =
                (static_cast<std::uint32_t>(block[offset]) << 24U) |
                (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                static_cast<std::uint32_t>(block[offset + 3U]);
        }
        for (std::size_t word = 16U; word < schedule.size(); ++word) {
            const std::uint32_t first = schedule[word - 15U];
            const std::uint32_t second = schedule[word - 2U];
            const std::uint32_t sigma0 =
                rotate_right(first, 7U) ^
                rotate_right(first, 18U) ^ (first >> 3U);
            const std::uint32_t sigma1 =
                rotate_right(second, 17U) ^
                rotate_right(second, 19U) ^ (second >> 10U);
            schedule[word] = schedule[word - 16U] + sigma0 +
                             schedule[word - 7U] + sigma1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];

        for (std::size_t round = 0; round < schedule.size(); ++round) {
            const std::uint32_t sum1 =
                rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                rotate_right(e, 25U);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 =
                h + sum1 + choice + sha256_round_constants[round] +
                schedule[round];
            const std::uint32_t sum0 =
                rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                rotate_right(a, 22U);
            const std::uint32_t majority =
                (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = sum0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_{0};
    std::uint64_t byte_count_{0};
};

void append_byte(Sha256& hash, std::uint8_t value) {
    const std::array<std::uint8_t, 1> encoded{value};
    hash.update(encoded);
}

void append_u64(Sha256& hash, std::uint64_t value) {
    std::array<std::uint8_t, 8> encoded{};
    for (std::size_t byte = 0; byte < encoded.size(); ++byte) {
        encoded[encoded.size() - 1U - byte] =
            static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
    hash.update(encoded);
}

void append_size(Sha256& hash, std::size_t value) {
    static_assert(
        std::numeric_limits<std::size_t>::digits <=
        std::numeric_limits<std::uint64_t>::digits,
        "canonical problem sizes require at most 64-bit size_t");
    append_u64(hash, static_cast<std::uint64_t>(value));
}

void append_binary64(Sha256& hash, double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    static_assert(std::numeric_limits<double>::is_iec559);
    static_assert(std::numeric_limits<double>::radix == 2);
    static_assert(std::numeric_limits<double>::digits == 53);
    // Signed zero and NaN payloads identify the exact supplied input.
    append_u64(hash, std::bit_cast<std::uint64_t>(value));
}

void append_ascii(Sha256& hash, std::string_view text) {
    hash.update(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()),
        text.size()));
}

void append_vector(Sha256& hash, const DenseVector& vector) {
    append_size(hash, vector.size());
    for (const double value : vector.values()) {
        append_binary64(hash, value);
    }
}

void append_matrix(Sha256& hash, const DenseMatrix& matrix) {
    append_size(hash, matrix.rows());
    append_size(hash, matrix.columns());
    append_size(hash, matrix.values().size());
    for (const double value : matrix.values()) {
        append_binary64(hash, value);
    }
}

void append_projection(Sha256& hash, const Projection& projection) {
    const std::size_t rows = projection.pixel_count();
    const std::size_t columns = projection.coefficient_count();
    if (rows != 0 &&
        columns > (std::numeric_limits<std::size_t>::max() / rows)) {
        throw std::length_error(
            "projection dimensions exceed canonical digest limits");
    }

    append_size(hash, rows);
    append_size(hash, columns);
    append_size(hash, rows * columns);

    const std::vector<std::size_t>& row_offsets =
        projection.row_offsets();
    const std::vector<std::size_t>& column_indices =
        projection.column_indices();
    const std::vector<double>& values = projection.values();
    for (std::size_t row = 0; row < rows; ++row) {
        std::size_t entry = row_offsets[row];
        const std::size_t end = row_offsets[row + 1U];
        for (std::size_t column = 0; column < columns; ++column) {
            if (entry < end && column_indices[entry] == column) {
                append_binary64(hash, values[entry]);
                ++entry;
            } else {
                // Omitted CSR entries are canonical positive binary64 zero.
                append_binary64(hash, 0.0);
            }
        }
    }
}

[[nodiscard]] std::string hexadecimal_digest(
    const std::array<std::uint8_t, 32>& digest) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string result{"sha256:"};
    result.reserve(result.size() + (digest.size() * 2U));
    for (const std::uint8_t byte : digest) {
        result.push_back(digits[static_cast<std::size_t>(byte >> 4U)]);
        result.push_back(digits[static_cast<std::size_t>(byte & 0x0fU)]);
    }
    return result;
}

}  // namespace

std::string compute_problem_digest(
    const TransportSystem& system, const Projection& projection,
    const CertificationOptions& options) {
    Sha256 hash;
    append_ascii(hash, "lumenbound.problem-digest.v1");
    append_byte(hash, 0x00U);

    append_byte(hash, 0x01U);
    append_size(hash, system.emission_band_count());
    for (const DenseVector& emission : system.emissions()) {
        append_vector(hash, emission);
    }

    append_byte(hash, 0x02U);
    append_size(hash, system.transport_operator_count());
    for (const DenseMatrix& transport : system.transport_operators()) {
        append_matrix(hash, transport);
    }

    append_byte(hash, 0x03U);
    append_projection(hash, projection);

    append_byte(hash, 0x04U);
    append_binary64(hash, options.signal_peak);
    append_binary64(hash, options.target_psnr);
    append_size(hash, options.maximum_iterations);
    append_byte(hash, options.retain_iteration_snapshots ? 0x01U : 0x00U);

    append_byte(hash, 0xffU);
    return hexadecimal_digest(hash.finish());
}

}  // namespace lumenbound
