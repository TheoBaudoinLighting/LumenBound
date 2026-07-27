#include "lumenbound/core/cornell_box_demo.hpp"

#include "lumenbound/certification/certifier.hpp"
#include "lumenbound/core/backend.hpp"
#include "lumenbound/io/output.hpp"
#include "lumenbound/math/rounding.hpp"
#include "lumenbound/solver/candidate_solver.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lumenbound {
namespace {

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 scale(Vec3 value, double factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] DiffusePatchMaterial material(
    double red_reflectance, double green_reflectance,
    double blue_reflectance, double red_emission = 0.0,
    double green_emission = 0.0, double blue_emission = 0.0) {
    return {
        {red_reflectance, green_reflectance, blue_reflectance},
        {red_emission, green_emission, blue_emission},
    };
}

void append_box(
    std::vector<OrientedRectangle>& surfaces, std::string_view name,
    Vec3 center, double width, double depth, double height,
    double yaw_degrees, const DiffusePatchMaterial& box_material,
    std::size_t horizontal_subdivisions,
    std::size_t vertical_subdivisions) {
    const double radians =
        yaw_degrees * (std::numbers::pi_v<double> / 180.0);
    const Vec3 axis_width{
        std::cos(radians), 0.0, -std::sin(radians)};
    const Vec3 axis_depth{
        std::sin(radians), 0.0, std::cos(radians)};
    const Vec3 up{0.0, height, 0.0};
    const double half_width = width * 0.5;
    const double half_depth = depth * 0.5;
    const Vec3 width_vector = scale(axis_width, width);
    const Vec3 depth_vector = scale(axis_depth, depth);
    const Vec3 negative_width_vector = scale(width_vector, -1.0);
    const Vec3 negative_depth_vector = scale(depth_vector, -1.0);
    const Vec3 width_low = scale(axis_width, -half_width);
    const Vec3 width_high = scale(axis_width, half_width);
    const Vec3 depth_low = scale(axis_depth, -half_depth);
    const Vec3 depth_high = scale(axis_depth, half_depth);
    const Vec3 base_center{center.x, 0.0, center.z};
    const Vec3 top_center{center.x, height, center.z};
    const std::string prefix(name);

    surfaces.push_back(OrientedRectangle{
        prefix + "-top",
        add(top_center, add(width_low, depth_high)),
        width_vector,
        negative_depth_vector,
        horizontal_subdivisions,
        horizontal_subdivisions,
        box_material,
    });
    surfaces.push_back(OrientedRectangle{
        prefix + "-negative-width",
        add(base_center, add(width_low, depth_low)),
        depth_vector,
        up,
        horizontal_subdivisions,
        vertical_subdivisions,
        box_material,
    });
    surfaces.push_back(OrientedRectangle{
        prefix + "-positive-width",
        add(base_center, add(width_high, depth_high)),
        negative_depth_vector,
        up,
        horizontal_subdivisions,
        vertical_subdivisions,
        box_material,
    });
    surfaces.push_back(OrientedRectangle{
        prefix + "-negative-depth",
        add(base_center, add(width_high, depth_low)),
        negative_width_vector,
        up,
        horizontal_subdivisions,
        vertical_subdivisions,
        box_material,
    });
    surfaces.push_back(OrientedRectangle{
        prefix + "-positive-depth",
        add(base_center, add(width_low, depth_high)),
        width_vector,
        up,
        horizontal_subdivisions,
        vertical_subdivisions,
        box_material,
    });
}

[[nodiscard]] std::vector<OrientedRectangle>
make_cornell_surfaces() {
    constexpr double unit = 1.0 / 256.0;
    const DiffusePatchMaterial white =
        material(192.0 * unit, 192.0 * unit, 192.0 * unit);
    const DiffusePatchMaterial red =
        material(192.0 * unit, 16.0 * unit, 12.0 * unit);
    const DiffusePatchMaterial green =
        material(16.0 * unit, 176.0 * unit, 20.0 * unit);
    const DiffusePatchMaterial box =
        material(176.0 * unit, 176.0 * unit, 176.0 * unit);
    const DiffusePatchMaterial light =
        material(0.0, 0.0, 0.0, 3.5, 3.25, 2.75);

    std::vector<OrientedRectangle> surfaces;
    surfaces.reserve(16);
    surfaces.push_back(OrientedRectangle{
        "back-wall",
        {1.0, 0.0, 2.0},
        {-2.0, 0.0, 0.0},
        {0.0, 2.0, 0.0},
        6,
        6,
        white,
    });
    surfaces.push_back(OrientedRectangle{
        "floor",
        {-1.0, 0.0, 2.0},
        {2.0, 0.0, 0.0},
        {0.0, 0.0, -2.0},
        6,
        6,
        white,
    });
    surfaces.push_back(OrientedRectangle{
        "ceiling",
        {-1.0, 2.0, 0.0},
        {2.0, 0.0, 0.0},
        {0.0, 0.0, 2.0},
        6,
        6,
        white,
    });
    surfaces.push_back(OrientedRectangle{
        "left-wall",
        {-1.0, 0.0, 2.0},
        {0.0, 0.0, -2.0},
        {0.0, 2.0, 0.0},
        6,
        6,
        red,
    });
    surfaces.push_back(OrientedRectangle{
        "right-wall",
        {1.0, 0.0, 0.0},
        {0.0, 0.0, 2.0},
        {0.0, 2.0, 0.0},
        6,
        6,
        green,
    });
    surfaces.push_back(OrientedRectangle{
        "ceiling-emitter",
        {-0.325, 1.99, 0.775},
        {0.65, 0.0, 0.0},
        {0.0, 0.0, 0.55},
        2,
        2,
        light,
    });

    append_box(surfaces, "short-box", {-0.38, 0.0, 1.05},
               0.65, 0.65, 0.60, -16.0, box, 3, 2);
    append_box(surfaces, "tall-box", {0.38, 0.0, 1.38},
               0.58, 0.58, 1.20, 17.0, box, 3, 4);
    return surfaces;
}

[[nodiscard]] std::optional<double> maximum_interval_width(
    const Certificate& certificate) {
    if (certificate.coefficient_bounds.empty()) {
        return std::nullopt;
    }
    double maximum_width = 0.0;
    for (const BoundedValue& bound : certificate.coefficient_bounds) {
        maximum_width = std::max(
            maximum_width,
            math::subtract_up(bound.upper, bound.lower));
    }
    return maximum_width;
}

[[nodiscard]] std::string summary_text(
    const Certificate& certificate,
    const DiffusePatchAssemblyDiagnostics& diagnostics,
    std::string_view backend_name) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream << "LumenBound cornell-box\n"
           << "assembly_status: DeterministicUnbounded\n"
           << "assembly_method: "
              "deterministic_diffuse_patch_collocation_v1\n"
           << "continuous_scene_certified: false\n"
           << "proof_status: "
           << to_string(certificate.proof_status) << '\n'
           << "proof_failure: "
           << to_string(certificate.proof_failure) << '\n'
           << "proof_reason: " << certificate.proof_reason << '\n'
           << "target_status: "
           << to_string(certificate.target_status) << '\n'
           << "target_reason: " << certificate.target_reason << '\n'
           << "backend: " << backend_name << '\n'
           << "problem_digest: ";
    if (certificate.problem_digest.empty()) {
        stream << "unavailable";
    } else {
        stream << certificate.problem_digest;
    }
    stream << '\n'
           << "surfaces: " << diagnostics.surface_count << '\n'
           << "patches: " << diagnostics.patch_count << '\n'
           << "transport_rays: "
           << diagnostics.transport_ray_count << '\n'
           << "projection_nonzeros: "
           << diagnostics.projection_nonzero_count << '\n'
           << "q_upper: ";
    if (certificate.contraction_upper_bound.has_value()) {
        stream << *certificate.contraction_upper_bound;
    } else {
        stream << "unavailable";
    }
    stream << '\n'
           << "interval_iterations: "
           << certificate.interval_iteration_count << '\n'
           << "maximum_coefficient_interval_width: ";
    const std::optional<double> maximum_width =
        maximum_interval_width(certificate);
    if (maximum_width.has_value()) {
        stream << *maximum_width;
    } else {
        stream << "unavailable";
    }
    stream << '\n'
           << "mse_upper: ";
    if (certificate.mse_upper_bound.has_value()) {
        stream << *certificate.mse_upper_bound;
    } else {
        stream << "unavailable";
    }
    stream << '\n'
           << "psnr_lower: ";
    if (certificate.psnr_lower_bound_kind == PsnrBoundKind::Finite &&
        certificate.psnr_lower_bound.has_value()) {
        stream << *certificate.psnr_lower_bound;
    } else if (certificate.psnr_lower_bound_kind ==
               PsnrBoundKind::PositiveInfinity) {
        stream << "positive_infinity";
    } else {
        stream << "unavailable";
    }
    stream << '\n';
    return stream.str();
}

