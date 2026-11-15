#include <QtTest/QtTest>

#include "trakcrate/xml_lite.h"

using namespace trakcrate;

namespace {

bool same(const XmlNode& a, const XmlNode& b)
{
    if (a.name != b.name || a.attrs != b.attrs || a.children.size() != b.children.size()) return false;
    if (a.children.empty() && a.text != b.text) return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!same(a.children[i], b.children[i])) return false;
    return true;
}

} // namespace

class TestXmlLite : public QObject
{
    Q_OBJECT
private slots:
    void roundTripAwkwardText()
    {
        XmlNode root;
        root.name = "mix";
        root.attrs["a"] = "x & y < z > \"q\" 'r'";
        root.attrs["multi"] = "line1\nline2\ttab\r";
        XmlNode c;
        c.name = "name";
        c.text = "Fish & Chips <b> \"quoted\" 'single'\nnew line\r\n  indented ünïcödé 日本語 🎧 ]]>";
        root.children.push_back(c);
        XmlNode e;
        e.name = "empty";
        root.children.push_back(e);
        XmlNode nest;
        nest.name = "nest";
        nest.children.push_back(c);
        root.children.push_back(nest);
        XmlNode back;
        std::string err;
        QVERIFY2(xmlParse(xmlWrite(root), &back, &err), err.c_str());
        QVERIFY(same(root, back));
    }

    void parsesHandWrittenXml()
    {
        const std::string x = "\xEF\xBB\xBF<?xml version='1.0'?>\n<!-- c --><a x='1' y=\"2\">\n <!-- inner --><b>t&#65;&#x42;&amp;</b>\n<c/></a>\n<!-- end -->";
        XmlNode n;
        std::string err;
        QVERIFY2(xmlParse(x, &n, &err), err.c_str());
        QCOMPARE(n.attr("x"), std::string("1"));
        QCOMPARE(n.children.size(), size_t(2));
        QCOMPARE(n.child("b")->text, std::string("tAB&"));
        QVERIFY(n.child("c") != nullptr);
        QVERIFY(n.child("zz") == nullptr);
        QCOMPARE(n.attr("none", "def"), std::string("def"));
    }

    void malformedGivesErrorWithLine()
    {
        const char* bad[] = {"", "plain", "<a>", "<a></b>", "<a x=1/>", "<a x='1/>", "<a>&bogus;</a>", "<a>&#0;</a>", "<a>&#xZZ;</a>", "<a/><b/>",
                             "<a x='1' x='2'/>", "<a><!-- x</a>", "<1a/>", "<a\n\n<b/></a>"};
        for (const char* b : bad) {
            XmlNode n;
            std::string err;
            QVERIFY2(!xmlParse(b, &n, &err), b);
            QVERIFY2(err.rfind("line ", 0) == 0, (std::string(b) + " -> " + err).c_str());
        }
        XmlNode n;
        std::string err;
        QVERIFY(!xmlParse("<a>\n<b>\n</a>", &n, &err));
        QVERIFY(err.find("line 3") != std::string::npos);
    }

    void deepNestingIsRefusedNotCrashed()
    {
        std::string s;
        for (int i = 0; i < 5000; ++i) s += "<a>";
        XmlNode n;
        QVERIFY(!xmlParse(s, &n));
    }

    void mutationsNeverCrash()
    {
        XmlNode root;
        root.name = "mix";
        root.attrs["v"] = "1";
        XmlNode c;
        c.name = "x";
        c.text = "a&b";
        c.attrs["k"] = "<>";
        root.children = {c, c};
        const std::string good = xmlWrite(root);
        unsigned seed = 7;
        auto rnd = [&]() { return seed = seed * 1103515245u + 12345u; };
        for (int i = 0; i < 3000; ++i) {
            std::string m = good;
            const int edits = 1 + int(rnd() % 3);
            for (int k = 0; k < edits; ++k) {
                const size_t pos = (rnd() >> 8) % m.size();
                switch (rnd() % 3) {
                case 0: m[pos] = char(rnd() >> 16); break;
                case 1: m.erase(pos, 1 + (rnd() >> 8) % 5); break;
                default: m.insert(pos, "<&\">"); break;
                }
                if (m.empty()) m = "x";
            }
            XmlNode n;
            std::string err;
            if (!xmlParse(m, &n, &err)) QVERIFY(!err.empty());
        }
    }
};

QTEST_APPLESS_MAIN(TestXmlLite)
#include "test_trakcrate_xml_lite.moc"
