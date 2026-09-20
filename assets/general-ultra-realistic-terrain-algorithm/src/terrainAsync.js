/**
 * Async terrain generation — uses WebGPU PDE when gpuAccelerator is set
 */
import { generateTerrain, generateBlendedTerrain, DEFAULT_CONFIG } from './terrain.js';

function deepMerge(target, source) {
  const result = { ...target };
  for (const key of Object.keys(source)) {
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      result[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      result[key] = source[key];
    }
  }
  return result;
}

export async function generateTerrainAutoAsync(config = {}) {
  const cfg = deepMerge(DEFAULT_CONFIG, config);
  const hasGpu = cfg.geomorph?.gpuAccelerator?.ready;

  if (!hasGpu) {
    if (cfg.blend?.enabled) return generateBlendedTerrain(cfg);
    return generateTerrain(cfg);
  }

  const { generateTerrainGpu, generateBlendedTerrainAsync } = await import('./terrainGpuPipeline.js');

  if (cfg.blend?.enabled) {
    return generateBlendedTerrainAsync(cfg);
  }
  return generateTerrainGpu(cfg);
}
