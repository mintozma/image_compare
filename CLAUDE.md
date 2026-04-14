# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

순수 바닐라 JS 웹 앱으로 **빌드 과정 없음**. 브라우저에서 `index.html`을 직접 열어 실행한다.

```
# 로컬 실행 (Node.js 설치 시)
npx serve .
# 또는 그냥 index.html을 브라우저에서 열기
```

## Architecture

### 파일 구성
- `index.html` — 전체 UI 마크업. CSS/JS는 외부 파일로 분리.
- `style.css` — CSS 변수(`--bg-base`, `--accent` 등) 기반 다크 테마. 반응형(700px 분기점).
- `main.js` — 앱 로직 전체. 세 단계로 구성:
  - **STEP 1**: 파일 드롭존 이벤트, 미디어 로드 (`loadMediaFile`)
  - **STEP 2**: 비교 엔진 — rAF 루프(`drawFrame`), 4가지 렌더 모드, Worker 통신
  - **STEP 3**: 재생 컨트롤(영상 전용), PNG/CSV 내보내기
- `worker.js` — 픽셀 차이 계산 전용 Web Worker. **단, main.js 내부에도 동일 코드가 인라인 문자열로 내장**되어 있어 `file://` 프로토콜에서도 동작한다.

### 핵심 데이터 흐름

```
파일 드롭/선택
  → loadMediaFile() → <video> 또는 <img> 에 objectURL 할당
  → initComparison() → offscreen canvas 크기 확정, Worker 생성
  → rAF 루프: drawFrame()
      ├─ offA/offB에 현재 프레임 그리기
      ├─ sendToWorker() → Worker가 mask(Uint8Array) + diffPct 반환
      └─ renderFrame() → 현재 mode에 따라 viewCanvas에 합성
```

### 렌더 모드 (`cmp.mode`)
| 값 | 동작 |
|---|---|
| `diff` | offA 위에 차이 픽셀을 강조색으로 덮어씀 |
| `sidebyside` | 캔버스 폭을 2배로 늘려 좌=A, 우=B+마스크 |
| `overlay` | offA 위에 반투명 마스크 레이어 합성 |
| `slider` | clip으로 좌=A / 우=B 분할, 드래그 가능 |

### Worker 통신
- `sendToWorker()`: `pixelsA`, `pixelsB`(Transferable ArrayBuffer)와 `threshold`를 전송
- `onWorkerMessage()`: `mask`(Uint8Array)와 `diffPct` 수신 → `cmp.lastMask` 갱신

### 영상 동기화
`syncVideos()`: A를 마스터로 두고 B의 `currentTime` 오차가 0.05초 초과 시 강제 보정.

## 주요 전역 상태

- `slots.a / slots.b` — 각 슬롯의 DOM 참조, 미디어 엘리먼트, 파일 타입
- `cmp` — 비교 엔진 상태(mode, threshold, highlightRGB, sliderX, worker 등)
- `compareW / compareH` — 비교 기준 해상도(두 소스 중 큰 값)
- `report.log` — CSV 내보내기용 `{time, diffPct}` 배열

## 환경 메모 (Windows)

- 컴파일러: nProtect Online Security(`nossvc` 서비스)가 MinGW `cc1.exe` 실행을 차단함. C 소스(`filediff.c`) 컴파일 필요 시 `net stop nossvc` 후 진행.
- Node.js 24 설치됨 (`C:\Program Files\nodejs`)
- GitHub 계정: `mintozma`, 원격 저장소: `https://github.com/mintozma/image_compare`
