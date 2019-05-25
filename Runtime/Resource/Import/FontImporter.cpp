/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES ==========================
#include "FontImporter.h"
#include "ft2build.h"
#include "freetype/freetype.h"
#include "../../Logging/Log.h"
#include "../../Math/MathHelper.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Font/Glyph.h"
#include "../../Rendering/Font/Font.h"
#include "../../RHI/RHI_Texture.h"
#include "../../RHI/RHI_Texture2D.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
using namespace Helper;
//=============================

namespace Spartan
{
	// A minimum size for a texture holding all visible ASCII characters
	static const uint32_t GLYPH_START		= 32;
	static const uint32_t GLYPH_END			= 127;
	static const uint32_t ATLAS_MAX_WIDTH	= 512;

	FT_UInt32 g_char_flags = FT_LOAD_DEFAULT | FT_LOAD_RENDER;

	namespace FreeTypeHelper
	{
		inline bool HandleError(int error_code)
		{
			if (error_code == FT_Err_Ok)
				return false;

			switch (error_code)
			{
				// Generic errors
				case FT_Err_Cannot_Open_Resource:	LOG_ERROR("FreeType: Cannot open resource.") break;
				case FT_Err_Unknown_File_Format:	LOG_ERROR("FreeType: Unknown file format."); break;
				case FT_Err_Invalid_File_Format:	LOG_ERROR("FreeType: Broken file."); break;
				case FT_Err_Invalid_Version:		LOG_ERROR("FreeType: Invalid FreeType version."); break;
				case FT_Err_Lower_Module_Version:	LOG_ERROR("FreeType: Module version is too low."); break;
				case FT_Err_Invalid_Argument:		LOG_ERROR("FreeType: Invalid argument."); break;
				case FT_Err_Unimplemented_Feature:	LOG_ERROR("FreeType: Unimplemented feature."); break;
				case FT_Err_Invalid_Table:			LOG_ERROR("FreeType: Invalid table."); break;
				case FT_Err_Invalid_Offset:			LOG_ERROR("FreeType: Invalid offset."); break;
				case FT_Err_Array_Too_Large:		LOG_ERROR("FreeType: Array allocation size too large."); break;
				case FT_Err_Missing_Module:			LOG_ERROR("FreeType: Missing module."); break;
				case FT_Err_Missing_Property:		LOG_ERROR("FreeType: Missing property."); break;
				// Glyph/character errors
				case FT_Err_Invalid_Glyph_Index:	LOG_ERROR("FreeType: Invalid glyph index."); break;
				case FT_Err_Invalid_Character_Code:	LOG_ERROR("FreeType: Invalid character code."); break;
				case FT_Err_Invalid_Glyph_Format:	LOG_ERROR("FreeType: Unsupported glyph format."); break;
				case FT_Err_Cannot_Render_Glyph:	LOG_ERROR("FreeType: Cannot render this glyph format."); break;
				case FT_Err_Invalid_Outline:		LOG_ERROR("FreeType: Invalid outline."); break;
				case FT_Err_Invalid_Composite:		LOG_ERROR("FreeType: Invalid composite glyph."); break;
				case FT_Err_Too_Many_Hints:			LOG_ERROR("FreeType: Too many hints."); break;
				case FT_Err_Invalid_Pixel_Size:		LOG_ERROR("FreeType: Invalid pixel size."); break;
				// Handle errors
				case FT_Err_Invalid_Handle:			LOG_ERROR("FreeType: Invalid object handle."); break;
				case FT_Err_Invalid_Library_Handle:	LOG_ERROR("FreeType: Invalid library handle."); break;
				case FT_Err_Invalid_Driver_Handle:	LOG_ERROR("FreeType: Invalid module handle."); break;
				case FT_Err_Invalid_Face_Handle:	LOG_ERROR("FreeType: Invalid face handle."); break;
				case FT_Err_Invalid_Size_Handle:	LOG_ERROR("FreeType: Invalid size handle."); break;
				case FT_Err_Invalid_Slot_Handle:	LOG_ERROR("FreeType: Invalid glyph slot handle."); break;
				case FT_Err_Invalid_CharMap_Handle:	LOG_ERROR("FreeType: Invalid charmap handle."); break;
				case FT_Err_Invalid_Cache_Handle:	LOG_ERROR("FreeType: Invalid cache manager handle."); break;
				case FT_Err_Invalid_Stream_Handle:	LOG_ERROR("FreeType: Invalid stream handle."); break;
				// Driver errors
				case FT_Err_Too_Many_Drivers:		LOG_ERROR("FreeType: Too many modules."); break;
				case FT_Err_Too_Many_Extensions:	LOG_ERROR("FreeType: Too many extensions."); break;
				// Memory errors
				case FT_Err_Out_Of_Memory:		LOG_ERROR("FreeType: Out of memory."); break;
				case FT_Err_Unlisted_Object:	LOG_ERROR("FreeType: Unlisted object."); break;
				// Stream errors
				case FT_Err_Cannot_Open_Stream:			LOG_ERROR("FreeType: Cannot open stream."); break;
				case FT_Err_Invalid_Stream_Seek:		LOG_ERROR("FreeType: Invalid stream seek."); break;
				case FT_Err_Invalid_Stream_Skip:		LOG_ERROR("FreeType: Invalid stream skip."); break;
				case FT_Err_Invalid_Stream_Read:		LOG_ERROR("FreeType: Invalid stream read."); break;
				case FT_Err_Invalid_Stream_Operation:	LOG_ERROR("FreeType: Invalid stream operation."); break;
				case FT_Err_Invalid_Frame_Operation:	LOG_ERROR("FreeType: Invalid frame operation."); break;
				case FT_Err_Nested_Frame_Access:		LOG_ERROR("FreeType: Nested frame access."); break;
				case FT_Err_Invalid_Frame_Read:			LOG_ERROR("FreeType: Invalid frame read."); break;
				// Raster errors
				case FT_Err_Raster_Uninitialized:	LOG_ERROR("FreeType: Raster uninitialized."); break;
				case FT_Err_Raster_Corrupted:		LOG_ERROR("FreeType: Raster corrupted."); break;
				case FT_Err_Raster_Overflow:		LOG_ERROR("FreeType: Raster overflow."); break;
				case FT_Err_Raster_Negative_Height:	LOG_ERROR("FreeType: Negative height while rastering."); break;
				// Cache errors
				case FT_Err_Too_Many_Caches:	LOG_ERROR("FreeType: Too many registered caches."); break;
				// TrueType and SFNT errors 
				case FT_Err_Invalid_Opcode:				LOG_ERROR("FreeType: Invalid opcode."); break;
				case FT_Err_Too_Few_Arguments:			LOG_ERROR("FreeType: Too few arguments."); break;
				case FT_Err_Stack_Overflow:				LOG_ERROR("FreeType: Stack overflow."); break;
				case FT_Err_Code_Overflow:				LOG_ERROR("FreeType: Code overflow."); break;
				case FT_Err_Bad_Argument:				LOG_ERROR("FreeType: Bad argument."); break;
				case FT_Err_Divide_By_Zero:				LOG_ERROR("FreeType: Division by zero."); break;
				case FT_Err_Invalid_Reference:			LOG_ERROR("FreeType: Invalid reference."); break;
				case FT_Err_Debug_OpCode:				LOG_ERROR("FreeType: Found debug opcode."); break;
				case FT_Err_ENDF_In_Exec_Stream:		LOG_ERROR("FreeType: Found ENDF opcode in execution stream."); break;
				case FT_Err_Nested_DEFS:				LOG_ERROR("FreeType: Nested DEFS."); break;
				case FT_Err_Invalid_CodeRange:			LOG_ERROR("FreeType: Invalid code range."); break;
				case FT_Err_Execution_Too_Long:			LOG_ERROR("FreeType: Execution context too long."); break;
				case FT_Err_Too_Many_Function_Defs:		LOG_ERROR("FreeType: Too many function definitions."); break;
				case FT_Err_Too_Many_Instruction_Defs:	LOG_ERROR("FreeType: Too many instruction definitions."); break;
				case FT_Err_Table_Missing:				LOG_ERROR("FreeType: SFNT font table missing."); break;
				case FT_Err_Horiz_Header_Missing:		LOG_ERROR("FreeType: Horizontal header (hhea) table missing."); break;
				case FT_Err_Locations_Missing:			LOG_ERROR("FreeType: Locations (loca) table missing."); break;
				case FT_Err_Name_Table_Missing:			LOG_ERROR("FreeType: Name table missing."); break;
				case FT_Err_CMap_Table_Missing:			LOG_ERROR("FreeType: Character map (cmap) table missing."); break;
				case FT_Err_Hmtx_Table_Missing:			LOG_ERROR("FreeType: Horizontal metrics (hmtx) table missing."); break;
				case FT_Err_Post_Table_Missing:			LOG_ERROR("FreeType: PostScript (post) table missing."); break;
				case FT_Err_Invalid_Horiz_Metrics:		LOG_ERROR("FreeType: Invalid horizontal metrics."); break;
				case FT_Err_Invalid_CharMap_Format:		LOG_ERROR("FreeType: Invalid character map (cma) format."); break;
				case FT_Err_Invalid_PPem:				LOG_ERROR("FreeType: Invalid ppem value."); break;
				case FT_Err_Invalid_Vert_Metrics:		LOG_ERROR("FreeType: Invalid vertical metrics."); break;
				case FT_Err_Could_Not_Find_Context:		LOG_ERROR("FreeType: Could not find context."); break;
				case FT_Err_Invalid_Post_Table_Format:	LOG_ERROR("FreeType: Invalid PostScript (post) table format."); break;
				case FT_Err_Invalid_Post_Table:			LOG_ERROR("FreeType: Invalid PostScript (post) table."); break;
				case FT_Err_DEF_In_Glyf_Bytecode:		LOG_ERROR("FreeType: Found FDEF or IDEF opcode in glyf bytecode."); break;
				// CFF, CID, and Type 1 errors 
				case FT_Err_Syntax_Error:			LOG_ERROR("FreeType: Opcode syntax error."); break;
				case FT_Err_Stack_Underflow:		LOG_ERROR("FreeType: Argument stack underflow."); break;
				case FT_Err_Ignore:					LOG_ERROR("FreeType: Ignore."); break;
				case FT_Err_No_Unicode_Glyph_Name:	LOG_ERROR("FreeType: No Unicode glyph name found."); break;
				case FT_Err_Glyph_Too_Big:			LOG_ERROR("FreeType: Glyph too big for hinting."); break;
				// BDF errors
				case FT_Err_Missing_Startfont_Field:		LOG_ERROR("FreeType: 'STARTFONT' field missing."); break;
				case FT_Err_Missing_Font_Field:				LOG_ERROR("FreeType: 'FONT' field missing."); break;
				case FT_Err_Missing_Size_Field:				LOG_ERROR("FreeType: 'SIZE' field missing."); break;
				case FT_Err_Missing_Fontboundingbox_Field:	LOG_ERROR("FreeType: 'FONTBOUNDINGBOX' field missing."); break;
				case FT_Err_Missing_Chars_Field:			LOG_ERROR("FreeType: 'CHARS' field missing."); break;
				case FT_Err_Missing_Startchar_Field:		LOG_ERROR("FreeType: 'STARTCHAR' field missing."); break;
				case FT_Err_Missing_Encoding_Field:			LOG_ERROR("FreeType: 'ENCODING' field missing."); break;
				case FT_Err_Missing_Bbx_Field:				LOG_ERROR("FreeType: 'BBX' field missing."); break;
				case FT_Err_Bbx_Too_Big:					LOG_ERROR("FreeType: 'BBX' too big."); break;
				case FT_Err_Corrupted_Font_Header:			LOG_ERROR("FreeType: Font header corrupted or missing fields."); break;
				case FT_Err_Corrupted_Font_Glyphs:			LOG_ERROR("FreeType: Font glyphs corrupted or missing fields."); break;
				// None
				default: LOG_ERROR("FreeType: Unknown error code."); break;
			}

			return true;
		}

