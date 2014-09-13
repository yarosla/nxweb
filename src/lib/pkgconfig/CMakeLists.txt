cmake_minimum_required(VERSION 2.8.4)

project(nxweb_pkgconfig)

set(EXTRA_INCLUDES_STR "")
foreach(F ${EXTRA_INCLUDES})
  set(EXTRA_INCLUDES_STR "${EXTRA_INCLUDES_STR} -I${F}")
endforeach(F IN EXTRA_LIBS)

set(EXTRA_LIBS_STR "")
foreach(F ${EXTRA_LIBS})
  set(EXTRA_LIBS_STR "${EXTRA_LIBS_STR} ${F}")
endforeach(F IN EXTRA_LIBS)

configure_file (
  "nxweb.pc.in"
  "${PROJECT_BINARY_DIR}/nxweb.pc"
  )

install(FILES "${PROJECT_BINARY_DIR}/nxweb.pc" DESTINATION lib/pkgconfig)
