#include "lumenbound/io/output.hpp"

#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace lumenbound {
namespace {

[[nodiscard]] double canonical_zero(double value) noexcept {
    return value == 0.0 ? 0.0 : value;
}

[[nodiscard]] std::string decimal_string(double value) {
    value = canonical_zero(value);
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            "JSON scalar encoding requires a finite value");
    }

    char buffer[64]{};
    const auto conversion =
        std::to_chars(std::begin(buffer), std::end(buffer), value,
                      std::chars_format::general,
                      std::numeric_limits<double>::max_digits10);
    if (conversion.ec != std::errc{}) {
        throw std::runtime_error("failed to encode a decimal value");
    }
    return std::string(buffer, conversion.ptr);
}

[[nodiscard]] std::string binary64_string(double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    value = canonical_zero(value);
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);

    char buffer[16]{};
    const auto conversion =
        std::to_chars(std::begin(buffer), std::end(buffer), bits, 16);
    if (conversion.ec != std::errc{}) {
        throw std::runtime_error("failed to encode binary64 bits");
    }

    const std::size_t digit_count =
        static_cast<std::size_t>(conversion.ptr - std::begin(buffer));
    return std::string(16U - digit_count, '0') +
           std::string(std::begin(buffer), conversion.ptr);
}

[[nodiscard]] std::string json_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw std::invalid_argument(
                    "JSON string contains an unsupported control character");
            }
            escaped.push_back(character);
            break;
        }
    }
    return escaped;
}

void append_scalar(std::ostringstream& stream, double value) {
    stream << "{\"decimal\":" << decimal_string(value)
           << ",\"binary64\":\"" << binary64_string(value) << "\"}";
}

void append_input_scalar(std::ostringstream& stream, double value) {
    if (std::isfinite(value)) {
        append_scalar(stream, value);
        return;
    }

    const char* classification = "nan";
    if (std::isinf(value)) {
        classification =
            std::signbit(value) ? "negative_infinity"
                                : "positive_infinity";
    }
    stream << "{\"decimal\":null,\"binary64\":\""
           << binary64_string(value) << "\",\"classification\":\""
           << classification << "\"}";
}

void append_optional_scalar(std::ostringstream& stream,
                            const std::optional<double>& value) {
    if (value.has_value()) {
        append_scalar(stream, *value);
    } else {
        stream << "null";
    }
}

void append_psnr(std::ostringstream& stream,
                 const Certificate& certificate) {
    stream << "{\"kind\":\""
           << to_string(certificate.psnr_lower_bound_kind)
           << "\",\"value\":";
    if (certificate.psnr_lower_bound_kind == PsnrBoundKind::Finite &&
        certificate.psnr_lower_bound.has_value()) {
        append_scalar(stream, *certificate.psnr_lower_bound);
    } else {
        stream << "null";
    }
    stream << '}';
}

void append_bounded_values(std::ostringstream& stream,
                           const std::vector<BoundedValue>& values) {
    stream << '[';
    for (std::size_t value_index = 0; value_index < values.size();
         ++value_index) {
        if (value_index != 0) {
            stream << ',';
        }
        const BoundedValue& value = values[value_index];
        stream << "\n    {\"band\":" << value.band
               << ",\"index\":" << value.index
               << ",\"status\":\"" << to_string(value.status)
               << "\",\"candidate\":";
        append_scalar(stream, value.candidate);
        stream << ",\"lower\":";
        append_scalar(stream, value.lower);
        stream << ",\"upper\":";
        append_scalar(stream, value.upper);
        stream << ",\"absolute_error_upper_bound\":";
        append_scalar(stream, value.error_bound);
        stream << '}';
    }
    if (!values.empty()) {
        stream << '\n' << "  ";
    }
    stream << ']';
}

[[nodiscard]] std::optional<double> maximum_coefficient_width(
    const Certificate& certificate) {
    if (certificate.coefficient_bounds.empty()) {
        return std::nullopt;
    }
    double maximum_width = 0.0;
    for (const BoundedValue& value : certificate.coefficient_bounds) {
        maximum_width =
            std::max(maximum_width,
                     math::subtract_up(value.upper, value.lower));
    }
    return maximum_width;
}

void remove_file_if_present(const std::filesystem::path& path) {
    std::error_code error;
    static_cast<void>(std::filesystem::remove(path, error));
    if (error) {
        throw std::runtime_error("failed to remove an old output file");
    }
}

void write_text(const std::filesystem::path& path,
                std::string_view contents) {
    if (contents.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("output file exceeds stream limits");
    }

    std::filesystem::path temporary_path = path;
    temporary_path += ".tmp";
    remove_file_if_present(temporary_path);

    std::ofstream stream(temporary_path,
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("failed to open output file");
    }
    stream.write(contents.data(),
                 static_cast<std::streamsize>(contents.size()));
    stream.flush();
    if (!stream) {
        stream.close();
        remove_file_if_present(temporary_path);
        throw std::runtime_error("failed to write output file");
    }
    stream.close();

    remove_file_if_present(path);
    std::error_code rename_error;
    std::filesystem::rename(temporary_path, path, rename_error);
    if (rename_error) {
        remove_file_if_present(temporary_path);
        throw std::runtime_error("failed to publish output file");
    }
}

