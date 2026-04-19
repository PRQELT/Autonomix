// Copyright Autonomix. All Rights Reserved.

#include "AutonomixMCPClient.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"

// ============================================================================
// MCP Client — Phase 4 stub implementation
//
// This file provides compilable stub implementations for FAutonomixMCPClient.
// Full MCP protocol (JSON-RPC 2.0 over Stdio/SSE/HTTP) implementation is
// deferred to a future sprint. The stubs allow the plugin to compile and load
// while MCP infrastructure is being built.
//
// To enable MCP servers, a full implementation needs:
//  - JSON-RPC 2.0 message framing over stdin/stdout (Stdio transport)
//  - SSE client for remote servers (SSE transport)
//  - FHttpModule streaming for StreamableHTTP transport
//  - initialize / tools/list / tools/call RPC methods
// ============================================================================

FAutonomixMCPClient::FAutonomixMCPClient()
{
}

FAutonomixMCPClient::~FAutonomixMCPClient()
{
	DisconnectAll();
}

void FAutonomixMCPClient::AddServer(const FAutonomixMCPServerConfig& Config)
{
	// Remove existing config with same name
	ServerConfigs.RemoveAll([&](const FAutonomixMCPServerConfig& C) {
		return C.Name == Config.Name;
	});
	ServerConfigs.Add(Config);
}

void FAutonomixMCPClient::RemoveServer(const FString& ServerName)
{
	ServerConfigs.RemoveAll([&](const FAutonomixMCPServerConfig& C) {
		return C.Name == ServerName;
	});
	DisconnectServer(ServerName);
}

bool FAutonomixMCPClient::LoadConfigFromDisk()
{
	const FString ConfigPath = GetConfigFilePath();
	if (!FPaths::FileExists(ConfigPath))
	{
		// No config file — normal for fresh installs
		return true;
	}

	FString JsonContent;
	if (!FFileHelper::LoadFileToString(JsonContent, *ConfigPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FAutonomixMCPClient: Failed to read config from %s"), *ConfigPath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FAutonomixMCPClient: Failed to parse config JSON from %s"), *ConfigPath);
		return false;
	}

	ServerConfigs.Empty();

	const TSharedPtr<FJsonObject>* ServersObj = nullptr;
	if (Root->TryGetObjectField(TEXT("mcpServers"), ServersObj) && ServersObj)
	{
		for (auto& Pair : (*ServersObj)->Values)
		{
			const TSharedPtr<FJsonObject>* ServerObj = nullptr;
			if (!Pair.Value->TryGetObject(ServerObj)) continue;

			FAutonomixMCPServerConfig Config;
			Config.Name = Pair.Key;

			FString TransportStr;
			(*ServerObj)->TryGetStringField(TEXT("type"), TransportStr);
			if (TransportStr == TEXT("sse")) Config.Transport = EAutonomixMCPTransport::SSE;
			else if (TransportStr == TEXT("streamable-http")) Config.Transport = EAutonomixMCPTransport::StreamableHTTP;
			else Config.Transport = EAutonomixMCPTransport::Stdio;

			(*ServerObj)->TryGetStringField(TEXT("command"), Config.Command);
			(*ServerObj)->TryGetStringField(TEXT("url"), Config.Url);
			(*ServerObj)->TryGetBoolField(TEXT("disabled"), Config.bEnabled);
			Config.bEnabled = !Config.bEnabled;  // disabled:true -> bEnabled:false

			const TArray<TSharedPtr<FJsonValue>>* ArgsArray = nullptr;
			if ((*ServerObj)->TryGetArrayField(TEXT("args"), ArgsArray))
			{
				for (const auto& Arg : *ArgsArray)
				{
					Config.Args.Add(Arg->AsString());
				}
			}

			ServerConfigs.Add(Config);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("FAutonomixMCPClient: Loaded %d server configs from disk"), ServerConfigs.Num());
	return true;
}

