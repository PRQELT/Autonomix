# Skill: Create Blueprint Interface
## Description
Create a Blueprint interface with C++ backing for interactable objects.
## Arguments
- {{arg}}: Interface name (without 'I' prefix, e.g., Interactable)
## Steps
1. **Create header** `I{{arg}}.h`:
   ```cpp
   UINTERFACE(MinimalAPI)
   class U{{arg}} : public UInterface { GENERATED_BODY() };
   class I{{arg}} {
       GENERATED_BODY()
   public:
       UFUNCTION(BlueprintNativeEvent)
       void Interact(AActor* Interactor);
   };
   ```

2. **Implement on an actor**:
   ```cpp
   class AMyActor : public AActor, public I{{arg}} {
       virtual void Interact_Implementation(AActor* Interactor) override;
   };
   ```

3. **Call via interface**:
   ```cpp
   if (I{{arg}}* Iface = Cast<I{{arg}}>(TargetActor))
       Iface->Execute_Interact(TargetActor, this);
   ```
