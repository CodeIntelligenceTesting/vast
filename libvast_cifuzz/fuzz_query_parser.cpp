#include <cifuzz/cifuzz.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>
#include <string>

#include "vast/system/application.hpp"
#include "vast/system/default_configuration.hpp"



FUZZ_TEST_SETUP() {
  // Perform any one-time setup required by the FUZZ_TEST function.
}

FUZZ_TEST(const uint8_t *data, size_t size) {
  vast::system::default_configuration cfg;
  auto [root, root_factory] = vast::system::make_application("vast");
  if (!root)
    return;
  std::string s((const char *) data, size);
  std::vector<std::string> command_line{"--node", "export", "json", s.c_str()};
  auto invocation = parse(*root, command_line.begin(), command_line.end());
}
