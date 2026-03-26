#include "tst_dummyadapter.h"

#include "ProtocolAdapter/adapterclient.h"
#include "ProtocolAdapter/adapterprocess.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

namespace {

constexpr int cSessionTimeoutMs = 10000;
constexpr int cReadTimeoutMs = 5000;

//! Minimal adapter configuration with no real connections or devices.
//! The dummymodbusadapter accepts this without attempting any real I/O.
QJsonObject minimalConfig()
{
    QJsonObject config;
    config["version"] = 1;
    config["general"] = QJsonObject();
    config["connections"] = QJsonArray();
    config["devices"] = QJsonArray();
    return config;
}

} /**
 * @brief Verify that the adapter's describe result contains required top-level fields.
 *
 * Waits for the AdapterClient::describeResult signal and asserts the returned JSON object
 * contains the keys "name", "version", "configVersion", "schema", "defaults", and "capabilities".
 * Also asserts that no sessionStarted or sessionError signals were emitted during describe.
 */

void TestDummyAdapter::describeReturnsRequiredFields()
{
    auto* process = new AdapterProcess();
    AdapterClient client(process);

    QSignalSpy spyDescribe(&client, &AdapterClient::describeResult);
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);

    client.prepareAdapter(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE));

    QVERIFY2(spyDescribe.wait(cSessionTimeoutMs), "No describeResult signal received");
    QCOMPARE(spyError.count(), 0);
    QCOMPARE(spyStarted.count(), 0);

    QJsonObject result = spyDescribe.at(0).at(0).value<QJsonObject>();

    QVERIFY2(result.contains("name"), "describe result missing 'name'");
    QVERIFY2(result.contains("version"), "describe result missing 'version'");
    QVERIFY2(result.contains("configVersion"), "describe result missing 'configVersion'");
    QVERIFY2(result.contains("schema"), "describe result missing 'schema'");
    QVERIFY2(result.contains("defaults"), "describe result missing 'defaults'");
    QVERIFY2(result.contains("capabilities"), "describe result missing 'capabilities'");

    client.stopSession();
}

/**
 * @brief Verifies the adapter's describe result reports the expected adapter name.
 *
 * Waits for the adapter to emit its describe result and asserts that the "name"
 * field equals "modbusAdapter". Stops the session afterward.
 */
void TestDummyAdapter::describeNameIsModbusAdapter()
{
    auto* process = new AdapterProcess();
    AdapterClient client(process);

    QSignalSpy spyDescribe(&client, &AdapterClient::describeResult);
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);

    client.prepareAdapter(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE));

    QVERIFY(spyDescribe.wait(cSessionTimeoutMs));
    QCOMPARE(spyStarted.count(), 0);

    QJsonObject result = spyDescribe.at(0).at(0).value<QJsonObject>();
    QCOMPARE(result["name"].toString(), QStringLiteral("modbusAdapter"));

    client.stopSession();
}

/**
 * @brief Exercises a full adapter session lifecycle from describe through read and stop.
 *
 * Starts an adapter process, supplies a minimal configuration when the adapter's describe
 * result is received, waits for the session to start (failing the test on timeout or error),
 * issues a read request and waits for a read result, then stops the session.
 */
void TestDummyAdapter::fullLifecycleSessionStarts()
{
    auto* process = new AdapterProcess();
    AdapterClient client(process);

    /* Provide config when describe result arrives so the lifecycle can continue */
    connect(&client, &AdapterClient::describeResult, &client,
            [&client]() { client.provideConfig(minimalConfig(), QStringList()); });

    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);

    client.prepareAdapter(QString::fromUtf8(DUMMY_ADAPTER_EXECUTABLE));

    QVERIFY2(spyStarted.wait(cSessionTimeoutMs), "sessionStarted not emitted");
    QCOMPARE(spyError.count(), 0);

    QSignalSpy spyData(&client, &AdapterClient::readDataResult);
    client.requestReadData();
    QVERIFY2(spyData.wait(cReadTimeoutMs), "readDataResult not emitted");

    client.stopSession();
}

QTEST_GUILESS_MAIN(TestDummyAdapter)
