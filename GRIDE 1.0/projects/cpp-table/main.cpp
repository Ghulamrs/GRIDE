// main.cpp - a few names against numbers: added, found, one refused twice over.
#include <cstdio>
#include "table.h"

int main() {
    Table t;
    t.add("pi", 3.14159);
    t.add("e", 2.71828);
    t.add("root2", 1.41421);

    double v = 0;
    if (t.find("e", v)) std::printf("e      %.5f\n", v);
    if (!t.find("tau", v)) std::printf("tau    not in the table\n");
    std::printf("pi again: %s\n", t.add("pi", 3.0) ? "added" : "refused, one entry per name");

    // Twelve is all it holds; the rest are refused and the table stays as it was.
    char name[8];
    int refused = 0;
    for (int i = 0; i < 12; ++i) {
        std::snprintf(name, sizeof name, "n%d", i);
        if (!t.add(name, i)) ++refused;
    }
    std::printf("held %d of %d, %d refused\n", t.held(), static_cast<int>(Table::kMost), refused);
    return 0;
}
