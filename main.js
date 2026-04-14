'use strict';

// ══════════════════════════════════════════════════════════════════════════════
// STEP 1 – 영상/이미지 로드
// ══════════════════════════════════════════════════════════════════════════════

const state = { a: false, b: false };

const slots = {
  a: {
    dropzone:  document.getElementById('dropzone-a'),
    fileInput: document.getElementById('file-input-a'),
    filename:  document.getElementById('filename-a'),    // compact 파일명 표시
    check:     document.getElementById('check-a'),       // 로드 완료 체크 아이콘
    preview:   document.getElementById('preview-a'),     // <video> – 렌더링 전용
    imgEl:     document.getElementById('preview-img-a'), // <img>   – 렌더링 전용
    sourceEl:  null,
    fileType:  null,
    objectURL: null,
  },
  b: {
    dropzone:  document.getElementById('dropzone-b'),
    fileInput: document.getElementById('file-input-b'),
    filename:  document.getElementById('filename-b'),
    check:     document.getElementById('check-b'),
    preview:   document.getElementById('preview-b'),
    imgEl:     document.getElementById('preview-img-b'),
    sourceEl:  null,
    fileType:  null,
    objectURL: null,
  },
};

const statusText = document.getElementById('status-text');
const btnCompare = document.getElementById('btn-compare');

// ── 유틸 ─────────────────────────────────────────────────────────────────────

/** 초 단위 → MM:SS.mmm */
function formatDuration(seconds) {
  if (!isFinite(seconds) || seconds < 0) return '—';
  const totalMs  = Math.round(seconds * 1000);
  const ms       = totalMs % 1000;
  const totalSec = Math.floor(totalMs / 1000);
  const sec      = totalSec % 60;
  const min      = Math.floor(totalSec / 60);
  return `${String(min).padStart(2, '0')}:${String(sec).padStart(2, '0')}.${String(ms).padStart(3, '0')}`;
}

/** hex → [R,G,B] */
function hexToRGB(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 0xff, (n >> 8) & 0xff, n & 0xff];
}

/** 브라우저의 비디오 코덱 지원 여부 확인 */
function checkVideoSupport(file) {
  const probe = document.createElement('video');
  const result = probe.canPlayType(file.type);
  return result !== '';   // 'maybe' 또는 'probably'
}

// ── 미디어 로드 ───────────────────────────────────────────────────────────────
function loadMediaFile(slotId, file) {
  const isVideo = file.type.startsWith('video/');
  const isImage = file.type.startsWith('image/');

  if (!isVideo && !isImage) {
    alert('지원하지 않는 파일 형식입니다.\n영상: MP4 · MOV · WebM\n이미지: JPG · PNG · GIF · WebP');
    return;
  }

  // 코덱 지원 여부 확인
  if (isVideo && !checkVideoSupport(file)) {
    alert(`브라우저가 "${file.type}" 형식을 지원하지 않습니다.\nMP4(H.264) 또는 WebM 파일을 사용해 주세요.`);
    return;
  }

  const slot = slots[slotId];
  if (slot.objectURL) {
    URL.revokeObjectURL(slot.objectURL);
    slot.objectURL = null;
  }

  const objectURL = URL.createObjectURL(file);
  slot.objectURL  = objectURL;

  function onLoaded(w, h) {
    slot.filename.textContent = file.name;
    slot.check.hidden         = false;
    slot.dropzone.classList.add('loaded');
    // 드롭존 title에 해상도 정보 표시
    slot.dropzone.title = `${file.name}  (${w} × ${h})`;
    state[slotId] = true;
    updateStatus();
  }

  if (isVideo) {
    slot.fileType       = 'video';
    slot.preview.src    = objectURL;
    slot.preview.hidden = false;
    slot.imgEl.hidden   = true;
    slot.sourceEl       = slot.preview;

    slot.preview.addEventListener('loadedmetadata', () => {
      const { videoWidth, videoHeight } = slot.preview;
      onLoaded(videoWidth, videoHeight);
    }, { once: true });

  } else {
    slot.fileType        = 'image';
    slot.imgEl.src       = objectURL;
    slot.imgEl.hidden    = false;
    slot.preview.hidden  = true;
    slot.sourceEl        = slot.imgEl;

    slot.imgEl.onload = () => {
      onLoaded(slot.imgEl.naturalWidth, slot.imgEl.naturalHeight);
    };
  }
}

