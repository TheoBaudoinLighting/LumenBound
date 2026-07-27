#include "lumenbound/core/cornell_box_demo.hpp"
#include "lumenbound/core/demo.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

void print_usage(std::ostream& stream) {
    stream
        << "Usage:\n"
        << "  lumenbound demo certified-patches"
           " [--output <directory>]"
           " [--peak <positive-value>]"
           " [--target-psnr <decibels>]"
           " [--max-iterations <count>]\n"
        << "  lumenbound demo cornell-box"
           " [--output <directory>]"
           " [--width <16..1024>]"
           " [--height <16..1024>]"
           " [--preview-exposure <positive-value>]"
           " [--preview-only]"
           " [--peak <positive-value>]"
           " [--target-psnr <decibels>]"
           " [--max-iterations <count>]\n";
}

[[nodiscard]] bool parse_double(std::string_view text, double& value) {
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), value,
                        std::chars_format::general);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size();
}

[[nodiscard]] bool parse_size(std::string_view text,
                              std::size_t& value) {
    std::uint64_t parsed = 0;
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} ||
        result.ptr != text.data() + text.size() ||
        parsed >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

}  // namespace

int main(int argument_count, char* arguments[]) {
    if (argument_count == 2 &&
        std::string_view(arguments[1]) == "--help") {
        print_usage(std::cout);
        return 0;
    }

    if (argument_count < 3 ||
        std::string_view(arguments[1]) != "demo") {
        print_usage(std::cerr);
        return 64;
    }

    const std::string_view demo_name(arguments[2]);
    const bool is_certified_patches =
        demo_name == "certified-patches";
    const bool is_cornell_box = demo_name == "cornell-box";
    if (!is_certified_patches && !is_cornell_box) {
        print_usage(std::cerr);
        return 64;
    }

    lumenbound::DemoOptions certified_patches_options;
    certified_patches_options.output_directory =
        "out/certified-patches";
    lumenbound::CornellBoxDemoOptions cornell_box_options;

    int argument_index = 3;
    while (argument_index < argument_count) {
        const std::string_view option(arguments[argument_index]);
        if (is_cornell_box && option == "--preview-only") {
            cornell_box_options.preview_only = true;
            ++argument_index;
            continue;
        }
        if (argument_index + 1 >= argument_count) {
            std::cerr << "missing value for option: " << option << '\n';
            return 64;
        }
        const std::string_view value(arguments[argument_index + 1]);

        if (option == "--output") {
            if (value.empty()) {
                std::cerr << "output directory must not be empty\n";
                return 64;
            }
            if (is_certified_patches) {
                certified_patches_options.output_directory = value;
            } else {
                cornell_box_options.output_directory = value;
            }
        } else if (option == "--peak") {
            double parsed = 0.0;
            if (!parse_double(value, parsed)) {
                std::cerr << "invalid signal peak: " << value << '\n';
                return 64;
            }
            if (is_certified_patches) {
                certified_patches_options.signal_peak = parsed;
            } else {
                cornell_box_options.signal_peak = parsed;
            }
        } else if (option == "--target-psnr") {
            double parsed = 0.0;
            if (!parse_double(value, parsed)) {
                std::cerr << "invalid PSNR target: " << value << '\n';
                return 64;
            }
            if (is_certified_patches) {
                certified_patches_options.target_psnr = parsed;
            } else {
                cornell_box_options.target_psnr = parsed;
            }
        } else if (option == "--max-iterations") {
            std::size_t parsed = 0;
            if (!parse_size(value, parsed)) {
                std::cerr << "invalid iteration limit: " << value << '\n';
                return 64;
            }
            if (is_certified_patches) {
                certified_patches_options.maximum_iterations = parsed;
            } else {
                cornell_box_options.maximum_iterations = parsed;
            }
        } else if (is_cornell_box && option == "--width") {
            if (!parse_size(value, cornell_box_options.image_width)) {
                std::cerr << "invalid Cornell image width: "
                          << value << '\n';
                return 64;
            }
        } else if (is_cornell_box && option == "--height") {
            if (!parse_size(value, cornell_box_options.image_height)) {
                std::cerr << "invalid Cornell image height: "
                          << value << '\n';
                return 64;
            }
        } else if (is_cornell_box &&
                   option == "--preview-exposure") {
            if (!parse_double(
                    value, cornell_box_options.preview_exposure)) {
                std::cerr << "invalid preview exposure: "
                          << value << '\n';
                return 64;
            }
        } else {
            std::cerr << "unknown option: " << option << '\n';
            return 64;
        }

        argument_index += 2;
    }

    if (is_certified_patches) {
        const lumenbound::DemoRunResult result =
            lumenbound::run_certified_patches(
                certified_patches_options, std::cout, std::cerr);
        return result.exit_code;
    }
    if (cornell_box_options.image_width < 16 ||
        cornell_box_options.image_width >
            (cornell_box_options.preview_only ? 1024U : 256U) ||
        cornell_box_options.image_height < 16 ||
        cornell_box_options.image_height >
            (cornell_box_options.preview_only ? 1024U : 256U)) {
        std::cerr
            << "Cornell image dimensions must be between 16 and "
            << (cornell_box_options.preview_only ? 1024 : 256)
            << '\n';
        return 64;
    }
    if (!std::isfinite(cornell_box_options.preview_exposure) ||
        cornell_box_options.preview_exposure <= 0.0) {
        std::cerr << "preview exposure must be finite and positive\n";
        return 64;
    }
    const lumenbound::CornellBoxDemoRunResult result =
        lumenbound::run_cornell_box(
            cornell_box_options, std::cout, std::cerr);
    return result.exit_code;
}
