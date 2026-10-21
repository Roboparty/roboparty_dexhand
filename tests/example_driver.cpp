// Exercise the actual example code with a fake Hand, never a hardware transport.
#define main wave_main
#include "../examples/cpp/wave_demo.cpp"
#undef main
#define main read_main
#include "../examples/cpp/read_state.cpp"
#undef main

namespace {
bool valid = true, fault = false, cleanup = true;
int connections = 0, disables = 0, disconnects = 0, motions = 0;
bool connected = false;
}
namespace rphand {
struct Hand::Impl {};
Hand::Hand() = default;
Hand::~Hand() = default;
bool Hand::connect(const ConnectionOptions&) { ++connections; connected = true; return true; }
bool Hand::is_connected() const { return connected; }
bool Hand::disable() { ++disables; return cleanup; }
void Hand::disconnect() { ++disconnects; connected = false; }
HandState Hand::get_state() const {
    HandState state;
    state.feedback_valid = valid;
    state.fault_codes[0] = fault ? 1 : 0;
    return state;
}
bool Hand::enable() { ++motions; return false; }
bool Hand::home() { ++motions; return false; }
bool Hand::set_speed(int) { ++motions; return false; }
bool Hand::set_current_limit(int) { ++motions; return false; }
bool Hand::set_torque_reached_mode(bool) { ++motions; return false; }
bool Hand::set_positions(const std::array<int, 6>&) { ++motions; return false; }
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const std::string mode = argv[1];
    if (mode == "targets") {
        for (int amplitude : {1, 6000, 10000})
            for (int tick = 0; tick <= 500; ++tick) {
                std::cout << amplitude << ' ' << tick;
                for (int value : target(tick / 50.0, amplitude)) std::cout << ' ' << value;
                std::cout << '\n';
            }
        return 0;
    }
    if (mode == "parse") {
        example::Options options;
        if (!example::parse(argc - 1, argv + 1, options)) return 2;
        const auto& c = options.connection;
        std::cout << c.interface << ' ' << c.node_id << ' ' << c.bitrate << ' '
                  << c.data_bitrate << ' ' << options.amplitude << ' ' << options.connect_only << '\n';
        return 0;
    }
    if (mode != "diagnostic") return 2;
#ifdef _WIN32
    std::string selector = "--serial", device = "F000";
#else
    std::string selector = "--interface", device = "mock";
#endif
    std::string program = "example", flag = "--connect-only";
    char* args[] = {program.data(), selector.data(), device.data(), flag.data()};
    for (int scenario = 0; scenario < 4; ++scenario) {
        valid = scenario != 1;
        fault = scenario == 2;
        cleanup = scenario != 3;
        connections = disables = disconnects = motions = 0;
        const int result = wave_main(4, args);
        if (result != (scenario == 0 ? 0 : 1) || connections != 1 || disables != 1
            || disconnects != 1 || motions != 0) return 1;
    }
    for (bool feedback : {true, false}) {
        valid = feedback; fault = false;
        connections = disables = disconnects = motions = 0;
        if (read_main(3, args) != (feedback ? 0 : 1) || connections != 1
            || disconnects != 1 || disables != 0 || motions != 0) return 1;
    }
    return 0;
}