function updateStatus() {
  const { a, b } = state;
  if (a && b) {
    statusText.textContent = '비교 준비 완료';
    btnCompare.disabled    = false;
    console.log('READY: A loaded, B loaded');
  } else if (a) {
    statusText.textContent = 'B를 로드하세요';
    btnCompare.disabled    = true;
  } else if (b) {
    statusText.textContent = 'A를 로드하세요';
    btnCompare.disabled    = true;
  } else {
    statusText.textContent = '두 파일을 로드하세요';
    btnCompare.disabled    = true;
  }
}

function bindSlotEvents(slotId) {
  const { dropzone, fileInput } = slots[slotId];

  dropzone.addEventListener('click', () => {
    if (!dropzone.classList.contains('loaded')) fileInput.click();
  });

  fileInput.addEventListener('change', () => {
    const file = fileInput.files[0];
    if (file) loadMediaFile(slotId, file);
    fileInput.value = '';
  });

  dropzone.addEventListener('dragover', (e) => {
    e.preventDefault();
    e.dataTransfer.dropEffect = 'copy';
    dropzone.classList.add('drag-over');
  });

  dropzone.addEventListener('dragleave', (e) => {
    if (!dropzone.contains(e.relatedTarget)) dropzone.classList.remove('drag-over');
  });

  dropzone.addEventListener('drop', (e) => {
    e.preventDefault();
    dropzone.classList.remove('drag-over');
    const file = e.dataTransfer.files[0];
    if (file) loadMediaFile(slotId, file);
  });
}

// ══════════════════════════════════════════════════════════════════════════════
// STEP 2 – 비교 엔진
// ══════════════════════════════════════════════════════════════════════════════

// Worker 소스를 문자열로 내장 → fetch / file:// 프로토콜 제약 없이 동작
const WORKER_SRC = `'use strict';
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
  self.postMessage({ mask, diffPct: (diffCount / total) * 100 }, [mask.buffer]);
};`;

// ── DOM 참조 ──────────────────────────────────────────────────────────────────
const canvasPlaceholder = document.getElementById('canvas-placeholder');
const compareSection    = document.getElementById('compare-section');
const viewCanvas        = document.getElementById('view-canvas');
const viewCtx        = viewCanvas.getContext('2d', { willReadFrequently: true });
const diffBadge      = document.getElementById('diff-badge');
const thresholdRange = document.getElementById('threshold-range');
const thresholdVal   = document.getElementById('threshold-val');
const colorPicker    = document.getElementById('color-picker');
const sliderHint     = document.getElementById('slider-hint');
const modeBtns       = document.querySelectorAll('.btn-mode');
const resWarning     = document.getElementById('res-warning');
const resWarningMsg  = document.getElementById('res-warning-msg');

// ── 오프스크린 캔버스 ─────────────────────────────────────────────────────────
const offA       = document.createElement('canvas');
const offB       = document.createElement('canvas');
const ctxOffA    = offA.getContext('2d', { willReadFrequently: true });
const ctxOffB    = offB.getContext('2d', { willReadFrequently: true });
const offOver    = document.createElement('canvas');
const ctxOffOver = offOver.getContext('2d');

// ── 비교 상태 ─────────────────────────────────────────────────────────────────
const cmp = {
  mode:         'diff',
  threshold:    10,
  highlightRGB: [255, 0, 0],
  sliderX:      0.5,
  running:      false,
  rafId:        null,
  worker:       null,
  workerBusy:   false,
  lastMask:     null,
  isPlaying:    false,
  isDragging:   false,
  diffEnabled:  true,   // 픽셀 비교 ON/OFF
};

let compareW = 0;
let compareH = 0;

function getSourceSize(slot) {
  return slot.fileType === 'video'
    ? { w: slot.preview.videoWidth,  h: slot.preview.videoHeight }
    : { w: slot.imgEl.naturalWidth, h: slot.imgEl.naturalHeight };
}

