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
#include <thread>
#include "../Logging/Log.h"
#include "../FileSystem/FileSystem.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace SettingsIO
{
	ofstream fout;
	ifstream fin;
	string file_name = "Directus.ini";
}

namespace Directus
{
	template <class T>
	void write_setting(ofstream& fout, const string& name, T value)
	{
		fout << name << "=" << value << endl;
	}

	template <class T>
	void read_setting(ifstream& fin, const string& name, T& value)
	{
		for (string line; getline(fin, line); )
		{
			const auto first_index = line.find_first_of('=');
			if (name == line.substr(0, first_index))
			{
				const auto lastindex = line.find_last_of('=');
				const auto read_value = line.substr(lastindex + 1, line.length());
				value = static_cast<T>(stof(read_value));
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
		if (FileSystem::FileExists(SettingsIO::file_name))
		{
			// Create a settings file
			SettingsIO::fin.open(SettingsIO::file_name, ifstream::in);

			float resolution_x = 0;
			float resolution_y = 0;

			// Read the settings
			read_setting(SettingsIO::fin, "bFullScreen",			m_isFullScreen);
			read_setting(SettingsIO::fin, "bIsMouseVisible",		m_isMouseVisible);
			read_setting(SettingsIO::fin, "fResolutionWidth",		resolution_x);
			read_setting(SettingsIO::fin, "fResolutionHeight",		resolution_y);
			read_setting(SettingsIO::fin, "iShadowMapResolution",	m_shadowMapResolution);
			read_setting(SettingsIO::fin, "iAnisotropy",			m_anisotropy);
			read_setting(SettingsIO::fin, "fFPSLimit",				m_fpsLimit);
			read_setting(SettingsIO::fin, "iMaxThreadCount",		m_maxThreadCount);

			m_windowSize = Vector2(resolution_x, resolution_y);

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
			SettingsIO::fout.open(SettingsIO::file_name, ofstream::out);

			// Write the settings
			write_setting(SettingsIO::fout, "bFullScreen",			m_isFullScreen);
			write_setting(SettingsIO::fout, "bIsMouseVisible",		m_isMouseVisible);
			write_setting(SettingsIO::fout, "fResolutionWidth",		m_windowSize.x);
			write_setting(SettingsIO::fout, "fResolutionHeight",	m_windowSize.y);
			write_setting(SettingsIO::fout, "iShadowMapResolution",	m_shadowMapResolution);
			write_setting(SettingsIO::fout, "iAnisotropy",			m_anisotropy);
			write_setting(SettingsIO::fout, "fFPSLimit",			m_fpsLimit);
			write_setting(SettingsIO::fout, "iMaxThreadCount",		m_maxThreadCount);

			// Close the file.
			SettingsIO::fout.close();
		}

		LOGF_INFO("Resolution: %dx%d",		static_cast<int>(m_windowSize.x), static_cast<int>(m_windowSize.y));
		LOGF_INFO("Shadow resolution: %d",	m_shadowMapResolution);
		LOGF_INFO("Anisotropy: %d",			m_anisotropy);
		LOGF_INFO("Max fps: %f",			m_fpsLimit);
		LOGF_INFO("Max threads: %d",		m_maxThreadCount);
	}

	void Settings::SetFpsLimit(const float fps)
	{
		if (m_fpsLimit != fps)
		{
			LOGF_INFO("FPS limit set to %f", fps);
		}

		m_fpsLimit = fps;
	}
}