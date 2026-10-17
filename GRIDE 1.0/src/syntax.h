#ifndef EDITOR_SYNTAX_H
#define EDITOR_SYNTAX_H

#include <cstddef>
#include <string>
#include <vector>

namespace editor {

const unsigned char KindNormal  = 0;
const unsigned char KindKeyword = 1;
const unsigned char KindType    = 2;
const unsigned char KindString  = 3;
const unsigned char KindChar    = 4;
const unsigned char KindComment = 5;
const unsigned char KindPreproc = 6;
const unsigned char KindNumber  = 7;
const unsigned char KindLabel   = 8;

enum Language {
    LangPlain = 0,
    LangC,
    LangCpp,
    LangShalimar,
    LangAsm,

    LangJson,
    LangCount
};

Language languageFor(const std::string& path);
// The language a *source* is compiled as - a header, an assembly file or
// anything else that is not compiled answers LangPlain. The one list the
// project's build, the pane's grouping and the colouring all read.
Language sourceLanguageFor(const std::string& path);
const char* languageName(Language lang);

const char* languageSuffix(Language lang);

struct SyntaxState {
    bool comment = false;
    bool string = false;
};

void advanceState(const std::string& line, Language lang, SyntaxState& state);

std::vector<unsigned char> highlight(const std::string& line, Language lang,
                                     SyntaxState& state);

const char* colourFor(unsigned char kind);

}

#endif
