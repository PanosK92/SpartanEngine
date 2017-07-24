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

//= INCLUDES ========================
#include "Settings.h"
#include <string>
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	//= Initialize with default values ===================================================
	bool Settings::m_isFullScreen = false;
	int Settings::m_vsync = (int)Off;
	bool Settings::m_isMouseVisible = true;
	int Settings::m_resolutionWidth = 1920;
	int Settings::m_resolutionHeight = 1080;
	float Settings::m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
	int Settings::m_shadowMapResolution = 2048;
	unsigned int Settings::m_anisotropy = 16;
	bool Settings::m_debugDraw = true;
	string Settings::m_settingsFileName = "Directus3D.ini";
	//====================================================================================
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

			// Read the settings
			ReadSetting(m_fin, "FullScreen", m_isFullScreen);
			ReadSetting(m_fin, "VSync", m_vsync);
			ReadSetting(m_fin, "IsMouseVisible", m_isMouseVisible);
			ReadSetting(m_fin, "ResolutionWidth", m_resolutionWidth);
			ReadSetting(m_fin, "ResolutionHeight", m_resolutionHeight);
			ReadSetting(m_fin, "ShadowMapResolution", m_shadowMapResolution);
			ReadSetting(m_fin, "Anisotropy", m_anisotropy);

			m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);

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
			WriteSetting(m_fout, "ResolutionWidth", m_resolutionWidth);
			WriteSetting(m_fout, "ResolutionHeight", m_resolutionHeight);
			WriteSetting(m_fout, "ShadowMapResolution", m_shadowMapResolution);
			WriteSetting(m_fout, "Anisotropy", m_anisotropy);

			// Close the file.
			m_fout.close();
		}
	}

	//= PROPERTIES ==========================================================
	bool Settings::IsFullScreen()
	{
		return m_isFullScreen;
	}

	bool Settings::IsMouseVisible()
	{
		return m_isMouseVisible;
	}

	VSync Settings::GetVSync()
	{
		return (VSync)m_vsync;
	}

	void Settings::SetResolution(int width, int height)
	{
		m_resolutionWidth = width;
		m_resolutionHeight = height;
		m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
	}

	Math::Vector2 Settings::GetResolution()
	{
		return Math::Vector2(m_resolutionWidth, m_resolutionHeight);
	}

	int Settings::GetResolutionWidth()
	{
		return m_resolutionWidth;
	}

	int Settings::GetResolutionHeight()
	{
		return m_resolutionHeight;
	}

	float Settings::GetScreenAspect()
	{
		return m_screenAspect;
	}

	int Settings::GetShadowMapResolution()
	{
		return m_shadowMapResolution;
	}

	unsigned Settings::GetAnisotropy()
	{
		return m_anisotropy;
	}

	void Settings::SetDebugDraw(bool enabled)
	{
		m_debugDraw = enabled;
	}

	bool Settings::GetDebugDraw()
	{
		return m_debugDraw;
	}
	//========================================================================
}