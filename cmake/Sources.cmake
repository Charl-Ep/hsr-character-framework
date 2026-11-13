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
    src/catalog/ActionRetirement.cpp
    src/catalog/ActionSourceStore.cpp
    src/serialization/ActionSourcesCodec.cpp
    src/serialization/ActorAudioCatalogCodec.cpp
    src/catalog/VisualActionCatalog.cpp
    src/catalog/ActorAudioCatalog.cpp
    src/catalog/CatalogBrowser.cpp
    src/catalog/CatalogLocalization.cpp
    src/catalog/OfficialCharacterFamilies.cpp
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

    # src/presentation/playback
    src/presentation/playback/ActionPlaybackEffects.cpp
    src/presentation/playback/ActionPlaybackVoices.cpp
    src/presentation/playback/ActionPlayback.cpp
    src/presentation/playback/TaskPlaybackBindings.cpp
    src/presentation/playback/ActionPlaybackLayers.cpp
    src/presentation/playback/AnimationRequests.cpp
    src/presentation/playback/AnimatorLayerWeights.cpp
    src/presentation/playback/ActionPlaybackHitEffects.cpp
    src/presentation/playback/ActionPlaybackHits.cpp
    src/presentation/playback/ExpressionPlayer.cpp
    src/presentation/playback/ActionPlaybackPoses.cpp
    src/presentation/playback/ActionPlaybackProgram.cpp
    src/presentation/playback/ReadyActions.cpp
    src/presentation/playback/ActionPlaybackReady.cpp
    src/presentation/playback/ActionPlaybackReaction.cpp

    # src/presentation/reactions
    src/presentation/reactions/HitRequests.cpp
    src/presentation/reactions/HitInbox.cpp
    src/presentation/reactions/ReactionPolicy.cpp
    src/presentation/reactions/HitEffectPolicy.cpp

    # src/presentation/effects
    src/presentation/effects/ActorEventEffects.cpp
    src/presentation/effects/VisualLayoutOperations.cpp
    src/presentation/effects/VisualEffectContext.cpp
    src/presentation/effects/VisualEffectBindings.cpp
    src/presentation/effects/VisualEventRoutes.cpp
    src/presentation/effects/SpatialHitEffects.cpp

    # src/presentation/scene
    src/presentation/scene/PresentationProfile.cpp
    src/presentation/scene/VisualProgramPlayer.cpp
    src/presentation/scene/VisualProgramPlayerContexts.cpp
    src/presentation/scene/VisualProgramPlayerInstructions.cpp
    src/presentation/scene/VisualProgramPlayerSnapshot.cpp
    src/presentation/scene/VisualProgramSnapshot.cpp
    src/presentation/scene/VisualProgramRuntime.cpp
    src/presentation/scene/VisualBodyResources.cpp
    src/presentation/scene/VisualSceneTransients.cpp
    src/presentation/scene/VisualTargets.cpp

    # src/presentation/model
    src/presentation/model/ActorModelScopes.cpp
    src/presentation/model/ActorRenderingScopes.cpp
    src/presentation/model/ModelActionHandoff.cpp

    # src/presentation/audio
    src/presentation/audio/ActorBgmScopes.cpp
    src/presentation/audio/ActorVoicePlayer.cpp

    # src/presentation/timing
    src/presentation/timing/ActorTimingScopes.cpp

    # src/presentation/movement
    src/presentation/movement/VisualEntityMotion.cpp
    src/presentation/movement/VisualMovement.cpp

    # src/presentation/partners
    src/presentation/partners/VisualPartners.cpp

    # src/presentation/projectiles
    src/presentation/projectiles/VisualProjectiles.cpp

    # src/presentation/timeline
    src/presentation/timeline/VisualTimelines.cpp

    # src/tasks
    src/tasks/ContextNumbers.cpp
    src/tasks/TaskContext.cpp
    src/tasks/TaskContextRegistry.cpp
    src/tasks/TaskEvents.cpp
    src/tasks/TaskSources.cpp
    src/tasks/TaskSequence.cpp
    src/tasks/TaskPresentation.cpp
    src/tasks/TaskCatalog.cpp
    src/tasks/TaskController.cpp
    src/tasks/TaskLifetimes.cpp
    src/tasks/TaskSelection.cpp

    # src/serialization
    src/serialization/PresentationProfileCodec.cpp
    src/serialization/VisualBgmCodec.cpp
    src/serialization/VisualLayoutCodec.cpp
    src/serialization/VisualProgramCodec.cpp
    src/serialization/VisualProgramLibraryCodec.cpp
    src/serialization/TaskCatalogCodec.cpp
    src/serialization/ScriptComposition.cpp
    src/serialization/AnimationParameterCodec.cpp
    src/serialization/VisualModelCodec.cpp
    src/serialization/TaskControlCodec.cpp
    src/serialization/HitEffectsCodec.cpp
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
    tests/application/CharacterTaskDirectoryTests.cpp
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
    tests/catalog/ActionSourceStoreTests.cpp
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
    tests/presentation/playback/ActionPlaybackTests.cpp
    tests/presentation/reactions/HitRequestTests.cpp
    tests/presentation/playback/AnimatorLayerWeightsTests.cpp
    tests/presentation/reactions/ReactionPolicyTests.cpp
    tests/presentation/reactions/HitEffectPolicyTests.cpp
    tests/presentation/scene/PresentationProfileTests.cpp
    tests/presentation/effects/VisualEffectContextTests.cpp
    tests/presentation/scene/VisualTargetTests.cpp
    tests/presentation/movement/VisualEntityMotionTests.cpp
    tests/presentation/projectiles/VisualProjectileTests.cpp
    tests/presentation/scene/VisualProgramObserverTests.cpp
    tests/presentation/timeline/VisualTimelinesTests.cpp
    tests/presentation/partners/VisualPartnersTests.cpp
    tests/presentation/playback/ReadyPlaybackTests.cpp
    tests/presentation/timeline/TimelinePolicyTests.cpp
    tests/presentation/playback/VisualPlaybackTests.cpp

    # tests/tasks
    tests/tasks/TaskModelTests.cpp
    tests/tasks/TaskSequenceTests.cpp
    tests/tasks/TaskPresentationTests.cpp
    tests/tasks/TaskCatalogTests.cpp
)
