// Copyright Doupi Design 2025. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/InputChord.h"
#include "Styling/AppStyle.h"

// Commands class for the plugin's UI integration
class FTeleportToMouseCommands : public TCommands<FTeleportToMouseCommands>
{
public:
    FTeleportToMouseCommands()
        : TCommands<FTeleportToMouseCommands>(
            TEXT("TeleportToMouse"),
            NSLOCTEXT("Contexts", "TeleportToMouse", "Teleport To Mouse Plugin"),
            NAME_None,
            FAppStyle::GetAppStyleSetName())
    {
    }

    // Register the plugin's commands
    virtual void RegisterCommands() override;

    // Command for the teleport action
    TSharedPtr<FUICommandInfo> TeleportCommand;
};

// Handler for spline-specific teleport operations
class FSplineTeleportHandler
{
public:
    // Teleports selected spline control points to a target location
    static bool TeleportSelectedSplinePoint(const FVector& Location, const class FTeleportToMouseModule* Module);
};

class FTeleportToMouseModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    void OnTeleportHotkeyPressed();

    // Made public so SplineTeleportHandler can access it
    FVector SnapLocationToGrid(const FVector& Location) const;

private:
    FVector GetTeleportLocation(const FLevelEditorViewportClient* ViewportClient, bool& bSuccess) const;
    void TeleportActors(const FVector& Location);
    TSharedPtr<FUICommandList> CommandList;
    TWeakObjectPtr<UWorld> CachedWorld;
    TWeakObjectPtr<ULevel> CachedLevel;
};