#include <juce_audio_plugin_client/juce_audio_plugin_client.h>
#include "PluginProcessor.h"

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FretworkAudioProcessor();
}
