const ECC_M_TABLE = [
  0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26
];
const SIZE_TABLE = [
  0, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57
];
const ALIGN_TABLE: number[][] = [
  [], [], [6, 18], [6, 22], [6, 26], [6, 30], [6, 34],
  [6, 22, 38], [6, 24, 42], [6, 26, 46], [6, 28, 50]
];

function gfMul(a: number, b: number): number {
  let result = 0;
  for (let i = 0; i < 8; i++) {
    if (b & 1)
      result ^= a;
    const hi = a & 0x80;
    a = (a << 1) & 0xff;
    if (hi)
      a ^= 0x1d;
    b >>= 1;
  }
  return result;
}

function reedSolomon(data: number[], ecCount: number): number[] {
  const ecc = new Array(ecCount).fill(0);
  let generator = [1];
  let root = 1;
  for (let i = 0; i < ecCount; i++) {
    const next = new Array(generator.length + 1).fill(0);
    for (let j = 0; j < generator.length; j++) {
      next[j] ^= generator[j];
      next[j + 1] ^= gfMul(generator[j], root);
    }
    generator = next;
    root = gfMul(root, 2);
  }
  for (const byte of data) {
    const factor = byte ^ ecc[0];
    ecc.shift();
    ecc.push(0);
    for (let i = 0; i < ecCount; i++)
      ecc[i] ^= gfMul(generator[i + 1] ?? 0, factor);
  }
  return ecc;
}

function capacity(version: number): number {
  const size = SIZE_TABLE[version];
  let modules = size * size;
  modules -= 3 * 8 * 8;
  modules -= 2 * 15;
  modules -= size * 2 - 16 - 16;
  if (version >= 2) {
    const aligns = ALIGN_TABLE[version];
    const count = aligns.length ** 2 - 3;
    modules -= count * 25;
  }
  if (version >= 7)
    modules -= 2 * 18;
  return Math.floor(modules / 8) - ECC_M_TABLE[version];
}

export function encodeQrModules(text: string): boolean[][] {
  const bytes = Array.from(new TextEncoder().encode(text));
  let version = 1;
  while (version <= 10 && bytes.length + 2 > capacity(version))
    version++;
  if (version > 10)
    throw new Error("QR payload too long");
  const dataCapacity = capacity(version);
  const bits: number[] = [];
  const push = (value: number, length: number) => {
    for (let i = length - 1; i >= 0; i--)
      bits.push((value >> i) & 1);
  };
  push(0b0100, 4);
  push(bytes.length, version < 10 ? 8 : 16);
  for (const byte of bytes)
    push(byte, 8);
  const totalBits = dataCapacity * 8;
  for (const pad of [0, 1, 0, 0]) {
    if (bits.length >= totalBits)
      break;
    bits.push(pad);
  }
  while (bits.length % 8)
    bits.push(0);
  const padBytes = [0xec, 0x11];
  let padIndex = 0;
  while (bits.length < totalBits) {
    push(padBytes[padIndex % 2], 8);
    padIndex++;
  }
  const data: number[] = [];
  for (let i = 0; i < bits.length; i += 8) {
    let value = 0;
    for (let j = 0; j < 8; j++)
      value = (value << 1) | bits[i + j];
    data.push(value);
  }
  const ecc = reedSolomon(data, ECC_M_TABLE[version]);
  const code = [...data, ...ecc];
  const size = SIZE_TABLE[version];
  const modules = Array.from({ length: size }, () => new Array<boolean>(size).fill(false));
  const reserved = Array.from({ length: size }, () => new Array<boolean>(size).fill(false));
  const set = (x: number, y: number, dark: boolean, reserve = true) => {
    if (x < 0 || y < 0 || x >= size || y >= size)
      return;
    modules[y][x] = dark;
    if (reserve)
      reserved[y][x] = true;
  };
  const finder = (ox: number, oy: number) => {
    for (let y = -1; y <= 7; y++)
      for (let x = -1; x <= 7; x++) {
        const dark = x >= 0 && x <= 6 && y >= 0 && y <= 6
          && (x === 0 || x === 6 || y === 0 || y === 6 || (x >= 2 && x <= 4 && y >= 2 && y <= 4));
        set(ox + x, oy + y, dark);
      }
  };
  finder(0, 0);
  finder(size - 7, 0);
  finder(0, size - 7);
  for (let i = 8; i < size - 8; i++) {
    set(i, 6, i % 2 === 0);
    set(6, i, i % 2 === 0);
  }
  for (const y of ALIGN_TABLE[version])
    for (const x of ALIGN_TABLE[version]) {
      if ((x < 9 && y < 9) || (x > size - 10 && y < 9) || (x < 9 && y > size - 10))
        continue;
      for (let dy = -2; dy <= 2; dy++)
        for (let dx = -2; dx <= 2; dx++)
          set(x + dx, y + dy, Math.max(Math.abs(dx), Math.abs(dy)) !== 1);
    }
  set(8, size - 8, true);
  for (let i = 0; i < 8; i++) {
    reserved[8][i] = true;
    reserved[i][8] = true;
    reserved[8][size - 1 - i] = true;
    reserved[size - 1 - i][8] = true;
  }
  reserved[8][8] = true;
  let bit = 0;
  const total = code.length * 8;
  for (let col = size - 1; col > 0; col -= 2) {
    if (col === 6)
      col--;
    for (let rowPass = 0; rowPass < size; rowPass++) {
      const upward = Math.floor((size - 1 - col) / 2) % 2 === 0;
      const y = upward ? size - 1 - rowPass : rowPass;
      for (let dx = 0; dx < 2; dx++) {
        const x = col - dx;
        if (reserved[y][x] || bit >= total)
          continue;
        const byte = code[Math.floor(bit / 8)];
        const dark = ((byte >> (7 - (bit % 8))) & 1) === 1;
        const mask = ((y + x) % 2) === 0;
        set(x, y, dark !== mask, false);
        bit++;
      }
    }
  }
  const format = 0b101010000010010;
  const formatBit = (index: number) => ((format >> index) & 1) === 1;
  const writeFormat = (x: number, y: number, index: number) => {
    if (x >= 0 && y >= 0 && x < size && y < size)
      modules[y][x] = formatBit(index);
  };
  for (let i = 0; i <= 5; i++)
    writeFormat(8, i, i);
  writeFormat(8, 7, 6);
  writeFormat(8, 8, 7);
  writeFormat(7, 8, 8);
  for (let i = 9; i < 15; i++)
    writeFormat(14 - i, 8, i);
  for (let i = 0; i < 8; i++)
    writeFormat(size - 1 - i, 8, i);
  for (let i = 8; i < 15; i++)
    writeFormat(8, size - 15 + i, i);
  modules[size - 8][8] = true;
  return modules;
}

export function drawQr(canvas: HTMLCanvasElement, text: string, maxPx = 180): void {
  const modules = encodeQrModules(text);
  const size = modules.length;
  const scale = Math.max(2, Math.floor(maxPx / size));
  canvas.width = (size + 2) * scale;
  canvas.height = (size + 2) * scale;
  const context = canvas.getContext("2d");
  if (!context)
    return;
  context.fillStyle = "#fff";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.fillStyle = "#111";
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++)
      if (modules[y][x])
        context.fillRect((x + 1) * scale, (y + 1) * scale, scale, scale);
}
