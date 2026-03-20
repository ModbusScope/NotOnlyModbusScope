#ifndef TST_ADAPTERCLIENT_H
#define TST_ADAPTERCLIENT_H

#include <QObject>

class TestAdapterClient : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void lifecycleInitializeToStart();
    void readDataValidResults();
    void readDataEmptyRegisters();
    void errorResponseEmitsSessionError();
    void unexpectedResponseId();
    void invalidJson();
    void notificationIgnored();
};

#endif // TST_ADAPTERCLIENT_H
