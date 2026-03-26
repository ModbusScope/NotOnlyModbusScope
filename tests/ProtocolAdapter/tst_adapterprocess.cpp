#include "tst_adapterprocess.h"

#include "ProtocolAdapter/adapterprocess.h"

#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

void TestAdapterProcess::init()
{
}

void TestAdapterProcess::cleanup()
{
}

void TestAdapterProcess::notRunningByDefault()
{
    AdapterProcess process;
    QVERIFY(!process.isRunning());
}

void TestAdapterProcess::startFailsWithBadPath()
{
    AdapterProcess process;
    QSignalSpy spyError(&process, &AdapterProcess::processError);

    bool result = process.start(QStringLiteral("./nonexistent_adapter_binary_xyz"));

    QVERIFY(!result);
    QVERIFY(!process.isRunning());
    QCOMPARE(spyError.count(), 1);
}

void TestAdapterProcess::sendRequestEmitsResponseReceived()
{
    AdapterProcess process;
    QSignalSpy spyResponse(&process, &AdapterProcess::responseReceived);
    QSignalSpy spyProcessError(&process, &AdapterProcess::processError);

    bool started = process.start(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE));
    QVERIFY2(started, "dummymodbusadapter failed to start");

    process.sendRequest(QStringLiteral("adapter.describe"), QJsonObject());

    /* Close the write channel so the adapter flushes its responses and exits */
    process.stop();

    QCOMPARE(spyProcessError.count(), 0);
    QCOMPARE(spyResponse.count(), 1);
    QList<QVariant> args = spyResponse.at(0);
    QCOMPARE(args.at(1).toString(), QStringLiteral("adapter.describe"));
    QJsonValue resultValue = args.at(2).value<QJsonValue>();
    QVERIFY(resultValue.isObject());
    QJsonObject result = resultValue.toObject();
    QVERIFY(result.contains(QStringLiteral("name")));
}

void TestAdapterProcess::processFinishedEmittedOnStop()
{
    AdapterProcess process;
    QSignalSpy spyFinished(&process, &AdapterProcess::processFinished);

    QVERIFY(process.start(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE)));
    QVERIFY(process.isRunning());

    process.stop();

    QVERIFY(!process.isRunning());
    QCOMPARE(spyFinished.count(), 1);
}

void TestAdapterProcess::isRunningAfterStart()
{
    AdapterProcess process;

    QVERIFY(!process.isRunning());
    QVERIFY(process.start(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE)));
    QVERIFY(process.isRunning());

    process.stop();
    QVERIFY(!process.isRunning());
}

void TestAdapterProcess::sendRequestBeforeStartEmitsError()
{
    AdapterProcess process;
    QSignalSpy spyError(&process, &AdapterProcess::processError);

    int id = process.sendRequest(QStringLiteral("adapter.describe"), QJsonObject());

    QCOMPARE(id, -1);
    QCOMPARE(spyError.count(), 1);
    QVERIFY(spyError.at(0).at(0).toString().contains("not running"));
}

void TestAdapterProcess::stopWhenNotRunningIsNoOp()
{
    AdapterProcess process;
    QSignalSpy spyError(&process, &AdapterProcess::processError);
    QSignalSpy spyFinished(&process, &AdapterProcess::processFinished);

    QVERIFY(!process.isRunning());
    process.stop();

    /* stop() on an idle process must not emit processError or processFinished */
    QCOMPARE(spyError.count(), 0);
    QCOMPARE(spyFinished.count(), 0);
}

void TestAdapterProcess::startAlreadyRunningReturnsTrue()
{
    AdapterProcess process;

    QVERIFY(process.start(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE)));
    QVERIFY(process.isRunning());

    /* A second start() call while running must return true without spawning a new process */
    bool result = process.start(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE));
    QVERIFY(result);
    QVERIFY(process.isRunning());

    process.stop();
}

QTEST_GUILESS_MAIN(TestAdapterProcess)