[[nodiscard]] std::string candidate_csv(
    const CertificationResult& result) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "band,coefficient,candidate,candidate_binary64\n";
    for (std::size_t band = 0;
         band < result.candidate.values.size(); ++band) {
        for (std::size_t coefficient = 0;
             coefficient < result.candidate.values[band].size();
             ++coefficient) {
            const double value =
                result.candidate.values[band][coefficient];
            stream << band << ',' << coefficient << ','
                   << decimal_string(value) << ','
                   << binary64_string(value) << '\n';
        }
    }
    return stream.str();
}

[[nodiscard]] std::string coefficient_bounds_csv(
    const Certificate& certificate) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream
        << "band,coefficient,status,candidate,lower,upper,"
           "absolute_error_upper_bound\n";
    for (const BoundedValue& value : certificate.coefficient_bounds) {
        stream << value.band << ',' << value.index << ','
               << to_string(value.status) << ','
               << decimal_string(value.candidate) << ','
               << decimal_string(value.lower) << ','
               << decimal_string(value.upper) << ','
               << decimal_string(value.error_bound) << '\n';
    }
    return stream.str();
}

[[nodiscard]] std::string pixels_csv(const Certificate& certificate) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream
        << "pixel,band,status,candidate,lower,upper,"
           "absolute_error_upper_bound\n";
    for (const BoundedValue& value : certificate.pixel_bounds) {
        stream << value.index << ',' << value.band << ','
               << to_string(value.status) << ','
               << decimal_string(value.candidate) << ','
               << decimal_string(value.lower) << ','
               << decimal_string(value.upper) << ','
               << decimal_string(value.error_bound) << '\n';
    }
    return stream.str();
}

[[nodiscard]] int preview_channel(double value, double signal_peak) {
    const double normalized = std::clamp(value / signal_peak, 0.0, 1.0);
    return static_cast<int>(std::lround(normalized * 255.0));
}

[[nodiscard]] std::string preview_ppm(const Certificate& certificate,
                                      std::size_t width,
                                      std::size_t height) {
    if (width == 0 || height == 0 ||
        width > (std::numeric_limits<std::size_t>::max() / height) ||
        (width * height) != certificate.pixel_count ||
        certificate.spectral_coefficient_count < 3) {
        throw std::invalid_argument(
            "preview dimensions or band count are invalid");
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "P3\n"
           << "# Non-certifying linear clamp preview\n"
           << width << ' ' << height << "\n255\n";

    for (std::size_t pixel = 0; pixel < certificate.pixel_count;
         ++pixel) {
        int channels[3]{};
        for (std::size_t band = 0; band < 3; ++band) {
            const auto found = std::find_if(
                certificate.pixel_bounds.begin(),
                certificate.pixel_bounds.end(),
                [band, pixel](const BoundedValue& value) {
                    return value.band == band && value.index == pixel;
                });
            if (found == certificate.pixel_bounds.end()) {
                throw std::runtime_error(
                    "preview pixel coefficient is missing");
            }
            channels[band] =
                preview_channel(found->candidate,
                                certificate.signal_peak);
        }
        stream << channels[0] << ' ' << channels[1] << ' '
               << channels[2] << '\n';
    }
    return stream.str();
}

}  // namespace

std::string certificate_to_json(const Certificate& certificate) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "{\n"
           << "  \"schema_version\":\""
           << json_escape(certificate.schema_version) << "\",\n"
           << "  \"status\":\"" << to_string(certificate.status)
           << "\",\n"
           << "  \"reason\":\""
           << json_escape(certificate.reason) << "\",\n"
           << "  \"dimensions\":{\"spectral_coefficients\":"
           << certificate.spectral_coefficient_count
           << ",\"transport_coefficients\":"
           << certificate.transport_coefficient_count
           << ",\"pixels\":" << certificate.pixel_count << "},\n"
           << "  \"assumptions\":[";
    for (std::size_t assumption_index = 0;
         assumption_index < certificate.assumptions.size();
         ++assumption_index) {
        if (assumption_index != 0) {
            stream << ',';
        }
        stream << "\n    \""
               << json_escape(
                      certificate.assumptions[assumption_index])
               << '"';
    }
    if (!certificate.assumptions.empty()) {
        stream << '\n' << "  ";
    }
    stream << "],\n"
           << "  \"contraction_upper_bound\":";
    append_optional_scalar(stream,
                           certificate.contraction_upper_bound);
    stream << ",\n"
           << "  \"interval_iteration_count\":"
           << certificate.interval_iteration_count << ",\n"
           << "  \"coefficient_bounds\":";
    append_bounded_values(stream, certificate.coefficient_bounds);
    stream << ",\n"
           << "  \"pixel_bounds\":";
    append_bounded_values(stream, certificate.pixel_bounds);
    stream << ",\n"
           << "  \"residual_upper_bound\":";
    append_optional_scalar(stream, certificate.residual_upper_bound);
    stream << ",\n"
           << "  \"candidate_error_upper_bound\":";
    append_optional_scalar(
        stream, certificate.candidate_error_upper_bound);
    stream << ",\n"
           << "  \"mse_upper_bound\":";
    append_optional_scalar(stream, certificate.mse_upper_bound);
    stream << ",\n"
           << "  \"psnr_lower_bound\":";
    append_psnr(stream, certificate);
    stream << ",\n"
           << "  \"signal_peak\":";
    append_input_scalar(stream, certificate.signal_peak);
    stream << ",\n"
           << "  \"target_psnr\":";
    append_input_scalar(stream, certificate.target_psnr);
    stream << ",\n"
           << "  \"output_domain\":\"raw_linear_coefficients\"\n"
           << "}\n";
    return stream.str();
}

