import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

let scene, camera, renderer, controls, terrainMesh;
let biomeCatalog = [];
let terrainWorker = null;
let workerReqId = 0;

function getTerrainWorker() {
  if (!terrainWorker) {
    terrainWorker = new Worker('/workers/terrain-worker.js', { type: 'module' });
  }
  return terrainWorker;
}

function buildConfigFromUI() {
  const blendMode = document.getElementById('blendMode').value;
  const blend = blendMode === 'blend';
  const biomeGroup = document.getElementById('biomeGroup').value;

  const config = {
    gridSize: parseInt(document.getElementById('size').value),
    worldSize: parseFloat(document.getElementById('worldSize').value),
    seed: 42,
    geomorph: { iterations: parseInt(document.getElementById('iterations').value) },
    biome: {
      biomeGroup: biomeGroup === 'custom' ? 'all' : biomeGroup,
      enabledBiomes: null,
    },
  };

  if (blend) {
    config.blend = {
      enabled: true,
      presetA: document.getElementById('presetA').value,
      presetB: document.getElementById('presetB').value,
      mask: { type: document.getElementById('maskType').value, scale: 80, sharpness: 1.2 },
    };
  } else {
    config.preset = document.getElementById('preset').value;
    config.blend = { enabled: false };
  }

  if (biomeGroup === 'custom') {
    const selected = getSelectedBiomes();
    if (selected.length > 0) config.biome.enabledBiomes = selected;
  }

  return config;
}

function generateInWorker(config, useGpu) {
  return new Promise((resolve, reject) => {
    const id = ++workerReqId;
    const worker = getTerrainWorker();

    const handler = (e) => {
      if (e.data.id !== id) return;
      worker.removeEventListener('message', handler);
      if (e.data.ok) resolve(e.data.data);
      else reject(new Error(e.data.error));
    };

    worker.addEventListener('message', handler);
    worker.postMessage({ id, config, useGpu });
  });
}

const GRID_SIZE_OPTIONS = [96, 128, 192, 256, 320, 384, 512, 640, 768, 1024];

function recommendedGridSize(worldSize) {
  const target = Math.round(parseFloat(worldSize));
  return GRID_SIZE_OPTIONS.reduce((best, s) =>
    Math.abs(s - target) < Math.abs(best - target) ? s : best
  );
}

function updateGridHint() {
  const worldSize = parseFloat(document.getElementById('worldSize').value);
  const gridSize = parseInt(document.getElementById('size').value);
  const cellSize = worldSize / (gridSize - 1);
  const hint = document.getElementById('gridHint');
  const recommended = recommendedGridSize(worldSize);
  const match = gridSize === recommended ? '✓' : `(ideal: ${recommended})`;
  hint.textContent = `${cellSize.toFixed(2)} u/vértice ${match}`;
}

function syncGridToWorldSize() {
  const worldSize = document.getElementById('worldSize').value;
  const recommended = String(recommendedGridSize(worldSize));
  const sizeSelect = document.getElementById('size');
  if ([...sizeSelect.options].some((o) => o.value === recommended)) {
    sizeSelect.value = recommended;
  }
  updateGridHint();
}

function init() {
  const container = document.getElementById('canvas-container');

  scene = new THREE.Scene();
  scene.background = new THREE.Color(0x0a0a12);
  scene.fog = new THREE.Fog(0x0a0a12, 80, 250);

  camera = new THREE.PerspectiveCamera(55, window.innerWidth / window.innerHeight, 0.1, 2000);
  camera.position.set(60, 50, 60);

  renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  container.appendChild(renderer.domElement);

  controls = new OrbitControls(camera, renderer.domElement);
  controls.enableDamping = true;
  controls.dampingFactor = 0.08;
  controls.maxPolarAngle = Math.PI / 2.1;
  controls.target.set(0, 5, 0);

  const ambient = new THREE.AmbientLight(0x404060, 0.6);
  scene.add(ambient);

  const sun = new THREE.DirectionalLight(0xfff5e6, 1.2);
  sun.position.set(50, 80, 30);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  sun.shadow.camera.near = 10;
  sun.shadow.camera.far = 800;
  sun.shadow.camera.left = -120;
  sun.shadow.camera.right = 120;
  sun.shadow.camera.top = 120;
  sun.shadow.camera.bottom = -120;
  scene.add(sun);

  const fill = new THREE.DirectionalLight(0x6688cc, 0.3);
  fill.position.set(-30, 20, -40);
  scene.add(fill);

  window.addEventListener('resize', onResize);
  document.getElementById('generate').addEventListener('click', loadTerrain);
  document.getElementById('blendMode').addEventListener('change', toggleBlendUI);
  document.getElementById('biomeGroup').addEventListener('change', toggleBiomeUI);
  document.getElementById('biomeSelectAll').addEventListener('click', () => setAllBiomes(true));
  document.getElementById('biomeSelectNone').addEventListener('click', () => setAllBiomes(false));
  document.getElementById('worldSize').addEventListener('change', syncGridToWorldSize);
  document.getElementById('size').addEventListener('change', updateGridHint);

  toggleBlendUI();
  updateGridHint();
  loadBiomeCatalog().then(() => {
    toggleBiomeUI();
    loadTerrain();
  });
  animate();
}

