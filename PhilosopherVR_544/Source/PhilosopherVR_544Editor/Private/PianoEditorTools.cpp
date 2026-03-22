#include "PianoEditorTools.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/Platform.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "ToolMenus.h"
#include "Sound/SoundBase.h"
#include "UObject/NameTypes.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "FPianoEditorTools"

namespace PianoEditorTools
{
	static const FString PianoAudioFolder = TEXT("/Game/Audio/pianokeys/All_88_Key_Sounds_EDITED_(mp3)");
	static const FString PianoBlueprintFolder = TEXT("/Game/Blueprints/BP_PianoRelated");
	static const FString PianoManagerAssetPath = TEXT("/Game/Blueprints/BP_PianoRelated/BP_PianoManager.BP_PianoManager");
	static const FString PianoKeyPrefix = TEXT("BP_PianoKey_Child_");
	static const FString PianoManagerPrefix = TEXT("BP_PianoManager");
	static const FName PianoFolderPath(TEXT("Piano"));

	struct FSpawnSpacingInput
	{
		double X = 1.0;
		double Y = 0.0;
		double Z = 0.0;
	};

	template <typename T>
	static bool ExtractTrailingIndex(const FString& Name, T& OutValue)
	{
		int32 EndIndex = Name.Len() - 1;
		while (EndIndex >= 0 && FChar::IsDigit(Name[EndIndex]))
		{
			--EndIndex;
		}

		if (EndIndex >= Name.Len() - 1)
		{
			return false;
		}

		const FString Digits = Name.Mid(EndIndex + 1);
		if (Digits.IsEmpty())
		{
			return false;
		}

		LexFromString(OutValue, *Digits);
		return true;
	}

	static TArray<FAssetData> GetAssetsInFolder(const FString& FolderPath, UClass* ClassFilter)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(*FolderPath);
		Filter.bRecursivePaths = false;
		if (ClassFilter)
		{
			Filter.ClassPaths.Add(ClassFilter->GetClassPathName());
			Filter.bRecursiveClasses = true;
		}

