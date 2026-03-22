#pragma once

#include "CoreMinimal.h"

class FUICommandList;

class FPianoEditorTools
{
public:
	static void RegisterMenus();
	static void UnregisterMenus();

private:
	static void RunAssignPianoKeyAssets();
	static void RunSpawnPianoKeysInLevel();
	static void RunWirePianoSceneReferences();
	static void RunFillPianoManagerSongKeys();
	static void RunAllPianoTools();
};
