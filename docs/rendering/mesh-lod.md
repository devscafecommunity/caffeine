# Mesh LOD

Meshes with at least 600 indices get two clustered LODs (Rossignac vertex clustering) in `GpuSceneRenderer`. The source mesh is never rewritten.

| Distância (mundo / escala) | LOD |
|----------------------------|-----|
| < 40 m | original |
| 40–110 m | célula = 4.5% da diagonal |
| > 110 m | célula = 12% da diagonal |

A troca usa hysteresis de 12% para não oscilar na fronteira. Terreno continua no `TerrainLodSystem`. Objetos perto da câmara, incluindo a esfera do viewport, ficam no LOD 0.

Instancing GPU ainda não existe: `CommandBuffer` só tem `drawIndexed`. Occlusion culling também não — só frustum.
