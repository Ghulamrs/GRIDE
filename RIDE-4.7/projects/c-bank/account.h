/* account.h - a bank account and the three things done to one. */
#ifndef ACCOUNT_H
#define ACCOUNT_H

struct account {
    char owner[32];
    long balance;          /* in cents */
};

void account_open(struct account *a, const char *owner, long cents);
int  account_withdraw(struct account *a, long cents);   /* 0 when refused */
void account_deposit(struct account *a, long cents);
void account_print(const struct account *a);

#endif
