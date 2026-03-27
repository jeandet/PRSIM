#include <prism/prism.hpp>

#include <iostream>
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

    dashboard.settings.dark_mode.on_change().connect([](const bool& v) {
        std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
    });

    prism::model_app("PRISM Model Dashboard", dashboard);
}
