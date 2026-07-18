// SPDX-License-Identifier: GPL-2.0-or-later

// #define TIMESTAMPDEBUG

#include <QCryptographicHash>
#include <QDebug>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

#include <QtCore>

#include <algorithm>
#include <cmath>

#include "hantekdsocontrol.h"
#include "mathchannel.h"
#include "scopesettings.h"
#include "usb/scopedevice.h"

using namespace Hantek;
using namespace Dso;

constexpr double HantekDsoControl::EEPROM_NULL_HALF_WIDTH_DEFAULT;
constexpr double HantekDsoControl::EEPROM_NULL_HALF_WIDTH_MIN;
constexpr double HantekDsoControl::EEPROM_NULL_HALF_WIDTH_MAX;

struct EEPROMCalibrationBundle {
    QString outputDirectory;
    QString reportPath;
    QString originalPath;
    QString candidatePath;
    QString iniSnapshotPath;
    QString checksumsPath;
    QString timestamp;
    QString deviceModel;
    QString deviceSerial;
    QByteArray originalBytes;
    QByteArray candidateBytes;
    QByteArray iniContents;
    QByteArray preparationReport;
    double nullHalfWidth = HantekDsoControl::EEPROM_NULL_HALF_WIDTH_DEFAULT;
    unsigned suppressedResidualCount = 0;
    unsigned appliedResidualCount = 0;
    bool candidateDiffers = false;
    double persistedOffset[ HANTEK_GAIN_STEPS ][ HANTEK_CHANNEL_NUMBER ] = {};
    double persistedHighSpeedOffset[ HANTEK_GAIN_STEPS ][ HANTEK_CHANNEL_NUMBER ] = {};
};


static bool readCalibrationRegion( ScopeDevice *device, QByteArray &contents, QString &errorMessage ) {
    ControlGetCalibration command;
    const int result = device->controlRead( &command );
    if ( result != int( sizeof( CalibrationValues ) ) ) {
        errorMessage = result < 0 ? libUsbErrorString( result )
                                  : QString( "received %1 of %2 bytes" )
                                        .arg( result )
                                        .arg( int( sizeof( CalibrationValues ) ) );
        return false;
    }

    contents = QByteArray( reinterpret_cast< const char * >( command.data() ), int( sizeof( CalibrationValues ) ) );
    return true;
}


static bool writeLowSpeedCalibrationRegions( ScopeDevice *device, const QByteArray &contents, QString &errorMessage ) {
    if ( contents.size() != int( sizeof( CalibrationValues ) ) ) {
        errorMessage = "the calibration image is not exactly 80 bytes";
        return false;
    }

    struct Chunk {
        int imageOffset;
        int eepromAddress;
    };
    const Chunk chunks[] = { { 0, 8 }, { 8, 16 }, { 48, 56 }, { 56, 64 } };
    for ( const Chunk &chunk : chunks ) {
        QByteArray data = contents.mid( chunk.imageOffset, 8 );
        const int result = device->controlTransfer(
            uint8_t( LIBUSB_REQUEST_TYPE_VENDOR ) | uint8_t( LIBUSB_ENDPOINT_OUT ),
            uint8_t( ControlCode::CONTROL_EEPROM ), reinterpret_cast< unsigned char * >( data.data() ),
            unsigned( data.size() ), chunk.eepromAddress, 0 );
        if ( result != data.size() ) {
            errorMessage =
                result < 0
                    ? libUsbErrorString( result )
                    : QString( "wrote %1 of %2 bytes at EEPROM address 0x%3" )
                          .arg( result )
                          .arg( data.size() )
                          .arg( chunk.eepromAddress, 2, 16, QLatin1Char( '0' ) );
            return false;
        }
        QThread::msleep( 25 ); // allow the EEPROM write cycle to complete before the next aligned chunk
    }
    return true;
}


static bool writeFileAtomically( const QString &targetPath, const QByteArray &contents ) {
    QSaveFile target( targetPath );
    if ( !target.open( QIODevice::WriteOnly ) )
        return false;
    if ( target.write( contents ) != contents.size() ) {
        target.cancelWriting();
        return false;
    }
    if ( !target.commit() )
        return false;

    QFile verification( targetPath );
    return verification.open( QIODevice::ReadOnly ) && verification.readAll() == contents;
}


static bool copyFileAtomically( const QString &sourcePath, const QString &targetPath ) {
    QFile source( sourcePath );
    if ( !source.open( QIODevice::ReadOnly ) )
        return false;
    const QByteArray contents = source.readAll();
    return source.error() == QFileDevice::NoError && writeFileAtomically( targetPath, contents );
}


static QByteArray sha256( const QByteArray &contents ) {
    return QCryptographicHash::hash( contents, QCryptographicHash::Sha256 ).toHex();
}


static QString hexByte( uint8_t value ) {
    return QString( "%1" ).arg( value, 2, 16, QLatin1Char( '0' ) ).toUpper();
}


static double median( std::vector< double > values ) {
    if ( values.empty() )
        return 0.0;

    std::sort( values.begin(), values.end() );
    const size_t middle = values.size() / 2;
    return values.size() % 2 ? values[ middle ] : ( values[ middle - 1 ] + values[ middle ] ) / 2.0;
}


static bool validCalibrationByte( uint8_t value ) { return value != 0 && value != 0xFF; }


static double bytesToOffset( uint8_t offsetRaw, uint8_t offsetFine ) {
    if ( validCalibrationByte( offsetRaw ) && validCalibrationByte( offsetFine ) )
        return offsetRaw + ( offsetFine - 0x80 ) / 250.0;

    return 0x80; // no valid correction: ADC binary-offset centre
}


static bool offsetToBytes( double offset, uint8_t &offsetRaw, uint8_t &offsetFine ) {
    if ( !qIsFinite( offset ) )
        return false;

    const int raw = qRound( offset );
    const int fine = qRound( 0x80 + 250.0 * ( offset - raw ) );
    if ( raw < 1 || raw > 0xFE || fine < 1 || fine > 0xFE )
        return false;

    offsetRaw = uint8_t( raw );
    offsetFine = uint8_t( fine );
    return true;
}


static QString calibrationFilePath( const QString &calibrationName ) {
    QSettings legacySettings( QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(),
                              calibrationName );
    const QString legacyPath = legacySettings.fileName();

#if defined( Q_OS_MAC )
    const QString applicationDataPath = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
    if ( applicationDataPath.isEmpty() )
        return legacyPath;

    const QString calibrationDirectory = QDir( applicationDataPath ).filePath( "Calibration" );
    if ( !QDir().mkpath( calibrationDirectory ) ) {
        qWarning() << "Unable to create calibration directory" << calibrationDirectory << "- using" << legacyPath;
        return legacyPath;
    }

    const QString preferredPath = QDir( calibrationDirectory ).filePath( calibrationName + ".ini" );
    if ( !QFileInfo::exists( preferredPath ) && QFileInfo::exists( legacyPath ) ) {
        if ( !copyFileAtomically( legacyPath, preferredPath ) ) {
            qWarning() << "Unable to migrate calibration file from" << legacyPath << "to" << preferredPath;
            return legacyPath;
        }
        qInfo() << "Migrated calibration file from" << legacyPath << "to" << preferredPath;
    }
    return preferredPath;
#else
    return legacyPath;
#endif
}


HantekDsoControl::HantekDsoControl( ScopeDevice *device, const DSOModel *model, int verboseLevel )
    : verboseLevel( verboseLevel ), scopeDevice( device ), model( model ), specification( model->spec() ),
      controlsettings( &( specification->samplerate.single ), specification->channels ) {

    if ( verboseLevel > 1 )
        qDebug() << " HantekDsoControl::HantekDsoControl()";
    qRegisterMetaType< DSOsamples * >();
    qRegisterMetaType< QList< double > >();

    if ( device && specification->fixedUSBinLength )
        device->overwriteInPacketLength( unsigned( specification->fixedUSBinLength ) );
    // Apply special requirements by the devices model
    model->applyRequirements( this );

    getCalibrationFromIniFile();

    stateMachineRunning = true;
}


HantekDsoControl::~HantekDsoControl() {
    if ( scope && scope->verboseLevel > 1 )
        qDebug() << " HantekDsoControl::~HantekDsoControl()";
    while ( firstControlCommand ) {
        ControlCommand *t = firstControlCommand->next;
        delete firstControlCommand;
        firstControlCommand = t;
    }
}


void HantekDsoControl::prepareForShutdown() {
    if ( verboseLevel > 1 )
        qDebug() << " HDC::prepareForShutdown()";
    offsetRepeatabilityStudyActive = false;
    offsetRepeatabilityStudyFinalizationPending = false;
    if ( offsetCalibrationActive ) {
        memcpy( offsetCorrection, offsetCalibrationOriginal, sizeof( offsetCorrection ) );
        offsetCalibrationActive = false;
    }
}


const ScopeDevice *HantekDsoControl::getDevice() const {
    QReadLocker locker( &scopeDeviceLock );
    return scopeDevice;
}


bool HantekDsoControl::isDeviceAvailable() const {
    QReadLocker locker( &scopeDeviceLock );
    return scopeDevice && deviceConnectionState == DeviceConnectionState::Connected && scopeDevice->isConnected();
}


bool HantekDsoControl::deviceNotConnected() { return !isDeviceAvailable(); }


void HantekDsoControl::restoreTargets() {
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::restoreTargets()";
    if ( controlsettings.samplerate.target.samplerateSet == ControlSettingsSamplerateTarget::Samplerrate )
        setSamplerate();
    else
        setRecordTime();
}


void HantekDsoControl::updateSamplerateLimits() {
    QList< double > sampleSteps;
    double limit = isSingleChannel() ? specification->samplerate.single.max : specification->samplerate.multi.max;

    if ( controlsettings.samplerate.current > limit ) {
        setSamplerate( limit );
    }
    for ( auto &v : specification->fixedSampleRates ) {
        if ( v.samplerate <= limit ) {
            sampleSteps << v.samplerate;
        }
    }
    if ( verboseLevel > 3 )
        qDebug() << "   HDC::updateSamplerateLimits()" << sampleSteps;
    else if ( verboseLevel > 2 )
        qDebug() << "  HDC::updateSamplerateLimits()" << sampleSteps.first() << "..." << sampleSteps.last();
    emit samplerateSet( 1, sampleSteps );
}


void HantekDsoControl::controlSetSamplerate( uint8_t sampleIndex ) {
    static uint8_t lastIndex = 0xFF;
    uint8_t id = specification->fixedSampleRates[ sampleIndex ].id;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::controlSetSamplerate()" << sampleIndex << "id:" << id;
    if ( verboseLevel > 3 )
        qDebug() << "   ThreadID:" << QThread::currentThreadId();
    modifyCommand< ControlSetSamplerate >( ControlCode::CONTROL_SETSAMPLERATE )->setSamplerate( id, sampleIndex );
    if ( sampleIndex != lastIndex ) { // samplerate has changed, start new sampling
        restartSampling();
    }
    lastIndex = sampleIndex;
}


