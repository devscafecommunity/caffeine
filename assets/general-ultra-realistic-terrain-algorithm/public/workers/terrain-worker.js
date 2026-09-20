/**
 * Web Worker — gera terreno off-thread com WebGPU quando disponível
 */
import { initGpuAcceleration, generateTerrainClient, DEFAULT_CONFIG } from '../dist/terrain-gen.js';

let gpuReady = false;

async function ensureGpu() {
  try {
    gpuReady = await initGpuAcceleration();
  } catch {
    gpuReady = false;
  }
  return gpuReady;
}

self.onmessage = async (e) => {
  const { id, config, useGpu } = e.data;

  try {
    if (useGpu && !gpuReady) {
      await ensureGpu();
    }

    const cfg = { ...DEFAULT_CONFIG, ...config };
    const result = await generateTerrainClient(cfg, useGpu);

    self.postMessage({
      id,
      ok: true,
      data: {
        size: result.gridSize,
        worldSize: result.worldSize,
        heights: result.heights,
        biomeColors: result.biomeColors,
        metadata: {
          stats: result.stats,
          activeBiomes: result.activeBiomes,
          blended: result.blended,
          gpuAccelerated: result.gpuAccelerated ?? false,
          config: {
            gridSize: result.gridSize,
            worldSize: result.worldSize,
            iterations: cfg.geomorph?.iterations,
            blend: cfg.blend?.enabled,
            biomeGroup: cfg.biome?.biomeGroup,
          },
        },
      },
    });
  } catch (err) {
    self.postMessage({ id, ok: false, error: err.message });
  }
};

ensureGpu();
