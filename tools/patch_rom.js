#!/usr/bin/env node
/**
 * Patch a 16 MB Tamagotchi Paradise backup ROM with build/tama_stage.bin
 * using Paramix Studio's own installApp() and stub_tpl.bin.
 *
 * Usage:
 *   node tools/patch_rom.js <backup_rom.bin> [app.bin] [out_rom.bin]
 */
'use strict';

const fs = require('fs');
const vm = require('vm');
const path = require('path');

const backupPath = process.argv[2];
const appPath = process.argv[3] || path.join(__dirname, '..', 'build', 'tama_stage.bin');
const outPath = process.argv[4] || path.join(__dirname, '..', 'build', 'paradise_water_stage.bin');

if (!backupPath) {
  console.error('Usage: node tools/patch_rom.js <backup_rom.bin> [app.bin] [out_rom.bin]');
  process.exit(1);
}

const studioHtml = fs.readFileSync(path.join(__dirname, 'ParamixStudio-v1.7.6.html'), 'utf8');
const scripts = [...studioHtml.matchAll(/<script>([\s\S]*?)<\/script>/g)].map((m) => m[1]);
const emuSrc = scripts.find((s) => s.includes('function installApp(') && s.includes('EMU_STUB_B64'));
if (!emuSrc) {
  throw new Error('Could not locate ParamixEmu script in tools/ParamixStudio-v1.7.6.html');
}

const stubMatch = emuSrc.match(/\bEMU_STUB_B64\s*=\s*'([^']+)'/);
if (!stubMatch) {
  throw new Error('Could not locate EMU_STUB_B64 in ParamixEmu script');
}
const stubTpl = new Uint8Array(Buffer.from(stubMatch[1], 'base64'));

const ctx = { window: {}, globalThis: {}, Uint8Array, Array, Object, Math, String, Number, Buffer, WeakMap, Error };
ctx.window = ctx;
ctx.globalThis = ctx;
vm.runInNewContext(emuSrc, ctx);

const rom = new Uint8Array(fs.readFileSync(backupPath));
const app = new Uint8Array(fs.readFileSync(appPath));
console.log(`Backup ROM : ${backupPath} (${rom.length} bytes)`);
console.log(`App binary : ${appPath} (${app.length} bytes)`);
console.log(`Stub TPL   : ${stubTpl.length} bytes`);

const patched = ctx.ParamixEmu.installApp(rom, app, stubTpl);
fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, patched);
console.log(`Patched ROM: ${outPath} (${patched.length} bytes)`);