		TArray<FAssetData> Assets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().GetAssets(Filter, Assets);
		return Assets;
	}

	static TArray<FAssetData> GetSortedAudioAssets()
	{
		TArray<FAssetData> Assets = GetAssetsInFolder(PianoAudioFolder, USoundBase::StaticClass());
		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			int32 AIndex = 0;
			int32 BIndex = 0;
			ExtractTrailingIndex(A.AssetName.ToString(), AIndex);
			ExtractTrailingIndex(B.AssetName.ToString(), BIndex);
			return AIndex < BIndex;
		});
		return Assets;
	}

	static TArray<FAssetData> GetSortedKeyBlueprintAssets()
	{
		TArray<FAssetData> Assets = GetAssetsInFolder(PianoBlueprintFolder, UBlueprint::StaticClass());
		Assets = Assets.FilterByPredicate([](const FAssetData& Asset)
		{
			return Asset.AssetName.ToString().StartsWith(PianoKeyPrefix);
		});

		Assets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			int32 AIndex = 0;
			int32 BIndex = 0;
			ExtractTrailingIndex(A.AssetName.ToString(), AIndex);
			ExtractTrailingIndex(B.AssetName.ToString(), BIndex);
			return AIndex < BIndex;
		});

		return Assets;
	}

	static FProperty* FindPropertyByNames(UStruct* Struct, std::initializer_list<const TCHAR*> CandidateNames)
	{
		for (const TCHAR* Candidate : CandidateNames)
		{
			if (FProperty* Property = Struct->FindPropertyByName(FName(Candidate)))
			{
				return Property;
			}
		}

		return nullptr;
	}

	static bool SetKeyIdProperty(UObject* TargetObject, int32 Index)
	{
		if (!TargetObject)
		{
			return false;
		}

		FProperty* Property = FindPropertyByNames(TargetObject->GetClass(), { TEXT("KeyID"), TEXT("KeyId") });
		if (!Property)
		{
			return false;
		}

		if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			IntProperty->SetPropertyValue_InContainer(TargetObject, Index);
			return true;
		}

		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			NameProperty->SetPropertyValue_InContainer(TargetObject, FName(*FString::FromInt(Index)));
			return true;
		}

		if (FStrProperty* StringProperty = CastField<FStrProperty>(Property))
		{
			StringProperty->SetPropertyValue_InContainer(TargetObject, FString::FromInt(Index));
			return true;
		}

		return false;
	}

	static bool SetSoundProperty(UObject* TargetObject, USoundBase* Sound)
	{
		if (!TargetObject)
		{
			return false;
		}

		FProperty* Property = FindPropertyByNames(TargetObject->GetClass(), { TEXT("NoteSound") });
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			if (Sound && !Sound->IsA(ObjectProperty->PropertyClass))
			{
				return false;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(TargetObject, Sound);
			return true;
		}

		return false;
	}

	static bool SetManagerReferenceProperty(AActor* KeyActor, AActor* ManagerActor)
	{
		if (!KeyActor || !ManagerActor)
		{
			return false;
		}

		FProperty* Property = FindPropertyByNames(KeyActor->GetClass(), { TEXT("PianoManagerRef"), TEXT("ManagerRef") });
		if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			if (!ManagerActor->IsA(ObjectProperty->PropertyClass))
			{
				return false;
			}

			ObjectProperty->SetObjectPropertyValue_InContainer(KeyActor, ManagerActor);
			return true;
		}

		return false;
	}

	static bool SetPianoKeysArrayProperty(AActor* ManagerActor, const TArray<AActor*>& KeyActors)
	{
		if (!ManagerActor)
		{
			return false;
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(FindPropertyByNames(ManagerActor->GetClass(), { TEXT("PianoKeys") }));
		if (!ArrayProperty)
		{
			return false;
		}

		FObjectProperty* InnerObjectProperty = CastField<FObjectProperty>(ArrayProperty->Inner);
		if (!InnerObjectProperty)
		{
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(ManagerActor));
		Helper.Resize(KeyActors.Num());

		for (int32 Index = 0; Index < KeyActors.Num(); ++Index)
		{
			UObject* Value = KeyActors[Index];
			if (Value && !Value->IsA(InnerObjectProperty->PropertyClass))
			{
				return false;
			}

			InnerObjectProperty->SetObjectPropertyValue(Helper.GetRawPtr(Index), Value);
		}

		return true;
	}

	static bool SetSongKeysArrayProperty(UObject* TargetObject)
	{
		if (!TargetObject)
		{
			return false;
		}

		FArrayProperty* ArrayProperty = CastField<FArrayProperty>(FindPropertyByNames(TargetObject->GetClass(), { TEXT("SongKeys") }));
		if (!ArrayProperty)
		{
			return false;
		}

		FNameProperty* InnerNameProperty = CastField<FNameProperty>(ArrayProperty->Inner);
		if (!InnerNameProperty)
		{
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(TargetObject));
		Helper.Resize(88);

		for (int32 Index = 0; Index < 88; ++Index)
		{
			InnerNameProperty->SetPropertyValue(Helper.GetRawPtr(Index), FName(*FString::FromInt(Index)));
		}

		return true;
	}

	static bool SaveAssetPackage(UObject* Asset)
	{
		if (!Asset)
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			return false;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;

		return UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);
	}

	static TArray<AActor*> GetEditorWorldActorsByClassPrefix(const FString& ClassPrefix)
	{
		TArray<AActor*> Results;
		if (!GEditor)
		{
			return Results;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return Results;
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !Actor->GetClass())
			{
				continue;
			}

			if (Actor->GetClass()->GetName().StartsWith(ClassPrefix))
			{
				Results.Add(Actor);
			}
		}

		return Results;
	}

	static TArray<AActor*> GetSortedSceneKeyActors()
	{
		TArray<AActor*> Actors = GetEditorWorldActorsByClassPrefix(PianoKeyPrefix);
		Actors.Sort([](const AActor& A, const AActor& B)
		{
			int32 AIndex = 0;
			int32 BIndex = 0;
			ExtractTrailingIndex(A.GetActorLabel(), AIndex);
			ExtractTrailingIndex(B.GetActorLabel(), BIndex);
			return AIndex < BIndex;
		});
		return Actors;
	}

	static AActor* GetSingleSceneManagerActor()
	{
		TArray<AActor*> Managers = GetEditorWorldActorsByClassPrefix(PianoManagerPrefix);
		return Managers.Num() == 1 ? Managers[0] : nullptr;
	}

	static bool SpawnKeyBlueprintActor(UBlueprint* Blueprint, const int32 Index, const FVector& Location, AActor*& OutActor)
	{
		OutActor = nullptr;

		if (!Blueprint || !Blueprint->GeneratedClass || !GEditor)
		{
			return false;
		}

		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return false;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedActor = World->SpawnActor<AActor>(
			Blueprint->GeneratedClass,
			FTransform(FRotator::ZeroRotator, Location),
			SpawnParameters);

		if (!SpawnedActor)
		{
			return false;
		}

		SpawnedActor->SetActorLabel(FString::Printf(TEXT("%s%02d"), *PianoKeyPrefix, Index));
		SpawnedActor->SetFolderPath(PianoFolderPath);
		SpawnedActor->MarkPackageDirty();
		OutActor = SpawnedActor;
		return true;
	}

	static void ShowInfoDialog(const FText& Message)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Message);
	}

	static bool ShowSpawnSpacingDialog(FSpawnSpacingInput& InOutSpacing)
	{
		bool bAccepted = false;

		TSharedPtr<SEditableTextBox> XTextBox;
		TSharedPtr<SEditableTextBox> YTextBox;
		TSharedPtr<SEditableTextBox> ZTextBox;

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(LOCTEXT("SpawnSpacingDialogTitle", "Piano Key Spacing"))
			.ClientSize(FVector2D(360.0f, 170.0f))
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			.SizingRule(ESizingRule::FixedSize);

		auto TryAccept = [&]()
		{
			double ParsedX = InOutSpacing.X;
			double ParsedY = InOutSpacing.Y;
			double ParsedZ = InOutSpacing.Z;

			const bool bValidX = XTextBox.IsValid() && LexTryParseString(ParsedX, *XTextBox->GetText().ToString());
			const bool bValidY = YTextBox.IsValid() && LexTryParseString(ParsedY, *YTextBox->GetText().ToString());
			const bool bValidZ = ZTextBox.IsValid() && LexTryParseString(ParsedZ, *ZTextBox->GetText().ToString());
			if (!bValidX || !bValidY || !bValidZ)
			{
				ShowInfoDialog(LOCTEXT("SpawnSpacingInvalidInput", "Please enter valid numeric values for X, Y, and Z spacing."));
				return;
			}

			InOutSpacing.X = ParsedX;
			InOutSpacing.Y = ParsedY;
			InOutSpacing.Z = ParsedZ;
			bAccepted = true;
			Window->RequestDestroyWindow();
		};

		Window->SetContent(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.0f, 12.0f, 12.0f, 6.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SpawnSpacingDialogHelp", "Enter the per-key spacing to use when spawning BP_PianoKey_Child_00-87."))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(12.0f, 6.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(8.0f, 4.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpawnSpacingXLabel", "X Spacing"))
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SAssignNew(XTextBox, SEditableTextBox)
					.Text(FText::AsNumber(InOutSpacing.X))
				]
				+ SUniformGridPanel::Slot(0, 1)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpawnSpacingYLabel", "Y Spacing"))
				]
				+ SUniformGridPanel::Slot(1, 1)
				[
					SAssignNew(YTextBox, SEditableTextBox)
					.Text(FText::AsNumber(InOutSpacing.Y))
				]
				+ SUniformGridPanel::Slot(0, 2)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SpawnSpacingZLabel", "Z Spacing"))
				]
				+ SUniformGridPanel::Slot(1, 2)
				[
					SAssignNew(ZTextBox, SEditableTextBox)
					.Text(FText::AsNumber(InOutSpacing.Z))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(12.0f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(8.0f, 0.0f))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SpawnSpacingCancel", "Cancel"))
					.OnClicked_Lambda([&]() -> FReply
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SpawnSpacingOk", "Spawn"))
					.OnClicked_Lambda([&]() -> FReply
					{
						TryAccept();
						return FReply::Handled();
					})
				]
			]);

		FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr));
		return bAccepted;
	}
}

