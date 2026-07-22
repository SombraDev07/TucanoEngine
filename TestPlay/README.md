# TestPlay — exes de teste (cenário limpo + editor)

Pasta de deploy dos binários de teste. O CMake copia aqui após o build:

| Exe | Função |
|-----|--------|
| `SkyLab.exe` | Cenário limpo: chão + horizonte, **clouds / atmosphere / câmera** (WASD + RMB) |
| `TestEditor.exe` | Mesmo cenário + **editor** ImGui (atmosfera, clouds, câmera, presets) |

## Rodar

```powershell
cd c:\TucanoEngine\TestPlay
.\SkyLab.exe
.\TestEditor.exe
```

Shaders ficam em `TestPlay/shaders/` (copiados do build).

## Controles

- **WASD / QE** — mover · **Shift** — rápido · **RMB** — olhar
- **F12** — screenshot
- **TestEditor**: painéis Atmosphere / Clouds / Camera

Não coloque Sponza nem demos pesadas aqui — só o lab de céu.
