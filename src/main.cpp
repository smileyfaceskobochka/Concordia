#include "Engine.h"
#include <iostream>

int main() {
  Engine engine;
  if (!engine.init("config/engine_config.yaml")) {
    std::cerr << "Engine initialization failed\n";
    return EXIT_FAILURE;
  }
  engine.run();
  engine.shutdown();
  return EXIT_SUCCESS;
}
