cmake_minimum_required(VERSION 3.20)
project(raytracer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RT_BUILD_TESTS      "Build unit and integration tests" ON)
option(RT_BUILD_BENCHMARKS "Build benchmarks"                  ON)
option(RT_SANITIZE         "Enable ASan+UBSan (address,undefined)" OFF)
option(RT_TSAN             "Enable ThreadSanitizer"            OFF)
option(RT_NATIVE           "Compile with -march=native"        OFF)
option(RT_WASM_THREADS     "WebAssembly build uses pthreads"   ON)

if(EMSCRIPTEN)
  # Browser build: only the core library and the WebAssembly entry point.
  set(RT_BUILD_TESTS OFF)
  set(RT_BUILD_BENCHMARKS OFF)
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Warnings / sanitizers are applied through an interface target so that the
# vendored third-party code is not compiled with -Werror-style strictness.
add_library(rt_options INTERFACE)
target_compile_options(rt_options INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion>)
if(RT_NATIVE)
  target_compile_options(rt_options INTERFACE -march=native)
endif()
if(RT_SANITIZE)
  target_compile_options(rt_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  target_link_options(rt_options INTERFACE -fsanitize=address,undefined)
endif()
if(RT_TSAN)
  target_compile_options(rt_options INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(rt_options INTERFACE -fsanitize=thread)
endif()
if(EMSCRIPTEN)
  # Native wasm exceptions are zero-cost on the non-throwing path (the scene
  # loader reports errors by throwing); SIMD lets clang vectorise Vec3 maths.
  target_compile_options(rt_options INTERFACE -fwasm-exceptions -msimd128)
  target_link_options(rt_options INTERFACE -fwasm-exceptions)
  if(RT_WASM_THREADS)
    target_compile_options(rt_options INTERFACE -pthread)
    target_link_options(rt_options INTERFACE -pthread)
  endif()
endif()

# ---------------------------------------------------------------------------
# Core rendering library: no UI, no networking, no SDL.
add_library(rtcore
  src/io/json.cpp
  src/io/scene_io.cpp
  src/io/image_io.cpp
  src/io/obj_loader.cpp
  src/geometry/mesh.cpp
  src/geometry/bvh.cpp
  src/core/scene.cpp
  src/materials/material.cpp
  src/rendering/integrator.cpp
  src/rendering/renderer.cpp
  src/rendering/tonemap.cpp
)
target_include_directories(rtcore
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(rtcore PUBLIC Threads::Threads rt_options)

# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
  # WebAssembly renderer for the browser editor (see apps/rt_wasm.cpp and
  # tools/build_wasm.sh). Output: rt-mt.mjs/.wasm or rt-st.mjs/.wasm.
  if(RT_WASM_THREADS)
    set(RT_WASM_VARIANT mt)
  else()
    set(RT_WASM_VARIANT st)
  endif()
  add_executable(rt_wasm apps/rt_wasm.cpp)
  target_link_libraries(rt_wasm PRIVATE rtcore)
  # EM_ASM blocks refer to their arguments as $0, $1, ...
  set_source_files_properties(apps/rt_wasm.cpp PROPERTIES COMPILE_OPTIONS -Wno-dollar-in-identifier-extension)
  set_target_properties(rt_wasm PROPERTIES OUTPUT_NAME rt-${RT_WASM_VARIANT} SUFFIX ".mjs")
  target_link_options(rt_wasm PRIVATE
    -sMODULARIZE -sEXPORT_ES6 -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=2GB -sSTACK_SIZE=1MB
    -sEXPORTED_FUNCTIONS=_malloc,_free
    -sEXPORTED_RUNTIME_METHODS=stringToNewUTF8,UTF8ToString
    -Wno-pthreads-mem-growth)  # JS touches the heap once per frame, so slower JS heap access is harmless
  if(RT_WASM_THREADS)
    target_link_options(rt_wasm PRIVATE
      -sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency -sDEFAULT_PTHREAD_STACK_SIZE=1MB)
  endif()
else()
  # Command-line renderer
  add_executable(rt apps/rt_cli.cpp)
  target_link_libraries(rt PRIVATE rtcore)
endif()

# ---------------------------------------------------------------------------
if(RT_BUILD_TESTS)
  enable_testing()
  add_library(catch2_main STATIC third_party/catch2/catch_amalgamated.cpp)
  target_include_directories(catch2_main PUBLIC third_party)
  target_compile_features(catch2_main PUBLIC cxx_std_20)

  add_executable(rt_tests
    tests/test_math.cpp
    tests/test_geometry.cpp
    tests/test_materials.cpp
    tests/test_bvh.cpp
    tests/test_scene_io.cpp
    tests/test_render.cpp
  )
  target_link_libraries(rt_tests PRIVATE rtcore catch2_main)
  target_compile_definitions(rt_tests PRIVATE RT_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
  add_test(NAME rt_tests COMMAND rt_tests)

  # End-to-end: the CLI renders a shipped scene and writes a valid PNG.
  add_test(NAME cli_renders_scene
    COMMAND rt ${CMAKE_CURRENT_SOURCE_DIR}/assets/scenes/first_light.json
            -o ${CMAKE_CURRENT_BINARY_DIR}/cli_test.png --width 64 --height 48 --spp 4)
endif()

if(RT_BUILD_BENCHMARKS)
  add_executable(bench_bvh benchmarks/bench_bvh.cpp)
  target_link_libraries(bench_bvh PRIVATE rtcore)
  add_executable(bench_threads benchmarks/bench_threads.cpp)
  target_link_libraries(bench_threads PRIVATE rtcore)
endif()
