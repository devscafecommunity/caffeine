/**
 * Geomorphological PDE operators (v3.0)
 *
 * Caprock lithology, landslide slip planes, wind planar abrasion
 */

import { fBm, lithologicResistance } from './noise.js';
import { repairHeightArtifacts } from './smooth.js';

function idx(x, z, size) {
  return z * size + x;
}

function getHeight(heights, x, z, size) {
  return heights[idx(x, z, size)];
}

function worldCoords(x, z, size, cellSize) {
  const half = (cellSize * (size - 1)) / 2;
  return { worldX: x * cellSize - half, worldZ: z * cellSize - half };
}

function laplacian(heights, x, z, size) {
  const hC = getHeight(heights, x, z, size);
  return (
    getHeight(heights, x - 1, z, size) +
    getHeight(heights, x + 1, z, size) +
    getHeight(heights, x, z - 1, size) +
    getHeight(heights, x, z + 1, size) -
    4 * hC
  );
}

function gradient(heights, x, z, size, cellSize) {
  const dhdx =
    (getHeight(heights, x + 1, z, size) - getHeight(heights, x - 1, z, size)) / (2 * cellSize);
  const dhdz =
    (getHeight(heights, x, z + 1, size) - getHeight(heights, x, z - 1, size)) / (2 * cellSize);
  const slope = Math.sqrt(dhdx * dhdx + dhdz * dhdz);
  return { dhdx, dhdz, slope };
}

function steepestDownhill(heights, x, z, size, cellSize) {
  const hC = getHeight(heights, x, z, size);
  let maxDrop = 0;
  let bestIdx = -1;

  for (let dz = -1; dz <= 1; dz++) {
    for (let dx = -1; dx <= 1; dx++) {
      if (dx === 0 && dz === 0) continue;
      const nx = x + dx;
      const nz = z + dz;
      if (nx < 0 || nx >= size || nz < 0 || nz >= size) continue;

      const dist = Math.sqrt(dx * dx + dz * dz) * cellSize;
      const drop = (hC - getHeight(heights, nx, nz, size)) / dist;
      if (drop > maxDrop) {
        maxDrop = drop;
        bestIdx = idx(nx, nz, size);
      }
    }
  }

  return { idx: bestIdx, maxDrop };
}

function flowDirection(heights, x, z, size, cellSize) {
  const hC = getHeight(heights, x, z, size);
  let maxDrop = 0;
  let dirX = 0;
  let dirZ = 0;

  for (let dz = -1; dz <= 1; dz++) {
    for (let dx = -1; dx <= 1; dx++) {
      if (dx === 0 && dz === 0) continue;
      const nx = x + dx;
      const nz = z + dz;
      if (nx < 0 || nx >= size || nz < 0 || nz >= size) continue;

      const dist = Math.sqrt(dx * dx + dz * dz) * cellSize;
      const drop = (hC - getHeight(heights, nx, nz, size)) / dist;
      if (drop > maxDrop) {
        maxDrop = drop;
        dirX = dx;
        dirZ = dz;
      }
    }
  }

  return { dirX, dirZ };
}

function effectiveDrainage(accumulation, size) {
  return Math.log1p(accumulation / size);
}

function localMeanHeight(heights, x, z, size, radius) {
  let sum = 0;
  let count = 0;
  for (let dz = -radius; dz <= radius; dz++) {
    for (let dx = -radius; dx <= radius; dx++) {
      const nx = x + dx;
      const nz = z + dz;
      if (nx < 0 || nx >= size || nz < 0 || nz >= size) continue;
      sum += heights[idx(nx, nz, size)];
      count++;
    }
  }
  return sum / count;
}

function caprockDivisor(h, worldX, worldZ, lithology, noiseStrata) {
  const faultNoise =
    fBm(worldX, worldZ, lithology.noiseScale, lithology.octaves, noiseStrata) * lithology.noiseAmp;
  const strataPhase = Math.sin(lithology.kStrata * h + faultNoise);
  return strataPhase > lithology.caprockThreshold ? lithology.caprockHardness : 1.0;
}

