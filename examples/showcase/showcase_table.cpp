#include "showcase_common.hpp"

#if __cpp_impl_reflection
#include <prism/ui/table.hpp>

struct Reading {
    prism::Field<std::string> sensor{""};
    prism::Field<double> value{0.0};
};

struct TableShowcase {
    prism::List<Reading> readings;

    TableShowcase() {
        readings.push_back(Reading{.sensor = {"battery"}, .value = {3.7}});
        readings.push_back(Reading{.sensor = {"bus"}, .value = {12.1}});
        readings.push_back(Reading{.sensor = {"temp"}, .value = {41.2}});
    }

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.table(readings).headers({"Sensor", "Value"});
    }
};

int main(int argc, char* argv[]) {
    TableShowcase model;
    return showcase(argc, argv, model, 320, 160);
}
#else
// wrap_row_storage() is reflection-only; produce a trivial placeholder SVG so
// build_by_default keeps working on non-reflection toolchains.
int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::ofstream(argv[1]) << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    return 0;
}
#endif
