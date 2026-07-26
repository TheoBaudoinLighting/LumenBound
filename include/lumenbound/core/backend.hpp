#pragma once

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <string_view>

namespace lumenbound {

class CertificationBackend {
public:
    virtual ~CertificationBackend() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual CertificationResult run(
        const TransportSystem& system, const Projection& projection,
        const CertificationOptions& options) const = 0;
};

class CpuReferenceBackend final : public CertificationBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] CertificationResult run(
        const TransportSystem& system, const Projection& projection,
        const CertificationOptions& options) const override;
};

}  // namespace lumenbound
