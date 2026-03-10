<p align="center">
  <h1 align="center">🤖 Autonomix — AI Developer for Unreal Engine</h1>
  <p align="center">
    <strong>A production-grade Unreal Engine editor plugin that uses AI to autonomously create, modify, and manage entire game projects — directly inside the editor.</strong>
  </p>
  <p align="center">
    <a href="#features">Features</a> •
    <a href="#supported-providers">Providers</a> •
    <a href="#installation">Installation</a> •
    <a href="#quick-start">Quick Start</a> •
    <a href="#architecture">Architecture</a> •
    <a href="#tool-capabilities">Tools</a> •
    <a href="#license">License</a>
  </p>
</p>

---

## Overview

**Autonomix** brings agentic AI capabilities into the Unreal Engine editor. Instead of just generating code snippets, it operates as a fully autonomous developer — creating Blueprints via T3D injection, editing C++ source files, managing levels, materials, meshes, widgets, PCG graphs, animations, and more — all through a conversational chat interface embedded in the editor.

Think of it as **Cursor/Roo Code, but for Unreal Engine** — with deep engine integration that goes far beyond text editing.

### Key Differentiators

- **T3D Blueprint Injection** — Creates entire Blueprint node graphs in a single transaction using UE's native T3D format. No node-by-node API calls.
- **GUID Placeholder System** — AI uses human-readable tokens (`LINK_1`, `GUID_A`, `NODEREF_Entry`) that get resolved to real engine GUIDs automatically, preserving cross-node pin links.
- **Agentic Tool Loop** — The AI autonomously plans, executes tools, verifies results, and iterates until the task is complete.
- **Multi-Provider Support** — Works with Anthropic Claude, OpenAI, Google Gemini, DeepSeek, Mistral, xAI, OpenRouter, Ollama, and LM Studio.
- **Smart Context Management** — Automatic conversation condensation, sliding-window truncation, and token budget management for long sessions.

---

## Features

### 🎨 Editor Integration
- **Dockable Chat Panel** — Slate-based chat UI with real-time streaming, syntax-highlighted code blocks, diff viewers, and inline asset previews
- **Multi-Tab Conversations** — Work on multiple tasks simultaneously with persistent tab state
- **Context Bar** — Live token usage, cost tracking, and one-click context condensation
- **Auto-Approval System** — Configure automatic approval for read-only operations with safety limits

### 🔧 Blueprint Tools
| Tool | Description |
|------|-------------|
| `create_blueprint` | Create new Blueprint classes (Actor, Character, Component, etc.) |
| `add_blueprint_nodes` | Inject entire node graphs via T3D with GUID placeholder resolution |
| `modify_blueprint_nodes` | Edit existing nodes — change properties, pin values, connections |
| `connect_blueprint_pins` | Wire pins between nodes with type validation |
| `get_blueprint_info` | Full T3D readback of all graphs — the AI sees exactly what the engine has |
| `verify_blueprint_connections` | Comprehensive connection audit with pin-level diagnostics |

### 📝 C++ Tools
| Tool | Description |
|------|-------------|
| `read_file` | Read source files with line numbers |
| `write_to_file` | Create new source files |
| `apply_diff` | Apply surgical search/replace edits to existing files |
| `search_files` | Regex search across the project |
| `list_files` | Browse project directory structure |

### 🏗️ Level & World Tools
| Tool | Description |
|------|-------------|
| `create_level` | Create new levels/maps |
| `place_actor` | Spawn actors in the level with transform and properties |
| `modify_actor` | Edit actor properties, components, transforms |
| `get_level_info` | Query level contents and actor details |

### 🎭 Material Tools
| Tool | Description |
|------|-------------|
| `create_material` | Create materials with expression node graphs |
| `modify_material` | Edit material properties and parameters |
| `create_material_instance` | Create material instances with parameter overrides |

### 📦 Additional Tool Domains
- **Mesh Tools** — Static/skeletal mesh operations, LOD management
- **Animation Tools** — Animation Blueprint, montage, and blend space operations
- **Widget Tools** — UMG widget creation and modification
- **PCG Tools** — Procedural Content Generation graph management
- **Enhanced Input Tools** — Input action and mapping context creation
- **Build Tools** — Compile, cook, package projects
- **Performance Tools** — Profiling, stat capture, optimization analysis
- **Settings Tools** — Project settings management
- **Source Control Tools** — Git/Perforce integration
- **Context Tools** — On-demand project exploration (assets, classes, hierarchy)
- **Task Tools** — Task delegation, todo management, checkpoints

---

## Supported Providers

| Provider | Models | Streaming | Extended Thinking |
|----------|--------|-----------|-------------------|
| **Anthropic** | Claude Sonnet 4, Claude Opus 4, Claude 3.5 Sonnet, etc. | ✅ SSE | ✅ budget_tokens |
| **OpenAI** | GPT-4o, GPT-4 Turbo, o1, o3, etc. | ✅ SSE | ✅ reasoning_effort |
| **Google** | Gemini 2.5 Pro/Flash, Gemini 3.x | ✅ SSE | ✅ thinkingBudget |
| **DeepSeek** | DeepSeek V3, DeepSeek R1 | ✅ SSE | ✅ reasoning_content |
| **Mistral** | Mistral Large, Codestral, etc. | ✅ SSE | — |
| **xAI** | Grok-2, Grok-3, etc. | ✅ SSE | — |
| **OpenRouter** | Any model via OpenRouter | ✅ SSE | Varies |
| **Ollama** | Any local model | ✅ SSE | — |
| **LM Studio** | Any local model | ✅ SSE | — |
| **Custom** | Any OpenAI-compatible endpoint | ✅ SSE | — |

