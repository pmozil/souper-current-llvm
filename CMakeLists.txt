cmake_minimum_required(VERSION 3.8)
project(Souper)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_MACOSX_RPATH NEW)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()


# ============================================================================
# LLVM
# ============================================================================

# Use system llvm-config by default.
# Can be overridden with:
#   -DLLVM_CONFIG_EXECUTABLE=/path/to/llvm-config

if(NOT LLVM_CONFIG_EXECUTABLE)
  find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config
    REQUIRED
  )
endif()

if(NOT EXISTS "${LLVM_CONFIG_EXECUTABLE}")
  message(FATAL_ERROR
    "llvm-config could not be found: ${LLVM_CONFIG_EXECUTABLE}"
  )
endif()

message(STATUS "LLVM llvm-config: ${LLVM_CONFIG_EXECUTABLE}")


# ============================================================================
# ZSTD
# ============================================================================

find_library(ZSTD_LIBRARY
  NAMES zstd
  REQUIRED
)

get_filename_component(
  ZSTD_LIBRARY_DIR
  "${ZSTD_LIBRARY}"
  DIRECTORY
)

message(STATUS "zstd library: ${ZSTD_LIBRARY}")


# ============================================================================
# LLVM configuration
# ============================================================================

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --includedir
  OUTPUT_VARIABLE LLVM_INCLUDEDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --cppflags
  OUTPUT_VARIABLE LLVM_CXXFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(
  LLVM_CXXFLAGS
  "${LLVM_CXXFLAGS} -fno-exceptions -fno-rtti -Wno-deprecated-enum-enum-conversion"
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --libs
  OUTPUT_VARIABLE LLVM_LIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --system-libs
  OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(LLVM_LIBS "${LLVM_LIBS} ${LLVM_SYSTEM_LIBS}")
string(STRIP "${LLVM_LIBS}" LLVM_LIBS)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --ldflags
  OUTPUT_VARIABLE LLVM_LDFLAGS
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(PASS_LDFLAGS "${LLVM_LDFLAGS}")

# Keep zstd available to the LLVM link step.
set(LLVM_LDFLAGS "${LLVM_LDFLAGS} -L${ZSTD_LIBRARY_DIR}")

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --bindir
  OUTPUT_VARIABLE LLVM_BINDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
  COMMAND ${LLVM_CONFIG_EXECUTABLE} --obj-root
  OUTPUT_VARIABLE LLVM_BUILD
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "LLVM include dir: ${LLVM_INCLUDEDIR}")
message(STATUS "LLVM binary dir:  ${LLVM_BINDIR}")
message(STATUS "LLVM object dir:  ${LLVM_BUILD}")


# ============================================================================
# Alive2
# ============================================================================

# Alive2 is not assumed to be installed by build_deps.sh anymore.
#
# Configure with:
#
#   -DALIVE2_ROOT=/path/to/alive2
#
# Expected layout:
#
#   ${ALIVE2_ROOT}/
#       ... headers ...
#       build/
#           libir.a
#           libsmt.a
#           libtools.a
#           libutil.a
#
# If your Alive2 build uses a different layout, change ALIVE2_BUILDDIR below.

set(ALIVE2_ROOT "" CACHE PATH
  "Root directory of the Alive2 source/build installation"
)

if(NOT ALIVE2_ROOT)
  message(FATAL_ERROR
    "Alive2 is required. "
    "Configure with -DALIVE2_ROOT=/path/to/alive2"
  )
endif()

set(ALIVE_SRCDIR "${ALIVE2_ROOT}")
set(ALIVE_BUILDDIR "${ALIVE2_ROOT}/build")

message(STATUS "Alive2 root:    ${ALIVE_SRCDIR}")
message(STATUS "Alive2 build:   ${ALIVE_BUILDDIR}")


find_library(ALIVE_IR
  NAMES ir
  PATHS "${ALIVE_BUILDDIR}"
  NO_DEFAULT_PATH
  REQUIRED
)

find_library(ALIVE_SMT
  NAMES smt
  PATHS "${ALIVE_BUILDDIR}"
  NO_DEFAULT_PATH
  REQUIRED
)

find_library(ALIVE_TOOLS
  NAMES tools
  PATHS "${ALIVE_BUILDDIR}"
  NO_DEFAULT_PATH
  REQUIRED
)

