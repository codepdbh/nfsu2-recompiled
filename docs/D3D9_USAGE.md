# Использование Direct3D 9 (измеренное)

Источник: `src/runtime/d3d9_trace.c` (`D3D_TRACE=summary`, значение по
умолчанию), один прогон рекомпилированной игры через загрузку и вступительные
ролики, 1,320 кадров. Каждый прогон перезаписывает
`logs/d3d9_usage_<stamp>.txt` с тем же форматом; это спецификация, которой
должен соответствовать рендерер Vulkan, и она растёт по мере прохождения
дальнейших частей игры (меню и гонки добавят состояния и форматы).

## Устройство

`IDirect3D9::CreateDevice`: адаптер 0, `D3DDEVTYPE_HAL`, поведение `0x50`
(`HARDWARE_VERTEXPROCESSING | FPU_PRESERVE`), back buffer 640x480
`D3DFMT_X8R8G8B8` (21) x1, без MSAA, `D3DSWAPEFFECT_DISCARD`, **полноэкранный
режим**, авто depth-stencil `D3DFMT_D24S8` (75), `D3DPRESENT_INTERVAL_IMMEDIATE`.
Используется `Reset` (обработка device-lost при потере фокуса окном).

## На кадр (интро)

~496 вызовов D3D, 14 draw-вызовов, 14 `SetTexture`, ~166 `SetRenderState` на
кадр.

## Вызванные методы

| Метод | вызовов | | Метод | вызовов |
|---|---|---|---|---|
| SetRenderState | 230,771 | | SetVertexShader / SetPixelShader | 19,822 / 19,822 |
| SetTextureStageState | 185,536 | | SetVertexShaderConstantF | 19,808 |
| SetSamplerState | 146,661 | | DrawPrimitiveUP | 19,766 |
| SetTexture | 19,866 | | SetVertexDeclaration | 6,641 |
| SetRenderTarget / SetDepthStencilSurface | 2,644 / 2,644 | | SetViewport | 2,620 |
| Clear | 1,343 | | BeginScene / EndScene | 1,311 / 1,311 |
| TestCooperativeLevel | 1,328 | | Present | 1,320 |
| CreateTexture | 814 | | BeginStateBlock / EndStateBlock | 421 / 380 |
| GetDeviceCaps | 103 | | CreateVertexDeclaration | 79 |
| CreateVertexShader / CreatePixelShader | 34 / 34 | | ValidateDevice | 33 |
| CreateDepthStencilSurface | 32 | | CreateVertexBuffer / CreateIndexBuffer | 31 / 31 |
| SetPixelShaderConstantF | 32 | | LightEnable | 10 |
| CreateCubeTexture | 8 | | GetBackBuffer | 8 |
| Reset | 6 | | GetDepthStencilSurface | 4 |
| SetStreamSource / SetIndices | 3 / 3 | | CreateRenderTarget / StretchRect | 2 / 2 |
| SetFVF | 2 | | GetAvailableTextureMem | 2 |

Пока не встречались: `DrawPrimitive`, `DrawIndexedPrimitive`,
`DrawIndexedPrimitiveUP`, `SetTransform`, фиксированная функция освещения
помимо `LightEnable`, запросы (queries).

## Встреченные значения

- **Render states** (`D3DRENDERSTATETYPE`): 7 ZENABLE, 14 ZWRITEENABLE, 15 ALPHATESTENABLE, 19 SRCBLEND, 20 DESTBLEND, 22 CULLMODE, 23 ZFUNC, 24 ALPHAREF, 25 ALPHAFUNC, 27 ALPHABLENDENABLE, 28 FOGENABLE, 29 SPECULARENABLE, 34 FOGCOLOR, 60 TEXTUREFACTOR, 137 LIGHTING, 141 COLORVERTEX, 145 VERTEXBLEND, 161 MULTISAMPLEANTIALIAS, 168 COLORWRITEENABLE.
- **Texture stage states**: 1 COLOROP, 4 ALPHAOP, 11 TEXCOORDINDEX, 24 TEXTURETRANSFORMFLAGS.
- **Sampler states**: 1 ADDRESSU, 2 ADDRESSV, 5 MAGFILTER, 6 MINFILTER, 7 MIPFILTER, 10 MAXANISOTROPY.
- **Форматы текстур**: X8R8G8B8 (21), DXT1 (`'DXT1'`), DXT5 (`'DXT5'`); кубические карты X8R8G8B8; render target'ы X8R8G8B8; depth-поверхности D16 (80) плюс авто depth D24S8.
- **Самая большая текстура**: 1024x1024.
- **Вершинные буферы**: использование `WRITEONLY` и `WRITEONLY|DYNAMIC`; индексные буферы `D3DFMT_INDEX16`.
- **Вершинный ввод**: 79 вершинных деклараций и `SetFVF(0x142)` = XYZ|DIFFUSE|TEX1.
- **Типы примитивов**: 6 = `D3DPT_TRIANGLEFAN` (все draw-вызовы на данный момент — это `DrawPrimitiveUP`).
- **Шейдеры**: создано 34 вершинных + 34 пиксельных шейдера (из эффектов D3DX `.fx` в ресурсах RT_RCDATA exe-файла, скомпилированных во время выполнения статически слинкованным D3DX).