[[nodiscard]] std::string preview_only_summary_text(
    const CandidateSolution& candidate,
    const DiffusePatchAssemblyDiagnostics& diagnostics) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream
        << "LumenBound cornell-box\n"
        << "render_mode: PreviewOnly\n"
        << "assembly_status: DeterministicUnbounded\n"
        << "assembly_method: "
           "deterministic_diffuse_patch_collocation_v1\n"
        << "continuous_scene_certified: false\n"
        << "finite_system_proof_status: NotRun\n"
        << "backend: cpu-reference\n"
        << "surfaces: " << diagnostics.surface_count << '\n'
        << "patches: " << diagnostics.patch_count << '\n'
        << "transport_rays: "
        << diagnostics.transport_ray_count << '\n'
        << "projection_nonzeros: "
        << diagnostics.projection_nonzero_count << '\n'
        << "candidate_residual_infinity_norm: "
        << candidate.residual_infinity_norm << '\n';
    return stream.str();
}

[[nodiscard]] std::string assembly_record(
    const Certificate& certificate,
    const DiffusePatchAssemblyDiagnostics& diagnostics,
    const CornellBoxDemoOptions& options) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream
        << "{\n"
        << "  \"schema_version\":\"lumenbound.diffuse-assembly.v1\",\n"
        << "  \"scene_id\":\"cornell-box-v1\",\n"
        << "  \"assembly_status\":\"DeterministicUnbounded\",\n"
        << "  \"assembly_method\":"
           "\"deterministic_diffuse_patch_collocation_v1\",\n"
        << "  \"problem_digest\":\""
        << certificate.problem_digest << "\",\n"
        << "  \"finite_system_proof_status\":\""
        << to_string(certificate.proof_status) << "\",\n"
        << "  \"finite_system_target_status\":\""
        << to_string(certificate.target_status) << "\",\n"
        << "  \"proof_boundary\":{"
           "\"finite_binary64_system_in_scope\":true,"
           "\"continuous_scene\":false,"
           "\"geometry\":false,"
           "\"visibility\":false,"
           "\"quadrature\":false,"
           "\"discretization\":false,"
           "\"display_mapping\":false},\n"
        << "  \"band_semantics\":["
           "\"declared_linear_srgb_r\","
           "\"declared_linear_srgb_g\","
           "\"declared_linear_srgb_b\"],\n"
        << "  \"colorimetry_status\":\"unvalidated_demo_rgb\",\n"
        << "  \"projection_reconstruction\":"
           "\"nonnegative_bilinear_patch_reconstruction\",\n"
        << "  \"projection_role\":"
           "\"display_oriented_finite_reconstruction\",\n"
        << "  \"preview_mapping\":"
           "\"exposure_then_clamp_then_srgb_oetf_then_u8\",\n"
        << "  \"preview_exposure\":" << options.preview_exposure << ",\n"
        << "  \"image\":{\"width\":" << options.image_width
        << ",\"height\":" << options.image_height
        << ",\"origin\":\"top_left\",\"order\":\"row_major\"},\n"
        << "  \"quadrature\":{"
           "\"surface_samples_per_axis\":2,"
           "\"hemisphere_radial_steps\":8,"
           "\"hemisphere_azimuth_steps\":32,"
           "\"pixel_samples_per_axis\":2},\n"
        << "  \"counts\":{\"surfaces\":"
        << diagnostics.surface_count
        << ",\"patches\":" << diagnostics.patch_count
        << ",\"bands\":" << diagnostics.coefficient_band_count
        << ",\"transport_rays\":" << diagnostics.transport_ray_count
        << ",\"escaped_transport_rays\":"
        << diagnostics.escaped_transport_ray_count
        << ",\"blocked_backface_transport_rays\":"
        << diagnostics.blocked_backface_transport_ray_count
        << ",\"primary_rays\":" << diagnostics.primary_ray_count
        << ",\"missed_primary_rays\":"
        << diagnostics.missed_primary_ray_count
        << ",\"blocked_backface_primary_rays\":"
        << diagnostics.blocked_backface_primary_ray_count
        << ",\"projection_nonzeros\":"
        << diagnostics.projection_nonzero_count << "},\n"
        << "  \"maximum_reflectance\":"
        << diagnostics.maximum_reflectance << ",\n"
        << "  \"maximum_form_factor_row_sum\":"
        << diagnostics.maximum_form_factor_row_sum << ",\n"
        << "  \"maximum_projection_row_sum\":"
        << diagnostics.maximum_projection_row_sum << ",\n"
        << "  \"ray_origin_offset\":"
        << diagnostics.ray_origin_offset << ",\n"
        << "  \"intersection_epsilon\":"
        << diagnostics.intersection_epsilon << "\n"
        << "}\n";
    return stream.str();
}

