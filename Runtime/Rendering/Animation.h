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

//= INCLUDES =====================
#include "../Resource/IResource.h"
#include "../Math/Matrix.h"
//================================

namespace Directus
{
	struct VertexWeight
	{
		unsigned int vertexID;
		float weight;
	};

	struct Bone
	{
		std::string name;
		std::vector<VertexWeight> vertexWeights;
		Math::Matrix offset;
	};

	struct KeyVector
	{
		double time;
		Math::Vector3 value;
	};

	struct KeyQuaternion
	{
		double time;
		Math::Quaternion value;
	};

	struct AnimationNode
	{
		std::string name;
		std::vector<KeyVector> positionFrames;
		std::vector<KeyQuaternion> rotationFrames;
		std::vector<KeyVector> scaleFrames;
	};

	class ENGINE_CLASS Animation : public IResource
	{
	public:
		Animation(Context* context);
		~Animation();

		//= RESOURCE INTERFACE ========================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		//=============================================

		void SetName(const std::string& name) { m_name = name; }
		void SetDuration(double duration) { m_duration = duration; }
		void SetTicksPerSec(double ticksPerSec) { m_ticksPerSec = ticksPerSec; }

	private:
		std::string m_name;
		double m_duration;
		double m_ticksPerSec;

		// Each channel controls a single node
		std::vector<AnimationNode> m_channels;
	};
}