Dso::ErrorCode HantekDsoControl::setSamplerate( double samplerate ) {
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setSamplerate()" << samplerate;
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;

    if ( samplerate == 0.0 ) {
        samplerate = controlsettings.samplerate.target.samplerate;
    } else {
        controlsettings.samplerate.target.samplerate = samplerate;
        controlsettings.samplerate.target.samplerateSet = ControlSettingsSamplerateTarget::Samplerrate;
    }
    uint8_t sampleIndex;
    for ( sampleIndex = 0; sampleIndex < specification->fixedSampleRates.size() - 1; ++sampleIndex ) {
        if ( long( round( specification->fixedSampleRates[ sampleIndex ].samplerate ) ) ==
             long( round( samplerate ) ) ) // dont compare double == double
            break;
    }
    controlSetSamplerate( sampleIndex );
    unsigned oversample = specification->fixedSampleRates[ sampleIndex ].oversampling;
    setDownsampling( oversample );
    controlsettings.samplerate.current = samplerate;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setSamplerate() emit samplerateCalculated" << samplerate << oversample;
    emit samplerateCalculated( samplerate, oversample );
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setRecordTime( double duration ) {
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setRecordTime()" << duration;
    if ( verboseLevel > 3 )
        qDebug() << "   ThreadID:" << QThread::currentThreadId();
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;

    if ( duration == 0.0 ) {
        return Dso::ErrorCode::NONE;
    } else {
        controlsettings.samplerate.target.duration = duration;
        controlsettings.samplerate.target.samplerateSet = ControlSettingsSamplerateTarget::Duration;
    }
    if ( verboseLevel > 2 )
        qDebug() << "  duration =" << duration;

    double srLimit;
    if ( isSingleChannel() )
        srLimit = ( specification->samplerate.single ).max;
    else
        srLimit = ( specification->samplerate.multi ).max;
    // For now - we go for the SAMPLESIZE (= 20000) size sampling, defined in dsosamples.h
    // Find highest samplerate using less equal half of these samples to obtain our duration.
    uint8_t sampleIndex = 0;
    for ( uint8_t iii = 0; iii < specification->fixedSampleRates.size(); ++iii ) {
        double sRate = specification->fixedSampleRates[ iii ].samplerate;
        if ( verboseLevel > 3 )
            qDebug() << "   sampleIndex:" << sampleIndex << "sRate:" << sRate << "sRate*duration:" << sRate * duration;
        // Ensure that at least 1/2 of remaining samples are available for SW trigger algorithm
        // for stability reason avoid the highest sample rate as default
        if ( sRate < srLimit && sRate * duration <= SAMPLESIZE / 2 ) {
            sampleIndex = iii;
        }
    }
    controlSetSamplerate( sampleIndex );
    unsigned oversampling = specification->fixedSampleRates[ sampleIndex ].oversampling;
    setDownsampling( oversampling );
    double samplerate = specification->fixedSampleRates[ sampleIndex ].samplerate;
    controlsettings.samplerate.current = samplerate;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setRecordTime() emit samplerateChanged" << samplerate << oversampling;
    emit samplerateCalculated( samplerate, oversampling );
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setCalFreq( double calfreq ) {
    unsigned int cf;
    if ( calfreq < 1000 ) { // 50, 60, 100, 200, 500 -> 105, 106, 110, 120, 150
        cf = 100 + unsigned( round( calfreq / 10 ) );
        if ( 110 == cf ) // special case for sigrok FW (e.g. DDS120) 100Hz -> 0
            cf = 0;
    } else if ( calfreq <= 5500 && unsigned( calfreq ) % 1000 ) { // non integer multiples of 1 kHz
        cf = 200 + unsigned( round( calfreq / 100 ) );            // in the range 1000 .. 5500 Hz
    } else {
        cf = unsigned( round( calfreq / 1000 ) ); // 1000, ..., 100000 -> 1, ..., 100
    }
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setCalFreq()" << calfreq << cf;
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    // control command for setting
    modifyCommand< ControlSetCalFreq >( ControlCode::CONTROL_SETCALFREQ )->setCalFreq( uint8_t( cf ) );
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setChannelUsed( ChannelID channel, bool used ) {
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setChannelUsed()" << channel << used;
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( channel > specification->channels )
        return Dso::ErrorCode::PARAMETER;
    // Update settings
    controlsettings.voltage[ channel ].used = used;
    // Calculate the UsedChannels field for the command
    UsedChannels usedChannels = UsedChannels::USED_CH1;
    controlsettings.channelCount = 1;

    if ( controlsettings.voltage[ 1 ].used ) {
        controlsettings.channelCount = 2;
        if ( controlsettings.voltage[ 0 ].used ) {
            usedChannels = UsedChannels::USED_CH1CH2;
        } else {
            usedChannels = UsedChannels::USED_CH2;
        }
    }
    setSingleChannel( usedChannels == UsedChannels::USED_CH1 );
    if ( verboseLevel > 2 )
        qDebug() << "  usedChannels" << QString( "%1" ).arg( int( usedChannels ), 2, 2, QLatin1Char( '0' ) );
    modifyCommand< ControlSetNumChannels >( ControlCode::CONTROL_SETNUMCHANNELS )->setNumChannels( isSingleChannel() ? 1 : 2 );
    // Check if fast rate mode availability changed
    updateSamplerateLimits();
    restoreTargets();
    requestRefresh(); // force new data conversion
    // sampleSetupChanged = true; // skip next raw samples block to avoid artefacts
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setChannelInverted( ChannelID channel, bool inverted ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( channel > specification->channels )
        return Dso::ErrorCode::PARAMETER;
    // Update settings
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setChannelInverted()" << channel << inverted;
    controlsettings.voltage[ channel ].inverted = inverted;
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setGain( ChannelID channel, double gain ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;

    if ( channel > specification->channels )
        return Dso::ErrorCode::PARAMETER;

    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setGain()" << channel << gain;
    static uint8_t lastGain[ 2 ] = { 0xFF, 0xFF };
    gain /= controlsettings.voltage[ channel ].probeAttn; // gain needs to be scaled by probe attenuation
    // Find lowest gain voltage that's at least as high as the requested
    uint8_t gainID;
    for ( gainID = 0; gainID < specification->gain.size() - 1; ++gainID )
        if ( specification->gain[ gainID ].Vdiv >= gain )
            break;
    uint8_t gainValue = specification->gain[ gainID ].gainValue;
    if ( channel == 0 ) {
        modifyCommand< ControlSetGain_CH1 >( ControlCode::CONTROL_SETGAIN_CH1 )->setGainCH1( gainValue, gainID );
        if ( lastGain[ 0 ] != gainValue ) { // HW gain changed, start new samples
            restartSampling();
        }
        lastGain[ 0 ] = gainValue;
    } else if ( channel == 1 ) {
        modifyCommand< ControlSetGain_CH2 >( ControlCode::CONTROL_SETGAIN_CH2 )->setGainCH2( gainValue, gainID );
        if ( lastGain[ 1 ] != gainValue ) { // HW gain changed, start new samples
            restartSampling();
        }
        lastGain[ 1 ] = gainValue;
    } else if ( channel == 2 ) {
        // do nothing
    } else
        qDebug( "%s: Unsupported channel: %i\n", __func__, channel );
    controlsettings.voltage[ channel ].gain = gainID;
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setProbe( ChannelID channel, double probeAttn ) {
    if ( channel >= specification->channels )
        return Dso::ErrorCode::PARAMETER;

    controlsettings.voltage[ channel ].probeAttn = probeAttn;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setProbe()" << channel << probeAttn;
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setCoupling( ChannelID channel, Dso::Coupling coupling ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;

    if ( channel >= specification->channels )
        return Dso::ErrorCode::PARAMETER;

    static int lastCoupling[ 2 ] = { -1, -1 };
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setCoupling()" << channel << int( coupling );
    if ( hasCommand( ControlCode::CONTROL_SETCOUPLING ) ) // don't send command if it is not implemented (like on the 6022)
        modifyCommand< ControlSetCoupling >( ControlCode::CONTROL_SETCOUPLING )
            ->setCoupling( channel, coupling == Dso::Coupling::DC );
    controlsettings.voltage[ channel ].coupling = coupling;
    if ( lastCoupling[ channel ] != int( coupling ) ) { // HW coupling changed, start new samples
        restartSampling();
    }
    lastCoupling[ channel ] = int( coupling );
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setTriggerMode( Dso::TriggerMode mode ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;

    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerMode()" << int( mode );
    static Dso::TriggerMode lastMode;
    controlsettings.trigger.mode = mode;
    if ( Dso::TriggerMode::SINGLE != mode )
        enableSamplingUI();
    // trigger mode changed NONE <-> !NONE
    if ( ( Dso::TriggerMode::ROLL == mode && Dso::TriggerMode::ROLL != lastMode ) ||
         ( Dso::TriggerMode::ROLL != mode && Dso::TriggerMode::ROLL == lastMode ) ) {
        restartSampling(); // invalidate old samples
        raw.freeRun = Dso::TriggerMode::ROLL == mode;
    }
    lastMode = mode;
    requestRefresh();
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setTriggerSource( int channel ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerSource()" << channel;
    controlsettings.trigger.source = channel;
    requestRefresh();
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setTriggerSmooth( int smooth ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerSmooth()" << smooth;
    controlsettings.trigger.smooth = smooth;
    requestRefresh();
    return Dso::ErrorCode::NONE;
}


// trigger level in Volt
Dso::ErrorCode HantekDsoControl::setTriggerLevel( ChannelID channel, double level ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( channel > specification->channels )
        return Dso::ErrorCode::PARAMETER;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerLevel()" << channel << level;
    controlsettings.trigger.level[ channel ] = level;
    requestRefresh();
    displayInterval = 0; // update screen immediately
    return Dso::ErrorCode::NONE;
}


Dso::ErrorCode HantekDsoControl::setTriggerSlope( Dso::Slope slope ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerSlope()" << int( slope );
    controlsettings.trigger.slope = slope;
    requestRefresh();
    return Dso::ErrorCode::NONE;
}


// set trigger position (0.0 - 1.0)
Dso::ErrorCode HantekDsoControl::setTriggerPosition( double position ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::setTriggerPosition()" << position;
    controlsettings.trigger.position = position;
    requestRefresh();
    return Dso::ErrorCode::NONE;
}


// Initialize the device with the current settings.
void HantekDsoControl::applySettings( const DsoSettingsScope *dsoSettingsScope ) {
    if ( verboseLevel > 1 )
        qDebug() << " HDC::applySettings()";
    scope = dsoSettingsScope;
    bool mathUsed = dsoSettingsScope->anyUsed( specification->channels );
    for ( ChannelID channel = 0; channel <= specification->channels; ++channel ) {
        setProbe( channel, dsoSettingsScope->voltage[ channel ].probeAttn );
        setGain( channel, dsoSettingsScope->gain( channel ) );
        setTriggerLevel( channel, dsoSettingsScope->voltage[ channel ].trigger );
        setChannelUsed( channel, mathUsed | dsoSettingsScope->anyUsed( channel ) );
        setChannelInverted( channel, dsoSettingsScope->voltage[ channel ].inverted );
        if ( channel < specification->channels )
            setCoupling( channel, Dso::Coupling( dsoSettingsScope->voltage[ channel ].couplingOrMathIndex ) );
    }

    setRecordTime( dsoSettingsScope->horizontal.timebase * DIVS_TIME );
    setCalFreq( dsoSettingsScope->horizontal.calfreq );
    setTriggerMode( dsoSettingsScope->trigger.mode );
    setTriggerPosition( dsoSettingsScope->trigger.position );
    setTriggerSlope( dsoSettingsScope->trigger.slope );
    setTriggerSource( dsoSettingsScope->trigger.source );
    setTriggerSmooth( dsoSettingsScope->trigger.smooth );
    mathChannel = std::unique_ptr< MathChannel >( new MathChannel( scope ) );
    triggering = std::unique_ptr< Triggering >( new Triggering( scope, controlsettings ) );
}


void HantekDsoControl::parkForReconnect() {
    if ( verboseLevel > 1 )
        qDebug() << " HDC::parkForReconnect()";

    if ( offsetRepeatabilityStudyActive )
        finishOffsetRepeatabilityStudy( false, tr( "The study stopped because the oscilloscope disconnected." ) );

    {
        QWriteLocker locker( &scopeDeviceLock );
        if ( deviceConnectionState == DeviceConnectionState::Parked )
            return;

        resumeSamplingAfterReconnect = samplingUI;
        samplingUI = false;
        samplingStarted = false;
        deviceConnectionState = DeviceConnectionState::Parked;
        if ( scopeDevice )
            scopeDevice->stopSampling();
    }

    {
        QWriteLocker rawLocker( &raw.lock );
        raw.valid = false;
        raw.rollMode = false;
    }

    emit showSamplingStatus( false );
    emit deviceAvailabilityChanged( false );
}


void HantekDsoControl::rebindDevice( ScopeDevice *newScopeDevice ) {
    if ( verboseLevel > 1 )
        qDebug() << " HDC::rebindDevice()";

    bool shouldResume = false;
    {
        QWriteLocker locker( &scopeDeviceLock );
        deviceConnectionState = DeviceConnectionState::Rebinding;
        scopeDevice = newScopeDevice;
        if ( scopeDevice && specification->fixedUSBinLength )
            scopeDevice->overwriteInPacketLength( unsigned( specification->fixedUSBinLength ) );
        shouldResume = resumeSamplingAfterReconnect;
        resumeSamplingAfterReconnect = false;
        deviceConnectionState = DeviceConnectionState::Connected;
    }

    getCalibrationFromIniFile();
    if ( scope )
        applySettings( scope );

    if ( shouldResume )
        enableSamplingUI( true );

    emit deviceAvailabilityChanged( true );
}


/// \brief Starts a new sampling block.
void HantekDsoControl::restartSampling() {
    if ( verboseLevel > 4 )
        qDebug() << "    HDC::restartSampling()";
    QReadLocker locker( &scopeDeviceLock );
    if ( scopeDevice && deviceConnectionState == DeviceConnectionState::Connected )
        scopeDevice->stopSampling();
    raw.rollMode = false;
}


/// \brief Start sampling process.
void HantekDsoControl::enableSamplingUI( bool enabled ) {
    if ( verboseLevel > 3 )
        qDebug() << "   HDC::enableSampling()" << enabled;
    if ( enabled && triggering && controlsettings.trigger.mode == Dso::TriggerMode::SINGLE )
        triggering->resetTriggeredPositionRaw(); // invalidate previous result, wait for new trigger
    else if ( controlsettings.trigger.mode == Dso::TriggerMode::ROLL )
        samplingStarted = enabled; // start / stop roll mode sampling (almost) immediately
    samplingUI = enabled;
    updateSamplerateLimits();
    emit showSamplingStatus( enabled );
}


unsigned HantekDsoControl::getRecordLength() const {
    unsigned rawsize = getSamplesize();
    rawsize *= downsamplingNumber;         // take multiple samples for oversampling
    rawsize = grossSampleCount( rawsize ); // adjust for skipping of minimal 2048 leading samples
    if ( verboseLevel > 4 )
        qDebug() << "    HDC::getRecordLength() ->" << rawsize;
    return rawsize;
}


Dso::ErrorCode HantekDsoControl::getCalibrationFromIniFile() {
    // Persistent storage: unique offset/gain calibration file:
    // Linux, Unix: "$HOME/.config/OpenHantek/DSO-6022BE_NNNNNNNNNNNN_calibration.ini"
    // macOS: "$HOME/Library/Application Support/OpenHantek/OpenHantek6022/Calibration/"
    //        "DSO-6022BE_NNNNNNNNNNNN_calibration.ini"
    // Windows: "%APPDATA%\OpenHantek\DSO-6022BE_NNNNNNNNNNNN_calibration.ini"
    QString calName;
    {
        QReadLocker locker( &scopeDeviceLock );
        if ( !scopeDevice )
            return Dso::ErrorCode::CONNECTION;
        calName = scopeDevice->getModel()->name + "_" + scopeDevice->getSerialNumber() + "_calibration";
    }
    if ( verboseLevel > 2 )
        qDebug() << "  Calibration data:" << calName + ".ini";

    calibrationFileName = calibrationFilePath( calName );
    QSettings calibrationSettings( calibrationFileName, QSettings::IniFormat );

    // load the offsets (persistent, saved at shutdown as "*.ini" file,  )
    calibrationSettings.beginGroup( "offset" );
    for ( int ch = 0; ch < HANTEK_CHANNEL_NUMBER; ++ch ) {
        calibrationSettings.beginGroup( "ch" + QString::number( ch ) );
        int index = 0;
        for ( const auto &g : model->spec()->gain ) {
            offsetCorrection[ index ][ ch ] =
                calibrationSettings.value( ( QString::number( int( g.Vdiv * 1000 ) ) + "mV" ), 0.0 ).toDouble();
            ++index;
        }
        calibrationSettings.endGroup();
    }
    calibrationSettings.endGroup();

    separateHighSpeedOffsetCorrection = calibrationSettings.childGroups().contains( "offset_high" );
    calibrationSettings.beginGroup( "offset_high" );
    for ( int ch = 0; ch < HANTEK_CHANNEL_NUMBER; ++ch ) {
        calibrationSettings.beginGroup( "ch" + QString::number( ch ) );
        int index = 0;
        for ( const auto &g : model->spec()->gain ) {
            highSpeedOffsetCorrection[ index ][ ch ] =
                calibrationSettings
                    .value( QString::number( int( g.Vdiv * 1000 ) ) + "mV", offsetCorrection[ index ][ ch ] )
                    .toDouble();
            ++index;
        }
        calibrationSettings.endGroup();
    }
    calibrationSettings.endGroup();

    // load the gain (provided by user)
    calibrationSettings.beginGroup( "gain" );
    for ( int ch = 0; ch < 2; ++ch ) {
        calibrationSettings.beginGroup( "ch" + QString::number( ch ) );
        int index = 0;
        for ( const auto &g : model->spec()->gain ) {
            gainCorrection[ index ][ ch ] =
                calibrationSettings.value( ( QString::number( int( g.Vdiv * 1000 ) ) + "mV" ), 1.0 ).toDouble();
            ++index;
        }
        calibrationSettings.endGroup();
    }
    calibrationSettings.endGroup(); // gain

    calibrationSettings.beginGroup( "eeprom" );
    replaceCalibrationEEPROM = calibrationSettings.value( "replace_eeprom", false ).toBool();
    calibrationSettings.endGroup(); // eeprom

    if ( replaceCalibrationEEPROM ) // values created by python tool "calibrate_6022.py" replace the EEPROM content
        memset( controlsettings.calibrationValues, 0xFF, sizeof( CalibrationValues ) );
    else // enhance the intrinsic calibration values from EEPROM
        getCalibrationFromEEPROM();

    return Dso::ErrorCode::NONE;
}


bool HantekDsoControl::saveOffsetCalibration() {
    if ( calibrationFileName.isEmpty() ) {
        emit statusMessage( tr( "Offset calibration not saved: no calibration INI file is available." ), 0 );
        return false;
    }

    const QString filePath = calibrationFileName;
    const QString backupPath = filePath + ".bak";
    const bool hadCalibrationFile = QFileInfo::exists( filePath );
    if ( hadCalibrationFile && !copyFileAtomically( filePath, backupPath ) ) {
        emit statusMessage( tr( "Offset calibration not saved: unable to create backup %1" ).arg( backupPath ), 0 );
        return false;
    }

    bool verified = false;
    {
        QSettings calibrationSettings( filePath, QSettings::IniFormat );
        calibrationSettings.beginGroup( "offset" );
        for ( int channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
            calibrationSettings.beginGroup( "ch" + QString::number( channel ) );
            int gainIndex = 0;
            for ( const auto &gain : model->spec()->gain ) {
                const double offset = round( 100.0 * offsetCorrection[ gainIndex ][ channel ] ) / 100.0;
                calibrationSettings.setValue( QString::number( int( gain.Vdiv * 1000 ) ) + "mV", offset );
                ++gainIndex;
            }
            calibrationSettings.endGroup();
        }
        calibrationSettings.endGroup();
        calibrationSettings.sync();
        verified = calibrationSettings.status() == QSettings::NoError;
    }

    QSettings verification( filePath, QSettings::IniFormat );
    verification.beginGroup( "offset" );
    for ( int channel = 0; verified && channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
        verification.beginGroup( "ch" + QString::number( channel ) );
        int gainIndex = 0;
        for ( const auto &gain : model->spec()->gain ) {
            const double expected = round( 100.0 * offsetCorrection[ gainIndex ][ channel ] ) / 100.0;
            const double stored =
                verification.value( QString::number( int( gain.Vdiv * 1000 ) ) + "mV", std::numeric_limits< double >::quiet_NaN() )
                    .toDouble();
            if ( !qIsFinite( stored ) || qAbs( stored - expected ) > 0.0001 ) {
                verified = false;
                break;
            }
            ++gainIndex;
        }
        verification.endGroup();
    }
    verification.endGroup();
    verified = verified && verification.status() == QSettings::NoError;

    if ( !verified ) {
        const bool restored = hadCalibrationFile ? copyFileAtomically( backupPath, filePath )
                                                 : ( !QFileInfo::exists( filePath ) || QFile::remove( filePath ) );
        getCalibrationFromIniFile();
        emit statusMessage( restored ? tr( "Offset calibration not saved; the previous INI file was restored." )
                                     : tr( "Offset calibration save failed and the previous INI file could not be restored." ),
                            0 );
        return false;
    }

    if ( !separateHighSpeedOffsetCorrection )
        memcpy( highSpeedOffsetCorrection, offsetCorrection, sizeof( offsetCorrection ) );

    emit statusMessage(
        hadCalibrationFile
            ? tr( "Offset calibration saved to %1. Previous settings: %2" ).arg( filePath, backupPath )
            : tr( "Offset calibration saved to %1" ).arg( filePath ),
        0 );
    return true;
}


bool HantekDsoControl::prepareEEPROMCalibrationBundle( bool writeRequested, double nullHalfWidth,
                                                        EEPROMCalibrationBundle &bundle, QString &errorMessage ) {
    const auto fail = [ this, &bundle, &errorMessage ]( const QString &reason ) {
        errorMessage = tr( "EEPROM safety preparation failed: %1 EEPROM NOT WRITTEN." ).arg( reason );
        if ( !bundle.outputDirectory.isEmpty() )
            errorMessage += tr( " Partial safety folder: %1" ).arg( bundle.outputDirectory );
    };

    static_assert( sizeof( CalibrationValues ) == 80, "Unexpected EEPROM calibration layout" );
    if ( !qIsFinite( nullHalfWidth ) || nullHalfWidth < EEPROM_NULL_HALF_WIDTH_MIN ||
         nullHalfWidth > EEPROM_NULL_HALF_WIDTH_MAX ) {
        fail( tr( "the null half-width is outside the supported 0.00 to 0.50 ADC-count range." ) );
        return false;
    }
    bundle.nullHalfWidth = nullHalfWidth;

    if ( offsetCalibrationActive ) {
        fail( tr( "finish or cancel offset calibration first." ) );
        return false;
    }
    if ( replaceCalibrationEEPROM ) {
        fail( tr( "the INI file is configured to replace, rather than enhance, EEPROM calibration." ) );
        return false;
    }
    if ( model->spec()->gain.size() != HANTEK_GAIN_STEPS ) {
        fail( tr( "the oscilloscope does not use the expected eight-range calibration layout." ) );
        return false;
    }
    if ( calibrationFileName.isEmpty() || !QFileInfo::exists( calibrationFileName ) ) {
        fail( tr( "complete and save offset calibration before preparing EEPROM files." ) );
        return false;
    }

    QFile iniFile( calibrationFileName );
    if ( !iniFile.open( QIODevice::ReadOnly ) ) {
        fail( tr( "the saved INI file could not be opened for its safety snapshot." ) );
        return false;
    }
    bundle.iniContents = iniFile.readAll();
    if ( iniFile.error() != QFileDevice::NoError ) {
        fail( tr( "the saved INI file could not be read for its safety snapshot." ) );
        return false;
    }

    QSettings persistedSettings( calibrationFileName, QSettings::IniFormat );
    persistedSettings.beginGroup( "offset" );
    for ( int channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
        persistedSettings.beginGroup( "ch" + QString::number( channel ) );
        for ( int gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
            const QString key =
                QString::number( int( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) ) + "mV";
            if ( !persistedSettings.contains( key ) ) {
                persistedSettings.endGroup();
                persistedSettings.endGroup();
                fail( tr( "the saved INI file does not contain all 16 calibrated ranges." ) );
                return false;
            }
            const double value = persistedSettings.value( key ).toDouble();
            if ( !qIsFinite( value ) ) {
                persistedSettings.endGroup();
                persistedSettings.endGroup();
                fail( tr( "the saved INI file contains a non-numeric offset." ) );
                return false;
            }
            bundle.persistedOffset[ gainIndex ][ channel ] = value;
        }
        persistedSettings.endGroup();
    }
    persistedSettings.endGroup();

    persistedSettings.beginGroup( "offset_high" );
    for ( int channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
        persistedSettings.beginGroup( "ch" + QString::number( channel ) );
        for ( int gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
            const QString key =
                QString::number( int( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) ) + "mV";
            const double value =
                persistedSettings.value( key, bundle.persistedOffset[ gainIndex ][ channel ] ).toDouble();
            if ( !qIsFinite( value ) ) {
                persistedSettings.endGroup();
                persistedSettings.endGroup();
                fail( tr( "the saved INI file contains a non-numeric high-speed offset." ) );
                return false;
            }
            bundle.persistedHighSpeedOffset[ gainIndex ][ channel ] = value;
        }
        persistedSettings.endGroup();
    }
    persistedSettings.endGroup();

    if ( persistedSettings.status() != QSettings::NoError ) {
        fail( tr( "the saved INI file could not be read reliably." ) );
        return false;
    }

    QFile iniVerification( calibrationFileName );
    if ( !iniVerification.open( QIODevice::ReadOnly ) ) {
        fail( tr( "the saved INI file could not be reopened for verification." ) );
        return false;
    }
    const QByteArray verifiedIniContents = iniVerification.readAll();
    if ( iniVerification.error() != QFileDevice::NoError || verifiedIniContents != bundle.iniContents ) {
        fail( tr( "the saved INI file changed while its calibration values were being read." ) );
        return false;
    }

    QByteArray verificationBytes;
    {
        QWriteLocker locker( &scopeDeviceLock );
        if ( !scopeDevice || deviceConnectionState != DeviceConnectionState::Connected || !scopeDevice->isConnected() ||
             !scopeDevice->isRealHW() || !specification->hasCalibrationEEPROM ) {
            fail( tr( "a connected physical oscilloscope with calibration EEPROM is required." ) );
            return false;
        }

        bundle.deviceModel = scopeDevice->getModel()->name;
        bundle.deviceSerial = scopeDevice->getSerialNumber();
        QString readError;
        if ( !readCalibrationRegion( scopeDevice, bundle.originalBytes, readError ) ||
             !readCalibrationRegion( scopeDevice, verificationBytes, readError ) ) {
            fail( tr( "two complete 80-byte EEPROM reads could not be obtained: %1." ).arg( readError ) );
            return false;
        }
    }

    if ( bundle.originalBytes != verificationBytes ) {
        fail( tr( "two consecutive EEPROM reads did not match." ) );
        return false;
    }

    CalibrationValues originalValues = {};
    CalibrationValues candidateValues = {};
    memcpy( &originalValues, bundle.originalBytes.constData(), sizeof( CalibrationValues ) );
    memcpy( &candidateValues, bundle.originalBytes.constData(), sizeof( CalibrationValues ) );

    QString calculationReport;
    QTextStream calculations( &calculationReport );
    calculations
        << "Range\tChannel\tEEPROM addresses\tOriginal interpretation\tOriginal centre (ADC count)\t"
           "Raw INI residual (ADC count)\tEffective residual (ADC count)\tNull-window decision\t"
           "Candidate centre (ADC count)\tOriginal bytes\tCandidate bytes\n";

    for ( int gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
        for ( int channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
            const uint8_t originalRaw = originalValues.off.ls.step[ gainIndex ][ channel ];
            const uint8_t originalFine = originalValues.fine.ls.step[ gainIndex ][ channel ];
            const bool originalBytesValid =
                validCalibrationByte( originalRaw ) && validCalibrationByte( originalFine );
            const double originalOffset = bytesToOffset( originalRaw, originalFine );
            const double rawResidual = bundle.persistedOffset[ gainIndex ][ channel ];
            const bool residualSuppressed =
                nullHalfWidth > 0.0 && qAbs( rawResidual ) <= nullHalfWidth;
            const double effectiveResidual = residualSuppressed ? 0.0 : rawResidual;
            const char *nullDecision =
                nullHalfWidth == 0.0
                    ? "filter disabled -> retained"
                    : ( residualSuppressed ? "inside window -> ignored" : "outside window -> retained" );
            if ( residualSuppressed )
                ++bundle.suppressedResidualCount;
            else
                ++bundle.appliedResidualCount;
            const double proposedOffset = originalOffset + effectiveResidual;
            uint8_t candidateRaw = 0;
            uint8_t candidateFine = 0;
            if ( !offsetToBytes( proposedOffset, candidateRaw, candidateFine ) ) {
                fail( tr( "a proposed low-speed offset cannot be represented safely in EEPROM." ) );
                return false;
            }

            candidateValues.off.ls.step[ gainIndex ][ channel ] = candidateRaw;
            candidateValues.fine.ls.step[ gainIndex ][ channel ] = candidateFine;

            const int byteIndex = gainIndex * HANTEK_CHANNEL_NUMBER + channel;
            const int rawAddress = 8 + byteIndex;
            const int fineAddress = 8 + 48 + byteIndex;
            const int millivolts = int( model->spec()->gain[ gainIndex ].Vdiv * 1000 );
            calculations << millivolts << " mV/div\tCH" << channel + 1 << "\t0x"
                         << QString::number( rawAddress, 16 ).toUpper() << "/0x"
                         << QString::number( fineAddress, 16 ).toUpper() << "\t"
                         << ( originalBytesValid ? "EEPROM value" : "invalid bytes -> app default" ) << "\t"
                         << QString::number( originalOffset, 'f', 3 ) << "\t"
                         << QString::number( rawResidual, 'f', 9 ) << "\t"
                         << QString::number( effectiveResidual, 'f', 9 ) << "\t"
                         << nullDecision << "\t"
                         << QString::number( bytesToOffset( candidateRaw, candidateFine ), 'f', 3 ) << "\t0x"
                         << hexByte( originalRaw ) << "/0x" << hexByte( originalFine ) << "\t0x"
                         << hexByte( candidateRaw ) << "/0x" << hexByte( candidateFine ) << "\n";
        }
    }

    if ( memcmp( &originalValues.off.hs, &candidateValues.off.hs, sizeof( originalValues.off.hs ) ) != 0 ||
         memcmp( &originalValues.gain, &candidateValues.gain, sizeof( originalValues.gain ) ) != 0 ||
         memcmp( &originalValues.fine.hs, &candidateValues.fine.hs, sizeof( originalValues.fine.hs ) ) != 0 ) {
        fail( tr( "the candidate changed protected high-speed or gain calibration bytes." ) );
        return false;
    }

    bundle.candidateBytes =
        QByteArray( reinterpret_cast< const char * >( &candidateValues ), int( sizeof( CalibrationValues ) ) );
    bundle.candidateDiffers = bundle.candidateBytes != bundle.originalBytes;
    for ( int index = 0; index < bundle.candidateBytes.size(); ++index ) {
        const bool permittedLowSpeedByte = index < 16 || ( index >= 48 && index < 64 );
        if ( bundle.candidateBytes[ index ] != bundle.originalBytes[ index ] && !permittedLowSpeedByte ) {
            fail( tr( "the candidate changed a byte outside the two permitted low-speed offset regions." ) );
            return false;
        }
    }

    bundle.timestamp = QDateTime::currentDateTimeUtc().toString( "yyyyMMdd'T'HHmmsszzz'Z'" );
    const QString deviceDirectory = QFileInfo( calibrationFileName ).completeBaseName();
    bundle.outputDirectory =
        QDir( QFileInfo( calibrationFileName ).absolutePath() )
            .filePath( "EEPROM Backups/" + deviceDirectory + "/" + bundle.timestamp );
    if ( !QDir().mkpath( bundle.outputDirectory ) ) {
        fail( tr( "the safety output folder could not be created." ) );
        return false;
    }

    bundle.originalPath =
        QDir( bundle.outputDirectory ).filePath( "eeprom-calibration-original-80-bytes.bin" );
    bundle.candidatePath =
        QDir( bundle.outputDirectory )
            .filePath( writeRequested && bundle.candidateDiffers
                           ? "eeprom-calibration-candidate-80-bytes.bin"
                           : "eeprom-calibration-candidate-80-bytes-NOT-WRITTEN.bin" );
    bundle.iniSnapshotPath =
        QDir( bundle.outputDirectory ).filePath( "calibration-ini-snapshot.ini" );
    bundle.reportPath = QDir( bundle.outputDirectory )
                            .filePath( !bundle.candidateDiffers
                                           ? "EEPROM-no-material-change-report.txt"
                                           : ( writeRequested ? "EEPROM-pre-write-safety-report.txt"
                                                              : "EEPROM-dry-run-report.txt" ) );
    bundle.checksumsPath = QDir( bundle.outputDirectory ).filePath( "SHA256SUMS.txt" );

    if ( !writeFileAtomically( bundle.originalPath, bundle.originalBytes ) ) {
        fail( tr( "the original EEPROM backup could not be saved and verified." ) );
        return false;
    }
    if ( !writeFileAtomically( bundle.iniSnapshotPath, bundle.iniContents ) ) {
        fail( tr( "the calibration INI snapshot could not be saved and verified." ) );
        return false;
    }
    if ( !writeFileAtomically( bundle.candidatePath, bundle.candidateBytes ) ) {
        fail( tr( "the read-only candidate image could not be saved and verified." ) );
        return false;
    }

    QString reportText;
    QTextStream report( &reportText );
    report << ( !bundle.candidateDiffers
                    ? "OpenHantek6022 EEPROM calibration no-material-change result\n"
                    : ( writeRequested ? "OpenHantek6022 EEPROM pre-write safety preparation\n"
                                       : "OpenHantek6022 EEPROM calibration safety dry run\n" ) )
           << "====================================================\n\n"
           << ( !bundle.candidateDiffers
                    ? "RESULT: NO MATERIAL CHANGE; EEPROM NOT WRITTEN\n\n"
                    : ( writeRequested ? "STATUS AT PREPARATION: EEPROM NOT YET WRITTEN\n\n"
                                       : "RESULT: EEPROM NOT WRITTEN\n\n" ) )
           << "UTC timestamp: " << bundle.timestamp << "\n"
           << "Device model: " << bundle.deviceModel << "\n"
           << "Device serial: " << bundle.deviceSerial << "\n"
           << "Calibration region: 80 bytes at EEPROM addresses 0x08 through 0x57\n"
           << "INI source: " << calibrationFileName << "\n"
           << "Null half-width: " << QString::number( bundle.nullHalfWidth, 'f', 2 ) << " ADC count\n"
           << "Null filtering: " << ( bundle.nullHalfWidth > 0.0 ? "enabled" : "disabled" ) << "\n"
           << "Residuals ignored by null window: " << bundle.suppressedResidualCount << " of 16\n"
           << "Residuals retained for candidate: " << bundle.appliedResidualCount << " of 16\n"
           << "Advanced physical write requested: " << ( writeRequested ? "yes" : "no" ) << "\n"
           << "Physical write permitted after byte comparison: "
           << ( writeRequested && bundle.candidateDiffers ? "yes" : "no" ) << "\n"
           << "Post-quantization candidate differs from EEPROM: "
           << ( bundle.candidateDiffers ? "yes" : "no" ) << "\n\n"
           << "Safety checks completed:\n"
           << "- Two independent 80-byte device reads matched exactly.\n"
           << "- The original EEPROM backup was written atomically and read back from disk.\n"
           << "- The saved calibration INI was copied atomically and read back from disk.\n"
           << "- The candidate was written atomically and read back from disk.\n"
           << "- Each low-speed residual was compared with the selected zero-centered null window.\n"
           << "- Residuals at or inside an enabled null half-width were replaced with zero before\n"
           << "  the candidate bytes were calculated.\n"
           << "- Candidate changes are restricted to low-speed raw offsets (0x08-0x17)\n"
           << "  and low-speed fine offsets (0x38-0x47).\n"
           << "- High-speed offsets, gain calibration, and all other bytes are unchanged.\n"
           << "- The final write decision uses exact equality of the complete 80-byte images after\n"
           << "  EEPROM byte quantization; an identical candidate cannot be written.\n"
           << "- Invalid original offset byte pairs are shown explicitly and interpreted as centre 128,\n"
           << "  matching the application's existing calibration behaviour.\n"
           << "- No USB write command had been issued when this preparation report was created.\n"
           << "- The active INI file had not been changed when this preparation report was created.\n\n"
           << "Files:\n"
           << "- Original EEPROM backup: " << bundle.originalPath << "\n"
           << "- Proposed image (not yet written): " << bundle.candidatePath << "\n"
           << "- Calibration INI snapshot: " << bundle.iniSnapshotPath << "\n"
           << "- Checksums: " << bundle.checksumsPath << "\n\n"
           << "SHA-256:\n"
           << "- Original EEPROM: " << sha256( bundle.originalBytes ) << "\n"
           << "- Proposed image: " << sha256( bundle.candidateBytes ) << "\n"
           << "- INI snapshot: " << sha256( bundle.iniContents ) << "\n\n"
           << "Proposed low-speed offset changes:\n"
           << calculationReport << "\n";
    if ( !bundle.candidateDiffers )
        report << "The complete post-quantization candidate is byte-identical to the current EEPROM.\n"
               << "The physical write path is blocked even when the advanced update option was selected.\n"
               << "RESULT: NO MATERIAL CHANGE; EEPROM NOT WRITTEN\n";
    else if ( writeRequested )
        report << "A separate EEPROM-update-result.txt file must record the guarded transaction outcome.\n"
               << "STATUS AT PREPARATION: EEPROM NOT YET WRITTEN\n";
    else
        report << "This candidate must be reviewed before any EEPROM-writing code is enabled.\n"
               << "RESULT: EEPROM NOT WRITTEN\n";

    bundle.preparationReport = reportText.toUtf8();
    if ( !writeFileAtomically( bundle.reportPath, bundle.preparationReport ) ) {
        fail( tr( "the human-readable dry-run report could not be saved and verified." ) );
        return false;
    }

    QByteArray checksumContents;
    checksumContents += sha256( bundle.originalBytes ) + "  eeprom-calibration-original-80-bytes.bin\n";
    checksumContents += sha256( bundle.candidateBytes ) + "  " +
                        QFileInfo( bundle.candidatePath ).fileName().toUtf8() + "\n";
    checksumContents += sha256( bundle.iniContents ) + "  calibration-ini-snapshot.ini\n";
    checksumContents += sha256( bundle.preparationReport ) + "  " +
                        QFileInfo( bundle.reportPath ).fileName().toUtf8() + "\n";
    if ( !writeFileAtomically( bundle.checksumsPath, checksumContents ) ) {
        fail( tr( "the checksum manifest could not be saved and verified." ) );
        return false;
    }

    return true;
}


void HantekDsoControl::prepareEEPROMCalibrationDryRun( double nullHalfWidth ) {
    EEPROMCalibrationBundle bundle;
    QString message;
    if ( !prepareEEPROMCalibrationBundle( false, nullHalfWidth, bundle, message ) ) {
        emit statusMessage( message, 0 );
        emit eepromCalibrationDryRunFinished( false, bundle.outputDirectory, QString(), message );
        return;
    }

    message =
        bundle.candidateDiffers
            ? tr( "EEPROM safety files created at %1. EEPROM NOT WRITTEN." ).arg( bundle.outputDirectory )
            : tr( "No material low-speed calibration change remains after null filtering and EEPROM byte "
                  "quantization. Safety files created at %1. EEPROM NOT WRITTEN." )
                  .arg( bundle.outputDirectory );
    emit statusMessage( message, 0 );
    emit eepromCalibrationDryRunFinished( true, bundle.outputDirectory, bundle.reportPath, message );
}


bool HantekDsoControl::reconcileCalibrationIniAfterEEPROMWrite( const EEPROMCalibrationBundle &bundle,
                                                                QString &errorMessage ) {
    QFile currentIni( calibrationFileName );
    if ( !currentIni.open( QIODevice::ReadOnly ) || currentIni.readAll() != bundle.iniContents ) {
        errorMessage = tr( "the active calibration INI changed after the safety snapshot." );
        return false;
    }

    const QString backupPath = calibrationFileName + ".bak";
    if ( !writeFileAtomically( backupPath, bundle.iniContents ) ) {
        errorMessage = tr( "the pre-update INI backup could not be saved and verified." );
        return false;
    }

    QTemporaryFile stagedFile(
        QDir( QFileInfo( calibrationFileName ).absolutePath() ).filePath( ".eeprom-calibration-XXXXXX.ini" ) );
    stagedFile.setAutoRemove( true );
    if ( !stagedFile.open() ) {
        errorMessage = tr( "a temporary INI file could not be created." );
        return false;
    }
    const QString stagedPath = stagedFile.fileName();
    stagedFile.close();

    {
        QSettings source( calibrationFileName, QSettings::IniFormat );
        QSettings staged( stagedPath, QSettings::IniFormat );
        for ( const QString &key : source.allKeys() )
            staged.setValue( key, source.value( key ) );

        for ( int channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
            for ( int gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
                const QString range =
                    QString::number( int( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) ) + "mV";
                staged.setValue( QString( "offset/ch%1/%2" ).arg( channel ).arg( range ), 0.0 );
                staged.setValue( QString( "offset_high/ch%1/%2" ).arg( channel ).arg( range ),
                                 bundle.persistedHighSpeedOffset[ gainIndex ][ channel ] );
            }
        }
        staged.setValue( "eeprom/replace_eeprom", false );
        staged.sync();
        if ( source.status() != QSettings::NoError || staged.status() != QSettings::NoError ) {
            errorMessage = tr( "the reconciled INI file could not be staged reliably." );
            return false;
        }
    }

    QFile stagedIni( stagedPath );
    if ( !stagedIni.open( QIODevice::ReadOnly ) ) {
        errorMessage = tr( "the staged INI file could not be read." );
        return false;
    }
    const QByteArray stagedContents = stagedIni.readAll();
    if ( stagedIni.error() != QFileDevice::NoError || !writeFileAtomically( calibrationFileName, stagedContents ) ) {
        errorMessage = tr( "the reconciled INI file could not be installed atomically." );
        return false;
    }

    bool verified = true;
    QSettings verification( calibrationFileName, QSettings::IniFormat );
    for ( int channel = 0; verified && channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
        for ( int gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
            const QString range =
                QString::number( int( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) ) + "mV";
            const double low =
                verification.value( QString( "offset/ch%1/%2" ).arg( channel ).arg( range ),
                                    std::numeric_limits< double >::quiet_NaN() )
                    .toDouble();
            const double high =
                verification.value( QString( "offset_high/ch%1/%2" ).arg( channel ).arg( range ),
                                    std::numeric_limits< double >::quiet_NaN() )
                    .toDouble();
            verified = qIsFinite( low ) && qAbs( low ) <= 0.0001 && qIsFinite( high ) &&
                       qAbs( high - bundle.persistedHighSpeedOffset[ gainIndex ][ channel ] ) <= 0.0001;
        }
    }
    verified = verified && !verification.value( "eeprom/replace_eeprom", true ).toBool() &&
               verification.status() == QSettings::NoError;
    if ( verified )
        return true;

    const bool restored = writeFileAtomically( calibrationFileName, bundle.iniContents );
    errorMessage =
        restored ? tr( "the reconciled INI failed verification; the original INI was restored." )
                 : tr( "the reconciled INI failed verification and the original INI could not be restored." );
    return false;
}


void HantekDsoControl::updateEEPROMCalibrationSafely( double nullHalfWidth ) {
    EEPROMCalibrationBundle bundle;
    QString message;
    if ( !prepareEEPROMCalibrationBundle( true, nullHalfWidth, bundle, message ) ) {
        emit statusMessage( message, 0 );
        emit eepromCalibrationUpdateFinished( false, false, true, bundle.outputDirectory, QString(), message );
        return;
    }

    if ( !bundle.candidateDiffers ) {
        message =
            tr( "No material low-speed calibration change remains after null filtering and EEPROM byte "
                "quantization. EEPROM NOT WRITTEN. Safety bundle: %1" )
                .arg( bundle.outputDirectory );
        emit statusMessage( message, 0 );
        emit eepromCalibrationUpdateFinished( true, false, true, bundle.outputDirectory, bundle.reportPath, message );
        return;
    }

    const QString readbackPath =
        QDir( bundle.outputDirectory ).filePath( "eeprom-calibration-readback-after-write-80-bytes.bin" );
    const QString rollbackReadbackPath =
        QDir( bundle.outputDirectory ).filePath( "eeprom-calibration-readback-after-rollback-80-bytes.bin" );
    const QString iniAfterPath =
        QDir( bundle.outputDirectory ).filePath( "calibration-ini-after-eeprom-update.ini" );
    const QString resultPath = QDir( bundle.outputDirectory ).filePath( "EEPROM-update-result.txt" );
    const QString statePath = QDir( bundle.outputDirectory ).filePath( "EEPROM-update-state.txt" );
    const QString finalChecksumsPath =
        QDir( bundle.outputDirectory ).filePath( "SHA256SUMS-after-update.txt" );

    bool success = false;
    bool writeAttempted = false;
    bool rollbackSucceeded = true;
    bool iniRestored = true;
    QByteArray candidateReadback;
    QByteArray rollbackReadback;
    QByteArray iniAfterContents;
    QString transactionError;

    const QByteArray preparedState =
        QString( "STATE: PREPARED; EEPROM NOT WRITTEN\nUTC timestamp: %1\nNull half-width: %2 ADC count\n"
                 "Original backup: %3\nCandidate: %4\n" )
            .arg( bundle.timestamp, QString::number( bundle.nullHalfWidth, 'f', 2 ), bundle.originalPath,
                  bundle.candidatePath )
            .toUtf8();
    if ( !writeFileAtomically( statePath, preparedState ) )
        transactionError = tr( "The persistent pre-write state marker could not be saved." );

    {
        QWriteLocker locker( &scopeDeviceLock );
        if ( transactionError.isEmpty() &&
             ( !scopeDevice || deviceConnectionState != DeviceConnectionState::Connected || !scopeDevice->isConnected() ||
               !scopeDevice->isRealHW() || !specification->hasCalibrationEEPROM ) ) {
            transactionError = tr( "The oscilloscope disconnected before the guarded update." );
        }

        QByteArray preWriteReadback;
        if ( transactionError.isEmpty() ) {
            QString readError;
            if ( !readCalibrationRegion( scopeDevice, preWriteReadback, readError ) )
                transactionError = tr( "The final pre-write EEPROM read failed: %1." ).arg( readError );
            else if ( preWriteReadback != bundle.originalBytes )
                transactionError = tr( "The EEPROM changed after its backup was created; the update was cancelled." );
        }

        if ( transactionError.isEmpty() ) {
            const QByteArray writingState =
                QString( "STATE: WRITE IN PROGRESS\nUTC timestamp: %1\nNull half-width: %2 ADC count\n"
                         "Do not remove this safety folder.\nOriginal backup: %3\nCandidate: %4\n" )
                    .arg( bundle.timestamp, QString::number( bundle.nullHalfWidth, 'f', 2 ), bundle.originalPath,
                          bundle.candidatePath )
                    .toUtf8();
            if ( !writeFileAtomically( statePath, writingState ) ) {
                transactionError = tr( "The persistent in-progress state marker could not be saved." );
            } else {
                writeAttempted = true;
                QString writeError;
                if ( !writeLowSpeedCalibrationRegions( scopeDevice, bundle.candidateBytes, writeError ) )
                    transactionError = tr( "The limited EEPROM write failed: %1." ).arg( writeError );
            }
        }

        if ( transactionError.isEmpty() ) {
            QString readError;
            if ( !readCalibrationRegion( scopeDevice, candidateReadback, readError ) )
                transactionError = tr( "The post-write EEPROM readback failed: %1." ).arg( readError );
            else if ( candidateReadback != bundle.candidateBytes )
                transactionError = tr( "The full 80-byte EEPROM readback did not match the candidate." );
            else if ( !writeFileAtomically( readbackPath, candidateReadback ) )
                transactionError = tr( "The verified EEPROM readback could not be saved to the safety folder." );
        }

        if ( transactionError.isEmpty() &&
             !reconcileCalibrationIniAfterEEPROMWrite( bundle, transactionError ) ) {
            transactionError = tr( "INI reconciliation failed: %1" ).arg( transactionError );
        }

        if ( transactionError.isEmpty() ) {
            QFile iniAfter( calibrationFileName );
            if ( !iniAfter.open( QIODevice::ReadOnly ) ) {
                transactionError = tr( "The reconciled INI could not be opened for its final snapshot." );
            } else {
                iniAfterContents = iniAfter.readAll();
                if ( iniAfter.error() != QFileDevice::NoError ||
                     !writeFileAtomically( iniAfterPath, iniAfterContents ) )
                    transactionError = tr( "The reconciled INI snapshot could not be saved and verified." );
            }
        }

        QByteArray successReport;
        if ( transactionError.isEmpty() ) {
            QString reportText;
            QTextStream report( &reportText );
            report << "OpenHantek6022 guarded EEPROM calibration update\n"
                   << "================================================\n\n"
                   << "RESULT: EEPROM UPDATE VERIFIED\n\n"
                   << "UTC timestamp: " << bundle.timestamp << "\n"
                   << "Device model: " << bundle.deviceModel << "\n"
                   << "Device serial: " << bundle.deviceSerial << "\n"
                   << "Null half-width: " << QString::number( bundle.nullHalfWidth, 'f', 2 )
                   << " ADC count\n"
                   << "Residuals ignored by null window: " << bundle.suppressedResidualCount << " of 16\n"
                   << "Residuals retained for candidate: " << bundle.appliedResidualCount << " of 16\n\n"
                   << "Verified transaction:\n"
                   << "- The original 80-byte calibration region was backed up before writing.\n"
                   << "- Only four aligned 8-byte chunks were written at EEPROM addresses\n"
                   << "  0x08, 0x10, 0x38, and 0x40.\n"
                   << "- A full 80-byte readback matched the candidate exactly.\n"
                   << "- Protected high-speed, gain, and unrelated bytes remained unchanged.\n"
                   << "- The pre-update INI is preserved in the safety folder and as "
                   << calibrationFileName << ".bak.\n"
                   << "- Active low-speed INI residuals are now zero to prevent double correction.\n"
                   << "- Previous residuals are retained under [offset_high] for high-speed sampling.\n"
                   << "- [eeprom] replace_eeprom=false was verified.\n\n"
                   << "SHA-256:\n"
                   << "- Original EEPROM: " << sha256( bundle.originalBytes ) << "\n"
                   << "- Candidate EEPROM: " << sha256( bundle.candidateBytes ) << "\n"
                   << "- Device readback: " << sha256( candidateReadback ) << "\n"
                   << "- INI before update: " << sha256( bundle.iniContents ) << "\n"
                   << "- INI after update: " << sha256( iniAfterContents ) << "\n\n"
                   << "RESULT: EEPROM UPDATE VERIFIED\n";
            successReport = reportText.toUtf8();
            if ( !writeFileAtomically( resultPath, successReport ) )
                transactionError = tr( "The verified transaction report could not be saved." );
        }

        if ( transactionError.isEmpty() ) {
            const QByteArray verifiedState =
                QString( "STATE: VERIFIED\nUTC timestamp: %1\nNull half-width: %2 ADC count\n"
                         "EEPROM readback and INI reconciliation succeeded.\n" )
                    .arg( bundle.timestamp, QString::number( bundle.nullHalfWidth, 'f', 2 ) )
                    .toUtf8();
            if ( !writeFileAtomically( statePath, verifiedState ) )
                transactionError = tr( "The verified transaction state marker could not be saved." );
        }

        if ( transactionError.isEmpty() ) {
            QByteArray checksums;
            checksums += sha256( bundle.originalBytes ) + "  eeprom-calibration-original-80-bytes.bin\n";
            checksums += sha256( bundle.candidateBytes ) + "  " +
                         QFileInfo( bundle.candidatePath ).fileName().toUtf8() + "\n";
            checksums += sha256( bundle.iniContents ) + "  calibration-ini-snapshot.ini\n";
            checksums += sha256( bundle.preparationReport ) + "  EEPROM-pre-write-safety-report.txt\n";
            checksums +=
                sha256( candidateReadback ) + "  eeprom-calibration-readback-after-write-80-bytes.bin\n";
            checksums += sha256( iniAfterContents ) + "  calibration-ini-after-eeprom-update.ini\n";
            checksums += sha256( successReport ) + "  EEPROM-update-result.txt\n";
            QFile stateFile( statePath );
            if ( !stateFile.open( QIODevice::ReadOnly ) ) {
                transactionError = tr( "The verified state marker could not be reopened." );
            } else {
                const QByteArray stateContents = stateFile.readAll();
                if ( stateFile.error() != QFileDevice::NoError )
                    transactionError = tr( "The verified state marker could not be read." );
                else
                    checksums += sha256( stateContents ) + "  EEPROM-update-state.txt\n";
            }
            if ( transactionError.isEmpty() && !writeFileAtomically( finalChecksumsPath, checksums ) )
                transactionError = tr( "The final checksum manifest could not be saved." );
        }

        if ( !transactionError.isEmpty() && writeAttempted ) {
            iniRestored = writeFileAtomically( calibrationFileName, bundle.iniContents );

            QString rollbackError;
            rollbackSucceeded = writeLowSpeedCalibrationRegions( scopeDevice, bundle.originalBytes, rollbackError );
            if ( rollbackSucceeded ) {
                QString readError;
                rollbackSucceeded = readCalibrationRegion( scopeDevice, rollbackReadback, readError ) &&
                                    rollbackReadback == bundle.originalBytes;
                if ( !rollbackSucceeded && rollbackError.isEmpty() )
                    rollbackError = readError.isEmpty() ? tr( "rollback readback did not match the original backup" )
                                                        : readError;
            }
            if ( !rollbackReadback.isEmpty() )
                writeFileAtomically( rollbackReadbackPath, rollbackReadback );
            if ( rollbackSucceeded ) {
                memcpy( controlsettings.calibrationValues, bundle.originalBytes.constData(), sizeof( CalibrationValues ) );
            } else {
                transactionError += tr( " AUTOMATIC ROLLBACK FAILED: %1." ).arg( rollbackError );
                if ( rollbackReadback.size() == int( sizeof( CalibrationValues ) ) )
                    memcpy( controlsettings.calibrationValues, rollbackReadback.constData(), sizeof( CalibrationValues ) );
            }
            if ( !iniRestored )
                transactionError += tr( " The original INI file could not be restored." );
        }

        if ( transactionError.isEmpty() ) {
            memcpy( controlsettings.calibrationValues, bundle.candidateBytes.constData(), sizeof( CalibrationValues ) );
            memset( offsetCorrection, 0, sizeof( offsetCorrection ) );
            memcpy( highSpeedOffsetCorrection, bundle.persistedHighSpeedOffset,
                    sizeof( highSpeedOffsetCorrection ) );
            separateHighSpeedOffsetCorrection = true;
            replaceCalibrationEEPROM = false;
            success = true;
        }
    }

    if ( !success ) {
        QString failureText;
        QTextStream failure( &failureText );
        failure << "OpenHantek6022 guarded EEPROM calibration update\n"
                << "================================================\n\n"
                << "RESULT: EEPROM UPDATE FAILED\n\n"
                << "UTC timestamp: " << bundle.timestamp << "\n"
                << "Device model: " << bundle.deviceModel << "\n"
                << "Device serial: " << bundle.deviceSerial << "\n"
                << "Null half-width: " << QString::number( bundle.nullHalfWidth, 'f', 2 ) << " ADC count\n"
                << "Residuals ignored by null window: " << bundle.suppressedResidualCount << " of 16\n"
                << "Residuals retained for candidate: " << bundle.appliedResidualCount << " of 16\n"
                << "Failure: " << transactionError << "\n"
                << "EEPROM write attempted: " << ( writeAttempted ? "yes" : "no" ) << "\n"
                << "Rollback: "
                << ( writeAttempted ? ( rollbackSucceeded ? "verified" : "FAILED" ) : "not required; no write occurred" )
                << "\n"
                << "Original INI restored: " << ( iniRestored ? "yes" : "NO" ) << "\n\n"
                << ( rollbackSucceeded
                         ? "The original EEPROM calibration region is verified or no write occurred.\n"
                         : "Do not perform another EEPROM update. Preserve this safety folder for recovery.\n" )
                << "RESULT: EEPROM UPDATE FAILED\n";
        const QByteArray failureReport = failureText.toUtf8();
        writeFileAtomically( resultPath, failureReport );
        const QByteArray failureState =
            QString( "STATE: %1\nUTC timestamp: %2\nNull half-width: %3 ADC count\nFailure: %4\n" )
                .arg( writeAttempted ? ( rollbackSucceeded ? "ROLLBACK VERIFIED" : "ROLLBACK FAILED" )
                                     : "CANCELLED BEFORE WRITE",
                      bundle.timestamp, QString::number( bundle.nullHalfWidth, 'f', 2 ), transactionError )
                .toUtf8();
        writeFileAtomically( statePath, failureState );

        message = rollbackSucceeded
                      ? tr( "EEPROM update cancelled or failed safely; the original data is verified. %1" )
                            .arg( transactionError )
                      : tr( "CRITICAL: EEPROM update and automatic rollback failed. %1 Safety folder: %2" )
                            .arg( transactionError, bundle.outputDirectory );
        emit statusMessage( message, 0 );
        if ( !rollbackSucceeded )
            emit communicationError();
        emit eepromCalibrationUpdateFinished( false, writeAttempted, rollbackSucceeded, bundle.outputDirectory,
                                              resultPath, message );
        return;
    }

    message = tr( "EEPROM update and readback verified. Safety bundle: %1" ).arg( bundle.outputDirectory );
    emit statusMessage( message, 0 );
    emit eepromCalibrationUpdateFinished( true, true, true, bundle.outputDirectory, resultPath, message );
}


Dso::ErrorCode HantekDsoControl::getCalibrationFromEEPROM() {
    // Get calibration data from EEPROM
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::getCalibrationFromEEPROM()";
    int errorCode = -1;
    bool realHardware = false;
    {
        QReadLocker locker( &scopeDeviceLock );
        realHardware = scopeDevice && deviceConnectionState == DeviceConnectionState::Connected && scopeDevice->isRealHW();
        if ( realHardware && specification->hasCalibrationEEPROM )
            errorCode = scopeDevice->controlRead( &controlsettings.cmdGetCalibration );
    }
    if ( errorCode < 0 ) {
        // invalidate the calibration values.
        memset( controlsettings.calibrationValues, 0xFF, sizeof( CalibrationValues ) );
        if ( realHardware ) {
            QString message = tr( "Couldn't get calibration data from oscilloscope's EEPROM. Use a config file for calibration!" );
            qWarning() << message;
            emit statusMessage( message, 0 );
            emit communicationError();
            return Dso::ErrorCode::CONNECTION;
        } else {
            return Dso::ErrorCode::NONE;
        }
    }
    memcpy( controlsettings.calibrationValues, controlsettings.cmdGetCalibration.data(), sizeof( CalibrationValues ) );
    if ( verboseLevel > 3 ) {
        QDebug line = qDebug().noquote();
        line << "   HDC::calibrationValues" << sizeof( CalibrationValues );
        line = qDebug().noquote() << "   .off.ls: ";
        for ( int g = 0; g < 8; ++g )
            for ( int c = 0; c < 2; ++c )
                line << QString::number( controlsettings.calibrationValues->off.ls.step[ g ][ c ], 16 );
        line = qDebug().noquote() << "   .off.hs: ";
        for ( int g = 0; g < 8; ++g )
            for ( int c = 0; c < 2; ++c )
                line << QString::number( controlsettings.calibrationValues->off.hs.step[ g ][ c ], 16 );
        line = qDebug().noquote() << "   .gain:   ";
        for ( int g = 0; g < 8; ++g )
            for ( int c = 0; c < 2; ++c )
                line << QString::number( controlsettings.calibrationValues->gain.step[ g ][ c ], 16 );
        line = qDebug().noquote() << "   .fine.ls:";
        for ( int g = 0; g < 8; ++g )
            for ( int c = 0; c < 2; ++c )
                line << QString::number( controlsettings.calibrationValues->fine.ls.step[ g ][ c ], 16 );
        line = qDebug().noquote() << "   .fine.hs:";
        for ( int g = 0; g < 8; ++g )
            for ( int c = 0; c < 2; ++c )
                line << QString::number( controlsettings.calibrationValues->fine.hs.step[ g ][ c ], 16 );
    }
    return Dso::ErrorCode::NONE;
}


void HantekDsoControl::resetOffsetCalibration() {
    memset( offsetCalibrationSum, 0, sizeof( offsetCalibrationSum ) );
    memset( offsetCalibrationFirst, 0, sizeof( offsetCalibrationFirst ) );
    memset( offsetCalibrationFrames, 0, sizeof( offsetCalibrationFrames ) );
    memset( offsetCalibrationComplete, 0, sizeof( offsetCalibrationComplete ) );
    memset( offsetCalibrationUnstableRetries, 0, sizeof( offsetCalibrationUnstableRetries ) );
    memset( offsetCalibrationClippedRejections, 0, sizeof( offsetCalibrationClippedRejections ) );
    for ( unsigned channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel )
        offsetCalibrationLastGain[ channel ] = HANTEK_GAIN_STEPS;
}


unsigned HantekDsoControl::completedOffsetCalibrationRanges() const {
    unsigned completed = 0;
    for ( unsigned gainIndex = 0; gainIndex < HANTEK_GAIN_STEPS; ++gainIndex )
        for ( unsigned channel = 0; channel < HANTEK_CHANNEL_NUMBER; ++channel )
            if ( offsetCalibrationComplete[ gainIndex ][ channel ] )
                ++completed;
    return completed;
}


void HantekDsoControl::processOffsetCalibrationFrame( ChannelID channel, unsigned gainIndex, double liveOffset, bool clipped ) {
    if ( channel >= HANTEK_CHANNEL_NUMBER || gainIndex >= HANTEK_GAIN_STEPS )
        return;

    if ( offsetCalibrationLastGain[ channel ] != gainIndex ) {
        if ( offsetRepeatabilityStudyActive ) {
            offsetRepeatabilityFrameEvents.push_back(
                { offsetRepeatabilityStudyRun, gainIndex, channel,
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ), liveOffset,
                  std::numeric_limits< double >::quiet_NaN(), result.samplerate, "discarded-after-range-change" } );
        }
        offsetCalibrationLastGain[ channel ] = gainIndex;
        offsetCalibrationSum[ gainIndex ][ channel ] = 0.0;
        offsetCalibrationFrames[ gainIndex ][ channel ] = 0;
        return; // discard the first frame after changing the input range
    }
    if ( offsetCalibrationComplete[ gainIndex ][ channel ] )
        return;

    constexpr double maximumOffset = 25.0;
    constexpr double maximumFrameDifference = 1.0;
    if ( clipped || qAbs( liveOffset ) > maximumOffset ) {
        if ( offsetRepeatabilityStudyActive ) {
            const QString decision = clipped ? "rejected-clipped" : "rejected-out-of-range";
            offsetRepeatabilityFrameEvents.push_back(
                { offsetRepeatabilityStudyRun, gainIndex, channel,
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ), liveOffset,
                  std::numeric_limits< double >::quiet_NaN(), result.samplerate, decision } );
            if ( clipped )
                ++offsetCalibrationClippedRejections[ gainIndex ][ channel ];
            else
                ++offsetCalibrationUnstableRetries[ gainIndex ][ channel ];
        }
        offsetCalibrationSum[ gainIndex ][ channel ] = 0.0;
        offsetCalibrationFrames[ gainIndex ][ channel ] = 0;
        emit statusMessage( tr( "Offset calibration is waiting for a stable, grounded signal on CH%1." ).arg( channel + 1 ),
                            3000 );
        return;
    }

    if ( !offsetCalibrationFrames[ gainIndex ][ channel ] ) {
        if ( offsetRepeatabilityStudyActive ) {
            offsetRepeatabilityFrameEvents.push_back(
                { offsetRepeatabilityStudyRun, gainIndex, channel,
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ), liveOffset,
                  std::numeric_limits< double >::quiet_NaN(), result.samplerate, "accepted-first" } );
        }
        offsetCalibrationFirst[ gainIndex ][ channel ] = liveOffset;
        offsetCalibrationSum[ gainIndex ][ channel ] = liveOffset;
        offsetCalibrationFrames[ gainIndex ][ channel ] = 1;
        return;
    }

    const double frameDifference = liveOffset - offsetCalibrationFirst[ gainIndex ][ channel ];
    if ( qAbs( frameDifference ) > maximumFrameDifference ) {
        if ( offsetRepeatabilityStudyActive ) {
            offsetRepeatabilityFrameEvents.push_back(
                { offsetRepeatabilityStudyRun, gainIndex, channel,
                  QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ), liveOffset, frameDifference,
                  result.samplerate, "unstable-reset-new-first" } );
            ++offsetCalibrationUnstableRetries[ gainIndex ][ channel ];
        }
        offsetCalibrationFirst[ gainIndex ][ channel ] = liveOffset;
        offsetCalibrationSum[ gainIndex ][ channel ] = liveOffset;
        offsetCalibrationFrames[ gainIndex ][ channel ] = 1;
        emit statusMessage( tr( "Offset calibration is retrying an unstable measurement on CH%1." ).arg( channel + 1 ),
                            3000 );
        return;
    }

    offsetCalibrationSum[ gainIndex ][ channel ] += liveOffset;
    ++offsetCalibrationFrames[ gainIndex ][ channel ];
    constexpr unsigned stableFramesNeeded = 2;
    if ( offsetCalibrationFrames[ gainIndex ][ channel ] < stableFramesNeeded )
        return;

    const double measuredOffset = offsetCalibrationSum[ gainIndex ][ channel ] / offsetCalibrationFrames[ gainIndex ][ channel ];
    if ( offsetRepeatabilityStudyActive ) {
        offsetRepeatabilityFrameEvents.push_back(
            { offsetRepeatabilityStudyRun, gainIndex, channel,
              QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ), liveOffset, frameDifference,
              result.samplerate, "accepted-final" } );
        offsetRepeatabilityResults.push_back(
            { offsetRepeatabilityStudyRun, gainIndex, channel,
              QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ),
              offsetCalibrationFirst[ gainIndex ][ channel ], liveOffset, measuredOffset, result.samplerate,
              offsetCalibrationUnstableRetries[ gainIndex ][ channel ],
              offsetCalibrationClippedRejections[ gainIndex ][ channel ] } );
    } else {
        offsetCorrection[ gainIndex ][ channel ] = measuredOffset;
    }
    offsetCalibrationComplete[ gainIndex ][ channel ] = true;

    const unsigned completed = completedOffsetCalibrationRanges();
    const unsigned total = HANTEK_GAIN_STEPS * HANTEK_CHANNEL_NUMBER;
    if ( completed == total ) {
        if ( offsetRepeatabilityStudyActive ) {
            completeOffsetRepeatabilityRun();
            return;
        }
        emit statusMessage( tr( "Offset calibration measured all %1 ranges. Turn off Calibrate Offset to save." ).arg( total ),
                            0 );
    } else {
        const int millivolts = int( round( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) );
        emit statusMessage( tr( "Offset calibration: CH%1 %2 mV/div complete (%3 of %4)." )
                                .arg( channel + 1 )
                                .arg( millivolts )
                                .arg( completed )
                                .arg( total ),
                            3000 );
    }
}


void HantekDsoControl::startOffsetRepeatabilityStudy() {
    offsetRepeatabilityStudyDirectory.clear();
    offsetRepeatabilityStudyTimestamp.clear();
    QString message;
    const auto fail = [ this, &message ]( const QString &reason ) {
        message = tr( "Offset repeatability study could not start: %1 No calibration or EEPROM data was changed." )
                      .arg( reason );
        emit statusMessage( message, 0 );
        emit offsetRepeatabilityStudyStateChanged( false );
        emit offsetRepeatabilityStudyFinished( false, offsetRepeatabilityStudyDirectory, QString(), message );
    };

    if ( offsetCalibrationActive ) {
        fail( tr( "finish or cancel the current calibration operation first." ) );
        return;
    }
    if ( model->spec()->gain.size() != HANTEK_GAIN_STEPS ) {
        fail( tr( "the oscilloscope does not use the expected eight-range calibration layout." ) );
        return;
    }
    if ( controlsettings.voltage.size() < HANTEK_CHANNEL_NUMBER || !controlsettings.voltage[ 0 ].used ||
         !controlsettings.voltage[ 1 ].used ) {
        fail( tr( "both channels must be enabled." ) );
        return;
    }
    if ( controlsettings.samplerate.current < 10e3 || controlsettings.samplerate.current > 100e3 ) {
        fail( tr( "select a 10 to 100 kS/s sample rate, such as the 10 ms/div timebase." ) );
        return;
    }
    if ( calibrationFileName.isEmpty() || !QFileInfo::exists( calibrationFileName ) ) {
        fail( tr( "an active device-specific calibration INI file is required for the unchanged-file check." ) );
        return;
    }

    QFile iniFile( calibrationFileName );
    if ( !iniFile.open( QIODevice::ReadOnly ) ) {
        fail( tr( "the active calibration INI could not be opened for its safety snapshot." ) );
        return;
    }
    const QByteArray iniContents = iniFile.readAll();
    if ( iniFile.error() != QFileDevice::NoError ) {
        fail( tr( "the active calibration INI could not be read reliably." ) );
        return;
    }

    QByteArray eepromBytes;
    QByteArray eepromVerification;
    QString deviceModel;
    QString deviceSerial;
    {
        QWriteLocker locker( &scopeDeviceLock );
        if ( !scopeDevice || deviceConnectionState != DeviceConnectionState::Connected || !scopeDevice->isConnected() ||
             !scopeDevice->isRealHW() || !specification->hasCalibrationEEPROM ) {
            fail( tr( "a connected physical oscilloscope with calibration EEPROM is required." ) );
            return;
        }

        deviceModel = scopeDevice->getModel()->name;
        deviceSerial = scopeDevice->getSerialNumber();
        QString readError;
        if ( !readCalibrationRegion( scopeDevice, eepromBytes, readError ) ||
             !readCalibrationRegion( scopeDevice, eepromVerification, readError ) ) {
            fail( tr( "two complete 80-byte EEPROM reads could not be obtained: %1." ).arg( readError ) );
            return;
        }
    }
    if ( eepromBytes != eepromVerification ) {
        fail( tr( "two consecutive EEPROM reads did not match." ) );
        return;
    }

    offsetRepeatabilityStudyTimestamp = QDateTime::currentDateTimeUtc().toString( "yyyyMMdd'T'HHmmsszzz'Z'" );
    const QString deviceDirectory = QFileInfo( calibrationFileName ).completeBaseName();
    offsetRepeatabilityStudyDirectory =
        QDir( QFileInfo( calibrationFileName ).absolutePath() )
            .filePath( "Offset Repeatability Studies/" + deviceDirectory + "/" + offsetRepeatabilityStudyTimestamp );
    if ( !QDir().mkpath( offsetRepeatabilityStudyDirectory ) ) {
        fail( tr( "the study output folder could not be created." ) );
        return;
    }

    const QString iniSnapshotPath =
        QDir( offsetRepeatabilityStudyDirectory ).filePath( "calibration-ini-before-study.ini" );
    const QString eepromSnapshotPath =
        QDir( offsetRepeatabilityStudyDirectory ).filePath( "eeprom-calibration-original-80-bytes.bin" );
    if ( !writeFileAtomically( iniSnapshotPath, iniContents ) ||
         !writeFileAtomically( eepromSnapshotPath, eepromBytes ) ) {
        fail( tr( "the initial INI and EEPROM snapshots could not be saved and verified." ) );
        return;
    }

    QString initialMetadata;
    QTextStream metadata( &initialMetadata );
    metadata << "OpenHantek6022 offset repeatability study\n"
             << "============================================\n\n"
             << "STATUS: COLLECTION IN PROGRESS; EEPROM AND ACTIVE INI NOT WRITTEN\n\n"
             << "UTC timestamp: " << offsetRepeatabilityStudyTimestamp << "\n"
             << "Device model: " << deviceModel << "\n"
             << "Device serial: " << deviceSerial << "\n"
             << "Application version: " << QCoreApplication::applicationVersion() << "\n"
             << "Planned complete runs: " << OFFSET_REPEATABILITY_RUNS << "\n"
             << "Combinations per run: " << HANTEK_GAIN_STEPS * HANTEK_CHANNEL_NUMBER << "\n"
             << "Initial sample rate: " << QString::number( controlsettings.samplerate.current, 'f', 3 ) << " S/s\n"
             << "Odd runs: ascending from 20 to 5000 mV/div\n"
             << "Even runs: descending from 5000 to 20 mV/div\n";
    if ( !writeFileAtomically( QDir( offsetRepeatabilityStudyDirectory ).filePath( "study-metadata.txt" ),
                               initialMetadata.toUtf8() ) ) {
        fail( tr( "the initial study metadata could not be saved and verified." ) );
        return;
    }

    offsetRepeatabilityStudyDeviceModel = deviceModel;
    offsetRepeatabilityStudyDeviceSerial = deviceSerial;
    offsetRepeatabilityStudyIniContents = iniContents;
    offsetRepeatabilityStudyEEPROMBytes = eepromBytes;
    offsetRepeatabilityStudyFinalizationPending = false;
    offsetRepeatabilityStudyRun = 0;
    offsetRepeatabilityFrameEvents.clear();
    offsetRepeatabilityResults.clear();
    for ( unsigned run = 0; run < OFFSET_REPEATABILITY_RUNS; ++run ) {
        offsetRepeatabilityRunStarted[ run ].clear();
        offsetRepeatabilityRunCompleted[ run ].clear();
    }
    offsetRepeatabilityRunStarted[ 0 ] = QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
    memcpy( offsetCalibrationOriginal, offsetCorrection, sizeof( offsetCorrection ) );
    resetOffsetCalibration();
    offsetRepeatabilityStudyActive = true;
    offsetCalibrationActive = true;

    emit offsetRepeatabilityStudyStateChanged( true );
    message =
        tr( "Offset repeatability study run 1 of 8: select 20, 50, 100, 200, 500, 1000, 2000, then 5000 mV/div." );
    emit statusMessage( message, 0 );
}


void HantekDsoControl::cancelOffsetRepeatabilityStudy() {
    if ( !offsetRepeatabilityStudyActive )
        return;

    finishOffsetRepeatabilityStudy( false, tr( "The study was cancelled before all eight runs were complete." ) );
}


void HantekDsoControl::continueOffsetRepeatabilityStudy() {
    if ( !offsetRepeatabilityStudyActive || offsetCalibrationActive ||
         offsetRepeatabilityStudyRun + 1 >= OFFSET_REPEATABILITY_RUNS )
        return;

    ++offsetRepeatabilityStudyRun;
    resetOffsetCalibration();
    offsetRepeatabilityRunStarted[ offsetRepeatabilityStudyRun ] =
        QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
    offsetCalibrationActive = true;

    const bool ascending = offsetRepeatabilityStudyRun % 2 == 0;
    const QString direction =
        ascending
            ? tr( "select 20, 50, 100, 200, 500, 1000, 2000, then 5000 mV/div." )
            : tr( "select 5000, 2000, 1000, 500, 200, 100, 50, then 20 mV/div." );
    emit statusMessage( tr( "Offset repeatability study run %1 of %2: %3" )
                            .arg( offsetRepeatabilityStudyRun + 1 )
                            .arg( OFFSET_REPEATABILITY_RUNS )
                            .arg( direction ),
                        0 );
}


void HantekDsoControl::completeOffsetRepeatabilityRun() {
    if ( !offsetRepeatabilityStudyActive || offsetRepeatabilityStudyRun >= OFFSET_REPEATABILITY_RUNS )
        return;

    offsetRepeatabilityRunCompleted[ offsetRepeatabilityStudyRun ] =
        QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs );
    if ( offsetRepeatabilityStudyRun + 1 >= OFFSET_REPEATABILITY_RUNS ) {
        offsetCalibrationActive = false;
        // Finalization needs exclusive device access. Defer it until sample conversion releases raw/result locks.
        offsetRepeatabilityStudyFinalizationPending = true;
        emit statusMessage( tr( "Offset repeatability study run 8 complete. Verifying unchanged calibration data." ),
                            0 );
        return;
    }

    offsetCalibrationActive = false;
    const unsigned nextRun = offsetRepeatabilityStudyRun + 1;
    emit statusMessage( tr( "Offset repeatability study run %1 complete. Pause before continuing with run %2." )
                            .arg( offsetRepeatabilityStudyRun + 1 )
                            .arg( nextRun + 1 ),
                        0 );
    emit offsetRepeatabilityStudyRunCompleted( offsetRepeatabilityStudyRun + 1, nextRun + 1,
                                               nextRun % 2 == 0 );
}


void HantekDsoControl::finishOffsetRepeatabilityStudy( bool completed, const QString &reason ) {
    offsetCalibrationActive = false;
    offsetRepeatabilityStudyActive = false;
    offsetRepeatabilityStudyFinalizationPending = false;

    const unsigned expectedResults = OFFSET_REPEATABILITY_RUNS * HANTEK_GAIN_STEPS * HANTEK_CHANNEL_NUMBER;
    const bool resultCountComplete = offsetRepeatabilityResults.size() == std::size_t( expectedResults );

    QByteArray finalIniContents;
    bool iniUnchanged = false;
    QFile finalIni( calibrationFileName );
    if ( finalIni.open( QIODevice::ReadOnly ) ) {
        finalIniContents = finalIni.readAll();
        iniUnchanged = finalIni.error() == QFileDevice::NoError &&
                       finalIniContents == offsetRepeatabilityStudyIniContents;
    }

    QByteArray finalEEPROMBytes;
    QByteArray finalEEPROMVerification;
    bool eepromUnchanged = false;
    {
        QWriteLocker locker( &scopeDeviceLock );
        if ( scopeDevice && deviceConnectionState == DeviceConnectionState::Connected && scopeDevice->isConnected() &&
             scopeDevice->isRealHW() && specification->hasCalibrationEEPROM ) {
            QString readError;
            eepromUnchanged = readCalibrationRegion( scopeDevice, finalEEPROMBytes, readError ) &&
                              readCalibrationRegion( scopeDevice, finalEEPROMVerification, readError ) &&
                              finalEEPROMBytes == finalEEPROMVerification &&
                              finalEEPROMBytes == offsetRepeatabilityStudyEEPROMBytes;
        }
    }

    auto sortedResults = offsetRepeatabilityResults;
    std::sort( sortedResults.begin(), sortedResults.end(), []( const OffsetRepeatabilityResult &left,
                                                               const OffsetRepeatabilityResult &right ) {
        if ( left.run != right.run )
            return left.run < right.run;
        if ( left.gainIndex != right.gainIndex )
            return left.gainIndex < right.gainIndex;
        return left.channel < right.channel;
    } );

    double minimumSamplerate = std::numeric_limits< double >::max();
    double maximumSamplerate = 0.0;
    for ( const OffsetRepeatabilityResult &record : sortedResults ) {
        minimumSamplerate = qMin( minimumSamplerate, record.samplerate );
        maximumSamplerate = qMax( maximumSamplerate, record.samplerate );
    }
    const bool samplerateConsistent =
        resultCountComplete && minimumSamplerate >= 10e3 && maximumSamplerate <= 100e3 &&
        maximumSamplerate - minimumSamplerate <= qMax( 1.0, maximumSamplerate * 0.001 );

    QString frameCsvText;
    QTextStream frameCsv( &frameCsvText );
    frameCsv << "run,utc_timestamp,channel,range_mV_div,live_offset_adc_count,difference_from_first_adc_count,"
                "sample_rate_S_per_s,decision\n";
    for ( const OffsetRepeatabilityFrameEvent &event : offsetRepeatabilityFrameEvents ) {
        const int rangeMillivolts = int( qRound( model->spec()->gain[ event.gainIndex ].Vdiv * 1000 ) );
        frameCsv << event.run + 1 << "," << event.timestamp << ",CH" << event.channel + 1 << ","
                 << rangeMillivolts << "," << QString::number( event.liveOffset, 'f', 9 ) << ","
                 << ( qIsFinite( event.differenceFromFirst )
                          ? QString::number( event.differenceFromFirst, 'f', 9 )
                          : QString() )
                 << "," << QString::number( event.samplerate, 'f', 3 ) << "," << event.decision << "\n";
    }

    QString runMeansCsvText;
    QTextStream runMeansCsv( &runMeansCsvText );
    runMeansCsv << "run,run_started_utc,run_completed_utc,measurement_completed_utc,range_order,channel,range_mV_div,"
                   "first_frame_adc_count,second_frame_adc_count,mean_adc_count,absolute_frame_difference_adc_count,"
                   "unstable_retry_count,clipped_rejection_count,sample_rate_S_per_s\n";
    for ( const OffsetRepeatabilityResult &record : sortedResults ) {
        const int rangeMillivolts = int( qRound( model->spec()->gain[ record.gainIndex ].Vdiv * 1000 ) );
        runMeansCsv << record.run + 1 << "," << offsetRepeatabilityRunStarted[ record.run ] << ","
                    << offsetRepeatabilityRunCompleted[ record.run ] << "," << record.timestamp << ","
                    << ( record.run % 2 == 0 ? "ascending" : "descending" ) << ",CH" << record.channel + 1 << ","
                    << rangeMillivolts << "," << QString::number( record.firstFrame, 'f', 9 ) << ","
                    << QString::number( record.secondFrame, 'f', 9 ) << ","
                    << QString::number( record.mean, 'f', 9 ) << ","
                    << QString::number( qAbs( record.secondFrame - record.firstFrame ), 'f', 9 ) << ","
                    << record.unstableRetries << "," << record.clippedRejections << ","
                    << QString::number( record.samplerate, 'f', 3 ) << "\n";
    }

    QString analysisCsvText;
    QTextStream analysisCsv( &analysisCsvText );
    analysisCsv << "channel,range_mV_div,run_1_mean,run_2_mean,run_3_mean,run_4_mean,run_5_mean,run_6_mean,"
                   "run_7_mean,run_8_mean,mean,median,sample_sd,mad,robust_sigma,within_run_mean_noise,"
                   "effective_sigma,min,max,peak_to_peak,max_deviation_from_median,ci95_low,ci95_high,"
                   "linear_drift_per_run,suggested_null_half_width,interpretation\n";

    std::vector< double > suggestedWidths;
    bool analysisComplete = resultCountComplete;
    for ( unsigned channel = 0; analysisComplete && channel < HANTEK_CHANNEL_NUMBER; ++channel ) {
        for ( unsigned gainIndex = 0; analysisComplete && gainIndex < HANTEK_GAIN_STEPS; ++gainIndex ) {
            std::vector< double > means;
            std::vector< double > frameDifferences;
            for ( unsigned run = 0; run < OFFSET_REPEATABILITY_RUNS; ++run ) {
                const auto match = std::find_if(
                    sortedResults.begin(), sortedResults.end(),
                    [ run, gainIndex, channel ]( const OffsetRepeatabilityResult &record ) {
                        return record.run == run && record.gainIndex == gainIndex && record.channel == channel;
                    } );
                if ( match == sortedResults.end() ) {
                    analysisComplete = false;
                    break;
                }
                means.push_back( match->mean );
                frameDifferences.push_back( match->secondFrame - match->firstFrame );
            }
            if ( !analysisComplete )
                break;

            double mean = 0.0;
            for ( double value : means )
                mean += value;
            mean /= means.size();

            double varianceSum = 0.0;
            for ( double value : means )
                varianceSum += ( value - mean ) * ( value - mean );
            const double sampleStandardDeviation = std::sqrt( varianceSum / ( means.size() - 1 ) );
            const double medianValue = median( means );

            std::vector< double > absoluteDeviations;
            double maximumDeviation = 0.0;
            for ( double value : means ) {
                const double deviation = qAbs( value - medianValue );
                absoluteDeviations.push_back( deviation );
                maximumDeviation = qMax( maximumDeviation, deviation );
            }
            const double medianAbsoluteDeviation = median( absoluteDeviations );
            const double robustSigma = 1.4826 * medianAbsoluteDeviation;

            double withinRunSquares = 0.0;
            for ( double difference : frameDifferences )
                withinRunSquares += difference * difference;
            const double withinRunMeanNoise = std::sqrt( withinRunSquares / frameDifferences.size() ) / 2.0;
            const double effectiveSigma = qMax( sampleStandardDeviation, qMax( robustSigma, withinRunMeanNoise ) );
            const auto minMax = std::minmax_element( means.begin(), means.end() );
            const double confidenceHalfWidth = 2.365 * sampleStandardDeviation / std::sqrt( double( means.size() ) );
            const double confidenceLow = mean - confidenceHalfWidth;
            const double confidenceHigh = mean + confidenceHalfWidth;

            const double runCentre = ( OFFSET_REPEATABILITY_RUNS + 1 ) / 2.0;
            double slopeNumerator = 0.0;
            double slopeDenominator = 0.0;
            for ( unsigned run = 0; run < OFFSET_REPEATABILITY_RUNS; ++run ) {
                const double centredRun = double( run + 1 ) - runCentre;
                slopeNumerator += centredRun * ( means[ run ] - mean );
                slopeDenominator += centredRun * centredRun;
            }
            const double slope = slopeDenominator > 0.0 ? slopeNumerator / slopeDenominator : 0.0;
            const double suggestedWidth =
                std::ceil( 100.0 * qMax( 0.01, qMax( 3.5 * effectiveSigma, maximumDeviation + 0.01 ) ) ) / 100.0;
            suggestedWidths.push_back( suggestedWidth );
            const QString interpretation =
                confidenceLow > 0.0 || confidenceHigh < 0.0 ? "repeatable-residual" : "consistent-with-zero";
            const int rangeMillivolts = int( qRound( model->spec()->gain[ gainIndex ].Vdiv * 1000 ) );

            analysisCsv << "CH" << channel + 1 << "," << rangeMillivolts;
            for ( double value : means )
                analysisCsv << "," << QString::number( value, 'f', 9 );
            analysisCsv << "," << QString::number( mean, 'f', 9 ) << ","
                        << QString::number( medianValue, 'f', 9 ) << ","
                        << QString::number( sampleStandardDeviation, 'f', 9 ) << ","
                        << QString::number( medianAbsoluteDeviation, 'f', 9 ) << ","
                        << QString::number( robustSigma, 'f', 9 ) << ","
                        << QString::number( withinRunMeanNoise, 'f', 9 ) << ","
                        << QString::number( effectiveSigma, 'f', 9 ) << ","
                        << QString::number( *minMax.first, 'f', 9 ) << ","
                        << QString::number( *minMax.second, 'f', 9 ) << ","
                        << QString::number( *minMax.second - *minMax.first, 'f', 9 ) << ","
                        << QString::number( maximumDeviation, 'f', 9 ) << ","
                        << QString::number( confidenceLow, 'f', 9 ) << ","
                        << QString::number( confidenceHigh, 'f', 9 ) << ","
                        << QString::number( slope, 'f', 9 ) << ","
                        << QString::number( suggestedWidth, 'f', 2 ) << "," << interpretation << "\n";
        }
    }

    double globalSuggestedWidth = 0.0;
    double medianSuggestedWidth = 0.0;
    if ( analysisComplete && !suggestedWidths.empty() ) {
        globalSuggestedWidth = *std::max_element( suggestedWidths.begin(), suggestedWidths.end() );
        medianSuggestedWidth = median( suggestedWidths );
    }

    const bool studyComplete = completed && resultCountComplete && analysisComplete;
    QString metadataText;
    QTextStream metadata( &metadataText );
    metadata << "OpenHantek6022 offset repeatability study\n"
             << "============================================\n\n"
             << "STATUS: " << ( studyComplete ? "EIGHT RUNS COLLECTED" : "INCOMPLETE" ) << "\n"
             << "EEPROM write commands issued: no\n"
             << "Active INI write commands issued: no\n\n"
             << "UTC timestamp: " << offsetRepeatabilityStudyTimestamp << "\n"
             << "Device model: " << offsetRepeatabilityStudyDeviceModel << "\n"
             << "Device serial: " << offsetRepeatabilityStudyDeviceSerial << "\n"
             << "Application version: " << QCoreApplication::applicationVersion() << "\n"
             << "Completed results: " << qulonglong( offsetRepeatabilityResults.size() ) << " of " << expectedResults
             << "\n"
             << "Frame events recorded: " << qulonglong( offsetRepeatabilityFrameEvents.size() ) << "\n"
             << "Sample-rate consistency: " << ( samplerateConsistent ? "verified" : "NOT VERIFIED" ) << "\n";
    for ( unsigned run = 0; run < OFFSET_REPEATABILITY_RUNS; ++run ) {
        metadata << "Run " << run + 1 << " started: " << offsetRepeatabilityRunStarted[ run ]
                 << "; completed: " << offsetRepeatabilityRunCompleted[ run ]
                 << "; order: " << ( run % 2 == 0 ? "ascending" : "descending" ) << "\n";
    }
    if ( !reason.isEmpty() )
        metadata << "Completion note: " << reason << "\n";

    QString reportText;
    QTextStream report( &reportText );
    report << "OpenHantek6022 offset repeatability analysis\n"
           << "================================================\n\n"
           << "RESULT: " << ( studyComplete ? "EIGHT-RUN DATASET COMPLETE" : "STUDY INCOMPLETE" ) << "\n\n"
           << "Device: " << offsetRepeatabilityStudyDeviceModel << " "
           << offsetRepeatabilityStudyDeviceSerial << "\n"
           << "Study timestamp: " << offsetRepeatabilityStudyTimestamp << "\n"
           << "Completed range results: " << qulonglong( offsetRepeatabilityResults.size() ) << " of "
           << expectedResults << "\n"
           << "Active INI unchanged: " << ( iniUnchanged ? "verified" : "NOT VERIFIED" ) << "\n"
           << "EEPROM unchanged after two matching final reads: "
           << ( eepromUnchanged ? "verified" : "NOT VERIFIED" ) << "\n"
           << "Sample rate remained within 10 to 100 kS/s and within 0.1% across results: "
           << ( samplerateConsistent ? "verified" : "NOT VERIFIED" ) << "\n"
           << "EEPROM write commands issued by this study: none\n"
           << "Active INI write commands issued by this study: none\n\n";
    if ( !sortedResults.empty() )
        report << "Observed sample-rate range: " << QString::number( minimumSamplerate, 'f', 3 ) << " to "
               << QString::number( maximumSamplerate, 'f', 3 ) << " S/s\n\n";
    if ( !reason.isEmpty() )
        report << "Completion note: " << reason << "\n\n";
    report << "Per-range analysis:\n"
           << "- Each run mean is the average of the two accepted stable frames.\n"
           << "- Robust sigma is 1.4826 times the median absolute deviation.\n"
           << "- Within-run mean noise is RMS(frame 2 - frame 1) divided by 2.\n"
           << "- Effective sigma is the maximum of sample SD, robust sigma, and within-run mean noise.\n"
           << "- Suggested half-width is rounded upward to 0.01 ADC count and is the maximum of\n"
           << "  0.01, 3.5 times effective sigma, and maximum observed median deviation plus 0.01.\n"
           << "- A 95% confidence interval excluding zero is labelled repeatable-residual.\n\n";
    if ( analysisComplete ) {
        report << "Provisional single global null half-width: "
               << QString::number( globalSuggestedWidth, 'f', 2 ) << " ADC count\n";
        if ( !samplerateConsistent )
            report << "WARNING: Do not use this null-width result because the sample rate was not consistent.\n";
        if ( medianSuggestedWidth > 0.0 && globalSuggestedWidth > 2.0 * medianSuggestedWidth )
            report << "WARNING: One or more ranges dominate the global width by more than 2x the median.\n";
        report << "This is a short-term starting value. Validate it across another day, temperature change,\n"
               << "or USB power cycle before using it as a long-term EEPROM update threshold.\n\n";
    } else {
        report << "No null-width recommendation was calculated because all eight runs were not complete.\n\n";
    }
    report << "Detailed measurements: offset-calibration-run-means.csv\n"
           << "Every accepted and rejected frame event: offset-calibration-frames.csv\n"
           << "Per-range statistics: offset-repeatability-analysis.csv\n";

    struct StudyArtifact {
        QString fileName;
        QByteArray contents;
    };
    std::vector< StudyArtifact > artifacts = {
        { "study-metadata.txt", metadataText.toUtf8() },
        { "calibration-ini-before-study.ini", offsetRepeatabilityStudyIniContents },
        { "eeprom-calibration-original-80-bytes.bin", offsetRepeatabilityStudyEEPROMBytes },
        { "offset-calibration-frames.csv", frameCsvText.toUtf8() },
        { "offset-calibration-run-means.csv", runMeansCsvText.toUtf8() },
        { "offset-repeatability-analysis.csv", analysisCsvText.toUtf8() },
        { "offset-repeatability-report.txt", reportText.toUtf8() },
    };
    if ( !finalEEPROMBytes.isEmpty() )
        artifacts.push_back( { "eeprom-calibration-final-80-bytes.bin", finalEEPROMBytes } );

    bool artifactsWritten = true;
    QByteArray checksums;
    for ( const StudyArtifact &artifact : artifacts ) {
        const QString path = QDir( offsetRepeatabilityStudyDirectory ).filePath( artifact.fileName );
        if ( !writeFileAtomically( path, artifact.contents ) )
            artifactsWritten = false;
        checksums += sha256( artifact.contents ) + "  " + artifact.fileName.toUtf8() + "\n";
    }
    if ( !writeFileAtomically( QDir( offsetRepeatabilityStudyDirectory ).filePath( "SHA256SUMS.txt" ), checksums ) )
        artifactsWritten = false;

    const bool success = studyComplete && samplerateConsistent && iniUnchanged && eepromUnchanged && artifactsWritten;
    const QString reportPath =
        artifactsWritten ? QDir( offsetRepeatabilityStudyDirectory ).filePath( "offset-repeatability-report.txt" )
                         : QString();
    QString message;
    if ( success ) {
        message =
            tr( "Eight-run offset repeatability study completed without changing EEPROM or the active INI. Bundle: %1" )
                .arg( offsetRepeatabilityStudyDirectory );
    } else if ( !artifactsWritten ) {
        message = tr( "The offset repeatability study ended, but one or more study files could not be saved or verified. "
                      "Partial folder: %1" )
                      .arg( offsetRepeatabilityStudyDirectory );
    } else {
        message =
            tr( "The offset repeatability study is incomplete or its unchanged-state checks did not pass. Review: %1" )
                .arg( reportPath );
    }

    emit statusMessage( message, 0 );
    emit offsetRepeatabilityStudyStateChanged( false );
    emit offsetRepeatabilityStudyFinished( success, offsetRepeatabilityStudyDirectory, reportPath, message );
}


void HantekDsoControl::calibrateOffset( bool enable ) {
    if ( offsetRepeatabilityStudyActive ) {
        emit statusMessage( tr( "Finish or cancel the offset repeatability study before calibrating offset." ), 0 );
        emit offsetCalibrationStateChanged( false );
        return;
    }

    if ( enable ) {
        if ( offsetCalibrationActive )
            return;
        if ( controlsettings.voltage.size() < HANTEK_CHANNEL_NUMBER || !controlsettings.voltage[ 0 ].used ||
             !controlsettings.voltage[ 1 ].used ) {
            emit statusMessage( tr( "Offset calibration requires both channels to be enabled." ), 0 );
            emit offsetCalibrationStateChanged( false );
            return;
        }
        if ( controlsettings.samplerate.current < 10e3 || controlsettings.samplerate.current > 100e3 ) {
            emit statusMessage( tr( "Offset calibration requires a 10 to 100 kS/s sample rate. Select 10 ms/div and try again." ),
                                0 );
            emit offsetCalibrationStateChanged( false );
            return;
        }

        memcpy( offsetCalibrationOriginal, offsetCorrection, sizeof( offsetCorrection ) );
        resetOffsetCalibration();
        offsetCalibrationActive = true;
        emit offsetCalibrationStateChanged( true );
        emit statusMessage( tr( "Offset calibration started. Slowly select every voltage range on both channels." ), 0 );
        return;
    }

    if ( !offsetCalibrationActive )
        return;
    offsetCalibrationActive = false;
    emit offsetCalibrationStateChanged( false );

    const unsigned completed = completedOffsetCalibrationRanges();
    const unsigned total = HANTEK_GAIN_STEPS * HANTEK_CHANNEL_NUMBER;
    if ( completed != total ) {
        memcpy( offsetCorrection, offsetCalibrationOriginal, sizeof( offsetCorrection ) );
        emit statusMessage( tr( "Offset calibration cancelled: %1 of %2 ranges measured. The INI file was not changed." )
                                .arg( completed )
                                .arg( total ),
                            0 );
        return;
    }

    if ( !saveOffsetCalibration() )
        memcpy( offsetCorrection, offsetCalibrationOriginal, sizeof( offsetCorrection ) );
}


void HantekDsoControl::quitSampling() {
    if ( verboseLevel > 2 )
        qDebug() << "  HDC::quitSampling()";
    enableSamplingUI( false );
    capturing = false;
    auto controlCommand = ControlStopSampling();
    timestampDebug( QString( "Sending control command 0x%1 (Stop Sampling): %2" )
                        .arg( QString::number( controlCommand.code, 16 ),
                              hexdecDump( controlCommand.data(), unsigned( controlCommand.size() ) ) ) );
    {
        QReadLocker locker( &scopeDeviceLock );
        if ( !scopeDevice )
            return;
        scopeDevice->stopSampling();
        if ( scopeDevice->isDemoDevice() || deviceConnectionState != DeviceConnectionState::Connected || !scopeDevice->isConnected() )
            return;
        int errorCode = scopeDevice->controlWrite( &controlCommand );
        if ( errorCode < 0 ) {
            qWarning() << "controlWrite: stop sampling failed: " << libUsbErrorString( errorCode );
            emit communicationError();
        }
        return;
    }
}


static double byteToGain( uint8_t gain ) {
    if ( gain && gain != 255 ) // data valid
        return 1.0 + ( gain - 0x80 ) / 500.0;
    else
        return 1.0;
}


void HantekDsoControl::convertRawDataToSamples() {
    QReadLocker rawLocker( &raw.lock );
    activeChannels = raw.channels;
    const unsigned rawSampleCount = unsigned( raw.data.size() ) / activeChannels;
    if ( !rawSampleCount )
        return;
    const unsigned rawOversampling = raw.oversampling;
    const bool freeRunning = rawSampleCount / rawOversampling < SAMPLESIZE; // amount needed for sw trigger
    const unsigned sampleCount = freeRunning ? rawSampleCount : netSampleCount( rawSampleCount );
    const unsigned resultSamples = freeRunning ? sampleCount / rawOversampling - 1 : sampleCount / rawOversampling;
    const unsigned skipSamples = rawSampleCount - sampleCount;
    if ( verboseLevel > 4 )
        qDebug() << "    HDC::convertRawDataToSamples()" << raw.tag;
    QWriteLocker resultLocker( &result.lock );
    result.freeRunning = freeRunning;
    result.tag = raw.tag;
    result.samplerate = raw.samplerate / raw.oversampling;
    // Prepare result buffers
    result.data.resize( specification->channels + 1 ); // CH0, CH1, MATH
    for ( ChannelID channelCounter = 0; channelCounter <= specification->channels; ++channelCounter )
        result.data[ channelCounter ].clear();

    // Convert channel data
    // Channels are using their separate buffers
    for ( ChannelID channel = 0; channel < activeChannels; ++channel ) {
        const unsigned gainIndex = raw.gainIndex[ channel ];
        const double voltageScale = specification->voltageScale[ channel ][ gainIndex ];
        const double probeAttn = controlsettings.voltage[ channel ].probeAttn;
        const double sign = controlsettings.voltage[ channel ].inverted ? -1.0 : 1.0;

        // shift + individual offset for each channel and gain
        // get offset value from eeprom[ 8 .. 39 and (if available) 56 .. 87]
        uint8_t offsetRaw;
        uint8_t offsetFine;
        if ( result.samplerate < 30e6 ) {
            offsetRaw = controlsettings.calibrationValues->off.ls.step[ gainIndex ][ channel ];
            offsetFine = controlsettings.calibrationValues->fine.ls.step[ gainIndex ][ channel ];
        } else {
            offsetRaw = controlsettings.calibrationValues->off.hs.step[ gainIndex ][ channel ];
            offsetFine = controlsettings.calibrationValues->fine.hs.step[ gainIndex ][ channel ];
        }
        // calibration values from EEPROM
        channelOffset[ channel ] = offsetRaw;
        double offsetCalibration = bytesToOffset( offsetRaw, offsetFine );
        double gainCalibration = byteToGain( controlsettings.calibrationValues->gain.step[ gainIndex ][ channel ] );
        // Convert data from the oscilloscope and write it into the channel sample buffer
        unsigned rawBufPos = 0;
        if ( raw.freeRun && raw.rollMode ) // show the "new" samples on the right screen side
            rawBufPos = raw.received;      // start with remaining "old" samples in buffer
        result.data[ channel ].resize( resultSamples );
        rawBufPos += skipSamples * activeChannels; // skip first unstable samples
        result.clipped &= ~( 0x01 << channel );    // clear clipping flag

        double gainCorr = gainCorrection[ gainIndex ][ channel ];
        double offsetCorr = result.samplerate < 30e6 ? offsetCorrection[ gainIndex ][ channel ]
                                                     : highSpeedOffsetCorrection[ gainIndex ][ channel ];
        double liveOffset = 0.0;
        bool calibrationClipped = false;

        for ( unsigned index = 0; index < resultSamples;
              ++index, rawBufPos += activeChannels * rawOversampling ) { // advance either by one or two blocks
            if ( rawBufPos + rawOversampling * activeChannels > rawSampleCount * activeChannels )
                rawBufPos = 0; // (roll mode) show "new" samples after the "old" samples
            double sample = 0.0;
            for ( unsigned iii = 0; iii < rawOversampling * activeChannels; iii += activeChannels ) {
                int rawSample = raw.data[ rawBufPos + channel + iii ]; // CH1/CH2/CH1/CH2 ...
                if ( rawSample == 0x00 || rawSample == 0xFF ) {        // min or max -> clipped
                    result.clipped |= 0x01 << channel;
                    calibrationClipped = true;
                }
                sample += double( rawSample ) - offsetCalibration;
            }
            sample /= rawOversampling;
            if ( offsetCalibrationActive )
                liveOffset += sample;
            // qDebug() << channel << offsetCorrection[ gainIndex ][ channel ];
            sample -= offsetCorr;
            sample *= gainCorr;

            result.data[ channel ][ index ] = sign * sample / voltageScale * gainCalibration * probeAttn;
        }
        if ( offsetCalibrationActive )
            processOffsetCalibrationFrame( channel, gainIndex, liveOffset / resultSamples, calibrationClipped );
    }
} // convertRawDataToSamples()


/// \brief Updates the interval of the periodic thread timer.
void HantekDsoControl::updateInterval() {
    // Check the current oscilloscope state every time 25% of the time
    //  the buffer should be refilled (-> acquireInterval in ms)
    // Use real 100% rate for demo device
    int sampleInterval = int( getSamplesize() * 1000.0 / controlsettings.samplerate.current );
    // Slower update reduces CPU load but it worsens the triggering of rare events
    // Display can be filled at slower rate (not faster than displayInterval)
    if ( scope ) // init is done
        acquireInterval = int( 1000 * scope->horizontal.acquireInterval );
    else
        acquireInterval = 1;
#ifdef Q_PROCESSOR_ARM
    displayInterval = 200; // update display at least every 200 ms
#else
    displayInterval = 100; // update display at least every 100 ms
#endif
    acquireInterval = qMin( qMax( sampleInterval, acquireInterval ), 100 ); // at least every 100 ms
}


/// \brief State machine for the device communication
void HantekDsoControl::stateMachine() {

    bool triggered = false;
    if ( verboseLevel > 4 )
        qDebug() << "    HDC::stateMachine()" << raw.tag;

    // we have a sample available ...
    // ... that is either a new sample or we are in free run mode or a new trigger search is needed
    static unsigned lastTag = UINT32_MAX; // detect new raw data
    if ( samplingStarted && raw.valid && ( raw.tag != lastTag || raw.freeRun || refreshNeeded() ) ) {
        lastTag = raw.tag;
        convertRawDataToSamples(); // process samples, apply gain settings etc.
        // This point is outside convertRawDataToSamples()'s raw/result lock scope.
        if ( offsetRepeatabilityStudyFinalizationPending )
            finishOffsetRepeatabilityStudy( true );
        mathChannel->calculate( result );
        QWriteLocker resultLocker( &result.lock );
        if ( !result.freeRunning ) { // trigger mode != NONE
            // trigger functions below are in separate file "triggering.cpp"
            triggering->searchTriggeredPosition( result );          // detect trigger point
            triggered = triggering->provideTriggeredData( result ); // present either free running or last triggered trace
        } else {                                                    // free running display
            triggered = false;
            result.triggeredPosition = 0;
        }
    } else { // TODO: check if this is needed anymore: start with correct calibration frequency
        static bool firstFreq = true;
        if ( firstFreq && scope ) {
            setCalFreq( scope->horizontal.calfreq );
            firstFreq = false;
        }
    }
    static int delayDisplay = 0;                // timer for display
    static bool lastTriggered = false;          // state of last frame
    static bool skipEven = true;                // even or odd frames were skipped
    delayDisplay += qMax( acquireInterval, 1 ); // count up with every state machine loop
    // always run the display (slowly at t=displayInterval) to allow user interaction
    // ... but update immediately if new triggered data is available after untriggered
    // skip an even number of frames when slope == Dso::Slope::Both
    if ( ( triggered && !lastTriggered )                                 // show new data immediately
         || ( ( delayDisplay >= displayInterval )                        // or wait some time ...
              && ( ( controlsettings.trigger.slope != Dso::Slope::Both ) // ... for ↗ or ↘ slope
                   || skipEven ) ) ) {                                   // and drop even no. of frames
        skipEven = true;                                                 // zero frames -> even
        delayDisplay = 0;
        timestampDebug( QString( "samplesAvailable %1" ).arg( result.tag ) );
        emit samplesAvailable( &result ); // via signal/slot -> PostProcessing::input()
    } else {
        skipEven = !skipEven;
    }
    lastTriggered = triggered; // save state

    static bool skipFirstSingle = true; // skip 1st triggered single trace to avoid old data
    // Stop sampling if we're in single trigger mode and have a triggered trace (txh No13)
    if ( isSamplingUI() && controlsettings.trigger.mode == Dso::TriggerMode::SINGLE && triggering->getTriggeredPositionRaw() ) {
        if ( verboseLevel > 5 )
            qDebug() << "     HDC::stateMachine() stop sampling" << raw.tag;
        if ( skipFirstSingle ) { // skip the 1st measurement in single mode
            skipFirstSingle = false;
        } else {
            enableSamplingUI( false ); // update UI sample indicator run/stop
            samplingStarted = false;
        }
    }

    if ( isSamplingUI() ) { // triggered by action "start sampling" and call to enableSampling()
        lastTag = raw.tag;
        // Sampling hasn't started, update the expected sample count
        expectedSampleCount = getSampleCount();
        timestampDebug( "Starting to capture" );
        samplingStarted = true;
    }

    updateInterval(); // calculate new acquire timing
    if ( stateMachineRunning ) {
#if ( QT_VERSION >= QT_VERSION_CHECK( 5, 4, 0 ) )
        QTimer::singleShot( acquireInterval, this, &HantekDsoControl::stateMachine );
#else
        QTimer::singleShot( acquireInterval, this, SLOT( stateMachine() ) );
#endif
    }
}


void HantekDsoControl::addCommand( ControlCommand *newCommand, bool pending ) {
    newCommand->pending = pending;
    control[ newCommand->code ] = newCommand;
    newCommand->next = firstControlCommand;
    firstControlCommand = newCommand;
}


// sending control commands to the scope:
// format: "cc <CC> <DD> <DD> ..."
// <CC> = control code, e.g. E6 (SETCALFREQ)
// <DD> = data, e.g. 01 = 1kHz or 69 (= 105 dec) = 50 Hz
// all <CC> and <DD> uint8_t values must consist of 2 hex encoded digits
// come here with validated strings from mainwindow.cpp
Dso::ErrorCode HantekDsoControl::stringCommand( const QString &commandString ) {
    if ( deviceNotConnected() )
        return Dso::ErrorCode::CONNECTION;
#if ( QT_VERSION >= QT_VERSION_CHECK( 5, 15, 0 ) )
    QStringList commandParts = commandString.split( ' ', Qt::SkipEmptyParts );
#else
    QStringList commandParts = commandString.split( ' ', QString::SkipEmptyParts );
#endif
    if ( commandParts.count() < 1 )
        return Dso::ErrorCode::PARAMETER;
    if ( commandParts[ 0 ] == "cc" || commandParts[ 0 ] == "CC" ) {
        if ( commandParts.count() < 2 )
            return Dso::ErrorCode::PARAMETER;

        uint8_t codeIndex = 0;
        hexParse( commandParts[ 1 ], &codeIndex, 1 );
        QString data = commandString.section( ' ', 2, -1, QString::SectionSkipEmpty );

        if ( !control[ codeIndex ] )
            return Dso::ErrorCode::UNSUPPORTED;

        QString name = "";
        if ( codeIndex >= 0xe0 && codeIndex <= 0xe6 )
            name = controlNames[ codeIndex - 0xe0 ];

        ControlCommand *c = modifyCommand< ControlCommand >( ControlCode( codeIndex ) );
        hexParse( data, c->data(), unsigned( c->size() ) );
        if ( verboseLevel > 2 )
            qDebug().noquote() << "  " + commandParts[ 0 ]
                               << QString( "0x%1 (%2) %3" )
                                      .arg( QString::number( codeIndex, 16 ), name, decDump( c->data(), unsigned( c->size() ) ) );
        if ( int( c->size() ) != commandParts.count() - 2 )
            return Dso::ErrorCode::PARAMETER;
        return Dso::ErrorCode::NONE;
    } else if ( commandParts[ 0 ] == "freq" ) {     // simple example for manual frequency command "freq nn"
        if ( commandParts.count() < 2 )             // command and one parameter needed
            return Dso::ErrorCode::PARAMETER;       // .. otherwise -> error
        unsigned freq = commandParts[ 1 ].toUInt(); // decode parameter as one decimal value into freq
        if ( !freq || freq > 100000 )               // parameter valid?
            return Dso::ErrorCode::PARAMETER;       // .. otherwise -> error
        if ( verboseLevel > 2 )                     // verbose enough?
            qDebug( "  freq %d", freq );            // .. show the parameter
        return setCalFreq( freq );                  // and call the scope function
    }
    return Dso::ErrorCode::UNSUPPORTED;
}
