# Skill: Setup Actor Replication
## Description
Configure an actor class for network replication in multiplayer UE games.
## Arguments
- {{arg}}: Actor class to configure replication for
## Steps
1. **Constructor**: Enable replication
   ```cpp
   bReplicates = true;
   SetReplicateMovement(true);
   ```

2. **Add GetLifetimeReplicatedProps**:
   ```cpp
   void {{arg}}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
   {
       Super::GetLifetimeReplicatedProps(OutLifetimeProps);
       DOREPLIFETIME({{arg}}, Health);
       DOREPLIFETIME_CONDITION({{arg}}, bIsVisible, COND_OwnerOnly);
   }
   ```

3. **Mark properties for replication**:
   ```cpp
   UPROPERTY(Replicated)
   float Health;
   UPROPERTY(ReplicatedUsing=OnRep_Health)
   float Health;
   UFUNCTION()
   void OnRep_Health();
   ```

4. **Server RPCs** for player actions:
   ```cpp
   UFUNCTION(Server, Reliable)
   void ServerDoAction();
   ```

5. **Required include**: `#include "Net/UnrealNetwork.h"`
