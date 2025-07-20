// Copyright Doupi Design 2025. All Rights Reserved.

#include "TeleportToMouse.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandInfo.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "SceneView.h"
#include "LevelEditor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "LevelEditorViewport.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Editor/EditorEngine.h" 
#include "Engine/Selection.h"
#include "Editor/GroupActor.h"
#include "ComponentVisualizer.h"
#include "Components/SplineComponent.h"
#include "SplineComponentVisualizer.h"
#include "LevelEditorActions.h"

#define LOCTEXT_NAMESPACE "TeleportToMouse"

// Register the plugin commands and set up Ctrl+T as the hotkey
void FTeleportToMouseCommands::RegisterCommands()
{
    UI_COMMAND(TeleportCommand, "Teleport to Mouse", "Teleports selected objects to mouse position", EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::T));
}

// Handle teleporting of selected spline points while maintaining their relative positions
bool FSplineTeleportHandler::TeleportSelectedSplinePoint(const FVector& Location, const FTeleportToMouseModule* Module)
{
    // Early exit if no editor or selected components
    if (!GEditor || !GEditor->GetSelectedComponents() || !Module) return false;

    // Get snapped location
    const FVector SnappedLocation = Module->SnapLocationToGrid(Location);

    // Get the active viewport client
    FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
    if (!ViewportClient) return false;

    // Iterate through selected components looking for splines
    for (FSelectedEditableComponentIterator It(*GEditor->GetSelectedComponents()); It; ++It)
    {
        if (USplineComponent* SplineComp = Cast<USplineComponent>(*It))
        {
            // Get the spline visualizer
            TSharedPtr<FComponentVisualizer> Visualizer = GUnrealEd->FindComponentVisualizer(SplineComp->GetClass());
            if (auto* SplineVisualizer = static_cast<FSplineComponentVisualizer*>(Visualizer.Get()))
            {
                const TSet<int32>& SelectedKeys = SplineVisualizer->GetSelectedKeys();
                if (SelectedKeys.Num() > 0)
                {
                    // Start undo/redo transaction
                    const FScopedTransaction Transaction(LOCTEXT("TeleportSplinePoint", "Move Spline Points"));
                    SplineComp->Modify();

                    // Use the current widget pivot as our reference point
                    FVector ReferenceLocation = ViewportClient->GetWidgetLocation();

                    // Calculate offset from reference to target
                    FVector MoveOffset = SnappedLocation - ReferenceLocation;

                    // Move all selected points by the offset
                    for (int32 Key : SelectedKeys)
                    {
                        FVector CurrentLocation = SplineComp->GetLocationAtSplinePoint(Key, ESplineCoordinateSpace::World);
                        SplineComp->SetLocationAtSplinePoint(Key, CurrentLocation + MoveOffset, ESplineCoordinateSpace::World);
                    }

                    SplineComp->UpdateSpline();
                    return true;
                }
            }
        }
    }
    return false;
}

// Initialize the plugin module
void FTeleportToMouseModule::StartupModule()
{
    // Create and register commands
    CommandList = MakeShareable(new FUICommandList);
    FTeleportToMouseCommands::Register();

    // Cache world and level references
    if (GEditor)
    {
        CachedWorld = GEditor->GetEditorWorldContext().World();
        if (CachedWorld.IsValid())
        {
            CachedLevel = CachedWorld->GetCurrentLevel();
        }
    }

    // Map the teleport command to our handler
    CommandList->MapAction(
        FTeleportToMouseCommands::Get().TeleportCommand,
        FExecuteAction::CreateRaw(this, &FTeleportToMouseModule::OnTeleportHotkeyPressed),
        FCanExecuteAction());

    // Add our commands to the editor's action list
    FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
    LevelEditorModule.GetGlobalLevelEditorActions()->Append(CommandList.ToSharedRef());
}

