var __defProp = Object.defineProperty;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __esm = (fn, res) => function __init() {
  return fn && (res = (0, fn[__getOwnPropNames(fn)[0]])(fn = 0)), res;
};
var __export = (target, all) => {
  for (var name in all)
    __defProp(target, name, { get: all[name], enumerable: true });
};

// node_modules/.pnpm/simplex-noise@4.0.3/node_modules/simplex-noise/dist/esm/simplex-noise.js
function createNoise2D(random = Math.random) {
  const perm = buildPermutationTable(random);
  const permGrad2x = new Float64Array(perm).map((v) => grad2[v % 12 * 2]);
  const permGrad2y = new Float64Array(perm).map((v) => grad2[v % 12 * 2 + 1]);
  return function noise2D(x, y) {
    let n0 = 0;
    let n1 = 0;
    let n2 = 0;
    const s = (x + y) * F2;
    const i = fastFloor(x + s);
    const j = fastFloor(y + s);
    const t = (i + j) * G2;
    const X0 = i - t;
    const Y0 = j - t;
    const x0 = x - X0;
    const y0 = y - Y0;
    let i1, j1;
    if (x0 > y0) {
      i1 = 1;
      j1 = 0;
    } else {
      i1 = 0;
      j1 = 1;
    }
    const x1 = x0 - i1 + G2;
    const y1 = y0 - j1 + G2;
    const x2 = x0 - 1 + 2 * G2;
    const y2 = y0 - 1 + 2 * G2;
    const ii = i & 255;
    const jj = j & 255;
    let t0 = 0.5 - x0 * x0 - y0 * y0;
    if (t0 >= 0) {
      const gi0 = ii + perm[jj];
      const g0x = permGrad2x[gi0];
      const g0y = permGrad2y[gi0];
      t0 *= t0;
      n0 = t0 * t0 * (g0x * x0 + g0y * y0);
    }
    let t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 >= 0) {
      const gi1 = ii + i1 + perm[jj + j1];
      const g1x = permGrad2x[gi1];
      const g1y = permGrad2y[gi1];
      t1 *= t1;
      n1 = t1 * t1 * (g1x * x1 + g1y * y1);
    }
    let t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 >= 0) {
      const gi2 = ii + 1 + perm[jj + 1];
      const g2x = permGrad2x[gi2];
      const g2y = permGrad2y[gi2];
      t2 *= t2;
      n2 = t2 * t2 * (g2x * x2 + g2y * y2);
    }
    return 70 * (n0 + n1 + n2);
  };
}
function buildPermutationTable(random) {
  const tableSize = 512;
  const p = new Uint8Array(tableSize);
  for (let i = 0; i < tableSize / 2; i++) {
    p[i] = i;
  }
  for (let i = 0; i < tableSize / 2 - 1; i++) {
    const r = i + ~~(random() * (256 - i));
    const aux = p[i];
    p[i] = p[r];
    p[r] = aux;
  }
  for (let i = 256; i < tableSize; i++) {
    p[i] = p[i - 256];
  }
  return p;
}
var SQRT3, SQRT5, F2, G2, F3, G3, F4, G4, fastFloor, grad2;
var init_simplex_noise = __esm({
  "node_modules/.pnpm/simplex-noise@4.0.3/node_modules/simplex-noise/dist/esm/simplex-noise.js"() {
    SQRT3 = /* @__PURE__ */ Math.sqrt(3);
    SQRT5 = /* @__PURE__ */ Math.sqrt(5);
    F2 = 0.5 * (SQRT3 - 1);
    G2 = (3 - SQRT3) / 6;
    F3 = 1 / 3;
    G3 = 1 / 6;
    F4 = (SQRT5 - 1) / 4;
    G4 = (5 - SQRT5) / 20;
    fastFloor = (x) => Math.floor(x) | 0;
    grad2 = /* @__PURE__ */ new Float64Array([
      1,
      1,
      -1,
      1,
      1,
      -1,
      -1,
      -1,
      1,
      0,
      -1,
      0,
      1,
      0,
      -1,
      0,
      0,
      1,
      0,
      -1,
      0,
      1,
      0,
      -1
    ]);
  }
});

// src/noise.js
function seededRandom(seed) {
  let s = seed;
  return () => {
    s = (s * 16807 + 0) % 2147483647;
    return (s - 1) / 2147483646;
  };
}
function createNoiseField(seed) {
  return createNoise2D(seededRandom(seed));
}
function fBm(x, y, scale, octaves, noiseFn) {
  let value = 0;
  let amplitude = 1;
  let frequency = 1 / scale;
  let maxAmp = 0;
  for (let i = 0; i < octaves; i++) {
    value += noiseFn(x * frequency, y * frequency) * amplitude;
    maxAmp += amplitude;
    amplitude *= 0.5;
    frequency *= 2;
  }
  return value / maxAmp;
}
function displacementField(x, y, params, noiseWarpX, noiseWarpY) {
  const { warpScale, warpOctaves } = params;
  const psiX = fBm(x, y, warpScale, warpOctaves, noiseWarpX);
  const psiY = fBm(x, y, warpScale, warpOctaves, noiseWarpY);
  return { psiX, psiY };
}
function initialHeight(x, y, params, noiseWarpX, noiseWarpY, noiseBase) {
  const { warpStrength, baseScale, baseHeight, baseOctaves } = params;
  const { psiX, psiY } = displacementField(x, y, params, noiseWarpX, noiseWarpY);
  const warpedX = x + psiX * warpStrength;
  const warpedY = y + psiY * warpStrength;
  const h0 = fBm(warpedX, warpedY, baseScale, baseOctaves, noiseBase);
  return h0 * baseHeight;
}
function lithologicResistance(x, y, height, params, noiseStrata) {
  const {
    kz,
    kStrata,
    dipAngle,
    strikeAngle,
    noiseScale,
    noiseAmp,
    octaves,
    epsilon,
    Rbase,
    deltaR,
    caprockThreshold,
    caprockHardness
  } = params;
  const strikeRad = strikeAngle * Math.PI / 180;
  const dipRad = dipAngle * Math.PI / 180;
  const alongStrike = x * Math.cos(strikeRad) + y * Math.sin(strikeRad);
  const faultNoise = fBm(x, y, noiseScale, octaves, noiseStrata) * noiseAmp;
  const strataPhase = Math.sin(kStrata * height + faultNoise);
  const layerStep = strataPhase > 0 ? 1 : 0.35;
  const layerR = Rbase + deltaR * layerStep;
  const dipPhase = kz * height + dipRad * alongStrike + faultNoise * 0.5;
  const dipSin = Math.sin(dipPhase);
  const dipR = epsilon + (1 - epsilon) * dipSin * dipSin;
  let R = layerR * dipR;
  if (strataPhase > caprockThreshold) {
    R *= caprockHardness;
  }
  return Math.max(epsilon, R);
}
function createNoiseGenerators(seeds = {}) {
  const {
    warpX = 1e3,
    warpY = 2e3,
    base = 0,
    tectonic = 500,
    strata = 6e3,
    micro = 7e3
  } = seeds;
  return {
    noiseWarpX: createNoiseField(warpX),
    noiseWarpY: createNoiseField(warpY),
    noiseBase: createNoiseField(base),
    noiseTectonic: createNoiseField(tectonic),
    noiseStrata: createNoiseField(strata),
    noiseMicro: createNoiseField(micro)
  };
}
var init_noise = __esm({
  "src/noise.js"() {
    init_simplex_noise();
  }
});