void write_atomic_text(const std::filesystem::path& path,
                       std::string_view text) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code error;
    static_cast<void>(std::filesystem::remove(temporary, error));
    if (error) {
        throw std::runtime_error(
            "failed to remove stale assembly output");
    }

    std::ofstream stream(
        temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error(
            "failed to open assembly output");
    }
    stream.write(text.data(),
                 static_cast<std::streamsize>(text.size()));
    stream.flush();
    if (!stream) {
        stream.close();
        static_cast<void>(std::filesystem::remove(temporary, error));
        throw std::runtime_error(
            "failed to write assembly output");
    }
    stream.close();

    error.clear();
    static_cast<void>(std::filesystem::remove(path, error));
    if (error) {
        static_cast<void>(std::filesystem::remove(temporary, error));
        throw std::runtime_error(
            "failed to replace assembly output");
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code cleanup_error;
        static_cast<void>(
            std::filesystem::remove(temporary, cleanup_error));
        throw std::runtime_error(
            "failed to publish assembly output");
    }
}

}  // namespace

CornellBoxProblem make_cornell_box_problem(
    std::size_t image_width, std::size_t image_height) {
    const DiffusePatchAssemblyOptions assembly_options{
        image_width,
        image_height,
        2,
        8,
        32,
        2,
    };
    const PinholeCamera camera{
        {0.0, 1.0, -3.2},
        {0.0, 0.92, 1.10},
        {0.0, 1.0, 0.0},
        40.0,
    };
    return CornellBoxProblem{
        assemble_diffuse_patch_problem(
            make_cornell_surfaces(), camera, assembly_options),
        image_width,
        image_height,
    };
}

