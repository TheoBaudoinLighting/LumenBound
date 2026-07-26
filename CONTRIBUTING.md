# Contributing to LumenBound

LumenBound is currently a research prototype for finite-dimensional positive
transport systems. Contributions should preserve the distinction between a
candidate estimate and a checked certificate.

## Development setup

A C++23 compiler and CMake 3.25 or newer are required. Configure, build, and
test the development preset with:

```text
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

The configure presets leave generator selection to CMake. Linux continuous
integration exercises Unix Makefiles with both GCC and Clang.

## Change requirements

- Keep the mathematical core independent from command-line and file-output
  code.
- Preserve fixed iteration order and deterministic tie-breaking.
- Use outward-rounded arithmetic for every value that contributes to a
  certificate.
- Reject invalid input explicitly; do not introduce silent fallbacks.
- Add focused tests for public APIs, numerical edge cases, and failure modes.
- Record reproducible commands for numerical or performance measurements.
- Update the relevant mathematical and limitation documentation when a
  guarantee or assumption changes.

Run the complete development test suite before submitting a change. Generated
demonstration output belongs under `out/` and must not be committed.

## Public changes

Use concise technical prose in code comments, documentation, and commit
messages. Keep commits focused and do not include local configuration, build
artifacts, or unrelated files.

By contributing, you agree that your contribution is licensed under the
Apache License 2.0.
