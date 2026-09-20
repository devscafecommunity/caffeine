/**
 * WebGPU compute accelerator for geomorphological PDE iterations
 * Runs diffusion + tectonic uplift + slope erosion in parallel on GPU
 */

const PDE_SHADER = `
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

export class PdeGpuAccelerator {
  constructor(device, pipeline, bindGroupLayout) {
    this.device = device;
    this.pipeline = pipeline;
    this.bindGroupLayout = bindGroupLayout;
    this.ready = true;
  }

  static async create() {
    if (typeof navigator === 'undefined' || !navigator.gpu) return null;

    try {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) return null;
      const device = await adapter.requestDevice();
      const module = device.createShaderModule({ code: PDE_SHADER });
      const bindGroupLayout = device.createBindGroupLayout({
        entries: [
          { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'read-only-storage' } },
          { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'storage' } },
          { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: 'uniform' } },
        ],
      });
      const pipeline = device.createComputePipeline({
        layout: device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] }),
        compute: { module, entryPoint: 'main' },
      });
      return new PdeGpuAccelerator(device, pipeline, bindGroupLayout);
    } catch {
      return null;
    }
  }

  async runIterations(heights, size, cellSize, params) {
    const { iterations, dt, Kd, Ke, tectonic } = params;
    const byteLen = heights.length * 4;
    const worldHalf = (cellSize * (size - 1)) / 2;

    const bufA = this.device.createBuffer({
      size: byteLen,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC,
    });
    const bufB = this.device.createBuffer({
      size: byteLen,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
    });
    const uniformBuf = this.device.createBuffer({
      size: 48,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
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
        { binding: 2, resource: { buffer: uniformBuf } },
      ],
    });
    const bindB = this.device.createBindGroup({
      layout: this.bindGroupLayout,
      entries: [
        { binding: 0, resource: { buffer: bufB } },
        { binding: 1, resource: { buffer: bufA } },
        { binding: 2, resource: { buffer: uniformBuf } },
      ],
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
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ,
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
}
