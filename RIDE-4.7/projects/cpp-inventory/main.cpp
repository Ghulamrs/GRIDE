// main.cpp - stock a shop, sell some, and report.
#include "inventory.h"

int main() {
    Inventory shop;
    shop.add(Item("pencil", 0.50, 120));
    shop.add(Item("notebook", 2.25, 40));
    shop.add(Item("ruler", 1.10, 15));

    if (Item *n = shop.find("notebook")) n->take(12);
    if (Item *r = shop.find("ruler")) r->take(20);
    shop.report();
    return 0;
}
