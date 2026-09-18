#include "TabCycle.h"

namespace TabCycle {

int nextIndex(int current, int count, bool forward) {
    if (count <= 1) return current;
    return forward ? (current + 1) % count : (current - 1 + count) % count;
}

}  // namespace TabCycle
