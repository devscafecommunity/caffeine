/**
 * Biome module — delegates to Universal Parametric Biome System (Holdridge/Whittaker extended)
 */

import { fBm } from './noise.js';
import {
  computeUniversalBiomeFields,
  applyBiomeReliefShaping,
  BIOME_ANCHORS,
  getBiomeCatalog,
  evaluateBiomeWeights,
  getEnvironmentalVector,
  interpolateBiomeProperties,
} from './universalBiome.js';

export function temperatureField(x, y, height, params, noiseTemp) {
  const { T0, lapseRate, noiseScale, noiseAmp } = params;
  const noise = fBm(x, y, noiseScale, 3, noiseTemp) * noiseAmp;
  return T0 - lapseRate * height + noise;
}

export function moistureField(x, y, heights, size, cellSize, params, noiseMoisture) {
  const { M0, windX, windZ, condensation, evaporation, noiseAmp, noiseScale } = params;
  const moisture = new Float32Array(size * size);

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      const worldX = (x / (size - 1) - 0.5) * cellSize * (size - 1);
      const worldZ = (z / (size - 1) - 0.5) * cellSize * (size - 1);
      const noise = fBm(worldX, worldZ, noiseScale, 3, noiseMoisture) * noiseAmp;

      let dhdx = 0;
      let dhdz = 0;
      if (x > 0 && x < size - 1 && z > 0 && z < size - 1) {
        dhdx = (heights[i + 1] - heights[i - 1]) / (2 * cellSize);
        dhdz = (heights[i + size] - heights[i - size]) / (2 * cellSize);
      }

      const windDotGrad = windX * dhdx + windZ * dhdz;
      const orographic =
        condensation * Math.max(0, windDotGrad) - evaporation * Math.max(0, -windDotGrad);

      moisture[i] = M0 + orographic + noise;
    }
  }

  return moisture;
}

export function computeBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture) {
  return computeUniversalBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture);
}

export {
  BIOME_ANCHORS,
  getBiomeCatalog,
  evaluateBiomeWeights,
  getEnvironmentalVector,
  interpolateBiomeProperties,
  applyBiomeReliefShaping,
};

export const DEFAULT_BIOME_PARAMS = {
  snowLine: 14,
  precipitation: 1.0,
  maxSlope: 1.2,
  biomeGroup: 'all',
  enabledBiomes: null,
  reliefShaping: {
    enabled: true,
    flatAmplitude: 4.0,
    flatReliefCutoff: 0.32,
    strength: 0.22,
    artifactThreshold: 6.0,
  },
  tNorm: { min: -25, max: 35 },
  pNorm: { min: -0.5, max: 3.0 },
  temperature: { T0: 25, lapseRate: 0.6, noiseScale: 180, noiseAmp: 8 },
  moisture: {
    M0: 0.5,
    windX: 1,
    windZ: 0.3,
    condensation: 0.8,
    evaporation: 0.5,
    noiseScale: 150,
    noiseAmp: 0.35,
  },
};
