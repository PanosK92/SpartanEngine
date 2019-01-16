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
#include FT_FREETYPE_H 
#include "../../Logging/Log.h"
#include "../../Math/MathHelper.h"
#include "../../Core/Settings.h"
#include "../../Rendering/Font/Glyph.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

// A minimum size for a texture holding all visible ASCII characters
#define GLYPH_START 32
#define GLYPH_END 127
#define ATLAS_MAX_WIDTH 512

namespace Directus
{
	FT_Library m_library;

	FontImporter::FontImporter(Context* context)
	{
		m_context = context;

		if (FT_Init_FreeType(&m_library))
		{
			LOG_ERROR("FreeType: Failed to initialize.");
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

	bool FontImporter::LoadFromFile(const string& filePath, int size, vector<std::byte>& atlasBuffer, unsigned int& atlasWidth, unsigned int& atlasHeight, map<unsigned int, Glyph>& glyphs)
	{
		FT_Face face;

		// Load font
		if (HandleError(FT_New_Face(m_library, filePath.c_str(), 0, &face)))
		{
			FT_Done_Face(face);
			return false;
		}

		// Set size
		int height = size;
		if (HandleError(FT_Set_Char_Size(face, 0, height << 6, 96, 96)))
		{
			FT_Done_Face(face);
			return false;
		}

		// Try to estimate the size of the font atlas texture
		unsigned int rowHeight = 0;
		ComputeAtlasTextureDimensions(face, atlasWidth, atlasHeight, rowHeight);

		if (atlasWidth > 8192 || atlasHeight > 8192)
		{
			LOG_ERROR("FontImporter: The resulting font texture atlas is too large (" + to_string(atlasWidth) + "x" + to_string(atlasHeight) + "). Try using a smaller font size.");
			return false;
		}

		// Go through each glyph and create a texture atlas
		atlasBuffer.resize(atlasWidth * atlasHeight);
		int penX = 0, penY = 0;
		for (unsigned int i = GLYPH_START; i < GLYPH_END; i++)
		{
			FT_UInt32 loadMode_ = 0;
			loadMode_ |= FT_LOAD_DEFAULT;
			loadMode_ |= FT_LOAD_RENDER;
			loadMode_ |= FT_LOAD_FORCE_AUTOHINT;
			loadMode_ |= FT_LOAD_NO_HINTING;
			loadMode_ |= FT_LOAD_TARGET_LIGHT;

			if (HandleError(FT_Load_Char(face, i, loadMode_)))
			{
				// Free memory
				FT_Done_Face(face);
				return false;
			}

			FT_Bitmap* bitmap = &face->glyph->bitmap;
			if (penX + bitmap->width >= atlasWidth)
			{
				penX = 0;
				penY += rowHeight;
			}

			auto bytes	= (byte*)bitmap->buffer;
			int	pitch	= bitmap->pitch;
			for (unsigned int row = 0; row < bitmap->rows; row++)
			{
				for (unsigned int col = 0; col < bitmap->width; col++)
				{
					int x = penX + col;
					int y = penY + row;
					atlasBuffer[y * atlasWidth + x] = bytes[row * pitch + col];
				}
			}

			// Save glyph info
			Glyph glyph;
			glyph.xLeft = penX;
			glyph.yTop = penY;
			glyph.xRight = penX + bitmap->width;
			glyph.yBottom = penY + bitmap->rows;
			glyph.width = glyph.xRight - glyph.xLeft;
			glyph.height = glyph.yBottom - glyph.yTop;
			glyph.uvXLeft = (float)glyph.xLeft / (float)atlasWidth;
			glyph.uvXRight = (float)glyph.xRight / (float)atlasWidth;
			glyph.uvYTop = (float)glyph.yTop / (float)atlasHeight;
			glyph.uvYBottom = (float)glyph.yBottom / (float)atlasHeight;
			glyph.descent = rowHeight - face->glyph->bitmap_top;
			glyph.horizontalOffset = face->glyph->advance.x >> 6;
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

			glyphs[i] = (glyph);

			penX += bitmap->width + 1;
		}

		// Free memory
		FT_Done_Face(face);

		return true;
	}

	void FontImporter::ComputeAtlasTextureDimensions(FT_FaceRec_* face, unsigned int& atlasWidth, unsigned int& atlasHeight, unsigned int& rowHeight)
	{
		int penX = 0;
		rowHeight = GetCharacterMaxHeight(face);
		for (int i = GLYPH_START; i < GLYPH_END; i++)
		{
			if (HandleError(FT_Load_Char(face, i, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT))) { continue; }

			FT_Bitmap* bitmap = &face->glyph->bitmap;

			penX += bitmap->width + 1;
			atlasHeight = Max<int>(atlasHeight, bitmap->rows);

			// If the pen is about to exceed ATLAS_MAX_WIDTH 
			// we have to switch row. Hence, the height of the
			// texture atlas must increase to fit that row.
			if (penX + bitmap->width >= ATLAS_MAX_WIDTH)
			{
				penX = 0;
				atlasHeight += rowHeight;
				atlasWidth = ATLAS_MAX_WIDTH;
			}
		}
	}

	int FontImporter::GetCharacterMaxHeight(FT_FaceRec_* face)
	{
		int maxHeight = 0;
		for (int i = GLYPH_START; i < GLYPH_END; i++)
		{
			if (HandleError(FT_Load_Char(face, i, FT_LOAD_RENDER))) { continue; }

			FT_Bitmap* bitmap = &face->glyph->bitmap;
			maxHeight = Max<int>(maxHeight, bitmap->rows);
		}

		return maxHeight;
	}

	bool FontImporter::HandleError(int errorCode)
	{
		// If there is no error, just return
		if (errorCode == FT_Err_Ok)
			return false;

		switch (errorCode)
		{
			// Generic errors
		case FT_Err_Cannot_Open_Resource:
			LOG_ERROR("FreeType: Cannot open resource.");
			break;
		case FT_Err_Unknown_File_Format:
			LOG_ERROR("FreeType: Unknown file format.");
			break;
		case FT_Err_Invalid_File_Format:
			LOG_ERROR("FreeType: Broken file.");
			break;
		case FT_Err_Invalid_Version:
			LOG_ERROR("FreeType: Invalid FreeType version.");
			break;
		case FT_Err_Lower_Module_Version:
			LOG_ERROR("FreeType: Module version is too low.");
			break;
		case FT_Err_Invalid_Argument:
			LOG_ERROR("FreeType: Invalid argument.");
			break;
		case FT_Err_Unimplemented_Feature:
			LOG_ERROR("FreeType: Unimplemented feature.");
			break;
		case FT_Err_Invalid_Table:
			LOG_ERROR("FreeType: Invalid table.");
			break;
		case FT_Err_Invalid_Offset:
			LOG_ERROR("FreeType: Invalid offset.");
			break;
		case FT_Err_Array_Too_Large:
			LOG_ERROR("FreeType: Array allocation size too large.");
			break;
		case FT_Err_Missing_Module:
			LOG_ERROR("FreeType: Missing module.");
			break;
		case FT_Err_Missing_Property:
			LOG_ERROR("FreeType: Missing property.");
			break;

			// Glyph/character errors
		case FT_Err_Invalid_Glyph_Index:
			LOG_ERROR("FreeType: Invalid glyph index.");
			break;
		case FT_Err_Invalid_Character_Code:
			LOG_ERROR("FreeType: Invalid character code.");
			break;
		case FT_Err_Invalid_Glyph_Format:
			LOG_ERROR("FreeType: Unsupported glyph format.");
			break;
		case FT_Err_Cannot_Render_Glyph:
			LOG_ERROR("FreeType: Cannot render this glyph format.");
			break;
		case FT_Err_Invalid_Outline:
			LOG_ERROR("FreeType: Invalid outline.");
			break;
		case FT_Err_Invalid_Composite:
			LOG_ERROR("FreeType: Invalid composite glyph.");
			break;
		case FT_Err_Too_Many_Hints:
			LOG_ERROR("FreeType: Too many hints.");
			break;
		case FT_Err_Invalid_Pixel_Size:
			LOG_ERROR("FreeType: Invalid pixel size.");
			break;

			// Handle errors
		case FT_Err_Invalid_Handle:
			LOG_ERROR("FreeType: Invalid object handle.");
			break;
		case FT_Err_Invalid_Library_Handle:
			LOG_ERROR("FreeType: Invalid library handle.");
			break;
		case FT_Err_Invalid_Driver_Handle:
			LOG_ERROR("FreeType: Invalid module handle.");
			break;
		case FT_Err_Invalid_Face_Handle:
			LOG_ERROR("FreeType: Invalid face handle.");
			break;
		case FT_Err_Invalid_Size_Handle:
			LOG_ERROR("FreeType: Invalid size handle.");
			break;
		case FT_Err_Invalid_Slot_Handle:
			LOG_ERROR("FreeType: Invalid glyph slot handle.");
			break;
		case FT_Err_Invalid_CharMap_Handle:
			LOG_ERROR("FreeType: Invalid charmap handle.");
			break;
		case FT_Err_Invalid_Cache_Handle:
			LOG_ERROR("FreeType: Invalid cache manager handle.");
			break;
		case FT_Err_Invalid_Stream_Handle:
			LOG_ERROR("FreeType: Invalid stream handle.");
			break;

			// Driver errors
		case FT_Err_Too_Many_Drivers:
			LOG_ERROR("FreeType: Too many modules.");
			break;
		case FT_Err_Too_Many_Extensions:
			LOG_ERROR("FreeType: Too many extensions.");
			break;

			// Memory errors
		case FT_Err_Out_Of_Memory:
			LOG_ERROR("FreeType: Out of memory.");
			break;
		case FT_Err_Unlisted_Object:
			LOG_ERROR("FreeType: Unlisted object.");
			break;

			// Stream errors
		case FT_Err_Cannot_Open_Stream:
			LOG_ERROR("FreeType: Cannot open stream.");
			break;
		case FT_Err_Invalid_Stream_Seek:
			LOG_ERROR("FreeType: Invalid stream seek.");
			break;
		case FT_Err_Invalid_Stream_Skip:
			LOG_ERROR("FreeType: Invalid stream skip.");
			break;
		case FT_Err_Invalid_Stream_Read:
			LOG_ERROR("FreeType: Invalid stream read.");
			break;
		case FT_Err_Invalid_Stream_Operation:
			LOG_ERROR("FreeType: Invalid stream operation.");
			break;
		case FT_Err_Invalid_Frame_Operation:
			LOG_ERROR("FreeType: Invalid frame operation.");
			break;
		case FT_Err_Nested_Frame_Access:
			LOG_ERROR("FreeType: Nested frame access.");
			break;
		case FT_Err_Invalid_Frame_Read:
			LOG_ERROR("FreeType: Invalid frame read.");
			break;

			// Raster errors
		case FT_Err_Raster_Uninitialized:
			LOG_ERROR("FreeType: Raster uninitialized.");
			break;
		case FT_Err_Raster_Corrupted:
			LOG_ERROR("FreeType: Raster corrupted.");
			break;
		case FT_Err_Raster_Overflow:
			LOG_ERROR("FreeType: Raster overflow.");
			break;
		case FT_Err_Raster_Negative_Height:
			LOG_ERROR("FreeType: Negative height while rastering.");
			break;

			// Cache errors
		case FT_Err_Too_Many_Caches:
			LOG_ERROR("FreeType: Too many registered caches.");
			break;

			// TrueType and SFNT errors 
		case FT_Err_Invalid_Opcode:
			LOG_ERROR("FreeType: Invalid opcode.");
			break;
		case FT_Err_Too_Few_Arguments:
			LOG_ERROR("FreeType: Too few arguments.");
			break;
		case FT_Err_Stack_Overflow:
			LOG_ERROR("FreeType: Stack overflow.");
			break;
		case FT_Err_Code_Overflow:
			LOG_ERROR("FreeType: Code overflow.");
			break;
		case FT_Err_Bad_Argument:
			LOG_ERROR("FreeType: Bad argument.");
			break;
		case FT_Err_Divide_By_Zero:
			LOG_ERROR("FreeType: Division by zero.");
			break;
		case FT_Err_Invalid_Reference:
			LOG_ERROR("FreeType: Invalid reference.");
			break;
		case FT_Err_Debug_OpCode:
			LOG_ERROR("FreeType: Found debug opcode.");
			break;
		case FT_Err_ENDF_In_Exec_Stream:
			LOG_ERROR("FreeType: Found ENDF opcode in execution stream.");
			break;
		case FT_Err_Nested_DEFS:
			LOG_ERROR("FreeType: Nested DEFS.");
			break;
		case FT_Err_Invalid_CodeRange:
			LOG_ERROR("FreeType: Invalid code range.");
			break;
		case FT_Err_Execution_Too_Long:
			LOG_ERROR("FreeType: Execution context too long.");
			break;
		case FT_Err_Too_Many_Function_Defs:
			LOG_ERROR("FreeType: Too many function definitions.");
			break;
		case FT_Err_Too_Many_Instruction_Defs:
			LOG_ERROR("FreeType: Too many instruction definitions.");
			break;
		case FT_Err_Table_Missing:
			LOG_ERROR("FreeType: SFNT font table missing.");
			break;
		case FT_Err_Horiz_Header_Missing:
			LOG_ERROR("FreeType: Horizontal header (hhea) table missing.");
			break;
		case FT_Err_Locations_Missing:
			LOG_ERROR("FreeType: Locations (loca) table missing.");
			break;
		case FT_Err_Name_Table_Missing:
			LOG_ERROR("FreeType: Name table missing.");
			break;
		case FT_Err_CMap_Table_Missing:
			LOG_ERROR("FreeType: Character map (cmap) table missing.");
			break;
		case FT_Err_Hmtx_Table_Missing:
			LOG_ERROR("FreeType: Horizontal metrics (hmtx) table missing.");
			break;
		case FT_Err_Post_Table_Missing:
			LOG_ERROR("FreeType: PostScript (post) table missing.");
			break;
		case FT_Err_Invalid_Horiz_Metrics:
			LOG_ERROR("FreeType: Invalid horizontal metrics.");
			break;
		case FT_Err_Invalid_CharMap_Format:
			LOG_ERROR("FreeType: Invalid character map (cma) format.");
			break;
		case FT_Err_Invalid_PPem:
			LOG_ERROR("FreeType: Invalid ppem value.");
			break;
		case FT_Err_Invalid_Vert_Metrics:
			LOG_ERROR("FreeType: Invalid vertical metrics.");
			break;
		case FT_Err_Could_Not_Find_Context:
			LOG_ERROR("FreeType: Could not find context.");
			break;
		case FT_Err_Invalid_Post_Table_Format:
			LOG_ERROR("FreeType: Invalid PostScript (post) table format.");
			break;
		case FT_Err_Invalid_Post_Table:
			LOG_ERROR("FreeType: Invalid PostScript (post) table.");
			break;
		case FT_Err_DEF_In_Glyf_Bytecode:
			LOG_ERROR("FreeType: Found FDEF or IDEF opcode in glyf bytecode.");
			break;

			// CFF, CID, and Type 1 errors 
		case FT_Err_Syntax_Error:
			LOG_ERROR("FreeType: Opcode syntax error.");
			break;
		case FT_Err_Stack_Underflow:
			LOG_ERROR("FreeType: Argument stack underflow.");
			break;
		case FT_Err_Ignore:
			LOG_ERROR("FreeType: Ignore.");
			break;
		case FT_Err_No_Unicode_Glyph_Name:
			LOG_ERROR("FreeType: No Unicode glyph name found.");
			break;
		case FT_Err_Glyph_Too_Big:
			LOG_ERROR("FreeType: Glyph too big for hinting.");
			break;

			// BDF errors
		case FT_Err_Missing_Startfont_Field:
			LOG_ERROR("FreeType: 'STARTFONT' field missing.");
			break;
		case FT_Err_Missing_Font_Field:
			LOG_ERROR("FreeType: 'FONT' field missing.");
			break;
		case FT_Err_Missing_Size_Field:
			LOG_ERROR("FreeType: 'SIZE' field missing.");
			break;
		case FT_Err_Missing_Fontboundingbox_Field:
			LOG_ERROR("FreeType: 'FONTBOUNDINGBOX' field missing.");
			break;
		case FT_Err_Missing_Chars_Field:
			LOG_ERROR("FreeType: 'CHARS' field missing.");
			break;
		case FT_Err_Missing_Startchar_Field:
			LOG_ERROR("FreeType: 'STARTCHAR' field missing.");
			break;
		case FT_Err_Missing_Encoding_Field:
			LOG_ERROR("FreeType: 'ENCODING' field missing.");
			break;
		case FT_Err_Missing_Bbx_Field:
			LOG_ERROR("FreeType: 'BBX' field missing.");
			break;
		case FT_Err_Bbx_Too_Big:
			LOG_ERROR("FreeType: 'BBX' too big.");
			break;
		case FT_Err_Corrupted_Font_Header:
			LOG_ERROR("FreeType: Font header corrupted or missing fields.");
			break;
		case FT_Err_Corrupted_Font_Glyphs:
			LOG_ERROR("FreeType: Font glyphs corrupted or missing fields.");
			break;

			// None
		default:
			LOG_ERROR("FreeType: Unknown error code.");
			break;
		}

		return true;
	}
}
