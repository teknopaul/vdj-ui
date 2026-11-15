#ifndef TRAKCRATE_XML_LITE_H
#define TRAKCRATE_XML_LITE_H

#include <map>
#include <string>
#include <vector>

// A tiny XML subset for the .mix files (no dependency): elements, attributes, text, comments, entities. No DTD, no namespaces, no CDATA.
namespace trakcrate {

struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attrs;
    std::string text;                      // exact text of an element without children; ignored by the writer when there are children
    std::vector<XmlNode> children;

    std::string attr(const std::string& key, const std::string& def = std::string()) const;
    const XmlNode* child(const std::string& childName) const;                 // first child with that name or null
    std::vector<const XmlNode*> childrenNamed(const std::string& childName) const;
};

// Indented document with an XML prolog. Escapes & < > " ' and control characters, so xmlParse(xmlWrite(n)) gives n back.
std::string xmlWrite(const XmlNode& root);

// Prolog and comments are skipped. False with a message including the line number for malformed input; never throws.
bool xmlParse(const std::string& text, XmlNode* out, std::string* err = nullptr);

} // namespace trakcrate

#endif
