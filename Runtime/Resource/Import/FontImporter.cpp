/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =======================
#include "FontImporter.h"
#include "ft2build.h"
#include FT_FREETYPE_H  
#include "../../Logging/Log.h"
#include "../../Graphics/Texture.h"
#include "../../Math/Vector2.h"
#include "../../Math/MathHelper.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

// We extract glyphs based on the ASCII table.
// Since the first 32 are just control codes, we skip them.
#define GLYPH_START 32
#define GLYPH_END 128 

namespace Directus
{
	struct Character 
	{
		Vector2 size;		// Size of glyph
		Vector2 bearing;	// Offset from baseline to left/top of glyph
		uint8_t advance;	// Offset to advance to next glyph
	};

	FT_Library m_library;

	FontImporter::FontImporter(Context* context)
	{
		m_context = context;
	}

	FontImporter::~FontImporter()
	{
		FT_Done_FreeType(m_library);
	}

	void FontImporter::Initialize()
	{
		if (FT_Init_FreeType(&m_library))
		{
			LOG_ERROR("FreeType: Failed to initialize.");
		}
	}

	unique_ptr<Texture> FontImporter::LoadFont(const string& filePath, int size)
	{
		FT_Face face;

		// Load font
		if (HandleError(FT_New_Face(m_library, filePath.c_str(), 0, &face)))
		{
			return unique_ptr<Texture>();
		}

		int height = size;
		if (HandleError(FT_Set_Pixel_Sizes(face, 0, height)))
		{
			return unique_ptr<Texture>();
		}
		
		// Go through each glyph and create a texture atlas
		unique_ptr<Texture> m_textureAtlas = make_unique<Texture>(m_context);
		int atlasHeight = 0;
		int atlasWidth = 0;
		vector<unsigned char> m_buffer;
		for (int i = 0; i < 2; i++)
		{
			FT_Error error = FT_Load_Char(face, 'A', FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_LIGHT);
			HandleError(error);

			FT_Bitmap* bitmap = &face->glyph->bitmap;
			unsigned char* bits = bitmap->buffer;

			for (unsigned int y = 0; y < bitmap->rows; y++)
			{
				for (unsigned int x = 0; x < bitmap->width; x++)
				{
					m_buffer.push_back(bits[y * bitmap->pitch + x]);
				}
			}
			atlasWidth += bitmap->width;
			atlasHeight = Max<int>(atlasHeight, bitmap->rows);

			/*Character character;
			character.size = Vector2(face->glyph->bitmap.width, face->glyph->bitmap.rows);
			character.bearing = Vector2(face->glyph->bitmap_left, face->glyph->bitmap_top);
			character.advance = face->glyph->advance.x;*/
		}

		m_textureAtlas->CreateFromMemory(atlasWidth, atlasHeight, 1, m_buffer.data(), R_8_UNORM);
		LOG_INFO("Font Texture Atlas: " + to_string(atlasWidth) + "x" + to_string(atlasHeight) + ", Buffer: " + to_string((float)m_buffer.size() / 8192.0f) + " KB");

		return m_textureAtlas;
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
