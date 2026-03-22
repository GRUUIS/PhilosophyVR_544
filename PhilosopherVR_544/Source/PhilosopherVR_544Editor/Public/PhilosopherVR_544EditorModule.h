#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FPhilosopherVR_544EditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
};
