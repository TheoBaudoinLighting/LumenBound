#include "lumenbound/core/demo.hpp"

#include "lumenbound/core/backend.hpp"
#include "lumenbound/io/output.hpp"
#include "lumenbound/math/rounding.hpp"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <ostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace lumenbound {
namespace {

[[nodiscard]] DenseMatrix dyadic_transport(
    std::initializer_list<double> numerators) {
    std::vector<double> values;
    values.reserve(numerators.size());
    for (const double numerator : numerators) {
        values.push_back(numerator / 64.0);
    }
    return DenseMatrix(6, 6, std::move(values));
}

[[nodiscard]] DenseVector dyadic_coefficients(
    std::initializer_list<double> numerators) {
    std::vector<double> values;
    values.reserve(numerators.size());
    for (const double numerator : numerators) {
        values.push_back(numerator / 16.0);
    }
    return DenseVector(std::move(values));
}

[[nodiscard]] DenseVector manufacture_emission(
    const DenseMatrix& transport, const DenseVector& exact) {
    return exact - transport.multiply(exact);
}

[[nodiscard]] std::optional<double> maximum_interval_width(
    const Certificate& certificate) {
    if (certificate.coefficient_bounds.empty()) {
        return std::nullopt;
    }
    double maximum_width = 0.0;
    for (const BoundedValue& bound : certificate.coefficient_bounds) {
        maximum_width =
            std::max(maximum_width,
                     math::subtract_up(bound.upper, bound.lower));
    }
    return maximum_width;
}

[[nodiscard]] std::string summary_text(
    const Certificate& certificate, std::string_view backend_name) {
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(
        std::numeric_limits<double>::max_digits10);
    stream << "LumenBound certified-patches\n"
           << "status: " << to_string(certificate.status) << '\n'
           << "reason: " << certificate.reason << '\n'
           << "backend: " << backend_name << '\n'
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
    switch (certificate.psnr_lower_bound_kind) {
    case PsnrBoundKind::Finite:
        if (certificate.psnr_lower_bound.has_value()) {
            stream << *certificate.psnr_lower_bound;
        } else {
            stream << "unavailable";
        }
        break;
    case PsnrBoundKind::PositiveInfinity:
        stream << "positive_infinity";
        break;
    case PsnrBoundKind::Unavailable:
        stream << "unavailable";
        break;
    }
    stream << '\n';
    return stream.str();
}

}  // namespace

ManufacturedProblem make_certified_patches_problem() {
    std::vector<DenseMatrix> transports;
    transports.push_back(dyadic_transport({
        2, 1, 0, 1, 0, 0,
        1, 2, 1, 0, 1, 0,
        0, 1, 3, 1, 0, 1,
        1, 0, 1, 2, 1, 0,
        0, 1, 0, 1, 2, 1,
        1, 0, 1, 0, 1, 2,
    }));
    transports.push_back(dyadic_transport({
        1, 1, 1, 0, 0, 0,
        0, 2, 1, 1, 0, 0,
        1, 0, 2, 1, 1, 0,
        0, 1, 0, 3, 1, 1,
        1, 0, 1, 0, 2, 1,
        0, 1, 0, 1, 1, 2,
    }));
    transports.push_back(dyadic_transport({
        2, 0, 1, 0, 1, 0,
        1, 1, 0, 1, 0, 1,
        0, 1, 2, 0, 1, 1,
        1, 0, 1, 2, 0, 1,
        0, 1, 0, 1, 3, 1,
        1, 0, 1, 0, 1, 2,
    }));

    std::vector<DenseVector> exact;
    exact.push_back(dyadic_coefficients({13, 10, 7, 5, 8, 11}));
    exact.push_back(dyadic_coefficients({3, 6, 9, 12, 10, 7}));
    exact.push_back(dyadic_coefficients({2, 4, 3, 6, 5, 4}));

    std::vector<DenseVector> emissions;
    emissions.reserve(exact.size());
    for (std::size_t band = 0; band < exact.size(); ++band) {
        emissions.push_back(
            manufacture_emission(transports[band], exact[band]));
    }

    DenseMatrix projection_weights(
        4, 6,
        {
            0.5, 0.25, 0.25, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.25, 0.5, 0.25, 0.0,
            0.0, 0.25, 0.0, 0.0, 0.25, 0.5,
            0.25, 0.0, 0.0, 0.25, 0.0, 0.5,
        });

    return ManufacturedProblem{
        TransportSystem(std::move(emissions), std::move(transports)),
        Projection(std::move(projection_weights)),
        std::move(exact),
        2,
        2,
    };
}

DemoRunResult run_certified_patches(const DemoOptions& options,
                                    std::ostream& summary,
                                    std::ostream& errors) {
    ManufacturedProblem problem = make_certified_patches_problem();
    const CertificationOptions certification_options{
        options.signal_peak,
        options.target_psnr,
        options.maximum_iterations,
    };

    const CpuReferenceBackend backend;
    CertificationResult result =
        backend.run(problem.system, problem.projection,
                    certification_options);

    try {
        write_demo_outputs(options.output_directory, result,
                           problem.image_width, problem.image_height);
    } catch (const std::exception&) {
        result.certificate.status = CertificateStatus::NumericalFailure;
        result.certificate.reason = "demo_output_write_failed";
        errors << "demo failed: " << result.certificate.reason << '\n';
        return DemoRunResult{3, std::move(result)};
    }

    summary << summary_text(result.certificate, backend.name());
    if (result.certificate.status != CertificateStatus::Certified) {
        errors << "certification failed: "
               << to_string(result.certificate.status) << " ("
               << result.certificate.reason << ")\n";
        return DemoRunResult{2, std::move(result)};
    }
    return DemoRunResult{0, std::move(result)};
}

}  // namespace lumenbound