// ── 비교 초기화 ───────────────────────────────────────────────────────────────
function initComparison() {
  if (!state.a || !state.b) {
    alert('두 파일을 모두 로드한 후 비교를 시작하세요.');
    return;
  }

  const dimA = getSourceSize(slots.a);
  const dimB = getSourceSize(slots.b);

  // 해상도 불일치 경고
  if (dimA.w !== dimB.w || dimA.h !== dimB.h) {
    resWarningMsg.textContent =
      `해상도가 다릅니다 (A: ${dimA.w}×${dimA.h} / B: ${dimB.w}×${dimB.h}). ` +
      `더 큰 해상도(${Math.max(dimA.w, dimB.w)}×${Math.max(dimA.h, dimB.h)}) 기준으로 자동 조정합니다.`;
    resWarning.hidden = false;
  } else {
    resWarning.hidden = true;
  }

  compareW = Math.max(dimA.w, dimB.w);
  compareH = Math.max(dimA.h, dimB.h);

  offA.width = offB.width = offOver.width  = compareW;
  offA.height = offB.height = offOver.height = compareH;

  resizeViewCanvas(cmp.mode);

  // Report 초기화
  report.log   = [];
  report.lastT = -1;

  // Worker 즉시 생성 (인라인 소스 → 동기적, file:// 프로토콜 호환)
  spawnWorker();

  // 플레이스홀더 숨기고 캔버스 표시
  canvasPlaceholder.hidden = true;
  viewCanvas.style.display = 'block';

  // 재생 관련 UI 설정
  const hasVideo = slots.a.fileType === 'video' || slots.b.fileType === 'video';
  playbackBar.hidden   = !hasVideo;
  btnPlay.disabled     = !hasVideo;
  btnPlay.title        = hasVideo ? '재생 / 일시정지 (Space)' : '이미지 비교 시 재생 불가';
  btnPrevFrame.disabled = !hasVideo;
  btnNextFrame.disabled = !hasVideo;
  speedSelect.disabled  = !hasVideo;

  if (hasVideo) {
    const masterVideo = slots.a.fileType === 'video' ? slots.a.preview : slots.b.preview;
    timelineRange.max = 10000;
    updateTimelineUI(masterVideo);

    // 영상 종료 이벤트
    const onEnded = () => pauseVideos(true);
    slots.a.preview.removeEventListener('ended', onEnded);
    slots.b.preview.removeEventListener('ended', onEnded);
    slots.a.preview.addEventListener('ended', onEnded, { once: true });
    slots.b.preview.addEventListener('ended', onEnded, { once: true });
  }

  cmp.running = true;
  cmp.rafId   = requestAnimationFrame(drawFrame);

  btnCompare.textContent = '비교 시작 ✓';
  btnCompare.disabled    = true;
}

function spawnWorker() {
  if (cmp.worker) cmp.worker.terminate();
  const blob      = new Blob([WORKER_SRC], { type: 'application/javascript' });
  const blobURL   = URL.createObjectURL(blob);
  cmp.worker      = new Worker(blobURL);
  cmp.worker.onmessage = onWorkerMessage;
  cmp.worker.onerror   = (e) => console.error('Worker 오류:', e);
  URL.revokeObjectURL(blobURL);  // Worker 생성 후 즉시 해제 가능
}

function stopComparison() {
  cmp.running = false;
  cancelAnimationFrame(cmp.rafId);
  pauseVideos(false);

  if (cmp.worker) { cmp.worker.terminate(); cmp.worker = null; }

  // 캔버스 숨기고 플레이스홀더 복원
  viewCanvas.style.display = 'none';
  canvasPlaceholder.hidden = false;
  resWarning.hidden        = true;
  cmp.lastMask          = null;
  cmp.workerBusy        = false;
  cmp.isPlaying         = false;

  btnCompare.textContent = '비교 시작';
  btnCompare.disabled    = false;
  btnCompare.addEventListener('click', onBtnCompareClick, { once: true });
}

// ── Worker 응답 ───────────────────────────────────────────────────────────────
function onWorkerMessage({ data }) {
  cmp.lastMask   = data.mask;
  cmp.workerBusy = false;

  const pct = data.diffPct;
  diffBadge.textContent = `${pct.toFixed(2)}%`;
  diffBadge.classList.toggle('high', pct > 20);

  // CSV 이력 누적: 재생 중이고 마지막 기록과 시간이 다를 때만
  if (cmp.isPlaying && slots.a.fileType === 'video') {
    const t = slots.a.preview.currentTime;
    if (Math.abs(t - report.lastT) > 0.001) {
      report.log.push({ time: t, diffPct: pct });
      report.lastT = t;
    }
  }
}