void FPianoEditorTools::RegisterMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	UToolMenu* ToolsMenu = ToolMenus->ExtendMenu("LevelEditor.MainMenu.Tools");
	FToolMenuSection& Section = ToolsMenu->FindOrAddSection("PianoTools");

	Section.AddSubMenu(
		"PianoToolsSubMenu",
		LOCTEXT("PianoToolsSubMenu", "Piano"),
		LOCTEXT("PianoToolsSubMenuTooltip", "Batch tools for piano keys, sounds, and scene references."),
		FNewToolMenuDelegate::CreateLambda([](UToolMenu* SubMenu)
		{
			FToolMenuSection& PianoSection = SubMenu->AddSection("PianoToolsCommands", LOCTEXT("PianoToolsHeader", "Piano Tools"));
			PianoSection.AddMenuEntry(
				"RunAllPianoTools",
				LOCTEXT("RunAllPianoTools", "Run All Piano Tools"),
				LOCTEXT("RunAllPianoToolsTooltip", "Assign key assets, wire scene references, and fill SongKeys."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FPianoEditorTools::RunAllPianoTools)));

			PianoSection.AddMenuEntry(
				"AssignPianoKeyAssets",
				LOCTEXT("AssignPianoKeyAssets", "Assign Piano Key Assets"),
				LOCTEXT("AssignPianoKeyAssetsTooltip", "Write KeyID and NoteSound to BP_PianoKey_Child_00-87."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FPianoEditorTools::RunAssignPianoKeyAssets)));

			PianoSection.AddMenuEntry(
				"SpawnPianoKeysInLevel",
				LOCTEXT("SpawnPianoKeysInLevel", "Spawn Piano Keys In Level"),
				LOCTEXT("SpawnPianoKeysInLevelTooltip", "Spawn BP_PianoKey_Child_00-87 into the current level after entering the desired X, Y, and Z spacing."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FPianoEditorTools::RunSpawnPianoKeysInLevel)));

			PianoSection.AddMenuEntry(
				"WirePianoSceneReferences",
				LOCTEXT("WirePianoSceneReferences", "Wire Piano Scene References"),
				LOCTEXT("WirePianoSceneReferencesTooltip", "Assign PianoManagerRef on scene keys and fill the manager's PianoKeys array."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FPianoEditorTools::RunWirePianoSceneReferences)));

			PianoSection.AddMenuEntry(
				"FillPianoManagerSongKeys",
				LOCTEXT("FillPianoManagerSongKeys", "Fill Piano Manager SongKeys"),
				LOCTEXT("FillPianoManagerSongKeysTooltip", "Fill SongKeys with names 0-87 on the BP_PianoManager asset and scene instance."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FPianoEditorTools::RunFillPianoManagerSongKeys)));
		}));
}

