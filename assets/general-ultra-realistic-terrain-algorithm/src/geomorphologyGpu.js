/**
 * GPU-accelerated geomorphology — PDE loop on WebGPU, post-passes on CPU
 */
import {
  applyVolcanicStructures,
  applyFluvialIncisionPass,
  applyLandslideFinalPass,
  applyHydraulicDetailPass,
  clampHeightsToEnvelope,
} from './geomorphology.js';
import { repairHeightArtifacts } from './smooth.js';

export async function simulateGeomorphologyGpu(heights, size, cellSize, params, noiseGenerators) {
  const gpu = params.gpuAccelerator;
  if (!gpu?.ready) {
    const { simulateGeomorphology } = await import('./geomorphology.js');
    return simulateGeomorphology(heights, size, cellSize, params, noiseGenerators);
  }

  let current = applyVolcanicStructures(heights, size, cellSize, params.volcanic);

  current = await gpu.runIterations(current, size, cellSize, params);

  const postPde = new Float32Array(current);
  current = applyFluvialIncisionPass(
    current,
    size,
    cellSize,
    params.fluvialIncision,
    params.precipitation
  );
  current = applyLandslideFinalPass(
    current,
    size,
    cellSize,
    params.landslide,
    params.lithology,
    noiseGenerators.noiseStrata
  );
  current = applyHydraulicDetailPass(
    current,
    size,
    cellSize,
    params.hydraulicDetail,
    noiseGenerators.noiseMicro,
    params.precipitation
  );
  current = clampHeightsToEnvelope(current, postPde, params.heightEnvelope);
  repairHeightArtifacts(current, size, params.artifactThreshold ?? 6.5);

  return { heights: current };
}
