#pragma once

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/projection/projection.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace lumenbound {

struct ManufacturedProblem {
    TransportSystem system;
    Projection projection;
    std::vector<DenseVector> exact_coefficients;
    std::size_t image_width;
    std::size_t image_height;
};

struct DemoOptions {
    std::filesystem::path output_directory;
    double signal_peak{1.0};
    double target_psnr{80.0};
    std::size_t maximum_iterations{512};
};

struct DemoRunResult {
    int exit_code;
    CertificationResult certification;
};

[[nodiscard]] ManufacturedProblem make_certified_patches_problem();
[[nodiscard]] DemoRunResult run_certified_patches(
    const DemoOptions& options, std::ostream& summary,
    std::ostream& errors);

}  // namespace lumenbound