void FPianoEditorTools::UnregisterMenus()
{
}

void FPianoEditorTools::RunAssignPianoKeyAssets()
{
	using namespace PianoEditorTools;

	const TArray<FAssetData> AudioAssets = GetSortedAudioAssets();
	const TArray<FAssetData> KeyBlueprintAssets = GetSortedKeyBlueprintAssets();

	if (AudioAssets.Num() != 88 || KeyBlueprintAssets.Num() != 88)
	{
		ShowInfoDialog(FText::Format(
			LOCTEXT("AssignAssetsCountMismatch", "Expected 88 audio assets and 88 key blueprints.\nFound audio: {0}\nFound key blueprints: {1}"),
			FText::AsNumber(AudioAssets.Num()),
			FText::AsNumber(KeyBlueprintAssets.Num())));
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignPianoKeyAssetsTransaction", "Assign Piano Key Assets"));

	int32 UpdatedCount = 0;
	for (int32 Index = 0; Index < 88; ++Index)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(KeyBlueprintAssets[Index].GetAsset());
		USoundBase* Sound = Cast<USoundBase>(AudioAssets[Index].GetAsset());
		if (!Blueprint || !Blueprint->GeneratedClass || !Sound)
		{
			continue;
		}

		UObject* DefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		if (!DefaultObject)
		{
			continue;
		}

		Blueprint->Modify();
		DefaultObject->Modify();

		const bool bSetKeyId = SetKeyIdProperty(DefaultObject, Index);
		const bool bSetSound = SetSoundProperty(DefaultObject, Sound);
		if (!bSetKeyId || !bSetSound)
		{
			continue;
		}

		DefaultObject->PostEditChange();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		SaveAssetPackage(Blueprint);
		++UpdatedCount;
	}

	ShowInfoDialog(FText::Format(
		LOCTEXT("AssignAssetsDone", "Updated {0} piano key blueprint assets."),
		FText::AsNumber(UpdatedCount)));
}

