#include "MetadataModule.h"

#include "os/Atomic.h"
#include "os/Mutex.h"
#include "os/File.h"
#include "vm/Exception.h"
#include "vm/String.h"
#include "vm/Assembly.h"
#include "vm/Class.h"
#include "vm/Object.h"
#include "vm/Image.h"
#include "vm/MetadataLock.h"
#include "utils/Logging.h"
#include "utils/MemoryMappedFile.h"
#include "utils/Memory.h"

#include "../interpreter/InterpreterModule.h"

#include "Assembly.h"
#include "InterpreterImage.h"
#include "ConsistentAOTHomologousImage.h"
#include "SuperSetAOTHomologousImage.h"
#include "MetadataPool.h"

using namespace il2cpp;

namespace hotc233
{

namespace metadata
{



    void MetadataModule::Initialize()
    {
        MetadataPool::Initialize();
        InterpreterImage::Initialize();
        Assembly::InitializePlaceHolderAssemblies();
    }

    Image* MetadataModule::GetUnderlyingInterpreterImage(const MethodInfo* methodInfo)
    {
        return metadata::IsInterpreterMethod(methodInfo) ? hotc233::metadata::MetadataModule::GetImage(methodInfo->klass)
            : (metadata::Image*)hotc233::metadata::AOTHomologousImage::FindImageByAssembly(
                methodInfo->klass->rank ? il2cpp_defaults.corlib->assembly : methodInfo->klass->image->assembly);
    }
}
}
