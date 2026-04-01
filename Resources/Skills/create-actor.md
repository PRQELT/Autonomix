# Skill: Create Actor
## Description
Create a complete Unreal Engine Actor with C++ class and optional Blueprint child.
## Arguments
- {{arg}}: The class name for the actor (e.g., APickupItem)
## Steps
1. **Create the header file** (`Source/[Project]/{{arg}}.h`):
   - Include UCLASS macro with appropriate specifiers
   - Include GENERATED_BODY()
   - Declare BeginPlay() and Tick() override
   - Add root component (USceneComponent or USphereComponent)
   - Add any mesh/collision components as needed

2. **Create the source file** (`Source/[Project]/{{arg}}.cpp`):
   - Implement constructor with CreateDefaultSubobject for components
   - Set bReplicates if multiplayer is needed
   - Implement BeginPlay and Tick

3. **Verify includes** are correct and there are no circular dependencies

4. **Build** the project to verify the new class compiles

## Example
```cpp
// AMyPickup.h
UCLASS()
class MYPROJECT_API AMyPickup : public AActor
{
    GENERATED_BODY()
public:
    AMyPickup();
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY(VisibleAnywhere)
    UStaticMeshComponent* MeshComponent;
};
```
