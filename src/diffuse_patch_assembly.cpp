#include "lumenbound/transport/diffuse_patch_assembly.hpp"

#include "lumenbound/math/dense_matrix.hpp"
#include "lumenbound/math/dense_vector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lumenbound {
namespace {

constexpr double ray_origin_offset = 1.0e-7;
constexpr double intersection_epsilon = 1.0e-10;
constexpr double orthogonality_tolerance = 1.0e-10;

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

struct PreparedSurface {
    const OrientedRectangle* source{nullptr};
    Vec3 normal;
    double edge_u_squared{0.0};
    double edge_v_squared{0.0};
    std::size_t patch_offset{0};
};

struct Patch {
    std::size_t surface_index{0};
    std::size_t cell_u{0};
    std::size_t cell_v{0};
};

struct SurfaceHit {
    std::size_t surface_index{0};
    std::size_t patch_index{0};
    bool front_facing{false};
    double parameter_u{0.0};
    double parameter_v{0.0};
};

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 scale(Vec3 value, double factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] double dot(Vec3 left, Vec3 right) noexcept {
    return (left.x * right.x) + (left.y * right.y) +
           (left.z * right.z);
}

[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right) noexcept {
    return {
        (left.y * right.z) - (left.z * right.y),
        (left.z * right.x) - (left.x * right.z),
        (left.x * right.y) - (left.y * right.x),
    };
}

[[nodiscard]] bool is_finite(Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

[[nodiscard]] double length(Vec3 value) noexcept {
    return std::sqrt(dot(value, value));
}

[[nodiscard]] Vec3 normalize(Vec3 value, const char* reason) {
    const double value_length = length(value);
    if (!std::isfinite(value_length) || value_length <= 0.0) {
        throw std::invalid_argument(reason);
    }
    const Vec3 result = scale(value, 1.0 / value_length);
    if (!is_finite(result)) {
        throw std::invalid_argument(reason);
    }
    return result;
}

[[nodiscard]] std::size_t checked_product(std::size_t left,
                                          std::size_t right,
                                          const char* reason) {
    if (left != 0 &&
        right > (std::numeric_limits<std::size_t>::max() / left)) {
        throw std::length_error(reason);
    }
    return left * right;
}

[[nodiscard]] bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool is_exact_binary64_integer(
    std::size_t value) noexcept {
    if constexpr (std::numeric_limits<std::size_t>::digits <=
                  std::numeric_limits<double>::digits) {
        return true;
    } else {
        constexpr std::uint64_t maximum_exact_integer =
            std::uint64_t{1} << 53U;
        return value <=
               static_cast<std::size_t>(maximum_exact_integer);
    }
}

void validate_options(const DiffusePatchAssemblyOptions& options) {
    if (options.image_width == 0 || options.image_height == 0) {
        throw std::invalid_argument(
            "diffuse patch image dimensions must be positive");
    }
    if (options.surface_samples_per_axis == 0 ||
        options.hemisphere_radial_steps == 0 ||
        options.hemisphere_azimuth_steps == 0 ||
        options.pixel_samples_per_axis == 0) {
        throw std::invalid_argument(
            "diffuse patch quadrature dimensions must be positive");
    }

    const std::size_t surface_sample_count = checked_product(
        options.surface_samples_per_axis,
        options.surface_samples_per_axis,
        "diffuse patch surface quadrature size overflows");
    const std::size_t direction_count = checked_product(
        options.hemisphere_radial_steps,
        options.hemisphere_azimuth_steps,
        "diffuse patch directional quadrature size overflows");
    const std::size_t transport_sample_count = checked_product(
        surface_sample_count, direction_count,
        "diffuse patch transport quadrature size overflows");
    const std::size_t pixel_sample_count = checked_product(
        options.pixel_samples_per_axis,
        options.pixel_samples_per_axis,
        "diffuse patch pixel quadrature size overflows");
    if (!is_exact_binary64_integer(transport_sample_count) ||
        !is_exact_binary64_integer(pixel_sample_count) ||
        !is_power_of_two(transport_sample_count) ||
        !is_power_of_two(pixel_sample_count)) {
        throw std::invalid_argument(
            "diffuse patch quadrature counts must be exact powers of two");
    }
    static_cast<void>(checked_product(
        options.image_width, options.image_height,
        "diffuse patch pixel count overflows"));
}

[[nodiscard]] std::vector<PreparedSurface> prepare_surfaces(
    const std::vector<OrientedRectangle>& surfaces,
    std::vector<Patch>& patches, std::size_t& band_count,
    double& maximum_reflectance) {
    if (surfaces.empty()) {
        throw std::invalid_argument(
            "diffuse patch scene requires at least one surface");
    }

    std::vector<PreparedSurface> prepared;
    prepared.reserve(surfaces.size());
    band_count = 0;
    maximum_reflectance = 0.0;

    for (std::size_t surface_index = 0;
         surface_index < surfaces.size(); ++surface_index) {
        const OrientedRectangle& surface = surfaces[surface_index];
        if (surface.name.empty()) {
            throw std::invalid_argument(
                "diffuse patch surface names must not be empty");
        }
        if (!is_finite(surface.origin) || !is_finite(surface.edge_u) ||
            !is_finite(surface.edge_v)) {
            throw std::invalid_argument(
                "diffuse patch surface geometry must be finite");
        }
        if (surface.subdivisions_u == 0 ||
            surface.subdivisions_v == 0) {
            throw std::invalid_argument(
                "diffuse patch surface subdivisions must be positive");
        }

        const double edge_u_length = length(surface.edge_u);
        const double edge_v_length = length(surface.edge_v);
        const Vec3 surface_cross =
            cross(surface.edge_u, surface.edge_v);
        const double cross_length = length(surface_cross);
        if (!std::isfinite(edge_u_length) ||
            !std::isfinite(edge_v_length) ||
            !std::isfinite(cross_length) ||
            edge_u_length <= 0.0 || edge_v_length <= 0.0 ||
            cross_length <= 0.0) {
            throw std::invalid_argument(
                "diffuse patch surface edges are degenerate");
        }
        const double normalized_edge_dot =
            std::abs(dot(surface.edge_u, surface.edge_v)) /
            (edge_u_length * edge_v_length);
        if (!std::isfinite(normalized_edge_dot) ||
            normalized_edge_dot > orthogonality_tolerance) {
            throw std::invalid_argument(
                "diffuse patch surfaces must be rectangular");
        }

        if (surface.material.reflectance.empty() ||
            surface.material.reflectance.size() !=
                surface.material.emission.size()) {
            throw std::invalid_argument(
                "diffuse patch material band dimensions are invalid");
        }
        if (band_count == 0) {
            band_count = surface.material.reflectance.size();
        } else if (surface.material.reflectance.size() != band_count) {
            throw std::invalid_argument(
                "diffuse patch material band counts do not match");
        }
        for (std::size_t band = 0; band < band_count; ++band) {
            const double reflectance =
                surface.material.reflectance[band];
            const double emission = surface.material.emission[band];
            if (!std::isfinite(reflectance) ||
                !std::isfinite(emission)) {
                throw std::invalid_argument(
                    "diffuse patch material coefficients must be finite");
            }
            if (reflectance < 0.0 || reflectance >= 1.0) {
                throw std::invalid_argument(
                    "diffuse patch reflectance must be in [0,1)");
            }
            if (emission < 0.0) {
                throw std::invalid_argument(
                    "diffuse patch emission must be nonnegative");
            }
            maximum_reflectance =
                std::max(maximum_reflectance, reflectance);
        }

        const std::size_t surface_patch_count = checked_product(
            surface.subdivisions_u, surface.subdivisions_v,
            "diffuse patch surface patch count overflows");
        if (surface_patch_count >
            (std::numeric_limits<std::size_t>::max() -
             patches.size())) {
            throw std::length_error(
                "diffuse patch scene patch count overflows");
        }

        const std::size_t patch_offset = patches.size();
        const Vec3 normal =
            scale(surface_cross, 1.0 / cross_length);
        prepared.push_back(PreparedSurface{
            &surface,
            normal,
            dot(surface.edge_u, surface.edge_u),
            dot(surface.edge_v, surface.edge_v),
            patch_offset,
        });

        for (std::size_t cell_v = 0;
             cell_v < surface.subdivisions_v; ++cell_v) {
            for (std::size_t cell_u = 0;
                 cell_u < surface.subdivisions_u; ++cell_u) {
                patches.push_back(Patch{
                    surface_index,
                    cell_u,
                    cell_v,
                });
            }
        }
    }
    return prepared;
}

[[nodiscard]] std::optional<SurfaceHit> trace(
    const Ray& ray, const std::vector<PreparedSurface>& surfaces) {
    std::optional<SurfaceHit> nearest;
    double nearest_distance = std::numeric_limits<double>::infinity();

    for (std::size_t surface_index = 0;
         surface_index < surfaces.size(); ++surface_index) {
        const PreparedSurface& prepared = surfaces[surface_index];
        const OrientedRectangle& surface = *prepared.source;
        const double denominator = dot(ray.direction, prepared.normal);
        if (!std::isfinite(denominator) ||
            std::abs(denominator) <= intersection_epsilon) {
            continue;
        }

        const double distance =
            dot(subtract(surface.origin, ray.origin), prepared.normal) /
            denominator;
        if (!std::isfinite(distance) ||
            distance <= ray_origin_offset ||
            distance >= nearest_distance) {
            continue;
        }

        const Vec3 position =
            add(ray.origin, scale(ray.direction, distance));
        const Vec3 relative = subtract(position, surface.origin);
        double parameter_u =
            dot(relative, surface.edge_u) / prepared.edge_u_squared;
        double parameter_v =
            dot(relative, surface.edge_v) / prepared.edge_v_squared;
        if (parameter_u < -intersection_epsilon ||
            parameter_u > 1.0 + intersection_epsilon ||
            parameter_v < -intersection_epsilon ||
            parameter_v > 1.0 + intersection_epsilon) {
            continue;
        }
        parameter_u = std::clamp(parameter_u, 0.0, 1.0);
        parameter_v = std::clamp(parameter_v, 0.0, 1.0);

        const double scaled_u =
            parameter_u *
            static_cast<double>(surface.subdivisions_u);
        const double scaled_v =
            parameter_v *
            static_cast<double>(surface.subdivisions_v);
        const std::size_t cell_u = std::min(
            static_cast<std::size_t>(scaled_u),
            surface.subdivisions_u - 1U);
        const std::size_t cell_v = std::min(
            static_cast<std::size_t>(scaled_v),
            surface.subdivisions_v - 1U);
        const std::size_t patch_index =
            prepared.patch_offset +
            (cell_v * surface.subdivisions_u) + cell_u;

        nearest_distance = distance;
        nearest = SurfaceHit{
            surface_index,
            patch_index,
            denominator < 0.0,
            parameter_u,
            parameter_v,
        };
        // Strict distance comparison above leaves an exact tie with the
        // lower surface index already stored.
    }
    return nearest;
}

[[nodiscard]] Vec3 point_on_patch(
    const Patch& patch, const PreparedSurface& prepared,
    std::size_t sample_u, std::size_t sample_v,
    std::size_t samples_per_axis) {
    const OrientedRectangle& surface = *prepared.source;
    const double within_u =
        (static_cast<double>(sample_u) + 0.5) /
        static_cast<double>(samples_per_axis);
    const double within_v =
        (static_cast<double>(sample_v) + 0.5) /
        static_cast<double>(samples_per_axis);
    const double parameter_u =
        (static_cast<double>(patch.cell_u) + within_u) /
        static_cast<double>(surface.subdivisions_u);
    const double parameter_v =
        (static_cast<double>(patch.cell_v) + within_v) /
        static_cast<double>(surface.subdivisions_v);
    return add(surface.origin,
               add(scale(surface.edge_u, parameter_u),
                   scale(surface.edge_v, parameter_v)));
}

[[nodiscard]] Vec3 cosine_direction(
    const PreparedSurface& surface, std::size_t radial_index,
    std::size_t azimuth_index, std::size_t radial_steps,
    std::size_t azimuth_steps) {
    const Vec3 tangent = normalize(
        surface.source->edge_u,
        "diffuse patch tangent construction failed");
    const Vec3 bitangent = normalize(
        cross(surface.normal, tangent),
        "diffuse patch bitangent construction failed");
    const double radial_parameter =
        (static_cast<double>(radial_index) + 0.5) /
        static_cast<double>(radial_steps);
    const double azimuth_parameter =
        (static_cast<double>(azimuth_index) + 0.5) /
        static_cast<double>(azimuth_steps);
    const double radial = std::sqrt(radial_parameter);
    const double azimuth =
        2.0 * std::numbers::pi_v<double> * azimuth_parameter;
    const double tangent_component = radial * std::cos(azimuth);
    const double bitangent_component = radial * std::sin(azimuth);
    const double normal_component =
        std::sqrt(1.0 - radial_parameter);
    return normalize(
        add(scale(tangent, tangent_component),
            add(scale(bitangent, bitangent_component),
                scale(surface.normal, normal_component))),
        "diffuse patch direction construction failed");
}

[[nodiscard]] std::vector<std::vector<std::size_t>>
assemble_hit_counts(
    const std::vector<PreparedSurface>& surfaces,
    const std::vector<Patch>& patches,
    const DiffusePatchAssemblyOptions& options,
    std::size_t& ray_count, std::size_t& escape_count,
    std::size_t& blocked_backface_count) {
    const std::size_t patch_count = patches.size();
    const std::size_t matrix_size = checked_product(
        patch_count, patch_count,
        "diffuse patch hit-count matrix size overflows");
    std::vector<std::size_t> flat_counts(matrix_size, 0U);

    const std::size_t samples_per_patch = checked_product(
        checked_product(options.surface_samples_per_axis,
                        options.surface_samples_per_axis,
                        "diffuse patch spatial quadrature size overflows"),
        checked_product(options.hemisphere_radial_steps,
                        options.hemisphere_azimuth_steps,
                        "diffuse patch angular quadrature size overflows"),
        "diffuse patch transport quadrature size overflows");
    ray_count = checked_product(
        patch_count, samples_per_patch,
        "diffuse patch transport ray count overflows");
    escape_count = 0;
    blocked_backface_count = 0;

    for (std::size_t receiver = 0;
         receiver < patch_count; ++receiver) {
        const Patch& patch = patches[receiver];
        const PreparedSurface& surface =
            surfaces[patch.surface_index];
        for (std::size_t sample_v = 0;
             sample_v < options.surface_samples_per_axis; ++sample_v) {
            for (std::size_t sample_u = 0;
                 sample_u < options.surface_samples_per_axis; ++sample_u) {
                const Vec3 surface_point = point_on_patch(
                    patch, surface, sample_u, sample_v,
                    options.surface_samples_per_axis);
                const Vec3 origin = add(
                    surface_point,
                    scale(surface.normal, ray_origin_offset));
                for (std::size_t radial = 0;
                     radial < options.hemisphere_radial_steps; ++radial) {
                    for (std::size_t azimuth = 0;
                         azimuth <
                         options.hemisphere_azimuth_steps;
                         ++azimuth) {
                        const Vec3 direction = cosine_direction(
                            surface, radial, azimuth,
                            options.hemisphere_radial_steps,
                            options.hemisphere_azimuth_steps);
                        const std::optional<SurfaceHit> hit =
                            trace(Ray{origin, direction}, surfaces);
                        if (!hit.has_value()) {
                            ++escape_count;
                            continue;
                        }
                        if (!hit->front_facing) {
                            ++blocked_backface_count;
                            continue;
                        }
                        const std::size_t offset =
                            (receiver * patch_count) +
                            hit->patch_index;
                        ++flat_counts[offset];
                    }
                }
            }
        }
    }

    std::vector<std::vector<std::size_t>> rows;
    rows.reserve(patch_count);
    for (std::size_t row = 0; row < patch_count; ++row) {
        const auto begin =
            flat_counts.begin() +
            static_cast<std::ptrdiff_t>(row * patch_count);
        rows.emplace_back(begin, begin +
            static_cast<std::ptrdiff_t>(patch_count));
    }
    return rows;
}

[[nodiscard]] std::vector<DenseMatrix> assemble_transport(
    const std::vector<PreparedSurface>& surfaces,
    const std::vector<Patch>& patches,
    const std::vector<std::vector<std::size_t>>& hit_counts,
    std::size_t band_count, std::size_t samples_per_patch,
    double& maximum_form_factor_row_sum) {
    const std::size_t patch_count = patches.size();
    const std::size_t matrix_size = checked_product(
        patch_count, patch_count,
        "diffuse patch transport matrix size overflows");
    std::vector<std::vector<double>> band_values(
        band_count, std::vector<double>(matrix_size, 0.0));
    const double denominator =
        static_cast<double>(samples_per_patch);
    maximum_form_factor_row_sum = 0.0;

    for (std::size_t receiver = 0;
         receiver < patch_count; ++receiver) {
        std::size_t row_hit_count = 0;
        for (std::size_t source = 0;
             source < patch_count; ++source) {
            const std::size_t count = hit_counts[receiver][source];
            row_hit_count += count;
            if (count == 0) {
                continue;
            }
            const double form_factor =
                static_cast<double>(count) / denominator;
            const DiffusePatchMaterial& material =
                surfaces[patches[receiver].surface_index]
                    .source->material;
            for (std::size_t band = 0; band < band_count; ++band) {
                band_values[band][
                    (receiver * patch_count) + source] =
                    material.reflectance[band] * form_factor;
            }
        }
        if (row_hit_count > samples_per_patch) {
            throw std::runtime_error(
                "diffuse patch quadrature assigned one ray more than once");
        }
        maximum_form_factor_row_sum =
            std::max(maximum_form_factor_row_sum,
                     static_cast<double>(row_hit_count) /
                         denominator);
    }

    std::vector<DenseMatrix> transport;
    transport.reserve(band_count);
    for (std::size_t band = 0; band < band_count; ++band) {
        transport.emplace_back(
            patch_count, patch_count,
            std::move(band_values[band]));
    }
    return transport;
}

[[nodiscard]] std::vector<DenseVector> assemble_emission(
    const std::vector<PreparedSurface>& surfaces,
    const std::vector<Patch>& patches, std::size_t band_count) {
    std::vector<std::vector<double>> values(
        band_count, std::vector<double>(patches.size(), 0.0));
    for (std::size_t patch_index = 0;
         patch_index < patches.size(); ++patch_index) {
        const DiffusePatchMaterial& material =
            surfaces[patches[patch_index].surface_index]
                .source->material;
        for (std::size_t band = 0; band < band_count; ++band) {
            values[band][patch_index] = material.emission[band];
        }
    }

    std::vector<DenseVector> emission;
    emission.reserve(band_count);
    for (std::size_t band = 0; band < band_count; ++band) {
        emission.emplace_back(std::move(values[band]));
    }
    return emission;
}

struct CameraFrame {
    Vec3 forward;
    Vec3 right;
    Vec3 up;
    double half_vertical_extent{0.0};
};

[[nodiscard]] CameraFrame make_camera_frame(
    const PinholeCamera& camera) {
    if (!is_finite(camera.position) || !is_finite(camera.target) ||
        !is_finite(camera.nominal_up) ||
        !std::isfinite(camera.vertical_field_of_view_degrees) ||
        camera.vertical_field_of_view_degrees <= 0.0 ||
        camera.vertical_field_of_view_degrees >= 179.0) {
        throw std::invalid_argument(
            "diffuse patch camera parameters are invalid");
    }
    const Vec3 forward = normalize(
        subtract(camera.target, camera.position),
        "diffuse patch camera position and target must differ");
    const Vec3 right = normalize(
        cross(camera.nominal_up, forward),
        "diffuse patch camera up vector is parallel to its view");
    const Vec3 up = normalize(
        cross(forward, right),
        "diffuse patch camera frame construction failed");
    const double radians =
        camera.vertical_field_of_view_degrees *
        (std::numbers::pi_v<double> / 180.0);
    const double half_vertical_extent = std::tan(radians * 0.5);
    if (!std::isfinite(half_vertical_extent) ||
        half_vertical_extent <= 0.0) {
        throw std::invalid_argument(
            "diffuse patch camera field of view is invalid");
    }
    return {forward, right, up, half_vertical_extent};
}

[[nodiscard]] Ray make_camera_ray(
    const PinholeCamera& camera, const CameraFrame& frame,
    const DiffusePatchAssemblyOptions& options,
    std::size_t pixel_x, std::size_t pixel_y,
    std::size_t sample_x, std::size_t sample_y) {
    const double subpixel_x =
        (static_cast<double>(sample_x) + 0.5) /
        static_cast<double>(options.pixel_samples_per_axis);
    const double subpixel_y =
        (static_cast<double>(sample_y) + 0.5) /
        static_cast<double>(options.pixel_samples_per_axis);
    const double normalized_x =
        ((static_cast<double>(pixel_x) + subpixel_x) /
             static_cast<double>(options.image_width) *
         2.0) -
        1.0;
    const double normalized_y =
        1.0 -
        ((static_cast<double>(pixel_y) + subpixel_y) /
             static_cast<double>(options.image_height) *
         2.0);
    const double aspect =
        static_cast<double>(options.image_width) /
        static_cast<double>(options.image_height);
    const Vec3 direction = normalize(
        add(frame.forward,
            add(scale(frame.right,
                      normalized_x * aspect *
                          frame.half_vertical_extent),
                scale(frame.up,
                      normalized_y * frame.half_vertical_extent))),
        "diffuse patch camera ray construction failed");
    return {camera.position, direction};
}

void append_bilinear_patch_weights(
    const SurfaceHit& hit,
    const std::vector<PreparedSurface>& surfaces,
    double sample_weight,
    std::vector<std::pair<std::size_t, double>>& entries) {
    const PreparedSurface& prepared = surfaces[hit.surface_index];
    const OrientedRectangle& surface = *prepared.source;
    const double grid_u =
        (hit.parameter_u *
         static_cast<double>(surface.subdivisions_u)) -
        0.5;
    const double grid_v =
        (hit.parameter_v *
         static_cast<double>(surface.subdivisions_v)) -
        0.5;
    const double floor_u = std::floor(grid_u);
    const double floor_v = std::floor(grid_v);
    const double fraction_u = grid_u - floor_u;
    const double fraction_v = grid_v - floor_v;

    const auto clamp_cell = [](double cell,
                               std::size_t count) -> std::size_t {
        if (cell <= 0.0) {
            return 0;
        }
        const double maximum = static_cast<double>(count - 1U);
        if (cell >= maximum) {
            return count - 1U;
        }
        return static_cast<std::size_t>(cell);
    };

    const std::size_t cells_u[2]{
        clamp_cell(floor_u, surface.subdivisions_u),
        clamp_cell(floor_u + 1.0, surface.subdivisions_u),
    };
    const std::size_t cells_v[2]{
        clamp_cell(floor_v, surface.subdivisions_v),
        clamp_cell(floor_v + 1.0, surface.subdivisions_v),
    };
    const double weights_u[2]{1.0 - fraction_u, fraction_u};
    const double weights_v[2]{1.0 - fraction_v, fraction_v};

    for (std::size_t v = 0; v < 2U; ++v) {
        for (std::size_t u = 0; u < 2U; ++u) {
            const double weight =
                sample_weight * weights_u[u] * weights_v[v];
            if (weight == 0.0) {
                continue;
            }
            const std::size_t patch_index =
                prepared.patch_offset +
                (cells_v[v] * surface.subdivisions_u) +
                cells_u[u];
            entries.emplace_back(patch_index, weight);
        }
    }
}

[[nodiscard]] Projection assemble_projection(
    const std::vector<PreparedSurface>& surfaces,
    std::size_t patch_count, const PinholeCamera& camera,
    const DiffusePatchAssemblyOptions& options,
    std::size_t& primary_ray_count, std::size_t& missed_ray_count,
    std::size_t& blocked_backface_count,
    double& maximum_projection_row_sum) {
    const std::size_t pixel_count = checked_product(
        options.image_width, options.image_height,
        "diffuse patch pixel count overflows");
    const std::size_t samples_per_pixel = checked_product(
        options.pixel_samples_per_axis,
        options.pixel_samples_per_axis,
        "diffuse patch pixel quadrature size overflows");
    const std::size_t maximum_row_entry_count = checked_product(
        samples_per_pixel, std::size_t{4},
        "diffuse patch projection row size overflows");
    primary_ray_count = checked_product(
        pixel_count, samples_per_pixel,
        "diffuse patch primary ray count overflows");
    missed_ray_count = 0;
    blocked_backface_count = 0;
    maximum_projection_row_sum = 0.0;
    const double sample_weight =
        1.0 / static_cast<double>(samples_per_pixel);
    const CameraFrame frame = make_camera_frame(camera);

    std::vector<std::size_t> row_offsets;
    std::vector<std::size_t> columns;
    std::vector<double> values;
    row_offsets.reserve(pixel_count + 1U);
    row_offsets.push_back(0U);

    for (std::size_t pixel_y = 0;
         pixel_y < options.image_height; ++pixel_y) {
        for (std::size_t pixel_x = 0;
             pixel_x < options.image_width; ++pixel_x) {
            std::vector<std::pair<std::size_t, double>> row_entries;
            row_entries.reserve(maximum_row_entry_count);
            for (std::size_t sample_y = 0;
                 sample_y < options.pixel_samples_per_axis; ++sample_y) {
                for (std::size_t sample_x = 0;
                     sample_x < options.pixel_samples_per_axis; ++sample_x) {
                    const Ray ray = make_camera_ray(
                        camera, frame, options, pixel_x, pixel_y,
                        sample_x, sample_y);
                    const std::optional<SurfaceHit> hit =
                        trace(ray, surfaces);
                    if (!hit.has_value()) {
                        ++missed_ray_count;
                        continue;
                    }
                    if (!hit->front_facing) {
                        ++blocked_backface_count;
                        continue;
                    }
                    append_bilinear_patch_weights(
                        *hit, surfaces, sample_weight, row_entries);
                }
            }

            std::stable_sort(
                row_entries.begin(), row_entries.end(),
                [](const auto& left, const auto& right) {
                    return left.first < right.first;
                });
            double projection_row_sum = 0.0;
            for (std::size_t entry = 0;
                 entry < row_entries.size();) {
                const std::size_t column = row_entries[entry].first;
                double weight = 0.0;
                do {
                    weight += row_entries[entry].second;
                    ++entry;
                } while (entry < row_entries.size() &&
                         row_entries[entry].first == column);
                if (!std::isfinite(weight) || weight < 0.0) {
                    throw std::runtime_error(
                        "diffuse patch projection weight is invalid");
                }
                if (weight != 0.0) {
                    columns.push_back(column);
                    values.push_back(weight);
                    projection_row_sum += weight;
                }
            }
            maximum_projection_row_sum =
                std::max(maximum_projection_row_sum,
                         projection_row_sum);
            row_offsets.push_back(values.size());
        }
    }

    return Projection(pixel_count, patch_count, std::move(row_offsets),
                      std::move(columns), std::move(values));
}

}  // namespace

DiffusePatchAssemblyResult assemble_diffuse_patch_problem(
    const std::vector<OrientedRectangle>& surfaces,
    const PinholeCamera& camera,
    const DiffusePatchAssemblyOptions& options) {
    validate_options(options);

    std::vector<Patch> patches;
    std::size_t band_count = 0;
    double maximum_reflectance = 0.0;
    const std::vector<PreparedSurface> prepared = prepare_surfaces(
        surfaces, patches, band_count, maximum_reflectance);
    if (patches.empty()) {
        throw std::invalid_argument(
            "diffuse patch scene produced no patches");
    }

    std::size_t transport_ray_count = 0;
    std::size_t escaped_transport_ray_count = 0;
    std::size_t blocked_backface_transport_ray_count = 0;
    const std::vector<std::vector<std::size_t>> hit_counts =
        assemble_hit_counts(
            prepared, patches, options, transport_ray_count,
            escaped_transport_ray_count,
            blocked_backface_transport_ray_count);
    const std::size_t samples_per_patch = checked_product(
        checked_product(options.surface_samples_per_axis,
                        options.surface_samples_per_axis,
                        "diffuse patch spatial quadrature size overflows"),
        checked_product(options.hemisphere_radial_steps,
                        options.hemisphere_azimuth_steps,
                        "diffuse patch angular quadrature size overflows"),
        "diffuse patch transport quadrature size overflows");

    double maximum_form_factor_row_sum = 0.0;
    std::vector<DenseMatrix> transport = assemble_transport(
        prepared, patches, hit_counts, band_count, samples_per_patch,
        maximum_form_factor_row_sum);
    std::vector<DenseVector> emission =
        assemble_emission(prepared, patches, band_count);

    std::size_t primary_ray_count = 0;
    std::size_t missed_primary_ray_count = 0;
    std::size_t blocked_backface_primary_ray_count = 0;
    double maximum_projection_row_sum = 0.0;
    Projection projection = assemble_projection(
        prepared, patches.size(), camera, options,
        primary_ray_count, missed_primary_ray_count,
        blocked_backface_primary_ray_count,
        maximum_projection_row_sum);
    const std::size_t projection_nonzero_count =
        projection.stored_entry_count();

    return DiffusePatchAssemblyResult{
        TransportSystem(std::move(emission), std::move(transport)),
        std::move(projection),
        DiffusePatchAssemblyDiagnostics{
            surfaces.size(),
            patches.size(),
            band_count,
            transport_ray_count,
            escaped_transport_ray_count,
            blocked_backface_transport_ray_count,
            primary_ray_count,
            missed_primary_ray_count,
            blocked_backface_primary_ray_count,
            projection_nonzero_count,
            maximum_reflectance,
            maximum_form_factor_row_sum,
            maximum_projection_row_sum,
            ray_origin_offset,
            intersection_epsilon,
        },
    };
}

}  // namespace lumenbound
