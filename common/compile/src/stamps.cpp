// stamps.cpp - which steps of a plan need to run (the design's section 5, "Up to date"). A step is up to date
// when its stamp is the one of its last run that succeeded, what it writes is there, and nothing it reads is
// newer than what it writes. The stamps of a build are kept in <object folder>\<project>.stamps, one line per
// output; the three outlooks ask here, so they rebuild alike.
#include "context.h"

#include <map>

namespace gride { namespace compile {

struct StampsData {
    Plan plan;
    FileSystem *fs;
    std::string file;
    std::map<std::string, std::string> kept;      // an output, lower case, relative to the object folder -> its stamp
    bool changed;
    StampsData() : fs(0), changed(false) {}

    std::string key(const Step &s) const {
        return s.outputs.empty() ? std::string() : lower(relative(plan.objectFolder, s.outputs[0]));
    }
};

Stamps::Stamps(const Workspace &w, const Request &r, const Plan &plan) : data_(new StampsData()) {
    StampsData &d = *data_;
    d.plan = plan;
    d.fs = w.data_->fs;
    if (plan.objectFolder.empty()) return;
    d.file = combine(plan.objectFolder, stem(r.project) + ".stamps");
    std::string text;
    if (!d.fs->read(d.file, text)) return;
    Words lines = split(replaceAll(text, "\r", ""), "\n");
    for (size_t i = 0; i < lines.size(); ++i) {
        size_t tab = lines[i].find('\t');
        if (tab != std::string::npos) d.kept[lines[i].substr(tab + 1)] = lines[i].substr(0, tab);
    }
}

Stamps::~Stamps() { delete data_; }

bool Stamps::upToDate(int step) const {
    const StampsData &d = *data_;
    if (step < 0 || static_cast<size_t>(step) >= d.plan.steps.size()) return false;
    const Step &s = d.plan.steps[step];
    if (s.outputs.empty()) return false;
    std::map<std::string, std::string>::const_iterator it = d.kept.find(d.key(s));
    if (it == d.kept.end() || it->second != s.stamp) return false;
    long long oldest = 0;
    for (size_t i = 0; i < s.outputs.size(); ++i) {
        long long t = d.fs->modified(s.outputs[i]);
        if (t == 0) return false;
        if (oldest == 0 || t < oldest) oldest = t;
    }
    for (size_t i = 0; i < s.inputs.size(); ++i) {
        long long t = d.fs->modified(s.inputs[i]);
        if (t == 0 || t > oldest) return false;       // an input that is gone is not up to date either
    }
    return true;
}

// a step that ran: remembered when it succeeded, forgotten when it failed
void Stamps::ran(int step, bool succeeded) {
    StampsData &d = *data_;
    if (step < 0 || static_cast<size_t>(step) >= d.plan.steps.size()) return;
    const Step &s = d.plan.steps[step];
    std::string k = d.key(s);
    if (k.empty()) return;
    if (succeeded) d.kept[k] = s.stamp; else d.kept.erase(k);
    d.changed = true;
}

void Stamps::save() {
    StampsData &d = *data_;
    if (!d.changed || d.file.empty()) return;
    std::string text;
    for (std::map<std::string, std::string>::const_iterator it = d.kept.begin(); it != d.kept.end(); ++it)
        text += it->second + "\t" + it->first + "\r\n";
    d.fs->write(d.file, text);
    d.changed = false;
}

}}
