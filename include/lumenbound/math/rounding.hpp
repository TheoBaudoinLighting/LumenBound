#pragma once

namespace lumenbound::math {

[[nodiscard]] bool supports_certified_rounding() noexcept;

[[nodiscard]] double add_down(double left, double right);
[[nodiscard]] double add_up(double left, double right);
[[nodiscard]] double subtract_down(double left, double right);
[[nodiscard]] double subtract_up(double left, double right);
[[nodiscard]] double multiply_down(double left, double right);
[[nodiscard]] double multiply_up(double left, double right);
[[nodiscard]] double divide_down(double numerator, double denominator);
[[nodiscard]] double divide_up(double numerator, double denominator);

}  // namespace lumenbound::math