// ── rAF 렌더 루프 ─────────────────────────────────────────────────────────────
function drawFrame() {
  if (!cmp.running) return;

  // 프레임 캡처
  ctxOffA.drawImage(slots.a.sourceEl, 0, 0, compareW, compareH);
  ctxOffB.drawImage(slots.b.sourceEl, 0, 0, compareW, compareH);

  // 동기화: vidA 기준으로 vidB 보정
  syncVideos();

  // Worker에 픽셀 전송
  if (!cmp.workerBusy && cmp.worker) sendToWorker();

  // 렌더링
  renderFrame();

  // 타임라인 UI 업데이트
  if (!isScrubbing && slots.a.fileType === 'video') {
    updateTimelineUI(slots.a.preview);
  }

  cmp.rafId = requestAnimationFrame(drawFrame);
}

/** vidA를 마스터로 vidB currentTime 오차 보정 (0.05초 이내 유지) */
function syncVideos() {
  const vA = slots.a.preview;
  const vB = slots.b.preview;
  if (slots.a.fileType !== 'video' || slots.b.fileType !== 'video') return;
  if (!cmp.isPlaying) return;

  const drift = vA.currentTime - vB.currentTime;
  if (Math.abs(drift) > 0.05) {
    vB.currentTime = vA.currentTime;
  }
}

function sendToWorker() {
  if (!cmp.worker || cmp.workerBusy) return;
  const pixA  = ctxOffA.getImageData(0, 0, compareW, compareH);
  const pixB  = ctxOffB.getImageData(0, 0, compareW, compareH);
  const copyA = new Uint8ClampedArray(pixA.data);
  const copyB = new Uint8ClampedArray(pixB.data);

  cmp.workerBusy = true;
  cmp.worker.postMessage(
    { pixelsA: copyA, pixelsB: copyB, width: compareW, height: compareH, threshold: cmp.threshold },
    [copyA.buffer, copyB.buffer]
  );
}

// ── 렌더러 ───────────────────────────────────────────────────────────────────
function renderFrame() {
  if (compareW === 0) return;
  switch (cmp.mode) {
    case 'diff':       renderDiff();       break;
    case 'sidebyside': renderSideBySide(); break;
    case 'overlay':    renderOverlay();    break;
    case 'slider':     renderSlider();     break;
  }
}

function renderDiff() {
  viewCtx.drawImage(offA, 0, 0);
  if (!cmp.diffEnabled || !cmp.lastMask) return;
  const img = viewCtx.getImageData(0, 0, compareW, compareH);
  applyMask(img.data, cmp.lastMask, cmp.highlightRGB, 255);
  viewCtx.putImageData(img, 0, 0);
}

function renderSideBySide() {
  viewCtx.drawImage(offA, 0,        0);
  viewCtx.drawImage(offB, compareW, 0);
  if (cmp.diffEnabled && cmp.lastMask) {
    const rightImg = viewCtx.getImageData(compareW, 0, compareW, compareH);
    applyMask(rightImg.data, cmp.lastMask, cmp.highlightRGB, 255);
    viewCtx.putImageData(rightImg, compareW, 0);
  }
  viewCtx.strokeStyle = 'rgba(255,255,255,0.3)';
  viewCtx.lineWidth   = 1;
  viewCtx.beginPath();
  viewCtx.moveTo(compareW, 0);
  viewCtx.lineTo(compareW, compareH);
  viewCtx.stroke();
}

function renderOverlay() {
  viewCtx.drawImage(offA, 0, 0);
  if (!cmp.diffEnabled || !cmp.lastMask) return;
  const overlayImg = ctxOffOver.createImageData(compareW, compareH);
  applyMask(overlayImg.data, cmp.lastMask, cmp.highlightRGB, 180);
  ctxOffOver.clearRect(0, 0, compareW, compareH);
  ctxOffOver.putImageData(overlayImg, 0, 0);
  viewCtx.drawImage(offOver, 0, 0);
}

