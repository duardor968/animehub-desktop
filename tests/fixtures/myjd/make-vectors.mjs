// Reference values for the MyJDownloader protocol, computed with WebCrypto the way the
// `jdownloader-connect` client of AnimeHub Web does:
//   node tests/fixtures/myjd/make-vectors.mjs
const enc = new TextEncoder();
const hex = (buffer) => [...new Uint8Array(buffer)].map((x) => x.toString(16).padStart(2, '0')).join('');
const sha256 = (data) => crypto.subtle.digest('SHA-256', data);

const email = 'duardo@example.test';
const password = 'contraseña de prueba';
const session = 'a1b2c3d4e5f60718293a4b5c6d7e8f90';

const login = await sha256(enc.encode(`${email}${password}server`));
const device = await sha256(enc.encode(`${email}${password}device`));
const merge = (a, tokenHex) => {
  const b = new Uint8Array(tokenHex.match(/[\dA-F]{2}/gi).map((s) => parseInt(s, 16)));
  const out = new Uint8Array(a.byteLength + b.byteLength);
  out.set(new Uint8Array(a), 0);
  out.set(b, a.byteLength);
  return sha256(out);
};
const serverToken = await merge(login, session);
const deviceToken = await merge(device, session);

const path = `/my/connect?${new URLSearchParams({ email, appkey: 'animehub-desktop', rid: '1791990000000' })}`;
const hmacKey = await crypto.subtle.importKey('raw', login, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
const signature = hex(await crypto.subtle.sign('HMAC', hmacKey, enc.encode(path)));

const payload = JSON.stringify({ apiVer: 1, rid: 1791990000001, url: '/linkgrabberv2/addLinks' });
const iv = deviceToken.slice(0, 16);
const aesKey = await crypto.subtle.importKey('raw', deviceToken.slice(16, 32), { name: 'AES-CBC' }, false, ['encrypt']);
const cipher = Buffer.from(await crypto.subtle.encrypt({ name: 'AES-CBC', iv }, aesKey, enc.encode(payload))).toString('base64');

console.log(JSON.stringify({
  email, password, session, path,
  login: hex(login), device: hex(device),
  serverToken: hex(serverToken), deviceToken: hex(deviceToken),
  signature, payload, cipher,
}, null, 2));
