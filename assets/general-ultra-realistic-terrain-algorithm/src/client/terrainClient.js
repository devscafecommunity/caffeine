/**
 * Browser client entry — terrain generation with optional WebGPU PDE acceleration
 */
import {
  DEFAULT_CONFIG,
  recommendedGridSize,
  scaleConfigForWorldSize,
} from '../terrain.js';
import { PdeGpuAccelerator } from '../gpu/pdeGpu.js';

let gpuAccelerator = null;
let gpuInitPromise = null;

export async function initGpuAcceleration() {
  if (gpuInitPromise) return gpuInitPromise;
  gpuInitPromise = PdeGpuAccelerator.create().then((acc) => {
    gpuAccelerator = acc;
    return acc?.ready ?? false;
  });
  return gpuInitPromise;
}

export function isGpuReady() {
  return gpuAccelerator?.ready ?? false;
}

export async function generateTerrainClient(config, useGpu = true) {
  const cfg = scaleConfigForWorldSize({ ...DEFAULT_CONFIG, ...config });

  if (useGpu && gpuAccelerator?.ready) {
    cfg.geomorph = { ...cfg.geomorph, gpuAccelerator };
  }

  const { generateTerrainAutoAsync } = await import('../terrainAsync.js');
  return generateTerrainAutoAsync(cfg);
}

export { DEFAULT_CONFIG, recommendedGridSize };
