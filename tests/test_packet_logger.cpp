#include <QtTest>
#include <QThread>
#include <atomic>
#include <thread>
#include <vector>
#include "debug/packet_logger.h"

using namespace vdj;

// The logger is called from libcdj's update, beat and discovery threads and from the GUI thread at once.
class TestPacketLogger : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        PacketLogger::instance().setEnabled(false);
        PacketLogger::instance().setPacketLevel(true);
    }

    void nothingIsRecordedWhileDisabled()
    {
        auto& l = PacketLogger::instance();
        const int before = l.entryCount();
        l.log("USER", "ignored");
        l.log("UPDATE", "ignored");
        QCOMPARE(l.entryCount(), before);
        QVERIFY(!l.packetLevel());
    }

    void packetLevelCanBeSwitchedOffOnItsOwn()
    {
        auto& l = PacketLogger::instance();
        l.setEnabled(true);
        const int before = l.entryCount();
        l.setPacketLevel(false);
        QVERIFY(!l.packetLevel());
        l.log("UPDATE", "dropped");
        l.log("BEAT", "dropped");
        l.log("DISCOVERY", "dropped");
        QCOMPARE(l.entryCount(), before);
        l.log("USER", "kept");
        l.log("LINK", "kept");
        QCOMPARE(l.entryCount(), before + 2);
        l.setPacketLevel(true);
        QVERIFY(l.packetLevel());
        l.log("UPDATE", "kept");
        QCOMPARE(l.entryCount(), before + 3);
        QCOMPARE(l.entries().last().type, QString("UPDATE"));
    }

    void concurrentLoggingKeepsTheBufferConsistent()
    {
        auto& l = PacketLogger::instance();
        l.setEnabled(true);
        std::atomic<bool> stop{false};
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t)
            threads.emplace_back([&l, t]() {
                for (int i = 0; i < 4000; ++i) l.log(t % 2 ? "UPDATE" : "BEAT", QString("t%1 n%2").arg(t).arg(i));   // past the 5000 entry limit
            });
        std::thread reader([&]() { while (!stop) { const auto e = l.entries(); if (e.size() > l.maxEntries()) qFatal("buffer larger than its limit"); } });
        for (auto& th : threads) th.join();
        stop = true;
        reader.join();
        QVERIFY(l.entryCount() <= l.maxEntries());
        QCOMPARE(l.entryCount(), l.maxEntries());
        for (const auto& e : l.entries())
            QVERIFY(e.type == "UPDATE" || e.type == "BEAT" || e.type == "USER" || e.type == "LINK");
    }
};

QTEST_MAIN(TestPacketLogger)
#include "test_packet_logger.moc"
