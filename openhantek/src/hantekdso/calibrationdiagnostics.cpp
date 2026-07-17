// SPDX-License-Identifier: GPL-2.0-or-later

// CAL_DIAG_TEMP: This entire file is temporary and must not be merged into main.

#include "calibrationdiagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>


QString CalibrationDiagnostics::logFilePath() {
    const QString logDirectory = QDir::home().filePath( "Hantek6022BL/OpenHantek6022/diagnostic-logs" );
    return QDir( logDirectory ).filePath( "offset-calibration-diagnostic.log" );
}


bool CalibrationDiagnostics::begin( const QString &modelName, const QString &serialNumber, const QString &calibrationFile,
                                    const QString &calibrationSource, const void *calibrationBytes,
                                    std::size_t calibrationByteCount ) {
    const QString path = logFilePath();
    if ( !QDir().mkpath( QFileInfo( path ).absolutePath() ) )
        return false;

    {
        QMutexLocker< QMutex > locker( &mutex );
        if ( logFile.isOpen() )
            logFile.close();
        logFile.setFileName( path );
        if ( !logFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
            return false;
    }

    const QByteArray bytes( static_cast< const char * >( calibrationBytes ), int( calibrationByteCount ) );
    writeLine( "BEGIN diagnostic=true persistence=dry-run" );
    writeLine( QString( "APPLICATION name=\"%1\" version=\"%2\"" )
                   .arg( QCoreApplication::applicationName(), QCoreApplication::applicationVersion() ) );
    writeLine( QString( "DEVICE model=\"%1\" serial=\"%2\"" ).arg( modelName, serialNumber ) );
    writeLine( QString( "SOURCE mode=\"%1\" calibrationFile=\"%2\"" ).arg( calibrationSource, calibrationFile ) );
    writeLine( QString( "EFFECTIVE_CALIBRATION_BASELINE byteCount=%1 bytes=\"%2\"" )
                   .arg( qulonglong( calibrationByteCount ) )
                   .arg( QString::fromLatin1( bytes.toHex( ' ' ) ) ) );
    return true;
}


void CalibrationDiagnostics::logCorrection( unsigned gainIndex, unsigned channel, double offsetCorrection,
                                              double gainCorrection ) {
    QString line;
    QTextStream stream( &line );
    stream.setLocale( QLocale::c() );
    stream << "INI_CORRECTION gainIndex=" << gainIndex << " channel=" << channel << " offset=" << offsetCorrection
           << " gain=" << gainCorrection;
    writeLine( line );
}


void CalibrationDiagnostics::logFrame( unsigned frameTag, unsigned channel, unsigned gainIndex, unsigned gainValue,
                                       double samplerate, unsigned oversampling, unsigned resultSamples, unsigned skippedSamples,
                                       int minimum, int maximum, double liveOffset, double existingOffset, double proposedOffset,
                                       uint8_t proposedRaw, uint8_t proposedFine, const QString &decision ) {
    QString line;
    QTextStream stream( &line );
    stream.setLocale( QLocale::c() );
    stream << "FRAME tag=" << frameTag << " channel=" << channel << " gainIndex=" << gainIndex << " gainValue=" << gainValue
           << " samplerate=" << samplerate << " oversampling=" << oversampling << " samples=" << resultSamples
           << " skipped=" << skippedSamples << " minimum=" << minimum << " maximum=" << maximum
           << " spread=" << maximum - minimum << " liveOffset=" << liveOffset << " existingOffset=" << existingOffset
           << " proposedOffset=" << proposedOffset << " encodedRaw=" << unsigned( proposedRaw )
           << " encodedFine=" << unsigned( proposedFine ) << " decision=" << decision;
    writeLine( line );
}


void CalibrationDiagnostics::note( const QString &message ) { writeLine( "NOTE " + message ); }


void CalibrationDiagnostics::writeLine( const QString &message ) {
    QMutexLocker< QMutex > locker( &mutex );
    if ( !logFile.isOpen() )
        return;

    const QString timestamp = QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
    const quintptr threadId = reinterpret_cast< quintptr >( QThread::currentThreadId() );
    const QByteArray line = QString( "%1 thread=0x%2 %3\n" ).arg( timestamp, QString::number( threadId, 16 ), message ).toUtf8();
    logFile.write( line );
    logFile.flush();
}
