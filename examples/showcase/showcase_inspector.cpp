#include "showcase_common.hpp"

#if __cpp_impl_reflection
#include <prism/widgets/inspector.hpp>

struct DeviceState {
    float voltage = 3.3f;
    int mode = 2;
    bool enabled = true;
};

int main(int argc, char* argv[]) {
    prism::Shared<DeviceState> device_state{DeviceState{}};
    prism::inspector::Inspector<DeviceState> inspector(device_state);
    return showcase(argc, argv, inspector, 300, 165);
}
#else
// Inspector<T> is reflection-only; produce a trivial placeholder SVG so
// build_by_default keeps working on non-reflection toolchains.
int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    std::ofstream(argv[1]) << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>\n";
    return 0;
}
#endif
