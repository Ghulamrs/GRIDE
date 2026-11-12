#ifndef SHALIMAR_VERSION_H
#define SHALIMAR_VERSION_H

/* Shalimar's number, once. The compiler answers it to --version and prints it in its banner (src/Driver.cpp), and
   the runtime carries it in a string of its own (Runtime.cpp): a program compiled by one shalimar links the runtime
   built with it, so the two are one release and share one number. */
#define SHALIMAR_VERSION "1.3"

#endif
