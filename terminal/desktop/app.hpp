#pragma once

#include <string>
#include <cstdint>

namespace obsidian {
namespace desktop {

struct AppConfig {
    std::string title{"Obsidian Quantitative Platform"};
    uint32_t width{1280};
    uint32_t height{800};
    bool headless{false};
};

class App {
public:
    App();
    ~App();

    // Lifecycle hooks
    bool initialize(const AppConfig& config);
    int run();
    void shutdown();

private:
    bool initialized_{false};
    bool running_{false};
};

} // namespace desktop
} // namespace obsidian
