#include "app.hpp"
#include <iostream>

namespace obsidian {
namespace desktop {

App::App() = default;

App::~App() {
    if (initialized_) {
        shutdown();
    }
}

bool App::initialize(const AppConfig& config) {
    (void)config;
    if (initialized_) {
        return true;
    }

    // Prepare runtime subsystem hooks (allocators, event loop, renderer)
    initialized_ = true;
    running_ = false;
    return true;
}

int App::run() {
    if (!initialized_) {
        return 1;
    }

    running_ = true;

    // Main execution loop placeholder
    // In future iterations: poll window events, process lock-free ringbuffers, render frames
    running_ = false;

    return 0;
}

void App::shutdown() {
    if (!initialized_) {
        return;
    }

    running_ = false;
    initialized_ = false;
}

} // namespace desktop
} // namespace obsidian
