#include <QtTest/QtTest>

class TestMain : public QObject
{
    Q_OBJECT

private slots:
    void sanityCheck();
};

void TestMain::sanityCheck()
{
    QVERIFY(true);
}

QTEST_MAIN(TestMain)
#include "test_main.moc"
