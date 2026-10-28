'use strict';

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const reference = require('../manuals/ST20/ST20_C2_C4-Core-Instruction-Set-Reference-Manual--72-TRN-273-01.json');
const catalog = new Map(reference.instructions.map(entry => [entry.mnemonic, entry]));
const options = { rounds: 2, samples: 16, seed: 0x20c2c4, operations: null, isolate: 1, priority: 0, interrupted: 0, trapshadow: 0, trapmasks: 0, crossshadow: 0 };
const selected = [
  'add', 'sub', 'sum', 'diff', 'prod', 'mul', 'div', 'rem', 'gt',
  'and', 'or', 'xor', 'not', 'rev', 'dup', 'xdble', 'xsword', 'xbword',
  'bitcnt', 'bitrevword', 'bitrevnbits', 'bcnt', 'wcnt', 'bsub', 'wsub', 'wsubdb',
  'norm', 'lsum', 'ldiff', 'ladd', 'lsub', 'lmul', 'ldiv', 'lshl', 'lshr', 'shl', 'shr',
  'satadd', 'satsub', 'satmul', 'slmul', 'sulmul', 'crcbyte', 'crcword', 'fmul',
  'unpacksn', 'fptesterr', 'ldinf', 'cb', 'cbu', 'cs', 'csu', 'csngl', 'cflerr',
  'ccnt1', 'csub0', 'cword', 'cir', 'ciru', 'adc', 'eqc', 'testerr', 'seterr',
  'gtu', 'ssub', 'pop', 'mint', 'ldpri', 'ldpi', 'ldlp', 'ldnlp', 'nop', 'xword',
  'ldmemstartval', 'testhalterr', 'clrhalterr', 'sethalterr',
  'lb', 'lbx', 'ls', 'lsx', 'ldnl', 'sb', 'ss', 'stnl',
  'devlb', 'devls', 'devlw', 'devsb', 'devss', 'devsw', 'move', 'devmove',
  'move2dinit', 'move2dall', 'move2dzero', 'move2dnonzero',
  'gintenb', 'gintdis', 'trapenb', 'trapdis', 'clockenb', 'clockdis',
  'ldclock', 'ldtimer', 'stclock', 'sttimer', 'testpranal', 'ldprodid', 'lddevid',
  'saveh', 'savel', 'sthb', 'sthf', 'stlb', 'stlf', 'swapqueue', 'insertqueue',
  'runp', 'startp', 'timeslice', 'wait', 'signal', 'resetch', 'stoperr', 'causeerror',
  'alt', 'talt', 'enbc', 'enbs', 'enbt', 'disc', 'diss', 'dist', 'altwt', 'taltwt', 'altend',
  'ldtraph', 'sttraph', 'ldtrapped', 'sttrapped', 'ldshadow', 'stshadow',
  'intdis', 'intenb', 'settimeslice',
  'in', 'out', 'outbyte', 'outword', 'swaptimer', 'tin',
  'ajw', 'gajw', 'j', 'cj', 'gcall', 'call', 'ret', 'lend', 'endp', 'restart', 'iret', 'stopp', 'tret',
  'postnormsn', 'roundsn', 'reboot',
];
const loads = new Set(['lb', 'lbx', 'ls', 'lsx', 'ldnl', 'devlb', 'devls', 'devlw']);
const stores = new Set(['sb', 'ss', 'stnl', 'devsb', 'devss', 'devsw']);
const copies = new Set(['move', 'devmove', 'move2dinit', 'move2dall', 'move2dzero', 'move2dnonzero']);
const clocks = new Set(['clockenb', 'clockdis', 'ldclock', 'ldtimer', 'stclock', 'sttimer']);
const queues = new Set(['saveh', 'savel', 'sthb', 'sthf', 'stlb', 'stlf', 'swapqueue', 'insertqueue', 'runp', 'startp', 'timeslice']);
const alternatives = new Set(['alt', 'talt', 'enbc', 'enbs', 'enbt', 'disc', 'diss', 'dist', 'altwt', 'taltwt', 'altend']);
const records = new Set(['ldtraph', 'sttraph', 'ldtrapped', 'sttrapped']);
const channels = new Set(['in', 'out', 'outbyte', 'outword']);
const contextChanges = new Set(['ajw', 'gajw', 'call', 'ret', 'endp', 'restart', 'iret', 'stopp', 'tret']);
const nativeUnavailable = new Set(['reboot']);
function instructionBytes(name) {
  if (name === 'taltwt') return [0x25, 0xf1];
  return catalog.get(name).encoding.bytes_hex.map(value => parseInt(value, 16));
}
function fixtureScope(name) {
  if (loads.has(name) || stores.has(name)) return 'initialized RAM, supported alignments and short word offsets; no MMIO';
  if (copies.has(name)) return 'native atomic RAM copies, lengths 0..16, two rows/stride 20 for 2D; no interrupted operations';
  if (alternatives.has(name)) return 'ready/skip/channel guard paths; no blocking wait or timer expiry; altend offset 0';
  if (records.has(name)) return 'four-word records, all groups and both priorities; no monitor traps';
  if (['ldshadow', 'stshadow'].includes(name)) return `${options.crossshadow ? `LP caller captured by HP run trap/tret, ${options.crossshadow >= 4 ? options.crossshadow % 2 ? 'invalid return to queued LP fallback' : 'invalidation/reactivation' : options.crossshadow >= 2 ? 'edited valid Status/Wptr/Iptr' : 'fixed caller Wptr/Iptr'}, ` : options.trapshadow ? 'HP breakpoint handler/tret, independent ' : 'inactive '}LP bank, idle block group; no native pending operation`;
  if (channels.has(name)) return 'matched software-channel peer, lengths 0..16 or scalar output; no external channel';
  if (clocks.has(name)) return 'clock-register data and enable masks with automatic ticks frozen';
  if (queues.has(name)) return name === 'timeslice' ? 'empty ready queues; no rotation/preemption' : 'RAM queue mutation and readback; no preemption';
  if (['ldprodid', 'lddevid'].includes(name)) return 'isolated getter with local configured ID aligned to oracle; hardware default/alias verified separately';
  if (['intdis', 'intenb'].includes(name)) return 'LP stack effects with no pending HP work; preemption suppression is not covered';
  if (name === 'settimeslice') return 'LP boolean state readback; no periodic quantum expiry';
  if (name === 'wait') return 'nonzero semaphore count only; no blocking';
  if (name === 'signal') return 'empty waiter queue and count increment/wrap';
  if (name === 'resetch') return 'software-channel reset; no hardware channel';
  if (name === 'stoperr') return 'clear error flag, fallthrough only';
  if (name === 'tin') return 'already-expired deadline with frozen clock; no blocking';
  if (name === 'swaptimer') return 'timer-front swap and timestamp read; no alarm expiry';
  if (name === 'lend') return 'boundary and seeded word counts, zero branch distance and RAM index/count readback';
  if (name === 'endp') return 'final branch/count 1 only';
  if (name === 'restart') return 'valid native record with no pending operation';
  if (name === 'iret') return 'valid standalone save frame; simulator controller context remains unresolved';
  if (name === 'tret') return options.trapmasks ? 'independent caller/handler global and trap masks, direct handler reads and return restoration' : 'breakpoint handler entry and return, aligned handler stack inputs';
  if (name === 'causeerror') return 'enabled causes with group handlers; unexpected core control flow blocks completion';
  if (name === 'postnormsn') return 'native workspace exponent, shifts 0..64, denormal guards, exponent wrap/clamp and norm/postnormsn/roundsn composition';
  if (name === 'roundsn') return 'native biased exponent, nearest-even ties, sticky guards, subnormal carry and overflow';
  if (nativeUnavailable.has(name)) return name === 'reboot' ? 'bounded probe; emulator reboot restarts at START_ADDR (boot from ROM), not compared' : 'zero native FP probe; emulator native FP format is unsupported';
  return 'boundary and seeded operands in native mode; defined architectural results only';
}
const boundaries = [0, 1, 2, 31, 32, 127, 128, 255, 256, 32767, 32768, 65535,
  65536, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff];
const origin = 0x7ff00000;
const hex = value => (value >>> 0).toString(16).padStart(8, '0');

function run(command, args, extra = {}) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', timeout: 90000,
    maxBuffer: 64 * 1024 * 1024, ...extra });
  if (result.error || result.status !== 0)
    throw new Error(`${command} failed: ${result.error || result.status}\n${result.stdout || ''}${result.stderr || ''}`);
  return result.stdout;
}

