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
    prism::Field<prism::Button> increment{{"Increment"}};
    prism::Field<int> counter{0};
    prism::State<int> request_count{0};
};

int main() {
    Dashboard dashboard;
    std::vector<prism::Connection> connections;

    prism::model_app("PRISM Model Dashboard", dashboard, [&](prism::AppContext& ctx) {
        auto sched = ctx.scheduler();

        // Cross-component wiring: volume change updates status label (on app thread)
        connections.push_back(
            dashboard.settings.volume.on_change()
            | prism::on(sched)
            | prism::then([&](const prism::Slider<>& s) {
                  std::cout << "Volume: " << s.value << "\n";
                  if (s.value > 0.9)
                      dashboard.status.set({"Volume is very high!"});
              })
        );

        // Button click increments counter
        connections.push_back(
            dashboard.increment.on_change()
            | prism::on(sched)
            | prism::then([&](const prism::Button&) {
                  dashboard.counter.set(dashboard.counter.get() + 1);
                  std::cout << "Counter: " << dashboard.counter.get() << "\n";
              })
        );

        dashboard.settings.dark_mode.observe([](const bool& v) {
            std::cout << "Dark mode: " << (v ? "ON" : "OFF") << "\n";
        });
    });
}
