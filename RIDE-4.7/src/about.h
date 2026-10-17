#ifndef EDITOR_ABOUT_H
#define EDITOR_ABOUT_H

#include <string>
#include <vector>

namespace editor {

namespace about {

const char* name();
const char* version();

std::vector<std::string> lines();
// About's last lines on their own - the release, the time, the CRC and what the release record says -
// for a box that shows them apart from the rest: the macOS panel's line under its credits.
std::vector<std::string> stampLines();
// A file's CRC-32 as About and the release record compute it, in eight hex digits; "" if it cannot be read.
std::string crc32Text(const std::string& file);
// The release record: every program in `directory` - and in `more`, under names not yet listed - by CRC-32, size and
// name, written into directory/release.crc as the installer's last step. About compares its own program with it.
int writeReleaseRecord(const std::string& directory, const std::vector<std::string>& more = std::vector<std::string>());
// Help > Environment: every tool, header directory and library in force, the file each resolved to, and why.
std::vector<std::string> environment();

}
}

#endif