async function loadBiomeCatalog() {
  try {
    const res = await fetch('/api/biomes');
    const data = await res.json();
    biomeCatalog = data.biomes || [];
    buildBiomeChecklist();
  } catch (err) {
    console.warn('Could not load biome catalog:', err);
  }
}

function buildBiomeChecklist() {
  const container = document.getElementById('biomeCustom');
  container.innerHTML = '';

  for (const biome of biomeCatalog) {
    const label = document.createElement('label');
    label.className = 'biome-item';
    label.innerHTML = `
      <input type="checkbox" value="${biome.id}" checked>
      <span>${formatBiomeName(biome.id)}</span>
    `;
    container.appendChild(label);
  }
}

function formatBiomeName(id) {
  return id.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
}

function setAllBiomes(checked) {
  document.querySelectorAll('#biomeCustom input[type="checkbox"]').forEach((el) => {
    el.checked = checked;
  });
}

function getSelectedBiomes() {
  const checked = [...document.querySelectorAll('#biomeCustom input[type="checkbox"]:checked')];
  return checked.map((el) => el.value);
}

function toggleBiomeUI() {
  const group = document.getElementById('biomeGroup').value;
  const custom = group === 'custom';
  const list = document.getElementById('biomeCustom');
  const actions = document.getElementById('biomeActions');
  const hint = document.getElementById('biomeGroupHint');

  list.classList.toggle('visible', custom);
  actions.style.display = custom ? 'flex' : 'none';

  if (custom) {
    hint.textContent = 'Selecione os biomas individualmente';
  } else if (group === 'all') {
    hint.textContent = 'Todos os 32 biomas ativos';
  } else {
    hint.textContent = `Grupo "${group}" selecionado`;
  }
}

function updateSceneScale(worldSize) {
  const fogNear = worldSize * 0.5;
  const fogFar = worldSize * 2.5;
  scene.fog.near = fogNear;
  scene.fog.far = fogFar;
  camera.far = Math.max(2000, worldSize * 4);
  camera.updateProjectionMatrix();
}

async function loadTerrain() {
  const btn = document.getElementById('generate');
  const loading = document.getElementById('loading');
  const useGpu = document.getElementById('useGpu').checked;
  btn.disabled = true;
  const gridSize = parseInt(document.getElementById('size').value);
  const mode = useGpu ? 'GPU' : 'CPU';
  loading.textContent = gridSize >= 768
    ? `Gerando terreno (${gridSize}², ${mode})...`
    : `Gerando terreno (${mode})...`;
  loading.style.display = 'flex';

  const config = buildConfigFromUI();

  try {
    let data;
    if (useGpu) {
      try {
        data = await generateInWorker(config, true);
        document.getElementById('gpuHint').textContent = data.metadata.gpuAccelerated
          ? '✓ WebGPU ativo — geração local'
          : 'Worker CPU — WebGPU indisponível';
      } catch (workerErr) {
        console.warn('Worker/GPU failed, falling back to API:', workerErr);
        data = await fetchFromApi(config);
        document.getElementById('gpuHint').textContent = 'Fallback: servidor CPU';
      }
    } else {
      data = await fetchFromApi(config);
      document.getElementById('gpuHint').textContent = 'Servidor CPU';
    }
    buildMesh(data);
    updateStats(data.metadata);
  } catch (err) {
    console.error('Failed to load terrain:', err);
  } finally {
    btn.disabled = false;
    loading.textContent = 'Gerando terreno...';
    loading.style.display = 'none';
  }
}

