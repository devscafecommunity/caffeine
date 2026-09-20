/**
 * Correção cirúrgica de artefatos — só células com saltos verticais impossíveis
 * (buracos/blocos afundados), preservando erosão e detalhe geológico
 */
export function repairHeightArtifacts(heights, size, jumpThreshold = 5.0) {
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

export function boxBlurField(field, size, radius) {
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
