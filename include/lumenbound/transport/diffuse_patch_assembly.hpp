#pragma once

#include "lumenbound/projection/projection.hpp"
#include "lumenbound/transport/transport_system.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lumenbound {

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct DiffusePatchMaterial {
    std::vector<double> reflectance;
    std::vector<double> emission;
};

struct OrientedRectangle {
    std::string name;
    Vec3 origin;
    Vec3 edge_u;
    Vec3 edge_v;
    std::size_t subdivisions_u{1};
    std::size_t subdivisions_v{1};
    DiffusePatchMaterial material;
};

struct PinholeCamera {
    Vec3 position;
    Vec3 target;
    Vec3 nominal_up{0.0, 1.0, 0.0};
    double vertical_field_of_view_degrees{40.0};
};

struct DiffusePatchAssemblyOptions {
    std::size_t image_width{128};
    std::size_t image_height{128};
    std::size_t surface_samples_per_axis{2};
    std::size_t hemisphere_radial_steps{8};
    std::size_t hemisphere_azimuth_steps{32};
    std::size_t pixel_samples_per_axis{2};
};

struct DiffusePatchAssemblyDiagnostics {
    std::size_t surface_count{0};
    std::size_t patch_count{0};
    std::size_t coefficient_band_count{0};
    std::size_t transport_ray_count{0};
    std::size_t escaped_transport_ray_count{0};
    std::size_t blocked_backface_transport_ray_count{0};
    std::size_t primary_ray_count{0};
    std::size_t missed_primary_ray_count{0};
    std::size_t blocked_backface_primary_ray_count{0};
    std::size_t projection_nonzero_count{0};
    double maximum_reflectance{0.0};
    double maximum_form_factor_row_sum{0.0};
    double maximum_projection_row_sum{0.0};
    double ray_origin_offset{0.0};
    double intersection_epsilon{0.0};
};

struct DiffusePatchAssemblyResult {
    TransportSystem system;
    Projection projection;
    DiffusePatchAssemblyDiagnostics diagnostics;
};

[[nodiscard]] DiffusePatchAssemblyResult assemble_diffuse_patch_problem(
    const std::vector<OrientedRectangle>& surfaces,
    const PinholeCamera& camera,
    const DiffusePatchAssemblyOptions& options);

}  // namespace lumenbound