std::string metrics_to_json(const Certificate& certificate) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << "{\n"
           << "  \"schema_version\":\"lumenbound.metrics.v1\",\n"
           << "  \"status\":\"" << to_string(certificate.status)
           << "\",\n"
           << "  \"reason\":\""
           << json_escape(certificate.reason) << "\",\n"
           << "  \"contraction_upper_bound\":";
    append_optional_scalar(stream,
                           certificate.contraction_upper_bound);
    stream << ",\n"
           << "  \"interval_iteration_count\":"
           << certificate.interval_iteration_count << ",\n"
           << "  \"maximum_coefficient_interval_width\":";
    append_optional_scalar(
        stream, maximum_coefficient_width(certificate));
    stream << ",\n"
           << "  \"mse_upper_bound\":";
    append_optional_scalar(stream, certificate.mse_upper_bound);
    stream << ",\n"
           << "  \"psnr_lower_bound\":";
    append_psnr(stream, certificate);
    stream << ",\n"
           << "  \"signal_peak\":";
    append_input_scalar(stream, certificate.signal_peak);
    stream << ",\n"
           << "  \"target_psnr\":";
    append_input_scalar(stream, certificate.target_psnr);
    stream << "\n}\n";
    return stream.str();
}

void write_demo_outputs(const std::filesystem::path& output_directory,
                        const CertificationResult& result,
                        std::size_t image_width,
                        std::size_t image_height) {
    const std::string certificate_json =
        certificate_to_json(result.certificate);
    const std::string metrics_json =
        metrics_to_json(result.certificate);

    const bool has_candidate = !result.candidate.values.empty();
    const bool has_coefficient_bounds =
        !result.certificate.coefficient_bounds.empty();
    const bool has_pixel_bounds =
        !result.certificate.pixel_bounds.empty();
    const bool can_write_preview =
        has_pixel_bounds &&
        result.certificate.spectral_coefficient_count >= 3 &&
        std::isfinite(result.certificate.signal_peak) &&
        result.certificate.signal_peak > 0.0;

    std::string candidate_data;
    std::string coefficient_data;
    std::string pixel_data;
    std::string preview_data;
    if (has_candidate) {
        candidate_data = candidate_csv(result);
    }
    if (has_coefficient_bounds) {
        coefficient_data =
            coefficient_bounds_csv(result.certificate);
    }
    if (has_pixel_bounds) {
        pixel_data = pixels_csv(result.certificate);
    }
    if (can_write_preview) {
        preview_data = preview_ppm(
            result.certificate, image_width, image_height);
    }

    if (result.certificate.status == CertificateStatus::Certified &&
        (!has_candidate || !has_coefficient_bounds ||
         !has_pixel_bounds || !can_write_preview)) {
        throw std::runtime_error(
            "certified demonstration output is incomplete");
    }

    std::filesystem::create_directories(output_directory);
    if (!std::filesystem::is_directory(output_directory)) {
        throw std::runtime_error(
            "output path is not a directory");
    }

    constexpr std::array<std::string_view, 6> output_names{
        "candidate-coefficients.csv",
        "coefficient-bounds.csv",
        "linear-pixels.csv",
        "preview.ppm",
        "metrics.json",
        "certificate.json",
    };
    for (const std::string_view name : output_names) {
        const std::filesystem::path path = output_directory / name;
        remove_file_if_present(path);
        std::filesystem::path temporary_path = path;
        temporary_path += ".tmp";
        remove_file_if_present(temporary_path);
    }

    if (has_candidate) {
        write_text(output_directory / "candidate-coefficients.csv",
                   candidate_data);
    }
    if (has_coefficient_bounds) {
        write_text(output_directory / "coefficient-bounds.csv",
                   coefficient_data);
    }
    if (has_pixel_bounds) {
        write_text(output_directory / "linear-pixels.csv",
                   pixel_data);
    }
    if (can_write_preview) {
        write_text(output_directory / "preview.ppm", preview_data);
    }
    write_text(output_directory / "metrics.json", metrics_json);
    write_text(output_directory / "certificate.json",
               certificate_json);
}

}  // namespace lumenbound
