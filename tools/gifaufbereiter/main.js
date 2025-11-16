import { GifEncoder } from './gif-encoder.js';
import { quantizeImage } from './quantize.js';

const els = {
  file: document.getElementById('file'),
  process: document.getElementById('process'),
  download: document.getElementById('download'),
  reset: document.getElementById('reset'),
  status: document.getElementById('status'),
  preview: document.getElementById('preview'),
  infoSource: document.getElementById('info-source'),
  infoFrames: document.getElementById('info-frames'),
  infoOutput: document.getElementById('info-output'),
  infoLimit: document.getElementById('info-limit'),
  maxSize: document.getElementById('max-size'),
  maxSizeLabel: document.getElementById('max-size-label'),
  colors: document.getElementById('colors'),
  colorsLabel: document.getElementById('colors-label'),
  fps: document.getElementById('fps'),
  fpsLabel: document.getElementById('fps-label'),
  maxFrames: document.getElementById('max-frames'),
  framesLabel: document.getElementById('frames-label'),
  limit: document.getElementById('limit'),
  bgColor: document.getElementById('bg-color'),
  keepAlpha: document.getElementById('keep-alpha'),
  dither: document.getElementById('dither'),
  loop: document.getElementById('loop'),
  baseDelay: document.getElementById('base-delay')
};

const previewCtx = els.preview.getContext('2d');
const workingCanvas = document.createElement('canvas');
const workingCtx = workingCanvas.getContext('2d');

const state = {
  source: null,
  processedBlob: null,
  processedInfo: null,
  downloadUrl: null,
  processing: false
};

let dropDepth = 0;

function setStatus(text, variant = 'info') {
  els.status.textContent = text;
  els.status.classList.remove('error', 'success');
  if (variant === 'error') {
    els.status.classList.add('error');
  } else if (variant === 'success') {
    els.status.classList.add('success');
  }
}

function formatBytes(num) {
  if (!Number.isFinite(num)) return '–';
  if (num < 1024) return `${num.toFixed(0)} B`;
  if (num < 1024 * 1024) return `${(num / 1024).toFixed(1)} kB`;
  return `${(num / (1024 * 1024)).toFixed(1)} MB`;
}

function hexToRgb(hex) {
  const value = hex.replace('#', '');
  const chunk = value.length === 3
    ? value.split('').map((c) => parseInt(c + c, 16))
    : [value.slice(0, 2), value.slice(2, 4), value.slice(4, 6)].map((c) => parseInt(c, 16));
  return chunk;
}

function computeTargetDimensions(srcW, srcH, maxDimension) {
  if (!maxDimension || (srcW <= maxDimension && srcH <= maxDimension)) {
    return { width: Math.max(1, Math.round(srcW)), height: Math.max(1, Math.round(srcH)) };
  }
  const scale = maxDimension / Math.max(srcW, srcH);
  return {
    width: Math.max(1, Math.round(srcW * scale)),
    height: Math.max(1, Math.round(srcH * scale))
  };
}

function drawPreview(drawable, width, height) {
  const canvas = els.preview;
  previewCtx.clearRect(0, 0, canvas.width, canvas.height);
  previewCtx.fillStyle = '#080b12';
  previewCtx.fillRect(0, 0, canvas.width, canvas.height);
  if (!drawable) return;
  const scale = Math.min(canvas.width / width, canvas.height / height);
  const drawW = width * scale;
  const drawH = height * scale;
  const offsetX = (canvas.width - drawW) / 2;
  const offsetY = (canvas.height - drawH) / 2;
  previewCtx.imageSmoothingEnabled = true;
  previewCtx.imageSmoothingQuality = 'high';
  previewCtx.drawImage(drawable, offsetX, offsetY, drawW, drawH);
}

function resetState() {
  if (state.source?.kind === 'image' && state.source.bitmap) {
    state.source.bitmap.close?.();
  }
  state.source = null;
  state.processedBlob = null;
  state.processedInfo = null;
  if (state.downloadUrl) {
    URL.revokeObjectURL(state.downloadUrl);
    state.downloadUrl = null;
  }
  els.process.disabled = true;
  els.download.disabled = true;
  els.infoSource.textContent = '–';
  els.infoFrames.textContent = '–';
  els.infoOutput.textContent = '–';
  els.infoLimit.textContent = `${(parseInt(els.limit.value, 10) / 1024).toFixed(1)} kB`;
  els.infoLimit.classList.remove('ok', 'fail');
  drawPreview(null, 0, 0);
  setStatus('Zurückgesetzt. Datei auswählen, um loszulegen.');
}