function encode(functionNibble, value) {
  const word = value >>> 0;
  if (word < 16) return [(functionNibble << 4) | word];
  const prefix = word & 0x80000000
    ? [...encode(6, (~word) >>> 4)]
    : [...encode(2, word >>> 4)];
  return [...prefix, (functionNibble << 4) | (word & 15)];
}

function generate(seed, operations) {
  let randomState = seed >>> 0;
  function nextWord() {
    randomState ^= randomState << 13;
    randomState ^= randomState >>> 17;
    randomState ^= randomState << 5;
    return randomState >>> 0;
  }
  const bytes = [];
  const instructions = [];
  const cases = [];
  const fixups = [];
  const unexecutedAddresses = new Set();
  let lowWorkspace = 0x80000200;
  function emit(name, code, owner, setup = false, errorProbe = false) {
    let decodedOperand = 0;
    for (const byte of code) {
      decodedOperand = ((decodedOperand << 4) | (byte & 15)) >>> 0;
      if ((byte >> 4) === 6) decodedOperand = (~decodedOperand) >>> 0;
    }
    instructions.push({ address: origin + bytes.length, name, owner, setup, errorProbe, operand: decodedOperand });
    bytes.push(...code);
  }
  function secondary(name, owner, setup = false) {
    emit(name, instructionBytes(name), owner, setup);
  }
  function writeWord(address, value, owner, device = false) {
    emit('ldc', encode(4, value), owner, true);
    emit('ldc', encode(4, address), owner, true);
    if (device) secondary('devsw', owner, true);
    else emit('stnl', encode(14, 0), owner, true);
  }
  function stack(values, owner, setup = true) {
    for (const value of values) emit('ldc', encode(4, value), owner, setup);
  }
  function addressWord(address, resolve, owner) {
    const offset = bytes.length;
    emit('ldc', [...Array(7).fill(0x20), 0x40], owner, true);
    fixups.push({ offset, functionNibble: 4, resolve });
    stack([address], owner);
    emit('stnl', encode(14, 0), owner, true);
  }
  function branch(name, functionNibble, resolve, owner, setup = false) {
    const offset = bytes.length;
    emit(name, [...Array(7).fill(0x20), functionNibble << 4], owner, setup);
    fixups.push({ offset, functionNibble, resolve: () => (resolve() - origin - offset - 8) >>> 0 });
  }
  if (options.priority && options.interrupted < 2) {
    let entry;
    writeWord(0x800001f8, 0x80000000, 0);
    addressWord(0x800001fc, () => entry, 0);
    stack([0x80000201], 0);
    secondary('runp', 0, true);
    secondary('stopp', 0, true);
    entry = origin + bytes.length;
  }
  if (options.interrupted) {
    const name = operations[0];
    let handler;
    let finish;
    let lowEntry;
    let skippedInstructions = 0;
    const timerProbe = options.interrupted >= 2;
    const deviceProbe = options.interrupted >= 3;
    const source = deviceProbe ? 0x40010000 : 0x80001000;
    cases.push({ name, sample: 0, source: hex(source), destination: '80001200', length: 64 });
    stack([3], 0); secondary('clockenb', 0, true);
    if (!timerProbe) {
      writeWord(0x80003ff8, 0x80000000, 0);
      addressWord(0x80003ffc, () => handler, 0);
      writeWord(0x1ffffffc, 0x20000100, 0);
      writeWord(0x2000001c, 0x80004000, 0, true);
      writeWord(0x200000c0, 0x10080, 0, true);
      writeWord(0x20000100, 0, 0, true);
    }
    stack([0xff], 0); secondary('gintenb', 0, true);
    for (let index = 0; index < 48; ++index) {
      writeWord(source + index * 4, (seed ^ (index * 0x1020304)) >>> 0, 0);
      writeWord(0x80001200 + index * 4, 0xa5a55a5a, 0);
    }
    if (timerProbe) {
      stack([0, 0], 0); secondary('stclock', 0, true);
      if (deviceProbe) writeWord(0x800000f8, 0x80000000, 0);
      writeWord(0x800001f8, 0x80000000, 0);
      addressWord(0x800001fc, () => lowEntry, 0);
      stack([0x80000201], 0); secondary('runp', 0, true);
      stack([0], 0); secondary('tin', 0, true);
      handler = origin + bytes.length;
      const handlerStart = instructions.length;
      stack([0x80006100, 1, 15], 0); secondary('stshadow', 0);
      for (let index = 0; index < 12; ++index) {
        stack([0x80006100 + index * 4], 0, false);
        emit('ldnl', encode(3, 0), 0);
      }
      if (deviceProbe) {
        for (let index = 0; index < 10; ++index) {
          stack([0x80006500 + index * 4], 0, false);
          emit('ldnl', encode(3, 0), 0);
        }
      }
      branch('j', 0, () => finish, 0, true);
      skippedInstructions = instructions.length - handlerStart;
      lowEntry = origin + bytes.length;
    }
    if (name.startsWith('move2d')) {
      stack([80, 80, 2], 0); secondary('move2dinit', 0, true);
    }
    stack([source, 0x80001200, 64], 0);
    const interruptCount = instructions.length + 1 - skippedInstructions;
    secondary(name, 0);
    branch('j', 0, () => finish, 0, true);
    if (!timerProbe) handler = origin + bytes.length;
    for (const priority of timerProbe ? [] : [0, 1]) {
      stack([0x80006000 + priority * 0x100, priority, 15], 0);
      secondary('stshadow', 0);
    }
    for (const priority of timerProbe ? [] : [0, 1]) {
      for (let index = 0; index < 12; ++index) {
        stack([0x80006000 + priority * 0x100 + index * 4], 0, false);
        emit('ldnl', encode(3, 0), 0);
      }
    }
    finish = origin + bytes.length;
    if (deviceProbe) {
      for (let index = 0; index < 10; ++index) {
        stack([0x80006500 + index * 4], 0, false);
        emit('ldnl', encode(3, 0), 0);
      }
    }
    cases[0].interruptCount = interruptCount;
    cases[0].handler = hex(handler);
  }
  for (const name of options.interrupted ? [] : operations) {
    const entry = catalog.get(name);
    if (!entry || (!entry.encoding.bytes_hex && entry.instruction_class !== 'primary' && name !== 'taltwt'))
      throw new Error(`No reference encoding for ${name}`);
    for (let sample = 0; sample < boundaries.length + options.samples; ++sample) {
      let areg = sample < boundaries.length ? boundaries[sample] : nextWord();
      let breg = sample < boundaries.length ? boundaries[(sample * 7 + 3) % boundaries.length] : nextWord();
      let creg = sample < boundaries.length ? boundaries[(sample * 11 + 5) % boundaries.length] : nextWord();
      if (['shl', 'shr', 'bitrevnbits'].includes(name)) areg %= 33;
      if (['lshl', 'lshr'].includes(name)) areg %= 65;
      if (['ladd', 'lsub', 'lsum', 'ldiff'].includes(name)) creg &= 1;
      if (['div', 'rem', 'ldiv'].includes(name)) areg = areg || 1;
      if (name === 'ldiv') creg %= areg;
      if (name === 'cword') areg = (2 ** (sample % 32)) >>> 0;
      if (name === 'xword') {
        areg = (2 ** (sample % 32)) >>> 0;
        if (areg !== 0x80000000) breg &= (areg * 2 - 1) >>> 0;
      }
      const owner = cases.length;
      let immediate = sample < boundaries.length ? boundaries[(sample + 9) % boundaries.length] : nextWord();
      const pattern = areg;
      const memoryChecks = [];
      let continuation;
      let subroutine;
      let resume;
      let returnAddress;
      if (loads.has(name) || stores.has(name)) {
        const byteAccess = ['lb', 'lbx', 'sb', 'devlb', 'devsb'].includes(name);
        const shortAccess = ['ls', 'lsx', 'ss', 'devls', 'devss'].includes(name);
        areg = 0x80001100 + (byteAccess ? sample % 4 : shortAccess ? (sample % 2) * 2 : 0);
        if (['ldnl', 'stnl'].includes(name)) immediate = (sample % 7 - 3) >>> 0;
        const target = (areg + (['ldnl', 'stnl'].includes(name) ? immediate * 4 : 0)) >>> 0;
        memoryChecks.push(target & 0xfffffffc);
      }
      if (copies.has(name)) {
        areg = name === 'move2dinit' ? 2 : sample % 17;
        breg = name === 'move2dinit' ? 20 : 0x80001200 + sample % 4;
        creg = name === 'move2dinit' ? 20 : 0x80001000 + sample % 4;
        for (let index = 0; index < 16; ++index) memoryChecks.push(0x80001200 + index * 4);
      }
      if (clocks.has(name))
        areg = ['ldclock', 'stclock'].includes(name) ? sample % 2 : name === 'ldtimer' || name === 'sttimer' ? pattern : sample % 4;
      if (['trapenb', 'trapdis'].includes(name)) { areg &= 0x3fff; breg = 1; }
      if (queues.has(name)) {
        areg = ['swapqueue', 'insertqueue'].includes(name) ? sample % 2 :
          ['saveh', 'savel'].includes(name) ? 0x80006000 : name === 'startp' ? 0x80004000 :
          name === 'runp' ? 0x80004000 + sample % 2 : name === 'timeslice' ? pattern : 0x80004000;
        if (['swapqueue', 'insertqueue'].includes(name)) breg = 0x80004000;
        if (name === 'startp') breg = 0;
        if (['swapqueue', 'insertqueue'].includes(name)) creg = 0x80004000;
        memoryChecks.push(0x80006000, 0x80006004, 0x80003ff8, 0x80003ffc);
      }
      if (['wait', 'signal', 'resetch'].includes(name)) {
        areg = 0x80003000;
        memoryChecks.push(areg);
        if (name !== 'resetch') memoryChecks.push(areg + 4, areg + 8);
      }
      if (name === 'causeerror') {
        areg = (1 << (sample % 14)) >>> 0;
        if (areg >= 0x40) breg = options.priority;
      }
      if (alternatives.has(name)) {
        areg = ['disc', 'diss', 'dist'].includes(name) ? sample % 8 : sample % 2;
        if (name === 'enbc') breg = 0x80003000;
        if (['disc', 'diss', 'dist'].includes(name)) breg = sample % 2;
        if (name === 'disc') creg = 0x80003000;
        if (name === 'enbt') breg = 100 + sample;
        if (name === 'dist') creg = 0xfffffff0;
        memoryChecks.push(0x800000ec, 0x800000f0, 0x800000f4, 0x80000100, 0x80003000);
      }
      if (records.has(name)) {
        areg = sample % 4;
        breg = 0x80006000;
        creg = sample % 2;
        const record = 0x80000040 + creg * 0x80 + areg * 0x20 + (name.includes('trapped') ? 16 : 0);
        const destination = name.startsWith('ld') ? record : breg;
        for (let index = 0; index < 4; ++index) memoryChecks.push(destination + index * 4);
      }
      if (['ldshadow', 'stshadow'].includes(name)) {
        areg = options.crossshadow >= 4 ? 7 : sample % 16;
        breg = 1;
        creg = name === 'ldshadow' ? 0x80006000 : 0x80006200;
        for (let index = 0; index < 12; ++index) memoryChecks.push(0x80006200 + index * 4);
      }
      if (name === 'settimeslice') areg = sample % 2;
      if (channels.has(name)) {
        areg = ['in', 'out'].includes(name) ? sample % 17 : pattern;
        breg = 0x80003000;
        if (['in', 'out'].includes(name)) creg = 0x80001000;
        const destination = name === 'in' ? 0x80001000 : 0x80001200;
        for (let index = 0; index < 8; ++index) memoryChecks.push(destination + index * 4);
        memoryChecks.push(0x80003000, 0x80006000, 0x80006004);
      }
      if (name === 'swaptimer') { areg = sample % 2; breg = 0x80004000; }
      if (name === 'tin') areg = (0xffffffff - sample % 16) >>> 0;
      if (name === 'ajw') immediate = (sample % 9 - 4) >>> 0;
      if (name === 'gajw') areg = 0x80001000 + sample * 32;
      if (name === 'j') immediate = 1;
      if (name === 'cj') { areg = sample % 2; immediate = areg === 0 ? 1 : 0; }
      if (name === 'gcall') areg = 0;
      if (name === 'lend') { areg = 0; breg = 0x80003000; memoryChecks.push(breg, breg + 4); }
      if (name === 'endp') { areg = 0x80004000; memoryChecks.push(areg, areg + 4); }
      if (name === 'restart') areg = 0x80006000;
      if (name === 'call') memoryChecks.push(0x800000f0, 0x800000f4, 0x800000f8, 0x800000fc);
      if (name === 'reboot') areg = breg = creg = 0;
      if (name === 'postnormsn') creg = sample % 65;
      if (name === 'roundsn') creg = sample < boundaries.length ? boundaries[sample] : nextWord() % 300;
      let temporary = pattern;
      let normalizeFirst = false;
      if (['postnormsn', 'roundsn'].includes(name) && sample >= boundaries.length) {
        const fpSample = sample - boundaries.length;
        const exponents = [-65, -64, -63, -33, -32, -31, -24, -23, -2, -1, 0, 1, 2,
          126, 127, 128, 253, 254, 255, 256, 0x80000000, 0x7fffffff];
        const exponent = exponents[Math.floor(fpSample / 8) % exponents.length];
        areg = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0, 1, nextWord()][fpSample % 8];
        breg = [0x80000000, 0x8000007f, 0x80000080, 0x80000081,
          0x80000180, 0xffffff80, 0xffffffff, nextWord() | 0x80000000][fpSample % 8] >>> 0;
        if (name === 'postnormsn') {
          normalizeFirst = fpSample >= exponents.length * 8;
          if (normalizeFirst) {
            breg >>>= fpSample % 32;
            if (areg === 0 && breg === 0) areg = 1;
            creg = breg !== 0 ? Math.clz32(breg) : 32 + Math.clz32(areg);
          }
          temporary = (exponent + creg) >>> 0;
        }
        else { creg = exponent >>> 0; if (exponent === 0) breg &= 0x7fffffff; }
      }
      cases.push({ name, sample, areg: hex(areg), breg: hex(breg), creg: hex(creg), immediate: hex(immediate),
        ...(name === 'postnormsn' ? { temporary: hex(temporary), normalizeFirst } : {}) });
      if (options.trapmasks) {
        cases[owner].callerGlobal = hex(areg & 0xffff);
        cases[owner].handlerGlobal = hex(breg & 0xffff);
        cases[owner].callerTraps = hex((creg & 0x3fff) | 1);
        cases[owner].handlerTraps = hex(immediate & 0x3ffe);
        memoryChecks.push(0x80004018, 0x8000401c,
          (options.priority ? 0x80000200 : 0x80000100) + 24,
          (options.priority ? 0x80000200 : 0x80000100) + 28);
      }
      emit('clrhalterr', catalog.get('clrhalterr').encoding.bytes_hex.map(value => parseInt(value, 16)), owner, true);
      emit('testerr', catalog.get('testerr').encoding.bytes_hex.map(value => parseInt(value, 16)), owner, true);
      if (['intdis', 'intenb', 'settimeslice'].includes(name)) {
        secondary(name === 'intenb' ? 'intdis' : 'intenb', owner, true);
        if (name === 'settimeslice') { stack([1], owner); secondary('settimeslice', owner, true); }
      }
      if (channels.has(name)) {
        for (const priority of [0, 1]) {
          stack([priority, 0x3fff], owner); secondary('trapdis', owner, true);
          stack([0x80000000, 0x80000000, priority], owner); secondary('swapqueue', owner, true);
        }
        writeWord(0x80003000, 0x80004000, owner);
        writeWord(0x80003ff4, 0x80001200, owner);
        writeWord(0x80003ff8, 0x80000000, owner);
        writeWord(0x80003ffc, origin, owner);
        for (let index = 0; index < 8; ++index) {
          const value = (pattern ^ (index * 0x1020304)) >>> 0;
          writeWord(0x80001000 + index * 4, name === 'in' ? 0xa5a55a5a : value, owner);
          writeWord(0x80001200 + index * 4, name === 'in' ? value : 0xa5a55a5a, owner);
        }
      }
      if (name === 'swaptimer' || name === 'tin') {
        stack([3], owner); secondary('clockdis', owner, true);
        for (const priority of [0, 1]) {
          stack([0, priority], owner); secondary('stclock', owner, true);
          stack([0x80000000, priority], owner); secondary('swaptimer', owner, true);
        }
        writeWord(0x80003fec, 0x10000000, owner);
        writeWord(0x80003ff0, 0x80000000, owner);
      }
      if (name === 'lend') { writeWord(breg, pattern, owner); writeWord(breg + 4, pattern, owner); }
      if (name === 'ret') addressWord(0x80000100, () => continuation, owner);
      if (name === 'endp') { addressWord(areg, () => continuation, owner); writeWord(areg + 4, 1, owner); }
      if (name === 'restart' || name === 'iret') {
        if (name === 'iret') {
          writeWord(0x1ffffffc, 0x20000100, owner);
          writeWord(0x20000100, 0, owner, true);
        }
        const values = name === 'restart'
          ? [0x00ff0000, 0x80100000, 0x80004000, 0, pattern, breg, creg, 0, 0, 0, 0, 0]
          : [0x80100000, 0x80004000, 0, pattern, breg, creg];
        const base = name === 'restart' ? areg : 0x80000100;
        const iptrIndex = name === 'restart' ? 3 : 2;
        for (const [index, value] of values.entries()) {
          if (index === iptrIndex) addressWord(base + index * 4, () => continuation, owner);
          else writeWord(base + index * 4, value, owner);
        }
      }
      if (name === 'stopp') {
        for (const priority of [0, 1]) { stack([0x80000000, 0x80000000, priority], owner); secondary('swapqueue', owner, true); }
        writeWord(0x80003ff8, 0x80000000, owner);
        addressWord(0x80003ffc, () => continuation, owner);
        stack([0x80004000], owner); secondary('runp', owner, true);
      }
      if (name === 'tret' || name === 'causeerror') {
        const bit = name === 'tret' ? 0 : sample % 14;
        const group = bit === 0 ? 0 : bit <= 2 ? 1 : bit <= 5 ? 2 : 3;
        const handler = 0x80000040 + options.priority * 0x80 + group * 0x20;
        if (name === 'causeerror') {
          for (const priority of [0, 1]) { stack([priority, 0x3fff], owner); secondary('trapdis', owner, true); }
        }
        if (options.trapmasks) {
          stack([0xffff], owner); secondary('gintdis', owner, true);
          stack([areg & 0xffff], owner); secondary('gintenb', owner, true);
          stack([options.priority, 0x3fff], owner); secondary('trapdis', owner, true);
          stack([options.priority, (creg & 0x3fff) | 1], owner); secondary('trapenb', owner, true);
        }
        writeWord(handler, options.trapmasks ? (((breg & 0xffff) << 16) | (immediate & 0x3ffe)) >>> 0 : 0, owner);
        writeWord(handler + 4, 0x80000000, owner);
        writeWord(handler + 8, 0x80004000, owner);
        addressWord(handler + 12, () => subroutine, owner);
        stack([options.priority, (1 << bit) >>> 0], owner); secondary('trapenb', owner, true);
      }
      if (name === 'postnormsn') writeWord(0x80000100, temporary, owner);
      if (['gintenb', 'gintdis'].includes(name)) { stack([0xff], owner); secondary('gintenb', owner, true); }
      if (['trapenb', 'trapdis'].includes(name) || queues.has(name)) {
        for (const priority of [0, 1]) { stack([priority, 0x3fff], owner); secondary('trapdis', owner, true); }
      }
      if (clocks.has(name)) {
        stack([3], owner); secondary('clockdis', owner, true);
        for (const priority of [0, 1]) { stack([0, priority], owner); secondary('stclock', owner, true); }
        if (['ldclock', 'ldtimer'].includes(name)) {
          stack([pattern, name === 'ldtimer' ? 0 : areg], owner); secondary('stclock', owner, true);
        }
        if (name === 'clockdis') { stack([3], owner); secondary('clockenb', owner, true); }
      }
      if (queues.has(name)) {
        for (const priority of [0, 1]) { stack([0x80000000, 0x80000000, priority], owner); secondary('swapqueue', owner, true); }
        if (options.priority) { stack([1], owner); secondary('gintdis', owner, true); }
        for (const workspace of [0x80004000, 0x80004100]) {
          writeWord(workspace - 8, 0x80000000, owner);
          writeWord(workspace - 4, origin, owner);
        }
        if (['saveh', 'savel', 'insertqueue'].includes(name) && sample % 2) {
          const priority = name === 'saveh' ? 0 : name === 'savel' ? 1 : areg;
          stack([0x80004100, 0x80004100, priority], owner); secondary('swapqueue', owner, true);
        }
        writeWord(0x80006000, 0xa5a55a5a, owner);
        writeWord(0x80006004, 0xa5a55a5a, owner);
      }
      if (['wait', 'signal', 'resetch'].includes(name)) {
        writeWord(areg, name === 'wait' ? sample + 1 : name === 'signal' ? pattern : sample % 2 ? 0x80004000 : 0x80000000, owner);
        if (name !== 'resetch') { writeWord(areg + 4, 0x80000000, owner); writeWord(areg + 8, 0x80000000, owner); }
      }
      if (alternatives.has(name)) {
        for (const address of memoryChecks) writeWord(address, address === 0x80003000 ? 0x80000000 : 0xffffffff, owner);
        secondary(['talt', 'enbt', 'dist', 'taltwt'].includes(name) ? 'talt' : 'alt', owner, true);
        if (['enbs', 'diss', 'altwt', 'taltwt', 'altend'].includes(name)) { stack([1], owner); secondary('enbs', owner, true); }
        if (name === 'disc') { stack([0x80003000, 1], owner); secondary('enbc', owner, true); }
        if (name === 'dist') { stack([0xffffffff, 1], owner); secondary('enbt', owner, true); }
        if (name === 'altend') writeWord(0x80000100, 0, owner);
      }
      if (records.has(name)) {
        const record = 0x80000040 + creg * 0x80 + areg * 0x20 + (name.includes('trapped') ? 16 : 0);
        const source = name.startsWith('ld') ? breg : record;
        for (const [index, value] of [0, 0x80000000, 0x80004000, pattern].entries()) writeWord(source + index * 4, value, owner);
      }
      if (['ldshadow', 'stshadow'].includes(name)) {
        const values = [0x00ff0000, 0x80100000, 0x80007000, origin, pattern, breg, 0x12345678, 0, 0, 0, 0, 0];
        if (options.crossshadow) values[2] = options.crossshadow >= 2 ? 0x80007000 + sample * 64 : 0x80000200;
        if (options.crossshadow >= 2) values[1] = sample % 2 ? 0x80000000 : 0x80100000;
        if (options.crossshadow >= 4) values[1] &= 0x7fffffff;
        for (let index = 0; index < 12; ++index) {
          if (options.crossshadow && index === 3) addressWord(0x80006000 + index * 4,
              () => options.crossshadow >= 2 ? resume : returnAddress, owner);
          else writeWord(0x80006000 + index * 4, values[index], owner);
          writeWord(0x80006200 + index * 4, 0xa5a55a5a, owner);
        }
        if (name === 'stshadow' && !options.crossshadow) { stack([0x80006000, 1, 7], owner); secondary('ldshadow', owner, true); }
        if (options.trapshadow) {
          writeWord(0x80000040, 0xffff0000, owner);
          writeWord(0x80000044, 0x80000000, owner);
          writeWord(0x80000048, 0x80004000, owner);
          addressWord(0x8000004c, () => subroutine, owner);
          stack([0, 1], owner); secondary('trapenb', owner, true);
        }
        if (options.crossshadow) {
          writeWord(0x800000a0, 0xffff0000, owner);
          writeWord(0x800000a4, 0x80000000, owner);
          writeWord(0x800000a8, 0x80004000, owner);
          addressWord(0x800000ac, () => subroutine, owner);
          writeWord(0x800040f8, 0x80000000, owner);
          addressWord(0x800040fc, () => resume, owner);
          stack([0, 0x400], owner); secondary('trapenb', owner, true);
        }
      }
      if (loads.has(name) || stores.has(name))
        writeWord(memoryChecks[0], loads.has(name) ? pattern : 0xa5a55a5a, owner);
      if (copies.has(name)) {
        for (let index = 0; index < 16; ++index) {
          writeWord(0x80001000 + index * 4, (pattern ^ (index * 0x10203)) & 0xff00ff, owner);
          writeWord(0x80001200 + index * 4, 0xa5a55a5a, owner);
        }
        if (name.startsWith('move2d') && name !== 'move2dinit') {
          for (const value of [20, 20, 2]) emit('ldc', encode(4, value), owner, true);
          secondary('move2dinit', owner, true);
        }
      }
      stack([creg, breg], owner);
      if (name === 'gcall') {
        const offset = bytes.length;
        emit('ldc', [...Array(7).fill(0x20), 0x40], owner, true);
        fixups.push({ offset, functionNibble: 4, resolve: () => {
          cases[owner].areg = hex(continuation);
          return continuation;
        } });
      } else stack([areg], owner);
      if (normalizeFirst) secondary('norm', owner, true);
      const code = entry.instruction_class === 'primary'
        ? encode(parseInt(entry.encoding.function_nibble, 16), immediate)
        : instructionBytes(name);
      if (name === 'call') {
        branch('call', 9, () => subroutine, owner);
        const resumeIndex = instructions.length;
        branch('j', 0, () => resume, owner, true);
        subroutine = origin + bytes.length;
        secondary('nop', owner);
        secondary('ret', owner);
        resume = origin + bytes.length;
        instructions.push(instructions.splice(resumeIndex, 1)[0]);
        stack([creg, breg, areg], owner);
      } else if ((options.trapshadow || options.crossshadow) && ['ldshadow', 'stshadow'].includes(name)) {
        if (options.crossshadow) {
          stack([0x80004100], owner);
          secondary('runp', owner, true);
        } else emit('j', encode(0, 0), owner, true);
        returnAddress = origin + bytes.length;
        const resumeIndex = instructions.length;
        branch('j', 0, () => resume, owner, true);
        subroutine = origin + bytes.length;
        stack([creg, breg, areg], owner);
        emit(name, code, owner);
        if (options.crossshadow >= 2) {
          stack([0x80006400, 1, 7], owner);
          secondary('stshadow', owner);
          for (let index = 0; index < 7; ++index) {
            stack([0x80006400 + index * 4], owner);
            emit('ldnl', encode(3, 0), owner);
          }
          if (options.crossshadow === 2) {
            for (let index = 0; index < 4; ++index) {
              stack([0x800000b0 + index * 4], owner);
              emit('ldnl', encode(3, 0), owner);
            }
          }
        }
        if (options.crossshadow >= 4) {
          if (options.crossshadow % 2 === 0) {
            writeWord(0x80006004, sample % 2 ? 0x80000000 : 0x80100000, owner);
            stack([0x80006000, 1, 7], owner);
            secondary('ldshadow', owner);
            stack([0x80006600, 1, 7], owner);
            secondary('stshadow', owner);
            for (let index = 0; index < 7; ++index) {
              stack([0x80006600 + index * 4], owner);
              emit('ldnl', encode(3, 0), owner);
            }
          } else {
            writeWord(0x80008ff8, 0x80000000, owner);
            addressWord(0x80008ffc, () => resume, owner);
            stack([0x80009001], owner);
            secondary('runp', owner);
          }
        }
        secondary('tret', owner, true);
        resume = origin + bytes.length;
        const returnBranch = instructions.splice(resumeIndex, 1)[0];
        if (options.crossshadow < 2 || name !== 'ldshadow' || !(areg & 2)) instructions.push(returnBranch);
        else unexecutedAddresses.add(returnBranch.address);
        if (options.crossshadow >= 2 && name === 'ldshadow' && (areg & 2))
          lowWorkspace = options.crossshadow >= 4 && options.crossshadow % 2 ? 0x80009000 : 0x80007000 + sample * 64;
      } else if (name === 'tret' || name === 'causeerror') {
        if (name === 'tret') emit('j', encode(0, 0), owner, true);
        else secondary('causeerror', owner);
        const resumeIndex = instructions.length;
        branch('j', 0, () => resume, owner, true);
        subroutine = origin + bytes.length;
        if (options.trapmasks) {
          stack([0], owner, false); secondary('gintdis', owner);
          emit('stl', encode(13, 6), owner);
          stack([options.priority, 0], owner, false); secondary('trapdis', owner);
          emit('stl', encode(13, 7), owner);
        }
        stack([creg, breg, areg], owner);
        secondary('tret', owner);
        resume = origin + bytes.length;
        instructions.push(instructions.splice(resumeIndex, 1)[0]);
        stack([creg, breg, areg], owner);
        if (options.trapmasks) {
          stack([0], owner, false); secondary('gintdis', owner);
          emit('stl', encode(13, 6), owner);
          stack([options.priority, 0], owner, false); secondary('trapdis', owner);
          emit('stl', encode(13, 7), owner);
        }
      } else emit(name, code, owner);
      if (name === 'j' || name === 'gcall' || name === 'cj' && areg === 0) bytes.push(0x40);
      continuation = origin + bytes.length;
      if (name === 'postnormsn') secondary('roundsn', owner);
      if (name === 'move2dinit') {
        for (const value of [0x80001000, 0x80001200, sample % 17]) emit('ldc', encode(4, value), owner);
        secondary('move2dall', owner);
      }
      if (options.crossshadow && name === 'ldshadow') {
        for (let index = 4; index < 7; ++index) {
          emit('stl', encode(13, index), owner);
          memoryChecks.push(lowWorkspace + index * 4);
        }
      }
      if (name === 'ldshadow') { stack([0x80006200, 1, 15], owner, false); secondary('stshadow', owner); }
      if (channels.has(name)) { stack([0x80006000], owner, false); secondary('saveh', owner); }
      if (queues.has(name) && !['saveh', 'savel'].includes(name)) {
        stack([0x80006000], owner, false);
        const priority = ['stlb', 'stlf'].includes(name) || ['swapqueue', 'insertqueue'].includes(name) && areg === 1 || name === 'runp' && (areg & 1);
        secondary(priority ? 'savel' : 'saveh', owner);
      }
      for (const address of memoryChecks) {
        emit('ldc', encode(4, address), owner);
        emit('ldnl', encode(3, 0), owner);
      }
      for (let index = 0; index < 3; ++index) emit('stl', encode(13, index), owner);
      for (let index = 2; index >= 0; --index) emit('ldl', encode(7, index), owner);
      for (let index = 0; index < 2; ++index)
        emit('rev', catalog.get('rev').encoding.bytes_hex.map(value => parseInt(value, 16)), owner);
      if (['gintenb', 'gintdis'].includes(name)) { stack([0], owner, false); secondary('gintdis', owner); }
      if (['trapenb', 'trapdis'].includes(name)) { stack([1, 0], owner, false); secondary('trapdis', owner); }
      if (clocks.has(name)) {
        if (['clockenb', 'clockdis', 'sttimer'].includes(name)) { stack([3], owner, false); secondary('clockdis', owner); }
        if (['stclock', 'sttimer'].includes(name)) {
          for (const priority of name === 'sttimer' ? [0, 1] : [areg]) { stack([priority], owner, false); secondary('ldclock', owner); }
        }
      }
      if (name === 'settimeslice') { stack([1], owner, false); secondary('settimeslice', owner); }
      if (name === 'intdis') secondary('intenb', owner);
      if (name === 'swaptimer') { stack([0x80000000, areg], owner, false); secondary('swaptimer', owner); }
      emit('testerr', catalog.get('testerr').encoding.bytes_hex.map(value => parseInt(value, 16)), owner);
      emit('stl', encode(13, 3), owner, false, true);
      if (contextChanges.has(name)) { stack([0x80000100], owner); secondary('gajw', owner, true); }
    }
  }
  for (const fixup of fixups) {
    const code = encode(fixup.functionNibble, fixup.resolve());
    if (code.length > 8) throw new Error('Address fixup exceeds its reserved instruction');
    bytes.splice(fixup.offset, 8, ...Array(8 - code.length).fill(0x20), ...code);
    const instruction = instructions.find(entry => entry.address === origin + fixup.offset);
    if (instruction) instruction.operand = fixup.resolve() >>> 0;
    else if (!unexecutedAddresses.has(origin + fixup.offset)) throw new Error('Missing executed fixup instruction');
  }
  const stopAddress = origin + bytes.length;
  bytes.push(...encode(15, 5014));
  if (bytes.length > 0x100000) throw new Error('Generated ROM exceeds its memory region');
  const lines = [];
  for (let offset = 0; offset < bytes.length; offset += 32)
    lines.push(`${hex(origin + offset)} ${bytes.slice(offset, offset + 32).map(value => value.toString(16).padStart(2, '0')).join(' ')}`);
  return { rom: `${lines.join('\n')}\n`, instructions, cases, stopAddress };
}

