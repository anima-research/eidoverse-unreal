#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
class FUnderwaterOpticsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/UnderwaterOptics"),
            FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("UnderwaterOptics"))->GetBaseDir(), TEXT("Shaders")));
    }
};
IMPLEMENT_MODULE(FUnderwaterOpticsModule, UnderwaterOptics)
