#include "PhilosopherVR_544EditorModule.h"

#include "Modules/ModuleManager.h"
#include "PianoEditorTools.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FPhilosopherVR_544EditorModule"

void FPhilosopherVR_544EditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FPhilosopherVR_544EditorModule::RegisterMenus));
}

void FPhilosopherVR_544EditorModule::ShutdownModule()
{
	FPianoEditorTools::UnregisterMenus();
	UToolMenus::UnRegisterStartupCallback(this);
}

void FPhilosopherVR_544EditorModule::RegisterMenus()
{
	FPianoEditorTools::RegisterMenus();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FPhilosopherVR_544EditorModule, PhilosopherVR_544Editor)
