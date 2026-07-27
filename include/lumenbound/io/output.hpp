#pragma once

#include "lumenbound/certification/certificate.hpp"
#include "lumenbound/math/dense_vector.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace lumenbound {

enum class PreviewMapping {
    FalseColorCoefficientClamp,
    DeclaredLinearSrgb,
};

struct PreviewSettings {
    PreviewMapping mapping{PreviewMapping::FalseColorCoefficientClamp};
    double exposure{1.0};
};

[[nodiscard]] std::string certificate_to_json(const Certificate& certificate);
[[nodiscard]] std::string metrics_to_json(const Certificate& certificate);

void write_demo_outputs(const std::filesystem::path& output_directory,
                        const CertificationResult& result,
                        std::size_t image_width, std::size_t image_height,
                        PreviewSettings preview_settings = {});

void write_preview_only_output(
    const std::filesystem::path& output_directory,
    const std::vector<DenseVector>& pixel_bands,
    std::size_t image_width, std::size_t image_height,
    double exposure);

}  // namespace lumenbound
