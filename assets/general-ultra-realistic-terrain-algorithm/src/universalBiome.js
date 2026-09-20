/**
 * Universal Biome System — Holdridge/Whittaker extended parametric space
 *
 * B(x,y) = (T, P, D, S, R) ∈ [0,1]⁵
 * Biome properties via Gaussian IDW over anchor centroids
 */

import { fBm } from './noise.js';
import { computeFlowAccumulation } from './geomorphology.js';
import { boxBlurField, repairHeightArtifacts } from './smooth.js';

function temperatureField(x, y, height, params, noiseTemp) {
  const { T0, lapseRate, noiseScale, noiseAmp } = params;
  const noise = fBm(x, y, noiseScale, 3, noiseTemp) * noiseAmp;
  return T0 - lapseRate * height + noise;
}

function moistureField(x, y, heights, size, cellSize, params, noiseMoisture) {
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

/**
 * Biome anchors in normalized parametric space (T, P, D, S, R)
 * T=temperature, P=precipitation, D=drainage, S=slope, R=roughness
 */
export const BIOME_ANCHORS = [
  // Tropical — relief: 0=flat plain, 1=mountainous
  { id: 'tropical_rainforest', T: 0.95, P: 0.98, D: 0.55, S: 0.25, R: 0.35, relief: 0.35, color: [0.04, 0.55, 0.14], erosionMult: 1.8, KdMult: 0.9 },
  { id: 'tropical_seasonal_rainforest', T: 0.90, P: 0.75, D: 0.50, S: 0.30, R: 0.30, relief: 0.32, color: [0.10, 0.58, 0.18], erosionMult: 1.6, KdMult: 0.95 },
  { id: 'tropical_seasonal_deciduous', T: 0.88, P: 0.60, D: 0.45, S: 0.28, R: 0.28, relief: 0.28, color: [0.22, 0.62, 0.16], erosionMult: 1.4, KdMult: 1.0 },
  { id: 'tropical_seasonal_semideciduous', T: 0.86, P: 0.55, D: 0.42, S: 0.30, R: 0.30, relief: 0.28, color: [0.28, 0.60, 0.20], erosionMult: 1.35, KdMult: 1.0 },
  { id: 'tropical_freshwater_swamp', T: 0.88, P: 0.92, D: 0.92, S: 0.05, R: 0.15, relief: 0.06, color: [0.12, 0.42, 0.22], erosionMult: 0.4, KdMult: 1.2 },
  { id: 'mangrove_swamp', T: 0.85, P: 0.85, D: 0.98, S: 0.02, R: 0.10, relief: 0.04, color: [0.18, 0.45, 0.30], erosionMult: 0.3, KdMult: 1.3 },
  { id: 'tropical_desert', T: 0.98, P: 0.03, D: 0.05, S: 0.25, R: 0.40, relief: 0.12, color: [0.92, 0.82, 0.52], erosionMult: 0.05, KdMult: 1.5 },

  // Temperate forests
  { id: 'temperate_giant_rainforest', T: 0.72, P: 0.90, D: 0.60, S: 0.35, R: 0.40, relief: 0.38, color: [0.06, 0.50, 0.16], erosionMult: 1.7, KdMult: 0.85 },
  { id: 'montane_rainforest', T: 0.55, P: 0.88, D: 0.50, S: 0.75, R: 0.55, relief: 0.78, color: [0.14, 0.48, 0.28], erosionMult: 1.5, KdMult: 0.9 },
  { id: 'temperate_deciduous_forest', T: 0.60, P: 0.65, D: 0.45, S: 0.30, R: 0.25, relief: 0.30, color: [0.30, 0.62, 0.14], erosionMult: 1.1, KdMult: 1.0 },
  { id: 'temperate_evergreen_needleleaf', T: 0.45, P: 0.55, D: 0.40, S: 0.35, R: 0.30, relief: 0.35, color: [0.10, 0.42, 0.22], erosionMult: 0.9, KdMult: 1.05 },
  { id: 'temperate_evergreen_sclerophyll', T: 0.65, P: 0.45, D: 0.35, S: 0.40, R: 0.35, relief: 0.32, color: [0.38, 0.55, 0.18], erosionMult: 0.7, KdMult: 1.1 },
  { id: 'temperate_freshwater_swamp', T: 0.50, P: 0.80, D: 0.90, S: 0.05, R: 0.12, relief: 0.06, color: [0.20, 0.45, 0.25], erosionMult: 0.35, KdMult: 1.25 },
  { id: 'temperate_woodland', T: 0.58, P: 0.50, D: 0.38, S: 0.25, R: 0.22, relief: 0.22, color: [0.55, 0.68, 0.28], erosionMult: 0.85, KdMult: 1.05 },

  // Shrublands & thorn
  { id: 'thorn_forest', T: 0.78, P: 0.25, D: 0.20, S: 0.30, R: 0.35, relief: 0.18, color: [0.72, 0.62, 0.30], erosionMult: 0.25, KdMult: 1.3 },
  { id: 'thorn_scrub', T: 0.75, P: 0.15, D: 0.15, S: 0.28, R: 0.32, relief: 0.15, color: [0.80, 0.70, 0.38], erosionMult: 0.18, KdMult: 1.35 },
  { id: 'temperate_shrubland_deciduous', T: 0.52, P: 0.40, D: 0.30, S: 0.32, R: 0.28, relief: 0.25, color: [0.65, 0.68, 0.32], erosionMult: 0.6, KdMult: 1.15 },
  { id: 'temperate_shrubland_heath', T: 0.48, P: 0.55, D: 0.35, S: 0.30, R: 0.25, relief: 0.22, color: [0.58, 0.62, 0.35], erosionMult: 0.55, KdMult: 1.1 },
  { id: 'temperate_shrubland_sclerophyll', T: 0.62, P: 0.35, D: 0.28, S: 0.38, R: 0.32, relief: 0.28, color: [0.52, 0.58, 0.28], erosionMult: 0.5, KdMult: 1.2 },
  { id: 'temperate_shrubland_subalpine_needleleaf', T: 0.32, P: 0.45, D: 0.35, S: 0.65, R: 0.45, relief: 0.72, color: [0.35, 0.50, 0.32], erosionMult: 0.65, KdMult: 1.0 },
  { id: 'temperate_shrubland_subalpine_broadleaf', T: 0.35, P: 0.50, D: 0.38, S: 0.60, R: 0.42, relief: 0.68, color: [0.40, 0.52, 0.30], erosionMult: 0.7, KdMult: 0.95 },

  // Grasslands & savanna
  { id: 'savanna', T: 0.82, P: 0.35, D: 0.30, S: 0.20, R: 0.22, relief: 0.14, color: [0.82, 0.75, 0.32], erosionMult: 0.45, KdMult: 1.2 },
  { id: 'temperate_grassland', T: 0.55, P: 0.42, D: 0.32, S: 0.18, R: 0.18, relief: 0.12, color: [0.72, 0.78, 0.35], erosionMult: 0.55, KdMult: 1.1 },
  { id: 'alpine_grassland', T: 0.22, P: 0.38, D: 0.28, S: 0.70, R: 0.50, relief: 0.82, color: [0.55, 0.65, 0.38], erosionMult: 0.8, KdMult: 0.95 },

  // Cold / alpine
  { id: 'taiga_subalpine_needleleaf', T: 0.25, P: 0.38, D: 0.35, S: 0.35, R: 0.30, relief: 0.38, color: [0.12, 0.35, 0.25], erosionMult: 0.45, KdMult: 1.1 },
  { id: 'elfin_woodland', T: 0.30, P: 0.88, D: 0.55, S: 0.88, R: 0.60, relief: 0.88, color: [0.25, 0.48, 0.35], erosionMult: 0.75, KdMult: 0.85 },
  { id: 'tundra', T: 0.12, P: 0.30, D: 0.25, S: 0.25, R: 0.20, relief: 0.18, color: [0.62, 0.65, 0.52], erosionMult: 0.35, KdMult: 1.4 },
  { id: 'arctic_alpine_desert', T: 0.05, P: 0.08, D: 0.10, S: 0.55, R: 0.65, relief: 0.55, color: [0.78, 0.76, 0.72], erosionMult: 0.15, KdMult: 1.6 },

  // Deserts
  { id: 'warm_temperate_desert', T: 0.80, P: 0.08, D: 0.08, S: 0.30, R: 0.38, relief: 0.10, color: [0.90, 0.78, 0.50], erosionMult: 0.08, KdMult: 1.45 },
  { id: 'cool_temperate_desert_scrub', T: 0.55, P: 0.12, D: 0.12, S: 0.35, R: 0.40, relief: 0.14, color: [0.82, 0.72, 0.48], erosionMult: 0.12, KdMult: 1.4 },

  // Wetlands
  { id: 'bog', T: 0.35, P: 0.72, D: 0.88, S: 0.03, R: 0.10, relief: 0.05, color: [0.28, 0.38, 0.22], erosionMult: 0.15, KdMult: 1.3 },
  { id: 'salt_marsh', T: 0.60, P: 0.65, D: 0.85, S: 0.02, R: 0.08, relief: 0.04, color: [0.55, 0.58, 0.42], erosionMult: 0.2, KdMult: 1.25 },
  { id: 'wetland', T: 0.50, P: 0.75, D: 0.80, S: 0.04, R: 0.12, relief: 0.06, color: [0.32, 0.48, 0.28], erosionMult: 0.25, KdMult: 1.2 },

  // Snow / ice cap (high altitude override)
  { id: 'snow_ice', T: 0.02, P: 0.50, D: 0.30, S: 0.50, R: 0.30, relief: 0.92, color: [0.96, 0.98, 1.0], erosionMult: 0.1, KdMult: 1.8 },
];

/** Grupos pré-definidos de biomas para iteração rápida */
export const BIOME_GROUPS = {
  all: null,
  tropical: [
    'tropical_rainforest',
    'tropical_seasonal_rainforest',
    'tropical_seasonal_deciduous',
    'tropical_seasonal_semideciduous',
    'tropical_freshwater_swamp',
    'mangrove_swamp',
    'tropical_desert',
  ],
  temperate: [
    'temperate_giant_rainforest',
    'montane_rainforest',
    'temperate_deciduous_forest',
    'temperate_evergreen_needleleaf',
    'temperate_evergreen_sclerophyll',
    'temperate_freshwater_swamp',
    'temperate_woodland',
    'temperate_shrubland_deciduous',
    'temperate_shrubland_heath',
    'temperate_shrubland_sclerophyll',
    'temperate_shrubland_subalpine_needleleaf',
    'temperate_shrubland_subalpine_broadleaf',
    'temperate_grassland',
  ],
  arid: [
    'tropical_desert',
    'warm_temperate_desert',
    'cool_temperate_desert_scrub',
    'thorn_forest',
    'thorn_scrub',
    'arctic_alpine_desert',
    'savanna',
  ],
  cold: [
    'taiga_subalpine_needleleaf',
    'elfin_woodland',
    'tundra',
    'arctic_alpine_desert',
    'alpine_grassland',
    'snow_ice',
    'temperate_shrubland_subalpine_needleleaf',
    'temperate_shrubland_subalpine_broadleaf',
  ],
  wetland: [
    'bog',
    'wetland',
    'salt_marsh',
    'mangrove_swamp',
    'tropical_freshwater_swamp',
    'temperate_freshwater_swamp',
  ],
  montane: ['montane_rainforest', 'elfin_woodland', 'alpine_grassland', 'snow_ice'],
};

const DEFAULT_WEIGHTS = { wT: 2.0, wP: 2.0, wD: 1.5, wS: 1.0, wR: 0.8, sharpness: 8.0 };
const COLOR_SHARPNESS = 5.0;
const DOMINANT_THRESHOLD = 0.28;

const RELIEF_BY_ID = new Map(BIOME_ANCHORS.map((b) => [b.id, b.relief ?? 0.5]));

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function saturateColor(rgb, factor = 1.35) {
  const lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
  return rgb.map((c) => clamp01(lum + (c - lum) * factor));
}

/**
 * Resolve which biome anchors are active for this generation
 * @param {{ biomeGroup?: string, enabledBiomes?: string[] | null }} params
 */
export function resolveActiveAnchors(params = {}) {
  const { biomeGroup = 'all', enabledBiomes = null } = params;

  if (Array.isArray(enabledBiomes) && enabledBiomes.length > 0) {
    const idSet = new Set(enabledBiomes);
    const filtered = BIOME_ANCHORS.filter((b) => idSet.has(b.id));
    if (filtered.length > 0) return filtered;
  }

  if (biomeGroup && biomeGroup !== 'all' && BIOME_GROUPS[biomeGroup]) {
    const idSet = new Set(BIOME_GROUPS[biomeGroup]);
    const filtered = BIOME_ANCHORS.filter((b) => idSet.has(b.id));
    if (filtered.length > 0) return filtered;
  }

  return BIOME_ANCHORS;
}

export function getBiomeGroupCatalog() {
  return Object.keys(BIOME_GROUPS).map((id) => ({
    id,
    count: BIOME_GROUPS[id] ? BIOME_GROUPS[id].length : BIOME_ANCHORS.length,
  }));
}

function clamp01(v) {
  return Math.max(0, Math.min(1, v));
}

function normalizeTemperature(celsius, params) {
  const { min = -25, max = 35 } = params;
  return clamp01((celsius - min) / (max - min));
}

function normalizePrecipitation(moisture, params) {
  const { min = -0.5, max = 3.0 } = params;
  return clamp01((moisture - min) / (max - min));
}

function normalizeDrainage(accumulation, size) {
  return clamp01(Math.log1p(accumulation / size) / Math.log1p(50));
}

function localRoughness(heights, x, z, size) {
  let sum = 0;
  let count = 0;
  const hC = heights[z * size + x];
  for (let dz = -1; dz <= 1; dz++) {
    for (let dx = -1; dx <= 1; dx++) {
      const nx = x + dx;
      const nz = z + dz;
      if (nx < 0 || nx >= size || nz < 0 || nz >= size) continue;
      sum += Math.abs(heights[nz * size + nx] - hC);
      count++;
    }
  }
  return clamp01((sum / count) * 2.5);
}

/**
 * Environmental vector B(x,y) = (T, P, D, S, R)
 */
export function getEnvironmentalVector(heights, x, z, size, cellSize, fields, params) {
  const i = z * size + x;
  const T = normalizeTemperature(fields.temperature[i], params.tNorm);
  const P = normalizePrecipitation(fields.moisture[i], params.pNorm);

  let dhdx = 0;
  let dhdz = 0;
  if (x > 0 && x < size - 1 && z > 0 && z < size - 1) {
    dhdx = (heights[i + 1] - heights[i - 1]) / (2 * cellSize);
    dhdz = (heights[i + size] - heights[i - size]) / (2 * cellSize);
  }
  const S = clamp01(Math.sqrt(dhdx * dhdx + dhdz * dhdz) / (params.maxSlope ?? 1.2));
  const D = normalizeDrainage(fields.drainage[i], size);
  const R = localRoughness(heights, x, z, size);

  return { T, P, D, S, R };
}

/**
 * Gaussian IDW biome weights — Σ w_i = 1
 */
export function evaluateBiomeWeights(env, anchors = BIOME_ANCHORS, weights = DEFAULT_WEIGHTS) {
  const result = new Float32Array(anchors.length);
  let total = 0;

  for (let i = 0; i < anchors.length; i++) {
    const b = anchors[i];
    const dT = env.T - b.T;
    const dP = env.P - b.P;
    const dD = env.D - b.D;
    const dS = env.S - b.S;
    const dR = env.R - b.R;

    const distSq =
      weights.wT * dT * dT +
      weights.wP * dP * dP +
      weights.wD * dD * dD +
      weights.wS * dS * dS +
      weights.wR * dR * dR;

    const w = Math.exp(-distSq * weights.sharpness);
    result[i] = w;
    total += w;
  }

  if (total > 0) {
    for (let i = 0; i < result.length; i++) result[i] /= total;
  }

  return result;
}

/**
 * Sharp color from dominant biome — avoids muddy averaging of 32 greens
 */
function resolveBiomeColor(weights, anchors) {
  let dominantIdx = 0;
  let maxW = 0;
  let sharpTotal = 0;
  const sharp = new Float32Array(anchors.length);

  for (let i = 0; i < anchors.length; i++) {
    const w = Math.pow(weights[i], COLOR_SHARPNESS);
    sharp[i] = w;
    sharpTotal += w;
    if (weights[i] > maxW) {
      maxW = weights[i];
      dominantIdx = i;
    }
  }

  if (maxW >= DOMINANT_THRESHOLD) {
    return saturateColor(anchors[dominantIdx].color);
  }

  const color = [0, 0, 0];
  if (sharpTotal > 0) {
    for (let i = 0; i < anchors.length; i++) {
      const w = sharp[i] / sharpTotal;
      color[0] += anchors[i].color[0] * w;
      color[1] += anchors[i].color[1] * w;
      color[2] += anchors[i].color[2] * w;
    }
  }
  return saturateColor(color);
}

/**
 * Interpolate color, erosionMult, KdMult from weight distribution
 */
export function interpolateBiomeProperties(weights, anchors = BIOME_ANCHORS) {
  let erosionMult = 0;
  let KdMult = 0;
  let dominantIdx = 0;
  let maxW = 0;

  for (let i = 0; i < anchors.length; i++) {
    const w = weights[i];
    const b = anchors[i];
    erosionMult += b.erosionMult * w;
    KdMult += b.KdMult * w;
    if (w > maxW) {
      maxW = w;
      dominantIdx = i;
    }
  }

  return {
    color: resolveBiomeColor(weights, anchors),
    erosionMult,
    KdMult,
    dominantId: anchors[dominantIdx].id,
    dominantWeight: maxW,
  };
}

/**
 * Shape terrain relief to match biome — suavizado para evitar blocos afundados
 */
export function applyBiomeReliefShaping(
  heights,
  size,
  cellSize,
  params,
  noiseTemp,
  noiseMoisture,
  options = {}
) {
  if (params.reliefShaping?.enabled === false) return;

  const pass1 = computeUniversalBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture);
  const flatReliefCutoff = params.reliefShaping?.flatReliefCutoff ?? 0.32;
  const blurRadius = Math.max(4, Math.floor(size / 24));
  const regionalMean = boxBlurField(heights, size, blurRadius);
  const flatAmp = params.reliefShaping?.flatAmplitude ?? 4.0;
  const strength = params.reliefShaping?.strength ?? 0.22;
  const domainRelief = options.domainRelief;

  for (let i = 0; i < heights.length; i++) {
    let relief = RELIEF_BY_ID.get(pass1.dominantBiome[i]) ?? 0.5;
    if (domainRelief) {
      relief = relief < 0.22 ? relief : Math.max(relief, domainRelief[i]);
    }

    // Montanhas/colinas: preservar erosão — só planícies/áridos são suavizados
    if (relief >= flatReliefCutoff) continue;

    const t = relief / flatReliefCutoff;
    const maxAmp = lerp(flatAmp * 0.6, flatAmp, t);
    const damp = lerp(0.55, 0.9, t);
    const dev = heights[i] - regionalMean[i];
    const target = regionalMean[i] + Math.max(-maxAmp, Math.min(maxAmp, dev * damp));
    heights[i] += (target - heights[i]) * strength * (1 - t * 0.5);
  }

  repairHeightArtifacts(heights, size, params.reliefShaping?.artifactThreshold ?? 6.0);
}

