const textEncoder = new TextEncoder();

function toAsciiBytes(text) {
  return Array.from(textEncoder.encode(text));
}

function writeWord(arr, value) {
  arr.push(value & 0xff, (value >> 8) & 0xff);
}

function nextPowerOfTwo(value) {
  let pow = 1;
  while (pow < value) pow <<= 1;
  return pow;
}

function chunkData(data) {
  const chunks = [];
  let i = 0;
  while (i < data.length) {
    const size = Math.min(255, data.length - i);
    const block = new Uint8Array(size + 1);
    block[0] = size;
    for (let j = 0; j < size; j++) {
      block[j + 1] = data[i + j];
    }
    chunks.push(block);
    i += size;
  }
  chunks.push(Uint8Array.of(0)); // block terminator
  return chunks;
}

function lzwEncode(pixels, minCodeSize) {
  const clearCode = 1 << minCodeSize;
  const endCode = clearCode + 1;
  let codeSize = minCodeSize + 1;
  const output = [];
  let bitBuffer = 0;
  let bitCount = 0;

  const writeCode = (code) => {
    bitBuffer |= code << bitCount;
    bitCount += codeSize;
    while (bitCount >= 8) {
      output.push(bitBuffer & 0xff);
      bitBuffer >>= 8;
      bitCount -= 8;
    }
  };

  const dictionary = new Map();
  let dictSize;
  const resetDictionary = () => {
    dictionary.clear();
    for (let i = 0; i < clearCode; i++) {
      dictionary.set(String.fromCharCode(i), i);
    }
    dictSize = endCode + 1;
    codeSize = minCodeSize + 1;
  };

  resetDictionary();
  writeCode(clearCode);

  if (!pixels.length) {
    writeCode(endCode);
    if (bitCount > 0) {
      output.push(bitBuffer & 0xff);
    }
    return output;
  }

  let w = String.fromCharCode(pixels[0]);

  for (let i = 1; i < pixels.length; i++) {
    const c = String.fromCharCode(pixels[i]);
    const wc = w + c;
    if (dictionary.has(wc)) {
      w = wc;
    } else {
      writeCode(dictionary.get(w));
      if (dictSize < 4096) {
        dictionary.set(wc, dictSize++);
        if (dictSize === (1 << codeSize) && codeSize < 12) {
          codeSize += 1;
        }
      } else {
        writeCode(clearCode);
        resetDictionary();
      }
      w = c;
    }
  }

  if (w !== '') {
    writeCode(dictionary.get(w));
  }

  writeCode(endCode);

  if (bitCount > 0) {
    output.push(bitBuffer & 0xff);
  }

  return output;
}

export class GifEncoder {
  constructor(width, height, options = {}) {
    this.width = width;
    this.height = height;
    this.loop = options.loop ?? 0;
    this.binary = [];
    this._writeHeader();
  }

  _writeHeader() {
    this._writeBytes([0x47, 0x49, 0x46, 0x38, 0x39, 0x61]); // GIF89a
    writeWord(this.binary, this.width);
    writeWord(this.binary, this.height);
    const gctFlag = 0;
    const colorResolution = 7;
    const sortFlag = 0;
    const gctSize = 0;
    const packed = (gctFlag << 7) | (colorResolution << 4) | (sortFlag << 3) | gctSize;
    this.binary.push(packed, 0x00, 0x00);

    if (this.loop !== null && this.loop !== undefined) {
      this.binary.push(0x21, 0xFF, 0x0B);
      this._writeBytes(toAsciiBytes('NETSCAPE2.0'));
      this._writeBytes([0x03, 0x01]);
      writeWord(this.binary, this.loop);
      this.binary.push(0x00);
    }
  }

  _writeBytes(arr) {
    this.binary.push(...arr);
  }

  addFrame({ indexedPixels, palette, delay = 100, transparentIndex = -1 }) {
    if (!indexedPixels || !palette) {
      throw new Error('Frame data ist unvollständig.');
    }

    const colorCount = Math.max(1, palette.length / 3);
    const tableSize = Math.max(2, Math.min(256, nextPowerOfTwo(colorCount)));
    const paddedPalette = new Uint8Array(tableSize * 3);
    paddedPalette.set(palette);

    const colorTableField = Math.log2(tableSize) - 1;
    const minCodeSize = Math.max(2, Math.ceil(Math.log2(tableSize)));

    // Graphics Control Extension
    this.binary.push(0x21, 0xF9, 0x04);
    const hasTransparency = transparentIndex >= 0;
    const disposal = hasTransparency ? 2 : 0;
    const gcePacked = (disposal << 2) | (hasTransparency ? 0x01 : 0x00);
    this.binary.push(gcePacked);
    const delayCs = Math.max(1, Math.min(65535, Math.round(delay / 10)));
    writeWord(this.binary, delayCs);
    this.binary.push(hasTransparency ? transparentIndex : 0x00, 0x00);

    // Image Descriptor
    this.binary.push(0x2C);
    writeWord(this.binary, 0);
    writeWord(this.binary, 0);
    writeWord(this.binary, this.width);
    writeWord(this.binary, this.height);
    const localColorTableFlag = 0x80;
    const interlace = 0x00;
    const sortFlag = 0x00;
    const packedField = localColorTableFlag | interlace | sortFlag | colorTableField;
    this.binary.push(packedField);

    this._writeBytes(paddedPalette);

    this.binary.push(minCodeSize);
    const lzwData = lzwEncode(indexedPixels, minCodeSize);
    const subBlocks = chunkData(lzwData);
    for (const block of subBlocks) {
      this._writeBytes(block);
    }
  }

  finish() {
    this.binary.push(0x3B);
    return new Uint8Array(this.binary);
  }
}
