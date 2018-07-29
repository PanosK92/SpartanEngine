/*
Copyright(c) 2016-2018 Panos Karabelas

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

#pragma once

//= INCLUDES ============
#include <vector>
#include <set>
#include <memory>
#include <map>
#include "../IRHI_Device.h"
#include "Windows.h" // Used for LPCSTR, must remove
//=======================

namespace Directus
{
	class D3D11_Shader
	{
	public:
		D3D11_Shader(D3D11_Device* graphicsDevice);
		~D3D11_Shader();

		bool Compile(const std::string& filePath);
		bool SetInputLayout(Input_Layout inputLayout);
		void SetName(const std::string& name) { m_name = name; }
		void AddDefine(const std::string& define, const std::string& value);
		bool IsCompiled() { return m_compiled; }

		ID3D11VertexShader* GetVertexShader()				{ return m_vertexShader; }
		ID3D11PixelShader* GetPixelShader()					{ return m_pixelShader; }
		std::shared_ptr<D3D11_InputLayout> GetInputLayout() { return m_D3D11InputLayout; }

	private:
		//= COMPILATION ================================================================================================================================================================================
		bool CompileVertexShader(ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, const std::string& path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros);
		bool CompilePixelShader(ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, const std::string& path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros);
		bool CompileShader(const std::string& filePath, D3D_SHADER_MACRO* macros, LPCSTR entryPoint, LPCSTR target, ID3DBlob** shaderBlobOut);
		void LogD3DCompilerError(ID3D10Blob* errorMessage);

		//= REFLECTION ============================
		using InputLayoutDesc = std::pair<std::vector<D3D11_INPUT_ELEMENT_DESC>, std::vector<std::string>>;
		InputLayoutDesc Reflect(ID3D10Blob* vsBlob) const;

		//= MISC ===========
		std::string m_name;
		std::string m_filePath;
		LPCSTR m_entrypoint;
		LPCSTR m_profile;
		bool m_compiled;
		ID3D11VertexShader* m_vertexShader;
		ID3D11PixelShader* m_pixelShader;
		ID3D10Blob* m_VSBlob = nullptr;

		//= MACROS =======================================
		std::vector<D3D_SHADER_MACRO> m_macros;
		// D3D_SHADER_MACRO stores pointers so we need
		// m_macrosStr to actually keep the defines around
		std::map<std::string, std::string> m_macrosStr;


		//= INPUT LAYOUT ======================
		std::shared_ptr<D3D11_InputLayout> m_D3D11InputLayout;
		bool m_layoutHasBeenSet;

		//= DEPENDENCIES============
		D3D11_Device* m_graphics;
	};
}
