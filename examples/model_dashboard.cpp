#include <prism/prism.hpp>

#include <iostream>
#include <string>

struct Settings {
    prism::Field<std::string> username{"Username", "jeandet"};
    prism::Field<bool> dark_mode{"Dark Mode", true};
    prism::Field<prism::Slider<>> volume{"Volume", {.value = 0.7}};
};

struct Dashboard {
    Settings settings;
    prism::Field<prism::Label<>> status{"Status", {"All systems go"}};
    prism::Field<int> counter{"Counter", 0};
    prism::State<int> request_count{0};
};

int main() {
    Dashboard dashboard;

    auto c1 = dashboard.settings.dark_mode.on_change().connect([](const bool& v) {
        std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
    });

    auto c2 = dashboard.settings.volume.on_change().connect([](const prism::Slider<>& s) {
        std::cout << "Volume: " << s.value << "\n";
    });

    prism::model_app("PRISM Model Dashboard", dashboard);
}
