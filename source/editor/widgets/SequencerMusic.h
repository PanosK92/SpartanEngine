/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include <string>
#include <vector>
#include <cstdint>
//=================

// listens to a song the way an editor would before cutting to it: tempo, where the bars start and how the energy rises and falls
namespace sequencer_music
{
    enum class SectionKind : uint8_t
    {
        Intro,
        Build,
        Drop,
        Breakdown,
        Outro
    };

    struct Section
    {
        float start      = 0.0f; // seconds into the file, on a bar line
        float end        = 0.0f;
        float energy     = 0.0f; // 0-1 against the loudest section
        SectionKind kind = SectionKind::Intro;
    };

    // every time is in seconds into the file
    struct Analysis
    {
        std::string path;
        std::string error;             // empty when the analysis succeeded
        float duration         = 0.0f;
        float bpm              = 0.0f;
        float beat_length      = 0.0f;
        float confidence       = 0.0f; // 0-1, how firmly the onsets lock to the beat grid
        float first_downbeat   = 0.0f; // the earliest bar start
        uint32_t beats_per_bar = 4;
        std::vector<float> bar_energy; // one per bar from first_downbeat, 0-1
        std::vector<Section> sections;
    };

    const char* get_section_name(SectionKind kind);

    // decodes mp3 or wav and analyzes it, the result is cached until the file changes on disk
    const Analysis& analyze(const std::string& path);
}
