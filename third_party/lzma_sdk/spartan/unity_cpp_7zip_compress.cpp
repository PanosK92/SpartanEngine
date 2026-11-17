// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.

#include "../CPP/7zip/Compress/CodecExports.cpp"
#include "../CPP/7zip/Compress/Bcj2Coder.cpp"
#include "../CPP/7zip/Compress/Bcj2Register.cpp"
#include "../CPP/7zip/Compress/BcjCoder.cpp"
#include "../CPP/7zip/Compress/BcjRegister.cpp"
#include "../CPP/7zip/Compress/BranchMisc.cpp"
#include "../CPP/7zip/Compress/BranchRegister.cpp"
#include "../CPP/7zip/Compress/ByteSwap.cpp"
#include "../CPP/7zip/Compress/CopyCoder.cpp"
#include "../CPP/7zip/Compress/CopyRegister.cpp"
#include "../CPP/7zip/Compress/DeltaFilter.cpp"
#include "../CPP/7zip/Compress/Lzma2Decoder.cpp"
#include "../CPP/7zip/Compress/Lzma2Encoder.cpp"
#include "../CPP/7zip/Compress/Lzma2Register.cpp"
#define SResToHRESULT SResToHRESULT_LzmaDecoder
#include "../CPP/7zip/Compress/LzmaDecoder.cpp"
#undef SResToHRESULT
#include "../CPP/7zip/Compress/LzmaEncoder.cpp"
#include "../CPP/7zip/Compress/LzmaRegister.cpp"