// src/smooth.js
function repairHeightArtifacts(heights, size, jumpThreshold = 5) {
  const next = new Float32Array(heights);
  for (let z = 1; z < size - 1; z++) {
    for (let x = 1; x < size - 1; x++) {
      const i = z * size + x;
      const h = heights[i];
      let neighborSum = 0;
      let neighborCount = 0;
      let maxJump = 0;
      for (let dz = -1; dz <= 1; dz++) {
        for (let dx = -1; dx <= 1; dx++) {
          if (dx === 0 && dz === 0) continue;
          const ni = (z + dz) * size + (x + dx);
          neighborSum += heights[ni];
          neighborCount++;
          maxJump = Math.max(maxJump, Math.abs(heights[ni] - h));
        }
      }
      if (maxJump > jumpThreshold) {
        const neighborMean = neighborSum / neighborCount;
        const blend = Math.min(0.75, (maxJump - jumpThreshold) / jumpThreshold);
        next[i] = h * (1 - blend) + neighborMean * blend;
      } else {
        next[i] = h;
      }
    }
  }
  heights.set(next);
}
function boxBlurField(field, size, radius) {
  const result = new Float32Array(field.length);
  const diam = radius * 2 + 1;
  const norm = 1 / (diam * diam);
  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      let sum = 0;
      for (let dz = -radius; dz <= radius; dz++) {
        for (let dx = -radius; dx <= radius; dx++) {
          const nx = Math.max(0, Math.min(size - 1, x + dx));
          const nz = Math.max(0, Math.min(size - 1, z + dz));
          sum += field[nz * size + nx];
        }
      }
      result[z * size + x] = sum * norm;
    }
  }
  return result;
}
var init_smooth = __esm({
  "src/smooth.js"() {
  }
});