find_library(ALIVE_UTIL
  NAMES util
  PATHS "${ALIVE_BUILDDIR}"
  NO_DEFAULT_PATH
  REQUIRED
)

message(STATUS "Alive2 IR:      ${ALIVE_IR}")
message(STATUS "Alive2 SMT:     ${ALIVE_SMT}")
message(STATUS "Alive2 TOOLS:   ${ALIVE_TOOLS}")
message(STATUS "Alive2 UTIL:    ${ALIVE_UTIL}")

set(ALIVE_LIBRARY
  ${ALIVE_IR}
  ${ALIVE_SMT}
  ${ALIVE_TOOLS}
  ${ALIVE_UTIL}
)


# ============================================================================
# General include paths
# ============================================================================

get_filename_component(ALIVE_PARENTDIR "${ALIVE_SRCDIR}" DIRECTORY)

include_directories(
  "${CMAKE_SOURCE_DIR}/third_party"
  "${ALIVE_SRCDIR}"
  "${ALIVE_PARENTDIR}"
)


# ============================================================================
# hiredis
# ============================================================================

find_path(HIREDIS_INCLUDE_DIR
  NAMES hiredis.h
  PATH_SUFFIXES hiredis
  REQUIRED
)

find_library(HIREDIS_LIBRARY
  NAMES hiredis
  REQUIRED
)

message(STATUS "hiredis include: ${HIREDIS_INCLUDE_DIR}")
message(STATUS "hiredis library: ${HIREDIS_LIBRARY}")

include_directories("${HIREDIS_INCLUDE_DIR}")


# ============================================================================
# Z3
# ============================================================================

find_program(Z3
  NAMES z3
  REQUIRED
)

find_path(Z3_INCLUDE_DIR
  NAMES z3++.h z3.h
  REQUIRED
)

find_library(Z3_LIBRARY
  NAMES z3
  REQUIRED
)

message(STATUS "Z3 executable: ${Z3}")
message(STATUS "Z3 include:    ${Z3_INCLUDE_DIR}")
message(STATUS "Z3 library:    ${Z3_LIBRARY}")

include_directories("${Z3_INCLUDE_DIR}")


# ============================================================================
# GoogleTest
# ============================================================================

set(GTEST_CXXFLAGS "-DGTEST_HAS_RTTI=0")

find_path(GTEST_INCLUDEDIR
  NAMES gtest/gtest.h
  REQUIRED
)

find_library(GTEST_LIBRARY
  NAMES gtest
  REQUIRED
)

find_library(GTEST_MAIN_LIBRARY
  NAMES gtest_main
  REQUIRED
)

set(GTEST_LIBS
  "${GTEST_MAIN_LIBRARY}"
  "${GTEST_LIBRARY}"
)

message(STATUS "GTest include:  ${GTEST_INCLUDEDIR}")
message(STATUS "GTest library:  ${GTEST_LIBRARY}")
message(STATUS "GTest main:     ${GTEST_MAIN_LIBRARY}")


# ============================================================================
# C++ configuration
# ============================================================================

set(LLVM_CXXFLAGS "${LLVM_CXXFLAGS} -std=c++20")

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++20")
  set(LLVM_CXXFLAGS "${LLVM_CXXFLAGS} -fvisibility-inlines-hidden")
  set(PASS_LDFLAGS "-Wl,-undefined,dynamic_lookup")
endif()


# ============================================================================
# KLEE
# ============================================================================

set(KLEE_ROOT
  "${CMAKE_SOURCE_DIR}/third_party/klee"
  CACHE PATH
  "KLEE source tree used by Souper"
)

if(NOT EXISTS "${KLEE_ROOT}/include/klee/Expr.h")
  message(FATAL_ERROR
    "KLEE headers not found.\n"
    "Expected: ${KLEE_ROOT}/include/klee/Expr.h"
  )
endif()

if(NOT EXISTS "${KLEE_ROOT}/lib/Expr/ArrayCache.cpp")
  message(FATAL_ERROR
    "KLEE Expr sources not found.\n"
    "Expected: ${KLEE_ROOT}/lib/Expr/ArrayCache.cpp"
  )
endif()

message(STATUS "KLEE root: ${KLEE_ROOT}")