export function computeFlowAccumulation(heights, size, precipitation = 1.0) {
  const accumulation = new Float32Array(size * size).fill(precipitation);
  const inDegree = new Int32Array(size * size);
  const receivers = new Array(size * size);

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = idx(x, z, size);
      const { dirX, dirZ } = flowDirection(heights, x, z, size, 1);
      if (dirX !== 0 || dirZ !== 0) {
        const receiver = idx(x + dirX, z + dirZ, size);
        receivers[i] = receiver;
        inDegree[receiver]++;
      } else {
        receivers[i] = -1;
      }
    }
  }

  const queue = [];
  for (let i = 0; i < size * size; i++) {
    if (inDegree[i] === 0) queue.push(i);
  }

  while (queue.length > 0) {
    const current = queue.shift();
    const receiver = receivers[current];
    if (receiver >= 0) {
      accumulation[receiver] += accumulation[current];
      inDegree[receiver]--;
      if (inDegree[receiver] === 0) queue.push(receiver);
    }
  }

  return accumulation;
}

export function tectonicUplift(x, y, params, noiseTectonic) {
  const { U0, kx, ky, alpha, p } = params;
  const phase = kx * x + ky * y + alpha * fBm(x, y, 80, 3, noiseTectonic);
  return U0 * Math.pow(Math.abs(Math.sin(phase)), p);
}

export function volcanicContribution(x, y, params) {
  const { centerX, centerY, Hv, sigma, calderaRadius, calderaDepth } = params;
  const dx = x - centerX;
  const dy = y - centerY;
  const dist = Math.sqrt(dx * dx + dy * dy);

  const cone = Hv * Math.exp(-dist / sigma);
  const caldera =
    calderaRadius > 0
      ? calderaDepth * Math.pow(Math.max(0, 1 - dist / calderaRadius), 2)
      : 0;

  return cone - caldera;
}

/**
 * Landslide / thermal slip operator
 * Δh_slip = -α·max(0, ‖∇h‖ - tan θ_c), mass deposited downhill
 */
function applyLandslideTransfer(heights, size, cellSize, params, lithology, noiseStrata) {
  const transfer = new Float32Array(size * size);
  const criticalSlope = Math.tan((params.thetaC * Math.PI) / 180);

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = idx(x, z, size);
      const hC = heights[i];
      const { worldX, worldZ } = worldCoords(x, z, size, cellSize);
      const { slope } = gradient(heights, x, z, size, cellSize);
      const excess = Math.max(0, slope - criticalSlope);
      if (excess <= 0) continue;

      const { idx: depositIdx, maxDrop } = steepestDownhill(heights, x, z, size, cellSize);
      if (depositIdx < 0 || maxDrop <= criticalSlope) continue;

      const capDiv = caprockDivisor(hC, worldX, worldZ, lithology, noiseStrata);
      const hDown = heights[depositIdx];
      const heightExcess = Math.max(0, hC - hDown - criticalSlope * cellSize);
      const amount = Math.min(
        (params.slipRate * (maxDrop - criticalSlope) * cellSize) / capDiv,
        heightExcess * params.maxFraction,
        params.maxTransfer
      );
      if (amount <= 1e-6) continue;

      transfer[i] -= amount;
      transfer[depositIdx] += amount * params.retention;
    }
  }

  return transfer;
}

export function applyLandslideFinalPass(heights, size, cellSize, params, lithology, noiseStrata) {
  if (!params.enabled) return heights;

  let current = new Float32Array(heights);
  for (let pass = 0; pass < params.finalPasses; pass++) {
    const transfer = applyLandslideTransfer(
      current,
      size,
      cellSize,
      params,
      lithology,
      noiseStrata
    );
    for (let i = 0; i < current.length; i++) {
      current[i] += transfer[i];
    }
  }

  return current;
}

