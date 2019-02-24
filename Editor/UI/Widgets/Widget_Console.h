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

#pragma once

//= INCLUDES ===============
#include "Widget.h"
#include <memory>
#include <functional>
#include <deque>
#include "Logging/ILogger.h"
#include "type_traits"        // for forward, move
#include "xstring"            // for string
//==========================

struct LogPackage
{
	std::string text;
	unsigned int error_level = 0;
};

// Implementation of Directus::ILogger so the engine can log into the editor
class EngineLogger : public Directus::ILogger
{
public:
	typedef std::function<void(LogPackage)> log_func;
	void SetCallback(log_func&& func)
	{
		m_log_func = std::forward<log_func>(func);
	}

	void Log(const std::string& text, const unsigned int error_level) override
	{
		LogPackage package;
		package.text = text;
		package.error_level = error_level;
		m_log_func(package);
	}

private:
	log_func m_log_func;
};

class Widget_Console : public Widget
{
public:
	Widget_Console(Directus::Context* context);
	void Tick(float delta_time) override;
	void AddLogPackage(const LogPackage& package);
	void Clear();

private:
	std::shared_ptr<EngineLogger> m_logger;
	std::deque<LogPackage> m_logs;
	unsigned int m_max_log_entries = 500;
	bool m_show_info;
	bool m_show_warnings;
	bool m_show_errors;
};