function renderSlider() {
  const splitX = Math.round(cmp.sliderX * compareW);

  viewCtx.save();
  viewCtx.beginPath();
  viewCtx.rect(0, 0, splitX, compareH);
  viewCtx.clip();
  viewCtx.drawImage(offA, 0, 0);
  viewCtx.restore();

  viewCtx.save();
  viewCtx.beginPath();
  viewCtx.rect(splitX, 0, compareW - splitX, compareH);
  viewCtx.clip();
  viewCtx.drawImage(offB, 0, 0);
  viewCtx.restore();

  viewCtx.save();
  viewCtx.strokeStyle = 'rgba(255,255,255,0.92)';
  viewCtx.lineWidth   = 2;
  viewCtx.shadowColor = 'rgba(0,0,0,0.6)';
  viewCtx.shadowBlur  = 6;
  viewCtx.beginPath();
  viewCtx.moveTo(splitX, 0);
  viewCtx.lineTo(splitX, compareH);
  viewCtx.stroke();
  viewCtx.restore();

  viewCtx.save();
  viewCtx.fillStyle = '#fff';
  viewCtx.shadowColor = 'rgba(0,0,0,0.5)';
  viewCtx.shadowBlur  = 8;
  viewCtx.beginPath();
  viewCtx.arc(splitX, compareH / 2, 14, 0, Math.PI * 2);
  viewCtx.fill();
  viewCtx.restore();

  viewCtx.save();
  viewCtx.fillStyle    = 'rgba(0,0,0,0.55)';
  viewCtx.font         = 'bold 12px sans-serif';
  viewCtx.textAlign    = 'center';
  viewCtx.textBaseline = 'middle';
  viewCtx.fillText('⟺', splitX, compareH / 2);
  viewCtx.restore();
}

function applyMask(pixels, mask, [r, g, b], alpha) {
  for (let i = 0; i < mask.length; i++) {
    if (mask[i]) {
      const j = i * 4;
      pixels[j]   = r;
      pixels[j+1] = g;
      pixels[j+2] = b;
      pixels[j+3] = alpha;
    }
  }
}

// ── 뷰 모드 전환 ──────────────────────────────────────────────────────────────
function setMode(mode) {
  cmp.mode = mode;
  resizeViewCanvas(mode);
  const isSlider = mode === 'slider';
  viewCanvas.classList.toggle('slider-mode', isSlider);
  sliderHint.hidden = !isSlider;
  modeBtns.forEach(btn => btn.classList.toggle('active', btn.dataset.mode === mode));
  if (compareW > 0) renderFrame();
}

function resizeViewCanvas(mode) {
  viewCanvas.width  = mode === 'sidebyside' ? compareW * 2 : compareW;
  viewCanvas.height = compareH;
}

// ══════════════════════════════════════════════════════════════════════════════
// STEP 3 – 재생 컨트롤 + 내보내기
// ══════════════════════════════════════════════════════════════════════════════

// ── DOM 참조: 재생 컨트롤 ─────────────────────────────────────────────────────
const btnPlay      = document.getElementById('btn-play');
const iconPlay     = document.getElementById('icon-play');
const iconPause    = document.getElementById('icon-pause');
const btnPrevFrame = document.getElementById('btn-prev-frame');
const btnNextFrame = document.getElementById('btn-next-frame');
const timeDisplay  = document.getElementById('time-display');
const timelineRange = document.getElementById('timeline-range');
const speedSelect  = document.getElementById('speed-select');
const playbackBar  = document.getElementById('playback-bar');

// ── DOM 참조: 내보내기 ────────────────────────────────────────────────────────
const btnSavePNG = document.getElementById('btn-save-png');
const btnSaveCSV = document.getElementById('btn-save-csv');

// ── CSV 리포트 누적 데이터 ────────────────────────────────────────────────────
const report = { log: [], lastT: -1 };

// ── 재생 컨트롤 ───────────────────────────────────────────────────────────────
const FRAME_SEC = 1 / 30;   // 1프레임 = 1/30초

/** vidA를 마스터로 두 영상 동시 재생 */
function playVideos() {
  const vA = slots.a.preview;
  const vB = slots.b.preview;

  if (slots.a.fileType === 'video') vA.play().catch(handlePlayError);
  if (slots.b.fileType === 'video') vB.play().catch(handlePlayError);

  cmp.isPlaying = true;
  btnPlay.classList.add('playing');
  iconPlay.hidden  = true;
  iconPause.hidden = false;
  btnPlay.lastChild.textContent = '정지';
}

/** 두 영상 일시정지
 * @param {boolean} resetUI - true이면 버튼 상태도 초기화 (종료 시)
 */
function pauseVideos(resetUI = true) {
  if (slots.a.fileType === 'video') slots.a.preview.pause();
  if (slots.b.fileType === 'video') slots.b.preview.pause();

  cmp.isPlaying = false;
  if (!resetUI) return;

  btnPlay.classList.remove('playing');
  iconPlay.hidden  = false;
  iconPause.hidden = true;
  btnPlay.lastChild.textContent = '재생';
}

