import { createNoiseGenerators, createNoiseField } from './noise.js';
import { simulateGeomorphology, DEFAULT_GEOMORPH_PARAMS } from './geomorphology.js';
import { computeBiomeFields, applyBiomeReliefShaping, DEFAULT_BIOME_PARAMS } from './biome.js';
import { buildHeightGrid } from './heightGrid.js';
import {
  generateWeightMask,
  blendHeights,
  applySlopeRockMask,
  DEFAULT_BLEND_PARAMS,
} from './blend.js';
import { resolveConfig } from './presets.js';

export const DEFAULT_NOISE_PARAMS = {
  warpScale: 40,
  warpStrength: 12,
  warpOctaves: 3,
  baseScale: 60,
  baseHeight: 15,
  baseOctaves: 5,
};

export const REFERENCE_WORLD_SIZE = 100;

export const GRID_SIZE_OPTIONS = [96, 128, 192, 256, 320, 384, 512, 640, 768, 1024];

export const DEFAULT_CONFIG = {
  gridSize: 192,
  worldSize: 100,
  seed: 42,
  noise: DEFAULT_NOISE_PARAMS,
  geomorph: DEFAULT_GEOMORPH_PARAMS,
  biome: DEFAULT_BIOME_PARAMS,
  blend: DEFAULT_BLEND_PARAMS,
};

/** ~1 vértice por unidade de mundo — adequado para mapas 500×500 → grid 512 */
export function recommendedGridSize(worldSize) {
  const target = Math.round(worldSize);
  return GRID_SIZE_OPTIONS.reduce((best, s) =>
    Math.abs(s - target) < Math.abs(best - target) ? s : best
  );
}

export function scaleConfigForWorldSize(cfg) {
  const factor = cfg.worldSize / REFERENCE_WORLD_SIZE;
  if (Math.abs(factor - 1) < 0.01) return cfg;

  const scaled = deepMerge(cfg, {
    noise: {
      warpScale: cfg.noise.warpScale * factor,
      warpStrength: cfg.noise.warpStrength * factor,
      baseScale: cfg.noise.baseScale * factor,
      baseHeight: cfg.noise.baseHeight * factor,
    },
    biome: {
      macroClimateScale: (cfg.biome.macroClimateScale ?? 200) * factor,
      snowLine: cfg.biome.snowLine * factor,
      temperature: { noiseScale: cfg.biome.temperature.noiseScale * factor },
      moisture: { noiseScale: cfg.biome.moisture.noiseScale * factor },
    },
  });

  if (scaled.blend?.mask) {
    scaled.blend = deepMerge(scaled.blend, {
      mask: { scale: (scaled.blend.mask.scale ?? 80) * factor },
    });
  }

  return scaled;
}

function runTerrainPipeline(cfg, seedOffset = 0, options = {}) {
  const seed = cfg.seed + seedOffset;
  const { gridSize, worldSize } = cfg;

  const noiseGenerators = createNoiseGenerators({
    warpX: seed + 1000,
    warpY: seed + 2000,
    base: seed,
    tectonic: seed + 500,
    strata: seed + 6000,
    micro: seed + 7000,
  });

  const { heights: h0, cellSize } = buildHeightGrid(gridSize, worldSize, cfg.noise, noiseGenerators);
  const { heights } = simulateGeomorphology(h0, gridSize, cellSize, cfg.geomorph, noiseGenerators);

  const noiseTemp = createNoiseField(seed + 3000);
  const noiseMoisture = createNoiseField(seed + 4000);
  if (!options.skipReliefShaping) {
    applyBiomeReliefShaping(heights, gridSize, cellSize, cfg.biome, noiseTemp, noiseMoisture);
  }
  const biomeResult = computeBiomeFields(
    heights,
    gridSize,
    cellSize,
    cfg.biome,
    noiseTemp,
    noiseMoisture
  );
  const { temperature, moisture, biomeColors, activeAnchors } = biomeResult;

  return {
    heights,
    h0,
    temperature,
    moisture,
    biomeColors,
    cellSize,
    noiseTemp,
    noiseMoisture,
    activeAnchors,
  };
}