function parseSimulator(text) {
  const states = [];
  for (const line of text.split(/\r?\n/)) {
    const state = /^inst I ([\da-f]{8}) W ([\da-f-]{8}) A ([\da-f-]{8}) B ([\da-f-]{8}) C ([\da-f-]{8}) /i.exec(line);
    if (state) states.push(state.slice(1).map(value => value.includes('-') ? null : value.toLowerCase()));
  }
  return states;
}

function writeReport(directory, report) {
  const stderr = path.join(directory, 'simulator.stderr');
  const banner = fs.existsSync(stderr)
    ? /st20sim: ST20 CPU simulator, version ([^\r\n]+)/.exec(fs.readFileSync(stderr, 'utf8'))
    : null;
  const oracle = {
    executable: `${process.env.ST20SIM_ROOT}\\bin\\st20sim.exe`,
    version: banner?.[1] ?? null,
  };
  fs.writeFileSync(path.join(directory, 'report.json'), JSON.stringify({ ...report, oracle }, null, 2));
}

function executeSimulator(directory, timeout = 30000) {
  const host = process.env.ST20SIM_HOST;
  if (!host) throw new Error('Set ST20SIM_HOST to your Windows SSH destination');
  if (!/^[\w.@:-]+$/.test(host) || host.startsWith('-')) throw new Error('Invalid ST20SIM_HOST');
  const sharedHome = process.env.ST20SIM_SHARED_HOME || '\\\\Mac\\Home';
  const relative = path.relative(os.homedir(), directory);
  if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Artifacts must be below the shared home directory');
  const guestDirectory = `${sharedHome}\\${relative.split(path.sep).join('\\')}`;
  const toolRoot = process.env.ST20SIM_ROOT;
  if (!toolRoot) throw new Error('Set ST20SIM_ROOT to your Windows ST20 toolchain directory');
  const quote = value => `'${value.replaceAll("'", "''")}'`;
  const script = `
$ProgressPreference = 'SilentlyContinue'
$ErrorActionPreference = 'Stop'
$env:ST20ROOT = ${quote(toolRoot)}
$directory = ${quote(guestDirectory)}
$child = Start-Process -FilePath ($env:ST20ROOT + '\\bin\\st20sim.exe') -WorkingDirectory $directory -ArgumentList @('-cfg', ('"' + $directory + '\\probe.cfg"'), '-event', ('"' + $directory + '\\clocks.events"'), '-wptr', '0x80000100', '-trace', '${options.interrupted ? 7 : 4}', '-tracelog', ('"' + $directory + '\\simulator.trace"'), ('"' + $directory + '\\probe.hex"')) -NoNewWindow -PassThru -RedirectStandardOutput ($directory + '\\simulator.stdout') -RedirectStandardError ($directory + '\\simulator.stderr')
$handle = $child.Handle
if (-not $child.WaitForExit(${timeout})) { $child.Kill(); $child.WaitForExit(); throw 'Simulator timed out' }
$child.Refresh()
Write-Output ('SIM_EXIT=' + $child.ExitCode)
`;
  const args = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5'];
  if (process.env.ST20SIM_CONTROL_PATH !== 'none')
    args.push('-S', process.env.ST20SIM_CONTROL_PATH || `/tmp/st20-windows-ssh-${host}-22`);
  const output = run('ssh', [...args, host, 'powershell.exe', '-NoProfile', '-EncodedCommand',
    Buffer.from(script, 'utf16le').toString('base64')]);
  const diagnostics = fs.readFileSync(path.join(directory, 'simulator.stdout'), 'utf8') +
    fs.readFileSync(path.join(directory, 'simulator.stderr'), 'utf8');
  if (!/SIM_EXIT=1\b/.test(output) || !/halted by RSIM_STOP psuedo-op/.test(diagnostics))
    throw new Error(`Simulator did not reach the deliberate STOP\n${output}\n${diagnostics}`);
  const trace = fs.readFileSync(path.join(directory, 'simulator.trace'), 'utf8');
  const unknown = /^inst I ([\da-f]{8}).* O \?\?\?/m.exec(trace);
  if (unknown) throw new Error(`Oracle does not implement the decoded instruction at ${unknown[1]}`);
  return parseSimulator(trace);
}

