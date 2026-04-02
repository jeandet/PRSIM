#include "showcase_common.hpp"

struct AudioMixer {
    prism::Field<prism::Slider<>> volume{{.value = 0.75, .min = 0.0, .max = 1.0}};
    prism::Field<prism::Slider<int>> quality{{.value = 3, .min = 1, .max = 5, .step = 1}};
    prism::Field<prism::Checkbox> mute{{.checked = false, .label = "Mute"}};

    void view(prism::WidgetTree::ViewBuilder& vb) {
        vb.vstack(volume, quality, mute);
    }
};

int main(int argc, char* argv[]) {
    AudioMixer model;
    return showcase(argc, argv, model, 400, 100);
}