function readSettings() {
  return {
    maxDimension: parseInt(els.maxSize.value, 10) || 240,
    colors: parseInt(els.colors.value, 10) || 64,
    fps: parseInt(els.fps.value, 10) || 8,
    maxFrames: parseInt(els.maxFrames.value, 10) || 60,
    limitBytes: parseInt(els.limit.value, 10) || 20000,
    keepAlpha: els.keepAlpha.checked,
    dither: els.dither.checked,
    loop: parseInt(els.loop.value, 10) || 0,
    baseDelay: parseInt(els.baseDelay.value, 10) || 120,
    bgRGB: hexToRgb(els.bgColor.value),
    bgCss: els.bgColor.value
  };
}

function updateLabels() {
  els.maxSizeLabel.textContent = `${els.maxSize.value} px`;
  els.colorsLabel.textContent = `${els.colors.value}`;
  els.fpsLabel.textContent = `${els.fps.value} fps`;
  els.framesLabel.textContent = `${els.maxFrames.value}`;
  els.infoLimit.textContent = `${(parseInt(els.limit.value, 10) / 1024).toFixed(1)} kB`;
}

async function handleFile(file) {
  if (!file) return;
  resetState();
  setStatus('Datei wird geladen …');
  try {
    const kind = detectFileKind(file);
    if (kind === 'video') {
      await prepareVideoSource(file);
    } else if (kind === 'gif') {
      await prepareGifSource(file);
    } else if (kind === 'image') {
      await prepareImageSource(file);
    } else {
      throw new Error('Dateityp wird nicht unterstützt.');
    }
    els.process.disabled = false;
    setStatus('Quelle geladen. Parameter anpassen und „GIF erzeugen“ klicken.', 'success');
  } catch (err) {
    console.error(err);
    setStatus(err.message || 'Fehler beim Laden der Datei.', 'error');
  }
}

function detectFileKind(file) {
  const mime = (file.type || '').toLowerCase();
  const name = (file.name || '').toLowerCase();
  if (mime.startsWith('video/')) return 'video';
  if (mime.startsWith('image/')) {
    if (mime === 'image/gif') return 'gif';
    return 'image';
  }
  if (name.endsWith('.mp4') || name.endsWith('.webm') || name.endsWith('.mov')) return 'video';
  if (name.endsWith('.gif')) return 'gif';
  if (name.endsWith('.jpg') || name.endsWith('.jpeg') || name.endsWith('.png') || name.endsWith('.bmp') || name.endsWith('.webp')) {
    return 'image';
  }
  return null;
}

async function prepareImageSource(file) {
  const bitmap = await createImageBitmap(file);
  state.source = {
    kind: 'image',
    bitmap,
    width: bitmap.width,
    height: bitmap.height,
    name: file.name
  };
  drawPreview(bitmap, bitmap.width, bitmap.height);
  els.infoSource.textContent = `${bitmap.width}×${bitmap.height} · ${formatBytes(file.size)}`;
  els.infoFrames.textContent = '1 (Standbild)';
}

async function prepareGifSource(file) {
  if (!('ImageDecoder' in window)) {
    throw new Error('GIF-Unterstützung benötigt einen Browser mit ImageDecoder (Chromium ≥110).');
  }
  const buffer = await file.arrayBuffer();
  const decoder = new ImageDecoder({ data: buffer, type: file.type || 'image/gif' });
  const track = decoder.tracks?.selectedTrack;
  const frameCount = track?.frameCount ?? 1;
  const first = await decoder.decode({ frameIndex: 0 });
  const bitmap = await createImageBitmap(first.image);
  const frameWidth = first.image.displayWidth || bitmap.width;
  const frameHeight = first.image.displayHeight || bitmap.height;
  drawPreview(bitmap, bitmap.width, bitmap.height);
  bitmap.close();
  first.image.close();
  state.source = {
    kind: 'gif',
    buffer,
    mime: file.type || 'image/gif',
    frameCount,
    width: frameWidth,
    height: frameHeight,
    name: file.name
  };
  els.infoSource.textContent = `${frameWidth}×${frameHeight} · ${formatBytes(file.size)}`;
  els.infoFrames.textContent = `${frameCount} (GIF)`;
}

async function prepareVideoSource(file) {
  const video = document.createElement('video');
  video.muted = true;
  video.playsInline = true;
  video.preload = 'auto';
  const url = URL.createObjectURL(file);
  video.src = url;
  await waitForEvent(video, 'loadedmetadata');
  const width = video.videoWidth || 240;
  const height = video.videoHeight || 240;
  try {
    await seekVideo(video, 0);
  } catch (err) {
    console.warn('Seek 0 fehlgeschlagen', err);
  }
  drawPreview(video, width, height);
  const duration = Number.isFinite(video.duration) ? video.duration : null;
  video.pause();
  video.removeAttribute('src');
  URL.revokeObjectURL(url);

  state.source = {
    kind: 'video',
    file,
    width,
    height,
    duration,
    name: file.name
  };
  els.infoSource.textContent = `Video ${width}×${height} · ${formatBytes(file.size)}`;
  els.infoFrames.textContent = 'Videoquelle';
}