export function applyHydraulicDetailPass(heights, size, cellSize, params, noiseMicro, precipitation) {
  if (!params.enabled) return heights;

  const result = new Float32Array(heights);
  const accumulation = computeFlowAccumulation(heights, size, precipitation);

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = idx(x, z, size);
      const { worldX, worldZ } = worldCoords(x, z, size, cellSize);
      const { slope } = gradient(heights, x, z, size, cellSize);
      const A = accumulation[i];

      if (slope < params.minSlope || A < params.minFlow) continue;

      const microNoise = fBm(worldX, worldZ, params.scale, params.octaves, noiseMicro);
      const detail = params.lambda * effectiveDrainage(A, size) * slope * microNoise;
      const maxDetail = params.maxDetail ?? 0.35;
      result[i] = heights[i] + Math.max(-maxDetail, Math.min(maxDetail, detail));
    }
  }

  return result;
}

/**
 * Incisão fluvial — sulcos e vales ao longo da rede de drenagem
 */
export function applyFluvialIncisionPass(heights, size, cellSize, params, precipitation) {
  if (!params.enabled) return heights;

  const { Kinc, m, n, minFlow } = params;
  const result = new Float32Array(heights);
  const accumulation = computeFlowAccumulation(heights, size, precipitation);

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = idx(x, z, size);
      const A = effectiveDrainage(accumulation[i], size);
      const { slope } = gradient(heights, x, z, size, cellSize);

      if (A < minFlow || slope < 0.01) {
        result[i] = heights[i];
        continue;
      }

      const incision = Kinc * Math.pow(A, m) * Math.pow(slope + 0.02, n);
      result[i] = heights[i] - incision;
    }
  }

  return result;
}

export function applyGeomorphologicalStep(heights, size, cellSize, params, noiseGenerators) {
  const next = new Float32Array(heights);
  const { Kd, Ke, Kw, m, n, precipitation, tectonic, glacial, lithology, wind, dt } = params;
  const { noiseTectonic, noiseStrata } = noiseGenerators;
  const accumulation = computeFlowAccumulation(heights, size, precipitation);

  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = idx(x, z, size);
      const { worldX, worldZ } = worldCoords(x, z, size, cellSize);

      const hC = heights[i];
      const lap = laplacian(heights, x, z, size);
      const { dhdx, dhdz, slope } = gradient(heights, x, z, size, cellSize);
      const Aeff = effectiveDrainage(accumulation[i], size);

      const diffusion = Kd * lap;

      const R = lithology.enabled
        ? lithologicResistance(worldX, worldZ, hC, lithology, noiseStrata)
        : 1.0;
      const erosion =
        (Ke / (R + lithology.epsilon)) * Math.pow(Aeff + 1e-6, m) * Math.pow(slope + 1e-6, n);

      const weathering = Kw * hC * Math.min(slope, 1);

      let windErosion = 0;
      if (wind.enabled) {
        const windDot = wind.windX * dhdx + wind.windZ * dhdz;
        const hLocal = localMeanHeight(heights, x, z, size, wind.localRadius);
        if (windDot > 0 && hC > hLocal) {
          windErosion = wind.Kwind * windDot * (hC - hLocal);
        }
      }

      let uplift = 0;
      if (tectonic.enabled) {
        uplift = tectonicUplift(worldX, worldZ, tectonic, noiseTectonic);
      }

      let glacialErosion = 0;
      if (glacial.enabled) {
        const iceThickness = Math.max(0, glacial.snowLine - hC);
        const iceVelocity = glacial.beta * iceThickness;
        glacialErosion = glacial.coeff * Math.pow(iceVelocity, glacial.k) * slope;
      }

      next[i] = hC + dt * (diffusion + uplift - erosion - weathering - glacialErosion - windErosion);
    }
  }

  return next;
}

