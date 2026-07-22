# 3thirdy — third-party SDKs (local)

Pasta para SDKs e libs que **não** vêm via FetchContent (glfw/glm/imgui continuam em `cmake/Dependencies.cmake`).

| Pasta | O quê | Como obter |
|-------|--------|------------|
| `nsight-aftermath/` | NVIDIA Nsight Aftermath (headers + lib + dll) | Download manual (login NVIDIA) — ver README da pasta |
| `nsight-aftermath-samples/` | Samples oficiais (referência) | `.\3thirdy\fetch.ps1` |
| `_incoming/` | Zips temporários antes de extrair | — |

## Convenção

1. Drop/extract SDK **dentro** de `3thirdy/<nome>/` com layout estável (`include/`, `lib/x64/`, …).
2. CMake auto-detecta se os ficheiros existem e liga `TUCANO_*` flags.
3. Binários proprietários **não** vão para o git (ver `.gitignore`); READMEs sim.

## Fetch script

```powershell
.\3thirdy\fetch.ps1
```