bool FAutonomixMCPClient::SaveConfigToDisk() const
{
	const FString ConfigPath = GetConfigFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> ServersObj = MakeShared<FJsonObject>();

	for (const FAutonomixMCPServerConfig& Config : ServerConfigs)
	{
		TSharedPtr<FJsonObject> ServerObj = MakeShared<FJsonObject>();

		FString TransportStr;
		switch (Config.Transport)
		{
		case EAutonomixMCPTransport::SSE:           TransportStr = TEXT("sse"); break;
		case EAutonomixMCPTransport::StreamableHTTP: TransportStr = TEXT("streamable-http"); break;
		default:                                     TransportStr = TEXT("stdio"); break;
		}

		ServerObj->SetStringField(TEXT("type"), TransportStr);
		if (!Config.Command.IsEmpty()) ServerObj->SetStringField(TEXT("command"), Config.Command);
		if (!Config.Url.IsEmpty())     ServerObj->SetStringField(TEXT("url"), Config.Url);
		if (!Config.bEnabled)          ServerObj->SetBoolField(TEXT("disabled"), true);

		TArray<TSharedPtr<FJsonValue>> ArgsArray;
		for (const FString& Arg : Config.Args)
		{
			ArgsArray.Add(MakeShared<FJsonValueString>(Arg));
		}
		if (ArgsArray.Num() > 0) ServerObj->SetArrayField(TEXT("args"), ArgsArray);

		ServersObj->SetObjectField(Config.Name, ServerObj);
	}

	Root->SetObjectField(TEXT("mcpServers"), ServersObj);

	FString JsonOutput;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonOutput);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	return FFileHelper::SaveStringToFile(JsonOutput, *ConfigPath);
}

int32 FAutonomixMCPClient::ConnectAll(TMap<FString, FString>& OutErrors)
{
	// Stub: full implementation deferred (requires JSON-RPC 2.0 transport)
	int32 Connected = 0;
	for (const FAutonomixMCPServerConfig& Config : ServerConfigs)
	{
		if (Config.bEnabled)
		{
			FString Error;
			if (ConnectServer(Config.Name, Error))
			{
				Connected++;
			}
			else
			{
				OutErrors.Add(Config.Name, Error);
			}
		}
	}
	return Connected;
}

bool FAutonomixMCPClient::ConnectServer(const FString& ServerName, FString& OutError)
{
	// Stub: full JSON-RPC initialization handshake deferred
	// When implemented, this will:
	// 1. Spawn the subprocess (Stdio) or connect to URL (SSE/HTTP)
	// 2. Send initialize request and await response
	// 3. Send tools/list request and populate ConnectedServerTools
	OutError = TEXT("MCP server connections are not yet implemented. Phase 4 stub.");
	return false;
}

void FAutonomixMCPClient::DisconnectAll()
{
	ConnectedServers.Empty();
	ConnectedServerTools.Empty();
}

void FAutonomixMCPClient::DisconnectServer(const FString& ServerName)
{
	ConnectedServers.Remove(ServerName);
	ConnectedServerTools.Remove(ServerName);
}

bool FAutonomixMCPClient::IsServerConnected(const FString& ServerName) const
{
	return ConnectedServers.Contains(ServerName);
}

TArray<FAutonomixMCPTool> FAutonomixMCPClient::GetAllTools() const
{
	TArray<FAutonomixMCPTool> All;
	for (const auto& Pair : ConnectedServerTools)
	{
		All.Append(Pair.Value);
	}
	return All;
}

TArray<FAutonomixMCPTool> FAutonomixMCPClient::GetToolsForServer(const FString& ServerName) const
{
	const TArray<FAutonomixMCPTool>* Found = ConnectedServerTools.Find(ServerName);
	return Found ? *Found : TArray<FAutonomixMCPTool>();
}

