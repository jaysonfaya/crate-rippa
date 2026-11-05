/* Crate Rippa Desktop (JJ DA PLUG): VST 2.4 binary interface, part 2 (events, time info, pins, speakers, the 2.x opcodes).
 * Freely written, see aeffect.h. MIT. */
#pragma once
#include "aeffect.h"

#pragma pack(push, 8)

enum AEffectXOpcodes
{
    effProcessEvents = effSetChunk + 1,   /* 25 */
    effCanBeAutomated,
    effString2Parameter,
    effGetNumProgramCategories,
    effGetProgramNameIndexed,
    effCopyProgram,
    effConnectInput,
    effConnectOutput,
    effGetInputProperties,
    effGetOutputProperties,
    effGetPlugCategory,                   /* 35 */
    effGetCurrentPosition,
    effGetDestinationBuffer,
    effOfflineNotify,
    effOfflinePrepare,
    effOfflineRun,                        /* 40 */
    effProcessVarIo,
    effSetSpeakerArrangement,
    effSetBlockSizeAndSampleRate,
    effSetBypass,
    effGetEffectName,                     /* 45 */
    effGetErrorText,
    effGetVendorString,
    effGetProductString,
    effGetVendorVersion,
    effVendorSpecific,                    /* 50 */
    effCanDo,
    effGetTailSize,
    effIdle,
    effGetIcon,
    effSetViewPosition,                   /* 55 */
    effGetParameterProperties,
    effKeysRequired,
    effGetVstVersion,
    effEditKeyDown,
    effEditKeyUp,                         /* 60 */
    effSetEditKnobMode,
    effGetMidiProgramName,
    effGetCurrentMidiProgram,
    effGetMidiProgramCategory,
    effHasMidiProgramsChanged,            /* 65 */
    effGetMidiKeyName,
    effBeginSetProgram,
    effEndSetProgram,
    effGetSpeakerArrangement,
    effShellGetNextPlugin,                /* 70 */
    effStartProcess,
    effStopProcess,
    effSetTotalSampleToProcess,
    effSetPanLaw,
    effBeginLoadBank,                     /* 75 */
    effBeginLoadProgram,
    effSetProcessPrecision,
    effGetNumMidiInputChannels,
    effGetNumMidiOutputChannels           /* 79 */
};

enum AudioMasterOpcodesX
{
    audioMasterWantMidi = audioMasterPinConnected + 2,   /* 6 */
    audioMasterGetTime,
    audioMasterProcessEvents,
    audioMasterSetTime,
    audioMasterTempoAt,                   /* 10 */
    audioMasterGetNumAutomatableParameters,
    audioMasterGetParameterQuantization,
    audioMasterIOChanged,
    audioMasterNeedIdle,
    audioMasterSizeWindow,                /* 15 */
    audioMasterGetSampleRate,
    audioMasterGetBlockSize,
    audioMasterGetInputLatency,
    audioMasterGetOutputLatency,
    audioMasterGetPreviousPlug,           /* 20 */
    audioMasterGetNextPlug,
    audioMasterWillReplaceOrAccumulate,
    audioMasterGetCurrentProcessLevel,
    audioMasterGetAutomationState,
    audioMasterOfflineStart,              /* 25 */
    audioMasterOfflineRead,
    audioMasterOfflineWrite,
    audioMasterOfflineGetCurrentPass,
    audioMasterOfflineGetCurrentMetaPass,
    audioMasterSetOutputSampleRate,       /* 30 */
    audioMasterGetOutputSpeakerArrangement,
    audioMasterGetVendorString,
    audioMasterGetProductString,
    audioMasterGetVendorVersion,
    audioMasterVendorSpecific,            /* 35 */
    audioMasterSetIcon,
    audioMasterCanDo,
    audioMasterGetLanguage,
    audioMasterOpenWindow,
    audioMasterCloseWindow,               /* 40 */
    audioMasterGetDirectory,
    audioMasterUpdateDisplay,
    audioMasterBeginEdit,
    audioMasterEndEdit,
    audioMasterOpenFileSelector,          /* 45 */
    audioMasterCloseFileSelector,
    audioMasterEditFile,
    audioMasterGetChunkFile,
    audioMasterGetInputSpeakerArrangement
};

enum
{
    kVstMaxNameLen       = 64,
    kVstMaxLabelLen      = 64,
    kVstMaxShortLabelLen = 8,
    kVstMaxCategLabelLen = 24,
    kVstMaxFileNameLen   = 100
};

/* ---- events */
enum VstEventTypes { kVstMidiType = 1, kVstSysExType = 6 };
enum VstMidiEventFlags { kVstMidiEventIsRealtime = 1 << 0 };

struct VstEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    char data[16];
};

struct VstEvents
{
    VstInt32 numEvents;
    VstIntPtr reserved;
    struct VstEvent *events[2];
};

struct VstMidiEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 noteLength;
    VstInt32 noteOffset;
    char midiData[4];
    char detune;
    char noteOffVelocity;
    char reserved1;
    char reserved2;
};

struct VstMidiSysexEvent
{
    VstInt32 type;
    VstInt32 byteSize;
    VstInt32 deltaFrames;
    VstInt32 flags;
    VstInt32 dumpBytes;
    VstIntPtr resvd1;
    char *sysexDump;
    VstIntPtr resvd2;
};

