/* main.c - two accounts, a transfer, and one refused withdrawal. */
#include <stdio.h>
#include "account.h"

int main(void)
{
    struct account alice, bob;
    account_open(&alice, "Alice", 10000);
    account_open(&bob, "Bob", 2550);

    if (account_withdraw(&alice, 4025)) account_deposit(&bob, 4025);
    if (!account_withdraw(&bob, 99999)) printf("Bob cannot take 999.99\n");

    account_print(&alice);
    account_print(&bob);
    return 0;
}
