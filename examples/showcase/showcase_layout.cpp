#include "showcase_common.hpp"

struct Settings {
    prism::Field<std::string> username{"jeandet"};
    prism::Field<bool> dark_mode{true};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(username, dark_mode);
    }
};

struct Dashboard {
    Settings settings;
    prism::Field<prism::Slider<>> brightness{{.value = 0.6, .min = 0.0, .max = 1.0}};
    prism::Field<prism::Button> apply{{"Apply"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack([&] {
            vb.component(settings);
            vb.hstack(brightness, apply);
        });
    }
};

int main(int argc, char* argv[]) {
    Dashboard model;
    return showcase(argc, argv, model, 400, 100);
}