/* ---- time info */
enum VstTimeInfoFlags
{
    kVstTransportChanged     = 1,
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,
    kVstAutomationWriting    = 1 << 6,
    kVstAutomationReading    = 1 << 7,
    kVstNanosValid           = 1 << 8,
    kVstPpqPosValid          = 1 << 9,
    kVstTempoValid           = 1 << 10,
    kVstBarsValid            = 1 << 11,
    kVstCyclePosValid        = 1 << 12,
    kVstTimeSigValid         = 1 << 13,
    kVstSmpteValid           = 1 << 14,
    kVstClockValid           = 1 << 15
};

enum VstSmpteFrameRate
{
    kVstSmpte24fps    = 0,
    kVstSmpte25fps    = 1,
    kVstSmpte2997fps  = 2,
    kVstSmpte30fps    = 3,
    kVstSmpte2997dfps = 4,
    kVstSmpte30dfps   = 5,
    kVstSmpteFilm16mm = 6,
    kVstSmpteFilm35mm = 7,
    kVstSmpte239fps   = 10,
    kVstSmpte249fps   = 11,
    kVstSmpte599fps   = 12,
    kVstSmpte60fps    = 13
};

struct VstTimeInfo
{
    double samplePos;
    double sampleRate;
    double nanoSeconds;
    double ppqPos;
    double tempo;
    double barStartPos;
    double cycleStartPos;
    double cycleEndPos;
    VstInt32 timeSigNumerator;
    VstInt32 timeSigDenominator;
    VstInt32 smpteOffset;
    VstInt32 smpteFrameRate;
    VstInt32 samplesToNextClock;
    VstInt32 flags;
};

/* ---- categories, precision */
enum VstPlugCategory
{
    kPlugCategUnknown = 0,
    kPlugCategEffect,
    kPlugCategSynth,
    kPlugCategAnalysis,
    kPlugCategMastering,
    kPlugCategSpacializer,
    kPlugCategRoomFx,
    kPlugSurroundFx,
    kPlugCategRestoration,
    kPlugCategOfflineProcess,
    kPlugCategShell,
    kPlugCategGenerator,
    kPlugCategMaxCount
};

enum VstProcessPrecision { kVstProcessPrecision32 = 0, kVstProcessPrecision64 = 1 };

enum VstAutomationStates { kVstAutomationUnsupported = 0, kVstAutomationOff, kVstAutomationRead, kVstAutomationWrite, kVstAutomationReadWrite };

/* ---- pins and speakers */
enum VstPinPropertiesFlags { kVstPinIsActive = 1 << 0, kVstPinIsStereo = 1 << 1, kVstPinUseSpeaker = 1 << 2 };

struct VstPinProperties
{
    char label[kVstMaxLabelLen];
    VstInt32 flags;
    VstInt32 arrangementType;
    char shortLabel[kVstMaxShortLabelLen];
    char future[48];
};

struct VstSpeakerProperties
{
    float azimuth;
    float elevation;
    float radius;
    float reserved;
    char name[kVstMaxNameLen];
    VstInt32 type;
    char future[28];
};

struct VstSpeakerArrangement
{
    VstInt32 type;
    VstInt32 numChannels;
    struct VstSpeakerProperties speakers[8];
};

enum VstSpeakerType
{
    kSpeakerUndefined = 0x7fffffff,
    kSpeakerM = 0,
    kSpeakerL,
    kSpeakerR,
    kSpeakerC,
    kSpeakerLfe,
    kSpeakerLs,
    kSpeakerRs,
    kSpeakerLc,
    kSpeakerRc,
    kSpeakerS,
    kSpeakerCs = kSpeakerS,
    kSpeakerSl,
    kSpeakerSr,
    kSpeakerTm,
    kSpeakerTfl,
    kSpeakerTfc,
    kSpeakerTfr,
    kSpeakerTrl,
    kSpeakerTrc,
    kSpeakerTrr,
    kSpeakerLfe2
};

enum VstSpeakerArrangementType
{
    kSpeakerArrUserDefined = -2,
    kSpeakerArrEmpty = -1,
    kSpeakerArrMono = 0,
    kSpeakerArrStereo,
    kSpeakerArrStereoSurround,
    kSpeakerArrStereoCenter,
    kSpeakerArrStereoSide,
    kSpeakerArrStereoCLfe,
    kSpeakerArr30Cine,
    kSpeakerArr30Music,
    kSpeakerArr31Cine,
    kSpeakerArr31Music,
    kSpeakerArr40Cine,
    kSpeakerArr40Music,
    kSpeakerArr41Cine,
    kSpeakerArr41Music,
    kSpeakerArr50,
    kSpeakerArr51,
    kSpeakerArr60Cine,
    kSpeakerArr60Music,
    kSpeakerArr61Cine,
    kSpeakerArr61Music,
    kSpeakerArr70Cine,
    kSpeakerArr70Music,
    kSpeakerArr71Cine,
    kSpeakerArr71Music,
    kSpeakerArr80Cine,
    kSpeakerArr80Music,
    kSpeakerArr81Cine,
    kSpeakerArr81Music,
    kSpeakerArr102,
    kNumSpeakerArr
};

/* ---- MIDI key names */
struct MidiKeyName
{
    VstInt32 thisProgramIndex;
    VstInt32 thisKeyNumber;
    char keyName[kVstMaxNameLen];
    VstInt32 reserved;
    VstInt32 flags;
};

#pragma pack(pop)
