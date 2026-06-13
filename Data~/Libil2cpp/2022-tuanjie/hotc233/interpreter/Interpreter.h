#pragma once

#include "InterpreterDefs.h"

namespace hotc233
{

namespace interpreter
{

	class Interpreter
	{
	public:

		static void Execute(const MethodInfo* methodInfo, StackObject* args, void* ret);

	};

}
}

