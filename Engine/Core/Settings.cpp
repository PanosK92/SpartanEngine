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

//= INCLUDES ========================
#include "Settings.h"
#include <string>
#include <fstream>
#include <algorithm>
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
#include "../Math/MathHelper.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace SettingsIO
{
	ofstream fout;
	ifstream fin;
	string fileName = "Directus.ini";
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
			ReadSetting(SettingsIO::fin, "bIsMouseVisible",			m_isMouseVisible);
			ReadSetting(SettingsIO::fin, "fResolutionWidth",		resolutionX);
			ReadSetting(SettingsIO::fin, "fResolutionHeight",		resolutionY);
			ReadSetting(SettingsIO::fin, "iShadowMapResolution",	m_shadowMapResolution);
			ReadSetting(SettingsIO::fin, "iAnisotropy",				m_anisotropy);
			ReadSetting(SettingsIO::fin, "fFPSLimit",				m_fpsLimit);
			ReadSetting(SettingsIO::fin, "iMaxThreadCount",			m_maxThreadCount);

			m_windowSize = Vector2(resolutionX, resolutionY);

			if (m_fpsLimit == 0.0f)
			{
				m_fpsPolicy = FPS_Unlocked;
				m_fpsLimit	= FLT_MAX;
			}
			else if (m_fpsLimit > 0.0f)
			{
				m_fpsPolicy = FPS_Locked;				
			}
			else
			{
				m_fpsPolicy = FPS_MonitorMatch;
			}

			// Close the file.
			SettingsIO::fin.close();
		}
		else
		{
			// Create a settings file
			SettingsIO::fout.open(SettingsIO::fileName, ofstream::out);

			// Write the settings
			WriteSetting(SettingsIO::fout, "bFullScreen",			m_isFullScreen);
			WriteSetting(SettingsIO::fout, "bIsMouseVisible",		m_isMouseVisible);
			WriteSetting(SettingsIO::fout, "fResolutionWidth",		m_windowSize.x);
			WriteSetting(SettingsIO::fout, "fResolutionHeight",		m_windowSize.y);
			WriteSetting(SettingsIO::fout, "iShadowMapResolution",	m_shadowMapResolution);
			WriteSetting(SettingsIO::fout, "iAnisotropy",			m_anisotropy);
			WriteSetting(SettingsIO::fout, "fFPSLimit",				m_fpsLimit);
			WriteSetting(SettingsIO::fout, "iMaxThreadCount",		m_maxThreadCount);

			// Close the file.
			SettingsIO::fout.close();
		}

		LOGF_INFO("Resolution: %dx%d",		(int)m_windowSize.x, (int)m_windowSize.y);
		LOGF_INFO("Shadow resolution: %d",	m_shadowMapResolution);
		LOGF_INFO("Anisotropy: %d",			m_anisotropy);
		LOGF_INFO("Max fps: %f",			m_fpsLimit);
		LOGF_INFO("Max threads: %d",		m_maxThreadCount);
	}

	void Settings::DisplayMode_Add(unsigned int width, unsigned int height, unsigned int refreshRateNumerator, unsigned int refreshRateDenominator)
	{
		DisplayMode& mode = m_displayModes.emplace_back(width, height, refreshRateNumerator, refreshRateDenominator);

		// Try to deduce the maximum frame rate based on how fast is the monitor
		if (m_fpsPolicy == FPS_MonitorMatch)
		{
			FPS_SetLimit(Math::Helper::Max(m_fpsLimit, mode.refreshRate));
		}
	}

	bool Settings::DisplayMode_GetFastest(DisplayMode* displayMode)
	{
		if (m_displayModes.empty())
			return false;

		displayMode = &m_displayModes[0];
		for (auto& mode : m_displayModes)
		{
			if (displayMode->refreshRate < mode.refreshRate)
			{
				displayMode = &mode;
			}
		}

		return true;
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
		LOGF_INFO("%s (%d MB)", primaryAdapter->name.c_str(), primaryAdapter->memory);
	}

	void Settings::FPS_SetLimit(float fps)
	{
		if (m_fpsLimit != fps)
		{
			LOGF_INFO("FPS limit set to %f", fps);
		}

		m_fpsLimit = fps;
	}
}