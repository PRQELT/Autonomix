// Copyright Autonomix. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Transport type for MCP server connections.
 * Plain C++ enum — no Blueprint exposure needed.
 */
enum class EAutonomixMCPTransport : uint8
{
	/** Run a local subprocess and communicate via stdin/stdout */
	Stdio,

	/** Connect to a remote SSE (Server-Sent Events) endpoint */
	SSE,

	/** Connect via HTTP streaming (newer MCP transport) */
	StreamableHTTP,
};

/**
 * Configuration for a single MCP server.
 */
struct AUTONOMIXLLM_API FAutonomixMCPServerConfig
{
	/** Unique server name (used as prefix for tool names: mcp_[name]_[tool]) */
	FString Name;

	/** Transport type */
	EAutonomixMCPTransport Transport = EAutonomixMCPTransport::Stdio;

	/** For Stdio: command to run (e.g., "node", "python") */
	FString Command;

	/** For Stdio: command arguments */
	TArray<FString> Args;

	/** For Stdio: working directory (defaults to project root) */
	FString WorkingDirectory;

	/** For SSE/HTTP: base URL */
	FString Url;

	/** For SSE/HTTP: additional HTTP headers */
	TMap<FString, FString> Headers;

	/** Environment variables to pass to the subprocess */
	TMap<FString, FString> Env;

	/** Whether this server is enabled */
	bool bEnabled = true;

	/** Timeout for tool calls in seconds (default: 60) */
	int32 TimeoutSeconds = 60;

	/** Tools that are always auto-approved without user confirmation */
	TArray<FString> AlwaysAllowTools;

	/** Tools that are explicitly disabled */
	TArray<FString> DisabledTools;
};

/**
 * A tool exposed by an MCP server.
 */
struct AUTONOMIXLLM_API FAutonomixMCPTool
{
	/** Tool name (without server prefix) */
	FString Name;

	/** Fully-qualified tool name: mcp_[server]_[tool] */
	FString QualifiedName;

	/** Description of what this tool does */
	FString Description;

	/** JSON Schema for input parameters */
	TSharedPtr<FJsonObject> InputSchema;

	/** Which server this tool belongs to */
	FString ServerName;

	/** Whether this tool is auto-approved */
	bool bAlwaysAllow = false;
};

/**
 * Result of calling an MCP tool.
 */
struct AUTONOMIXLLM_API FAutonomixMCPToolResult
{
	bool bSuccess = false;
	FString Content;
	FString ErrorMessage;
};

/**
 * Model Context Protocol (MCP) client for connecting to external tool servers.
 *
 * Ported/adapted from Roo Code's McpHub.ts.
 *
 * MCP allows Autonomix to connect to external servers that expose additional
 * tools to Claude. Each server can expose any number of tools with custom
 * JSON schemas that are injected into the Claude API request alongside
 * Autonomix's built-in tools.
 *
 * SUPPORTED TRANSPORTS:
 *   - Stdio: Spawn a local subprocess (node, python, etc.)
 *   - SSE: Connect to a remote Server-Sent Events endpoint
 *   - StreamableHTTP: Modern HTTP streaming transport
 *
 * UE-SPECIFIC MCP SERVER IDEAS:
 *   - UE Build Server: Exposes build_project, get_compile_errors, hot_reload
 *   - Perforce Server: Exposes checkout_file, get_changelist, submit
 *   - Asset Registry Server: Exposes find_asset, get_dependencies, load_asset
 *   - Blueprint Compiler Server: Exposes compile_blueprint, get_blueprint_errors
 *
 * TOOL NAMING CONVENTION:
 *   MCP tools are exposed to Claude as: mcp_[serverName]_[toolName]
 *   e.g., a server "ue_build" with tool "compile" → "mcp_ue_build_compile"
 *
 * CONFIGURATION:
 *   MCP servers are configured in Project Settings → Autonomix → MCP Servers
 *   Or loaded from Resources/MCPServers/mcp_config.json
 *
 * INTEGRATION WITH TOOL SCHEMA REGISTRY:
 *   When MCP servers are connected, their tools are injected into the
 *   AutonomixToolSchemaRegistry as dynamic entries. They appear in Claude's
 *   tool list alongside built-in tools.
 *
 * NOTE: MCP integration is Phase 4 — requires substantial infrastructure.
 *   This header defines the interface. Implementation requires:
 *   - FPlatformProcess for Stdio transport
 *   - FHttpModule for SSE/HTTP transports
 *   - JSON-RPC 2.0 message protocol
 *   - Tool schema injection into AutonomixToolSchemaRegistry
 */
