#include <prism/prism.hpp>

#include <string>

struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};
    prism::Field<bool> dark_mode{"Dark Mode", true};
};

struct Dashboard {
    Settings settings;
    prism::Field<int> counter{"Counter", 0};
};

int main() {
    Dashboard dashboard;

    // Cross-component signal: log when dark_mode changes
    auto conn = dashboard.settings.dark_mode.on_change().connect(
        [](const bool& dark) {
            // In a real app this would retheme — for now just a signal test
            (void)dark;
        }
    );

    prism::model_app("PRISM Model Dashboard", dashboard);
}