set(KLEE_INCLUDE_DIR
  "${KLEE_ROOT}/include"
)

set(KLEE_EXPR_FILES
  "${KLEE_ROOT}/lib/Expr/ArrayCache.cpp"
  "${KLEE_ROOT}/lib/Expr/Constraints.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprBuilder.cpp"
  "${KLEE_ROOT}/lib/Expr/Expr.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprEvaluator.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprPPrinter.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprSMTLIBPrinter.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprUtil.cpp"
  "${KLEE_ROOT}/lib/Expr/ExprVisitor.cpp"
  "${KLEE_ROOT}/lib/Expr/Lexer.cpp"
  "${KLEE_ROOT}/lib/Expr/Parser.cpp"
  "${KLEE_ROOT}/lib/Expr/Updates.cpp"
)

add_library(kleeExpr STATIC ${KLEE_EXPR_FILES})

target_include_directories(kleeExpr
  PUBLIC
    "${KLEE_ROOT}/include"
)

include_directories(
  include
  "${KLEE_INCLUDE_DIR}"
  "${CMAKE_BINARY_DIR}/include"
)


# ============================================================================
# Souper Extractor
# ============================================================================

set(SOUPER_EXTRACTOR_FILES
  lib/Extractor/Candidates.cpp
  lib/Extractor/ExprBuilder.cpp
  lib/Extractor/KLEEBuilder.cpp
  lib/Extractor/Solver.cpp
  include/souper/Extractor/Candidates.h
  include/souper/Extractor/ExprBuilder.h
  include/souper/Extractor/Solver.h
)

add_library(souperExtractor STATIC
  ${SOUPER_EXTRACTOR_FILES}
)


# ============================================================================
# Souper KVStore
# ============================================================================

set(SOUPER_KVSTORE_FILES
  lib/KVStore/KVStore.cpp
  include/souper/KVStore/KVStore.h
)

add_library(souperKVStore STATIC
  ${SOUPER_KVSTORE_FILES}
)


# ============================================================================
# Souper Infer
# ============================================================================

set(SOUPER_INFER_FILES
  lib/Infer/InstSynthesis.cpp
  include/souper/Infer/InstSynthesis.h
  lib/Infer/ConstantSynthesis.cpp
  include/souper/Infer/ConstantSynthesis.h
  lib/Infer/EnumerativeSynthesis.cpp
  include/souper/Infer/EnumerativeSynthesis.h
  lib/Infer/AliveDriver.cpp
  include/souper/Infer/AliveDriver.h
  lib/Infer/Pruning.cpp
  include/souper/Infer/Pruning.h
  lib/Infer/Interpreter.cpp
  lib/Infer/AbstractInterpreter.cpp
  include/souper/Infer/Interpreter.h
  lib/Infer/Preconditions.cpp
  include/souper/Infer/Preconditions.h
)

add_library(souperInfer STATIC
  ${SOUPER_INFER_FILES}
)


# ============================================================================
# Souper Inst
# ============================================================================

set(SOUPER_INST_FILES
  lib/Inst/Inst.cpp
  include/souper/Inst/Inst.h
  include/souper/Inst/InstGraph.h
)

add_library(souperInst STATIC
  ${SOUPER_INST_FILES}
)


# ============================================================================
# Souper Parser
# ============================================================================

set(SOUPER_PARSER_FILES
  lib/Parser/Parser.cpp
  include/souper/Parser/Parser.h
)

add_library(souperParser STATIC
  ${SOUPER_PARSER_FILES}
)


# ============================================================================
# Souper SMTLIB2
# ============================================================================

set(SOUPER_SMTLIB2_FILES
  lib/SMTLIB2/Solver.cpp
  include/souper/SMTLIB2/Solver.h
)

add_library(souperSMTLIB2 STATIC
  ${SOUPER_SMTLIB2_FILES}
)


# ============================================================================
# Souper Tool
# ============================================================================

set(SOUPER_TOOL_FILES
  lib/Tool/CandidateMapUtils.cpp
  include/souper/Tool/CandidateMapUtils.h
  include/souper/Tool/GetSolver.h.in
)

add_library(souperTool STATIC
  ${SOUPER_TOOL_FILES}
)


# ============================================================================
# Souper Codegen
# ============================================================================

