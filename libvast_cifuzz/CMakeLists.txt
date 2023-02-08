if (NOT VAST_ENABLE_CIFUZZ)
  return()
endif ()

add_fuzz_test(libvast_cifuzz_query_parser "fuzz_query_parser.cpp")
target_link_libraries(
  libvast_cifuzz_query_parser
  PUBLIC vast::libvast
  PRIVATE vast::internal)
