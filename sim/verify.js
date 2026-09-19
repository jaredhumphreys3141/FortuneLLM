// Checks the simulator's ports against the C code they were ported from.
//
//   node verify.js
//
// The engine check is the important one: it runs the JS TinyGPT in greedy mode,
// where no random number is involved, and compares the fortune it writes with
// the one ../host/tinygpt --greedy writes from the same weights. They have to
// match character for character. If ../host/tinygpt has not been built, the
// check falls back to the output recorded below.
//
// It also renders a fortune screen, two landscapes and the boot screen to PNG,
// so the drawing ports can be looked at without a browser.

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { execFileSync } = require('child_process');

global.window = global;
global.performance = { now: () => Number(process.hrtime.bigint() / 1000n) / 1000 };

const HERE = __dirname;
for (const f of ['synthfont.js', 'scene.js', 'tinygpt.js', 'device.js']) require(path.join(HERE, f));

const meta = JSON.parse(fs.readFileSync(path.join(HERE, 'meta.json'), 'utf8'));
const b64 = s => { const b = Buffer.from(s, 'base64'); return new Uint8Array(b); };
const wb = b64(fs.readFileSync(path.join(HERE, 'weights.b64.txt'), 'utf8').trim());
const gpt = new window.TinyGPT(meta, wb.buffer.slice(wb.byteOffset, wb.byteOffset + wb.byteLength));

let failures = 0;
function check(label, pass, detail) {
  console.log((pass ? '  ok   ' : '  FAIL ') + label + (detail ? '   ' + detail : ''));
  if (!pass) failures++;
}

// ---- the engine, against ../host/tinygpt --greedy ----------------------------
// Recorded from ./tinygpt --greedy at the time this was written; used only when
// the harness has not been built.
const RECORDED = 'You will soon be rewarded for your curiosity before long.';
let expected = RECORDED, source = 'recorded output (host/tinygpt not built)';
const harness = path.join(HERE, '..', 'host', 'tinygpt');
if (fs.existsSync(harness)) {
  expected = execFileSync(harness, ['--greedy', '--max', '110'], { encoding: 'utf8' }).trim();
  source = 'host/tinygpt --greedy';
}

gpt.reset();
let out = '', tok = 0;
for (let i = 0; i < 110; i++) {
  const logits = gpt.step(tok);
  let best = 0;
  for (let k = 1; k < gpt.V; k++) if (logits[k] > logits[best]) best = k;
  tok = best;
  if (tok === 0) break;
  out += gpt.chars[tok];
}
console.log('\nEngine (gpt.h -> tinygpt.js), checked against ' + source);
check('greedy fortune matches', out === expected);
console.log('       C : ' + JSON.stringify(expected));
console.log('       JS: ' + JSON.stringify(out));

// The board computes in 32-bit floats and this runs in doubles, so the logits
// agree closely rather than exactly. A sampled fortune can therefore diverge
// from the board's even with the same seed; a greedy one does not.
if (fs.existsSync(harness)) {
  const dump = execFileSync(harness, ['--logits', '--prompt', 'The s'], { encoding: 'utf8' });
  const steps = dump.split('\n').filter(l => l && !l.startsWith('#')).map(l => l.trim().split(/\s+/).map(Number));
  const tokenOf = c => meta.chars.indexOf(c.charCodeAt(0));
  const fed = [0, ...'The s'.split('').map(tokenOf)];
  gpt.reset();
  let worst = 0;
  fed.forEach((t, i) => {
    const lg = gpt.step(t);
    if (!steps[i]) return;
    for (let k = 0; k < gpt.V; k++) worst = Math.max(worst, Math.abs(lg[k] - steps[i][k]));
  });
  check('logits agree to 1e-4', worst < 1e-4, 'worst |diff| ' + worst.toExponential(2));
}

// ---- the blocklist (blocklist.h -> tinygpt.js) -------------------------------
const bb = b64(meta.blockHashB64);
const blocklist = new window.Blocklist(
  new Uint32Array(bb.buffer, bb.byteOffset, meta.blockCount), meta.blockMaxWords);
console.log('\nBlocklist (blocklist.h -> tinygpt.js)');
check('table is sorted, so the binary search works', (() => {
  const h = new Uint32Array(bb.buffer, bb.byteOffset, meta.blockCount);
  for (let i = 1; i < h.length; i++) if ((h[i] >>> 0) < (h[i - 1] >>> 0)) return false;
  return true;
})());
check('ordinary words stay allowed', !blocklist.contains('go to hell today.')
  && !blocklist.contains('careful analysis pays.'));

// ---- the drawing code (synthfont.h, scene.h) ---------------------------------
const W = window.SCREEN_W, H = window.SCREEN_H;
const T = (() => { const t = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
const crc32 = b => { let c = 0xFFFFFFFF; for (let i = 0; i < b.length; i++) c = T[(c ^ b[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; };
function png(px, name) {
  const raw = Buffer.alloc((W * 3 + 1) * H);
  let o = 0;
  for (let y = 0; y < H; y++) {
    raw[o++] = 0;
    for (let x = 0; x < W; x++) {
      const v = px[y * W + x];
      raw[o++] = (v >> 11) << 3; raw[o++] = ((v >> 5) & 0x3F) << 2; raw[o++] = (v & 0x1F) << 3;
    }
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body));
    return Buffer.concat([len, body, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
  fs.writeFileSync(path.join(HERE, name), Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

console.log('\nDrawing (synthfont.h, scene.h -> synthfont.js, scene.js)');
const fb = new Uint16Array(W * H), dist = new Float32Array(W * H);
window.synth.render(fb, dist, W, H, expected);
png(fb, 'shot-fortune.png');
check('fortune screen renders and wraps',
  window.synth.fit(expected, W, H).lines.length > 1, 'shot-fortune.png');
for (const [seed, name] of [[20250919, 'shot-scene1.png'], [7, 'shot-scene2.png']]) {
  const s = window.scene.render(fb, dist, W, H, seed);
  png(fb, name);
  check('landscape seed ' + seed, !!s.palette, name + '   ' + s.palette + ', ' + s.near);
}

const dev = new window.Device({
  gpt, blocklist, meta, font: b64(meta.fontB64), onSerial() {}, onState() {}
});
dev.msPerChar = 0;
dev.boot(12345);
for (let i = 0; i < 60 && !dev.gen.finished; i++) dev.stepFortune(50);
png(dev.gfx.px, 'shot-boot.png');
check('boot screen renders', dev.gen.out.length > 0, 'shot-boot.png');

console.log(failures ? '\n' + failures + ' check(s) failed\n' : '\nall checks passed\n');
process.exit(failures ? 1 : 0);