// Get the world location under the mouse cursor using ray tracing
FVector FTeleportToMouseModule::GetTeleportLocation(const FLevelEditorViewportClient* ViewportClient, bool& bOutSuccess) const
{
    bOutSuccess = false;

    if (ViewportClient == nullptr || ViewportClient->Viewport == nullptr)
    {
        return FVector::ZeroVector;
    }

    FIntPoint MousePos;
    ViewportClient->Viewport->GetMousePos(MousePos);

    FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
        ViewportClient->Viewport,
        ViewportClient->GetScene(),
        FEngineShowFlags(ESFIM_Game))
        .SetRealtimeUpdate(true));

    FLevelEditorViewportClient* NonConstClient = const_cast<FLevelEditorViewportClient*>(ViewportClient);
    FSceneView* View = NonConstClient->CalcSceneView(&ViewFamily);

    if (View == nullptr)
    {
        return FVector::ZeroVector;
    }

    FVector2D ScreenPos(MousePos.X, MousePos.Y);
    FVector RayOrigin;
    FVector RayDirection;

    View->DeprojectFVector2D(ScreenPos, RayOrigin, RayDirection);

    // Set up collision query params
    FCollisionQueryParams QueryParams;
    QueryParams.bTraceComplex = true;

    // Add all selected actors to ignored actors
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
    {
        if (AActor* Actor = Cast<AActor>(*It))
        {
            QueryParams.AddIgnoredActor(Actor);

            // Also ignore actors in the same group if it's a grouped actor
            if (AGroupActor* GroupActor = AGroupActor::GetRootForActor(Actor))
            {
                TArray<AActor*> GroupedActors;
                GroupActor->GetGroupActors(GroupedActors);
                for (AActor* GroupedActor : GroupedActors)
                {
                    QueryParams.AddIgnoredActor(GroupedActor);
                }
            }
        }
    }

    const UWorld* World = GEditor->GetEditorWorldContext().World();
    if (World == nullptr)
    {
        return FVector::ZeroVector;
    }

    FHitResult HitResult;
    bool bHitSuccess = World->LineTraceSingleByChannel(
        HitResult,
        RayOrigin,
        RayOrigin + RayDirection * 100000.0f,
        ECC_Visibility,
        QueryParams);

    if (bHitSuccess)
    {
        bOutSuccess = true;
        return HitResult.Location;
    }

    return FVector::ZeroVector;
}

// Snap a location to the grid if grid snapping is enabled
FVector FTeleportToMouseModule::SnapLocationToGrid(const FVector& Location) const
{
    // Check if grid snap is enabled and editor is valid
    if (!FLevelEditorActionCallbacks::LocationGridSnap_IsChecked() || !GEditor)
    {
        return Location;
    }

    // Get current grid size
    const float GridSize = GEditor->GetGridSize();
    if (GridSize <= 0.0f)
    {
        return Location;
    }

    // Snap each component to the grid
    return FVector(
        FMath::RoundToFloat(Location.X / GridSize) * GridSize,
        FMath::RoundToFloat(Location.Y / GridSize) * GridSize,
        FMath::RoundToFloat(Location.Z / GridSize) * GridSize
    );
}

