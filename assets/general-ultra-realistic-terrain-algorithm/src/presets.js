/** Geomorphological node-tree presets (independent pipelines) */

export const PRESETS = {
  default: {
    label: 'Default (erosão + tectônica)',
  },
  volcanic: {
    label: 'Volcanic (cone + caldera)',
    geomorph: {
      volcanic: { enabled: true, centerX: 10, centerY: -5, Hv: 25, sigma: 12, calderaRadius: 6, calderaDepth: 8 },
      tectonic: { enabled: true, U0: 0.03 },
    },
  },
  glacial: {
    label: 'Glacial (U-valleys)',
    geomorph: {
      glacial: { enabled: true, snowLine: 10, beta: 0.15, k: 1.5, coeff: 0.008 },
      tectonic: { enabled: true, U0: 0.04, kx: 0.06, ky: 0.04 },
    },
  },
  alpine: {
    label: 'Alpine (tectônica + gelo)',
    geomorph: {
      iterations: 25,
      tectonic: { enabled: true, U0: 0.05, p: 2.0 },
      glacial: { enabled: true, snowLine: 8 },
    },
    biome: { snowLine: 10 },
  },
  mesa: {
    label: 'Mesa / Tepui (caprock + deslizamentos)',
    geomorph: {
      iterations: 20,
      Kd: 0.004,
      lithology: { caprockHardness: 4.0, caprockThreshold: 0.5, kStrata: 1.0 },
      landslide: { thetaC: 34, slipRate: 0.15, finalPasses: 8 },
      wind: { Kwind: 0.0035 },
      tectonic: { enabled: true, U0: 0.015, p: 1.2 },
    },
    biome: { moisture: { M0: 0.25, evaporation: 0.7 } },
  },
};

export function getPresetConfig(name) {
  return PRESETS[name] ? { ...PRESETS[name] } : {};
}

export function resolveConfig(baseConfig, presetName, overrides = {}) {
  const preset = getPresetConfig(presetName);
  const { label: _label, ...presetCfg } = preset;
  return deepMerge(deepMerge(baseConfig, presetCfg), overrides);
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