function waitForEvent(target, event) {
  return new Promise((resolve, reject) => {
    const onError = (err) => {
      target.removeEventListener(event, onLoaded);
      reject(err instanceof Error ? err : new Error('Video konnte nicht geladen werden.'));
    };
    const onLoaded = () => {
      target.removeEventListener('error', onError);
      resolve();
    };
    target.addEventListener(event, onLoaded, { once: true });
    target.addEventListener('error', onError, { once: true });
  });
}

function seekVideo(video, time) {
  return new Promise((resolve, reject) => {
    const targetTime = Number.isFinite(video.duration) ? Math.min(time, video.duration) : time;
    const onSeeked = () => {
      video.removeEventListener('error', onError);
      resolve();
    };
    const onError = () => {
      video.removeEventListener('seeked', onSeeked);
      reject(new Error('Video-Seek fehlgeschlagen.'));
    };
    video.addEventListener('seeked', onSeeked, { once: true });
    video.addEventListener('error', onError, { once: true });
    video.currentTime = Math.max(0, targetTime);
  });
}

function renderDrawable(drawable, dims, settings) {
  workingCanvas.width = dims.width;
  workingCanvas.height = dims.height;
  if (settings.keepAlpha) {
    workingCtx.clearRect(0, 0, dims.width, dims.height);
  } else {
    workingCtx.fillStyle = settings.bgCss;
    workingCtx.fillRect(0, 0, dims.width, dims.height);
  }
  workingCtx.imageSmoothingEnabled = true;
  workingCtx.imageSmoothingQuality = 'high';
  workingCtx.drawImage(drawable, 0, 0, dims.width, dims.height);
  return workingCtx.getImageData(0, 0, dims.width, dims.height);
}

async function buildGif(source, settings, onProgress) {
  let encoder = null;
  let frames = 0;
  let usedColors = 0;
  let dimsCache = null;

  const appendFrame = async (imageData, delayMs) => {
    if (!encoder) {
      encoder = new GifEncoder(imageData.width, imageData.height, { loop: settings.loop });
    }
    const q = quantizeImage(imageData, {
      maxColors: settings.colors,
      preserveAlpha: settings.keepAlpha,
      transparentThreshold: 24,
      dither: settings.dither,
      backgroundColor: settings.bgRGB
    });
    encoder.addFrame({
      indexedPixels: q.indexedPixels,
      palette: q.palette,
      delay: delayMs,
      transparentIndex: q.transparentIndex
    });
    frames += 1;
    usedColors = Math.max(usedColors, q.colorCount);
    onProgress?.(frames);
  };

  if (source.kind === 'image') {
    dimsCache = computeTargetDimensions(source.width, source.height, settings.maxDimension);
    const imageData = renderDrawable(source.bitmap, dimsCache, settings);
    await appendFrame(imageData, settings.baseDelay);
  } else if (source.kind === 'gif') {
    await decodeGif(source, settings, async (bitmap, durationMs, frameDims) => {
      if (!dimsCache) {
        dimsCache = computeTargetDimensions(frameDims.width, frameDims.height, settings.maxDimension);
      }
      const frameData = renderDrawable(bitmap, dimsCache, settings);
      bitmap.close();
      const delay = durationMs || settings.baseDelay;
      await appendFrame(frameData, delay);
    });
  } else if (source.kind === 'video') {
    dimsCache = computeTargetDimensions(source.width, source.height, settings.maxDimension);
    await sampleVideo(source, settings, dimsCache, async (frameData, delay) => {
      await appendFrame(frameData, delay);
    });
  }

  if (!encoder) {
    throw new Error('Keine Frames erzeugt (Einstellungen prüfen).');
  }

  const binary = encoder.finish();
  return {
    binary,
    frames,
    colors: usedColors,
    dims: { width: encoder.width, height: encoder.height }
  };
}

async function decodeGif(source, settings, onFrame) {
  if (!('ImageDecoder' in window)) {
    throw new Error('ImageDecoder nicht verfügbar.');
  }
  const decoder = new ImageDecoder({ data: source.buffer, type: source.mime || 'image/gif' });
  const track = decoder.tracks?.selectedTrack;
  const totalFrames = track?.frameCount ?? 1;
  const targetFrames = Math.min(settings.maxFrames, totalFrames);
  const step = Math.max(1, Math.floor(totalFrames / targetFrames));

  let produced = 0;
  for (let i = 0; i < totalFrames && produced < settings.maxFrames; i += step) {
    const result = await decoder.decode({ frameIndex: i });
    const frame = result.image;
    const bitmap = await createImageBitmap(frame);
    const durationUs = frame.duration || 0;
    const durationMs = durationUs ? durationUs / 1000 : 0;
    const frameDims = { width: frame.displayWidth || bitmap.width, height: frame.displayHeight || bitmap.height };
    await onFrame(bitmap, durationMs, frameDims);
    produced += 1;
    frame.close();
  }
}

