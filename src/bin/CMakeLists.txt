cmake_minimum_required(VERSION 2.8.4)

project(nxweb_exe)

set(EXE_SOURCE_FILES main.c)

include_directories(../include ${EXTRA_INCLUDES})

add_compile_options(-pthread)

add_executable(nxweb_exe ${EXE_SOURCE_FILES})
target_link_libraries(nxweb_exe nxweb_so pthread ${EXTRA_LIBS})
set_target_properties(nxweb_exe PROPERTIES OUTPUT_NAME nxweb)

install(TARGETS nxweb_exe DESTINATION bin)
