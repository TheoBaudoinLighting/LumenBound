cmake_minimum_required(VERSION 3.25)

project(
    LumenBound
    VERSION 0.3.0
    DESCRIPTION
        "Finite-dimensional deterministic light-transport reference core"
    LANGUAGES CXX
)

include(CTest)

option(
    LUMENBOUND_WARNINGS_AS_ERRORS
    "Treat warnings from project targets as errors"
    ON
)

function(lumenbound_configure_project_target target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    set_target_properties(
        ${target_name}
        PROPERTIES
            CXX_EXTENSIONS OFF
            CXX_STANDARD_REQUIRED ON
    )

    if(
        CMAKE_CXX_COMPILER_ID STREQUAL "MSVC"
        OR (
            CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
            AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
        )
    )
        target_compile_options(
            ${target_name}
            PRIVATE
                /W4
                /permissive-
                /fp:strict
        )
        if(LUMENBOUND_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    elseif(
        CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
    )
        target_compile_options(
            ${target_name}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -frounding-math
                -fno-fast-math
                -ffp-contract=off
        )
        if(LUMENBOUND_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    else()
        message(FATAL_ERROR
            "The certified core has no floating-point policy for compiler "
            "'${CMAKE_CXX_COMPILER_ID}' with frontend "
            "'${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}'.")
    endif()
endfunction()

function(lumenbound_disable_interprocedural_optimization target_name)
    set_target_properties(
        ${target_name}
        PROPERTIES
            INTERPROCEDURAL_OPTIMIZATION FALSE
    )

    set(
        lumenbound_configurations
        Debug
        Release
        RelWithDebInfo
        MinSizeRel
        ${CMAKE_CONFIGURATION_TYPES}
        ${CMAKE_BUILD_TYPE}
    )
    list(REMOVE_DUPLICATES lumenbound_configurations)
    foreach(configuration IN LISTS lumenbound_configurations)
        if(NOT configuration STREQUAL "")
            string(TOUPPER "${configuration}" configuration_upper)
            set_target_properties(
                ${target_name}
                PROPERTIES
                    "INTERPROCEDURAL_OPTIMIZATION_${configuration_upper}"
                        FALSE
            )
        endif()
    endforeach()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /GL-)
    elseif(
        CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
    )
        target_compile_options(${target_name} PRIVATE /clang:-fno-lto)
    else()
        target_compile_options(${target_name} PRIVATE -fno-lto)
    endif()
endfunction()

set(
    LUMENBOUND_CORE_SOURCES
    src/interval.cpp
    src/dense_vector.cpp
    src/dense_matrix.cpp
    src/transport_system.cpp
    src/diffuse_patch_assembly.cpp
    src/candidate_solver.cpp
    src/projection.cpp
    src/image_metrics.cpp
    src/problem_digest.cpp
    src/certifier.cpp
    src/output.cpp
    src/demo.cpp
    src/cornell_box_demo.cpp
)

add_library(lumenbound_core STATIC ${LUMENBOUND_CORE_SOURCES})
add_library(lumenbound::core ALIAS lumenbound_core)
lumenbound_configure_project_target(lumenbound_core)
lumenbound_disable_interprocedural_optimization(lumenbound_core)
target_compile_definitions(
    lumenbound_core
    PRIVATE
        LUMENBOUND_SOLVER_VERSION="${PROJECT_VERSION}"
)
target_include_directories(
    lumenbound_core
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
)

set(LUMENBOUND_APP_SOURCE "${PROJECT_SOURCE_DIR}/apps/lumenbound/main.cpp")
if(NOT EXISTS "${LUMENBOUND_APP_SOURCE}")
    message(FATAL_ERROR
        "The command-line entry point is missing: "
        "${LUMENBOUND_APP_SOURCE}")
endif()

add_executable(lumenbound "${LUMENBOUND_APP_SOURCE}")
lumenbound_configure_project_target(lumenbound)
target_link_libraries(lumenbound PRIVATE lumenbound::core)

# Keep the documented command path stable for single- and multi-config
# generators.
set_target_properties(
    lumenbound
    PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(
        lumenbound
        PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                "${CMAKE_BINARY_DIR}"
    )
endforeach()

if(BUILD_TESTING)
    add_executable(lumenbound_tests tests/test_main.cpp)
    lumenbound_configure_project_target(lumenbound_tests)
    target_link_libraries(lumenbound_tests PRIVATE lumenbound::core)
    set_target_properties(
        lumenbound_tests
        PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${configuration}" configuration_upper)
        set_target_properties(
            lumenbound_tests
            PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
                    "${CMAKE_BINARY_DIR}"
        )
    endforeach()

    add_test(NAME lumenbound.unit-and-integration COMMAND lumenbound_tests)
    set_tests_properties(
        lumenbound.unit-and-integration
        PROPERTIES
            TIMEOUT 60
    )

    add_test(
        NAME lumenbound.demo.certified-patches
        COMMAND
            lumenbound
            demo
            certified-patches
            --output
            "${CMAKE_BINARY_DIR}/test-output/certified-patches"
            --peak
            1.0
            --target-psnr
            80
    )
    set_tests_properties(
        lumenbound.demo.certified-patches
        PROPERTIES
            TIMEOUT 60
    )

    add_test(
        NAME lumenbound.demo.cornell-box
        COMMAND
            lumenbound
            demo
            cornell-box
            --output
            "${CMAKE_BINARY_DIR}/test-output/cornell-box"
            --width
            32
            --height
            32
            --preview-exposure
            1.0
            --peak
            4.0
            --target-psnr
            80
    )
    set_tests_properties(
        lumenbound.demo.cornell-box
        PROPERTIES
            TIMEOUT 60
    )

    add_test(
        NAME lumenbound.demo.iteration-limit
        COMMAND
            "${CMAKE_COMMAND}"
            "-DLUMENBOUND_PROGRAM=$<TARGET_FILE:lumenbound>"
            "-DLUMENBOUND_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/test-output/iteration-limit"
            -P
            "${PROJECT_SOURCE_DIR}/tests/expect_iteration_limit.cmake"
    )
    set_tests_properties(
        lumenbound.demo.iteration-limit
        PROPERTIES
            TIMEOUT 60
    )
endif()
