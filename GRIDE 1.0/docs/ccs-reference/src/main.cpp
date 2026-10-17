#include <stdio.h>
#include "config.h"

class Counter {
public:
    explicit Counter(int start) : n_(start) {}
    int next() { return ++n_; }
private:
    int n_;
};

int main()
{
    Counter c(40);
    c.next();
    printf("K6747cpp: level %d, counter %d\n", LEVEL, c.next());
    return 0;
}
