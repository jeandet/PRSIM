#include "showcase_common.hpp"

struct Counter {
    prism::Field<int> count{42};
    prism::Field<std::string> label{"Hello, PRISM!"};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(count, label);
    }
};

int main(int argc, char* argv[]) {
    Counter model;
    return showcase(argc, argv, model, 300, 70);
}
