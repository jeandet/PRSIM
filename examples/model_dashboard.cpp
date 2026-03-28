#include <prism/prism.hpp>

#include <iostream>
#include <string>

struct Settings {
    prism::Field<std::string> username{"jeandet"};
    prism::Field<bool> dark_mode{true};
    prism::Field<prism::Slider<>> volume{{.value = 0.7}};
};

struct Dashboard {
    Settings settings;
    prism::Field<prism::Label<>> status{{"All systems go"}};
    prism::Field<int> counter{0};
    prism::State<int> request_count{0};
};

int main() {
    Dashboard dashboard;

    dashboard.settings.dark_mode.observe([](const bool& v) {
        std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
    });

    dashboard.settings.volume.observe([](const prism::Slider<>& s) {
        std::cout << "Volume: " << s.value << "\n";
    });

    prism::model_app("PRISM Model Dashboard", dashboard);
}
