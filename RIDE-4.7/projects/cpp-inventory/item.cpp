// item.cpp - an item's value, and taking some away.
#include "item.h"

Item::Item(const std::string &name, double price, int count)
    : name_(name), price_(price), count_(count) {}

double Item::value() const { return price_ * count_; }

void Item::take(int n) { count_ = n > count_ ? 0 : count_ - n; }
