/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ================
#include "ShaderLoader.h"
#include <d3dcompiler.h>
#include <fstream>
#include "../IO/Log.h"
#include "../IO/FileHelper.h"
#include "../Misc/Globals.h"
//===========================

//= NAMESPACES =====
using namespace std;

//==================

ShaderLoader::ShaderLoader()
{
}

ShaderLoader::~ShaderLoader()
{
}

bool ShaderLoader::CompileVertexShader(ID3D10Blob** vsBlob, ID3D11VertexShader** vertexShader, string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros, D3D11Device* d3d11device)
{
	HRESULT result = CompileShader(path, macros, entrypoint, profile, vsBlob);
	if (FAILED(result))
		return false;

	// Create the shader from the buffer.
	ID3D10Blob* vsb = *vsBlob;
	result = d3d11device->GetDevice()->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, vertexShader);
	if (FAILED(result))
	{
		LOG("Failed to create vertex shader.", Log::Error);
		return false;
	}

	return true;
}


bool ShaderLoader::CompilePixelShader(ID3D10Blob** psBlob, ID3D11PixelShader** pixelShader, string path, LPCSTR entrypoint, LPCSTR profile, D3D_SHADER_MACRO* macros, D3D11Device* d3d11device)
{
	HRESULT result = CompileShader(path, macros, entrypoint, profile, psBlob);
	if (FAILED(result))
		return false;

	ID3D10Blob* psb = *psBlob;
	// Create the shader from the buffer.
	result = d3d11device->GetDevice()->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, pixelShader);
	if (FAILED(result))
	{
		LOG("Failed to create pixel shader.", Log::Error);
		return false;
	}

	return true;
}

wstring s2ws(const string& s)
{
	int len;
	int slength = int(s.length()) + 1;
	len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, nullptr, 0);
	wchar_t* buf = new wchar_t[len];
	MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
	wstring r(buf);
	delete[] buf;
	return r;
}

HRESULT ShaderLoader::CompileShader(string filePath, D3D_SHADER_MACRO* macros, LPCSTR entryPoint, LPCSTR target, ID3DBlob** shaderBlobOut)
{
	HRESULT hr;
	ID3DBlob* errorBlob = nullptr;
	ID3DBlob* shaderBlob = nullptr;

	unsigned compileFlags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_PREFER_FLOW_CONTROL;
#endif

	// Load and compile from file
	hr = D3DCompileFromFile(
		s2ws(filePath).c_str(),
		macros,
		D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint,
		target,
		compileFlags,
		0,
		&shaderBlob,
		&errorBlob
	);

	// Handle any errors
	if (FAILED(hr))
	{
		string shaderName = FileHelper::GetFileNameFromPath(filePath);
		if (errorBlob)
		{
			OutputCompilationErrorMessageAsTextFile(errorBlob);
			LOG("Failed to compile shader. File = " + shaderName +
			    ", EntryPoint = " + entryPoint +
			    ", Target = " + target +
			    ". Check shaderError.txt for more details.",
			    Log::Error);
			DirectusSafeRelease(errorBlob);
		}
		else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
			LOG("Failed to find shader \"" + shaderName + " \" with path \"" + filePath + "\".", Log::Error);
		else
			LOG("An unkown error occured when trying to load and compile \"" + shaderName + "\"", Log::Error);
	}

	// Write to blob out
	*shaderBlobOut = shaderBlob;

	return hr;
}

void ShaderLoader::OutputCompilationErrorMessageAsTextFile(ID3D10Blob* errorMessage)
{
	char* compileErrors;
	unsigned long bufferSize, i;
	ofstream fout;

	// Get a pointer to the error message text buffer.
	compileErrors = static_cast<char*>(errorMessage->GetBufferPointer());

	// Get the length of the message.
	bufferSize = errorMessage->GetBufferSize();

	// Open a file to write the error message to.
	fout.open("shaderError.txt");

	// Write out the error message.
	for (i = 0; i < bufferSize; i++)
		fout << compileErrors[i];

	// Close the file.
	fout.close();

	// Release the error message.
	errorMessage->Release();
}
