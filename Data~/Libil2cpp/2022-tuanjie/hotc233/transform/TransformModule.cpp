#include "TransformModule.h"
#include "TransformContext.h"

namespace hotc233
{
namespace transform
{
	void TransformModule::Initialize()
	{
		TransformContext::InitializeInstinctHandlers();
	}
}
}