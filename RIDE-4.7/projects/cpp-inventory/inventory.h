// inventory.h - the items together, found by name.
#ifndef INVENTORY_H
#define INVENTORY_H
#include <vector>
#include "item.h"

class Inventory {
public:
    void add(const Item &item);
    Item *find(const std::string &name);
    double total() const;
    void report() const;
private:
    std::vector<Item> items_;
};

#endif
