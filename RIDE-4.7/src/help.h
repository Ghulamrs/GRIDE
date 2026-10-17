#ifndef EDITOR_HELP_H
#define EDITOR_HELP_H

#include <string>
#include <vector>

namespace editor {
namespace help {

struct Page {
    const char* number;
    const char* title;
    const char* about;
    const char* file;
};

const std::vector<Page>& pages();

std::vector<std::string> contents();

}
}

#endif
