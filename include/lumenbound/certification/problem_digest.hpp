#pragma once

#include <string>

namespace lumenbound {

class Projection;
class TransportSystem;
struct CertificationOptions;

[[nodiscard]] std::string compute_problem_digest(
    const TransportSystem& system, const Projection& projection,
    const CertificationOptions& options);

}  // namespace lumenbound
