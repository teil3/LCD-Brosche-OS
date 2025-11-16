const SIGBITS = 5;
const RSHIFT = 8 - SIGBITS;
const HISTO_SIZE = 1 << (3 * SIGBITS);
const MAX_COLOR = 1 << SIGBITS;

function getColorIndex(r, g, b) {
  return (r << (2 * SIGBITS)) + (g << SIGBITS) + b;
}

class VBox {
  constructor(r1, r2, g1, g2, b1, b2, histogram) {
    this.r1 = r1; this.r2 = r2;
    this.g1 = g1; this.g2 = g2;
    this.b1 = b1; this.b2 = b2;
    this.histo = histogram;
  }

  volume() {
    return ((this.r2 - this.r1 + 1) * (this.g2 - this.g1 + 1) * (this.b2 - this.b1 + 1));
  }

  count() {
    let npix = 0;
    for (let r = this.r1; r <= this.r2; r++) {
      for (let g = this.g1; g <= this.g2; g++) {
        for (let b = this.b1; b <= this.b2; b++) {
          npix += this.histo[getColorIndex(r, g, b)] || 0;
        }
      }
    }
    return npix;
  }

  avg() {
    let ntot = 0;
    let rsum = 0;
    let gsum = 0;
    let bsum = 0;
    const mult = 1 << RSHIFT;

    for (let r = this.r1; r <= this.r2; r++) {
      for (let g = this.g1; g <= this.g2; g++) {
        for (let b = this.b1; b <= this.b2; b++) {
          const histIdx = getColorIndex(r, g, b);
          const hval = this.histo[histIdx] || 0;
          ntot += hval;
          rsum += hval * ((r << RSHIFT) + mult / 2);
          gsum += hval * ((g << RSHIFT) + mult / 2);
          bsum += hval * ((b << RSHIFT) + mult / 2);
        }
      }
    }

    if (!ntot) {
      return [0, 0, 0];
    }

    return [
      Math.round(rsum / ntot),
      Math.round(gsum / ntot),
      Math.round(bsum / ntot)
    ];
  }

  longestDimension() {
    const rLen = this.r2 - this.r1;
    const gLen = this.g2 - this.g1;
    const bLen = this.b2 - this.b1;
    if (rLen >= gLen && rLen >= bLen) return 'r';
    if (gLen >= rLen && gLen >= bLen) return 'g';
    return 'b';
  }

  split() {
    if (this.count() === 0) return [this];
    const dim = this.longestDimension();
    const colors = [];

    for (let r = this.r1; r <= this.r2; r++) {
      for (let g = this.g1; g <= this.g2; g++) {
        for (let b = this.b1; b <= this.b2; b++) {
          const idx = getColorIndex(r, g, b);
          const count = this.histo[idx] || 0;
          if (count) {
            colors.push({ r, g, b, count });
          }
        }
      }
    }

    if (!colors.length) {
      return [this];
    }

    colors.sort((a, b) => a[dim] - b[dim]);
    const total = colors.reduce((sum, c) => sum + c.count, 0);
    const mid = total / 2;
    let acc = 0;
    let splitIndex = 0;
    for (; splitIndex < colors.length; splitIndex++) {
      acc += colors[splitIndex].count;
      if (acc >= mid) break;
    }

    const sliceA = colors.slice(0, splitIndex + 1);
    const sliceB = colors.slice(splitIndex + 1);
    if (!sliceA.length || !sliceB.length) {
      const half = Math.floor(colors.length / 2);
      return [this._makeVBox(colors.slice(0, half)), this._makeVBox(colors.slice(half))].filter(Boolean);
    }

    return [this._makeVBox(sliceA), this._makeVBox(sliceB)].filter(Boolean);
  }

  _makeVBox(colors) {
    if (!colors.length) return null;
    let r1 = MAX_COLOR, r2 = -1;
    let g1 = MAX_COLOR, g2 = -1;
    let b1 = MAX_COLOR, b2 = -1;
    for (const c of colors) {
      if (c.r < r1) r1 = c.r;
      if (c.r > r2) r2 = c.r;
      if (c.g < g1) g1 = c.g;
      if (c.g > g2) g2 = c.g;
      if (c.b < b1) b1 = c.b;
      if (c.b > b2) b2 = c.b;
    }
    return new VBox(r1, r2, g1, g2, b1, b2, this.histo);
  }
}

class PriorityQueue {
  constructor(compare) {
    this.compare = compare;
    this.items = [];
  }
  push(item) {
    this.items.push(item);
    this.items.sort(this.compare);
  }
  pop() {
    return this.items.shift();
  }
  size() {
    return this.items.length;
  }
  toArray() {
    return [...this.items];
  }
}

