//= INCLUDES ========================
#include "Settings.h"
#include <string>
#include "../FileSystem/FileSystem.h"
#include "../Logging/Log.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

//= Initialize with default values =========
bool Settings::m_isFullScreen = false;
int Settings::m_vsync = (int)Off;
bool Settings::m_isMouseVisible = true;
int Settings::m_resolutionWidth = 1920;
int Settings::m_resolutionHeight = 1080;
float Settings::m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
int Settings::m_shadowMapResolution = 2048;
unsigned int Settings::m_anisotropy = 16;
//==========================================

ofstream Settings::m_fout;
ifstream Settings::m_fin;
string Settings::m_settingsFileName = "Directus3D.ini";

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
		size_t firstIndex = line.find_first_of("=");	
		if (name == line.substr(0, firstIndex))
		{
			size_t lastindex = line.find_last_of("=");
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
		LOG_INFO(m_vsync);
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

void Settings::SetResolution(int width, int height)
{
	m_resolutionWidth = width;
	m_resolutionHeight = height;
	m_screenAspect = float(m_resolutionWidth) / float(m_resolutionHeight);
}