set(SOUPER_CODEGEN_FILES
  lib/Codegen/Codegen.cpp
  include/souper/Codegen/Codegen.h
)

add_library(souperCodegen STATIC
  ${SOUPER_CODEGEN_FILES}
)


# ============================================================================
# Combined Souper sources
# ============================================================================

set(SOUPER_SOURCES
  ${SOUPER_EXTRACTOR_FILES}
  ${SOUPER_INST_FILES}
  ${SOUPER_KVSTORE_FILES}
  ${SOUPER_PARSER_FILES}
  ${SOUPER_SMTLIB2_FILES}
  ${SOUPER_TOOL_FILES}
  ${SOUPER_INFER_FILES}
  ${SOUPER_CODEGEN_FILES}
)


# ============================================================================
# LLVM passes
# ============================================================================

add_library(souperPass SHARED
  ${SOUPER_SOURCES}
  lib/Pass/Pass.cpp
)

target_link_libraries(souperPass
  PRIVATE
    ${LLVM_LIBS}
    ${Z3_LIBRARY}
    ${HIREDIS_LIBRARY}
    ${ALIVE_LIBRARY}
    ${ZSTD_LIBRARY}
    kleeExpr
)

add_library(souperPassProfileAll SHARED
  ${SOUPER_SOURCES}
  lib/Pass/Pass.cpp
)

target_compile_definitions(
  souperPassProfileAll
  PRIVATE
  DYNAMIC_PROFILE_ALL=1
)

target_link_libraries(souperPassProfileAll
  PRIVATE
    ${LLVM_LIBS}
    ${Z3_LIBRARY}
    ${HIREDIS_LIBRARY}
    ${ALIVE_LIBRARY}
    ${ZSTD_LIBRARY}
    kleeExpr
)


# ============================================================================
# Executables
# ============================================================================

add_executable(souper
  tools/souper.cpp
)

add_executable(internal-solver-test
  tools/internal-solver-test.cpp
)

add_executable(lexer-test
  tools/lexer-test.cpp
)

add_executable(parser-test
  tools/parser-test.cpp
)

add_executable(souper-check
  tools/souper-check.cpp
)

add_executable(souper-interpret
  tools/souper-interpret.cpp
)

add_executable(count-insts
  tools/count-insts.cpp
)

add_executable(souper2llvm
  tools/souper2llvm.cpp
)


# ============================================================================
# Unit tests
# ============================================================================

# add_executable(extractor_tests
#   unittests/Extractor/ExtractorTests.cpp
# )
#
# add_executable(inst_tests
#   unittests/Inst/InstTests.cpp
# )
#
# add_executable(parser_tests
#   unittests/Parser/ParserTests.cpp
# )
#
# add_executable(codegen_tests
#   unittests/Codegen/CodegenTests.cpp
# )
#
# add_executable(interpreter_tests
#   unittests/Interpreter/InterpreterInfra.cpp
#   unittests/Interpreter/InterpreterTests.cpp
# )
#
# add_executable(bulk_tests
#   unittests/Interpreter/InterpreterInfra.cpp
#   utils/gen-xfer-funcs/BulkTests.cpp
#   utils/gen-xfer-funcs/Verification.cpp
# )
#
# target_include_directories(
#   bulk_tests
#   PUBLIC
#   "${CMAKE_SOURCE_DIR}/unittests/Interpreter"
# )


# ============================================================================
# Configure generated test/helper files
# ============================================================================

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/gen-xfer-funcs/run_n.pl.in
  ${CMAKE_BINARY_DIR}/utils/gen-xfer-funcs/run_n.pl
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/gen-xfer-funcs/gen_xfer.pl.in
  ${CMAKE_BINARY_DIR}/utils/gen-xfer-funcs/gen_xfer.pl
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/gen-xfer-funcs/compile.sh.in
  ${CMAKE_BINARY_DIR}/utils/gen-xfer-funcs/compile.sh
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/gen-xfer-funcs/funcs.h
  ${CMAKE_BINARY_DIR}/utils/gen-xfer-funcs/funcs.h
  COPYONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/gen-xfer-funcs/look_n.pl
  ${CMAKE_BINARY_DIR}/utils/gen-xfer-funcs/look_n.pl
  COPYONLY
)


# ============================================================================
# Compiler flags / include directories
# ============================================================================

