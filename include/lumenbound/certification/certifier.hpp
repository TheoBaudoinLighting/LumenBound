#pragma once

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/transport/transport_system.hpp"

namespace lumenbound {

[[nodiscard]] CertificationResult certify(
    const TransportSystem& system, const Projection& projection,
    const CertificationOptions& options);

}  // namespace lumenbound