// src/geomorphology.js
var geomorphology_exports = {};
__export(geomorphology_exports, {
  DEFAULT_GEOMORPH_PARAMS: () => DEFAULT_GEOMORPH_PARAMS,
  applyFluvialIncisionPass: () => applyFluvialIncisionPass,
  applyGeomorphologicalStep: () => applyGeomorphologicalStep,
  applyHydraulicDetailPass: () => applyHydraulicDetailPass,
  applyLandslideFinalPass: () => applyLandslideFinalPass,
  applyVolcanicStructures: () => applyVolcanicStructures,
  clampHeightsToEnvelope: () => clampHeightsToEnvelope,
  computeFlowAccumulation: () => computeFlowAccumulation,
  simulateGeomorphology: () => simulateGeomorphology,
  tectonicUplift: () => tectonicUplift,
  volcanicContribution: () => volcanicContribution
});
function idx(x, z, size) {
  return z * size + x;
}
function getHeight(heights, x, z, size) {
  return heights[idx(x, z, size)];
}
function worldCoords(x, z, size, cellSize) {
  const half = cellSize * (size - 1) / 2;
  return { worldX: x * cellSize - half, worldZ: z * cellSize - half };
}
function laplacian(heights, x, z, size) {
  const hC = getHeight(heights, x, z, size);
  return getHeight(heights, x - 1, z, size) + getHeight(heights, x + 1, z, size) + getHeight(heights, x, z - 1, size) + getHeight(heights, x, z + 1, size) - 4 * hC;
}
function gradient(heights, x, z, size, cellSize) {
  const dhdx = (getHeight(heights, x + 1, z, size) - getHeight(heights, x - 1, z, size)) / (2 * cellSize);
  const dhdz = (getHeight(heights, x, z + 1, size) - getHeight(heights, x, z - 1, size)) / (2 * cellSize);
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
  const faultNoise = fBm(worldX, worldZ, lithology.noiseScale, lithology.octaves, noiseStrata) * lithology.noiseAmp;
  const strataPhase = Math.sin(lithology.kStrata * h + faultNoise);
  return strataPhase > lithology.caprockThreshold ? lithology.caprockHardness : 1;
}
function computeFlowAccumulation(heights, size, precipitation = 1) {
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
function tectonicUplift(x, y, params, noiseTectonic) {
  const { U0, kx, ky, alpha, p } = params;
  const phase = kx * x + ky * y + alpha * fBm(x, y, 80, 3, noiseTectonic);
  return U0 * Math.pow(Math.abs(Math.sin(phase)), p);
}
function volcanicContribution(x, y, params) {
  const { centerX, centerY, Hv, sigma, calderaRadius, calderaDepth } = params;
  const dx = x - centerX;
  const dy = y - centerY;
  const dist = Math.sqrt(dx * dx + dy * dy);
  const cone = Hv * Math.exp(-dist / sigma);
  const caldera = calderaRadius > 0 ? calderaDepth * Math.pow(Math.max(0, 1 - dist / calderaRadius), 2) : 0;
  return cone - caldera;
}
function applyLandslideTransfer(heights, size, cellSize, params, lithology, noiseStrata) {
  const transfer = new Float32Array(size * size);
  const criticalSlope = Math.tan(params.thetaC * Math.PI / 180);
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
        params.slipRate * (maxDrop - criticalSlope) * cellSize / capDiv,
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
function applyLandslideFinalPass(heights, size, cellSize, params, lithology, noiseStrata) {
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
function applyHydraulicDetailPass(heights, size, cellSize, params, noiseMicro, precipitation) {
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
function applyFluvialIncisionPass(heights, size, cellSize, params, precipitation) {
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
function applyGeomorphologicalStep(heights, size, cellSize, params, noiseGenerators) {
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
      const R = lithology.enabled ? lithologicResistance(worldX, worldZ, hC, lithology, noiseStrata) : 1;
      const erosion = Ke / (R + lithology.epsilon) * Math.pow(Aeff + 1e-6, m) * Math.pow(slope + 1e-6, n);
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
function applyVolcanicStructures(heights, size, cellSize, volcanic) {
  if (!volcanic.enabled) return heights;
  const result = new Float32Array(heights);
  const half = cellSize * (size - 1) / 2;
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
function simulateGeomorphology(heights, size, cellSize, params, noiseGenerators) {
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
function clampHeightsToEnvelope(heights, reference, envelope) {
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
var DEFAULT_GEOMORPH_PARAMS;
var init_geomorphology = __esm({
  "src/geomorphology.js"() {
    init_noise();
    init_smooth();
    DEFAULT_GEOMORPH_PARAMS = {
      iterations: 15,
      dt: 1,
      Kd: 4e-3,
      Ke: 7e-3,
      Kw: 5e-4,
      m: 0.55,
      n: 1.15,
      artifactThreshold: 6.5,
      fluvialIncision: {
        enabled: true,
        Kinc: 0.12,
        m: 0.65,
        n: 0.9,
        minFlow: 0.08
      },
      precipitation: 1,
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
        caprockHardness: 3
      },
      landslide: {
        enabled: true,
        thetaC: 33,
        slipRate: 0.12,
        retention: 0.88,
        maxTransfer: 0.28,
        maxFraction: 0.1,
        finalPasses: 5
      },
      wind: {
        enabled: true,
        Kwind: 2e-3,
        windX: 1,
        windZ: 0.35,
        localRadius: 3
      },
      hydraulicDetail: {
        enabled: true,
        lambda: 0.18,
        scale: 2,
        octaves: 5,
        minSlope: 0.03,
        minFlow: 0.25,
        maxDetail: 0.65
      },
      heightEnvelope: {
        marginBelow: 4,
        marginAbove: 6
      },
      tectonic: {
        enabled: true,
        U0: 0.02,
        kx: 0.08,
        ky: 0.05,
        alpha: 2,
        p: 1.5
      },
      volcanic: {
        enabled: false,
        centerX: 0,
        centerY: 0,
        Hv: 20,
        sigma: 15,
        calderaRadius: 8,
        calderaDepth: 5
      },
      glacial: {
        enabled: false,
        snowLine: 12,
        beta: 0.1,
        k: 1.5,
        coeff: 5e-3
      }
    };
  }
});

// src/universalBiome.js
function temperatureField(x, y, height, params, noiseTemp) {
  const { T0, lapseRate, noiseScale, noiseAmp } = params;
  const noise = fBm(x, y, noiseScale, 3, noiseTemp) * noiseAmp;
  return T0 - lapseRate * height + noise;
}
function moistureField(x, y, heights, size, cellSize, params, noiseMoisture) {
  const { M0, windX, windZ, condensation, evaporation, noiseAmp, noiseScale } = params;
  const moisture = new Float32Array(size * size);
  for (let z = 0; z < size; z++) {
    for (let x2 = 0; x2 < size; x2++) {
      const i = z * size + x2;
      const worldX = (x2 / (size - 1) - 0.5) * cellSize * (size - 1);
      const worldZ = (z / (size - 1) - 0.5) * cellSize * (size - 1);
      const noise = fBm(worldX, worldZ, noiseScale, 3, noiseMoisture) * noiseAmp;
      let dhdx = 0;
      let dhdz = 0;
      if (x2 > 0 && x2 < size - 1 && z > 0 && z < size - 1) {
        dhdx = (heights[i + 1] - heights[i - 1]) / (2 * cellSize);
        dhdz = (heights[i + size] - heights[i - size]) / (2 * cellSize);
      }
      const windDotGrad = windX * dhdx + windZ * dhdz;
      const orographic = condensation * Math.max(0, windDotGrad) - evaporation * Math.max(0, -windDotGrad);
      moisture[i] = M0 + orographic + noise;
    }
  }
  return moisture;
}
function lerp(a, b, t) {
  return a + (b - a) * t;
}
function saturateColor(rgb, factor = 1.35) {
  const lum = 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2];
  return rgb.map((c) => clamp01(lum + (c - lum) * factor));
}
function resolveActiveAnchors(params = {}) {
  const { biomeGroup = "all", enabledBiomes = null } = params;
  if (Array.isArray(enabledBiomes) && enabledBiomes.length > 0) {
    const idSet = new Set(enabledBiomes);
    const filtered = BIOME_ANCHORS.filter((b) => idSet.has(b.id));
    if (filtered.length > 0) return filtered;
  }
  if (biomeGroup && biomeGroup !== "all" && BIOME_GROUPS[biomeGroup]) {
    const idSet = new Set(BIOME_GROUPS[biomeGroup]);
    const filtered = BIOME_ANCHORS.filter((b) => idSet.has(b.id));
    if (filtered.length > 0) return filtered;
  }
  return BIOME_ANCHORS;
}
function clamp01(v) {
  return Math.max(0, Math.min(1, v));
}
function normalizeTemperature(celsius, params) {
  const { min = -25, max = 35 } = params;
  return clamp01((celsius - min) / (max - min));
}
function normalizePrecipitation(moisture, params) {
  const { min = -0.5, max = 3 } = params;
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
  return clamp01(sum / count * 2.5);
}
function getEnvironmentalVector(heights, x, z, size, cellSize, fields, params) {
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
function evaluateBiomeWeights(env, anchors = BIOME_ANCHORS, weights = DEFAULT_WEIGHTS) {
  const result = new Float32Array(anchors.length);
  let total = 0;
  for (let i = 0; i < anchors.length; i++) {
    const b = anchors[i];
    const dT = env.T - b.T;
    const dP = env.P - b.P;
    const dD = env.D - b.D;
    const dS = env.S - b.S;
    const dR = env.R - b.R;
    const distSq = weights.wT * dT * dT + weights.wP * dP * dP + weights.wD * dD * dD + weights.wS * dS * dS + weights.wR * dR * dR;
    const w = Math.exp(-distSq * weights.sharpness);
    result[i] = w;
    total += w;
  }
  if (total > 0) {
    for (let i = 0; i < result.length; i++) result[i] /= total;
  }
  return result;
}
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
function interpolateBiomeProperties(weights, anchors = BIOME_ANCHORS) {
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
    dominantWeight: maxW
  };
}
function applyBiomeReliefShaping(heights, size, cellSize, params, noiseTemp, noiseMoisture, options = {}) {
  if (params.reliefShaping?.enabled === false) return;
  const pass1 = computeUniversalBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture);
  const flatReliefCutoff = params.reliefShaping?.flatReliefCutoff ?? 0.32;
  const blurRadius = Math.max(4, Math.floor(size / 24));
  const regionalMean = boxBlurField(heights, size, blurRadius);
  const flatAmp = params.reliefShaping?.flatAmplitude ?? 4;
  const strength = params.reliefShaping?.strength ?? 0.22;
  const domainRelief = options.domainRelief;
  for (let i = 0; i < heights.length; i++) {
    let relief = RELIEF_BY_ID.get(pass1.dominantBiome[i]) ?? 0.5;
    if (domainRelief) {
      relief = relief < 0.22 ? relief : Math.max(relief, domainRelief[i]);
    }
    if (relief >= flatReliefCutoff) continue;
    const t = relief / flatReliefCutoff;
    const maxAmp = lerp(flatAmp * 0.6, flatAmp, t);
    const damp = lerp(0.55, 0.9, t);
    const dev = heights[i] - regionalMean[i];
    const target = regionalMean[i] + Math.max(-maxAmp, Math.min(maxAmp, dev * damp));
    heights[i] += (target - heights[i]) * strength * (1 - t * 0.5);
  }
  repairHeightArtifacts(heights, size, params.reliefShaping?.artifactThreshold ?? 6);
}
function computeUniversalBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture) {
  const temperature = new Float32Array(size * size);
  const moisture = moistureField(0, 0, heights, size, cellSize, params.moisture, noiseMoisture);
  const drainage = computeFlowAccumulation(heights, size, params.precipitation ?? 1);
  const biomeColors = new Float32Array(size * size * 3);
  const erosionMult = new Float32Array(size * size);
  const KdMult = new Float32Array(size * size);
  const dominantBiome = new Array(size * size);
  const half = cellSize * (size - 1) / 2;
  const envParams = {
    tNorm: params.tNorm ?? { min: -25, max: 35 },
    pNorm: params.pNorm ?? { min: -0.5, max: 3 },
    maxSlope: params.maxSlope ?? 1.2
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
      const macroP = fBm(worldX + 8e3, worldZ + 3e3, macroScale, 3, noiseMoisture) * 0.5 + 0.5;
      env = {
        ...env,
        T: clamp01(env.T * (1 - macroStrength) + macroT * macroStrength),
        P: clamp01(env.P * (1 - macroStrength) + macroP * macroStrength)
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
    activeAnchors: activeAnchors.map((b) => b.id)
  };
}
var BIOME_ANCHORS, BIOME_GROUPS, DEFAULT_WEIGHTS, COLOR_SHARPNESS, DOMINANT_THRESHOLD, RELIEF_BY_ID;
var init_universalBiome = __esm({
  "src/universalBiome.js"() {
    init_noise();
    init_geomorphology();
    init_smooth();
    BIOME_ANCHORS = [
      // Tropical — relief: 0=flat plain, 1=mountainous
      { id: "tropical_rainforest", T: 0.95, P: 0.98, D: 0.55, S: 0.25, R: 0.35, relief: 0.35, color: [0.04, 0.55, 0.14], erosionMult: 1.8, KdMult: 0.9 },
      { id: "tropical_seasonal_rainforest", T: 0.9, P: 0.75, D: 0.5, S: 0.3, R: 0.3, relief: 0.32, color: [0.1, 0.58, 0.18], erosionMult: 1.6, KdMult: 0.95 },
      { id: "tropical_seasonal_deciduous", T: 0.88, P: 0.6, D: 0.45, S: 0.28, R: 0.28, relief: 0.28, color: [0.22, 0.62, 0.16], erosionMult: 1.4, KdMult: 1 },
      { id: "tropical_seasonal_semideciduous", T: 0.86, P: 0.55, D: 0.42, S: 0.3, R: 0.3, relief: 0.28, color: [0.28, 0.6, 0.2], erosionMult: 1.35, KdMult: 1 },
      { id: "tropical_freshwater_swamp", T: 0.88, P: 0.92, D: 0.92, S: 0.05, R: 0.15, relief: 0.06, color: [0.12, 0.42, 0.22], erosionMult: 0.4, KdMult: 1.2 },
      { id: "mangrove_swamp", T: 0.85, P: 0.85, D: 0.98, S: 0.02, R: 0.1, relief: 0.04, color: [0.18, 0.45, 0.3], erosionMult: 0.3, KdMult: 1.3 },
      { id: "tropical_desert", T: 0.98, P: 0.03, D: 0.05, S: 0.25, R: 0.4, relief: 0.12, color: [0.92, 0.82, 0.52], erosionMult: 0.05, KdMult: 1.5 },
      // Temperate forests
      { id: "temperate_giant_rainforest", T: 0.72, P: 0.9, D: 0.6, S: 0.35, R: 0.4, relief: 0.38, color: [0.06, 0.5, 0.16], erosionMult: 1.7, KdMult: 0.85 },
      { id: "montane_rainforest", T: 0.55, P: 0.88, D: 0.5, S: 0.75, R: 0.55, relief: 0.78, color: [0.14, 0.48, 0.28], erosionMult: 1.5, KdMult: 0.9 },
      { id: "temperate_deciduous_forest", T: 0.6, P: 0.65, D: 0.45, S: 0.3, R: 0.25, relief: 0.3, color: [0.3, 0.62, 0.14], erosionMult: 1.1, KdMult: 1 },
      { id: "temperate_evergreen_needleleaf", T: 0.45, P: 0.55, D: 0.4, S: 0.35, R: 0.3, relief: 0.35, color: [0.1, 0.42, 0.22], erosionMult: 0.9, KdMult: 1.05 },
      { id: "temperate_evergreen_sclerophyll", T: 0.65, P: 0.45, D: 0.35, S: 0.4, R: 0.35, relief: 0.32, color: [0.38, 0.55, 0.18], erosionMult: 0.7, KdMult: 1.1 },
      { id: "temperate_freshwater_swamp", T: 0.5, P: 0.8, D: 0.9, S: 0.05, R: 0.12, relief: 0.06, color: [0.2, 0.45, 0.25], erosionMult: 0.35, KdMult: 1.25 },
      { id: "temperate_woodland", T: 0.58, P: 0.5, D: 0.38, S: 0.25, R: 0.22, relief: 0.22, color: [0.55, 0.68, 0.28], erosionMult: 0.85, KdMult: 1.05 },
      // Shrublands & thorn
      { id: "thorn_forest", T: 0.78, P: 0.25, D: 0.2, S: 0.3, R: 0.35, relief: 0.18, color: [0.72, 0.62, 0.3], erosionMult: 0.25, KdMult: 1.3 },
      { id: "thorn_scrub", T: 0.75, P: 0.15, D: 0.15, S: 0.28, R: 0.32, relief: 0.15, color: [0.8, 0.7, 0.38], erosionMult: 0.18, KdMult: 1.35 },
      { id: "temperate_shrubland_deciduous", T: 0.52, P: 0.4, D: 0.3, S: 0.32, R: 0.28, relief: 0.25, color: [0.65, 0.68, 0.32], erosionMult: 0.6, KdMult: 1.15 },
      { id: "temperate_shrubland_heath", T: 0.48, P: 0.55, D: 0.35, S: 0.3, R: 0.25, relief: 0.22, color: [0.58, 0.62, 0.35], erosionMult: 0.55, KdMult: 1.1 },
      { id: "temperate_shrubland_sclerophyll", T: 0.62, P: 0.35, D: 0.28, S: 0.38, R: 0.32, relief: 0.28, color: [0.52, 0.58, 0.28], erosionMult: 0.5, KdMult: 1.2 },
      { id: "temperate_shrubland_subalpine_needleleaf", T: 0.32, P: 0.45, D: 0.35, S: 0.65, R: 0.45, relief: 0.72, color: [0.35, 0.5, 0.32], erosionMult: 0.65, KdMult: 1 },
      { id: "temperate_shrubland_subalpine_broadleaf", T: 0.35, P: 0.5, D: 0.38, S: 0.6, R: 0.42, relief: 0.68, color: [0.4, 0.52, 0.3], erosionMult: 0.7, KdMult: 0.95 },
      // Grasslands & savanna
      { id: "savanna", T: 0.82, P: 0.35, D: 0.3, S: 0.2, R: 0.22, relief: 0.14, color: [0.82, 0.75, 0.32], erosionMult: 0.45, KdMult: 1.2 },
      { id: "temperate_grassland", T: 0.55, P: 0.42, D: 0.32, S: 0.18, R: 0.18, relief: 0.12, color: [0.72, 0.78, 0.35], erosionMult: 0.55, KdMult: 1.1 },
      { id: "alpine_grassland", T: 0.22, P: 0.38, D: 0.28, S: 0.7, R: 0.5, relief: 0.82, color: [0.55, 0.65, 0.38], erosionMult: 0.8, KdMult: 0.95 },
      // Cold / alpine
      { id: "taiga_subalpine_needleleaf", T: 0.25, P: 0.38, D: 0.35, S: 0.35, R: 0.3, relief: 0.38, color: [0.12, 0.35, 0.25], erosionMult: 0.45, KdMult: 1.1 },
      { id: "elfin_woodland", T: 0.3, P: 0.88, D: 0.55, S: 0.88, R: 0.6, relief: 0.88, color: [0.25, 0.48, 0.35], erosionMult: 0.75, KdMult: 0.85 },
      { id: "tundra", T: 0.12, P: 0.3, D: 0.25, S: 0.25, R: 0.2, relief: 0.18, color: [0.62, 0.65, 0.52], erosionMult: 0.35, KdMult: 1.4 },
      { id: "arctic_alpine_desert", T: 0.05, P: 0.08, D: 0.1, S: 0.55, R: 0.65, relief: 0.55, color: [0.78, 0.76, 0.72], erosionMult: 0.15, KdMult: 1.6 },
      // Deserts
      { id: "warm_temperate_desert", T: 0.8, P: 0.08, D: 0.08, S: 0.3, R: 0.38, relief: 0.1, color: [0.9, 0.78, 0.5], erosionMult: 0.08, KdMult: 1.45 },
      { id: "cool_temperate_desert_scrub", T: 0.55, P: 0.12, D: 0.12, S: 0.35, R: 0.4, relief: 0.14, color: [0.82, 0.72, 0.48], erosionMult: 0.12, KdMult: 1.4 },
      // Wetlands
      { id: "bog", T: 0.35, P: 0.72, D: 0.88, S: 0.03, R: 0.1, relief: 0.05, color: [0.28, 0.38, 0.22], erosionMult: 0.15, KdMult: 1.3 },
      { id: "salt_marsh", T: 0.6, P: 0.65, D: 0.85, S: 0.02, R: 0.08, relief: 0.04, color: [0.55, 0.58, 0.42], erosionMult: 0.2, KdMult: 1.25 },
      { id: "wetland", T: 0.5, P: 0.75, D: 0.8, S: 0.04, R: 0.12, relief: 0.06, color: [0.32, 0.48, 0.28], erosionMult: 0.25, KdMult: 1.2 },
      // Snow / ice cap (high altitude override)
      { id: "snow_ice", T: 0.02, P: 0.5, D: 0.3, S: 0.5, R: 0.3, relief: 0.92, color: [0.96, 0.98, 1], erosionMult: 0.1, KdMult: 1.8 }
    ];
    BIOME_GROUPS = {
      all: null,
      tropical: [
        "tropical_rainforest",
        "tropical_seasonal_rainforest",
        "tropical_seasonal_deciduous",
        "tropical_seasonal_semideciduous",
        "tropical_freshwater_swamp",
        "mangrove_swamp",
        "tropical_desert"
      ],
      temperate: [
        "temperate_giant_rainforest",
        "montane_rainforest",
        "temperate_deciduous_forest",
        "temperate_evergreen_needleleaf",
        "temperate_evergreen_sclerophyll",
        "temperate_freshwater_swamp",
        "temperate_woodland",
        "temperate_shrubland_deciduous",
        "temperate_shrubland_heath",
        "temperate_shrubland_sclerophyll",
        "temperate_shrubland_subalpine_needleleaf",
        "temperate_shrubland_subalpine_broadleaf",
        "temperate_grassland"
      ],
      arid: [
        "tropical_desert",
        "warm_temperate_desert",
        "cool_temperate_desert_scrub",
        "thorn_forest",
        "thorn_scrub",
        "arctic_alpine_desert",
        "savanna"
      ],
      cold: [
        "taiga_subalpine_needleleaf",
        "elfin_woodland",
        "tundra",
        "arctic_alpine_desert",
        "alpine_grassland",
        "snow_ice",
        "temperate_shrubland_subalpine_needleleaf",
        "temperate_shrubland_subalpine_broadleaf"
      ],
      wetland: [
        "bog",
        "wetland",
        "salt_marsh",
        "mangrove_swamp",
        "tropical_freshwater_swamp",
        "temperate_freshwater_swamp"
      ],
      montane: ["montane_rainforest", "elfin_woodland", "alpine_grassland", "snow_ice"]
    };
    DEFAULT_WEIGHTS = { wT: 2, wP: 2, wD: 1.5, wS: 1, wR: 0.8, sharpness: 8 };
    COLOR_SHARPNESS = 5;
    DOMINANT_THRESHOLD = 0.28;
    RELIEF_BY_ID = new Map(BIOME_ANCHORS.map((b) => [b.id, b.relief ?? 0.5]));
  }
});

// src/biome.js
function computeBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture) {
  return computeUniversalBiomeFields(heights, size, cellSize, params, noiseTemp, noiseMoisture);
}
var DEFAULT_BIOME_PARAMS;
var init_biome = __esm({
  "src/biome.js"() {
    init_noise();
    init_universalBiome();
    DEFAULT_BIOME_PARAMS = {
      snowLine: 14,
      precipitation: 1,
      maxSlope: 1.2,
      biomeGroup: "all",
      enabledBiomes: null,
      reliefShaping: {
        enabled: true,
        flatAmplitude: 4,
        flatReliefCutoff: 0.32,
        strength: 0.22,
        artifactThreshold: 6
      },
      tNorm: { min: -25, max: 35 },
      pNorm: { min: -0.5, max: 3 },
      temperature: { T0: 25, lapseRate: 0.6, noiseScale: 180, noiseAmp: 8 },
      moisture: {
        M0: 0.5,
        windX: 1,
        windZ: 0.3,
        condensation: 0.8,
        evaporation: 0.5,
        noiseScale: 150,
        noiseAmp: 0.35
      }
    };
  }
});

// src/heightGrid.js
function buildHeightGrid(size, worldSize, noiseParams, noiseGenerators) {
  const { noiseWarpX, noiseWarpY, noiseBase } = noiseGenerators;
  const heights = new Float32Array(size * size);
  const half = worldSize / 2;
  const cellSize = worldSize / (size - 1);
  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const worldX = x * cellSize - half;
      const worldZ = z * cellSize - half;
      const h = initialHeight(worldX, worldZ, noiseParams, noiseWarpX, noiseWarpY, noiseBase);
      heights[z * size + x] = h;
    }
  }
  return { heights, cellSize };
}
var init_heightGrid = __esm({
  "src/heightGrid.js"() {
    init_noise();
  }
});

// src/blend.js
function smoothstep(t) {
  const x = Math.max(0, Math.min(1, t));
  return x * x * (3 - 2 * x);
}
function lerp2(a, b, t) {
  return a + (b - a) * t;
}
function generateWeightMask(size, cellSize, params, seed) {
  const mask = new Float32Array(size * size);
  const half = cellSize * (size - 1) / 2;
  const { type, scale, offsetX, offsetZ, sharpness } = params;
  if (type === "noise") {
    const noise = createNoiseField(seed + 9e3);
    for (let z = 0; z < size; z++) {
      for (let x = 0; x < size; x++) {
        const worldX = x * cellSize - half;
        const worldZ = z * cellSize - half;
        const raw = fBm(worldX + offsetX, worldZ + offsetZ, scale, 4, noise);
        const normalized = (raw + 1) * 0.5;
        mask[z * size + x] = applySharpness(normalized, sharpness);
      }
    }
  } else if (type === "gradient") {
    for (let z = 0; z < size; z++) {
      for (let x = 0; x < size; x++) {
        const t = x / (size - 1);
        mask[z * size + x] = applySharpness(t, sharpness);
      }
    }
  } else if (type === "radial") {
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
function blendHeights(heightsA, heightsB, weightMask) {
  const result = new Float32Array(heightsA.length);
  for (let i = 0; i < result.length; i++) {
    const w = smoothstep(weightMask[i]);
    result[i] = lerp2(heightsA[i], heightsB[i], w);
  }
  return result;
}
function applySlopeRockMask(heights, biomeColors, size, cellSize, params) {
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
        result[i3] = lerp2(biomeColors[i3], r, blend);
        result[i3 + 1] = lerp2(biomeColors[i3 + 1], g, blend);
        result[i3 + 2] = lerp2(biomeColors[i3 + 2], b, blend);
      }
    }
  }
  return result;
}
var DEFAULT_BLEND_PARAMS;
var init_blend = __esm({
  "src/blend.js"() {
    init_noise();
    DEFAULT_BLEND_PARAMS = {
      enabled: false,
      presetA: "alpine",
      presetB: "mesa",
      mask: {
        type: "noise",
        scale: 80,
        offsetX: 0,
        offsetZ: 0,
        sharpness: 1,
        fallbackWeight: 0.5
      },
      slopeMask: {
        enabled: true,
        cliffSlope: 0.95,
        rockColor: [0.5, 0.42, 0.36]
      }
    };
  }
});

// src/presets.js
function getPresetConfig(name) {
  return PRESETS[name] ? { ...PRESETS[name] } : {};
}
function resolveConfig(baseConfig, presetName, overrides = {}) {
  const preset = getPresetConfig(presetName);
  const { label: _label, ...presetCfg } = preset;
  return deepMerge(deepMerge(baseConfig, presetCfg), overrides);
}
function deepMerge(target, source) {
  const result = { ...target };
  for (const key of Object.keys(source)) {
    if (source[key] && typeof source[key] === "object" && !Array.isArray(source[key])) {
      result[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      result[key] = source[key];
    }
  }
  return result;
}
var PRESETS;
var init_presets = __esm({
  "src/presets.js"() {
    PRESETS = {
      default: {
        label: "Default (eros\xE3o + tect\xF4nica)"
      },
      volcanic: {
        label: "Volcanic (cone + caldera)",
        geomorph: {
          volcanic: { enabled: true, centerX: 10, centerY: -5, Hv: 25, sigma: 12, calderaRadius: 6, calderaDepth: 8 },
          tectonic: { enabled: true, U0: 0.03 }
        }
      },
      glacial: {
        label: "Glacial (U-valleys)",
        geomorph: {
          glacial: { enabled: true, snowLine: 10, beta: 0.15, k: 1.5, coeff: 8e-3 },
          tectonic: { enabled: true, U0: 0.04, kx: 0.06, ky: 0.04 }
        }
      },
      alpine: {
        label: "Alpine (tect\xF4nica + gelo)",
        geomorph: {
          iterations: 25,
          tectonic: { enabled: true, U0: 0.05, p: 2 },
          glacial: { enabled: true, snowLine: 8 }
        },
        biome: { snowLine: 10 }
      },
      mesa: {
        label: "Mesa / Tepui (caprock + deslizamentos)",
        geomorph: {
          iterations: 20,
          Kd: 4e-3,
          lithology: { caprockHardness: 4, caprockThreshold: 0.5, kStrata: 1 },
          landslide: { thetaC: 34, slipRate: 0.15, finalPasses: 8 },
          wind: { Kwind: 35e-4 },
          tectonic: { enabled: true, U0: 0.015, p: 1.2 }
        },
        biome: { moisture: { M0: 0.25, evaporation: 0.7 } }
      }
    };
  }
});

// src/terrain.js
function recommendedGridSize(worldSize) {
  const target = Math.round(worldSize);
  return GRID_SIZE_OPTIONS.reduce(
    (best, s) => Math.abs(s - target) < Math.abs(best - target) ? s : best
  );
}
function scaleConfigForWorldSize(cfg) {
  const factor = cfg.worldSize / REFERENCE_WORLD_SIZE;
  if (Math.abs(factor - 1) < 0.01) return cfg;
  const scaled = deepMerge2(cfg, {
    noise: {
      warpScale: cfg.noise.warpScale * factor,
      warpStrength: cfg.noise.warpStrength * factor,
      baseScale: cfg.noise.baseScale * factor,
      baseHeight: cfg.noise.baseHeight * factor
    },
    biome: {
      macroClimateScale: (cfg.biome.macroClimateScale ?? 200) * factor,
      snowLine: cfg.biome.snowLine * factor,
      temperature: { noiseScale: cfg.biome.temperature.noiseScale * factor },
      moisture: { noiseScale: cfg.biome.moisture.noiseScale * factor }
    }
  });
  if (scaled.blend?.mask) {
    scaled.blend = deepMerge2(scaled.blend, {
      mask: { scale: (scaled.blend.mask.scale ?? 80) * factor }
    });
  }
  return scaled;
}
function runTerrainPipeline(cfg, seedOffset = 0, options = {}) {
  const seed = cfg.seed + seedOffset;
  const { gridSize, worldSize } = cfg;
  const noiseGenerators = createNoiseGenerators({
    warpX: seed + 1e3,
    warpY: seed + 2e3,
    base: seed,
    tectonic: seed + 500,
    strata: seed + 6e3,
    micro: seed + 7e3
  });
  const { heights: h0, cellSize } = buildHeightGrid(gridSize, worldSize, cfg.noise, noiseGenerators);
  const { heights } = simulateGeomorphology(h0, gridSize, cellSize, cfg.geomorph, noiseGenerators);
  const noiseTemp = createNoiseField(seed + 3e3);
  const noiseMoisture = createNoiseField(seed + 4e3);
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
    activeAnchors
  };
}
function generateTerrain(config = {}) {
  const cfg = scaleConfigForWorldSize(deepMerge2(DEFAULT_CONFIG, config));
  console.log("[1/4] Domain warping \u2014 computing h\u2080(x,y)...");
  console.log(`[2/4] Geomorphological PDE \u2014 ${cfg.geomorph.iterations} iterations...`);
  const pipeline = runTerrainPipeline(cfg);
  console.log("[3/4] Biome classification \u2014 T(x,y), M(x,y)...");
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
    activeBiomes: pipeline.activeAnchors
  };
}
function generateBlendedTerrain(config = {}) {
  const cfg = scaleConfigForWorldSize(deepMerge2(DEFAULT_CONFIG, config));
  const { blend, gridSize, worldSize, seed } = cfg;
  const cfgA = resolveConfig(cfg, blend.presetA);
  const cfgB = resolveConfig(cfg, blend.presetB, { seed: seed + 7777 });
  console.log(`[Blend] Domain A: ${blend.presetA}`);
  console.log("[1/6] Pipeline A \u2014 domain warping + PDE...");
  const domainA = runTerrainPipeline(cfgA, 0, { skipReliefShaping: true });
  console.log(`[Blend] Domain B: ${blend.presetB}`);
  console.log("[2/6] Pipeline B \u2014 domain warping + PDE...");
  const domainB = runTerrainPipeline(cfgB, 7777, { skipReliefShaping: true });
  console.log("[3/6] Weight mask W(x,y)...");
  const weightMask = generateWeightMask(gridSize, domainA.cellSize, blend.mask, seed);
  console.log("[4/6] Blending h_A and h_B via smoothstep...");
  const heights = blendHeights(domainA.heights, domainB.heights, weightMask);
  console.log("[5/6] Biome relief shaping + classification...");
  const noiseTemp = createNoiseField(seed + 3e3);
  const noiseMoisture = createNoiseField(seed + 4e3);
  const domainRelief = new Float32Array(gridSize * gridSize);
  for (let i = 0; i < domainRelief.length; i++) {
    const w = weightMask[i] * weightMask[i] * (3 - 2 * weightMask[i]);
    domainRelief[i] = (1 - w) * 0.88 + w * 0.52;
  }
  applyBiomeReliefShaping(heights, gridSize, domainA.cellSize, cfg.biome, noiseTemp, noiseMoisture, {
    domainRelief
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
    activeBiomes: activeAnchors
  };
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
    moisture: { min: minM, max: maxM }
  };
}
function deepMerge2(target, source) {
  const result = { ...target };
  for (const key of Object.keys(source)) {
    if (source[key] && typeof source[key] === "object" && !Array.isArray(source[key])) {
      result[key] = deepMerge2(target[key] || {}, source[key]);
    } else {
      result[key] = source[key];
    }
  }
  return result;
}
var DEFAULT_NOISE_PARAMS, REFERENCE_WORLD_SIZE, GRID_SIZE_OPTIONS, DEFAULT_CONFIG;
var init_terrain = __esm({
  "src/terrain.js"() {
    init_noise();
    init_geomorphology();
    init_biome();
    init_heightGrid();
    init_blend();
    init_presets();
    DEFAULT_NOISE_PARAMS = {
      warpScale: 40,
      warpStrength: 12,
      warpOctaves: 3,
      baseScale: 60,
      baseHeight: 15,
      baseOctaves: 5
    };
    REFERENCE_WORLD_SIZE = 100;
    GRID_SIZE_OPTIONS = [96, 128, 192, 256, 320, 384, 512, 640, 768, 1024];
    DEFAULT_CONFIG = {
      gridSize: 192,
      worldSize: 100,
      seed: 42,
      noise: DEFAULT_NOISE_PARAMS,
      geomorph: DEFAULT_GEOMORPH_PARAMS,
      biome: DEFAULT_BIOME_PARAMS,
      blend: DEFAULT_BLEND_PARAMS
    };
  }
});

// src/geomorphologyGpu.js
async function simulateGeomorphologyGpu(heights, size, cellSize, params, noiseGenerators) {
  const gpu = params.gpuAccelerator;
  if (!gpu?.ready) {
    const { simulateGeomorphology: simulateGeomorphology2 } = await Promise.resolve().then(() => (init_geomorphology(), geomorphology_exports));
    return simulateGeomorphology2(heights, size, cellSize, params, noiseGenerators);
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
var init_geomorphologyGpu = __esm({
  "src/geomorphologyGpu.js"() {
    init_geomorphology();
    init_smooth();
  }
});

// src/terrainGpuPipeline.js
var terrainGpuPipeline_exports = {};
__export(terrainGpuPipeline_exports, {
  generateBlendedTerrainAsync: () => generateBlendedTerrainAsync,
  generateTerrainGpu: () => generateTerrainGpu,
  runTerrainPipelineAsync: () => runTerrainPipelineAsync
});
async function runTerrainPipelineAsync(cfg, seedOffset = 0, options = {}) {
  const seed = cfg.seed + seedOffset;
  const { gridSize, worldSize } = cfg;
  const noiseGenerators = createNoiseGenerators({
    warpX: seed + 1e3,
    warpY: seed + 2e3,
    base: seed,
    tectonic: seed + 500,
    strata: seed + 6e3,
    micro: seed + 7e3
  });
  const { heights: h0, cellSize } = buildHeightGrid(gridSize, worldSize, cfg.noise, noiseGenerators);
  const { heights } = await simulateGeomorphologyGpu(
    h0,
    gridSize,
    cellSize,
    cfg.geomorph,
    noiseGenerators
  );
  const noiseTemp = createNoiseField(seed + 3e3);
  const noiseMoisture = createNoiseField(seed + 4e3);
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
    activeAnchors: biomeResult.activeAnchors
  };
}
async function generateTerrainGpu(cfg) {
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
      moisture: { min: minM, max: maxM }
    },
    blended: false,
    activeBiomes: pipeline.activeAnchors,
    gpuAccelerated: true
  };
}
async function generateBlendedTerrainAsync(cfg) {
  const { blend, gridSize, worldSize, seed } = cfg;
  const cfgA = resolveConfig(cfg, blend.presetA);
  const cfgB = resolveConfig(cfg, blend.presetB, { seed: seed + 7777 });
  const domainA = await runTerrainPipelineAsync(cfgA, 0, { skipReliefShaping: true });
  const domainB = await runTerrainPipelineAsync(cfgB, 7777, { skipReliefShaping: true });
  const weightMask = generateWeightMask(gridSize, domainA.cellSize, blend.mask, seed);
  const heights = blendHeights(domainA.heights, domainB.heights, weightMask);
  const noiseTemp = createNoiseField(seed + 3e3);
  const noiseMoisture = createNoiseField(seed + 4e3);
  const domainRelief = new Float32Array(gridSize * gridSize);
  for (let i = 0; i < domainRelief.length; i++) {
    const w = weightMask[i] * weightMask[i] * (3 - 2 * weightMask[i]);
    domainRelief[i] = (1 - w) * 0.88 + w * 0.52;
  }
  applyBiomeReliefShaping(heights, gridSize, domainA.cellSize, cfg.biome, noiseTemp, noiseMoisture, {
    domainRelief
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
      moisture: { min: minM, max: maxM }
    },
    blended: true,
    domains: { a: blend.presetA, b: blend.presetB },
    activeBiomes: biomeResult.activeAnchors,
    gpuAccelerated: true
  };
}
var init_terrainGpuPipeline = __esm({
  "src/terrainGpuPipeline.js"() {
    init_noise();
    init_geomorphologyGpu();
    init_biome();
    init_heightGrid();
    init_blend();
    init_presets();
    init_noise();
  }
});

