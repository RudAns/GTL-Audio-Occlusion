#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

// Maps /OcclusionWave to the project's Shaders folder so the FDTD compute shaders can be found.
class FOcclusionWaveModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString ShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/OcclusionWave"), ShaderDir);
	}
};

IMPLEMENT_MODULE(FOcclusionWaveModule, OcclusionWave);