		inline uint32_t GetCharacterMaxHeight(FT_Face& face)
		{
			uint32_t max_height = 0;
			for (uint32_t i = GLYPH_START; i < GLYPH_END; i++)
			{
				if (HandleError(FT_Load_Char(face, i, g_char_flags)))
					continue;

				FT_Bitmap* bitmap	= &face->glyph->bitmap;
				max_height			= Max<uint32_t>(max_height, bitmap->rows);
			}

			return max_height;
		}

		inline void ComputeAtlasTextureDimensions(FT_Face& face, uint32_t* atlas_width, uint32_t* atlas_height, uint32_t* row_height)
		{
			uint32_t pen_x = 0;
			(*row_height) = GetCharacterMaxHeight(face);
			for (uint32_t i = GLYPH_START; i < GLYPH_END; i++)
			{
				if (HandleError(FT_Load_Char(face, i, g_char_flags)))
					continue;

				FT_Bitmap* bitmap = &face->glyph->bitmap;

				pen_x			+= bitmap->width + 1;
				(*atlas_height)	= Max<uint32_t>((*atlas_height), bitmap->rows);

				// If the pen is about to exceed ATLAS_MAX_WIDTH we have to switch row. 
				// Hence, the height of the texture atlas must increase to fit that row.
				if (pen_x + bitmap->width >= ATLAS_MAX_WIDTH)
				{
					pen_x			= 0;
					(*atlas_height)	+= (*row_height);
					(*atlas_width)	= ATLAS_MAX_WIDTH;
				}
			}
		}
	}

