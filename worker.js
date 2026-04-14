'use strict';

/**
 * 픽셀 비교 Worker
 * Input : { pixelsA, pixelsB, width, height, threshold }
 * Output: { mask, diffPct }
 *   mask    – Uint8Array (length = width*height), 1 = 차이 픽셀
 *   diffPct – 차이 픽셀 비율 (0‒100)
 */
self.onmessage = ({ data }) => {
  const { pixelsA, pixelsB, width, height, threshold } = data;

  const total = width * height;
  const mask  = new Uint8Array(total);
  let diffCount = 0;

  for (let i = 0; i < total; i++) {
    const j  = i * 4;
    const dR = pixelsA[j]     - pixelsB[j];
    const dG = pixelsA[j + 1] - pixelsB[j + 1];
    const dB = pixelsA[j + 2] - pixelsB[j + 2];

    if (Math.sqrt(dR * dR + dG * dG + dB * dB) > threshold) {
      mask[i] = 1;
      diffCount++;
    }
  }

  self.postMessage(
    { mask, diffPct: (diffCount / total) * 100 },
    [mask.buffer]   // Transferable – zero-copy
  );
};