function handlePlayError(err) {
  if (err.name !== 'AbortError') {
    console.warn('재생 오류:', err);
    pauseVideos(true);
  }
}

/** 프레임 단위 이동 (dir: +1 또는 -1) */
function stepFrame(dir) {
  const vA = slots.a.preview;
  const vB = slots.b.preview;
  if (slots.a.fileType !== 'video') return;

  const t = Math.max(0, Math.min(vA.duration, vA.currentTime + dir * FRAME_SEC));
  vA.currentTime = t;
  if (slots.b.fileType === 'video') vB.currentTime = t;

  // 정지 상태에서도 즉시 렌더링
  if (!cmp.isPlaying) {
    // 한 틱 후 렌더링 (브라우저가 currentTime을 적용할 시간 확보)
    requestAnimationFrame(() => {
      ctxOffA.drawImage(slots.a.sourceEl, 0, 0, compareW, compareH);
      ctxOffB.drawImage(slots.b.sourceEl, 0, 0, compareW, compareH);
      cmp.workerBusy = false;
      sendToWorker();
      renderFrame();
    });
  }
}

/** 타임라인 UI 업데이트 (매 rAF 틱) */
let isScrubbing = false;

function updateTimelineUI(video) {
  if (!video || !isFinite(video.duration)) return;
  const t = video.currentTime;
  const d = video.duration;
  timeDisplay.textContent = `${formatDuration(t)} / ${formatDuration(d)}`;
  timelineRange.value = Math.round((t / d) * 10000);
}

/** 재생 속도를 두 영상에 동시 적용 */
function applyPlaybackRate(rate) {
  if (slots.a.fileType === 'video') slots.a.preview.playbackRate = rate;
  if (slots.b.fileType === 'video') slots.b.preview.playbackRate = rate;
}

// ── 재생 이벤트 ───────────────────────────────────────────────────────────────
btnPlay.addEventListener('click', () => {
  if (!cmp.running) {
    alert('비교를 먼저 시작하세요.');
    return;
  }
  if (cmp.isPlaying) pauseVideos(true);
  else               playVideos();
});

btnPrevFrame.addEventListener('click', () => stepFrame(-1));
btnNextFrame.addEventListener('click', () => stepFrame( 1));

// 타임라인 스크러버
timelineRange.addEventListener('mousedown',  () => { isScrubbing = true; });
timelineRange.addEventListener('touchstart', () => { isScrubbing = true; }, { passive: true });
timelineRange.addEventListener('mouseup',    () => { isScrubbing = false; });
timelineRange.addEventListener('touchend',   () => { isScrubbing = false; });

timelineRange.addEventListener('input', () => {
  const pct = +timelineRange.value / 10000;
  const vA  = slots.a.preview;
  const vB  = slots.b.preview;

  if (slots.a.fileType === 'video' && isFinite(vA.duration)) {
    const t = pct * vA.duration;
    vA.currentTime = t;
    timeDisplay.textContent = `${formatDuration(t)} / ${formatDuration(vA.duration)}`;
  }
  if (slots.b.fileType === 'video' && isFinite(vB.duration)) {
    vB.currentTime = pct * vB.duration;
  }
});

// 재생 속도
speedSelect.addEventListener('change', () => {
  applyPlaybackRate(+speedSelect.value);
});

