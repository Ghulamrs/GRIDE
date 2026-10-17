/* account.c - the account operations. */
#include <stdio.h>
#include <string.h>
#include "account.h"

void account_open(struct account *a, const char *owner, long cents)
{
    strncpy(a->owner, owner, sizeof a->owner - 1);
    a->owner[sizeof a->owner - 1] = '\0';
    a->balance = cents;
}

void account_deposit(struct account *a, long cents)
{
    a->balance += cents;
}

int account_withdraw(struct account *a, long cents)
{
    if (cents > a->balance) return 0;
    a->balance -= cents;
    return 1;
}

void account_print(const struct account *a)
{
    printf("%-8s %6ld.%02ld\n", a->owner, a->balance / 100, a->balance % 100);
}
