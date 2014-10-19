cmake_minimum_required(VERSION 2.8.4)

project(sample_modules)

set(LIB_SOURCE_FILES modules/benchmark.c modules/hello.c modules/upload.c modules/subrequests.c)

include_directories(../src/include ${EXTRA_INCLUDES})

add_compile_options(-pthread)

add_library(sample_modules_so MODULE ${LIB_SOURCE_FILES})
set_target_properties(sample_modules_so PROPERTIES SOVERSION 0.0.0)
set_target_properties(sample_modules_so PROPERTIES OUTPUT_NAME sample_modules)
set_target_properties(sample_modules_so PROPERTIES PREFIX "")
