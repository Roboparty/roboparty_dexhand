#include <rphand/hand.hpp>
#include "options.hpp"
#include <iostream>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            std::cout << "Usage: rphand_read_state [connection options]\n" << example::connection_help;
            return 0;
        }
    }
    example::Options options;
    if (!example::parse(argc, argv, options, false)) {
        std::cerr << example::connection_help;
        return 2;
    }
    rphand::Hand hand;
    if (!hand.connect(options.connection)) return 1;
    const auto state = hand.get_state();
    hand.disconnect();
    std::cout << "feedback_valid=" << state.feedback_valid << "\npositions:";
    for (int value : state.positions) std::cout << ' ' << value;
    std::cout << "\nfault_codes:";
    bool fault = false;
    for (int value : state.fault_codes) { std::cout << ' ' << value; fault |= value != 0; }
    std::cout << '\n';
    return state.feedback_valid && !fault ? 0 : 1;
}