function compare(program, simulator, local) {
  if (simulator.length !== program.instructions.length + 1 || local.length !== program.instructions.length)
    throw new Error(`Trace length mismatch: expected ${program.instructions.length}, simulator ${simulator.length}, emulator ${local.length}`);
  if (simulator.at(-1)[0] !== hex(program.stopAddress)) throw new Error('Wrong simulator termination address');
  const mismatches = new Map();
  let comparisons = 0;
  const fields = ['Iptr', 'Wdesc', 'A', 'B', 'C'];
  let defined = [false, false, false];
  const memoryDefined = new Map();
  const shadowDefined = [[false, false, false], [false, false, false]];
  let capturedStackLoaded = false;
  const readDefined = (address, width) => Array.from({ length: width }, (_, offset) =>
    memoryDefined.get((address + offset) >>> 0) === true).every(Boolean);
  function writeDefined(address, width, value) {
    for (let offset = 0; offset < width; ++offset) memoryDefined.set((address + offset) >>> 0, value);
  }
  for (let index = 0; index < local.length; ++index) {
    const instruction = program.instructions[index];
    for (let field = 0; field < fields.length; ++field) {
      if (instruction.setup || field >= 2 && !defined[field - 2]) continue;
      if (program.instructions[index].errorProbe && field > 2) continue;
      const expected = simulator[index][field];
      if (expected === null) continue;
      ++comparisons;
      if (expected !== local[index][field]) {
        const { owner, address, name } = program.instructions[index];
        if (!mismatches.has(owner)) mismatches.set(owner, { ...program.cases[owner],
          address: hex(address), checkpoint: name,
          field: program.instructions[index].errorProbe && field === 2 ? 'ErrorResult' : fields[field],
          simulator: expected, emulator: local[index][field] });
      }
    }
    const before = [...defined];
    const registers = local[index].slice(2, 5).map(value => parseInt(value, 16));
    const workspace = (parseInt(local[index][1], 16) & 0xfffffffc) >>> 0;
    const name = instruction.name;
    const definition = catalog.get(name)?.definition || '';
    for (const assignment of definition.matchAll(/([ABC])reg′[^←\n]*←\s*([^\n]+)/g)) {
      const target = 'ABC'.indexOf(assignment[1]);
      const expression = assignment[2];
      defined[target] = !/undefined/i.test(expression) &&
        [...expression.matchAll(/([ABC])reg(?!′)/g)].every(match => before['ABC'.indexOf(match[1])]);
    }
    if (name === 'xword') defined[0] = before[0] && before[1];
    if (name === 'unpacksn') defined[0] = before[0] && (registers[0] & 0x7f800000) !== 0x7f800000;
    if (name === 'csngl') defined = [before[0], before[2], false];
    if (options.crossshadow && name === 'runp' && (parseInt(local[index][1], 16) & 1)) {
      shadowDefined[1] = [...before];
      capturedStackLoaded = false;
    }
    if (options.crossshadow && name === 'tret') defined = capturedStackLoaded && !(options.crossshadow >= 4 && options.crossshadow % 2)
      ? [...shadowDefined[1]] : [false, false, false];
    if (name === 'testerr') defined = [true, before[0], before[1]];
    if (['sthf', 'sthb', 'stlf', 'stlb'].includes(name)) defined = [before[1], before[2], false];
    if (name === 'ldl') defined[0] = readDefined((workspace + instruction.operand * 4) >>> 0, 4);
    if (loads.has(name)) {
      const width = ['lb', 'lbx', 'devlb'].includes(name) ? 1 : ['ls', 'lsx', 'devls'].includes(name) ? 2 : 4;
      const address = (registers[0] + (name === 'ldnl' ? instruction.operand * 4 : 0)) >>> 0;
      defined[0] = before[0] && readDefined(address, width);
    }
    if (name === 'stl') writeDefined((workspace + instruction.operand * 4) >>> 0, 4, before[0]);
    if (name === 'stnl') writeDefined((registers[0] + instruction.operand * 4) >>> 0, 4, before[1]);
    if (['sb', 'ss', 'devsb', 'devss', 'devsw'].includes(name))
      writeDefined(registers[0], ['sb', 'devsb'].includes(name) ? 1 : ['ss', 'devss'].includes(name) ? 2 : 4, before[1]);
    if (records.has(name)) {
      const record = 0x80000040 + registers[2] * 0x80 + registers[0] * 0x20 + (name.includes('trapped') ? 16 : 0);
      const source = name.startsWith('ld') ? registers[1] : record;
      const destination = name.startsWith('ld') ? record : registers[1];
      const values = Array.from({ length: 16 }, (_, offset) => readDefined(source + offset, 1));
      values.forEach((value, offset) => writeDefined(destination + offset, 1, value));
    }
    if (name === 'call') {
      writeDefined(workspace - 16, 4, true);
      for (let field = 0; field < 3; ++field) writeDefined(workspace - 12 + field * 4, 4, before[field]);
    }
    if (name === 'restart' || name === 'iret') {
      const base = name === 'restart' ? registers[0] + 16 : workspace + 12;
      defined = [0, 1, 2].map(field => readDefined(base + field * 4, 4));
    }
    if (name === 'ldshadow' && (registers[0] & 4))
      shadowDefined[registers[1]] = [0, 1, 2].map(field => readDefined(registers[2] + 16 + field * 4, 4));
    if (options.crossshadow && name === 'ldshadow' && (registers[0] & 4)) capturedStackLoaded = true;
    if (name === 'stshadow') {
      if (options.crossshadow && (registers[0] & 1)) writeDefined(registers[2], 4, true);
      if (options.crossshadow && (registers[0] & 2)) writeDefined(registers[2] + 4, 12, true);
      const source = registers[1] === (parseInt(local[index][1], 16) & 1) ? before : shadowDefined[registers[1]];
      if (registers[0] & 4) source.forEach((value, field) => writeDefined(registers[2] + 16 + field * 4, 4, value));
      if (registers[0] & 8) writeDefined(registers[2] + 28, 20, false);
    }
    if (['saveh', 'savel'].includes(name)) writeDefined(registers[0], 8, true);
    if (name === 'stopp') defined = [false, false, false];
  }
  return { comparisons, mismatches: [...mismatches.values()] };
}