function medianCutQuantize(pixels, maxColors) {
  if (!pixels.length) {
    return [];
  }
  const histogram = new Uint32Array(HISTO_SIZE);
  let rmin = MAX_COLOR, rmax = 0,
      gmin = MAX_COLOR, gmax = 0,
      bmin = MAX_COLOR, bmax = 0;

  for (const [r, g, b] of pixels) {
    const ri = r >> RSHIFT;
    const gi = g >> RSHIFT;
    const bi = b >> RSHIFT;
    const idx = getColorIndex(ri, gi, bi);
    histogram[idx] = (histogram[idx] || 0) + 1;
    if (ri < rmin) rmin = ri;
    if (ri > rmax) rmax = ri;
    if (gi < gmin) gmin = gi;
    if (gi > gmax) gmax = gi;
    if (bi < bmin) bmin = bi;
    if (bi > bmax) bmax = bi;
  }

  const pq = new PriorityQueue((a, b) => b.count() - a.count());
  pq.push(new VBox(rmin, rmax, gmin, gmax, bmin, bmax, histogram));

  while (pq.size() < maxColors) {
    const vbox = pq.pop();
    if (!vbox || vbox.count() === 0) {
      break;
    }
    const [box1, box2] = vbox.split();
    if (box1) pq.push(box1);
    if (box2) pq.push(box2);
    if (!box2) break;
  }

  return pq.toArray().map((box) => box.avg());
}

function nearestColor(r, g, b, palette) {
  let minDist = Infinity;
  let best = 0;
  for (let i = 0; i < palette.length; i++) {
    const color = palette[i];
    const dr = r - color[0];
    const dg = g - color[1];
    const db = b - color[2];
    const dist = dr * dr + dg * dg + db * db;
    if (dist < minDist) {
      minDist = dist;
      best = i;
    }
  }
  return { index: best, color: palette[best] };
}

export function quantizeImage(imageData, options = {}) {
  const {
    maxColors = 64,
    preserveAlpha = true,
    transparentThreshold = 16,
    dither = true,
    backgroundColor = [0, 0, 0]
  } = options;

  const { data, width, height } = imageData;
  const totalPixels = width * height;
  const transparentMask = new Uint8Array(totalPixels);
  const samples = [];
  let hasTransparent = false;

  for (let i = 0; i < totalPixels; i++) {
    const idx = i * 4;
    const a = data[idx + 3];
    if (preserveAlpha && a <= transparentThreshold) {
      transparentMask[i] = 1;
      hasTransparent = true;
      continue;
    }
    samples.push([data[idx], data[idx + 1], data[idx + 2]]);
  }

  const effectiveMax = Math.min(256, Math.max(2, maxColors));
  const paletteColors = medianCutQuantize(samples, Math.max(1, Math.min(effectiveMax - (hasTransparent ? 1 : 0), samples.length)));

  if (!paletteColors.length) {
    paletteColors.push(backgroundColor.slice());
  }

  if (paletteColors.length === 1 && samples.length > 1) {
    paletteColors.push(paletteColors[0].slice());
  }

  let transparentIndex = -1;
  if (hasTransparent) {
    transparentIndex = 0;
    paletteColors.unshift(backgroundColor.slice());
  }

  while (paletteColors.length < 2) {
    paletteColors.push(paletteColors[0].slice());
  }

  const paletteFlat = new Uint8Array(paletteColors.length * 3);
  paletteColors.forEach((color, idx) => {
    const base = idx * 3;
    paletteFlat[base] = color[0];
    paletteFlat[base + 1] = color[1];
    paletteFlat[base + 2] = color[2];
  });

  const indexedPixels = new Uint8Array(totalPixels);

  if (dither) {
    const buffer = new Float32Array(data.length);
    for (let i = 0; i < data.length; i++) buffer[i] = data[i];

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const pxIndex = y * width + x;
        const idx = pxIndex * 4;
        if (transparentMask[pxIndex]) {
          indexedPixels[pxIndex] = transparentIndex >= 0 ? transparentIndex : 0;
          continue;
        }
        const { index, color } = nearestColor(buffer[idx], buffer[idx + 1], buffer[idx + 2], paletteColors);
        indexedPixels[pxIndex] = index;
        const errR = buffer[idx] - color[0];
        const errG = buffer[idx + 1] - color[1];
        const errB = buffer[idx + 2] - color[2];

        distributeError(buffer, width, height, x, y, errR, errG, errB, transparentMask);
      }
    }
  } else {
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const pxIndex = y * width + x;
        if (transparentMask[pxIndex]) {
          indexedPixels[pxIndex] = transparentIndex >= 0 ? transparentIndex : 0;
          continue;
        }
        const idx = pxIndex * 4;
        const { index } = nearestColor(data[idx], data[idx + 1], data[idx + 2], paletteColors);
        indexedPixels[pxIndex] = index;
      }
    }
  }

  return {
    palette: paletteFlat,
    indexedPixels,
    transparentIndex,
    colorCount: paletteColors.length
  };
}

function distributeError(buffer, width, height, x, y, errR, errG, errB, mask) {
  const addError = (nx, ny, factor) => {
    if (nx < 0 || nx >= width || ny < 0 || ny >= height) return;
    const nIndex = ny * width + nx;
    if (mask[nIndex]) return;
    const base = nIndex * 4;
    buffer[base] = clamp(buffer[base] + errR * factor);
    buffer[base + 1] = clamp(buffer[base + 1] + errG * factor);
    buffer[base + 2] = clamp(buffer[base + 2] + errB * factor);
  };

  addError(x + 1, y, 7 / 16);
  addError(x - 1, y + 1, 3 / 16);
  addError(x, y + 1, 5 / 16);
  addError(x + 1, y + 1, 1 / 16);
}

function clamp(val) {
  return Math.max(0, Math.min(255, val));
}
