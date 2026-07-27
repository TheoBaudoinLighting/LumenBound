if(NOT DEFINED LUMENBOUND_PROGRAM OR
   NOT DEFINED LUMENBOUND_OUTPUT_DIRECTORY)
    message(FATAL_ERROR "Expected-failure test arguments are missing")
endif()

execute_process(
    COMMAND
        "${LUMENBOUND_PROGRAM}"
        demo
        certified-patches
        --output
        "${LUMENBOUND_OUTPUT_DIRECTORY}"
        --peak
        1.0
        --target-psnr
        1000
        --max-iterations
        0
    RESULT_VARIABLE process_result
    OUTPUT_VARIABLE process_output
    ERROR_VARIABLE process_error
)

if(process_result EQUAL 0)
    message(FATAL_ERROR
        "The insufficient-iteration demonstration unexpectedly succeeded")
endif()

set(combined_output "${process_output}\n${process_error}")
if(NOT combined_output MATCHES "proof_status: Certified")
    message(FATAL_ERROR
        "The demonstration did not preserve its certified proof")
endif()

if(NOT combined_output MATCHES "target_status: IterationLimit")
    message(FATAL_ERROR
        "The demonstration did not report target iteration exhaustion")
endif()
