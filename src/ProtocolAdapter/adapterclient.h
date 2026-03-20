#ifndef ADAPTERCLIENT_H
#define ADAPTERCLIENT_H

#include "ProtocolAdapter/framingreader.h"
#include "util/result.h"

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QStringList>

/*!
 * \brief Client for an external adapter process communicating via JSON-RPC 2.0 over stdio.
 *
 * Manages the adapter process lifecycle:
 *   startSession() → initialize → configure → start → sessionStarted()
 *   requestReadData() → readData → readDataResult()
 *   stopSession() → shutdown → process exit
 *
 * All messages use Content-Length framing (as used in the Language Server Protocol).
 */
class AdapterClient : public QObject
{
    Q_OBJECT

public:
    explicit AdapterClient(QObject* parent = nullptr);
    ~AdapterClient();

    /*!
     * \brief Launch the adapter process and run the initialization lifecycle.
     *
     * Starts the adapter executable, then sequentially sends adapter.initialize,
     * adapter.configure, and adapter.start. Emits sessionStarted() on success.
     * \param config       JSON object passed as the \c config param to adapter.configure.
     * \param registerExpressions Register expression strings passed to adapter.start.
     */
    void startSession(QJsonObject config, QStringList registerExpressions);

    /*!
     * \brief Send an adapter.readData request to the active adapter.
     *
     * Must only be called after sessionStarted() has been emitted.
     * Emits readDataResult() when the adapter responds.
     */
    void requestReadData();

    /*!
     * \brief Send adapter.shutdown and terminate the adapter process.
     */
    void stopSession();

signals:
    /*!
     * \brief Emitted when the adapter has been initialized, configured, and started.
     */
    void sessionStarted();

    /*!
     * \brief Emitted when an adapter.readData response has been received.
     * \param results One entry per register, in the same order as the expressions passed to startSession().
     */
    void readDataResult(ResultDoubleList results);

    /*!
     * \brief Emitted when an unrecoverable error occurs (process failure, RPC error).
     * \param message Human-readable error description.
     */
    void sessionError(QString message);

protected slots:
    void onMessageReceived(QByteArray body);

protected:
    enum class State
    {
        IDLE,
        INITIALIZING,
        CONFIGURING,
        STARTING,
        ACTIVE,
        STOPPING
    };

    QMap<int, QString> _pendingMethods;
    State _state{ State::IDLE };
    int _nextRequestId{ 1 };

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    int sendRequest(const QString& method, const QJsonObject& params);
    void writeFramed(const QByteArray& json);
    void handleResponse(const QJsonObject& msg);
    void handleLifecycleResponse(const QString& method, const QJsonObject& result);

    QProcess* _pProcess;
    FramingReader* _pFramingReader;
    QJsonObject _pendingConfig;
    QStringList _pendingExpressions;
};

#endif // ADAPTERCLIENT_H
