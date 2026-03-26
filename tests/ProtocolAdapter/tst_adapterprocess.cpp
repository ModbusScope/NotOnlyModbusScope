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

/**
 * Verifies that sending an "adapter.describe" request causes a single responseReceived
 * signal whose payload is a JSON object containing a "name" field.
 *
 * Starts the adapter process using DUMMY_ADAPTER_EXECUTABLE, sends the
 * "adapter.describe" request, stops the process to flush responses, and asserts
 * that no processError was emitted, exactly one responseReceived was emitted,
 * the response's method equals "adapter.describe", and the response result is a
 * JSON object containing the "name" key.
 */
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

/**
 * @brief Verifies that stopping an AdapterProcess emits the processFinished signal.
 *
 * Starts a dummy adapter executable, confirms the process becomes running, stops it,
 * and asserts the process is no longer running and that exactly one processFinished
 * signal was emitted.
 */
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

QTEST_GUILESS_MAIN(TestAdapterProcess)
