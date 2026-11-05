/* Crate Rippa Desktop (JJ DA PLUG): a freely written description of the VST 2.4 binary interface (the ABI the hosts speak:
 * struct layouts, opcode numbers, flags). It is not Steinberg's SDK and contains none of its text; it only lists the
 * numbers and layouts a VST2 host such as MPC 2 expects, so that JUCE's VST2 wrapper can be compiled.
 * Part of Crate Rippa Desktop, MIT. Only for personal use builds: VST2 plug-ins are no longer licensed by Steinberg. */
#pragma once
#include <stdint.h>

#if defined(_WIN32)
 #define VSTCALLBACK __cdecl
#else
 #define VSTCALLBACK
#endif

#pragma pack(push, 8)

typedef int32_t VstInt32;
typedef int16_t VstInt16;
typedef intptr_t VstIntPtr;

#define CCONST(a, b, c, d) ((((VstInt32)(a)) << 24) | (((VstInt32)(b)) << 16) | (((VstInt32)(c)) << 8) | ((VstInt32)(d)))

#define kEffectMagic CCONST ('V', 's', 't', 'P')
#define kVstVersion 2400

struct AEffect;

typedef VstIntPtr (VSTCALLBACK *audioMasterCallback)(struct AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
typedef VstIntPtr (VSTCALLBACK *AEffectDispatcherProc)(struct AEffect *effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);
typedef void (VSTCALLBACK *AEffectProcessProc)(struct AEffect *effect, float **inputs, float **outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK *AEffectProcessDoubleProc)(struct AEffect *effect, double **inputs, double **outputs, VstInt32 sampleFrames);
typedef void (VSTCALLBACK *AEffectSetParameterProc)(struct AEffect *effect, VstInt32 index, float parameter);
typedef float (VSTCALLBACK *AEffectGetParameterProc)(struct AEffect *effect, VstInt32 index);

struct AEffect
{
    VstInt32 magic;
    AEffectDispatcherProc dispatcher;
    AEffectProcessProc process;              /* old accumulating call, unused */
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;
    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;
    VstIntPtr resvd1;
    VstIntPtr resvd2;
    VstInt32 initialDelay;
    VstInt32 realQualities;
    VstInt32 offQualities;
    float ioRatio;
    void *object;
    void *user;
    VstInt32 uniqueID;
    VstInt32 version;
    AEffectProcessProc processReplacing;
    AEffectProcessDoubleProc processDoubleReplacing;
    char future[56];
};

enum VstAEffectFlags
{
    effFlagsHasEditor          = 1 << 0,
    effFlagsCanReplacing       = 1 << 4,
    effFlagsProgramChunks      = 1 << 5,
    effFlagsIsSynth            = 1 << 8,
    effFlagsNoSoundInStop      = 1 << 9,
    effFlagsCanDoubleReplacing = 1 << 12
};

enum AEffectOpcodes
{
    effOpen = 0,
    effClose,
    effSetProgram,
    effGetProgram,
    effSetProgramName,
    effGetProgramName,
    effGetParamLabel,
    effGetParamDisplay,
    effGetParamName,
    effGetVu,
    effSetSampleRate,
    effSetBlockSize,
    effMainsChanged,
    effEditGetRect,
    effEditOpen,
    effEditClose,
    effEditDraw,
    effEditMouse,
    effEditKey,
    effEditIdle,
    effEditTop,
    effEditSleep,
    effIdentify,
    effGetChunk,
    effSetChunk,
    effNumOpcodes
};

enum AudioMasterOpcodes
{
    audioMasterAutomate = 0,
    audioMasterVersion,
    audioMasterCurrentId,
    audioMasterIdle,
    audioMasterPinConnected
};

enum
{
    kVstMaxProgNameLen   = 24,
    kVstMaxParamStrLen   = 8,
    kVstMaxVendorStrLen  = 64,
    kVstMaxProductStrLen = 64,
    kVstMaxEffectNameLen = 32
};

struct ERect
{
    VstInt16 top;
    VstInt16 left;
    VstInt16 bottom;
    VstInt16 right;
};

#pragma pack(pop)
