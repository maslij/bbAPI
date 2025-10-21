cmake_minimum_required(VERSION 3.14)
project(tAPI VERSION 0.1.0 LANGUAGES CXX)

# Configuration options
# - Command line parameter --ai-server-url can be used to set the AI server URL
# - Default is http://localhost:8000

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define BOOST_BIND_GLOBAL_PLACEHOLDERS to suppress bind placeholders warning
add_definitions(-DBOOST_BIND_GLOBAL_PLACEHOLDERS)
add_definitions(-DTRITON_ENABLE_ZLIB)
add_definitions(-DTRITON_ENABLE_HTTP=1)
add_definitions(-DTRITON_ENABLE_GRPC=1)

# Options
option(BUILD_TESTS "Build tests" OFF)

# Set third-party installation path
set(THIRD_PARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party)

# Check if third-party dependencies exist
if(NOT EXISTS "${THIRD_PARTY_DIR}/protobuf/lib/libprotobuf.a")
    message(FATAL_ERROR 
        "Third-party dependencies not found!\n"
        "Please run the dependency installation script first:\n"
        "  ./scripts/install_deps.sh\n"
        "Then re-run cmake.")
endif()

# Find required system packages
find_package(OpenCV 4.2 REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem program_options)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PostgreSQL REQUIRED)

# Find hiredis (Redis C client) via pkg-config
pkg_check_modules(HIREDIS REQUIRED hiredis)

# Find GStreamer package
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_VIDEO REQUIRED gstreamer-video-1.0)

# Find Paho MQTT C and C++ libraries (optional)
find_package(PahoMqttCpp QUIET)
if(NOT PahoMqttCpp_FOUND)
    message(STATUS "Paho MQTT C++ library not found, will try to build without MQTT support")
endif()

find_package(nlohmann_json REQUIRED)

# Find or fetch Crow (C++ web framework)
include(FetchContent)
FetchContent_Declare(
    crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG master
)
FetchContent_MakeAvailable(crow)

# Patch the Crow socket_adaptors.h file to fix compatibility with newer ASIO
execute_process(
    COMMAND bash -c "mkdir -p ${CMAKE_BINARY_DIR}/_deps/crow-src/include/crow && \
        if [ -f ${CMAKE_BINARY_DIR}/_deps/crow-src/include/crow/socket_adaptors.h ]; then \
            sed -i 's|#define GET_IO_CONTEXT(s) ((s).get_io_service())|#define GET_IO_CONTEXT(s) ((s).get_executor().context())|g' \
            ${CMAKE_BINARY_DIR}/_deps/crow-src/include/crow/socket_adaptors.h; \
        fi"
    RESULT_VARIABLE PATCH_RESULT
)
if(NOT PATCH_RESULT EQUAL 0)
    message(WARNING "Failed to patch Crow socket_adaptors.h file")
endif()

# Set up protoc and gRPC plugin paths
set(PROTOC_EXECUTABLE "${THIRD_PARTY_DIR}/protobuf/bin/protoc")
set(GRPC_CPP_PLUGIN_EXECUTABLE "${THIRD_PARTY_DIR}/grpc/bin/grpc_cpp_plugin")

