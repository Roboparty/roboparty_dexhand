#include <rphand/hand.hpp>

int main() {
    rphand::Hand hand;
    return hand.is_connected() || hand.get_state().feedback_valid ? 1 : 0;
}
