/**
 * GPU-aware terrain pipeline — PDE on WebGPU, post-process on CPU
 */
import { createNoiseField } from './noise.js';
import { simulateGeomorphologyGpu } from './geomorphologyGpu.js';
import { computeBiomeFields, applyBiomeReliefShaping } from './biome.js';
import { buildHeightGrid } from './heightGrid.js';
import {
  generateWeightMask,
  blendHeights,
  applySlopeRockMask,
} from './blend.js';
import { resolveConfig } from './presets.js';
import { createNoiseGenerators } from './noise.js';

export async function runTerrainPipelineAsync(cfg, seedOffset = 0, options = {}) {
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
  const { heights } = await simulateGeomorphologyGpu(
    h0,
    gridSize,
    cellSize,
    cfg.geomorph,
    noiseGenerators
  );

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

  return {
    heights,
    h0,
    temperature: biomeResult.temperature,
    moisture: biomeResult.moisture,
    biomeColors: biomeResult.biomeColors,
    cellSize,
    activeAnchors: biomeResult.activeAnchors,
  };
}

export async function generateTerrainGpu(cfg) {
  const resolved = cfg.preset ? resolveConfig(cfg, cfg.preset) : cfg;
  const pipeline = await runTerrainPipelineAsync(resolved, 0);

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

  let minH = Infinity, maxH = -Infinity, sumH = 0;
  let minT = Infinity, maxT = -Infinity;
  let minM = Infinity, maxM = -Infinity;

  for (let i = 0; i < pipeline.heights.length; i++) {
    minH = Math.min(minH, pipeline.heights[i]);
    maxH = Math.max(maxH, pipeline.heights[i]);
    sumH += pipeline.heights[i];
    minT = Math.min(minT, pipeline.temperature[i]);
    maxT = Math.max(maxT, pipeline.temperature[i]);
    minM = Math.min(minM, pipeline.moisture[i]);
    maxM = Math.max(maxM, pipeline.moisture[i]);
  }

  return {
    heights: pipeline.heights,
    temperature: pipeline.temperature,
    moisture: pipeline.moisture,
    biomeColors,
    gridSize: cfg.gridSize,
    worldSize: cfg.worldSize,
    cellSize: pipeline.cellSize,
    stats: {
      elevation: { min: minH, max: maxH, mean: sumH / pipeline.heights.length },
      temperature: { min: minT, max: maxT },
      moisture: { min: minM, max: maxM },
    },
    blended: false,
    activeBiomes: pipeline.activeAnchors,
    gpuAccelerated: true,
  };
}

export async function generateBlendedTerrainAsync(cfg) {
  const { blend, gridSize, worldSize, seed } = cfg;

  const cfgA = resolveConfig(cfg, blend.presetA);
  const cfgB = resolveConfig(cfg, blend.presetB, { seed: seed + 7777 });

  const domainA = await runTerrainPipelineAsync(cfgA, 0, { skipReliefShaping: true });
  const domainB = await runTerrainPipelineAsync(cfgB, 7777, { skipReliefShaping: true });

  const weightMask = generateWeightMask(gridSize, domainA.cellSize, blend.mask, seed);
  const heights = blendHeights(domainA.heights, domainB.heights, weightMask);

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

  let biomeColors = biomeResult.biomeColors;
  if (blend.slopeMask?.enabled) {
    biomeColors = applySlopeRockMask(
      heights,
      biomeColors,
      gridSize,
      domainA.cellSize,
      blend.slopeMask
    );
  }

  let minH = Infinity, maxH = -Infinity, sumH = 0;
  let minT = Infinity, maxT = -Infinity;
  let minM = Infinity, maxM = -Infinity;
  const { temperature, moisture } = biomeResult;

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
    heights,
    weightMask,
    temperature,
    moisture,
    biomeColors,
    gridSize,
    worldSize,
    cellSize: domainA.cellSize,
    stats: {
      elevation: { min: minH, max: maxH, mean: sumH / heights.length },
      temperature: { min: minT, max: maxT },
      moisture: { min: minM, max: maxM },
    },
    blended: true,
    domains: { a: blend.presetA, b: blend.presetB },
    activeBiomes: biomeResult.activeAnchors,
    gpuAccelerated: true,
  };
}
