/**
 * Fractal Brownian Motion (fBm) and Domain Warping
 *
 *   Ψ(x) = Σᵢ 2⁻ⁱ · Nₚ(2ⁱ·x/λw, S)
 *   x' = x + γ·Ψ(x)
 *   h₀(x) = Σⱼ 2⁻ʲ · Nₚ(2ʲ·x'/λb, S₀)
 */

import { createNoise2D } from 'simplex-noise';

function seededRandom(seed) {
  let s = seed;
  return () => {
    s = (s * 16807 + 0) % 2147483647;
    return (s - 1) / 2147483646;
  };
}

export function createNoiseField(seed) {
  return createNoise2D(seededRandom(seed));
}

export function fBm(x, y, scale, octaves, noiseFn) {
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

export function displacementField(x, y, params, noiseWarpX, noiseWarpY) {
  const { warpScale, warpOctaves } = params;
  const psiX = fBm(x, y, warpScale, warpOctaves, noiseWarpX);
  const psiY = fBm(x, y, warpScale, warpOctaves, noiseWarpY);
  return { psiX, psiY };
}

export function initialHeight(x, y, params, noiseWarpX, noiseWarpY, noiseBase) {
  const { warpStrength, baseScale, baseHeight, baseOctaves } = params;
  const { psiX, psiY } = displacementField(x, y, params, noiseWarpX, noiseWarpY);

  const warpedX = x + psiX * warpStrength;
  const warpedY = y + psiY * warpStrength;

  const h0 = fBm(warpedX, warpedY, baseScale, baseOctaves, noiseBase);
  return h0 * baseHeight;
}

/**
 * Stratified lithologic resistance R(x,y,h) ∈ [ε, 1]
 * Includes horizontal sedimentary layers, dip/strike anisotropy, and caprock hardening.
 *
 * R = R_base + ΔR·layer(h) combined with dip anisotropy;
 * caprock when sin(k_strata·h + N_fault) > threshold → R × R_cap
 */
export function lithologicResistance(x, y, height, params, noiseStrata) {
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
    caprockHardness,
  } = params;

  const strikeRad = (strikeAngle * Math.PI) / 180;
  const dipRad = (dipAngle * Math.PI) / 180;
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

/** @deprecated Use lithologicResistance */
export function stratifiedResistance(x, y, height, params, noiseStrata) {
  return lithologicResistance(x, y, height, params, noiseStrata);
}

export function createNoiseGenerators(seeds = {}) {
  const {
    warpX = 1000,
    warpY = 2000,
    base = 0,
    tectonic = 500,
    strata = 6000,
    micro = 7000,
  } = seeds;
  return {
    noiseWarpX: createNoiseField(warpX),
    noiseWarpY: createNoiseField(warpY),
    noiseBase: createNoiseField(base),
    noiseTectonic: createNoiseField(tectonic),
    noiseStrata: createNoiseField(strata),
    noiseMicro: createNoiseField(micro),
  };
}