// ── 키보드 단축키 ─────────────────────────────────────────────────────────────
document.addEventListener('keydown', (e) => {
  // input/select/textarea 포커스 중 단축키 비활성화
  const tag = document.activeElement.tagName;
  if (tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return;
  if (!cmp.running) return;

  switch (e.code) {
    case 'Space':
      e.preventDefault();
      if (cmp.isPlaying) pauseVideos(true);
      else               playVideos();
      break;
    case 'ArrowLeft':
      e.preventDefault();
      stepFrame(-1);
      break;
    case 'ArrowRight':
      e.preventDefault();
      stepFrame( 1);
      break;
  }
});

// ── 슬라이더 드래그 ───────────────────────────────────────────────────────────
function canvasXToSliderX(clientX) {
  const rect   = viewCanvas.getBoundingClientRect();
  const scaleX = compareW / rect.width;
  return Math.max(0, Math.min(1, (clientX - rect.left) * scaleX / compareW));
}

viewCanvas.addEventListener('mousedown', (e) => {
  if (cmp.mode !== 'slider') return;
  cmp.isDragging = true;
  cmp.sliderX    = canvasXToSliderX(e.clientX);
  renderFrame();
});

window.addEventListener('mousemove', (e) => {
  if (!cmp.isDragging) return;
  cmp.sliderX = canvasXToSliderX(e.clientX);
  renderFrame();
});

window.addEventListener('mouseup', () => { cmp.isDragging = false; });

viewCanvas.addEventListener('touchstart', (e) => {
  if (cmp.mode !== 'slider') return;
  e.preventDefault();
  cmp.isDragging = true;
  cmp.sliderX    = canvasXToSliderX(e.touches[0].clientX);
  renderFrame();
}, { passive: false });

window.addEventListener('touchmove', (e) => {
  if (!cmp.isDragging) return;
  e.preventDefault();
  cmp.sliderX = canvasXToSliderX(e.touches[0].clientX);
  renderFrame();
}, { passive: false });

window.addEventListener('touchend', () => { cmp.isDragging = false; });

// ── 임계값 / 색상 ─────────────────────────────────────────────────────────────
thresholdRange.addEventListener('input', () => {
  cmp.threshold            = +thresholdRange.value;
  thresholdVal.textContent = cmp.threshold;
  cmp.workerBusy           = false;
  sendToWorker();
});

colorPicker.addEventListener('input', () => {
  cmp.highlightRGB = hexToRGB(colorPicker.value);
  if (compareW > 0) renderFrame();
});

modeBtns.forEach(btn => btn.addEventListener('click', () => setMode(btn.dataset.mode)));

// ── 비교 ON/OFF 토글 ──────────────────────────────────────────────────────────
const btnDiffToggle = document.getElementById('btn-diff-toggle');

btnDiffToggle.addEventListener('click', () => {
  cmp.diffEnabled = !cmp.diffEnabled;
  btnDiffToggle.classList.toggle('active', cmp.diffEnabled);
  btnDiffToggle.textContent = cmp.diffEnabled ? '비교 ON' : '비교 OFF';
  if (compareW > 0) renderFrame();
});

// ══════════════════════════════════════════════════════════════════════════════
// STEP 3 – 내보내기
// ══════════════════════════════════════════════════════════════════════════════

// ── PNG 스크린샷 ──────────────────────────────────────────────────────────────
btnSavePNG.addEventListener('click', () => {
  if (!cmp.running || compareW === 0) {
    alert('비교를 먼저 시작하세요.');
    return;
  }

  // 파일명: diff_frame_MM-SS-mmm.png
  const t        = slots.a.fileType === 'video' ? slots.a.preview.currentTime : 0;
  const stamp    = formatDuration(t).replace(':', '-').replace('.', '-');  // MM-SS-mmm
  const filename = `diff_frame_${stamp}.png`;

  const url = viewCanvas.toDataURL('image/png');
  triggerDownload(url, filename);
});

// ── CSV 리포트 ────────────────────────────────────────────────────────────────
btnSaveCSV.addEventListener('click', () => {
  if (!cmp.running) {
    alert('비교를 먼저 시작하세요.');
    return;
  }
  if (report.log.length === 0) {
    alert('저장할 재생 이력이 없습니다.\n영상을 재생한 후 다시 시도하세요.');
    return;
  }

  const lines = ['Time,DiffPercent'];
  for (const { time, diffPct } of report.log) {
    lines.push(`${formatDuration(time)},${diffPct.toFixed(2)}`);
  }

  const blob = new Blob([lines.join('\r\n')], { type: 'text/csv;charset=utf-8;' });
  const url  = URL.createObjectURL(blob);
  triggerDownload(url, 'diff_report.csv');
  setTimeout(() => URL.revokeObjectURL(url), 2000);
});

/** <a download> 방식 즉시 다운로드 */
function triggerDownload(url, filename) {
  const a    = document.createElement('a');
  a.href     = url;
  a.download = filename;
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

// ── 비교 시작 ─────────────────────────────────────────────────────────────────
function onBtnCompareClick() {
  if (!state.a || !state.b) {
    alert('두 파일을 모두 로드한 후 비교를 시작하세요.');
    return;
  }
  initComparison();
}

btnCompare.addEventListener('click', onBtnCompareClick, { once: true });

// ── 초기화 ────────────────────────────────────────────────────────────────────
bindSlotEvents('a');
bindSlotEvents('b');
