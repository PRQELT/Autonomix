# Skill: Add Component to Actor
## Description
Add and configure a UE component on an existing actor class.
## Arguments
- {{arg1}}: Actor class name
- {{arg2}}: Component type (e.g., UStaticMeshComponent, UAudioComponent)
## Steps
1. **In header**, declare the component:
   ```cpp
   UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
   TObjectPtr<{{arg2}}> MyComponent;
   ```

2. **In constructor**, create and attach:
   ```cpp
   MyComponent = CreateDefaultSubobject<{{arg2}}>(TEXT("MyComponent"));
   MyComponent->SetupAttachment(RootComponent);
   ```

3. **Configure default properties** as needed

4. **Add required include** for the component type

## Notes
- Use TObjectPtr<> instead of raw pointers for GC safety
- VisibleAnywhere shows in viewport, EditAnywhere allows editing
