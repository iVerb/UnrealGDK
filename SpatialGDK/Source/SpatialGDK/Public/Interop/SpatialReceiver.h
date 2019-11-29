// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#pragma once

#include "CoreMinimal.h"

#include "EngineClasses/SpatialActorChannel.h"
#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialPackageMapClient.h"
#include "Interop/SpatialClassInfoManager.h"
#include "Schema/DynamicComponent.h"
#include "Schema/RPCPayload.h"
#include "Schema/SpawnData.h"
#include "Schema/StandardLibrary.h"
#include "Schema/UnrealObjectRef.h"
#include "SpatialCommonTypes.h"
#include "Utils/RPCContainer.h"

#include <WorkerSDK/improbable/c_schema.h>
#include <WorkerSDK/improbable/c_worker.h>

#include "SpatialReceiver.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSpatialReceiver, Log, All);

class USpatialNetConnection;
class USpatialSender;
class UGlobalStateManager;
class SpatialLoadBalanceEnforcer;

struct PendingAddComponentWrapper
{
	PendingAddComponentWrapper() = default;
	PendingAddComponentWrapper(Worker_EntityId InEntityId, Worker_ComponentId InComponentId, TUniquePtr<SpatialGDK::DynamicComponent>&& InData)
		: EntityId(InEntityId), ComponentId(InComponentId), Data(MoveTemp(InData)) {}

	Worker_EntityId EntityId;
	Worker_ComponentId ComponentId;
	TUniquePtr<SpatialGDK::DynamicComponent> Data;
};

DECLARE_DELEGATE_OneParam(EntityQueryDelegate, const Worker_EntityQueryResponseOp&);
DECLARE_DELEGATE_OneParam(ReserveEntityIDsDelegate, const Worker_ReserveEntityIdsResponseOp&);
DECLARE_DELEGATE_OneParam(CreateEntityDelegate, const Worker_CreateEntityResponseOp&);

DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityAddedDelegate, const Worker_EntityId);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityRemovedDelegate, const Worker_EntityId);

UCLASS()
class USpatialReceiver : public UObject
{
	GENERATED_BODY()

public:
	void Init(USpatialNetDriver* NetDriver, FTimerManager* InTimerManager);

	// Dispatcher Calls
	void OnCriticalSection(bool InCriticalSection);
	void OnAddEntity(const Worker_AddEntityOp& Op);
	void OnAddComponent(const Worker_AddComponentOp& Op);
	void OnRemoveEntity(const Worker_RemoveEntityOp& Op);
	void OnRemoveComponent(const Worker_RemoveComponentOp& Op);
	void FlushRemoveComponentOps();
	void RemoveComponentOpsForEntity(Worker_EntityId EntityId);
	void OnAuthorityChange(const Worker_AuthorityChangeOp& Op);

	void OnComponentUpdate(const Worker_ComponentUpdateOp& Op);
	void HandleRPC(const Worker_ComponentUpdateOp& Op);

	void ProcessRPCEventField(Worker_EntityId EntityId, const Worker_ComponentUpdateOp &Op, const Worker_ComponentId RPCEndpointComponentId, bool bPacked);

	void OnCommandRequest(const Worker_CommandRequestOp& Op);
	void OnCommandResponse(const Worker_CommandResponseOp& Op);

	void OnReserveEntityIdsResponse(const Worker_ReserveEntityIdsResponseOp& Op);
	void OnCreateEntityResponse(const Worker_CreateEntityResponseOp& Op);

	void AddPendingActorRequest(Worker_RequestId RequestId, USpatialActorChannel* Channel);
	void AddPendingReliableRPC(Worker_RequestId RequestId, TSharedRef<struct FReliableRPCForRetry> ReliableRPC);

	void AddEntityQueryDelegate(Worker_RequestId RequestId, EntityQueryDelegate Delegate);
	void AddReserveEntityIdsDelegate(Worker_RequestId RequestId, ReserveEntityIDsDelegate Delegate);
	void AddCreateEntityDelegate(Worker_RequestId RequestId, const CreateEntityDelegate& Delegate);

