#pragma once

#include "lumenbound/certification/certificate.hpp"

#include <cstddef>
#include <filesystem>
#include <string>

namespace lumenbound {

[[nodiscard]] std::string certificate_to_json(const Certificate& certificate);
[[nodiscard]] std::string metrics_to_json(const Certificate& certificate);

void write_demo_outputs(const std::filesystem::path& output_directory,
                        const CertificationResult& result,
                        std::size_t image_width, std::size_t image_height);

}  // namespace lumenbound
