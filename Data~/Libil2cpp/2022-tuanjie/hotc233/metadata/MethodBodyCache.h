#pragma once

#include "Image.h"

namespace hotc233
{
namespace metadata
{
	class MethodBodyCache
	{
	public:
		static MethodBody* GetMethodBody(hotc233::metadata::Image* image, uint32_t token);
		static void EnableShrinkMethodBodyCache(bool shrink);

		static bool IsInlineable(const MethodInfo* method);
		static void DisableInline(const MethodInfo* method);
	};
}
}