export function applyVolcanicStructures(heights, size, cellSize, volcanic) {
  if (!volcanic.enabled) return heights;

  const result = new Float32Array(heights);
  const half = (cellSize * (size - 1)) / 2;

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      const worldX = x * cellSize - half;
      const worldZ = z * cellSize - half;
      result[i] = heights[i] + volcanicContribution(worldX, worldZ, volcanic);
    }
  }

  return result;
}

export function simulateGeomorphology(heights, size, cellSize, params, noiseGenerators) {
  let current = applyVolcanicStructures(heights, size, cellSize, params.volcanic);
  const history = [];

  for (let iter = 0; iter < params.iterations; iter++) {
    current = applyGeomorphologicalStep(current, size, cellSize, params, noiseGenerators);
    if (params.saveHistory && (iter % params.historyInterval === 0 || iter === params.iterations - 1)) {
      history.push({ iteration: iter, heights: new Float32Array(current) });
    }
  }

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

  return { heights: current, history };
}

export function clampHeightsToEnvelope(heights, reference, envelope) {
  const { marginBelow, marginAbove } = envelope;
  let refMin = Infinity;
  let refMax = -Infinity;

  for (let i = 0; i < reference.length; i++) {
    refMin = Math.min(refMin, reference[i]);
    refMax = Math.max(refMax, reference[i]);
  }

  const lo = refMin - marginBelow;
  const hi = refMax + marginAbove;
  const result = new Float32Array(heights.length);

  for (let i = 0; i < heights.length; i++) {
    const h = heights[i];
    result[i] = Number.isFinite(h) ? Math.max(lo, Math.min(hi, h)) : reference[i];
  }

  return result;
}

export const DEFAULT_GEOMORPH_PARAMS = {
  iterations: 15,
  dt: 1.0,
  Kd: 0.004,
  Ke: 0.007,
  Kw: 0.0005,
  m: 0.55,
  n: 1.15,
  artifactThreshold: 6.5,
  fluvialIncision: {
    enabled: true,
    Kinc: 0.12,
    m: 0.65,
    n: 0.9,
    minFlow: 0.08,
  },
  precipitation: 1.0,
  saveHistory: false,
  historyInterval: 5,
  lithology: {
    enabled: true,
    kz: 0.18,
    kStrata: 0.8,
    dipAngle: 28,
    strikeAngle: 42,
    noiseScale: 14,
    noiseAmp: 0.9,
    octaves: 4,
    epsilon: 0.08,
    Rbase: 0.5,
    deltaR: 0.5,
    caprockThreshold: 0.55,
    caprockHardness: 3.0,
  },
  landslide: {
    enabled: true,
    thetaC: 33,
    slipRate: 0.12,
    retention: 0.88,
    maxTransfer: 0.28,
    maxFraction: 0.1,
    finalPasses: 5,
  },
  wind: {
    enabled: true,
    Kwind: 0.002,
    windX: 1.0,
    windZ: 0.35,
    localRadius: 3,
  },
  hydraulicDetail: {
    enabled: true,
    lambda: 0.18,
    scale: 2.0,
    octaves: 5,
    minSlope: 0.03,
    minFlow: 0.25,
    maxDetail: 0.65,
  },
  heightEnvelope: {
    marginBelow: 4,
    marginAbove: 6,
  },
  tectonic: {
    enabled: true,
    U0: 0.02,
    kx: 0.08,
    ky: 0.05,
    alpha: 2.0,
    p: 1.5,
  },
  volcanic: {
    enabled: false,
    centerX: 0,
    centerY: 0,
    Hv: 20,
    sigma: 15,
    calderaRadius: 8,
    calderaDepth: 5,
  },
  glacial: {
    enabled: false,
    snowLine: 12,
    beta: 0.1,
    k: 1.5,
    coeff: 0.005,
  },
};
