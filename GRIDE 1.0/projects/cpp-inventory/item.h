// item.h - one line of stock: a name, a price, a quantity.
#ifndef ITEM_H
#define ITEM_H
#include <string>

class Item {
public:
    Item(const std::string &name, double price, int count);
    const std::string &name() const { return name_; }
    double value() const;
    void take(int n);
    int count() const { return count_; }
private:
    std::string name_;
    double price_;
    int count_;
};

#endif