// Main function to teleport selected actors to a target location
void FTeleportToMouseModule::TeleportActors(const FVector& Location)
{
    TSet<AActor*> ActorsToMove;
    TSet<AGroupActor*> LockedGroups;
    TSet<AGroupActor*> UnlockedGroups;

    // Collect selected actors and their groups
    for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor) continue;

        // Handle grouped actors
        AGroupActor* GroupActor = AGroupActor::GetRootForActor(Actor);
        if (GroupActor)
        {
            if (GroupActor->IsLocked())
            {
                LockedGroups.Add(GroupActor);
                TArray<AActor*> GroupedActors;
                GroupActor->GetGroupActors(GroupedActors);
                ActorsToMove.Append(GroupedActors);
            }
            else
            {
                UnlockedGroups.Add(GroupActor);
                ActorsToMove.Add(Actor);
            }
        }
        else
        {
            ActorsToMove.Add(Actor);
        }
    }

    if (ActorsToMove.Num() == 0) return;

    // Start undo/redo transaction
    const FScopedTransaction Transaction(LOCTEXT("TeleportObjects", "Teleport Objects to Mouse"));

    // Apply grid snapping if enabled
    const FVector SnappedLocation = SnapLocationToGrid(Location);

    // Get the pivot point of the entire selection
    FVector ReferenceLocation = GEditor->GetPivotLocation();

    // Calculate relative offsets to maintain positioning between actors
    TArray<FVector> RelativeOffsets;
    RelativeOffsets.Reserve(ActorsToMove.Num());

    for (AActor* Actor : ActorsToMove)
    {
        RelativeOffsets.Add(Actor->GetActorLocation() - ReferenceLocation);
    }

    // Calculate locked group actor offsets
    TArray<FVector> GroupOffsets;
    GroupOffsets.Reserve(LockedGroups.Num());
    for (AGroupActor* GroupActor : LockedGroups)
    {
        GroupOffsets.Add(GroupActor->GetActorLocation() - ReferenceLocation);
    }

    // Prepare all group actors for modification
    for (AGroupActor* GroupActor : LockedGroups)
    {
        GroupActor->Modify();
    }
    for (AGroupActor* GroupActor : UnlockedGroups)
    {
        GroupActor->Modify();
    }

    // Move all actors while maintaining relative positions
    int32 Index = 0;
    for (AActor* Actor : ActorsToMove)
    {
        Actor->Modify();
        Actor->SetActorLocation(SnappedLocation + RelativeOffsets[Index++]);
        Actor->PostEditMove(true);
        Actor->MarkPackageDirty();
    }

    // Update locked group actors
    Index = 0;
    for (AGroupActor* GroupActor : LockedGroups)
    {
        GroupActor->SetActorLocation(SnappedLocation + GroupOffsets[Index++]);
        GroupActor->MarkPackageDirty();
    }

    // Update unlocked group actors
    for (AGroupActor* GroupActor : UnlockedGroups)
    {
        // Recalculate group location based on its members' new positions
        TArray<AActor*> GroupedActors;
        GroupActor->GetGroupActors(GroupedActors);

        if (GroupedActors.Num() > 0)
        {
            // Calculate the center point of all grouped actors
            FVector CenterLocation = FVector::ZeroVector;
            for (AActor* GroupedActor : GroupedActors)
            {
                CenterLocation += GroupedActor->GetActorLocation();
            }
            CenterLocation /= GroupedActors.Num();

            GroupActor->SetActorLocation(CenterLocation);
            GroupActor->MarkPackageDirty();
        }
    }

    // Update editor pivot location
    if (GUnrealEd)
    {
        GUnrealEd->UpdatePivotLocationForSelection();
    }
}

// Handle the teleport hotkey press
void FTeleportToMouseModule::OnTeleportHotkeyPressed()
{
    if (!GEditor || !GEditor->GetActiveViewport()) return;

    // Get location under mouse cursor
    bool bSuccess = false;
    FVector Location = GetTeleportLocation(
        static_cast<FLevelEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()),
        bSuccess
    );

    if (!bSuccess) return;

    // Try to teleport spline points first, if none selected, teleport actors
    if (FSplineTeleportHandler::TeleportSelectedSplinePoint(Location, this))
    {
        GEditor->RedrawLevelEditingViewports();
        return;
    }

    TeleportActors(Location);

    // Update viewport display
    if (FLevelEditorViewportClient* ViewportClient = static_cast<FLevelEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient()))
    {
        ViewportClient->Invalidate();
        GEditor->RedrawLevelEditingViewports();
        GEditor->NoteSelectionChange();
    }
}

// Clean up when module is shut down
void FTeleportToMouseModule::ShutdownModule()
{
    FTeleportToMouseCommands::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTeleportToMouseModule, TeleportToMouse)