	void OnEntityQueryResponse(const Worker_EntityQueryResponseOp& Op);

	void ResolvePendingOperations(UObject* Object, const FUnrealObjectRef& ObjectRef);
	void FlushRetryRPCs();

	void OnDisconnect(Worker_DisconnectOp& Op);

	void RemoveActor(Worker_EntityId EntityId);
	bool IsPendingOpsOnChannel(USpatialActorChannel& Channel);

	void ClearPendingRPCs(Worker_EntityId EntityId);

	void CleanupRepStateMap(FSpatialObjectRepState& Replicator);
	void CleanupIncomingRefMap(FSpatialObjectRepState& Replicator, FChannelObjectPair);
	void MoveMappedObjectToUnmapped(FUnrealObjectRef&);

private:

	void CheckRepStateMapInvariants();
	void CheckRepStateMapInvariants(FSpatialObjectRepState& Replicator, FChannelObjectPair Object);

	void EnterCriticalSection();
	void LeaveCriticalSection();

	void ReceiveActor(Worker_EntityId EntityId);
	void DestroyActor(AActor* Actor, Worker_EntityId EntityId);

	AActor* TryGetOrCreateActor(SpatialGDK::UnrealMetadata* UnrealMetadata, SpatialGDK::SpawnData* SpawnData);
	AActor* CreateActor(SpatialGDK::UnrealMetadata* UnrealMetadata, SpatialGDK::SpawnData* SpawnData);

	USpatialActorChannel* RecreateDormantSpatialChannel(AActor* Actor, Worker_EntityId EntityID);
	void ProcessRemoveComponent(const Worker_RemoveComponentOp& Op);

	static FTransform GetRelativeSpawnTransform(UClass* ActorClass, FTransform SpawnTransform);

	void HandlePlayerLifecycleAuthority(const Worker_AuthorityChangeOp& Op, class APlayerController* PlayerController);
	void HandleActorAuthority(const Worker_AuthorityChangeOp& Op);

	void ApplyComponentDataOnActorCreation(Worker_EntityId EntityId, const Worker_ComponentData& Data, USpatialActorChannel& Channel, const FClassInfo& ActorClassInfo);
	void ApplyComponentData(USpatialActorChannel& Channel, UObject& TargetObject, const Worker_ComponentData& Data);
	// This is called for AddComponentOps not in a critical section, which means they are not a part of the initial entity creation.
	void HandleIndividualAddComponent(const Worker_AddComponentOp& Op);
	void AttachDynamicSubobject(AActor* Actor, Worker_EntityId EntityId, const FClassInfo& Info);

	void ApplyComponentUpdate(const Worker_ComponentUpdate& ComponentUpdate, UObject& TargetObject, USpatialActorChannel& Channel, bool bIsHandover);

	FRPCErrorInfo ApplyRPC(const FPendingRPCParams& Params);
	ERPCResult ApplyRPCInternal(UObject* TargetObject, UFunction* Function, const SpatialGDK::RPCPayload& Payload, const FString& SenderWorkerId, bool bApplyWithUnresolvedRefs = false);

	void ReceiveCommandResponse(const Worker_CommandResponseOp& Op);

	bool IsReceivedEntityTornOff(Worker_EntityId EntityId);

	void UpdateSpatialObjectRepState(USpatialActorChannel& Channel, FObjectReferencesMap&& TempRefMap, UObject& ObjectPtr, const TSet<FUnrealObjectRef>& UnresolvedRefs);
	void QueueIncomingRepUpdates(USpatialActorChannel& Channel, UObject& Object, const TSet<FUnrealObjectRef>& UnresolvedRefs);

	void ProcessOrQueueIncomingRPC(const FUnrealObjectRef& InTargetObjectRef, SpatialGDK::RPCPayload&& InPayload);

