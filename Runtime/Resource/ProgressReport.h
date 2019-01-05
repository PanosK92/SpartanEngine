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

//= INCLUDES ==================
#include "../Core/EngineDefs.h"
#include <string>
#include <map>
//=============================

namespace Directus
{
	static int g_progress_ModelImporter = 0;
	static int g_progress_Scene			= 1;

	struct Progress
	{
		Progress(){ Clear(); }

		void Clear()
		{
			status.clear();
			jobsDone	= 0;
			jobCount	= 0;
			isLoading	= false;
		}

		std::string status;
		int jobsDone;
		int jobCount;
		bool isLoading;
	};

	class ENGINE_CLASS ProgressReport
	{
	public:
		static ProgressReport& Get()
		{
			static ProgressReport instance;
			return instance;
		}

		ProgressReport(){}

		void Reset(int progressID)
		{
			m_reports[progressID].Clear();
		}

		const std::string& GetStatus(int progressID)				{ return m_reports[progressID].status; }
		void SetStatus(int progressID, const std::string& status)	{ m_reports[progressID].status = status; }
		void SetJobCount(int progressID, int jobCount)				{ m_reports[progressID].jobCount = jobCount;}
		void IncrementJobsDone(int progressID)						{ m_reports[progressID].jobsDone++; }
		void SetJobsDone(int progressID, int jobsDone)				{ m_reports[progressID].jobsDone = jobsDone; }
		float GetPercentage(int progressID)							{ return (float)m_reports[progressID].jobsDone / (float)m_reports[progressID].jobCount; }
		bool GetIsLoading(int progressID)							{ return m_reports[progressID].isLoading; }
		void SetIsLoading(int progressID, bool isLoading)			{ m_reports[progressID].isLoading = isLoading; }

	private:	
		std::map<int, Progress> m_reports;
	};
}