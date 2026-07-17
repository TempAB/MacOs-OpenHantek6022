// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// CAL_DIAG_TEMP: This entire file is temporary and must not be merged into main.

#include <QFile>
#include <QMutex>
#include <QString>

#include <cstddef>
#include <cstdint>


class CalibrationDiagnostics {
  public:
    static QString logFilePath();

    bool begin( const QString &modelName, const QString &serialNumber, const QString &calibrationFile,
                const QString &calibrationSource, const void *calibrationBytes, std::size_t calibrationByteCount );
    void logCorrection( unsigned gainIndex, unsigned channel, double offsetCorrection, double gainCorrection );
    void logFrame( unsigned frameTag, unsigned channel, unsigned gainIndex, unsigned gainValue, double samplerate,
                   unsigned oversampling, unsigned resultSamples, unsigned skippedSamples, int minimum, int maximum,
                   double liveOffset, double existingOffset, double proposedOffset, uint8_t proposedRaw,
                   uint8_t proposedFine, const QString &decision );
    void note( const QString &message );

  private:
    void writeLine( const QString &message );

    QFile logFile;
    QMutex mutex;
};
