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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ===========================
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include <sstream> 
#include <fstream>
#include <atomic>
#include <dxc/Support/WinIncludes.h>
#include <dxc/dxcapi.h>
//======================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Shader::~RHI_Shader()
	{
		auto rhi_context = m_rhi_device->GetContext();

		if (HasVertexShader())
		{
			vkDestroyShaderModule(rhi_context->device, static_cast<VkShaderModule>(m_vertex_shader), nullptr);
			m_vertex_shader = nullptr;
		}

		if (HasPixelShader())
		{
			vkDestroyShaderModule(rhi_context->device, static_cast<VkShaderModule>(m_pixel_shader), nullptr);
			m_pixel_shader = nullptr;
		}
	}

	/*
	OVERVIEW: HLSL Compiler

	Version: dxcompiler.dll: 1.4(dev;1989-72b1a67a)
	
	USAGE: dxc.exe [options] <inputs>
	
	Common Options:
	  -help              Display available options
	  -nologo            Suppress copyright message
	  -Qunused-arguments Don't emit warning for unused driver arguments
	
	Compilation Options:
	  -all_resources_bound    Enables agressive flattening
	  -auto-binding-space <value>
							  Set auto binding space - enables auto resource binding in libraries
	  -Cc                     Output color coded assembly listings
	  -default-linkage <value>
							  Set default linkage for non-shader functions when compiling or linking to a library target (internal, external)
	  -denorm <value>         select denormal value options (any, preserve, ftz). any is the default.
	  -D <value>              Define macro
	  -enable-16bit-types     Enable 16bit types and disable min precision types. Available in HLSL 2018 and shader model 6.2
	  -export-shaders-only    Only export shaders when compiling a library
	  -exports <value>        Specify exports when compiling a library: export1[[,export1_clone,...]=internal_name][;...]
	  -E <value>              Entry point name
	  -Fc <file>              Output assembly code listing file
	  -Fd <file>              Write debug information to the given file or directory; trail \ to auto-generate and imply Qstrip_priv
	  -Fe <file>              Output warnings and errors to the given file
	  -Fh <file>              Output header file containing object code
	  -flegacy-macro-expansion
							  Expand the operands before performing token-pasting operation (fxc behavior)
	  -flegacy-resource-reservation
							  Reserve unused explicit register assignments for compatibility with shader model 5.0 and below
	  -force_rootsig_ver <profile>
							  force root signature version (rootsig_1_1 if omitted)
	  -Fo <file>              Output object file
	  -Gec                    Enable backward compatibility mode
	  -Ges                    Enable strict mode
	  -Gfa                    Avoid flow control constructs
	  -Gfp                    Prefer flow control constructs
	  -Gis                    Force IEEE strictness
	  -HV <value>             HLSL version (2016, 2017, 2018). Default is 2018
	  -H                      Show header includes and nesting depth
	  -ignore-line-directives Ignore line directives
	  -I <value>              Add directory to include search path
	  -Lx                     Output hexadecimal literals
	  -Ni                     Output instruction numbers in assembly listings
	  -no-warnings            Suppress warnings
	  -not_use_legacy_cbuf_load
							  Do not use legacy cbuffer load
	  -No                     Output instruction byte offsets in assembly listings
	  -Odump                  Print the optimizer commands.
	  -Od                     Disable optimizations
	  -pack_optimized         Optimize signature packing assuming identical signature provided for each connecting stage
	  -pack_prefix_stable     (default) Pack signatures preserving prefix-stable property - appended elements will not disturb placement of prior elements
	  -recompile              recompile from DXIL container with Debug Info or Debug Info bitcode file
	  -res_may_alias          Assume that UAVs/SRVs may alias
	  -rootsig-define <value> Read root signature from a #define
	  -T <profile>            Set target profile.
		<profile>: ps_6_0, ps_6_1, ps_6_2, ps_6_3, ps_6_4,
			 vs_6_0, vs_6_1, vs_6_2, vs_6_3, vs_6_4,
			 cs_6_0, cs_6_1, cs_6_2, cs_6_3, cs_6_4,
			 gs_6_0, gs_6_1, gs_6_2, gs_6_3, gs_6_4,
			 ds_6_0, ds_6_1, ds_6_2, ds_6_3, ds_6_4,
			 hs_6_0, hs_6_1, hs_6_2, hs_6_3, hs_6_4,
			 lib_6_3, lib_6_4
	  -Vd                     Disable validation
	  -Vi                     Display details about the include process.
	  -Vn <name>              Use <name> as variable name in header file
	  -WX                     Treat warnings as errors
	  -Zi                     Enable debug information
	  -Zpc                    Pack matrices in column-major order
	  -Zpr                    Pack matrices in row-major order
	  -Zsb                    Build debug name considering only output binary
	  -Zss                    Build debug name considering source information
	
	Optimization Options:
	  -O0 Optimization Level 0
	  -O1 Optimization Level 1
	  -O2 Optimization Level 2
	  -O3 Optimization Level 3 (Default)
	
	SPIR-V CodeGen Options:
	  -fspv-debug=<value>     Specify whitelist of debug info category (file -> source -> line, tool)
	  -fspv-extension=<value> Specify SPIR-V extension permitted to use
	  -fspv-reflect           Emit additional SPIR-V instructions to aid reflection
	  -fspv-target-env=<value>
							  Specify the target environment: vulkan1.0 (default) or vulkan1.1
	  -fvk-b-shift <shift> <space>
							  Specify Vulkan binding number shift for b-type register
	  -fvk-bind-register <type-number> <space> <binding> <set>
							  Specify Vulkan descriptor set and binding for a specific register
	  -fvk-invert-y           Negate SV_Position.y before writing to stage output in VS/DS/GS to accommodate Vulkan's coordinate system
	  -fvk-s-shift <shift> <space>
							  Specify Vulkan binding number shift for s-type register
	  -fvk-t-shift <shift> <space>
							  Specify Vulkan binding number shift for t-type register
	  -fvk-u-shift <shift> <space>
							  Specify Vulkan binding number shift for u-type register
	  -fvk-use-dx-layout      Use DirectX memory layout for Vulkan resources
	  -fvk-use-dx-position-w  Reciprocate SV_Position.w after reading from stage input in PS to accommodate the difference between Vulkan and DirectX
	  -fvk-use-gl-layout      Use strict OpenGL std140/std430 memory layout for Vulkan resources
	  -fvk-use-scalar-layout  Use scalar memory layout for Vulkan resources
	  -Oconfig=<value>        Specify a comma-separated list of SPIRV-Tools passes to customize optimization configuration (see http://khr.io/hlsl2spirv#optimization)
	  -spirv                  Generate SPIR-V code
	
	Utility Options:
	  -dumpbin              Load a binary file rather than compiling
	  -extractrootsignature Extract root signature from shader bytecode (must be used with /Fo <file>)
	  -getprivate <file>    Save private data from shader blob
	  -P <value>            Preprocess to file (must be used alone)
	  -Qstrip_debug         Strip debug information from 4_0+ shader bytecode  (must be used with /Fo <file>)
	  -Qstrip_priv          Strip private data from shader bytecode  (must be used with /Fo <file>)
	  -Qstrip_reflect       Strip reflection data from shader bytecode  (must be used with /Fo <file>)
	  -Qstrip_rootsignature Strip root signature data from shader bytecode  (must be used with /Fo <file>)
	  -setprivate <file>    Private data to add to compiled shader blob
	  -setrootsignature <file>
							Attach root signature to shader bytecode
	  -verifyrootsignature <file>
							Verify shader bytecode with root signature
	*/

	namespace DxShaderCompiler
	{
		inline bool ValidateOperationResult(IDxcOperationResult* operation_result)
		{
			if (!operation_result)
				return true;

			// Get status
			HRESULT operation_status;
			operation_result->GetStatus(&operation_status);
			if (SUCCEEDED(operation_status))
				return true;

			// If the status is not successful, log error buffer
			IDxcBlobEncoding* error_buffer = nullptr;
			operation_result->GetErrorBuffer(&error_buffer);
			if (!error_buffer)
				return true;

			stringstream ss(string(static_cast<char*>(error_buffer->GetBufferPointer()), error_buffer->GetBufferSize()));
			string line;
			while (getline(ss, line, '\n'))
			{
				const auto is_error = line.find("error") != string::npos;
				if (is_error) LOG_ERROR(line) else LOG_WARNING(line);
			}

			safe_release(error_buffer);
			return false;
		}

		inline string utf16_utf8(wstring source)
		{
			string destination;
			wchar_t symbol;
			unsigned char code0;
			unsigned char code1;
			unsigned char code2;

			for (wstring::iterator it = source.begin(); it != source.end(); it++)
			{
				symbol = static_cast<wchar_t>(*it);

				code0 = 0;
				code1 = 0;
				code2 = 0;

				if (symbol < 0x00000080) 
				{
					code0 = static_cast<char>(*it);
					destination.push_back(code0);
				}
				else if (symbol < 0x00000800) 
				{
					code0 = symbol % 0x40;
					code1 = (symbol - code0) / 0x40;

					destination.push_back(0xc0 | code1);
					destination.push_back(0x80 | code0);
				}
				else 
				{
					code0 = symbol % 0x40;
					code1 = ((symbol - code0) / 0x40) % 0x40;
					code2 = (symbol - code0 - (0x40 * code1)) / 0x1000;

					destination.push_back(0xe0 | code2);
					destination.push_back(0x80 | code1);
					destination.push_back(0x80 | code0);
				}
			}

			return destination;
		}

		class Instance
		{
		public:
			Instance()
			{
				DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler), reinterpret_cast<void**>(&compiler));
				DxcCreateInstance(CLSID_DxcLibrary, __uuidof(IDxcLibrary), reinterpret_cast<void**>(&library));
			}

			static Instance& Get()
			{
				static Instance instance;
				return instance;
			}

			CComPtr<IDxcCompiler> compiler = nullptr;
			CComPtr<IDxcLibrary> library = nullptr;
		};

		typedef std::vector<uint8_t> Blob;
		Blob* IncludeDirectiveLoadCallback(const std::string& include_path)
		{
			vector<char> ret;
			ifstream include_file(include_path, ios_base::in);
			if (include_file)
			{
				include_file.seekg(0, ios::end);
				ret.resize(include_file.tellg());
				include_file.seekg(0, ios::beg);
				include_file.read(ret.data(), ret.size());
				ret.resize(include_file.gcount());
			}
			else
			{
				throw std::runtime_error(std::string("Failed to load include  ") + include_path + ".");
			}

			return new Blob(reinterpret_cast<const uint8_t*>(ret.data()), reinterpret_cast<const uint8_t*>(ret.data()) + ret.size());
		}

		class SpartanIncludeHandler : public IDxcIncludeHandler
		{
		public:
			explicit SpartanIncludeHandler(const string& shader_root_directory)
			{
				m_shader_root_directory = shader_root_directory;
			}

			HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR file_name, IDxcBlob** include_source) override
			{
				if ((file_name[0] == L'.') && (file_name[1] == L'/'))
				{
					file_name += 2;
				}
				
				string file_name_utf8 = utf16_utf8(file_name);

				auto blob_deleter = [](Blob* blob) { delete blob; };
				unique_ptr<Blob, decltype(blob_deleter)> source(nullptr, blob_deleter);
				try
				{
					source.reset(IncludeDirectiveLoadCallback(m_shader_root_directory + file_name_utf8));
				}
				catch (std::exception const& e)
				{
					LOGF_ERROR(e.what());
					return E_FAIL;
				}

				*include_source = nullptr;
				return Instance::Get().library->CreateBlobWithEncodingOnHeapCopy
				(
					static_cast<void*>(source->data()),
					static_cast<uint32_t>(source->size()),
					CP_UTF8,
					reinterpret_cast<IDxcBlobEncoding**>(include_source)
				);
			}

			ULONG STDMETHODCALLTYPE AddRef() override
			{
				++m_ref;
				return m_ref;
			}

			ULONG STDMETHODCALLTYPE Release() override
			{
				--m_ref;
				ULONG result = m_ref;
				if (result == 0)
				{
					delete this;
				}
				return result;
			}

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
			{
				if (IsEqualIID(iid, __uuidof(IDxcIncludeHandler)))
				{
					*object = dynamic_cast<IDxcIncludeHandler*>(this);
					this->AddRef();
					return S_OK;
				}
				else if (IsEqualIID(iid, __uuidof(IUnknown)))
				{
					*object = dynamic_cast<IUnknown*>(this);
					this->AddRef();
					return S_OK;
				}
				else
				{
					return E_NOINTERFACE;
				}
			}

			
		private:
			string m_shader_root_directory;
			atomic<ULONG> m_ref = 0;
		};
	}
	
	void* RHI_Shader::_Compile(const Shader_Type type, const string& shader, RHI_Vertex_Attribute_Type vertex_attributes /*= Vertex_Attribute_None*/)
	{
		// Deduce some things
		bool is_file		= FileSystem::IsSupportedShaderFile(shader);
		wstring file_name	= is_file ? FileSystem::StringToWstring(FileSystem::GetFileNameFromFilePath(shader)) : wstring(L"shader");
		string file_directory;
		if (is_file)
		{
			file_directory = FileSystem::GetDirectoryFromFilePath(shader);
		}

		// Arguments
		auto entry_point		= FileSystem::StringToWstring((type == Shader_Vertex) ? _RHI_Shader::entry_point_vertex : _RHI_Shader::entry_point_pixel);
		auto target_profile		= FileSystem::StringToWstring((type == Shader_Vertex) ? "vs_" + _RHI_Shader::shader_model : "ps_" + _RHI_Shader::shader_model);
		// auto include_directory	= wstring(L"-I ") + file_directory; // Doesn't work
		vector<LPCWSTR> arguments;
		{	
			arguments.emplace_back(L"-spirv");
			arguments.emplace_back(L"-fspv-reflect");			
			arguments.emplace_back(L"-fvk-use-dx-layout");
			//if (is_file) arguments.emplace_back(include_directory.c_str()); // Doesn't work
			if (type == Shader_Vertex) arguments.emplace_back(L"-fvk-invert-y"); // Can only be used in VS/DS/GS
			arguments.emplace_back(L"-flegacy-macro-expansion");
			#ifdef DEBUG
			arguments.emplace_back(L"-Zi");
			#endif
		}

		// Create standard defines
		vector<DxcDefine> defines =
		{
			DxcDefine{ L"COMPILE_VS", type == Shader_Vertex ? L"1" : L"0" },
			DxcDefine{ L"COMPILE_PS", type == Shader_Pixel ? L"1" : L"0" }
		};
		// Convert defines to wstring...
		map<wstring, wstring> defines_wstring;
		for (const auto& define : m_defines)
		{
			auto first	= FileSystem::StringToWstring(define.first);
			auto second = FileSystem::StringToWstring(define.second);
			defines_wstring[first] = second;
		}
		// ... and add them to our defines
		for (const auto& define : defines_wstring)
		{
			defines.emplace_back(DxcDefine{ define.first.c_str(), define.second.c_str() });
		}

		// Get shader source as a buffer
		CComPtr<IDxcBlobEncoding> shader_blob = nullptr;
		{
			HRESULT result;
			if (is_file)
			{
				auto file_path = FileSystem::StringToWstring(shader);				
				result = DxShaderCompiler::Instance::Get().library->CreateBlobFromFile(file_path.c_str(), nullptr, &shader_blob);
			}
			else // Source
			{
				result = DxShaderCompiler::Instance::Get().library->CreateBlobWithEncodingFromPinned(shader.c_str(), static_cast<uint32_t>(shader.size()), CP_UTF8, &shader_blob);
			}

			if (FAILED(result))
			{
				LOG_ERROR("Failed to create source buffer.");
				return nullptr;
			}
		}

		// Compile
		CComPtr<IDxcIncludeHandler> include_handler = new DxShaderCompiler::SpartanIncludeHandler(file_directory);
		CComPtr<IDxcOperationResult> compilation_result = nullptr;
		{
			if (FAILED(DxShaderCompiler::Instance::Get().compiler->Compile(
					shader_blob,												// shader blob
					file_name.c_str(),											// file name (for warnings and errors)
					entry_point.c_str(),										// entry point function
					target_profile.c_str(),										// target profile
					arguments.data(), static_cast<uint32_t>(arguments.size()),	// compilation arguments
					defines.data(), static_cast<uint32_t>(defines.size()),		// shader defines
					include_handler,											// handler for #include directives
					&compilation_result))
				)
			{
				LOGF_ERROR("Failed to compile %s", file_name.c_str());
				return nullptr;
			}

			if (!DxShaderCompiler::ValidateOperationResult(compilation_result))
			{
				LOGF_ERROR("Failed to compile %s", shader.c_str());
				return nullptr;
			}
		}
		
		// Create shader module
		CComPtr<IDxcBlob> shader_compiled	= nullptr;
		VkShaderModule shader_module		= nullptr;
		compilation_result->GetResult(&shader_compiled);			
		if (shader_compiled)
		{
			VkShaderModuleCreateInfo create_info = {};
			create_info.sType		= VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			create_info.codeSize	= static_cast<size_t>(shader_compiled->GetBufferSize());
			create_info.pCode		= reinterpret_cast<const uint32_t*>(shader_compiled->GetBufferPointer());
	
			if (vkCreateShaderModule(m_rhi_device->GetContext()->device, &create_info, nullptr, &shader_module) == VK_SUCCESS)
			{
				// Reflect shader resources (so that descriptor sets can be created later)
				_Reflect
				(
					type,
					reinterpret_cast<uint32_t*>(shader_compiled->GetBufferPointer()),
					static_cast<uint32_t>(shader_compiled->GetBufferSize() / 4)
				);

				// Input layout
				if (!m_input_layout->Create(nullptr, vertex_attributes))
				{
					LOGF_ERROR("Failed to create input layout for %s", FileSystem::GetFileNameFromFilePath(m_file_path).c_str());
				}				
			}	
			else
			{
				LOG_ERROR("Failed to create shader module.");
			}
		}	

		return static_cast<void*>(shader_module);
	}
}
#endif