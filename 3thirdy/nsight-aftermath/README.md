# Nsight Aftermath SDK (drop-in)

O SDK **não** pode ser baixado sem conta NVIDIA Developer — coloca o zip aqui depois do download.

## Passos

1. Abre https://developer.nvidia.com/nsight-aftermath/getting-started  
2. **Download Nsight Aftermath SDK for Windows** (2025.5+)  
3. Extrai para **esta pasta** de forma que exista:

```
3thirdy/nsight-aftermath/
  include/GFSDK_Aftermath.h
  include/GFSDK_Aftermath_GpuCrashDump.h   (e restantes headers)
  lib/x64/GFSDK_Aftermath_Lib.x64.lib
  lib/x64/GFSDK_Aftermath_Lib.x64.dll
```

Layouts aceites pelo CMake (qualquer um):

- `3thirdy/nsight-aftermath/include` + `lib/x64`
- `3thirdy/nsight-aftermath/<version>/include` + `lib/x64`
- Env `NSIGHT_AFTERMATH_SDK` apontando para a raiz do SDK

4. Reconfigura o CMake:

```powershell
cmake --preset=windows-release
cmake --build --preset=windows-release
```

Se detetado, o build imprime `Tucano: Nsight Aftermath ENABLED` e define `TUCANO_AFTERMATH`.

## Sem SDK

O engine continua com **DRED** (Microsoft). Aftermath é opcional e só NVIDIA.
