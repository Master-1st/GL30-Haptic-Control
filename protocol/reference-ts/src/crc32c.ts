export const POLY = 0x82f63b78;

let TABLE: number[] | null = null;

function buildTable(): number[] {
  const table = new Array<number>(256);
  for (let i = 0; i < 256; i++) {
    let crc = i;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 1) ? (crc >>> 1) ^ POLY : crc >>> 1;
    }
    table[i] = crc >>> 0;
  }
  return table;
}

export function crc32c(data: Uint8Array): number {
  if (!TABLE) TABLE = buildTable();
  let crc = 0xffffffff;
  for (const b of data) {
    crc = (crc >>> 8) ^ (TABLE![((crc ^ b) & 0xff) >>> 0]!);
  }
  return (crc ^ 0xffffffff) >>> 0;
}