async function fetchFromApi(config) {
  const blend = config.blend?.enabled;
  const params = new URLSearchParams({
    preset: config.preset || 'default',
    size: config.gridSize,
    worldSize: config.worldSize,
    iterations: config.geomorph.iterations,
    blend: blend ? '1' : '0',
    presetA: config.blend?.presetA || 'alpine',
    presetB: config.blend?.presetB || 'mesa',
    maskType: config.blend?.mask?.type || 'noise',
    biomeGroup: config.biome.biomeGroup,
  });
  if (config.biome.enabledBiomes?.length) {
    params.set('biomes', config.biome.enabledBiomes.join(','));
  }
  const res = await fetch(`/api/terrain?${params}`);
  return res.json();
}

function buildMesh(data) {
  if (terrainMesh) {
    scene.remove(terrainMesh);
    terrainMesh.geometry.dispose();
    terrainMesh.material.dispose();
  }

  const { size, worldSize, heights, biomeColors } = data;
  const half = worldSize / 2;
  updateSceneScale(worldSize);

  const geometry = new THREE.PlaneGeometry(worldSize, worldSize, size - 1, size - 1);
  geometry.rotateX(-Math.PI / 2);

  const positions = geometry.attributes.position;
  const colors = new Float32Array(size * size * 3);

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const i = z * size + x;
      positions.setY(i, heights[i]);

      if (biomeColors) {
        colors[i * 3] = biomeColors[i * 3];
        colors[i * 3 + 1] = biomeColors[i * 3 + 1];
        colors[i * 3 + 2] = biomeColors[i * 3 + 2];
      }
    }
  }

  geometry.setAttribute('color', new THREE.BufferAttribute(colors, 3));
  geometry.computeVertexNormals();

  const material = new THREE.MeshStandardMaterial({
    vertexColors: true,
    roughness: 0.85,
    metalness: 0.05,
    flatShading: false,
  });

  terrainMesh = new THREE.Mesh(geometry, material);
  terrainMesh.receiveShadow = true;
  terrainMesh.castShadow = true;
  scene.add(terrainMesh);

  let maxH = -Infinity;
  for (const h of heights) maxH = Math.max(maxH, h);
  const camDist = half * 1.2;
  camera.position.set(camDist, maxH + worldSize * 0.25, camDist);
  controls.target.set(0, maxH * 0.3, 0);
  controls.update();
}

function toggleBlendUI() {
  const blend = document.getElementById('blendMode').value === 'blend';
  document.getElementById('blendControls').style.display = blend ? 'block' : 'none';
  document.getElementById('preset').style.display = blend ? 'none' : 'block';
  document.getElementById('presetLabel').style.display = blend ? 'none' : 'block';
}

function updateStats(metadata) {
  const { stats, config, activeBiomes } = metadata;
  const el = document.getElementById('stats');
  const biomeInfo = activeBiomes
    ? `${activeBiomes.length} bioma${activeBiomes.length !== 1 ? 's' : ''}`
    : '—';

  el.innerHTML = `
    <div>Mapa: <span>${config.worldSize} × ${config.worldSize}</span> (grid ${config.gridSize}², ${(config.worldSize / (config.gridSize - 1)).toFixed(2)} u/vert)${metadata.gpuAccelerated ? ' <span>[GPU]</span>' : ''}</div>
    <div>Biomas: <span>${biomeInfo}</span></div>
    <div>Elevation: <span>${stats.elevation.min.toFixed(1)} → ${stats.elevation.max.toFixed(1)}</span></div>
    <div>Mean height: <span>${stats.elevation.mean.toFixed(1)}</span></div>
    <div>Temperature: <span>${stats.temperature.min.toFixed(0)}°C → ${stats.temperature.max.toFixed(0)}°C</span></div>
    <div>Moisture: <span>${stats.moisture.min.toFixed(2)} → ${stats.moisture.max.toFixed(2)}</span></div>
  `;
}

function onResize() {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
}

function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}

init();
