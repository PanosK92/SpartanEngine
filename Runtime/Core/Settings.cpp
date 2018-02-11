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

//= INCLUDES ========================
#include "Settings.h"
#include <string>
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	string Settings::g_versionAngelScript;
	string Settings::g_versionAssimp;
	string Settings::g_versionBullet;
	string Settings::g_versionFMOD;
	string Settings::g_versionFreeImage;
	string Settings::g_versionFreeType;
	string Settings::g_versionImGui;
	string Settings::g_versionPugiXML = "1.80";
	string Settings::g_versionSDL;

	//= Initialize with default values ================================================
	bool Settings::m_isFullScreen = false;
	int Settings::m_vsync = (int)Off;
	bool Settings::m_isMouseVisible = true;
	Vector2 Settings::m_resolution = Vector2(1920, 1080);
	Vector4 Settings::m_viewport = Vector4(0.0f, 0.0f, m_resolution.x, m_resolution.y);
	int Settings::m_shadowMapResolution = 2048;
	unsigned int Settings::m_anisotropy = 16;
	string Settings::m_settingsFileName = "Directus3D.ini";
	//=================================================================================
	ofstream Settings::m_fout;
	ifstream Settings::m_fin;

	template <class T>
	void WriteSetting(ofstream& fout, const string& name, T value)
	{
		fout << name << "=" << value << endl;
	}

	template <class T>
	void ReadSetting(ifstream& fin, const string& name, T& value)
	{
		for (string line; getline(fin, line); )
		{
			auto firstIndex = line.find_first_of("=");
			if (name == line.substr(0, firstIndex))
			{
				auto lastindex = line.find_last_of("=");
				string readValue = line.substr(lastindex + 1, line.length());
				value = (T)stof(readValue.c_str());
				return;
			}
		}
	}

	void Settings::Initialize()
	{
		if (FileSystem::FileExists(m_settingsFileName))
		{
			// Create a settings file
			m_fin.open(m_settingsFileName, ifstream::in);

			float resolutionX = 0;
			float resolutionY = 0;

			// Read the settings
			ReadSetting(m_fin, "FullScreen",			m_isFullScreen);
			ReadSetting(m_fin, "VSync",					m_vsync);
			ReadSetting(m_fin, "IsMouseVisible",		m_isMouseVisible);
			ReadSetting(m_fin, "ResolutionWidth",		resolutionX);
			ReadSetting(m_fin, "ResolutionHeight",		resolutionY);
			ReadSetting(m_fin, "ShadowMapResolution",	m_shadowMapResolution);
			ReadSetting(m_fin, "Anisotropy",			m_anisotropy);

			m_resolution = Vector2(resolutionX, resolutionY);

			// Close the file.
			m_fin.close();
		}
		else
		{
			// Create a settings file
			m_fout.open(m_settingsFileName, ofstream::out);

			// Write the settings
			WriteSetting(m_fout, "FullScreen", m_isFullScreen);
			WriteSetting(m_fout, "VSync", m_vsync);
			WriteSetting(m_fout, "IsMouseVisible", m_isMouseVisible);
			WriteSetting(m_fout, "ResolutionWidth", m_resolution.x);
			WriteSetting(m_fout, "ResolutionHeight", m_resolution.y);
			WriteSetting(m_fout, "ShadowMapResolution", m_shadowMapResolution);
			WriteSetting(m_fout, "Anisotropy", m_anisotropy);

			// Close the file.
			m_fout.close();
		}
	}

	//= PROPERTIES ==========================================================
	void Settings::SetResolution(int width, int height)
	{
		m_resolution = Vector2((float)width, (float)height);
	}

	void Settings::SetResolution(const Vector2& resolution)
	{
		m_resolution = resolution;
	}

	int Settings::GetResolutionWidth()
	{
		return (int)m_resolution.x;
	}

	int Settings::GetResolutionHeight()
	{
		return (int)m_resolution.y;
	}

	void Settings::SetViewport(int x, int y, int width, int height)
	{
		m_viewport = Vector4((float)x, (float)y, (float)width, (float)height);
	}

	void Settings::SetViewport(const Vector4& viewport)
	{
		m_viewport = viewport;
	}

	int Settings::GetViewportWidth()
	{
		return (int)m_viewport.z;
	}

	int Settings::GetViewportHeight()
	{
		return (int)m_viewport.w;
	}

	float Settings::GetScreenAspect()
	{
		return m_resolution.x / m_resolution.y;
	}

	//========================================================================
}