// src/terrainAsync.js
var terrainAsync_exports = {};
__export(terrainAsync_exports, {
  generateTerrainAutoAsync: () => generateTerrainAutoAsync
});
function deepMerge3(target, source) {
  const result = { ...target };
  for (const key of Object.keys(source)) {
    if (source[key] && typeof source[key] === "object" && !Array.isArray(source[key])) {
      result[key] = deepMerge3(target[key] || {}, source[key]);
    } else {
      result[key] = source[key];
    }
  }
  return result;
}
async function generateTerrainAutoAsync(config = {}) {
  const cfg = deepMerge3(DEFAULT_CONFIG, config);
  const hasGpu = cfg.geomorph?.gpuAccelerator?.ready;
  if (!hasGpu) {
    if (cfg.blend?.enabled) return generateBlendedTerrain(cfg);
    return generateTerrain(cfg);
  }
  const { generateTerrainGpu: generateTerrainGpu2, generateBlendedTerrainAsync: generateBlendedTerrainAsync2 } = await Promise.resolve().then(() => (init_terrainGpuPipeline(), terrainGpuPipeline_exports));
  if (cfg.blend?.enabled) {
    return generateBlendedTerrainAsync2(cfg);
  }
  return generateTerrainGpu2(cfg);
}
var init_terrainAsync = __esm({
  "src/terrainAsync.js"() {
    init_terrain();
  }
});