const FAutonomixMCPTool* FAutonomixMCPClient::GetTool(const FString& QualifiedToolName) const
{
	for (const auto& Pair : ConnectedServerTools)
	{
		for (const FAutonomixMCPTool& Tool : Pair.Value)
		{
			if (Tool.QualifiedName == QualifiedToolName)
			{
				return &Tool;
			}
		}
	}
	return nullptr;
}

TArray<TSharedPtr<FJsonObject>> FAutonomixMCPClient::GetToolSchemasForAPI() const
{
	// Returns tool schemas for all connected MCP servers.
	// Currently returns empty array (no servers connected in stub).
	// When implemented, each tool is formatted as a Claude-compatible tool schema
	// with name "mcp_[serverName]_[toolName]" and the server's input schema.
	TArray<TSharedPtr<FJsonObject>> Schemas;

	for (const auto& Pair : ConnectedServerTools)
	{
		for (const FAutonomixMCPTool& Tool : Pair.Value)
		{
			if (!Tool.InputSchema.IsValid()) continue;

			TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
			Schema->SetStringField(TEXT("name"), Tool.QualifiedName);
			Schema->SetStringField(TEXT("description"),
				FString::Printf(TEXT("[MCP:%s] %s"), *Tool.ServerName, *Tool.Description));
			Schema->SetObjectField(TEXT("input_schema"), Tool.InputSchema);
			Schemas.Add(Schema);
		}
	}

	return Schemas;
}

FAutonomixMCPToolResult FAutonomixMCPClient::ExecuteTool(
	const FString& QualifiedToolName,
	const TSharedPtr<FJsonObject>& Arguments
)
{
	FAutonomixMCPToolResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = FString::Printf(
		TEXT("MCP tool '%s' cannot be executed: MCP server connections not yet implemented."),
		*QualifiedToolName
	);
	return Result;
}

FString FAutonomixMCPClient::GetStatusSummary() const
{
	if (ServerConfigs.Num() == 0)
	{
		return TEXT("No MCP servers configured. Add servers to Resources/MCPServers/mcp_config.json.");
	}

	FString Summary = FString::Printf(TEXT("MCP Servers: %d configured, %d connected\n"),
		ServerConfigs.Num(), ConnectedServers.Num());

	for (const FAutonomixMCPServerConfig& Config : ServerConfigs)
	{
		const bool bConnected = ConnectedServers.Contains(Config.Name);
		const int32 ToolCount = ConnectedServerTools.Contains(Config.Name)
			? ConnectedServerTools[Config.Name].Num() : 0;

		Summary += FString::Printf(TEXT("  %s [%s] - %s (%d tools)\n"),
			*Config.Name,
			Config.bEnabled ? TEXT("enabled") : TEXT("disabled"),
			bConnected ? TEXT("connected") : TEXT("disconnected"),
			ToolCount
		);
	}

	return Summary;
}

FString FAutonomixMCPClient::GetConfigFilePath()
{
	FString PluginBaseDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Autonomix"));
	return FPaths::Combine(PluginBaseDir, TEXT("Resources"), TEXT("MCPServers"), TEXT("mcp_config.json"));
}

FAutonomixMCPToolResult FAutonomixMCPClient::ExecuteStdioTool(
	const FAutonomixMCPServerConfig& Config,
	const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments
)
{
	FAutonomixMCPToolResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = TEXT("Stdio transport not yet implemented.");
	return Result;
}

FAutonomixMCPToolResult FAutonomixMCPClient::ExecuteHTTPTool(
	const FAutonomixMCPServerConfig& Config,
	const FString& ToolName,
	const TSharedPtr<FJsonObject>& Arguments
)
{
	FAutonomixMCPToolResult Result;
	Result.bSuccess = false;
	Result.ErrorMessage = TEXT("HTTP/SSE transport not yet implemented.");
	return Result;
}

bool FAutonomixMCPClient::ListServerTools(
	const FAutonomixMCPServerConfig& Config,
	TArray<FAutonomixMCPTool>& OutTools,
	FString& OutError
)
{
	OutError = TEXT("ListServerTools not yet implemented.");
	return false;
}