# Function to compile protobuf files
function(compile_proto_files)
    set(PROTO_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/grpc_service.proto
        ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/health.proto
        ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/model_config.proto
    )
    
    set(PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    file(MAKE_DIRECTORY ${PROTO_OUTPUT_DIR})
    
    set(GENERATED_SOURCES "")
    set(GENERATED_HEADERS "")
    
    # Collect all output files
    foreach(PROTO_FILE ${PROTO_FILES})
        get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
        
        set(PROTO_SRCS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.cc")
        set(PROTO_HDRS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.pb.h")
        set(GRPC_SRCS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.cc")
        set(GRPC_HDRS "${PROTO_OUTPUT_DIR}/${PROTO_NAME}.grpc.pb.h")
        
        list(APPEND GENERATED_SOURCES ${PROTO_SRCS} ${GRPC_SRCS})
        list(APPEND GENERATED_HEADERS ${PROTO_HDRS} ${GRPC_HDRS})
    endforeach()
    
    # Create a single script that compiles all protobuf files
    set(PROTOC_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/compile_all_protos.cmake")
    file(WRITE ${PROTOC_SCRIPT} "
        # Check that tools exist
        if(NOT EXISTS \"${PROTOC_EXECUTABLE}\")
            message(FATAL_ERROR \"protoc not found at ${PROTOC_EXECUTABLE}\")
        endif()
        if(NOT EXISTS \"${GRPC_CPP_PLUGIN_EXECUTABLE}\")
            message(FATAL_ERROR \"grpc_cpp_plugin not found at ${GRPC_CPP_PLUGIN_EXECUTABLE}\")
        endif()
        
        # Compile each proto file
        set(PROTO_FILES \"${PROTO_FILES}\")
        foreach(PROTO_FILE IN LISTS PROTO_FILES)
            message(STATUS \"Compiling protobuf file: \${PROTO_FILE}\")
            execute_process(
                COMMAND \"${PROTOC_EXECUTABLE}\"
                    --cpp_out=${PROTO_OUTPUT_DIR}
                    --grpc_out=${PROTO_OUTPUT_DIR}
                    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
                    --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protobuf
                    \"\${PROTO_FILE}\"
                RESULT_VARIABLE PROTOC_RESULT
                OUTPUT_VARIABLE PROTOC_OUTPUT
                ERROR_VARIABLE PROTOC_ERROR
            )
            if(NOT PROTOC_RESULT EQUAL 0)
                message(FATAL_ERROR \"protoc failed for \${PROTO_FILE}\\nOutput: \${PROTOC_OUTPUT}\\nError: \${PROTOC_ERROR}\")
            endif()
        endforeach()
    ")
    
    # Create a single custom command that generates all protobuf files
    add_custom_command(
        OUTPUT ${GENERATED_SOURCES} ${GENERATED_HEADERS}
        COMMAND ${CMAKE_COMMAND} -P ${PROTOC_SCRIPT}
        DEPENDS ${PROTO_FILES}
        COMMENT "Compiling all protobuf files"
        VERBATIM
    )
    
    # Create a custom target for generated files
    add_custom_target(generate_proto_files ALL
        DEPENDS ${GENERATED_SOURCES} ${GENERATED_HEADERS}
    )
    
    # Set variables in parent scope
    set(PROTO_GENERATED_SOURCES ${GENERATED_SOURCES} PARENT_SCOPE)
    set(PROTO_GENERATED_HEADERS ${GENERATED_HEADERS} PARENT_SCOPE)
    set(PROTO_OUTPUT_DIRECTORY ${PROTO_OUTPUT_DIR} PARENT_SCOPE)
endfunction()

# Compile protobuf files
compile_proto_files()

# ByteTracker sources (new implementation)
set(BYTETRACKER_SOURCES_V2
    src/bytetrack/BYTETracker.cpp
    src/bytetrack/kalmanFilter.cpp
    src/bytetrack/lapjv.cpp
    src/bytetrack/STrack.cpp    
    src/bytetrack/utils.cpp
)

# Utility sources
set(UTILITY_SOURCES
    src/utilities/logger.cpp
    src/config_manager.cpp
    src/global_config.cpp
    src/utils/common.cpp
    src/utils/shm_utils.cc
)

# Triton client sources
set(TRITON_CLIENT_SOURCES
    src/utils/http_client.cpp
    src/utils/grpc_client.cpp
    src/utils/simple_grpc_client.cpp
    src/utils/common.cpp
    src/utils/cencode.cpp
)

# Source component sources
set(SOURCE_COMPONENT_SOURCES
    src/components/source/gstreamer_source.cpp
)

# Processor component sources
set(PROCESSOR_COMPONENT_SOURCES
    src/components/processor/object_detector_processor.cpp
    src/components/processor/object_tracker_processor.cpp
    src/components/processor/line_zone_manager.cpp
    src/components/processor/object_classification_processor.cpp
    src/components/processor/age_gender_detection_processor.cpp
    src/components/processor/polygon_zone_manager.cpp
    src/components/processor/zone_timer.cpp
)

# Geometry sources
set(GEOMETRY_SOURCES
    src/geometry/line_zone.cpp
    src/geometry/polygon_zone.cpp
)

# Sink component sources
set(SINK_COMPONENT_SOURCES
    src/components/sink/file_sink.cpp
    src/components/sink/database_sink.cpp
)

# Billing system sources
set(BILLING_SOURCES
    src/billing/billing_config.cpp
    src/billing/billing_client.cpp
    src/billing/repository.cpp
    src/billing/license_validator.cpp
    src/billing/entitlement_manager.cpp
    src/billing/usage_tracker.cpp
    src/database/postgres_connection.cpp
    src/database/redis_cache.cpp
)

# Vision pipeline sources
set(VISION_PIPELINE_SOURCES
    src/license.cpp
    src/camera_license_manager.cpp
    src/component.cpp
    src/camera.cpp
    src/camera_manager.cpp
    src/component_factory.cpp
    ${SOURCE_COMPONENT_SOURCES}
    ${PROCESSOR_COMPONENT_SOURCES}
    ${SINK_COMPONENT_SOURCES}
    ${GEOMETRY_SOURCES}
    ${BILLING_SOURCES}
)

# Main executable
add_executable(tAPI
    src/main.cpp
    src/api.cpp
    src/background_task_manager.cpp
    ${BYTETRACKER_SOURCES_V2}
    ${VISION_PIPELINE_SOURCES}
    ${UTILITY_SOURCES}
    ${TRITON_CLIENT_SOURCES}
    ${PROTO_GENERATED_SOURCES}
    src/utils/url_utils.cpp
)

# Add dependency on protobuf generation
add_dependencies(tAPI generate_proto_files)

# Include directories
target_include_directories(tAPI PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/utils
    ${PROTO_OUTPUT_DIRECTORY}
    ${THIRD_PARTY_DIR}/protobuf/include
    ${THIRD_PARTY_DIR}/grpc/include
    ${THIRD_PARTY_DIR}/absl/include
    ${THIRD_PARTY_DIR}/triton-common/include
    ${OpenCV_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${EIGEN3_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_VIDEO_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${PostgreSQL_INCLUDE_DIRS}
    ${HIREDIS_INCLUDE_DIRS}
)

# Define third-party libraries in correct linking order
set(PROTOBUF_GRPC_LIBRARIES
    # gRPC++ library (must come first)
    ${THIRD_PARTY_DIR}/grpc/lib/libgrpc++.a
    ${THIRD_PARTY_DIR}/grpc/lib/libgrpc.a
    
    # Protobuf library
    ${THIRD_PARTY_DIR}/protobuf/lib/libprotobuf.a
    
    # gRPC support libraries
    ${THIRD_PARTY_DIR}/grpc/lib/libaddress_sorting.a
    ${THIRD_PARTY_DIR}/grpc/lib/libgpr.a
    ${THIRD_PARTY_DIR}/grpc/lib/libupb.a
    
    # c-ares
    ${THIRD_PARTY_DIR}/c-ares/lib/libcares.a
    
    # Abseil libraries (in dependency order)
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_status.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_statusor.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_strings.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_strings_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_str_format_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cord.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cord_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cordz_info.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cordz_handle.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cordz_functions.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_cordz_sample_token.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_synchronization.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_time.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_time_zone.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_civil_time.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_graphcycles_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_symbolize.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_stacktrace.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_debugging_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_demangle_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_malloc_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_base.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_spinlock_wait.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_throw_delegate.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_raw_logging_internal.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_log_severity.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_int128.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_hash.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_city.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_low_level_hash.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_raw_hash_set.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_hashtablez_sampler.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_exponential_biased.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_bad_optional_access.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_bad_variant_access.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_bad_any_cast_impl.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_strerror.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_examine_stack.a
    
    # Random libraries
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_distributions.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_distribution_test_util.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_platform.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_pool_urbg.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_randen.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_randen_hwaes.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_randen_hwaes_impl.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_randen_slow.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_internal_seed_material.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_seed_gen_exception.a
    ${THIRD_PARTY_DIR}/grpc/lib/libabsl_random_seed_sequences.a
    
    # RE2
    ${THIRD_PARTY_DIR}/grpc/lib/libre2.a
)

# System libraries
set(SYSTEM_LIBRARIES
    ${OpenCV_LIBS}
    ${Boost_LIBRARIES}
    ${CURL_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_VIDEO_LIBRARIES}
    ${SQLite3_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
    ${HIREDIS_LIBRARIES}
    Crow::Crow
    nlohmann_json::nlohmann_json
    uuid
    pthread
    CURL::libcurl
    rt
    ZLIB::ZLIB
    ssl
    crypto
    z
)

# Link libraries
target_link_libraries(tAPI PRIVATE 
    ${SYSTEM_LIBRARIES}
    ${PROTOBUF_GRPC_LIBRARIES}
)

# Install
install(TARGETS tAPI
    RUNTIME DESTINATION bin
)

# Install generated protobuf headers for development
install(FILES ${PROTO_GENERATED_HEADERS}
    DESTINATION include/generated
)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif() 