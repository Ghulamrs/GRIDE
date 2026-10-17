// inventory.cpp - adding, finding, the total and a report.
#include <cstdio>
#include "inventory.h"

void Inventory::add(const Item &item) { items_.push_back(item); }

Item *Inventory::find(const std::string &name) {
    for (std::size_t i = 0; i < items_.size(); i++)
        if (items_[i].name() == name) return &items_[i];
    return 0;
}

double Inventory::total() const {
    double sum = 0;
    for (std::size_t i = 0; i < items_.size(); i++) sum += items_[i].value();
    return sum;
}

void Inventory::report() const {
    for (std::size_t i = 0; i < items_.size(); i++)
        std::printf("%-8s %3d %8.2f\n", items_[i].name().c_str(), items_[i].count(),
                    items_[i].value());
    std::printf("total        %8.2f\n", total());
}
