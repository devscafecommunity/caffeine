/**
 * Multi-domain terrain blending
 *
 * h_final(x,y) = Σ_k W_k(x,y) · h_k(x,y),  Σ W_k = 1
 * W_k smoothed via smoothstep for natural transitions
 */

import { fBm, createNoiseField } from './noise.js';

export function smoothstep(t) {
  const x = Math.max(0, Math.min(1, t));
  return x * x * (3 - 2 * x);
}

export function lerp(a, b, t) {
  return a + (b - a) * t;
}

/**
 * Generate weight mask W ∈ [0,1] where 0 = domain A, 1 = domain B
 */
export function generateWeightMask(size, cellSize, params, seed) {
  const mask = new Float32Array(size * size);
  const half = (cellSize * (size - 1)) / 2;
  const { type, scale, offsetX, offsetZ, sharpness } = params;

  if (type === 'noise') {
    const noise = createNoiseField(seed + 9000);
    for (let z = 0; z < size; z++) {
      for (let x = 0; x < size; x++) {
        const worldX = x * cellSize - half;
        const worldZ = z * cellSize - half;
        const raw = fBm(worldX + offsetX, worldZ + offsetZ, scale, 4, noise);
        const normalized = (raw + 1) * 0.5;
        mask[z * size + x] = applySharpness(normalized, sharpness);
      }
    }
  } else if (type === 'gradient') {
    for (let z = 0; z < size; z++) {
      for (let x = 0; x < size; x++) {
        const t = x / (size - 1);
        mask[z * size + x] = applySharpness(t, sharpness);
      }
    }
  } else if (type === 'radial') {
    const cx = size / 2;
    const cz = size / 2;
    const maxR = Math.sqrt(cx * cx + cz * cz);
    for (let z = 0; z < size; z++) {
      for (let x = 0; x < size; x++) {
        const dx = x - cx;
        const dz = z - cz;
        const t = Math.sqrt(dx * dx + dz * dz) / maxR;
        mask[z * size + x] = applySharpness(t, sharpness);
      }
    }
  } else {
    mask.fill(params.fallbackWeight ?? 0.5);
  }

  return mask;
}

function applySharpness(t, sharpness) {
  const s = smoothstep(t);
  if (sharpness <= 1) return s;
  return Math.pow(s, sharpness);
}

/**
 * Blend two height fields: h = (1 - W) · h_A + W · h_B
 */
export function blendHeights(heightsA, heightsB, weightMask) {
  const result = new Float32Array(heightsA.length);
  for (let i = 0; i < result.length; i++) {
    const w = smoothstep(weightMask[i]);
    result[i] = lerp(heightsA[i], heightsB[i], w);
  }
  return result;
}

/**
 * Blend biome vertex colors
 */
export function blendBiomeColors(colorsA, colorsB, weightMask) {
  const result = new Float32Array(colorsA.length);
  for (let i = 0; i < weightMask.length; i++) {
    const w = smoothstep(weightMask[i]);
    const i3 = i * 3;
    result[i3] = lerp(colorsA[i3], colorsB[i3], w);
    result[i3 + 1] = lerp(colorsA[i3 + 1], colorsB[i3 + 1], w);
    result[i3 + 2] = lerp(colorsA[i3 + 2], colorsB[i3 + 2], w);
  }
  return result;
}

/**
 * Slope-based rock band masking: steep slopes → rock color
 */
export function applySlopeRockMask(heights, biomeColors, size, cellSize, params) {
  const { cliffSlope, rockColor } = params;
  const result = new Float32Array(biomeColors);
  const [r, g, b] = rockColor;

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = z * size + x;
      const dhdx = (heights[i + 1] - heights[i - 1]) / (2 * cellSize);
      const dhdz = (heights[i + size] - heights[i - size]) / (2 * cellSize);
      const slope = Math.sqrt(dhdx * dhdx + dhdz * dhdz);

      if (slope > cliffSlope) {
        const blend = smoothstep(Math.min(1, (slope - cliffSlope) / cliffSlope));
        const i3 = i * 3;
        result[i3] = lerp(biomeColors[i3], r, blend);
        result[i3 + 1] = lerp(biomeColors[i3 + 1], g, blend);
        result[i3 + 2] = lerp(biomeColors[i3 + 2], b, blend);
      }
    }
  }

  return result;
}

export const DEFAULT_BLEND_PARAMS = {
  enabled: false,
  presetA: 'alpine',
  presetB: 'mesa',
  mask: {
    type: 'noise',
    scale: 80,
    offsetX: 0,
    offsetZ: 0,
    sharpness: 1.0,
    fallbackWeight: 0.5,
  },
  slopeMask: {
    enabled: true,
    cliffSlope: 0.95,
    rockColor: [0.50, 0.42, 0.36],
  },
};