function main() {
  for (let index = 2; index < process.argv.length; index += 2) {
    const key = process.argv[index].replace(/^--/, '');
    if (!Object.hasOwn(options, key) || process.argv[index + 1] === undefined)
      throw new Error('Usage: node tests/st20sim_differential.js [--rounds N] [--samples N] [--seed N] [--operations add,sub,...] [--isolate 0|1] [--priority 0|1] [--interrupted 0|1|2|3|4|5] [--trapshadow 0|1] [--trapmasks 0|1] [--crossshadow 0|1|2|3|4|5|6|7]');
    options[key] = key === 'operations' ? process.argv[index + 1].split(',') : Number(process.argv[index + 1]);
  }
  for (const key of ['rounds', 'samples', 'seed'])
    if (!Number.isInteger(options[key]) || options[key] < (key === 'samples' ? 0 : 1) ||
        options[key] > (key === 'seed' ? 0xffffffff : 1000)) throw new Error(`Invalid ${key}`);
  const lowPriorityControls = new Set(['intdis', 'intenb', 'settimeslice', 'stlf']);
  const operations = options.operations || selected.filter(name => lowPriorityControls.has(name) === Boolean(options.priority));
  if (!process.env.ST20SIM_HOST || !process.env.ST20SIM_ROOT)
    throw new Error('Set ST20SIM_HOST and ST20SIM_ROOT before running the differential tests');
  if (![0, 1].includes(options.isolate)) throw new Error('Invalid isolate');
  if (![0, 1].includes(options.priority)) throw new Error('Invalid priority');
  if (![0, 1, 2, 3, 4, 5].includes(options.interrupted)) throw new Error('Invalid interrupted');
  if (![0, 1].includes(options.trapshadow)) throw new Error('Invalid trapshadow');
  if (![0, 1].includes(options.trapmasks)) throw new Error('Invalid trapmasks');
  if (![0, 1, 2, 3, 4, 5, 6, 7].includes(options.crossshadow)) throw new Error('Invalid crossshadow');
  if (options.crossshadow >= 4 && operations.some(name => name !== 'ldshadow'))
    throw new Error('Captured validity fixtures require ldshadow');
  if (options.crossshadow && (options.priority !== 1 || options.interrupted || options.trapshadow || options.trapmasks || !options.isolate ||
      operations.some(name => !['ldshadow', 'stshadow'].includes(name))))
    throw new Error('Captured shadow fixtures require isolated LP ldshadow/stshadow operations');
  if (options.trapmasks && (options.interrupted || options.trapshadow || !options.isolate || operations.some(name => name !== 'tret')))
    throw new Error('Trap mask fixtures require isolated tret operations');
  if (options.trapshadow && (options.priority !== 0 || options.interrupted || !options.isolate ||
      operations.some(name => !['ldshadow', 'stshadow'].includes(name))))
    throw new Error('Trap shadow fixtures require isolated HP ldshadow/stshadow operations');
  if (options.interrupted >= 2 && options.priority !== 1)
    throw new Error('Timer-driven interrupted probes require --priority 1');
  if (options.interrupted && (!options.isolate || operations.some(name =>
      !['move', 'devmove', 'move2dall', 'move2dzero', 'move2dnonzero'].includes(name))))
    throw new Error('Interrupted oracle probes require isolated move operations');
  if (operations.some(name => lowPriorityControls.has(name)) && options.priority !== 1)
    throw new Error('intdis, intenb, settimeslice and stlf require --priority 1 and a separate LP batch');
  if (operations.length === 0 || operations.some(name => !selected.includes(name)))
    throw new Error('Operation not in the supported stateless test set');
  fs.mkdirSync(path.join(root, 'builddir', 'tests'), { recursive: true });
  const reports = [];
  let failed = false;
  let blocked = false;
  const batches = options.isolate ? operations.map(name => [name]) : [operations];
  for (let round = 0; round < options.rounds; ++round) {
    for (const batch of batches) {
    const seed = (options.seed + round) >>> 0 || 1;
    const directory = fs.mkdtempSync(path.join(root, 'builddir', 'tests', 'st20sim-'));
    console.log(`Round ${round + 1}/${options.rounds}${options.isolate ? ` [${batch.join(',')}]` : ''}, seed ${seed}, artifacts ${path.relative(root, directory)}`);
    let program;
    try {
    program = generate(seed, batch);
    if (options.interrupted >= 3) {
      const include = process.env.ST20SIM_INCLUDE || path.resolve(root, '..', 'ST20R1.8.1', 'include');
      run(process.env.ST20SIM_CLANG || 'clang', ['--target=i686-pc-windows-msvc', '-std=c17', '-O2', '-Wall', '-Wextra', '-Werror',
        '-fno-stack-protector', '-DWIN32', '-I', include, '-c', path.join(root, 'tests', 'st20sim_continuation_device.c'),
        '-o', path.join(directory, 'continuation-device.obj')]);
      run(process.env.ST20SIM_LINKER || 'lld-link', ['/dll', '/noentry', '/machine:x86',
        `/out:${path.join(directory, 'continuation-device.dll')}`, path.join(directory, 'continuation-device.obj')]);
    }
    fs.writeFileSync(path.join(directory, 'probe.hex'), program.rom);
    fs.writeFileSync(path.join(directory, 'probe.cfg'),
      'processor c2\nmemory RAM 0x80000000 0x200000 RAM\nmemory ROM1 0x7ff00000 0x100000 ROM\n' +
      (options.interrupted >= 3 ? 'memory PROBE 0x40010000 0x1000 DEVICE\n' +
        'simdll -file continuation-device.dll -init continuation_init ' +
        (options.interrupted === 4 ? '-parm signal -sig continuation_signal -siglevel 4 -poll_all -alu\n' :
          options.interrupted === 5 ? '-parm tick\n' : '-parm queue\n') +
        'simmem PROBE -mem continuation_memory\n' : '') +
      (options.interrupted === 1 || batch.includes('iret') ? 'memory CONTROL 0x1ffffff0 0x10 RAM\n' : '') + 'bootiptr 0x7ff00000\n');
    fs.writeFileSync(path.join(directory, 'clocks.events'),
      (options.interrupted === 1 ? `interrupt ${program.cases[0].interruptCount} -1 RPC 7\n` :
        options.interrupted === 2 ? `tick ${program.cases[0].interruptCount}\n` : '') +
      'tick 1000000000\nlotick 1000000000\n');
    fs.writeFileSync(path.join(directory, 'cases.json'), JSON.stringify({ seed, cases: program.cases, instructions: program.instructions }, null, 2));
    if (options.interrupted) {
      const simulator = executeSimulator(directory);
      if (options.interrupted >= 3) {
        const diagnostics = Array(10).fill(null);
        const byAddress = new Map(program.instructions.map(instruction => [hex(instruction.address), instruction]));
        for (let index = 0; index + 1 < simulator.length; ++index) {
          const state = simulator[index];
          if (byAddress.get(state[0])?.name !== 'ldnl' || state[2] === null) continue;
          const address = parseInt(state[2], 16);
          if (address >= 0x80006500 && address < 0x80006528 && (address & 3) === 0)
            diagnostics[(address - 0x80006500) / 4] = simulator[index + 1][2];
        }
        program.cases[0].deviceDiagnostics = diagnostics;
      }
      if (!simulator.some(state => state[0] === program.cases[0].handler))
        throw new Error('Preemption request did not enter the capture handler; no active native state captured');
      const shadow = Array(12);
      const instructionByAddress = new Map(program.instructions.map(instruction => [hex(instruction.address), instruction]));
      for (let index = 0; index + 1 < simulator.length; ++index) {
        const state = simulator[index];
        if (instructionByAddress.get(state[0])?.name !== 'ldnl' || state[2] === null) continue;
        const address = parseInt(state[2], 16);
        if (address >= 0x80006100 && address < 0x80006130 && (address & 3) === 0)
          shadow[(address - 0x80006100) / 4] = simulator[index + 1][2];
      }
      if (Array.from(shadow).some(word => word === undefined) || shadow[1] === null)
        throw new Error('Capture handler did not export a complete, valid LP shadow record');
      const pendingOperation = (parseInt(shadow[1], 16) & 0x7c000000) >>> 26;
      const copyAddress = hex(program.instructions.find(instruction => instruction.name === batch[0]).address);
      const capturePoint = pendingOperation ? 'active operation' : shadow[3] === copyAddress ? 'before copy' : 'instruction boundary';
      const report = { seed, priority: options.priority, status: pendingOperation ? 'Active oracle capture' : 'Boundary oracle capture', operations: batch,
        cases: 0, plannedCases: program.cases.length, states: simulator.length,
        shadow, pendingOperation, capturePoint,
        ...(program.cases[0].deviceDiagnostics ? { deviceDiagnostics: program.cases[0].deviceDiagnostics } : {}),
        scope: `${options.interrupted === 2 ? 'Timer-preempted' : options.interrupted === 3 ? 'Device-queued' : options.interrupted === 4 ? 'Device-signalled' : options.interrupted === 5 ? 'Memory-callback timer-event' : 'RPC-aborted'} copy and raw native shadow records; no emulator equivalence claim` };
      writeReport(directory, report);
      console.log(`Captured ${simulator.length} oracle states, ${capturePoint}, operation code ${pendingOperation}; no equivalence comparison`);
      reports.push({ directory: path.relative(root, directory), ...report });
      continue;
    }
    let simulator;
    let identityConfiguration;
    if (options.trapshadow || options.trapmasks || options.crossshadow) simulator = executeSimulator(directory);
    if ([2, 4, 5].includes(options.crossshadow)) {
      const byAddress = new Map(program.instructions.map(instruction => [hex(instruction.address), instruction]));
      const captures = program.cases.map(entry => ({ ...entry, shadowBeforeReturn: Array(7).fill(null),
        reactivatedShadow: Array(7).fill(null), trappedControl: Array(4).fill(null) }));
      for (let index = 0; index + 1 < simulator.length; ++index) {
        const state = simulator[index];
        const instruction = byAddress.get(state[0]);
        if (!instruction) continue;
        if (instruction.name === 'tret') captures[instruction.owner].afterReturn = simulator[index + 1];
        if (instruction.name === 'ldnl' && state[2] !== null) {
          const address = parseInt(state[2], 16);
          if (address >= 0x80006400 && address < 0x8000641c && (address & 3) === 0)
            captures[instruction.owner].shadowBeforeReturn[(address - 0x80006400) / 4] = simulator[index + 1][2];
          if (address >= 0x800000b0 && address < 0x800000c0 && (address & 3) === 0)
            captures[instruction.owner].trappedControl[(address - 0x800000b0) / 4] = simulator[index + 1][2];
          if (address >= 0x80006600 && address < 0x8000661c && (address & 3) === 0)
            captures[instruction.owner].reactivatedShadow[(address - 0x80006600) / 4] = simulator[index + 1][2];
        }
      }
      if (captures.some(entry => !entry.afterReturn)) throw new Error('Cross-priority control probe did not return for every case');
      const report = { seed, priority: options.priority, status: 'Oracle control probe', cases: 0,
        plannedCases: program.cases.length, captures, scope: 'edited captured LP control before/after tret; no emulator equivalence claim' };
      writeReport(directory, report);
      reports.push({ directory: path.relative(root, directory), ...report });
      console.log(`Captured ${captures.length} cross-priority control returns; oracle only`);
      continue;
    }
    if (batch.some(name => ['ldprodid', 'lddevid'].includes(name))) {
      if (batch.length !== 1) throw new Error('ID getters require isolated model-identity configuration');
      simulator = executeSimulator(directory);
      const outputs = program.instructions.map((instruction, index) => instruction.name === batch[0] ? simulator[index + 1]?.[2] : null).filter(value => value !== null);
      if (outputs.length !== program.cases.length || !/^[\da-f]{8}$/.test(outputs[0]) || outputs.some(value => value !== outputs[0]))
        throw new Error('Oracle identity is undefined or not constant');
      identityConfiguration = { operation: batch[0], value: outputs[0] };
    }
    if (batch.some(name => nativeUnavailable.has(name)))
      simulator = executeSimulator(directory, batch.includes('reboot') ? 1000 : 30000);
    const localText = run(path.join(root, 'builddir', 'cpu-instructions'),
      ['--sim-trace', path.join(directory, 'probe.hex'), String(program.instructions.length), '--frozen-clocks',
        ...(identityConfiguration ? ['--product-id', identityConfiguration.value] : [])]);
    fs.writeFileSync(path.join(directory, 'emulator.trace'), localText);
    const local = localText.trim().split('\n').map(line => {
      if (!/^state(?: [\da-f]{8}){5} [01]$/.test(line)) throw new Error(`Unexpected emulator trace line: ${line}`);
      const values = line.split(' ').slice(1);
      return [...values.slice(0, 5), Number(values[5])];
    });
    simulator ||= executeSimulator(directory);
    const result = compare(program, simulator, local);
    const byOperation = Object.fromEntries(batch.map(name => [name, {
      cases: program.cases.filter(entry => entry.name === name).length,
      mismatches: result.mismatches.filter(entry => entry.name === name).length,
      scope: fixtureScope(name),
    }]));
    const report = { seed, priority: options.priority, policy: 'reference-defined', frozenClocks: true, identityConfiguration,
      cases: program.cases.length, instructions: program.instructions.length, byOperation, ...result };
    writeReport(directory, report);
    console.log(`${report.cases} cases, ${report.comparisons} defined-value comparisons, ${report.mismatches.length} mismatching cases`);
    console.log('Divergences: ' + Object.entries(byOperation).filter(([, counts]) => counts.mismatches)
      .map(([name, counts]) => `${name} ${counts.mismatches}/${counts.cases}`).join(', '));
    const representatives = batch.map(name => report.mismatches.find(entry => entry.name === name)).filter(Boolean);
    for (const mismatch of representatives.slice(0, 8)) console.error(JSON.stringify(mismatch));
    reports.push({ directory: path.relative(root, directory), ...report });
    failed ||= report.mismatches.length !== 0;
    } catch (error) {
      const report = { seed, priority: options.priority, policy: options.interrupted ? 'oracle-only' : 'reference-defined', frozenClocks: options.interrupted !== 2,
        cases: 0, plannedCases: program ? program.cases.length : 0, instructions: program ? program.instructions.length : 0,
        status: 'Blocked', operations: batch, scopes: Object.fromEntries(batch.map(name => [name, fixtureScope(name)])), reason: error.message,
        ...(program?.cases[0]?.deviceDiagnostics ? { deviceDiagnostics: program.cases[0].deviceDiagnostics } : {}) };
      writeReport(directory, report);
      console.error(`Blocked ${batch.join(',')}: ${error.message.split('\n')[0]}`);
      reports.push({ directory: path.relative(root, directory), ...report });
      blocked = true;
      if (!options.isolate) throw error;
    }
    }
  }
  const oracleOnly = options.interrupted || [2, 4, 5].includes(options.crossshadow);
  console.log(`${oracleOnly ? 'Oracle probe' : 'Differential loop'}: ${options.rounds} rounds, ${reports.length} batches, ${reports.reduce((total, report) => total + (report.plannedCases ?? report.cases), 0)} planned cases, ${blocked ? 'INCOMPLETE' : failed ? 'FAIL' : oracleOnly ? 'CAPTURED' : 'PASS'}`);
  process.exitCode = blocked ? 2 : failed ? 1 : 0;
}

try { main(); } catch (error) { console.error(error.message); process.exitCode = 2; }