CornellBoxDemoRunResult run_cornell_box(
    const CornellBoxDemoOptions& options, std::ostream& summary,
    std::ostream& errors) {
    const std::size_t maximum_dimension =
        options.preview_only ? 1024U : 256U;
    if (options.image_width < 16 ||
        options.image_width > maximum_dimension ||
        options.image_height < 16 ||
        options.image_height > maximum_dimension) {
        errors << "demo failed: "
               << (options.preview_only
                       ? "cornell_preview_dimensions_must_be_16_to_1024"
                       : "cornell_image_dimensions_must_be_16_to_256")
               << '\n';
        return {};
    }
    if (!std::isfinite(options.preview_exposure) ||
        options.preview_exposure <= 0.0) {
        errors << "demo failed: preview_exposure_must_be_finite_and_positive\n";
        return {};
    }

    try {
        CornellBoxProblem problem = make_cornell_box_problem(
            options.image_width, options.image_height);
        if (options.preview_only) {
            const ProjectionValidationReport projection_validation =
                problem.assembly.projection.validate(
                    problem.assembly.system
                        .transport_coefficient_count());
            if (!projection_validation.valid()) {
                errors << "demo failed: invalid_preview_projection ("
                       << projection_validation.reason << ")\n";
                return CornellBoxDemoRunResult{
                    3, {}, problem.assembly.diagnostics};
            }

            CandidateSolution candidate =
                solve_candidate(problem.assembly.system);
            std::vector<DenseVector> pixel_bands;
            pixel_bands.reserve(candidate.values.size());
            for (const DenseVector& coefficient_band :
                 candidate.values) {
                pixel_bands.push_back(
                    problem.assembly.projection.project(
                        coefficient_band));
            }

            try {
                write_preview_only_output(
                    options.output_directory, pixel_bands,
                    problem.image_width, problem.image_height,
                    options.preview_exposure);
            } catch (const std::exception&) {
                errors
                    << "demo failed: demo_output_write_failed\n";
                return CornellBoxDemoRunResult{
                    3, {}, problem.assembly.diagnostics};
            }

            summary << preview_only_summary_text(
                candidate, problem.assembly.diagnostics);
            return CornellBoxDemoRunResult{
                0, {}, problem.assembly.diagnostics};
        }

        const CertificationOptions certification_options{
            options.signal_peak,
            options.target_psnr,
            options.maximum_iterations,
            false,
        };
        const CpuReferenceBackend backend;
        CertificationResult result = backend.run(
            problem.assembly.system, problem.assembly.projection,
            certification_options);

        try {
            const std::filesystem::path assembly_path =
                options.output_directory / "assembly.json";
            write_demo_outputs(
                options.output_directory, result,
                problem.image_width, problem.image_height,
                PreviewSettings{
                    PreviewMapping::DeclaredLinearSrgb,
                    options.preview_exposure,
                });
            write_atomic_text(
                assembly_path,
                assembly_record(result.certificate,
                                problem.assembly.diagnostics, options));
        } catch (const std::exception&) {
            errors << "demo failed: demo_output_write_failed\n";
            return CornellBoxDemoRunResult{
                3, std::move(result), problem.assembly.diagnostics};
        }

        summary << summary_text(
            result.certificate, problem.assembly.diagnostics,
            backend.name());
        if (result.certificate.proof_status != ProofStatus::Certified) {
            errors << "finite-system proof failed: "
                   << to_string(result.certificate.proof_failure) << " ("
                   << result.certificate.proof_reason << ")\n";
            return CornellBoxDemoRunResult{
                2, std::move(result), problem.assembly.diagnostics};
        }
        if (result.certificate.target_status != TargetStatus::Reached) {
            errors << "finite-system target not reached: "
                   << to_string(result.certificate.target_status) << " ("
                   << result.certificate.target_reason << ")\n";
            return CornellBoxDemoRunResult{
                2, std::move(result), problem.assembly.diagnostics};
        }
        return CornellBoxDemoRunResult{
            0, std::move(result), problem.assembly.diagnostics};
    } catch (const std::exception& error) {
        errors << "demo failed: cornell_box_assembly_failed ("
               << error.what() << ")\n";
        return {};
    }
}

}  // namespace lumenbound
