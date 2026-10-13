/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ============
#include <cstdint>
#include <string>
#include <vector>
#include "TreeSpecies.h"
//=======================

// paints the bark and the twig atlas of a species on the cpu, so every preset ships without a single
// downloaded texture. bark is a seamless tile wrapped around the stems, the atlas holds four twig variants,
// a twig of leaves (or needles, scales, a palm frond) whose base sits at the bottom centre of its tile
namespace spartan::trees
{
    struct texture_set
    {
        uint32_t bark_size = 512;
        uint32_t leaf_size = 1024;
        std::vector<uint8_t> bark_albedo; // srgb rgba8
        std::vector<uint8_t> bark_normal; // tangent space rgba8
        std::vector<uint8_t> leaf_albedo; // srgb rgba8, alpha is the cutout
        std::vector<uint8_t> leaf_normal;
    };

    struct texture_paths
    {
        std::string bark_albedo;
        std::string bark_normal;
        std::string leaf_albedo;
        std::string leaf_normal;
    };

    void generate_textures(const species& s, season time, texture_set& output);

    // writes the four maps into the directory under names keyed by the species state, an existing set is reused
    bool ensure_textures(const species& s, season time, const std::string& directory, texture_paths& paths);
}