async function sampleVideo(source, settings, dims, onFrame) {
  const video = document.createElement('video');
  video.muted = true;
  video.preload = 'auto';
  video.playsInline = true;
  const url = URL.createObjectURL(source.file);
  video.src = url;
  await waitForEvent(video, 'loadedmetadata');
  const duration = Number.isFinite(video.duration) ? video.duration : (source.duration || 5);
  const fps = Math.max(1, settings.fps);
  const delay = Math.max(20, Math.round(1000 / fps));
  const step = 1 / fps;
  let captured = 0;

  for (let t = 0; t < duration && captured < settings.maxFrames; t += step) {
    await seekVideo(video, t);
    const imageData = renderDrawable(video, dims, settings);
    await onFrame(imageData, delay);
    captured += 1;
  }

  if (captured === 0) {
    await seekVideo(video, 0);
    const imageData = renderDrawable(video, dims, settings);
    await onFrame(imageData, delay);
  }

  video.pause();
  video.removeAttribute('src');
  URL.revokeObjectURL(url);
}

async function processCurrent() {
  if (!state.source || state.processing) return;
  const settings = readSettings();
  state.processing = true;
  els.process.disabled = true;
  els.download.disabled = true;
  setStatus('Konvertiere … (dies kann je nach Video/GIF einige Sekunden dauern)');

  try {
    const result = await buildGif(state.source, settings, (frameCount) => {
      setStatus(`Konvertiere … ${frameCount} Frames verarbeitet`);
    });

    const blob = new Blob([result.binary], { type: 'image/gif' });
    state.processedBlob = blob;
    state.processedInfo = {
      frames: result.frames,
      colors: result.colors,
      dims: result.dims,
      size: blob.size
    };

    const withinLimit = blob.size <= settings.limitBytes;
    els.infoOutput.textContent = `${result.dims.width}×${result.dims.height} · ${formatBytes(blob.size)} (${result.frames} Frames)`;
    els.infoFrames.textContent = `${result.frames} (Output)`;
    els.infoLimit.classList.remove('ok', 'fail');
    els.infoLimit.classList.add(withinLimit ? 'ok' : 'fail');

    if (!withinLimit) {
      setStatus(`Achtung: ${formatBytes(blob.size)} überschreiten das Limit von ${formatBytes(settings.limitBytes)}. Parameter reduzieren!`, 'error');
      els.download.disabled = true;
    } else {
      setStatus('Fertig! GIF kann geladen werden.', 'success');
      els.download.disabled = false;
    }
  } catch (err) {
    console.error(err);
    setStatus(err.message || 'Fehler beim Encodieren.', 'error');
  } finally {
    state.processing = false;
    els.process.disabled = !state.source;
  }
}

function downloadGif() {
  if (!state.processedBlob) return;
  if (state.downloadUrl) {
    URL.revokeObjectURL(state.downloadUrl);
  }
  const url = URL.createObjectURL(state.processedBlob);
  state.downloadUrl = url;
  const a = document.createElement('a');
  const baseName = state.source?.name?.replace(/\.[^.]+$/, '') || 'slideshow';
  a.href = url;
  a.download = `${baseName}_brosche.gif`;
  a.click();
}

function setupDragDrop() {
  ['dragenter', 'dragover'].forEach((event) => {
    document.addEventListener(event, (e) => {
      e.preventDefault();
      dropDepth += 1;
      document.body.classList.add('drop-active');
    });
  });

  ['dragleave', 'drop'].forEach((event) => {
    document.addEventListener(event, (e) => {
      e.preventDefault();
      dropDepth = Math.max(0, dropDepth - 1);
      if (!dropDepth) {
        document.body.classList.remove('drop-active');
      }
    });
  });

  document.addEventListener('drop', (e) => {
    const file = e.dataTransfer?.files?.[0];
    if (file) {
      handleFile(file);
    }
    dropDepth = 0;
    document.body.classList.remove('drop-active');
  });
}

function init() {
  els.file.addEventListener('change', (e) => {
    const file = e.target.files?.[0];
    if (file) handleFile(file);
  });
  els.process.addEventListener('click', processCurrent);
  els.download.addEventListener('click', downloadGif);
  els.reset.addEventListener('click', resetState);
  [els.maxSize, els.colors, els.fps, els.maxFrames, els.limit].forEach((input) => {
    input.addEventListener('input', updateLabels);
  });

  setupDragDrop();
  updateLabels();
  resetState();
}

init();