void FPianoEditorTools::RunWirePianoSceneReferences()
{
	using namespace PianoEditorTools;

	AActor* ManagerActor = GetSingleSceneManagerActor();
	TArray<AActor*> KeyActors = GetSortedSceneKeyActors();

	if (!ManagerActor)
	{
		ShowInfoDialog(LOCTEXT("NoSingleManager", "Expected exactly one BP_PianoManager instance in the current editor world."));
		return;
	}

	if (KeyActors.Num() != 88)
	{
		ShowInfoDialog(FText::Format(
			LOCTEXT("SceneKeyCountMismatch", "Expected 88 piano key actors in the current editor world, but found {0}."),
			FText::AsNumber(KeyActors.Num())));
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("WirePianoSceneReferencesTransaction", "Wire Piano Scene References"));

	int32 WiredRefs = 0;
	for (AActor* KeyActor : KeyActors)
	{
		if (!KeyActor)
		{
			continue;
		}

		KeyActor->Modify();
		if (SetManagerReferenceProperty(KeyActor, ManagerActor))
		{
			KeyActor->PostEditChange();
			++WiredRefs;
		}
	}

	ManagerActor->Modify();
	const bool bSetArray = SetPianoKeysArrayProperty(ManagerActor, KeyActors);
	if (bSetArray)
	{
		ManagerActor->PostEditChange();
		ManagerActor->MarkPackageDirty();
	}

	if (UWorld* World = ManagerActor->GetWorld())
	{
		World->MarkPackageDirty();
	}

	ShowInfoDialog(FText::Format(
		LOCTEXT("WireSceneDone", "Assigned PianoManagerRef on {0} scene keys and {1} the manager's PianoKeys array."),
		FText::AsNumber(WiredRefs),
		bSetArray ? LOCTEXT("DidSetPianoKeys", "filled") : LOCTEXT("DidNotSetPianoKeys", "could not fill")));
}

