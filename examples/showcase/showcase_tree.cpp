#include "showcase_common.hpp"

#if __cpp_impl_reflection
#include <prism/ui/tree.hpp>

struct Sensors {
    float battery_v = 3.7f;
    float bus_v = 12.1f;
};

struct Device {
    std::string name = "Controller";
    Sensors sensors;
    int firmware = 12;
};

struct TreeShowcase {
    Device device;
    prism::TreeController ctrl{prism::wrap_struct_tree(device)};

    TreeShowcase() {
        ctrl.on_row_clicked(0, ctrl.rows[0]); // expand root
        ctrl.on_row_clicked(1, ctrl.rows[1]); // expand "sensors"
    }

    void view(prism::WidgetTree::ViewBuilder& vb) { vb.tree(ctrl); }
};

int main(int argc, char* argv[]) {
    TreeShowcase model;
    return showcase(argc, argv, model, 360, 220);
}
#else
// wrap_struct_tree() is reflection-only; produce a trivial placeholder SVG so
// build_by_default keeps working on non-reflection toolchains.
int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::ofstream(argv[1]) << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    return 0;
}
#endif
