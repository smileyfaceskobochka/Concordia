#include "nucleus/engine.h"
#include <SDL3/SDL.h>
#include <stdexcept>

int main(int argc, char* argv[]) {
    try {
        Nucleus::Engine engine;
        engine.run();
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Fatal: %s", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
