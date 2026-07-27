#pragma once

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/transport/diffuse_patch_assembly.hpp"

#include <cstddef>
#include <filesystem>
#include <iosfwd>

namespace lumenbound {

struct CornellBoxDemoOptions {
    std::filesystem::path output_directory{"out/cornell-box"};
    double signal_peak{4.0};
    double target_psnr{80.0};
    std::size_t maximum_iterations{512};
    std::size_t image_width{128};
    std::size_t image_height{128};
    double preview_exposure{1.0};
    bool preview_only{false};
};

struct CornellBoxProblem {
    DiffusePatchAssemblyResult assembly;
    std::size_t image_width{0};
    std::size_t image_height{0};
};

struct CornellBoxDemoRunResult {
    int exit_code{3};
    CertificationResult certification;
    DiffusePatchAssemblyDiagnostics assembly_diagnostics;
};

[[nodiscard]] CornellBoxProblem make_cornell_box_problem(
    std::size_t image_width, std::size_t image_height);

[[nodiscard]] CornellBoxDemoRunResult run_cornell_box(
    const CornellBoxDemoOptions& options, std::ostream& summary,
    std::ostream& errors);

}  // namespace lumenbound