foreach(target
    souper
    internal-solver-test
    lexer-test
    parser-test
    souper-check
    count-insts
    souper2llvm
    souper-interpret
    souperExtractor
    souperInfer
    souperInst
    souperKVStore
    souperParser
    souperSMTLIB2
    souperTool
    souperPass
    souperPassProfileAll
    kleeExpr
    souperCodegen
)
  set_target_properties(
    ${target}
    PROPERTIES
    COMPILE_FLAGS "${LLVM_CXXFLAGS}"
  )

  target_include_directories(
    ${target}
    PRIVATE
    "${LLVM_INCLUDEDIR}"
  )
endforeach()


# foreach(target
#     extractor_tests
#     inst_tests
#     parser_tests
#     interpreter_tests
#     bulk_tests
#     codegen_tests
# )
#   set_target_properties(
#     ${target}
#     PROPERTIES
#     COMPILE_FLAGS "${GTEST_CXXFLAGS} ${LLVM_CXXFLAGS}"
#   )
#
#   target_include_directories(
#     ${target}
#     PRIVATE
#     "${LLVM_INCLUDEDIR}"
#     "${GTEST_INCLUDEDIR}"
#   )
# endforeach()


# ============================================================================
# Static libraries
# ============================================================================

target_link_libraries(
  kleeExpr
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
)

target_link_libraries(
  souperExtractor
  souperParser
  souperKVStore
  souperInfer
  souperInst
  kleeExpr
  souperCodegen
)

target_link_libraries(
  souperInfer
  souperExtractor
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
  ${Z3_LIBRARY}
)

target_link_libraries(
  souperInst
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
)

target_link_libraries(
  souperKVStore
  ${HIREDIS_LIBRARY}
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
)

target_link_libraries(
  souperParser
  souperInst
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
  ${ALIVE_LIBRARY}
)

target_link_libraries(
  souperSMTLIB2
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
)

target_link_libraries(
  souperTool
  souperExtractor
  souperSMTLIB2
)

target_link_libraries(
  souperCodegen
  ${LLVM_LIBS}
  ${LLVM_LDFLAGS}
)


# ============================================================================
# Dynamic libraries
# ============================================================================

target_link_libraries(
  souperCodegen
  ${PASS_LDFLAGS}
)

# target_link_libraries(
#   souperPass
#   ${PASS_LDFLAGS}
#   ${HIREDIS_LIBRARY}
#   ${ALIVE_LIBRARY}
#   ${Z3_LIBRARY}
# )
#
# target_link_libraries(
#   souperPassProfileAll
#   ${PASS_LDFLAGS}
#   ${HIREDIS_LIBRARY}
#   ${ALIVE_LIBRARY}
#   ${Z3_LIBRARY}
# )


# ============================================================================
# Executables
# ============================================================================

target_link_libraries(
  souper
  souperExtractor
  souperKVStore
  souperParser
  souperSMTLIB2
  souperTool
  kleeExpr
  ${HIREDIS_LIBRARY}
  ${ALIVE_LIBRARY}
  ${Z3_LIBRARY}
)

target_link_libraries(
  internal-solver-test
  souperSMTLIB2
)

target_link_libraries(
  lexer-test
  souperParser
)

target_link_libraries(
  parser-test
  souperParser
)

target_link_libraries(
  souper-check
  souperTool
  souperExtractor
  souperKVStore
  souperSMTLIB2
  souperParser
  ${HIREDIS_LIBRARY}
  ${ALIVE_LIBRARY}
  ${Z3_LIBRARY}
)

target_link_libraries(
  souper-interpret
  souperTool
  souperExtractor
  souperKVStore
  souperSMTLIB2
  souperParser
  ${HIREDIS_LIBRARY}
  ${ALIVE_LIBRARY}
  ${Z3_LIBRARY}
)

target_link_libraries(
  count-insts
  souperParser
)

target_link_libraries(
  souper2llvm
  souperParser
  souperCodegen
)


# ============================================================================
# Unit test linking
# ============================================================================

