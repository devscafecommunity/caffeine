import express from 'express';
import path from 'path';
import { fileURLToPath } from 'url';
import { generateTerrainAuto, DEFAULT_CONFIG, recommendedGridSize } from './src/terrain.js';
import { resolveConfig } from './src/presets.js';
import { getBiomeCatalog, getBiomeGroupCatalog } from './src/universalBiome.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const app = express();
const PORT = 3456;

app.use(express.static(path.join(__dirname, 'public')));
app.use(express.json());

app.get('/api/terrain', (req, res) => {
  const preset = req.query.preset || 'default';
  const worldSize = parseFloat(req.query.worldSize) || DEFAULT_CONFIG.worldSize;
  const autoGrid = req.query.autoGrid === 'true' || req.query.autoGrid === '1';
  const gridSize = autoGrid
    ? recommendedGridSize(worldSize)
    : parseInt(req.query.size) || recommendedGridSize(worldSize);
  const iterations = parseInt(req.query.iterations) || 15;
  const blend = req.query.blend === 'true' || req.query.blend === '1';
  const presetA = req.query.presetA || 'alpine';
  const presetB = req.query.presetB || 'mesa';
  const maskType = req.query.maskType || 'noise';
  const biomeGroup = req.query.biomeGroup || 'all';
  const enabledBiomes = req.query.biomes
    ? req.query.biomes.split(',').map((s) => s.trim()).filter(Boolean)
    : null;

  const baseOverrides = {
    gridSize,
    worldSize,
    geomorph: { ...DEFAULT_CONFIG.geomorph, iterations },
    biome: {
      ...DEFAULT_CONFIG.biome,
      biomeGroup,
      enabledBiomes: enabledBiomes?.length ? enabledBiomes : null,
    },
  };

  let config = resolveConfig({ ...DEFAULT_CONFIG, ...baseOverrides }, preset);

  if (blend) {
    config = {
      ...DEFAULT_CONFIG,
      ...baseOverrides,
      blend: {
        enabled: true,
        presetA,
        presetB,
        mask: { type: maskType, scale: 80, sharpness: 1.2 },
        slopeMask: { enabled: true, cliffSlope: 0.95, rockColor: [0.50, 0.42, 0.36] },
      },
    };
  }

  console.log(
    `Generating: ${blend ? `blend ${presetA}+${presetB}` : preset}, grid=${gridSize}, world=${worldSize}, biomes=${enabledBiomes?.length || biomeGroup}`
  );
  const result = generateTerrainAuto(config);

  res.json({
    metadata: {
      preset: blend ? `blend:${presetA}+${presetB}` : preset,
      blended: result.blended,
      domains: result.domains,
      stats: result.stats,
      activeBiomes: result.activeBiomes,
      config: { gridSize, worldSize, iterations, blend, maskType, biomeGroup, enabledBiomes },
    },
    size: result.gridSize,
    worldSize: result.worldSize,
    heights: Array.from(result.heights),
    biomeColors: result.biomeColors ? Array.from(result.biomeColors) : null,
    weightMask: result.weightMask ? Array.from(result.weightMask) : null,
  });
});

app.get('/api/biomes', (_req, res) => {
  res.json({
    count: getBiomeCatalog().length,
    biomes: getBiomeCatalog(),
    groups: getBiomeGroupCatalog(),
  });
});

app.listen(PORT, () => {
  console.log(`\n  Terrain Viewer: http://localhost:${PORT}\n`);
});
