// Independent reference for the MEGA file format, using Node's OpenSSL. It prints the values
// that `tests/mega_test.cpp` checks the native implementation against:
//   node tests/fixtures/mega/make-vectors.mjs
import { createCipheriv, createHash } from 'node:crypto';

const size = 3 * 1024 * 1024 + 5; // several MAC chunks and a partial final block
const plain = Buffer.alloc(size);
for (let i = 0; i < size; i++) plain[i] = (i * 31 + 7) & 0xff;

// Fixed key material so the vectors are reproducible: the AES key and the CTR nonce. MEGA
// publishes [key ^ (nonce, metaMac), nonce, metaMac] as the eight words of the link key.
const keyWords = [0x0123_4567, 0x89ab_cdef, 0xfedc_ba98, 0x7654_3210];
const nonceWords = [0x0f1e_2d3c, 0x4b5a_6978];
const aesKey = Buffer.alloc(16);
keyWords.forEach((w, i) => aesKey.writeUInt32BE(w, i * 4));
const nonce = Buffer.alloc(8);
nonceWords.forEach((w, i) => nonce.writeUInt32BE(w, i * 4));

const ctr = createCipheriv('aes-128-ctr', aesKey, Buffer.concat([nonce, Buffer.alloc(8)]));
const cipher = Buffer.concat([ctr.update(plain), ctr.final()]);

// Chunk boundaries: 128 KiB, 256 KiB, ... up to 1 MiB, then 1 MiB each.
const chunks = [];
for (let start = 0, i = 1; start < size; i++) {
  const length = Math.min(Math.min(i, 8) * 128 * 1024, size - start);
  chunks.push([start, length]);
  start += length;
}
const cbcLast = (key, iv, data) => {
  const padded = Buffer.concat([data, Buffer.alloc((16 - (data.length % 16)) % 16)]);
  const c = createCipheriv('aes-128-cbc', key, iv).setAutoPadding(false);
  const out = Buffer.concat([c.update(padded), c.final()]);
  return out.subarray(out.length - 16);
};
const chunkMacs = chunks.map(([start, length]) =>
  cbcLast(aesKey, Buffer.concat([nonce, nonce]), plain.subarray(start, start + length)));
const fileMac = cbcLast(aesKey, Buffer.alloc(16), Buffer.concat(chunkMacs));
const metaMac = [
  (fileMac.readUInt32BE(0) ^ fileMac.readUInt32BE(4)) >>> 0,
  (fileMac.readUInt32BE(8) ^ fileMac.readUInt32BE(12)) >>> 0,
];

const tail = [...nonceWords, ...metaMac];
const linkKey = Buffer.alloc(32);
[...keyWords.map((w, i) => (w ^ tail[i]) >>> 0), ...tail]
  .forEach((w, i) => linkKey.writeUInt32BE(w >>> 0, i * 4));

const attributes = Buffer.from('MEGA{"n":"Sousou no Frieren - 01.mp4"}');
const attrPadded = Buffer.concat([attributes, Buffer.alloc((16 - (attributes.length % 16)) % 16)]);
const attrCipher = createCipheriv('aes-128-cbc', aesKey, Buffer.alloc(16)).setAutoPadding(false);
const at = Buffer.concat([attrCipher.update(attrPadded), attrCipher.final()]);

console.log(JSON.stringify({
  size,
  linkKey: linkKey.toString('base64url'),
  cipherSha256: createHash('sha256').update(cipher).digest('hex'),
  plainSha256: createHash('sha256').update(plain).digest('hex'),
  attributes: at.toString('base64url'),
}, null, 2));