/**
 * Full universal biome field computation
 */
export function computeUniversalBiomeFields(
  heights,
  size,
  cellSize,
  params,
  noiseTemp,
  noiseMoisture
) {
  const temperature = new Float32Array(size * size);
  const moisture = moistureField(0, 0, heights, size, cellSize, params.moisture, noiseMoisture);
  const drainage = computeFlowAccumulation(heights, size, params.precipitation ?? 1.0);
  const biomeColors = new Float32Array(size * size * 3);
  const erosionMult = new Float32Array(size * size);
  const KdMult = new Float32Array(size * size);
  const dominantBiome = new Array(size * size);

  const half = (cellSize * (size - 1)) / 2;
  const envParams = {
    tNorm: params.tNorm ?? { min: -25, max: 35 },
    pNorm: params.pNorm ?? { min: -0.5, max: 3.0 },
    maxSlope: params.maxSlope ?? 1.2,
  };

  const activeAnchors = resolveActiveAnchors(params);

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      const worldX = x * cellSize - half;
      const worldZ = z * cellSize - half;

      temperature[i] = temperatureField(worldX, worldZ, heights[i], params.temperature, noiseTemp);

      const fields = { temperature, moisture, drainage };
      let env = getEnvironmentalVector(heights, x, z, size, cellSize, fields, envParams);

      const macroScale = params.macroClimateScale ?? 200;
      const macroStrength = params.macroClimateStrength ?? 0.42;
      const macroT = fBm(worldX, worldZ, macroScale, 3, noiseTemp) * 0.5 + 0.5;
      const macroP = fBm(worldX + 8000, worldZ + 3000, macroScale, 3, noiseMoisture) * 0.5 + 0.5;
      env = {
        ...env,
        T: clamp01(env.T * (1 - macroStrength) + macroT * macroStrength),
        P: clamp01(env.P * (1 - macroStrength) + macroP * macroStrength),
      };

      if (heights[i] > params.snowLine) {
        env = { ...env, T: 0.02, S: Math.max(env.S, 0.5) };
      }

      const weights = evaluateBiomeWeights(env, activeAnchors);
      const props = interpolateBiomeProperties(weights, activeAnchors);

      biomeColors[i * 3] = props.color[0];
      biomeColors[i * 3 + 1] = props.color[1];
      biomeColors[i * 3 + 2] = props.color[2];
      erosionMult[i] = props.erosionMult;
      KdMult[i] = props.KdMult;
      dominantBiome[i] = props.dominantId;
    }
  }

  return {
    temperature,
    moisture,
    drainage,
    biomeColors,
    erosionMult,
    KdMult,
    dominantBiome,
    activeAnchors: activeAnchors.map((b) => b.id),
  };
}

export function getBiomeCatalog() {
  return BIOME_ANCHORS.map((b) => ({
    id: b.id,
    T: b.T,
    P: b.P,
    D: b.D,
    S: b.S,
    R: b.R,
    relief: b.relief,
    color: b.color,
  }));
}