# target_link_libraries(
#   extractor_tests
#   souperExtractor
#   souperParser
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
# )
#
# target_link_libraries(
#   inst_tests
#   souperInfer
#   souperPass
#   souperInst
#   souperExtractor
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
# )
#
# target_link_libraries(
#   parser_tests
#   souperParser
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
# )
#
# target_link_libraries(
#   codegen_tests
#   souperCodegen
#   souperInst
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
# )
#
# target_link_libraries(
#   interpreter_tests
#   souperInfer
#   souperInst
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
# )
#
# target_link_libraries(
#   bulk_tests
#   souperInfer
#   souperInst
#   ${GTEST_LIBS}
#   ${ALIVE_LIBRARY}
#   ${Z3_LIBRARY}
# )


# ============================================================================
# Tests
# ============================================================================

set(
  TEST_SYNTHESIS
  "ON"
  CACHE STRING
  "Enable additional, computationally intensive synthesis tests"
)

set(
  TEST_LONG_DURATION_SYNTHESIS
  ""
  CACHE STRING
  "Enable long duration (> 10 min) synthesis tests"
)

configure_file(
  ${CMAKE_SOURCE_DIR}/test/lit.site.cfg.in
  ${CMAKE_BINARY_DIR}/test/lit.site.cfg
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/run_lit.in
  ${CMAKE_BINARY_DIR}/run_lit
)

configure_file(
  ${CMAKE_SOURCE_DIR}/tools/gdbprinter.py
  ${CMAKE_BINARY_DIR}/souper-check-gdb.py
)


# ============================================================================
# Profiling runtime
# ============================================================================

add_library(profileRuntime STATIC
  runtime/souperPassProfile.c
)


# ============================================================================
# Check target
# ============================================================================

add_custom_target(check
  COMMAND ${CMAKE_BINARY_DIR}/run_lit
  DEPENDS
    # extractor_tests
    # inst_tests
    # parser-test
    # parser_tests
    profileRuntime
    souper
    souper-check
    souper-interpret
    souperPass
    souper2llvm
    souperPassProfileAll
    count-insts
    # interpreter_tests
    # bulk_tests
    # codegen_tests
  USES_TERMINAL
)


# ============================================================================
# Assertions
# ============================================================================

# We want assertions even in Release mode.
string(
  REPLACE
  "-DNDEBUG"
  ""
  CMAKE_CXX_FLAGS_RELEASE
  "${CMAKE_CXX_FLAGS_RELEASE}"
)


# ============================================================================
# Generated helper scripts/files
# ============================================================================

set(
  SOUPER_PASS
  ${CMAKE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}souperPass${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(
  SOUPER_PASS_PROFILE_ALL
  ${CMAKE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}souperPassProfileAll${CMAKE_SHARED_LIBRARY_SUFFIX}
)

set(
  PROFILE_LIBRARY
  ${CMAKE_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}profileRuntime${CMAKE_STATIC_LIBRARY_SUFFIX}
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/compare_pruning_output.in
  ${CMAKE_BINARY_DIR}/compare_pruning_output
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/reduce.in
  ${CMAKE_BINARY_DIR}/reduce
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/cache_dump.in
  ${CMAKE_BINARY_DIR}/cache_dump
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/cache_dfa.in
  ${CMAKE_BINARY_DIR}/cache_dfa
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/cache_import.in
  ${CMAKE_BINARY_DIR}/cache_import
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/cache_infer.in
  ${CMAKE_BINARY_DIR}/cache_infer
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/py_souper2llvm.in
  ${CMAKE_BINARY_DIR}/py_souper2llvm
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/include/souper/Tool/GetSolver.h.in
  ${CMAKE_BINARY_DIR}/include/souper/Tool/GetSolver.h
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/include/souper/KVStore/KVSocket.h.in
  ${CMAKE_BINARY_DIR}/include/souper/KVStore/KVSocket.h
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/redis-unix-socket.conf.in
  ${CMAKE_BINARY_DIR}/redis-unix-socket.conf
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/sclang.in
  ${CMAKE_BINARY_DIR}/sclang
  @ONLY
)

configure_file(
  ${CMAKE_SOURCE_DIR}/utils/sclang.in
  ${CMAKE_BINARY_DIR}/sclang++
  @ONLY
)


# ============================================================================
# Documentation
# ============================================================================

add_subdirectory(docs)

# Strip LLVM's -fno-exceptions and enforce -fexceptions for Z3
string(REPLACE "-fno-exceptions" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
