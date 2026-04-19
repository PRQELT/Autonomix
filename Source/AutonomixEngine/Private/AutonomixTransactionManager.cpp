// Copyright Autonomix. All Rights Reserved.

#include "AutonomixTransactionManager.h"
#include "AutonomixCoreModule.h"

FAutonomixTransactionManager::FAutonomixTransactionManager() {}
FAutonomixTransactionManager::~FAutonomixTransactionManager() {}
void FAutonomixTransactionManager::BeginUndoGroup(const FString& GroupName) { CurrentUndoGroup = GroupName; UndoGroupHistory.Add(GroupName); }
void FAutonomixTransactionManager::EndUndoGroup() { CurrentUndoGroup.Empty(); }
void FAutonomixTransactionManager::BeginTransaction(const FString& Description) { /* Stub: will use FScopedTransaction */ }
void FAutonomixTransactionManager::EndTransaction() { /* Stub */ }
bool FAutonomixTransactionManager::UndoLastGroup() { /* Stub */ return false; }
