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
#include <fstream>
#include <algorithm>
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace SettingsIO
{
	ofstream fout;
	ifstream fin;
	string fileName;
}

namespace Directus
{
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
			auto firstIndex = line.find_first_of('=');
			if (name == line.substr(0, firstIndex))
			{
				auto lastindex = line.find_last_of('=');
				string readValue = line.substr(lastindex + 1, line.length());
				value = (T)stof(readValue);
				return;
			}
		}
	}

	Settings::Settings()
	{
		m_maxThreadCount = thread::hardware_concurrency();
	}

	void Settings::Initialize()
	{
		if (FileSystem::FileExists(SettingsIO::fileName))
		{
			// Create a settings file
			SettingsIO::fin.open(SettingsIO::fileName, ifstream::in);

			float resolutionX = 0;
			float resolutionY = 0;

			// Read the settings
			ReadSetting(SettingsIO::fin, "bFullScreen",				m_isFullScreen);
			ReadSetting(SettingsIO::fin, "iVSync",					m_vsync);
			ReadSetting(SettingsIO::fin, "bIsMouseVisible",			m_isMouseVisible);
			ReadSetting(SettingsIO::fin, "fResolutionWidth",		resolutionX);
			ReadSetting(SettingsIO::fin, "fResolutionHeight",		resolutionY);
			ReadSetting(SettingsIO::fin, "iShadowMapResolution",	m_shadowMapResolution);
			ReadSetting(SettingsIO::fin, "iAnisotropy",				m_anisotropy);
			ReadSetting(SettingsIO::fin, "fFPSLimit",				m_maxFPS_game);
			ReadSetting(SettingsIO::fin, "iMaxThreadCount",			m_maxThreadCount);
			
			m_resolution = Vector2(resolutionX, resolutionY);

			// Close the file.
			SettingsIO::fin.close();
		}
		else
		{
			// Create a settings file
			SettingsIO::fout.open(SettingsIO::fileName, ofstream::out);

			// Write the settings
			WriteSetting(SettingsIO::fout, "bFullScreen",			m_isFullScreen);
			WriteSetting(SettingsIO::fout, "iVSync",				m_vsync);
			WriteSetting(SettingsIO::fout, "bIsMouseVisible",		m_isMouseVisible);
			WriteSetting(SettingsIO::fout, "fResolutionWidth",		m_resolution.x);
			WriteSetting(SettingsIO::fout, "fResolutionHeight",		m_resolution.y);
			WriteSetting(SettingsIO::fout, "iShadowMapResolution",	m_shadowMapResolution);
			WriteSetting(SettingsIO::fout, "iAnisotropy",			m_anisotropy);
			WriteSetting(SettingsIO::fout, "fFPSLimit",				m_maxFPS_game);
			WriteSetting(SettingsIO::fout, "iMaxThreadCount",		m_maxThreadCount);

			// Close the file.
			SettingsIO::fout.close();
		}

		LOGF_INFO("Settings::Initialize: Resolution: %dx%d",		(int)m_resolution.x, (int)m_resolution.y);
		LOGF_INFO("Settings::Initialize: Shadow resolution: %d",	m_shadowMapResolution);
		LOGF_INFO("Settings::Initialize: Anisotropy: %d",			m_anisotropy);
		LOGF_INFO("Settings::Initialize: Max fps: %f",				m_maxFPS_game);
		LOGF_INFO("Settings::Initialize: Max threads: %d",			m_maxThreadCount);
	}

	void Settings::DisplayMode_Add(unsigned int width, unsigned int height, unsigned int refreshRateNumerator, unsigned int refreshRateDenominator)
	{
		m_displayModes.emplace_back(width, height, refreshRateNumerator, refreshRateDenominator);
	}

	const Directus::DisplayMode& Settings::DisplayMode_GetFastest()
	{
		DisplayMode& fastestMode = m_displayModes.front();
		for (const auto& mode : m_displayModes)
		{
			if (fastestMode.refreshRate < mode.refreshRate)
			{
				fastestMode = mode;
			}
		}

		return fastestMode;
	}

	void Settings::DisplayAdapter_Add(const string& name, unsigned int memory, unsigned int vendorID, void* data)
	{
		m_displayAdapters.emplace_back(name, memory, vendorID, data);
		sort(m_displayAdapters.begin(), m_displayAdapters.end(), [](const DisplayAdapter& adapter1, const DisplayAdapter& adapter2)
		{
			return adapter1.memory > adapter2.memory;
		});		
	}

	void Settings::DisplayAdapter_SetPrimary(const DisplayAdapter* primaryAdapter)
	{
		if (!primaryAdapter)
			return;

		m_primaryAdapter = primaryAdapter;
		LOGF_INFO("Settings::DisplayAdapter_SetPrimary: %s (%d MB)", primaryAdapter->name.c_str(), primaryAdapter->memory);
	}
}