// src/client/terrainClient.js
init_terrain();

// src/gpu/pdeGpu.js
var PDE_SHADER = `
struct Params {
  size: u32,
  cellSize: f32,
  dt: f32,
  Kd: f32,
  Ke: f32,
  U0: f32,
  kx: f32,
  ky: f32,
  worldHalf: f32,
  iterations: u32,
};

@group(0) @binding(0) var<storage, read> heightsIn: array<f32>;
@group(0) @binding(1) var<storage, read_write> heightsOut: array<f32>;
@group(0) @binding(2) var<uniform> params: Params;

fn at(x: u32, z: u32, size: u32) -> u32 {
  return z * size + x;
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
  let size = params.size;
  let x = gid.x;
  let z = gid.y;
  if (x < 1u || z < 1u || x >= size - 1u || z >= size - 1u) {
    return;
  }
  let i = at(x, z, size);
  let hC = heightsIn[i];
  let lap = heightsIn[at(x - 1u, z, size)] + heightsIn[at(x + 1u, z, size)]
          + heightsIn[at(x, z - 1u, size)] + heightsIn[at(x, z + 1u, size)] - 4.0 * hC;

  let cs = params.cellSize;
  let dhdx = (heightsIn[at(x + 1u, z, size)] - heightsIn[at(x - 1u, z, size)]) / (2.0 * cs);
  let dhdz = (heightsIn[at(x, z + 1u, size)] - heightsIn[at(x, z - 1u, size)]) / (2.0 * cs);
  let slope = sqrt(dhdx * dhdx + dhdz * dhdz);

  let wx = f32(x) * cs - params.worldHalf;
  let wz = f32(z) * cs - params.worldHalf;
  let phase = params.kx * wx + params.ky * wz;
  let uplift = params.U0 * abs(sin(phase));

  let erosion = params.Ke * slope;
  heightsOut[i] = hC + params.dt * (params.Kd * lap + uplift - erosion);
}
`;
var PdeGpuAccelerator = class _PdeGpuAccelerator {
  constructor(device, pipeline, bindGroupLayout) {
    this.device = device;
    this.pipeline = pipeline;
    this.bindGroupLayout = bindGroupLayout;
    this.ready = true;
  }
  static async create() {
    if (typeof navigator === "undefined" || !navigator.gpu) return null;
    try {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) return null;
      const device = await adapter.requestDevice();
      const module = device.createShaderModule({ code: PDE_SHADER });
      const bindGroupLayout = device.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
          { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
          { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } }
        ]
      });
      const pipeline = device.createComputePipeline({
        layout: device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }),
        compute: { module, entryPoint: "main" }
      });
      return new _PdeGpuAccelerator(device, pipeline, bindGroupLayout);
    } catch {
      return null;
    }
  }
  async runIterations(heights, size, cellSize, params) {
    const { iterations, dt, Kd, Ke, tectonic } = params;
    const byteLen = heights.length * 4;
    const worldHalf = cellSize * (size - 1) / 2;
    const bufA = this.device.createBuffer({
      size: byteLen,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC
    });
    const bufB = this.device.createBuffer({
      size: byteLen,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST
    });
    const uniformBuf = this.device.createBuffer({
      size: 48,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    this.device.queue.writeBuffer(bufA, 0, heights);
    const uniformData = new ArrayBuffer(48);
    const view = new DataView(uniformData);
    view.setUint32(0, size, true);
    view.setFloat32(4, cellSize, true);
    view.setFloat32(8, dt, true);
    view.setFloat32(12, Kd, true);
    view.setFloat32(16, Ke, true);
    view.setFloat32(20, tectonic?.enabled ? tectonic.U0 : 0, true);
    view.setFloat32(24, tectonic?.kx ?? 0.08, true);
    view.setFloat32(28, tectonic?.ky ?? 0.05, true);
    view.setFloat32(32, worldHalf, true);
    view.setUint32(36, iterations, true);
    this.device.queue.writeBuffer(uniformBuf, 0, uniformData);
    const bindA = this.device.createBindGroup({
      layout: this.bindGroupLayout,
      entries: [
        { binding: 0, resource: { buffer: bufA } },
        { binding: 1, resource: { buffer: bufB } },
        { binding: 2, resource: { buffer: uniformBuf } }
      ]
    });
    const bindB = this.device.createBindGroup({
      layout: this.bindGroupLayout,
      entries: [
        { binding: 0, resource: { buffer: bufB } },
        { binding: 1, resource: { buffer: bufA } },
        { binding: 2, resource: { buffer: uniformBuf } }
      ]
    });
    const workgroups = Math.ceil(size / 8);
    const encoder = this.device.createCommandEncoder();
    for (let iter = 0; iter < iterations; iter++) {
      const pass = encoder.beginComputePass();
      pass.setPipeline(this.pipeline);
      pass.setBindGroup(0, iter % 2 === 0 ? bindA : bindB);
      pass.dispatchWorkgroups(workgroups, workgroups);
      pass.end();
    }
    const readBuf = this.device.createBuffer({
      size: byteLen,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
    });
    const src = iterations % 2 === 0 ? bufA : bufB;
    encoder.copyBufferToBuffer(src, 0, readBuf, 0, byteLen);
    this.device.queue.submit([encoder.finish()]);
    await readBuf.mapAsync(GPUMapMode.READ);
    const result = new Float32Array(readBuf.getMappedRange().slice(0));
    readBuf.unmap();
    bufA.destroy();
    bufB.destroy();
    uniformBuf.destroy();
    readBuf.destroy();
    return result;
  }
};

// src/client/terrainClient.js
var gpuAccelerator = null;
var gpuInitPromise = null;
async function initGpuAcceleration() {
  if (gpuInitPromise) return gpuInitPromise;
  gpuInitPromise = PdeGpuAccelerator.create().then((acc) => {
    gpuAccelerator = acc;
    return acc?.ready ?? false;
  });
  return gpuInitPromise;
}
function isGpuReady() {
  return gpuAccelerator?.ready ?? false;
}
async function generateTerrainClient(config, useGpu = true) {
  const cfg = scaleConfigForWorldSize({ ...DEFAULT_CONFIG, ...config });
  if (useGpu && gpuAccelerator?.ready) {
    cfg.geomorph = { ...cfg.geomorph, gpuAccelerator };
  }
  const { generateTerrainAutoAsync: generateTerrainAutoAsync2 } = await Promise.resolve().then(() => (init_terrainAsync(), terrainAsync_exports));
  return generateTerrainAutoAsync2(cfg);
}
export {
  DEFAULT_CONFIG,
  generateTerrainClient,
  initGpuAcceleration,
  isGpuReady,
  recommendedGridSize
};
