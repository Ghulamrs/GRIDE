//
//  SymbolTable.cpp
//  Compiler++
//
//  Created by G. R. Akhtar on 29/08/2026.
//
//  C++98 only.

#include "SymbolTable.h"

Scope::~Scope() {
    for (std::map<std::string, Symbol*>::iterator it = table.begin();
         it != table.end(); ++it) {
        delete it->second;
    }
}

bool Scope::insert(const std::string &name, Symbol *sym) {
    if (table.find(name) != table.end()) return false;
    table[name] = sym;
    return true;
}

Symbol *Scope::lookup(const std::string &name) const {
    std::map<std::string, Symbol*>::const_iterator it = table.find(name);
    if (it == table.end()) return 0;
    return it->second;
}

// --- SymbolTable ---

SymbolTable::SymbolTable() {
    stack.push_back(new Scope());       // the global scope
}

SymbolTable::~SymbolTable() {
    while (!stack.empty()) {
        delete stack.back();
        stack.pop_back();
    }
}

void SymbolTable::pushScope() {
    stack.push_back(new Scope());
}

void SymbolTable::popScope() {
    if (stack.size() <= 1) return;      // never pop the global scope
    delete stack.back();
    stack.pop_back();
}

bool SymbolTable::insert(const std::string &name, Symbol *sym) {
    if (stack.empty()) return false;
    return stack.back()->insert(name, sym);
}

// Innermost scope first, then outward -- lexical scoping, literally.
Symbol *SymbolTable::lookup(const std::string &name) const {
    for (std::size_t i = stack.size(); i > 0; --i) {
        Symbol *s = stack[i - 1]->lookup(name);
        if (s) return s;
    }
    return 0;
}

Symbol *SymbolTable::lookupLocal(const std::string &name) const {
    if (stack.empty()) return 0;
    return stack.back()->lookup(name);
}
