// text.h - words, paths and command lines. Nothing here touches the machine.
#ifndef GRIDE_COMPILE_TEXT_H
#define GRIDE_COMPILE_TEXT_H

#include <map>
#include <string>
#include <vector>

namespace gride { namespace compile {

typedef std::vector<std::string> Words;

std::string lower(const std::string &s);                        // ASCII letters only: paths and keywords
bool same(const std::string &a, const std::string &b);          // without regard to case
bool startsWith(const std::string &s, const std::string &head);
bool endsWith(const std::string &s, const std::string &tail);   // without regard to case
std::string trim(const std::string &s);
Words split(const std::string &s, const std::string &separators, bool keepEmpty = false);
std::string join(const Words &w, const std::string &between);
std::string replaceAll(std::string s, const std::string &what, const std::string &with);
bool contains(const Words &w, const std::string &s);            // without regard to case
void addOnce(Words &w, const std::string &s);

// ---- paths: '\' between folders, as Windows writes them ----
bool absolute(const std::string &path);
std::string slashes(const std::string &path);                   // every '/' a '\'
std::string forward(const std::string &path);                   // every '\' a '/'
std::string tidy(const std::string &path);                      // slashes, "." and ".." resolved, no trailing '\'
std::string combine(const std::string &folder, const std::string &path);    // path itself when it is absolute
std::string parent(const std::string &path);
std::string leaf(const std::string &path);
std::string stem(const std::string &path);                      // the leaf without its extension
std::string extension(const std::string &path);                 // ".c", lower case; "" for none
std::string relative(const std::string &folder, const std::string &path);   // path itself when not under folder
bool under(const std::string &folder, const std::string &path); // path is folder or below it
bool samePath(const std::string &a, const std::string &b);

bool wildcard(const std::string &pattern);                      // has a * or a ?
bool matches(const std::string &pattern, const std::string &name);   // * and ?, without regard to case

// ---- command lines ----
// A template or a command line cut into words by Windows' rules: blanks part words, a quoted stretch is one.
Words cutWords(const std::string &line);
std::string commandLine(const std::string &program, const Words &argv);

// $(Name) in a word: the names it uses, in order
Words namesIn(const std::string &word);

// A template filled word by word. A word that is one placeholder becomes that placeholder's words - none, one or
// many; a placeholder inside a longer word is filled in place (its words joined by a blank). A name not in the
// table is left as written and reported in `unknown`.
typedef std::map<std::string, Words> Table;
Words fill(const std::string &templ, const Table &table, Words *unknown = 0);

std::string stampHash(const std::string &text);                      // 16 hex digits, FNV-1a

}}

#endif
