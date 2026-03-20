#include "tst_adapterclient.h"

#include "ProtocolAdapter/adapterclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

/* ---- Test helper subclass ---- */

class TestableAdapterClient : public AdapterClient
{
public:
    using AdapterClient::AdapterClient;
    using AdapterClient::State;

    //! Force the internal state machine into a given state.
    void forceState(State s)
    {
        _state = s;
    }

    //! Register a pending request so a crafted response can be matched.
    //! Also advances the internal ID counter so subsequent sendRequest() calls do not reuse the seeded id.
    void forceAddPending(int id, const QString& method)
    {
        _pendingMethods.insert(id, method);
        if (id >= _nextRequestId)
        {
            _nextRequestId = id + 1;
        }
    }

    //! Inject a raw JSON body as if it arrived from the adapter process stdout.
    void injectMessage(const QByteArray& body)
    {
        onMessageReceived(body);
    }
};

/* ---- Helpers ---- */

static QByteArray makeResponse(int id, const QJsonObject& result)
{
    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["result"] = result;
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

static QByteArray makeErrorResponse(int id, int code, const QString& message)
{
    QJsonObject error;
    error["code"] = code;
    error["message"] = message;

    QJsonObject msg;
    msg["jsonrpc"] = "2.0";
    msg["id"] = id;
    msg["error"] = error;
    return QJsonDocument(msg).toJson(QJsonDocument::Compact);
}

/* ---- Tests ---- */

void TestAdapterClient::init()
{
}

void TestAdapterClient::cleanup()
{
}

void TestAdapterClient::lifecycleInitializeToStart()
{
    TestableAdapterClient client;
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);

    /* Seed the first request ID that initialize will use */
    client.forceState(TestableAdapterClient::State::INITIALIZING);
    client.forceAddPending(1, "adapter.initialize");

    /* initialize ok → implementation auto-sends configure (id=2) */
    client.injectMessage(makeResponse(1, QJsonObject{ { "status", "ok" } }));
    QCOMPARE(spyStarted.count(), 0);

    /* configure ok → implementation auto-sends start (id=3) */
    client.injectMessage(makeResponse(2, QJsonObject{ { "status", "ok" } }));
    QCOMPARE(spyStarted.count(), 0);

    /* start ok → sessionStarted emitted */
    client.injectMessage(makeResponse(3, QJsonObject{ { "status", "ok" } }));
    QCOMPARE(spyStarted.count(), 1);
    QCOMPARE(spyError.count(), 0);
}

void TestAdapterClient::readDataValidResults()
{
    TestableAdapterClient client;
    QSignalSpy spy(&client, &AdapterClient::readDataResult);

    client.forceState(TestableAdapterClient::State::ACTIVE);
    client.forceAddPending(1, "adapter.readData");

    QJsonArray registers;
    registers.append(QJsonObject{ { "valid", true }, { "value", 42.0 } });
    registers.append(QJsonObject{ { "valid", false }, { "value", 0.0 } });

    client.injectMessage(makeResponse(1, QJsonObject{ { "registers", registers } }));

    QCOMPARE(spy.count(), 1);
    ResultDoubleList results = spy.at(0).at(0).value<ResultDoubleList>();
    QCOMPARE(results.size(), 2);
    QVERIFY(results[0].isValid());
    QCOMPARE(results[0].value(), 42.0);
    QVERIFY(!results[1].isValid());
}

void TestAdapterClient::readDataEmptyRegisters()
{
    TestableAdapterClient client;
    QSignalSpy spy(&client, &AdapterClient::readDataResult);

    client.forceState(TestableAdapterClient::State::ACTIVE);
    client.forceAddPending(1, "adapter.readData");

    client.injectMessage(makeResponse(1, QJsonObject{ { "registers", QJsonArray{} } }));

    QCOMPARE(spy.count(), 1);
    ResultDoubleList results = spy.at(0).at(0).value<ResultDoubleList>();
    QCOMPARE(results.size(), 0);
}

void TestAdapterClient::errorResponseEmitsSessionError()
{
    TestableAdapterClient client;
    QSignalSpy spy(&client, &AdapterClient::sessionError);

    client.forceState(TestableAdapterClient::State::INITIALIZING);
    client.forceAddPending(1, "adapter.initialize");

    client.injectMessage(makeErrorResponse(1, -32602, "bad params"));

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toString().contains("bad params"));
}

void TestAdapterClient::unexpectedResponseId()
{
    TestableAdapterClient client;
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);
    QSignalSpy spyData(&client, &AdapterClient::readDataResult);

    client.forceState(TestableAdapterClient::State::ACTIVE);
    /* No pending request for id 99 */
    client.injectMessage(makeResponse(99, QJsonObject{ { "status", "ok" } }));

    QCOMPARE(spyStarted.count(), 0);
    QCOMPARE(spyError.count(), 0);
    QCOMPARE(spyData.count(), 0);
}

void TestAdapterClient::invalidJson()
{
    TestableAdapterClient client;
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);

    client.forceState(TestableAdapterClient::State::ACTIVE);
    client.injectMessage(QByteArray("not valid json {{{{"));

    QCOMPARE(spyStarted.count(), 0);
    QCOMPARE(spyError.count(), 0);
}

void TestAdapterClient::notificationIgnored()
{
    TestableAdapterClient client;
    QSignalSpy spyStarted(&client, &AdapterClient::sessionStarted);
    QSignalSpy spyError(&client, &AdapterClient::sessionError);
    QSignalSpy spyData(&client, &AdapterClient::readDataResult);

    client.forceState(TestableAdapterClient::State::ACTIVE);

    /* A notification has a "method" field and no "id" */
    QJsonObject notification;
    notification["jsonrpc"] = "2.0";
    notification["method"] = "adapter.diagnostic";
    notification["params"] = QJsonObject{ { "level", "info" }, { "message", "hello" } };
    client.injectMessage(QJsonDocument(notification).toJson(QJsonDocument::Compact));

    QCOMPARE(spyStarted.count(), 0);
    QCOMPARE(spyError.count(), 0);
    QCOMPARE(spyData.count(), 0);
}

QTEST_GUILESS_MAIN(TestAdapterClient)
