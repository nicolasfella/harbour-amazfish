#include "sportsdetailoperation.h"

#include <QDebug>
#include <QDataStream>

#include "mibandservice.h"
#include "typeconversion.h"
#include "activitykind.h"
#include "bipactivitydetailparser.h"
#include "amazfishconfig.h"

SportsDetailOperation::SportsDetailOperation(QBLEService *service, KDbConnection *conn, const ActivitySummary &summary) : AbstractFetchOperation(service), m_summary(summary)
{
    m_conn = conn;
    setLastSyncKey("device/lastsportsyncmillis");
}

void SportsDetailOperation::start()
{
    setStartDate(lastActivitySync());
    m_lastPacketCounter = -1;

    QByteArray rawDate = TypeConversion::dateTimeToBytes(startDate().toUTC(), 0, false);

    m_service->enableNotification(MiBandService::UUID_CHARACTERISTIC_MIBAND_ACTIVITY_DATA);
    m_service->enableNotification(MiBandService::UUID_CHARACTERISTIC_MIBAND_FETCH_DATA);

    //Send log read configuration
    m_service->writeValue(MiBandService::UUID_CHARACTERISTIC_MIBAND_FETCH_DATA, QByteArray(1, MiBandService::COMMAND_ACTIVITY_DATA_START_DATE) + QByteArray(1, MiBandService::COMMAND_ACTIVITY_DATA_TYPE_SPORTS_DETAILS) + rawDate);
}

void SportsDetailOperation::handleData(const QByteArray &data)
{
    if (data.length() < 2) {
        qDebug() << "unexpected sports summary data length: " << data.length();
        return;
    }

    if ((m_lastPacketCounter + 1) == (uint8_t)data[0] ) {
        m_lastPacketCounter++;
        if (m_lastPacketCounter >= 255) {
            m_lastPacketCounter =-1;
        }
        m_buffer += data.mid(1);
    } else {
        qDebug() << "invalid package counter: " << (uint8_t)data[0] << ", last was: " << m_lastPacketCounter;
        finished(false);
        return;
    }
}

bool SportsDetailOperation::finished(bool success)
{
    bool saved = false;
    if (success) {
        BipActivityDetailParser parser(m_summary);
        parser.parse(m_buffer);
        m_gpx = parser.toText();

        QDir cachelocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString filename = m_summary.name() + ".gpx";
        QString tcx = m_summary.name() + ".tcx";
        QFile logFile;

        QDir laufhelden(QDir::homePath() + "/Laufhelden/");

        qDebug() << "Checking for " << laufhelden.absolutePath();

        if (laufhelden.exists()) {
            logFile.setFileName(laufhelden.absolutePath() + "/" + filename);
        } else {
            logFile.setFileName(cachelocation.absolutePath() + "/" + filename);
        }

        if(logFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Saving to" << logFile.fileName();
            QTextStream stream( &logFile );
            stream << m_gpx;
        }
        logFile.close();

        //Saving TCX
        if (laufhelden.exists()) {
            logFile.setFileName(laufhelden.absolutePath() + "/" + tcx);
        } else {
            logFile.setFileName(cachelocation.absolutePath() + "/" + tcx);
        }

        if(logFile.open(QIODevice::WriteOnly)) {
            qDebug() << "Saving to" << logFile.fileName();
            QTextStream stream( &logFile );
            stream << parser.toTCX();
        }
        logFile.close();

        saved = saveSport(logFile.fileName());

        qDebug() << "End sport time is:" << m_summary.endTime() << m_summary.endTime().toMSecsSinceEpoch();
        m_summary.endTime().setTimeSpec(Qt::LocalTime);
        saveLastActivitySync(m_summary.endTime().toMSecsSinceEpoch());
    } else {
        setAbort(true);
    }
    return saved;
}

bool SportsDetailOperation::saveSport(const QString &filename)
{
    if (!m_conn || !m_conn->isDatabaseUsed()) {
        qDebug() << "Database not connected";
        return false;
    }

    auto config = AmazfishConfig::instance();
    uint id = qHash(config->profileName());
    uint devid = qHash(config->pairedAddress());

    m_summary.setProfileId(id);
    m_summary.setDeviceId(devid);
    m_summary.setGPX(filename);

    return m_summary.saveToDatabase(m_conn);
}
