#include <QtTest>

#include "network/discovery_line.h"

using namespace vdj;

class DiscoveryLineTest : public QObject {
    Q_OBJECT
private slots:
    void keepAliveLine()
    {
        QCOMPARE(formatDiscoveryLine(0x06, 2, true, "169.254.1.2", "XDJ-1000", 0),
                 QStringLiteral("p2 type=0x06 keep-alive model=XDJ-1000 ip=169.254.1.2 known=1"));
    }
    void unknownIpAndOverBackline()
    {
        const QString s = formatDiscoveryLine(0x06, 33, false, QString(), "DJM-900NXS2", 4);
        QVERIFY(s.startsWith("p33(over backline) type=0x06"));
        QVERIFY(s.contains("ip=? known=0"));
        QVERIFY(s.endsWith(" x4"));
    }
    void typeNames()
    {
        QCOMPARE(discoveryTypeName(0x02), QStringLiteral("id-use-req"));
        QCOMPARE(discoveryTypeName(0x08), QStringLiteral("id-collision"));
        QCOMPARE(discoveryTypeName(0x77), QStringLiteral("unknown"));
    }
    void limiter()
    {
        DiscoveryLineLimiter l(5000);
        int sup = -1;
        QVERIFY(l.allow(2, 6, 0, &sup));
        QCOMPARE(sup, 0);
        QVERIFY(!l.allow(2, 6, 1500, &sup));
        QVERIFY(!l.allow(2, 6, 3000, &sup));
        QVERIFY(l.allow(3, 6, 3000, &sup));          // another player is its own key
        QVERIFY(l.allow(2, 2, 3000, &sup));          // another type too
        QVERIFY(l.allow(2, 6, 5000, &sup));
        QCOMPARE(sup, 2);
    }
};

QTEST_APPLESS_MAIN(DiscoveryLineTest)
#include "test_discovery_line.moc"
