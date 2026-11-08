# Code style

Match the file you are editing. Where a file has no precedent, follow these conventions.

## Formatting

- UTF-8, LF line endings, 4-space indentation, no trailing whitespace ([`.editorconfig`](../.editorconfig)).
- Braces on their own lines:

```cpp
void Renderer::Pass_Example(RHI_CommandList* cmd_list)
{
    if (!cvar_example.GetValueAs<bool>())
    {
        return;
    }
}
```

- Do not wrap a line just to make it shorter; wrap only when it is genuinely long.
- Align related assignments and declarations in columns when they sit together, as the surrounding code does.

## Naming

| Kind | Convention | Example |
| ---- | ---------- | ------- |
| Classes, structs, enums | PascalCase | `CommandStack`, `RHI_Texture`, `EngineMode` |
| Public methods | PascalCase | `GetPosition`, `SetActive` |
| Members | `m_` prefix, snake_case | `m_position_local` |
| Locals, parameters, free functions | snake_case | `world_file_path`, `parse_request` |
| Helper namespaces | snake_case | `world_systems`, `editor_history` |
| Console variables | `cvar_` prefix; registered names are dotted lowercase | `cvar_bloom`, `"r.bloom"` |
| Lua and world scripts | snake_case file names, `<world>_<part>.lua` | `plan_tune_shop.lua` |

New code uses snake_case for anything that is not part of an existing PascalCase API.

## Files

- Engine code lives in `namespace spartan`.
- Every source file starts with the license header; copy it from a neighboring file.
- `.cpp` files include `pch.h` first, then group includes under `//= INCLUDES ===` markers.
- Keep file-local helpers in an anonymous namespace at the top of the `.cpp`.

## Comments

Comments state constraints the code cannot show: why an order matters, which thread owns something, a hardware or API quirk. They are short and do not narrate what the next line does or the history of a change.

## Architecture rules

- No game-specific logic in engine systems. Game behavior is components, world systems and Lua.
- The main thread owns the world, the renderer and the Lua state. Worker threads build data and hand it over (`World::EntityBatch`, `McpQueue`).
- Expose new tunables as console variables, and editable component fields as attributes so they get the inspector, undo and serialization for free.
- Anything an agent might need to do in the editor should also be reachable through the [MCP bridge](mcp.md).