void FPianoEditorTools::RunSpawnPianoKeysInLevel()
{
	using namespace PianoEditorTools;

	const TArray<FAssetData> KeyBlueprintAssets = GetSortedKeyBlueprintAssets();
	if (KeyBlueprintAssets.Num() != 88)
	{
		ShowInfoDialog(FText::Format(
			LOCTEXT("SpawnAssetsCountMismatch", "Expected 88 key blueprints, but found {0}."),
			FText::AsNumber(KeyBlueprintAssets.Num())));
		return;
	}

	if (GetSortedSceneKeyActors().Num() > 0)
	{
		ShowInfoDialog(LOCTEXT("SpawnKeysAlreadyExist", "Current level already contains piano key actors. Clear them first to avoid spawning duplicates."));
		return;
	}

	if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		ShowInfoDialog(LOCTEXT("SpawnNoEditorWorld", "Could not access the current editor world."));
		return;
	}

	FSpawnSpacingInput SpacingInput;
	if (!ShowSpawnSpacingDialog(SpacingInput))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SpawnPianoKeysTransaction", "Spawn Piano Keys In Level"));

	TArray<AActor*> SpawnedActors;
	SpawnedActors.Reserve(88);

	for (int32 Index = 0; Index < 88; ++Index)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(KeyBlueprintAssets[Index].GetAsset());
		AActor* SpawnedActor = nullptr;
		const FVector Location(
			static_cast<double>(Index) * SpacingInput.X,
			static_cast<double>(Index) * SpacingInput.Y,
			static_cast<double>(Index) * SpacingInput.Z);
		if (SpawnKeyBlueprintActor(Blueprint, Index, Location, SpawnedActor))
		{
			SpawnedActors.Add(SpawnedActor);
		}
	}

	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		World->MarkPackageDirty();
	}

	ShowInfoDialog(FText::Format(
		LOCTEXT("SpawnKeysDone", "Spawned {0} piano key actors into the current level in folder 'Piano'.\nSpacing: X={1}, Y={2}, Z={3}"),
		FText::AsNumber(SpawnedActors.Num()),
		FText::AsNumber(SpacingInput.X),
		FText::AsNumber(SpacingInput.Y),
		FText::AsNumber(SpacingInput.Z)));
}

void FPianoEditorTools::RunFillPianoManagerSongKeys()
{
	using namespace PianoEditorTools;

	UBlueprint* ManagerBlueprint = LoadObject<UBlueprint>(nullptr, *PianoManagerAssetPath);
	AActor* ManagerActor = GetSingleSceneManagerActor();

	const FScopedTransaction Transaction(LOCTEXT("FillSongKeysTransaction", "Fill Piano Manager SongKeys"));

	bool bUpdatedAsset = false;
	if (ManagerBlueprint && ManagerBlueprint->GeneratedClass)
	{
		UObject* DefaultObject = ManagerBlueprint->GeneratedClass->GetDefaultObject();
		if (DefaultObject)
		{
			ManagerBlueprint->Modify();
			DefaultObject->Modify();

			if (SetSongKeysArrayProperty(DefaultObject))
			{
				DefaultObject->PostEditChange();
				FBlueprintEditorUtils::MarkBlueprintAsModified(ManagerBlueprint);
				FKismetEditorUtilities::CompileBlueprint(ManagerBlueprint);
				SaveAssetPackage(ManagerBlueprint);
				bUpdatedAsset = true;
			}
		}
	}

	bool bUpdatedSceneInstance = false;
	if (ManagerActor)
	{
		ManagerActor->Modify();
		if (SetSongKeysArrayProperty(ManagerActor))
		{
			ManagerActor->PostEditChange();
			ManagerActor->MarkPackageDirty();
			if (UWorld* World = ManagerActor->GetWorld())
			{
				World->MarkPackageDirty();
			}
			bUpdatedSceneInstance = true;
		}
	}

	ShowInfoDialog(FText::Format(
		LOCTEXT("FillSongKeysDone", "BP_PianoManager asset SongKeys: {0}\nScene BP_PianoManager SongKeys: {1}"),
		bUpdatedAsset ? LOCTEXT("SongKeysUpdated", "updated") : LOCTEXT("SongKeysNotUpdated", "not updated"),
		bUpdatedSceneInstance ? LOCTEXT("SceneSongKeysUpdated", "updated") : LOCTEXT("SceneSongKeysNotUpdated", "not updated")));
}

void FPianoEditorTools::RunAllPianoTools()
{
	RunAssignPianoKeyAssets();
	RunSpawnPianoKeysInLevel();
	RunWirePianoSceneReferences();
	RunFillPianoManagerSongKeys();
}

#undef LOCTEXT_NAMESPACE