---

## Installation

### Prerequisites

- **Unreal Engine 5.3+** (tested on 5.3, 5.4, 5.5)
- **Visual Studio 2022** or compatible C++ compiler
- An API key from at least one supported provider

### Steps

1. **Clone the repository** into your project's `Plugins` folder:
   ```bash
   cd YourProject/Plugins
   git clone https://github.com/PRQELT/Autonomix.git
   ```

2. **Regenerate project files** (right-click `.uproject` → Generate Visual Studio project files)

3. **Build the project** — Open in your IDE and build (or launch UE which will compile automatically)

4. **Enable the plugin** — It should be enabled by default. If not: Edit → Plugins → search "Autonomix" → Enable → Restart

5. **Configure API key** — Edit → Project Settings → Plugins → Autonomix → enter your API key

### Alternative: Manual Copy

1. Download the repository as a ZIP
2. Extract the `Autonomix` folder into `YourProject/Plugins/Autonomix`
3. Follow steps 2-5 above

---

## Quick Start

1. **Open the Autonomix panel**: Window → Autonomix (or use the toolbar button)

2. **Start chatting**: Type a request in natural language:
   ```
   Create a third-person character Blueprint with health and stamina systems.
   Add a HUD widget that displays both values as progress bars.
   ```

3. **Review and approve**: The AI will present each action for approval before executing. Read-only operations can be auto-approved in settings.

4. **Iterate**: The AI verifies its work and iterates until the task is complete. You can interrupt, provide feedback, or redirect at any time.

### Example Tasks

- *"Create a door Blueprint that opens when the player overlaps a trigger box"*
- *"Add a main menu widget with Play, Settings, and Quit buttons"*
- *"Set up an Enhanced Input system with WASD movement and mouse look"*
- *"Create a PCG graph that scatters trees on landscape with density falloff near roads"*
- *"Profile the current level and suggest optimization improvements"*
- *"Create a material that blends between snow and rock based on world-space Z height"*

---

## Architecture

Autonomix is organized into 5 modules:

```
Autonomix/
├── Source/
│   ├── AutonomixCore/       # Types, settings, project context, interfaces
│   ├── AutonomixLLM/        # LLM clients, conversation management, context 
│   │                        # condensation, token counting, cost tracking,
│   │                        # tool result validation, model registry
│   ├── AutonomixEngine/     # Action routing, context gathering, diff application,
│   │                        # backup/checkpoint management, file operations,
│   │                        # safety gates, tool repetition detection
│   ├── AutonomixActions/    # Tool executors (Blueprint, C++, Level, Material,
│   │                        # Mesh, Animation, Widget, PCG, Build, etc.)
│   └── AutonomixUI/         # Slate widgets (chat panel, code blocks, diff
│                            # viewer, context bar, input area, history)
├── Resources/
│   ├── SystemPrompt/        # AI system prompt with rules and workflow
│   └── ToolSchemas/         # JSON tool definitions sent to the LLM
└── Autonomix.uplugin        # Plugin descriptor
```

### Key Design Patterns

- **T3D Injection Pipeline** — Blueprints are created by generating T3D text (the same format UE uses for clipboard copy/paste), resolving GUID placeholders, then calling `FEdGraphUtilities::ImportNodesFromText`. This gives the AI full control over node graphs in a single atomic operation.

- **Non-Destructive Truncation** — When the context window fills up, messages are tagged (not deleted) and filtered from the effective history. This preserves full history for rewind while keeping API payloads within limits.

- **Orphan Tool Result Validation** — Before every API call, a multi-pass validator ensures all `tool_result` blocks reference valid `tool_use` IDs. This prevents Claude HTTP 400 errors after context truncation.

- **Agentic Loop with Safety Gates** — The AI runs in a loop: receive response → execute tools → feed results back → repeat. Safety gates detect infinite loops, tool repetition, and dangerous operations.

---

## Configuration

All settings are in **Edit → Project Settings → Plugins → Autonomix**:

| Setting | Default | Description |
|---------|---------|-------------|
| **Provider** | Anthropic | Which AI provider to use |
| **API Key** | *(empty)* | Your provider API key |
| **Model** | claude-sonnet-4-6 | Model to use for generation |
| **Context Window** | 200K | Standard (200K) or Extended (1M) |
| **Max Response Tokens** | 8,192 | Maximum tokens per AI response |
| **Extended Thinking** | Off | Enable Claude's extended thinking mode |
| **Thinking Budget** | 3,000 | Token budget for extended thinking |
| **Auto-Condense** | On | Automatically condense context at 80% usage |
| **Auto-Approve Read** | Off | Auto-approve read-only tool calls |
| **Context Token Budget** | 30,000 | Max tokens for project context per request |
| **Show Cost Estimates** | On | Display running cost in the UI |

---

## Security

- **No hardcoded keys** — All API keys are stored in UE's per-project config system (`DefaultEditorPerProjectUserSettings.ini`), which is in your `Saved/Config/` folder and excluded from version control by default.
- **Password fields** — API key inputs use UE's `PasswordField` meta tag (masked in the UI).
- **The `.gitignore` excludes** `Config/`, `Saved/`, and all build artifacts.
- **Safety gates** prevent destructive operations without explicit approval.

---

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

---

## Acknowledgments

- Inspired by [Roo Code](https://github.com/RooVetGit/Roo-Code) (VS Code AI coding assistant) — context management and conversation condensation patterns were studied and adapted for the UE environment.
- Built with [Unreal Engine](https://www.unrealengine.com/) by Epic Games.
