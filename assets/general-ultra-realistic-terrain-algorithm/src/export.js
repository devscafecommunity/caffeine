import fs from 'fs';

export function exportToOBJ(geometry, filename) {
  let obj = '# Terrain — Domain Warping + Geomorphological PDE\n';
  const pos = geometry.attributes.position;
  const norm = geometry.attributes.normal;
  const index = geometry.index;

  for (let i = 0; i < pos.count; i++) {
    obj += `v ${pos.getX(i).toFixed(6)} ${pos.getY(i).toFixed(6)} ${pos.getZ(i).toFixed(6)}\n`;
  }

  for (let i = 0; i < norm.count; i++) {
    obj += `vn ${norm.getX(i).toFixed(6)} ${norm.getY(i).toFixed(6)} ${norm.getZ(i).toFixed(6)}\n`;
  }

  if (index) {
    for (let i = 0; i < index.count; i += 3) {
      const a = index.getX(i) + 1;
      const b = index.getX(i + 1) + 1;
      const c = index.getX(i + 2) + 1;
      obj += `f ${a}//${a} ${b}//${b} ${c}//${c}\n`;
    }
  }

  fs.writeFileSync(filename, obj);
  return filename;
}

export function exportHeightsToOBJ(heights, size, worldSize, filename) {
  const half = worldSize / 2;
  const cellSize = worldSize / (size - 1);
  let obj = '# Terrain — Domain Warping + Geomorphological PDE\n';

  for (let z = 0; z < size; z++) {
    for (let x = 0; x < size; x++) {
      const wx = x * cellSize - half;
      const wz = z * cellSize - half;
      const h = heights[z * size + x];
      obj += `v ${wx.toFixed(6)} ${h.toFixed(6)} ${wz.toFixed(6)}\n`;
    }
  }

  for (let z = 0; z < size - 1; z++) {
    for (let x = 0; x < size - 1; x++) {
      const a = z * size + x + 1;
      const b = a + 1;
      const c = a + size;
      const d = c + 1;
      obj += `f ${a} ${b} ${d}\n`;
      obj += `f ${a} ${d} ${c}\n`;
    }
  }

  fs.writeFileSync(filename, obj);
  return filename;
}

export function exportHeightmap(heights, size, filename) {
  const lines = [];
  for (let z = 0; z < size; z++) {
    const row = [];
    for (let x = 0; x < size; x++) {
      row.push(heights[z * size + x].toFixed(4));
    }
    lines.push(row.join(' '));
  }
  fs.writeFileSync(filename, lines.join('\n'));
  return filename;
}

export function terrainToJSON(heights, size, worldSize, biomeColors, metadata) {
  return JSON.stringify({
    metadata,
    size,
    worldSize,
    heights: Array.from(heights),
    biomeColors: biomeColors ? Array.from(biomeColors) : null,
  });
}
