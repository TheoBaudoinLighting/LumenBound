#include "lumenbound/core/demo.hpp"

#include <charconv>
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
           " --output <directory>"
           " --peak <positive-value>"
           " --target-psnr <decibels>"
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
        std::string_view(arguments[1]) != "demo" ||
        std::string_view(arguments[2]) != "certified-patches") {
        print_usage(std::cerr);
        return 64;
    }

    lumenbound::DemoOptions options;
    options.output_directory = "out/certified-patches";

    int argument_index = 3;
    while (argument_index < argument_count) {
        const std::string_view option(arguments[argument_index]);
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
            options.output_directory = value;
        } else if (option == "--peak") {
            if (!parse_double(value, options.signal_peak)) {
                std::cerr << "invalid signal peak: " << value << '\n';
                return 64;
            }
        } else if (option == "--target-psnr") {
            if (!parse_double(value, options.target_psnr)) {
                std::cerr << "invalid PSNR target: " << value << '\n';
                return 64;
            }
        } else if (option == "--max-iterations") {
            if (!parse_size(value, options.maximum_iterations)) {
                std::cerr << "invalid iteration limit: " << value << '\n';
                return 64;
            }
        } else {
            std::cerr << "unknown option: " << option << '\n';
            return 64;
        }

        argument_index += 2;
    }

    const lumenbound::DemoRunResult result =
        lumenbound::run_certified_patches(options, std::cout, std::cerr);
    return result.exit_code;
}
