# Skill: Setup Enhanced Input
## Description
Configure the Enhanced Input system with Input Actions and Mapping Contexts.
## Arguments
- {{arg}}: The action name to set up (e.g., Jump, Sprint, Interact)
## Steps
1. **Create Input Action** asset in Content/Input/Actions/IA_{{arg}}.uasset

2. **Add to Input Mapping Context** (create IMC_Default if it doesn't exist)

3. **In the character/pawn header**, add:
   ```cpp
   UPROPERTY(EditDefaultsOnly, Category="Input")
   TObjectPtr<UInputAction> {{arg}}Action;
   void Handle{{arg}}(const FInputActionValue& Value);
   ```

4. **In BeginPlay**, bind the action:
   ```cpp
   if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
       EIC->BindAction({{arg}}Action, ETriggerEvent::Triggered, this, &AMyChar::Handle{{arg}});
   ```

5. **Implement the handler function**

## Notes
- Include `EnhancedInput/Public/EnhancedInputComponent.h`
- The project must have the EnhancedInput plugin enabled
