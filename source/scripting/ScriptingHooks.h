/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace spartan::scripting
{
    // cutscenes authored in the editor's sequencer, the editor installs these and a build without it leaves them empty
    struct SequencerHooks
    {
        // plays project/sequencer_<name>.xml, the car its drive events were authored with stands in for car_entity_id
        std::function<bool(const std::string& name, uint64_t car_entity_id)> play;
        // hands the camera and the cars back, a finished cutscene holds its last shot until this is called
        std::function<void()> stop;
        // any playback, a cutscene or a sequence played from the editor, owns the screen
        std::function<bool()> is_playing;
        std::function<float()> get_time;
        std::function<float()> get_duration;
    };

    SequencerHooks& get_sequencer_hooks();
}