class AUTONOMIXLLM_API FAutonomixMCPClient
{
public:
	FAutonomixMCPClient();
	~FAutonomixMCPClient();

	// ---- Server Management ----

	/** Add a server configuration */
	void AddServer(const FAutonomixMCPServerConfig& Config);

	/** Remove a server by name */
	void RemoveServer(const FString& ServerName);

	/** Get all configured servers */
	const TArray<FAutonomixMCPServerConfig>& GetServers() const { return ServerConfigs; }

	/** Load server configurations from mcp_config.json */
	bool LoadConfigFromDisk();

	/** Save current server configurations to disk */
	bool SaveConfigToDisk() const;

	// ---- Connection ----

	/**
	 * Connect to all enabled servers.
	 * @param OutErrors  Connection errors per server (empty = all succeeded)
	 * @return Number of successfully connected servers
	 */
	int32 ConnectAll(TMap<FString, FString>& OutErrors);

	/** Connect to a specific server */
	bool ConnectServer(const FString& ServerName, FString& OutError);

	/** Disconnect from all servers */
	void DisconnectAll();

	/** Disconnect from a specific server */
	void DisconnectServer(const FString& ServerName);

	/** Check if a server is connected */
	bool IsServerConnected(const FString& ServerName) const;

	// ---- Tools ----

	/** Get all tools from all connected servers */
	TArray<FAutonomixMCPTool> GetAllTools() const;

	/** Get tools from a specific server */
	TArray<FAutonomixMCPTool> GetToolsForServer(const FString& ServerName) const;

	/** Get a tool by its qualified name (mcp_server_tool) */
	const FAutonomixMCPTool* GetTool(const FString& QualifiedToolName) const;

	/** Get all tools as JSON schemas for injection into Claude API */
	TArray<TSharedPtr<FJsonObject>> GetToolSchemasForAPI() const;

	// ---- Tool Execution ----

	/**
	 * Execute an MCP tool call.
	 *
	 * @param QualifiedToolName  Tool name in "mcp_server_tool" format
	 * @param Arguments          Tool input arguments as JSON object
	 * @return Tool result
	 */
	FAutonomixMCPToolResult ExecuteTool(
		const FString& QualifiedToolName,
		const TSharedPtr<FJsonObject>& Arguments
	);

	// ---- Diagnostics ----

	/** Get connection status for all servers as a formatted string */
	FString GetStatusSummary() const;

	/** Get path to MCP config file */
	static FString GetConfigFilePath();

private:
	TArray<FAutonomixMCPServerConfig> ServerConfigs;

	/** Connected servers: name -> tools */
	TMap<FString, TArray<FAutonomixMCPTool>> ConnectedServerTools;

	/** Server connection state */
	TSet<FString> ConnectedServers;

	/** Execute a Stdio tool call */
	FAutonomixMCPToolResult ExecuteStdioTool(
		const FAutonomixMCPServerConfig& Config,
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments
	);

	/** Execute an SSE/HTTP tool call */
	FAutonomixMCPToolResult ExecuteHTTPTool(
		const FAutonomixMCPServerConfig& Config,
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments
	);

	/** List tools from a connected server */
	bool ListServerTools(
		const FAutonomixMCPServerConfig& Config,
		TArray<FAutonomixMCPTool>& OutTools,
		FString& OutError
	);
};
