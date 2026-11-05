# vst2abi

A freely written description of the VST 2.4 binary interface (numbers and struct layouts that hosts like MPC 2 expect), so JUCE can build the VST2 plug-in without the Steinberg SDK. MIT. test/vst2_test.c checks the built plug-in with an independently typed copy of the ABI. For personal use: Steinberg no longer licenses VST2.
