#include "showcase_common.hpp"

struct TabsShowcase {
    prism::Field<prism::TextField<>> username{{.value = "jeandet"}};
    prism::Field<bool> dark_mode{true};
    prism::Field<prism::Slider<>> volume{{.value = 0.6}};
    prism::Field<prism::TabBar<>> tabs;

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.tabs(tabs, [&] {
            vb.tab("Account", [&](prism::WidgetTree::ViewBuilder& tvb) {
                tvb.vstack(username, dark_mode);
            });
            vb.tab("Audio", [&](prism::WidgetTree::ViewBuilder& tvb) {
                tvb.widget(volume);
            });
        });
    }
};

int main(int argc, char* argv[]) {
    TabsShowcase model;
    return showcase(argc, argv, model, 320, 140);
}
