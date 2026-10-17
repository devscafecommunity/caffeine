# Luz volumétrica

Ativa em **Forward Render Features → Volumetrics** (ou Lua `setVolumetrics`). Por defeito está **Off**.

Não há um segundo draw da geometria. O nevoeiro marcha da câmara até à superfície no shader que já corre:

- **Low** — 6 passos
- **Medium** — 12 passos
- **Density** e **Height** controlam a densidade e a queda com a altitude
- **Anisotropy** inclina o nevoeiro na direção do sol
- **Sample shadows** lê o shadow map em cada passo (mais caro; deixa desligado se o frame passar de 16 ms)

Um froxel 3D ainda não existe. Ver [`forward-render-features.md`](forward-render-features.md).
