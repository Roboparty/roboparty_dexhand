#include <dh116s/hand.hpp>

int main() {
    dh116s::Hand hand;
    return hand.is_connected() || hand.get_state().feedback_valid ? 1 : 0;
}