	FontImporter::FontImporter(Context* context)
	{
		m_context = context;

		if (FT_Init_FreeType(&m_library))
		{
			LOG_ERROR("Failed to initialize.");
			return;
		}

		// Get version
		FT_Int major;
		FT_Int minor;
		FT_Int rev;
		FT_Library_Version(m_library, &major, &minor, &rev);
		Settings::Get().m_versionFreeType = to_string(major) + "." + to_string(minor) + "." + to_string(rev);
	}

	FontImporter::~FontImporter()
	{
		FT_Done_FreeType(m_library);
	}

	bool FontImporter::LoadFromFile(Font* font, const string& file_path)
	{
		// Compute hinting flags
		g_char_flags |= font->GetForceAutohint() ? FT_LOAD_FORCE_AUTOHINT : 0;
		switch (font->GetHinting()) 
		{
			case Hinting_None:
				g_char_flags |= FT_LOAD_NO_HINTING;
				break;
			case Hinting_Light:
				g_char_flags |= FT_LOAD_TARGET_LIGHT;
				break;
			default: // Hinting_Normal
				g_char_flags |= FT_LOAD_TARGET_NORMAL;
			break;
		}

		// Load font
		FT_Face face;
		if (FreeTypeHelper::HandleError(FT_New_Face(m_library, file_path.c_str(), 0, &face)))
		{
			FT_Done_Face(face);
			return false;
		}

		// Set size
		if (FreeTypeHelper::HandleError(FT_Set_Char_Size(
			face,					// handle to face object
			0,						// char_width in 1/64th of points 
			font->GetSize() * 64,	// char_height in 1/64th of points
			96,						// horizontal device resolution
			96)))					// vertical device resolution
		{
			FT_Done_Face(face);
			return false;
		}

		// Estimate the size of the font atlas texture	
		uint32_t atlas_width	= 0;
		uint32_t atlas_height	= 0;	
		uint32_t row_height		= 0;
		FreeTypeHelper::ComputeAtlasTextureDimensions(face, &atlas_width, &atlas_height, &row_height);	
		std::vector<std::byte> atlas_buffer;
		atlas_buffer.resize(atlas_width * atlas_height);
		atlas_buffer.reserve(atlas_buffer.size());

		// Go through each glyph
		uint32_t pen_x = 0, pen_y = 0;
		for (uint32_t i = GLYPH_START; i < GLYPH_END; i++)
		{
			// Skip problematic glyphs
			if (FreeTypeHelper::HandleError(FT_Load_Char(face, i, g_char_flags)))
				continue;

			FT_Bitmap* bitmap = &face->glyph->bitmap;

			// If the pen is about to exceed ATLAS_MAX_WIDTH we have to switch row. 
			// Hence, the height of the texture atlas must increase to fit that row.
			if (pen_x + bitmap->width >= atlas_width)
			{
				pen_x = 0;
				pen_y += row_height;
			}

			// Read char bytes into atlas position
			for (uint32_t y = 0; y < bitmap->rows; y++)
			{
				for (uint32_t x = 0; x < bitmap->width; x++)
				{
					uint32_t _x	= pen_x + x;
					uint32_t _y	= pen_y + y;
					auto atlas_pos	= _x + _y * atlas_width;
					SPARTAN_ASSERT(atlas_buffer.size() > atlas_pos);

					switch (bitmap->pixel_mode) 
					{
						case FT_PIXEL_MODE_MONO: {
							// todo
						} break;

						case FT_PIXEL_MODE_GRAY: {					
							atlas_buffer[atlas_pos] = static_cast<std::byte>(bitmap->buffer[x + y * bitmap->width]);
						} break;

						case FT_PIXEL_MODE_BGRA: {
							// todo
						} break;

						default:
							LOG_ERROR("Font uses unsupported pixel format");
							break;							
					}
				}
			}
			atlas_buffer.shrink_to_fit();

			//  Compute glyph info
			Glyph glyph;
			glyph.xLeft				= pen_x;
			glyph.yTop				= pen_y;
			glyph.xRight			= pen_x + bitmap->width;
			glyph.yBottom			= pen_y + bitmap->rows;
			glyph.width				= glyph.xRight - glyph.xLeft;
			glyph.height			= glyph.yBottom - glyph.yTop;
			glyph.uvXLeft			= (float)glyph.xLeft / (float)atlas_width;
			glyph.uvXRight			= (float)glyph.xRight / (float)atlas_width;
			glyph.uvYTop			= (float)glyph.yTop / (float)atlas_height;
			glyph.uvYBottom			= (float)glyph.yBottom / (float)atlas_height;
			glyph.descent			= row_height - face->glyph->bitmap_top;
			glyph.horizontalOffset	= face->glyph->advance.x >> 6;

			// Kerning is the process of adjusting the position of two subsequent glyph images 
			// in a string of text in order to improve the general appearance of text. 
			// For example, if a glyph for an uppercase ‘A’ is followed by a glyph for an 
			// uppercase ‘V’, the space between the two glyphs can be slightly reduced to 
			// avoid extra ‘diagonal whitespace’.
			if (i >= 1 && FT_HAS_KERNING(face))
			{
				FT_Vector kerningVec;
				FT_Get_Kerning(face, i - 1, i, FT_KERNING_DEFAULT, &kerningVec);
				glyph.horizontalOffset += kerningVec.x >> 6;
			}
			// horizontal distance from the current cursor position to the leftmost border of the glyph image's bounding box.
			glyph.horizontalOffset += face->glyph->metrics.horiBearingX;

			font->GetGlyphs()[i] = glyph;

			pen_x += bitmap->width + 1;
		}

		// Free memory
		FT_Done_Face(face);

		// Create a font texture atlas form the provided data
		font->SetAtlas(move(static_pointer_cast<RHI_Texture>(make_shared<RHI_Texture2D>(m_context, atlas_width, atlas_height, Format_R8_UNORM, atlas_buffer))));

		return true;
	}
}