	void ResolvePendingOperations_Internal(UObject* Object, const FUnrealObjectRef& ObjectRef);
	void ResolveIncomingOperations(UObject* Object, const FUnrealObjectRef& ObjectRef);

	void ResolveObjectReferences(FRepLayout& RepLayout, UObject* ReplicatedObject, FObjectReferencesMap& ObjectReferencesMap, uint8* RESTRICT StoredData, uint8* RESTRICT Data, int32 MaxAbsOffset, TArray<UProperty*>& RepNotifies, bool& bOutSomeObjectsWereMapped, bool& bOutStillHasUnresolved);

	void ProcessQueuedResolvedObjects();
	void ProcessQueuedActorRPCsOnEntityCreation(AActor* Actor, SpatialGDK::RPCsOnEntityCreation& QueuedRPCs);
	void UpdateShadowData(Worker_EntityId EntityId);
	TWeakObjectPtr<USpatialActorChannel> PopPendingActorRequest(Worker_RequestId RequestId);

	AActor* FindSingletonActor(UClass* SingletonClass);

	void OnHeartbeatComponentUpdate(const Worker_ComponentUpdateOp& Op);

	void PeriodicallyProcessIncomingRPCs();

public:
	TMap<FUnrealObjectRef, TSet<FChannelObjectPair>> IncomingRefsMap;

	TMap<TPair<Worker_EntityId_Key, Worker_ComponentId>, TSharedRef<FPendingSubobjectAttachment>> PendingEntitySubobjectDelegations;

	FOnEntityAddedDelegate OnEntityAddedDelegate;
	FOnEntityRemovedDelegate OnEntityRemovedDelegate;

private:
	UPROPERTY()
	USpatialNetDriver* NetDriver;

	UPROPERTY()
	USpatialStaticComponentView* StaticComponentView;

	UPROPERTY()
	USpatialSender* Sender;

	UPROPERTY()
	USpatialPackageMapClient* PackageMap;

	UPROPERTY()
	USpatialClassInfoManager* ClassInfoManager;

	UPROPERTY()
	UGlobalStateManager* GlobalStateManager;

	SpatialLoadBalanceEnforcer* LoadBalanceEnforcer;

	FTimerManager* TimerManager;
	
	TArray<TPair<UObject*, FUnrealObjectRef>> ResolvedObjectQueue;
<<<<<<< HEAD

=======
	TMap<FUnrealObjectRef, TSet<FSpatialObjectRepState*>> ObjectRefToRepStateMap;
	TMap<FUnrealObjectRef, FIncomingRPCArray> IncomingRPCMap;
>>>>>>> Track mapped object refs
	FRPCContainer IncomingRPCs;

	bool bInCriticalSection;
	TArray<Worker_EntityId> PendingAddEntities;
	TArray<Worker_AuthorityChangeOp> PendingAuthorityChanges;
	TArray<PendingAddComponentWrapper> PendingAddComponents;
	TArray<Worker_RemoveComponentOp> QueuedRemoveComponentOps;

	TMap<Worker_RequestId_Key, TWeakObjectPtr<USpatialActorChannel>> PendingActorRequests;
	FReliableRPCMap PendingReliableRPCs;

	TMap<Worker_RequestId_Key, EntityQueryDelegate> EntityQueryDelegates;
	TMap<Worker_RequestId_Key, ReserveEntityIDsDelegate> ReserveEntityIDsDelegates;
	TMap<Worker_RequestId_Key, CreateEntityDelegate> CreateEntityDelegates;

	// This will map PlayerController entities to the corresponding SpatialNetConnection
	// for PlayerControllers that this server has authority over. This is used for player
	// lifecycle logic (Heartbeat component updates, disconnection logic).
	TMap<Worker_EntityId_Key, TWeakObjectPtr<USpatialNetConnection>> AuthorityPlayerControllerConnectionMap;

	TMap<TPair<Worker_EntityId_Key, Worker_ComponentId>, PendingAddComponentWrapper> PendingDynamicSubobjectComponents;
};
