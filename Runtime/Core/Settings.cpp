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
#include <fstream>
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
		m_isFullScreen			= false;
		m_vsync					= (int)Off;
		m_isMouseVisible		= true;
		m_resolution			= Vector2(1920, 1080);
		m_viewport				= Vector4(0.0f, 0.0f, m_resolution.x, m_resolution.y);
		m_shadowMapResolution	= 2048;
		m_anisotropy			= 16;
		SettingsIO::fileName	= "Directus3D.ini";
		g_versionPugiXML		= "1.80";
		m_maxFPS				= 165.0f;
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
			ReadSetting(SettingsIO::fin, "FullScreen",			m_isFullScreen);
			ReadSetting(SettingsIO::fin, "VSync",				m_vsync);
			ReadSetting(SettingsIO::fin, "IsMouseVisible",		m_isMouseVisible);
			ReadSetting(SettingsIO::fin, "ResolutionWidth",		resolutionX);
			ReadSetting(SettingsIO::fin, "ResolutionHeight",	resolutionY);
			ReadSetting(SettingsIO::fin, "ShadowMapResolution",	m_shadowMapResolution);
			ReadSetting(SettingsIO::fin, "Anisotropy",			m_anisotropy);
			ReadSetting(SettingsIO::fin, "FPSLimit",			m_maxFPS);
			
			m_resolution = Vector2(resolutionX, resolutionY);

			// Close the file.
			SettingsIO::fin.close();
		}
		else
		{
			// Create a settings file
			SettingsIO::fout.open(SettingsIO::fileName, ofstream::out);

			// Write the settings
			WriteSetting(SettingsIO::fout, "FullScreen",			m_isFullScreen);
			WriteSetting(SettingsIO::fout, "VSync",					m_vsync);
			WriteSetting(SettingsIO::fout, "IsMouseVisible",		m_isMouseVisible);
			WriteSetting(SettingsIO::fout, "ResolutionWidth",		m_resolution.x);
			WriteSetting(SettingsIO::fout, "ResolutionHeight",		m_resolution.y);
			WriteSetting(SettingsIO::fout, "ShadowMapResolution",	m_shadowMapResolution);
			WriteSetting(SettingsIO::fout, "Anisotropy",			m_anisotropy);
			WriteSetting(SettingsIO::fout, "FPSLimit",				m_maxFPS);

			// Close the file.
			SettingsIO::fout.close();
		}
	}
}