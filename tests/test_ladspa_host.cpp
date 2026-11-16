#include <QtTest>

#include <QTemporaryDir>

#include "audio/ladspa_effect.h"

using namespace vdj;

// The test plugin (tests/ladspa_test_plugin.cpp, built as ladspa-test-plugin.so): label "testgain", ports: control "gain", in.l in.r out.l out.r
class TestLadspaHost : public QObject {
    Q_OBJECT
private slots:
    void findsAPluginByLabelAndAppliesItsControl()
    {
        QString err;
        auto fx = LadspaEffect::create({"nothere", "testgain"}, 44100, QStringLiteral(VDJ_LADSPA_TEST_DIR), &err);
        QVERIFY2(fx, qPrintable(err));
        QCOMPARE(fx->label(), QString("testgain"));
        QVERIFY(fx->stereoNative());
        const int16_t in[8] = {1000, -1000, 2000, -2000, 3000, -3000, 4000, -4000};
        int16_t out[8] = {};
        fx->process(in, out, 4);
        for (int i = 0; i < 8; ++i) QCOMPARE(int(out[i]), int(in[i]) / 2);          // the default of the control is 0.5
        QVERIFY(fx->setControl("GAIN", 1.0f));
        fx->process(in, out, 4);
        for (int i = 0; i < 8; ++i) QCOMPARE(int(out[i]), int(in[i]));
        QVERIFY(!fx->setControl("nothing like this", 1.0f));
    }
    void inPlaceAndClipping()
    {
        auto fx = LadspaEffect::create({"testgain"}, 44100, QStringLiteral(VDJ_LADSPA_TEST_DIR));
        QVERIFY(fx);
        fx->setControl("gain", 4.0f);
        int16_t buf[4] = {20000, -20000, 100, -100};
        fx->process(buf, buf, 2);
        QCOMPARE(int(buf[0]), 32767);
        QCOMPARE(int(buf[1]), -32767);
        QCOMPARE(int(buf[2]), 400);
    }
    void missingLabelReportsWhere()
    {
        QString err;
        auto fx = LadspaEffect::create({"nope"}, 44100, QStringLiteral(VDJ_LADSPA_TEST_DIR), &err);
        QVERIFY(!fx);
        QVERIFY(err.contains("nope"));
    }
    void realCompressorIsOptional()
    {
        // only a smoke test: when a compressor plugin is installed it must load and run
        if (!LadspaEffect::compressorAvailable()) QSKIP("no LADSPA compressor installed");
        auto fx = LadspaEffect::createCompressor(44100);
        QVERIFY(fx);
        int16_t buf[2048];
        for (int i = 0; i < 1024; ++i) { buf[2 * i] = int16_t(20000 * std::sin(i * 0.05)); buf[2 * i + 1] = buf[2 * i]; }
        fx->process(buf, buf, 1024);
    }
};

QTEST_APPLESS_MAIN(TestLadspaHost)
#include "test_ladspa_host.moc"
