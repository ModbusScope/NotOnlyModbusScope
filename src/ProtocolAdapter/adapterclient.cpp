#include "ProtocolAdapter/adapterclient.h"

#include "util/scopelogging.h"

#include <QJsonArray>
#include <QJsonDocument>

static constexpr QLatin1StringView cAdapterPath{ "./modbusadapter" };

AdapterClient::AdapterClient(QObject* parent) : QObject(parent)
{
    _pProcess = new QProcess(this);
    _pFramingReader = new FramingReader(this);

    connect(_pProcess, &QProcess::readyReadStandardOutput, this, &AdapterClient::onReadyReadStdout);
    connect(_pProcess, &QProcess::readyReadStandardError, this, &AdapterClient::onReadyReadStderr);
    connect(_pProcess, &QProcess::errorOccurred, this, &AdapterClient::onProcessError);
    connect(_pProcess, &QProcess::finished, this, &AdapterClient::onProcessFinished);
    connect(_pFramingReader, &FramingReader::messageReceived, this, &AdapterClient::onMessageReceived);
}

AdapterClient::~AdapterClient()
{
    if (_pProcess->state() != QProcess::NotRunning)
    {
        _pProcess->kill();
        _pProcess->waitForFinished(1000);
    }
}

void AdapterClient::startSession(QJsonObject config, QStringList registerExpressions)
{
    if (_state != State::IDLE)
    {
        qCWarning(scopeComm) << "AdapterClient: startSession called in non-idle state";
        return;
    }

    _pendingConfig = config;
    _pendingExpressions = registerExpressions;
    _pendingMethods.clear();
    _nextRequestId = 1;

    _pProcess->start(cAdapterPath, QStringList());
    if (!_pProcess->waitForStarted(3000))
    {
        emit sessionError(QString("Failed to start adapter process: %1").arg(cAdapterPath));
        return;
    }

    qCInfo(scopeComm) << "AdapterClient: process started, sending initialize";
    _state = State::INITIALIZING;
    sendRequest("adapter.initialize", QJsonObject());
}

void AdapterClient::requestReadData()
{
    if (_state != State::ACTIVE)
    {
        qCWarning(scopeComm) << "AdapterClient: requestReadData called in non-active state";
        return;
    }

    sendRequest("adapter.readData", QJsonObject());
}

void AdapterClient::stopSession()
{
    if (_state == State::IDLE)
    {
        return;
    }

    if (_state == State::ACTIVE || _state == State::STARTING)
    {
        _state = State::STOPPING;
        sendRequest("adapter.shutdown", QJsonObject());
    }
    else
    {
        _pProcess->kill();
        _state = State::IDLE;
    }
}

int AdapterClient::sendRequest(const QString& method, const QJsonObject& params)
{
    int id = _nextRequestId++;

    QJsonObject request;
    request["jsonrpc"] = "2.0";
    request["id"] = id;
    request["method"] = method;
    request["params"] = params;

    QByteArray json = QJsonDocument(request).toJson(QJsonDocument::Compact);
    writeFramed(json);

    _pendingMethods.insert(id, method);
    return id;
}

void AdapterClient::writeFramed(const QByteArray& json)
{
    QByteArray frame = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    _pProcess->write(frame);
}

void AdapterClient::onReadyReadStdout()
{
    QByteArray data = _pProcess->readAllStandardOutput();
    _pFramingReader->feed(data);
}

void AdapterClient::onReadyReadStderr()
{
    QByteArray data = _pProcess->readAllStandardError();
    qCDebug(scopeComm) << "Adapter stderr:" << QString::fromUtf8(data).trimmed();
}

void AdapterClient::onMessageReceived(QByteArray body)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

    if (parseError.error != QJsonParseError::NoError)
    {
        qCWarning(scopeComm) << "AdapterClient: JSON parse error:" << parseError.errorString();
        return;
    }

    QJsonObject msg = doc.object();

    if (msg.contains("method"))
    {
        /* Notification — not handled yet */
        qCDebug(scopeComm) << "AdapterClient: notification:" << msg["method"].toString();
        return;
    }

    handleResponse(msg);
}

void AdapterClient::handleResponse(const QJsonObject& msg)
{
    int id = msg["id"].toInt(-1);
    if (id < 0 || !_pendingMethods.contains(id))
    {
        qCWarning(scopeComm) << "AdapterClient: unexpected response id:" << id;
        return;
    }

    QString method = _pendingMethods.take(id);

    if (msg.contains("error"))
    {
        QString errorMsg = msg["error"].toObject().value("message").toString();
        qCWarning(scopeComm) << "AdapterClient: error for" << method << ":" << errorMsg;
        emit sessionError(QString("Adapter error on %1: %2").arg(method, errorMsg));
        return;
    }

    QJsonObject result = msg["result"].toObject();
    handleLifecycleResponse(method, result);
}

void AdapterClient::handleLifecycleResponse(const QString& method, const QJsonObject& result)
{
    if (method == "adapter.initialize" && _state == State::INITIALIZING)
    {
        qCInfo(scopeComm) << "AdapterClient: initialized, sending configure";
        _state = State::CONFIGURING;
        QJsonObject params;
        params["config"] = _pendingConfig;
        sendRequest("adapter.configure", params);
    }
    else if (method == "adapter.configure" && _state == State::CONFIGURING)
    {
        qCInfo(scopeComm) << "AdapterClient: configured, sending start";
        _state = State::STARTING;
        QJsonObject params;
        params["registers"] = QJsonArray::fromStringList(_pendingExpressions);
        sendRequest("adapter.start", params);
    }
    else if (method == "adapter.start" && _state == State::STARTING)
    {
        qCInfo(scopeComm) << "AdapterClient: started";
        _state = State::ACTIVE;
        emit sessionStarted();
    }
    else if (method == "adapter.readData" && _state == State::ACTIVE)
    {
        ResultDoubleList results;
        const QJsonArray registers = result["registers"].toArray();
        for (const QJsonValue& entry : registers)
        {
            QJsonObject reg = entry.toObject();
            if (reg["valid"].toBool())
            {
                results.append(ResultDouble(reg["value"].toDouble(), ResultState::State::SUCCESS));
            }
            else
            {
                results.append(ResultDouble(0.0, ResultState::State::INVALID));
            }
        }
        emit readDataResult(results);
    }
    else if (method == "adapter.shutdown" && _state == State::STOPPING)
    {
        qCInfo(scopeComm) << "AdapterClient: shutdown acknowledged";
        _pProcess->waitForFinished(2000);
        _state = State::IDLE;
    }
    else
    {
        qCWarning(scopeComm) << "AdapterClient: unexpected response for" << method << "in state"
                             << static_cast<int>(_state);
    }
}

void AdapterClient::onProcessError(QProcess::ProcessError error)
{
    qCWarning(scopeComm) << "AdapterClient: process error:" << error;
    _state = State::IDLE;
    emit sessionError(QString("Adapter process error: %1").arg(static_cast<int>(error)));
}

void AdapterClient::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    qCInfo(scopeComm) << "AdapterClient: process finished, exit code:" << exitCode << "status:" << exitStatus;
    if (_state != State::IDLE)
    {
        _state = State::IDLE;
        emit sessionError("Adapter process exited unexpectedly");
    }
}