export function generateTerrain(config = {}) {
  const cfg = scaleConfigForWorldSize(deepMerge(DEFAULT_CONFIG, config));

  console.log('[1/4] Domain warping — computing h₀(x,y)...');
  console.log(`[2/4] Geomorphological PDE — ${cfg.geomorph.iterations} iterations...`);

  const pipeline = runTerrainPipeline(cfg);

  console.log('[3/4] Biome classification — T(x,y), M(x,y)...');

  let biomeColors = pipeline.biomeColors;
  if (cfg.blend?.slopeMask?.enabled) {
    biomeColors = applySlopeRockMask(
      pipeline.heights,
      biomeColors,
      cfg.gridSize,
      pipeline.cellSize,
      cfg.blend.slopeMask
    );
  }

  const stats = computeStats(pipeline.heights, pipeline.temperature, pipeline.moisture);

  return {
    heights: pipeline.heights,
    h0: pipeline.h0,
    temperature: pipeline.temperature,
    moisture: pipeline.moisture,
    biomeColors,
    gridSize: cfg.gridSize,
    worldSize: cfg.worldSize,
    cellSize: pipeline.cellSize,
    config: cfg,
    stats,
    blended: false,
    activeBiomes: pipeline.activeAnchors,
  };
}

/**
 * Multi-domain blend: h_final = (1-W)·h_A + W·h_B
 * Independent node trees per preset, merged via weight mask
 */
export function generateBlendedTerrain(config = {}) {
  const cfg = scaleConfigForWorldSize(deepMerge(DEFAULT_CONFIG, config));
  const { blend, gridSize, worldSize, seed } = cfg;

  const cfgA = resolveConfig(cfg, blend.presetA);
  const cfgB = resolveConfig(cfg, blend.presetB, { seed: seed + 7777 });

  console.log(`[Blend] Domain A: ${blend.presetA}`);
  console.log('[1/6] Pipeline A — domain warping + PDE...');
  const domainA = runTerrainPipeline(cfgA, 0, { skipReliefShaping: true });

  console.log(`[Blend] Domain B: ${blend.presetB}`);
  console.log('[2/6] Pipeline B — domain warping + PDE...');
  const domainB = runTerrainPipeline(cfgB, 7777, { skipReliefShaping: true });

  console.log('[3/6] Weight mask W(x,y)...');
  const weightMask = generateWeightMask(gridSize, domainA.cellSize, blend.mask, seed);

  console.log('[4/6] Blending h_A and h_B via smoothstep...');
  const heights = blendHeights(domainA.heights, domainB.heights, weightMask);

  console.log('[5/6] Biome relief shaping + classification...');
  const noiseTemp = createNoiseField(seed + 3000);
  const noiseMoisture = createNoiseField(seed + 4000);

  const domainRelief = new Float32Array(gridSize * gridSize);
  for (let i = 0; i < domainRelief.length; i++) {
    const w = weightMask[i] * weightMask[i] * (3 - 2 * weightMask[i]);
    domainRelief[i] = (1 - w) * 0.88 + w * 0.52;
  }
  applyBiomeReliefShaping(heights, gridSize, domainA.cellSize, cfg.biome, noiseTemp, noiseMoisture, {
    domainRelief,
  });
  const biomeResult = computeBiomeFields(
    heights,
    gridSize,
    domainA.cellSize,
    cfg.biome,
    noiseTemp,
    noiseMoisture
  );
  const { temperature, moisture, activeAnchors } = biomeResult;
  let biomeColors = biomeResult.biomeColors;

  if (blend.slopeMask?.enabled) {
    biomeColors = applySlopeRockMask(heights, biomeColors, gridSize, domainA.cellSize, blend.slopeMask);
  }

  const stats = computeStats(heights, temperature, moisture);

  return {
    heights,
    weightMask,
    temperature,
    moisture,
    biomeColors,
    gridSize,
    worldSize,
    cellSize: domainA.cellSize,
    config: cfg,
    stats,
    blended: true,
    domains: { a: blend.presetA, b: blend.presetB },
    activeBiomes: activeAnchors,
  };
}

export function generateTerrainAuto(config = {}) {
  const cfg = deepMerge(DEFAULT_CONFIG, config);
  if (cfg.blend?.enabled) {
    return generateBlendedTerrain(cfg);
  }
  return generateTerrain(cfg);
}

function computeStats(heights, temperature, moisture) {
  let minH = Infinity, maxH = -Infinity, sumH = 0;
  let minT = Infinity, maxT = -Infinity;
  let minM = Infinity, maxM = -Infinity;

  for (let i = 0; i < heights.length; i++) {
    minH = Math.min(minH, heights[i]);
    maxH = Math.max(maxH, heights[i]);
    sumH += heights[i];
    minT = Math.min(minT, temperature[i]);
    maxT = Math.max(maxT, temperature[i]);
    minM = Math.min(minM, moisture[i]);
    maxM = Math.max(maxM, moisture[i]);
  }

  return {
    elevation: { min: minH, max: maxH, mean: sumH / heights.length },
    temperature: { min: minT, max: maxT },
    moisture: { min: minM, max: maxM },
  };
}

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
