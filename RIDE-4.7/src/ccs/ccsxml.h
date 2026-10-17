#ifndef EDITOR_CCS_XML_H
#define EDITOR_CCS_XML_H

#include <string>
#include <utility>
#include <vector>

namespace editor {
namespace ccs {

// **As much XML as CCS writes**: elements, attributes, text and the five entities. The three files
// a CCS project is made of are Eclipse's, regular and machine-written, so nothing here reads a
// DTD, a namespace or CDATA - a document that needs those is refused with a line number.
struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string> > attributes;
    std::vector<XmlNode> children;
    std::string text;   // the element's own text, entities decoded, whitespace trimmed

    std::string attribute(const std::string& key, const std::string& fallback = std::string()) const;
    bool has(const std::string& key) const;
    // The first child of that name, or null; every child of that name, in order.
    const XmlNode* child(const std::string& name) const;
    std::vector<const XmlNode*> all(const std::string& name) const;
    // Every descendant of that name, in document order.
    void find(const std::string& name, std::vector<const XmlNode*>& into) const;
};

bool parseXml(const std::string& text, XmlNode& root, std::string& error);
bool readXmlFile(const std::string& file, XmlNode& root, std::string& error);

}
}

#endif
