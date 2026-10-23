set(HKSR_CONTENT_SOURCES

    # src/assets
    src/assets/MaterialAssets.cpp
    src/assets/MeshAssets.cpp
    src/assets/ModelPayload.cpp

    # src/serialization
    src/serialization/AssetManifestCodec.cpp
    src/serialization/CharacterPackageCodec.cpp
)

set(HKSR_RUNTIME_SOURCES

    # src/appearance
    src/appearance/AppearanceInputs.cpp
    src/appearance/AppearanceModel.cpp
    src/appearance/ArtSlot.cpp
    src/appearance/ModelEpoch.cpp
    src/appearance/NpcAppearance.cpp
    src/appearance/NpcModels.cpp
    src/appearance/PrivateVisual.cpp

    # src/application
    src/application/ApplicationCoordinator.cpp
    src/application/CharacterTasks.cpp
    src/application/ApplicationEvents.cpp
    src/application/ApplicationService.cpp
    src/application/OperationHistory.cpp

    # src/assets
    src/assets/MaterialObjects.cpp
    src/assets/MeshObjects.cpp
    src/assets/ModelAssets.cpp
    src/assets/ModelTargets.cpp
    src/assets/ReferenceArrays.cpp
    src/assets/RendererBindings.cpp
    src/assets/TextureObjects.cpp

    # src/catalog
    src/catalog/ActionCatalog.cpp
    src/catalog/VisualActionCatalog.cpp
    src/catalog/CatalogBrowser.cpp
    src/catalog/OfficialCharacterFamilies.cpp
    src/catalog/OfficialSourcePreferences.cpp
    src/catalog/SourceCatalog.cpp

    # src/character
    src/character/CharacterControls.cpp
    src/character/CharacterInstance.cpp
    src/character/CharacterSession.cpp
    src/character/SceneCharacters.cpp
    src/character/SceneIdentity.cpp

    # src/host
    src/host/FrameworkHost.cpp
    src/host/HostInspection.cpp
    src/host/HostPublication.cpp
    src/host/RuntimeServices.cpp

    # src/layout
    src/layout/LayoutController.cpp
    src/layout/LayoutIo.cpp
    src/layout/LayoutStorage.cpp
    src/layout/SceneLayout.cpp

    # src/presentation
    src/presentation/ActionEffects.cpp
    src/presentation/ActionPlayback.cpp
    src/presentation/AnimationRequests.cpp
    src/presentation/ActionVisualProgram.cpp
    src/presentation/ModelActionHandoff.cpp
    src/presentation/PresentationProfile.cpp
    src/presentation/ReadyActions.cpp
    src/presentation/ReadyPlayback.cpp
    src/presentation/VisualPlayback.cpp
    src/presentation/VisualEffectContext.cpp
    src/presentation/VisualEntityMotion.cpp
    src/presentation/VisualEffectBindings.cpp
    src/presentation/VisualEventRoutes.cpp
    src/presentation/VisualProgram.cpp
    src/presentation/VisualProgramContexts.cpp
    src/presentation/VisualInstructionScopes.cpp
    src/presentation/VisualProgramSnapshot.cpp
    src/presentation/VisualTimelines.cpp
    src/presentation/VisualPartners.cpp
    src/presentation/VisualProgramRuntime.cpp
    src/presentation/VisualMovement.cpp

    # src/tasks
    src/tasks/BattleControl.cpp
    src/tasks/TaskContext.cpp
    src/tasks/TaskSources.cpp
    src/tasks/TaskSequence.cpp
    src/tasks/TaskPresentation.cpp
    src/tasks/TaskCatalog.cpp
    src/tasks/TaskController.cpp
    src/tasks/TaskSelection.cpp

    # src/serialization
    src/serialization/PresentationProfileCodec.cpp
    src/serialization/VisualProgramCodec.cpp
    src/serialization/VisualProgramLibraryCodec.cpp
    src/serialization/TaskCatalogCodec.cpp
    src/serialization/TaskControlCodec.cpp
    src/serialization/BattleControlCodec.cpp
    src/serialization/SceneLayoutCodec.cpp
)

set(HKSR_UI_SOURCES

    # src/ui
    src/ui/Localization.cpp
    src/ui/Menu.cpp
    src/ui/MenuActions.cpp
    src/ui/MenuTaskInputs.cpp
    src/ui/MenuControls.cpp
    src/ui/MenuLayouts.cpp
    src/ui/MenuSource.cpp
    src/ui/Overlay.cpp
    src/ui/PresentHook.cpp
    src/ui/UiFonts.cpp
    src/ui/UiPreferences.cpp
)

set(HKSR_RUNTIME_TEST_SOURCES

    # tests/appearance
    tests/appearance/AppearanceInputTests.cpp
    tests/appearance/ModelProfileInputTests.cpp
    tests/appearance/PrivateVisualTests.cpp

    # tests/application
    tests/application/ApplicationCoordinatorTests.cpp
    tests/application/ApplicationEventsTests.cpp
    tests/application/ApplicationServiceTests.cpp
    tests/application/OperationHistoryTests.cpp

    # tests/assets
    tests/assets/MaterialObjectTests.cpp
    tests/assets/MeshObjectTests.cpp
    tests/assets/ModelAssetTests.cpp
    tests/assets/ModelTargetTests.cpp
    tests/assets/ReferenceArrayTests.cpp
    tests/assets/TextureObjectTests.cpp

    # tests/catalog
    tests/catalog/ActionCatalogTests.cpp
    tests/catalog/CatalogBrowserTests.cpp
    tests/catalog/SourceCatalogTests.cpp

    # tests/character
    tests/character/CharacterControlTests.cpp
    tests/character/SceneIdentityTests.cpp

    # tests/host
    tests/host/RuntimeTests.cpp

    # tests/layout
    tests/layout/LayoutControllerTests.cpp
    tests/layout/SceneLayoutTests.cpp

    # tests/presentation
    tests/presentation/ActionPlaybackTests.cpp
    tests/presentation/PresentationProfileTests.cpp
    tests/presentation/VisualEffectContextTests.cpp
    tests/presentation/VisualEntityMotionTests.cpp
    tests/presentation/VisualProgramObserverTests.cpp
    tests/presentation/VisualTimelinesTests.cpp
    tests/presentation/VisualPartnersTests.cpp
    tests/presentation/ReadyPlaybackTests.cpp
    tests/presentation/TimelinePolicyTests.cpp
    tests/presentation/VisualPlaybackTests.cpp

    # tests/tasks
    tests/tasks/BattleControlTests.cpp
    tests/tasks/TaskModelTests.cpp
    tests/tasks/TaskSequenceTests.cpp
    tests/tasks/TaskPresentationTests.cpp
    tests/tasks/TaskCatalogTests.cpp
)
