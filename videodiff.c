/*
 * videodiff.c  —  Image / Video Frame Color Diff Tool (Win32 GUI)
 *
 * Compile:
 *   gcc -O2 -mwindows -o VideoColorDiff_C.exe videodiff.c ^
 *       -lgdi32 -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lwindowscodecs
 *
 * Features:
 *   - 이미지/영상 프레임 로드 (PNG, JPEG, BMP, GIF 등 WIC 지원 포맷)
 *   - 4가지 비교 모드: 차이, 나란히, 오버레이, 슬라이더
 *   - 임계값 조절 (0-255)
 *   - 강조색 선택
 *   - 차이 픽셀 비율(%) 표시
 *   - PNG / CSV 내보내기
 *   - 드래그&드롭 파일 로드
 */

#define UNICODE
#define _UNICODE
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <wchar.h>

/* ── stb_image (PNG/JPEG/BMP 로더) ─────────────────────────────────────── */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* ══════════════════════════════════════════════════════════════════════════
   상수 / 매크로
   ══════════════════════════════════════════════════════════════════════════ */
#define APP_NAME    L"Image Color Diff Tool"
#define CANVAS_CLS  L"DiffCanvas"
#define ZONE_CLS    L"DropZone"

#define ZONE_H      70   /* 드롭존 높이 */

/* 뷰 모드 (차이 표시는 독립 토글) */
#define MODE_SIDEBYSIDE 0
#define MODE_OVERLAY    1
#define MODE_SLIDER     2

/* 컨트롤 ID */
#define IDC_BTN_LOAD_A    101
#define IDC_BTN_LOAD_B    102
#define IDC_BTN_COMPARE   103
#define IDC_BTN_DIFF_TOGGLE 109   /* 차이 보기 ON/OFF */
#define IDC_BTN_SIDE      110
#define IDC_BTN_OVERLAY   111
#define IDC_BTN_SLIDER    112
#define IDC_SLIDER_THRESH 120
#define IDC_LABEL_THRESH  121
#define IDC_BTN_COLOR     122
#define IDC_BTN_SAVE_PNG  130
#define IDC_BTN_SAVE_CSV  131
#define IDC_CANVAS        140
#define IDC_STATUS        150
#define IDC_DIFF_BADGE    151

/* 레이아웃 상수 (픽셀) */
#define TOPBAR_H  60
#define TOOLBAR_H 44
#define STATUS_H  24
#define BTN_W     100
#define BTN_H     28
#define SMALL_W   72
#define SMALL_H   26

/* ══════════════════════════════════════════════════════════════════════════
   데이터 구조
   ══════════════════════════════════════════════════════════════════════════ */
typedef unsigned char BYTE;

typedef struct {
    BYTE*  pixels;          /* RGBA, w*h*4 바이트 */
    int    w, h;
    WCHAR  path[MAX_PATH];
    BOOL   loaded;
} Slot;

/* ══════════════════════════════════════════════════════════════════════════
   전역 상태
   ══════════════════════════════════════════════════════════════════════════ */
static Slot   gA = {0}, gB = {0};
static BYTE*  gMask   = NULL;   /* 1=차이 픽셀, 크기 = cW*cH */
static float  gDiffPct = 0.f;
static int    cW = 0, cH = 0;  /* 비교 해상도 */

static int    gMode      = MODE_SIDEBYSIDE;
static BOOL   gDiffOn    = TRUE;   /* 차이 강조 ON/OFF (뷰 모드와 독립) */
static int    gThresh    = 10;
static BYTE   gHL_R = 255, gHL_G = 0, gHL_B = 0;  /* 강조색 */
static float  gSliderX   = 0.5f;
static BOOL   gSliderDrag = FALSE;
static BOOL   gCompared   = FALSE;

/* CSV 로그 */
typedef struct { int idx; float pct; } LogEntry;
static LogEntry* gLog    = NULL;
static int       gLogLen = 0;
static int       gLogCap = 0;

/* 드롭존 상태 */
typedef struct {
    int  slotId;    /* 0=A, 1=B */
    BOOL hover;     /* 마우스 오버 중 */
    BOOL dragOver;  /* 파일 드래그 중 (OLE 없이 WM_DROPFILES 전용) */
} ZoneData;

/* HWND 캐시 */
static HWND hWnd       = NULL;
static HWND hCanvas    = NULL;
static HWND hStatus    = NULL;
static HWND hBadge     = NULL;
static HWND hThreshSlider = NULL;
static HWND hThreshLabel  = NULL;
static HWND hBtnDiffToggle = NULL;  /* 차이 보기 토글 */
static HWND hModeBtn[3]   = {NULL};
static HWND hZoneA = NULL, hZoneB = NULL;  /* 드롭존 */
static HWND hBtnCompare = NULL;
static HWND hBtnColor = NULL;
static HWND hBtnSavePNG = NULL, hBtnSaveCSV = NULL;

/* ══════════════════════════════════════════════════════════════════════════
   전방 선언
   ══════════════════════════════════════════════════════════════════════════ */
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CanvasProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ZoneProc(HWND, UINT, WPARAM, LPARAM);
static void PaintZone(HWND hw, HDC hdc);

static BOOL  LoadSlot(Slot* s, const WCHAR* path);
static void  FreeSlot(Slot* s);
static void  DoCompare(void);
static void  PaintCanvas(HWND hc, HDC hdc);
static void  RenderSideBySide(BYTE* out, int w, int h, BOOL diffOn);
static void  RenderOverlay   (BYTE* out, int w, int h, BOOL diffOn);
static void  RenderSlider    (BYTE* out, int w, int h, float sx, BOOL diffOn);
static BOOL  PickFile(HWND owner, WCHAR* outPath);
static void  SavePNG(void);
static void  SaveCSV(void);
static void  SetStatus(const WCHAR* fmt, ...);
static void  UpdateBadge(void);
static void  SetMode(int m);
static BYTE  Clamp8(int v);
static void  AppendLog(float pct);
static void  CreateControls(HWND hw, HINSTANCE hi);
static void  RelayoutControls(HWND hw);

/* ══════════════════════════════════════════════════════════════════════════
   유틸리티
   ══════════════════════════════════════════════════════════════════════════ */
static inline BYTE Clamp8(int v) {
    return (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static void SetStatus(const WCHAR* fmt, ...) {
    WCHAR buf[512];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(buf, 512, fmt, ap);
    va_end(ap);
    if (hStatus) SetWindowTextW(hStatus, buf);
}

static void UpdateBadge(void) {
    WCHAR buf[64];
    if (gCompared)
        _snwprintf(buf, 64, L"%.2f%%", gDiffPct);
    else
        wcscpy(buf, L"—");
    if (hBadge) SetWindowTextW(hBadge, buf);
}

static void AppendLog(float pct) {
    if (gLogLen >= gLogCap) {
        gLogCap = gLogCap ? gLogCap * 2 : 256;
        gLog = (LogEntry*)realloc(gLog, gLogCap * sizeof(LogEntry));
    }
    gLog[gLogLen].idx = gLogLen;
    gLog[gLogLen].pct = pct;
    gLogLen++;
}

static void SetMode(int m) {
    gMode = m;
    for (int i = 0; i < 3; i++) {
        if (hModeBtn[i])
            SendMessageW(hModeBtn[i], BM_SETSTATE, i == m, 0);
    }
    if (hCanvas) InvalidateRect(hCanvas, NULL, FALSE);
}

static void SetDiffToggle(BOOL on) {
    gDiffOn = on;
    if (hBtnDiffToggle)
        SetWindowTextW(hBtnDiffToggle, on ? L"차이 ON" : L"차이 OFF");
    if (hCanvas) InvalidateRect(hCanvas, NULL, FALSE);
}

/* ══════════════════════════════════════════════════════════════════════════
   이미지 로드  (stb_image → RGBA)
   ══════════════════════════════════════════════════════════════════════════ */
static BOOL LoadSlot(Slot* s, const WCHAR* path) {
    FreeSlot(s);

    /* WCHAR → UTF-8 변환 (stb_image는 UTF-8 경로 지원) */
    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);

    int w, h, n;
    BYTE* data = stbi_load(utf8, &w, &h, &n, 4);  /* 항상 RGBA 출력 */
    if (!data) {
        SetStatus(L"로드 실패: %s", path);
        return FALSE;
    }

    s->pixels = data;
    s->w = w;
    s->h = h;
    wcscpy(s->path, path);
    s->loaded = TRUE;

    /* 파일명만 추출 */
    const WCHAR* fn = wcsrchr(path, L'\\');
    fn = fn ? fn + 1 : path;
    SetStatus(L"로드됨: %s  (%d × %d)", fn, w, h);
    return TRUE;
}

static void FreeSlot(Slot* s) {
    if (s->pixels) { stbi_image_free(s->pixels); s->pixels = NULL; }
    s->loaded = FALSE;
    s->w = s->h = 0;
}

/* ══════════════════════════════════════════════════════════════════════════
   드롭존 페인트
   ══════════════════════════════════════════════════════════════════════════ */
static void PaintZone(HWND hw, HDC hdc) {
    ZoneData* zd = (ZoneData*)GetWindowLongPtrW(hw, GWLP_USERDATA);
    if (!zd) return;

    Slot* s = (zd->slotId == 0) ? &gA : &gB;
    const WCHAR* label = (zd->slotId == 0) ? L"영상 A" : L"영상 B";

    RECT rc; GetClientRect(hw, &rc);
    int W = rc.right, H = rc.bottom;

    /* ── 백 버퍼 ── */
    HDC     memDC  = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ memOld = SelectObject(memDC, memBmp);

    /* 배경 */
    COLORREF bgColor  = s->loaded  ? RGB(14, 40, 20)  :
                        zd->hover  ? RGB(35, 35, 50)   :
                                     RGB(22, 22, 30);
    COLORREF brdColor = s->loaded  ? RGB(74, 222, 128) :
                        zd->hover  ? RGB(120, 120, 200):
                                     RGB(60, 60, 80);
    HBRUSH hBg = CreateSolidBrush(bgColor);
    FillRect(memDC, &rc, hBg);
    DeleteObject(hBg);

    /* 테두리 (점선 효과: 직접 사각형 그리기) */
    HPEN hPen = CreatePen(PS_DASH, 1, brdColor);
    HPEN oldPen = (HPEN)SelectObject(memDC, hPen);
    HBRUSH hNoBrush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, hNoBrush);
    RoundRect(memDC, 2, 2, W-2, H-2, 10, 10);
    SelectObject(memDC, oldPen);
    SelectObject(memDC, oldBrush);
    DeleteObject(hPen);

    SetBkMode(memDC, TRANSPARENT);

    if (s->loaded) {
        /* ── 로드됨: 체크 아이콘 + 파일명 ── */
        /* 체크 원 */
        HPEN gPen   = CreatePen(PS_SOLID, 2, RGB(74, 222, 128));
        HPEN gOld   = (HPEN)SelectObject(memDC, gPen);
        HBRUSH gBr  = CreateSolidBrush(RGB(30, 80, 40));
        HBRUSH gBrO = (HBRUSH)SelectObject(memDC, gBr);
        int cx = 24, cy = H/2, r = 10;
        Ellipse(memDC, cx-r, cy-r, cx+r, cy+r);
        SelectObject(memDC, gOld); SelectObject(memDC, gBrO);
        DeleteObject(gPen); DeleteObject(gBr);

        /* 체크 마크 */
        HPEN ckPen  = CreatePen(PS_SOLID, 2, RGB(74, 222, 128));
        HPEN ckOld  = (HPEN)SelectObject(memDC, ckPen);
        MoveToEx(memDC, cx-5, cy,   NULL);
        LineTo  (memDC, cx-1, cy+4);
        LineTo  (memDC, cx+5, cy-4);
        SelectObject(memDC, ckOld); DeleteObject(ckPen);

        /* 슬롯 레이블 */
        HFONT hfLabel = CreateFontW(11, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT oldF = (HFONT)SelectObject(memDC, hfLabel);
        SetTextColor(memDC, RGB(74, 222, 128));
        RECT rLabel = {40, 6, W-8, 24};
        DrawTextW(memDC, label, -1, &rLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(memDC, oldF); DeleteObject(hfLabel);

        /* 파일명 (잘라서 표시) */
        HFONT hfFile = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        oldF = (HFONT)SelectObject(memDC, hfFile);
        SetTextColor(memDC, RGB(200, 220, 200));
        const WCHAR* fn = wcsrchr(s->path, L'\\');
        fn = fn ? fn+1 : s->path;
        RECT rFile = {40, H/2 - 9, W - 8, H/2 + 9};
        DrawTextW(memDC, fn, -1, &rFile,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        /* 해상도 */
        WCHAR dim[32];
        _snwprintf(dim, 32, L"%d × %d", s->w, s->h);
        SetTextColor(memDC, RGB(120, 150, 120));
        RECT rDim = {40, H - 22, W - 8, H - 4};
        DrawTextW(memDC, dim, -1, &rDim, DT_LEFT | DT_SINGLELINE);
        SelectObject(memDC, oldF); DeleteObject(hfFile);

    } else {
        /* ── 비어있음: 업로드 아이콘 + 안내 텍스트 ── */
        /* 화살표 아이콘 */
        int cx = W/2, arY = H/2 - 12;
        HPEN uPen  = CreatePen(PS_SOLID, 2, brdColor);
        HPEN uOld  = (HPEN)SelectObject(memDC, uPen);
        HBRUSH uBr = (HBRUSH)GetStockObject(NULL_BRUSH);
        SelectObject(memDC, uBr);
        /* 수직선 */
        MoveToEx(memDC, cx, arY, NULL); LineTo(memDC, cx, arY + 16);
        /* 화살 머리 */
        MoveToEx(memDC, cx - 6, arY + 6, NULL); LineTo(memDC, cx, arY);
        LineTo(memDC, cx + 6, arY + 6);
        /* 바닥 가로선 */
        MoveToEx(memDC, cx - 8, arY + 20, NULL); LineTo(memDC, cx + 8, arY + 20);
        SelectObject(memDC, uOld); DeleteObject(uPen);

        /* 슬롯 레이블 */
        HFONT hfL = CreateFontW(13, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT oldF = (HFONT)SelectObject(memDC, hfL);
        SetTextColor(memDC, zd->hover ? RGB(180,180,255) : RGB(140,140,170));
        RECT rL = {0, 4, W, 20};
        DrawTextW(memDC, label, -1, &rL, DT_CENTER | DT_SINGLELINE);
        SelectObject(memDC, oldF); DeleteObject(hfL);

        /* 안내 문구 */
        HFONT hfHint = CreateFontW(11, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        oldF = (HFONT)SelectObject(memDC, hfHint);
        SetTextColor(memDC, zd->hover ? RGB(150,150,220) : RGB(90, 90, 110));
        RECT rHint = {0, H - 20, W, H - 4};
        DrawTextW(memDC, L"드래그 또는 클릭", -1, &rHint, DT_CENTER | DT_SINGLELINE);
        SelectObject(memDC, oldF); DeleteObject(hfHint);
    }

    /* 백 버퍼 → 화면 */
    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, memOld);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

/* ══════════════════════════════════════════════════════════════════════════
   드롭존 WndProc
   ══════════════════════════════════════════════════════════════════════════ */
LRESULT CALLBACK ZoneProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    ZoneData* zd = (ZoneData*)GetWindowLongPtrW(hw, GWLP_USERDATA);

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        PaintZone(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;

    /* 마우스 오버 → 밝기 변경 */
    case WM_MOUSEMOVE:
        if (!zd->hover) {
            zd->hover = TRUE;
            InvalidateRect(hw, NULL, FALSE);
            /* 마우스 이탈 추적 */
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hw, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;

    case WM_MOUSELEAVE:
        zd->hover = FALSE;
        InvalidateRect(hw, NULL, FALSE);
        return 0;

    /* 클릭 → 파일 대화상자 */
    case WM_LBUTTONDOWN: {
        WCHAR p[MAX_PATH] = L"";
        if (PickFile(GetParent(hw), p)) {
            Slot* s = (zd->slotId == 0) ? &gA : &gB;
            LoadSlot(s, p);
            InvalidateRect(hw, NULL, FALSE);
            EnableWindow(hBtnCompare, gA.loaded && gB.loaded);
        }
        return 0;
    }

    /* 파일 드롭 */
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        WCHAR p[MAX_PATH];
        /* 첫 번째 파일만 이 슬롯에 로드 */
        if (DragQueryFileW(hd, 0, p, MAX_PATH)) {
            Slot* s = (zd->slotId == 0) ? &gA : &gB;
            LoadSlot(s, p);
            InvalidateRect(hw, NULL, FALSE);
            EnableWindow(hBtnCompare, gA.loaded && gB.loaded);
        }
        DragFinish(hd);
        return 0;
    }

    /* 커서: 손 모양 */
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ══════════════════════════════════════════════════════════════════════════
   픽셀 비교
   ══════════════════════════════════════════════════════════════════════════ */
static void DoCompare(void) {
    if (!gA.loaded || !gB.loaded) return;

    /* 비교 해상도 = 두 이미지 중 큰 쪽 */
    cW = gA.w > gB.w ? gA.w : gB.w;
    cH = gA.h > gB.h ? gA.h : gB.h;

    free(gMask);
    gMask = (BYTE*)calloc(cW * cH, 1);

    int diffCount = 0;
    const BYTE* pA = gA.pixels;
    const BYTE* pB = gB.pixels;

    for (int y = 0; y < cH; y++) {
        for (int x = 0; x < cW; x++) {
            /* A 픽셀 (범위 밖이면 검정) */
            int rA = 0, gAv = 0, bA = 0;
            if (x < gA.w && y < gA.h) {
                int j = (y * gA.w + x) * 4;
                rA = pA[j]; gAv = pA[j+1]; bA = pA[j+2];
            }
            /* B 픽셀 */
            int rB = 0, gBv = 0, bB = 0;
            if (x < gB.w && y < gB.h) {
                int j = (y * gB.w + x) * 4;
                rB = pB[j]; gBv = pB[j+1]; bB = pB[j+2];
            }

            int dR = rA - rB, dG = gAv - gBv, dB2 = bA - bB;
            float dist = sqrtf((float)(dR*dR + dG*dG + dB2*dB2));
            if (dist > gThresh) {
                gMask[y * cW + x] = 1;
                diffCount++;
            }
        }
    }

    gDiffPct = cW * cH > 0 ? (float)diffCount / (cW * cH) * 100.f : 0.f;
    gCompared = TRUE;
    AppendLog(gDiffPct);
    UpdateBadge();
    SetStatus(L"비교 완료 — 차이 픽셀: %.2f%%", gDiffPct);
    if (hCanvas) InvalidateRect(hCanvas, NULL, FALSE);
}

/* ══════════════════════════════════════════════════════════════════════════
   렌더 함수 (RGBA 버퍼 → out)
   ══════════════════════════════════════════════════════════════════════════ */

/* ─── 공통 인라인: 픽셀 읽기 ────────────────────────────────────────────── */
static inline void ReadPixel(const Slot* s, int x, int y,
                              BYTE* r, BYTE* g, BYTE* b) {
    if (s->pixels && x < s->w && y < s->h) {
        int j = (y * s->w + x) * 4;
        *r = s->pixels[j]; *g = s->pixels[j+1]; *b = s->pixels[j+2];
    } else { *r = *g = *b = 0; }
}

/* ─── 차이 강조 적용 (픽셀 단위) ──────────────────────────────────────── */
static inline void ApplyDiff(int maskIdx, BYTE* r, BYTE* g, BYTE* b) {
    if (gMask && gMask[maskIdx]) {
        *r = gHL_R; *g = gHL_G; *b = gHL_B;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   렌더 함수
   ══════════════════════════════════════════════════════════════════════════ */

/* 나란히 보기: 좌=A, 우=B / diffOn 시 양쪽에 차이 강조 */
static void RenderSideBySide(BYTE* out, int w, int h, BOOL diffOn) {
    int W2 = w * 2;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int mi = y * w + x;

            /* 왼쪽: A (+ 차이 강조) */
            BYTE rA, gA2, bA;
            ReadPixel(&gA, x, y, &rA, &gA2, &bA);
            if (diffOn) ApplyDiff(mi, &rA, &gA2, &bA);
            int lo = (y * W2 + x) * 4;
            out[lo]   = rA; out[lo+1] = gA2; out[lo+2] = bA; out[lo+3] = 255;

            /* 오른쪽: B (+ 차이 강조) */
            BYTE rB, gBv, bB;
            ReadPixel(&gB, x, y, &rB, &gBv, &bB);
            if (diffOn) ApplyDiff(mi, &rB, &gBv, &bB);
            int ro = (y * W2 + w + x) * 4;
            out[ro]   = rB; out[ro+1] = gBv; out[ro+2] = bB; out[ro+3] = 255;
        }
    }
}

/* 오버레이: diffOn=ON → A 위에 차이 픽셀 반투명 강조
             diffOn=OFF → A 와 B 를 50% 블렌딩            */
static void RenderOverlay(BYTE* out, int w, int h, BOOL diffOn) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            BYTE rA, gAv, bA, rB, gBv, bB;
            ReadPixel(&gA, x, y, &rA, &gAv, &bA);
            ReadPixel(&gB, x, y, &rB, &gBv, &bB);

            BYTE r, g, b;
            if (diffOn) {
                /* A 베이스, 차이 픽셀에 강조색 50% 블렌딩 */
                r = rA; g = gAv; b = bA;
                if (gMask && gMask[i]) {
                    r = Clamp8((r + gHL_R) / 2);
                    g = Clamp8((g + gHL_G) / 2);
                    b = Clamp8((b + gHL_B) / 2);
                }
            } else {
                /* A 와 B 를 50% 블렌딩 */
                r = Clamp8((rA + rB) / 2);
                g = Clamp8((gAv + gBv) / 2);
                b = Clamp8((bA + bB) / 2);
            }
            out[i*4]   = r;
            out[i*4+1] = g;
            out[i*4+2] = b;
            out[i*4+3] = 255;
        }
    }
}

/* 슬라이더: 좌=A / 우=B, diffOn 시 해당 영역에 차이 강조 */
static void RenderSlider(BYTE* out, int w, int h, float sx, BOOL diffOn) {
    int split = (int)(sx * w);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            BYTE r, g, b;

            if (x == split) {
                /* 분할선: 흰색 */
                r = 255; g = 255; b = 255;
            } else if (x < split) {
                ReadPixel(&gA, x, y, &r, &g, &b);
                if (diffOn) ApplyDiff(i, &r, &g, &b);
            } else {
                ReadPixel(&gB, x, y, &r, &g, &b);
                if (diffOn) ApplyDiff(i, &r, &g, &b);
            }
            out[i*4]   = r;
            out[i*4+1] = g;
            out[i*4+2] = b;
            out[i*4+3] = 255;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   캔버스 페인트  —  더블 버퍼링으로 깜빡임 제거
   모든 합성을 백 버퍼(backDC)에서 완성한 뒤 BitBlt 한 번으로 화면에 출력
   ══════════════════════════════════════════════════════════════════════════ */
static void PaintCanvas(HWND hc, HDC hdc) {
    RECT rc;
    GetClientRect(hc, &rc);
    int canvasW = rc.right - rc.left;
    int canvasH = rc.bottom - rc.top;
    if (canvasW <= 0 || canvasH <= 0) return;

    /* ── 백 버퍼 DC 생성 (화면과 호환되는 메모리 DC) ── */
    HDC     backDC  = CreateCompatibleDC(hdc);
    HBITMAP backBmp = CreateCompatibleBitmap(hdc, canvasW, canvasH);
    HGDIOBJ backOld = SelectObject(backDC, backBmp);

    /* ── 배경을 백 버퍼에 채움 ── */
    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 18, 22));
    FillRect(backDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    /* ── 비교 전이면 안내 문자만 그리고 플러시 ── */
    if (!gCompared || !gA.loaded || !gB.loaded || cW == 0 || cH == 0) {
        SetBkMode(backDC, TRANSPARENT);
        SetTextColor(backDC, RGB(100, 100, 110));
        HFONT hf = CreateFontW(20, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Segoe UI");
        HFONT oldF = (HFONT)SelectObject(backDC, hf);
        DrawTextW(backDC, L"두 파일을 로드하고 [비교 시작]을 누르세요",
                  -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(backDC, oldF);
        DeleteObject(hf);
        goto flush;
    }

    {
        /* ── 픽셀 렌더 ── */
        int srcW = (gMode == MODE_SIDEBYSIDE) ? cW * 2 : cW;
        int srcH = cH;

        BYTE* buf = (BYTE*)malloc(srcW * srcH * 4);
        if (!buf) goto flush;

        switch (gMode) {
            case MODE_SIDEBYSIDE: RenderSideBySide(buf, cW, cH, gDiffOn);        break;
            case MODE_OVERLAY:    RenderOverlay(buf, cW, cH, gDiffOn);           break;
            case MODE_SLIDER:     RenderSlider(buf, cW, cH, gSliderX, gDiffOn);  break;
        }

        /* ── RGBA → BGRA (GDI DIB는 BGR 순서) ── */
        /* in-place 채널 스왑 (R↔B)으로 추가 malloc 불필요 */
        for (int i = 0; i < srcW * srcH; i++) {
            BYTE tmp    = buf[i*4];
            buf[i*4]    = buf[i*4+2];
            buf[i*4+2]  = tmp;
        }

        /* ── DIBSection 생성 (이미지 소스 DC) ── */
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = srcW;
        bmi.bmiHeader.biHeight      = -srcH;   /* top-down */
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC     srcDC  = CreateCompatibleDC(backDC);
        void*   pBits  = NULL;
        HBITMAP srcBmp = CreateDIBSection(backDC, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

        if (srcBmp && pBits) {
            memcpy(pBits, buf, srcW * srcH * 4);
            HGDIOBJ srcOld = SelectObject(srcDC, srcBmp);

            /* 종횡비 유지 */
            float scaleX = (float)canvasW / srcW;
            float scaleY = (float)canvasH / srcH;
            float scale  = scaleX < scaleY ? scaleX : scaleY;
            int dstW = (int)(srcW * scale);
            int dstH = (int)(srcH * scale);
            int dstX = (canvasW - dstW) / 2;
            int dstY = (canvasH - dstH) / 2;

            /* 백 버퍼에 스트레치 → 배경 위에 바로 겹침 */
            SetStretchBltMode(backDC, HALFTONE);
            StretchBlt(backDC, dstX, dstY, dstW, dstH,
                       srcDC, 0, 0, srcW, srcH, SRCCOPY);

            SelectObject(srcDC, srcOld);
            DeleteObject(srcBmp);
        }
        DeleteDC(srcDC);
        free(buf);
    }

flush:
    /* ── 백 버퍼 → 화면 (단 한 번의 BitBlt, 깜빡임 없음) ── */
    BitBlt(hdc, 0, 0, canvasW, canvasH, backDC, 0, 0, SRCCOPY);

    SelectObject(backDC, backOld);
    DeleteObject(backBmp);
    DeleteDC(backDC);
}

/* ══════════════════════════════════════════════════════════════════════════
   파일 열기 대화상자
   ══════════════════════════════════════════════════════════════════════════ */
static BOOL PickFile(HWND owner, WCHAR* outPath) {
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter =
        L"이미지 파일\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.webp\0"
        L"모든 파일\0*.*\0";
    ofn.lpstrFile   = outPath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn);
}

/* ══════════════════════════════════════════════════════════════════════════
   내보내기
   ══════════════════════════════════════════════════════════════════════════ */
static void SavePNG(void) {
    if (!gCompared || cW == 0 || cH == 0) {
        MessageBoxW(hWnd, L"먼저 비교를 실행하세요.", L"알림", MB_OK);
        return;
    }

    WCHAR path[MAX_PATH] = L"diff_result.png";
    OPENFILENAMEW sfn = {0};
    sfn.lStructSize  = sizeof(sfn);
    sfn.hwndOwner    = hWnd;
    sfn.lpstrFilter  = L"PNG 파일\0*.png\0";
    sfn.lpstrFile    = path;
    sfn.nMaxFile     = MAX_PATH;
    sfn.lpstrDefExt  = L"png";
    sfn.Flags        = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&sfn)) return;

    int srcW = (gMode == MODE_SIDEBYSIDE) ? cW * 2 : cW;
    BYTE* buf = (BYTE*)malloc(srcW * cH * 4);
    if (!buf) return;

    switch (gMode) {
        case MODE_SIDEBYSIDE: RenderSideBySide(buf, cW, cH, gDiffOn);        break;
        case MODE_OVERLAY:    RenderOverlay(buf, cW, cH, gDiffOn);           break;
        case MODE_SLIDER:     RenderSlider(buf, cW, cH, gSliderX, gDiffOn);  break;
    }

    char utf8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), NULL, NULL);
    stbi_write_png(utf8, srcW, cH, 4, buf, srcW * 4);
    free(buf);

    SetStatus(L"PNG 저장 완료: %s", path);
}

static void SaveCSV(void) {
    if (gLogLen == 0) {
        MessageBoxW(hWnd, L"비교 기록이 없습니다.", L"알림", MB_OK);
        return;
    }

    WCHAR path[MAX_PATH] = L"diff_report.csv";
    OPENFILENAMEW sfn = {0};
    sfn.lStructSize  = sizeof(sfn);
    sfn.hwndOwner    = hWnd;
    sfn.lpstrFilter  = L"CSV 파일\0*.csv\0";
    sfn.lpstrFile    = path;
    sfn.nMaxFile     = MAX_PATH;
    sfn.lpstrDefExt  = L"csv";
    sfn.Flags        = OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&sfn)) return;

    FILE* f = _wfopen(path, L"w");
    if (!f) { SetStatus(L"CSV 저장 실패"); return; }
    fprintf(f, "index,diffPct\n");
    for (int i = 0; i < gLogLen; i++)
        fprintf(f, "%d,%.4f\n", gLog[i].idx, gLog[i].pct);
    fclose(f);
    SetStatus(L"CSV 저장 완료: %s  (%d 행)", path, gLogLen);
}

/* ══════════════════════════════════════════════════════════════════════════
   컨트롤 생성
   ══════════════════════════════════════════════════════════════════════════ */
static void CreateControls(HWND hw, HINSTANCE hi) {
    HFONT hfUI = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* ── 드롭존 A ───────────────────────────── */
    hZoneA = CreateWindowExW(WS_EX_ACCEPTFILES, ZONE_CLS, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_LOAD_A, hi, NULL);
    {
        ZoneData* zd = (ZoneData*)calloc(1, sizeof(ZoneData));
        zd->slotId = 0;
        SetWindowLongPtrW(hZoneA, GWLP_USERDATA, (LONG_PTR)zd);
        DragAcceptFiles(hZoneA, TRUE);
    }

    /* ── 드롭존 B ───────────────────────────── */
    hZoneB = CreateWindowExW(WS_EX_ACCEPTFILES, ZONE_CLS, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_LOAD_B, hi, NULL);
    {
        ZoneData* zd = (ZoneData*)calloc(1, sizeof(ZoneData));
        zd->slotId = 1;
        SetWindowLongPtrW(hZoneB, GWLP_USERDATA, (LONG_PTR)zd);
        DragAcceptFiles(hZoneB, TRUE);
    }

    /* ── 비교 버튼 ──────────────────────────── */
    hBtnCompare = CreateWindowExW(0, L"BUTTON", L"비교 시작",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_COMPARE, hi, NULL);

    /* ── 툴바: 차이 토글 + 구분선 + 뷰 모드 3개 ── */
    hBtnDiffToggle = CreateWindowExW(0, L"BUTTON", L"차이 ON",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_DIFF_TOGGLE, hi, NULL);
    SendMessage(hBtnDiffToggle, WM_SETFONT, (WPARAM)hfUI, TRUE);

    const WCHAR* modeLabels[3] = { L"나란히 보기", L"오버레이", L"슬라이더" };
    int modeIds[3] = { IDC_BTN_SIDE, IDC_BTN_OVERLAY, IDC_BTN_SLIDER };
    for (int i = 0; i < 3; i++) {
        hModeBtn[i] = CreateWindowExW(0, L"BUTTON", modeLabels[i],
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hw, (HMENU)(UINT_PTR)modeIds[i], hi, NULL);
        SendMessage(hModeBtn[i], WM_SETFONT, (WPARAM)hfUI, TRUE);
    }
    /* 초기 선택: 나란히 보기 */
    SendMessageW(hModeBtn[0], BM_SETSTATE, TRUE, 0);

    hThreshLabel = CreateWindowExW(0, L"STATIC", L"임계값: 10",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hw, (HMENU)IDC_LABEL_THRESH, hi, NULL);

    hThreshSlider = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        0, 0, 0, 0, hw, (HMENU)IDC_SLIDER_THRESH, hi, NULL);
    SendMessageW(hThreshSlider, TBM_SETRANGE, FALSE, MAKELONG(0, 255));
    SendMessageW(hThreshSlider, TBM_SETPOS, TRUE, gThresh);

    hBtnColor = CreateWindowExW(0, L"BUTTON", L"강조색",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_COLOR, hi, NULL);

    hBtnSavePNG = CreateWindowExW(0, L"BUTTON", L"PNG",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_SAVE_PNG, hi, NULL);

    hBtnSaveCSV = CreateWindowExW(0, L"BUTTON", L"CSV",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hw, (HMENU)IDC_BTN_SAVE_CSV, hi, NULL);

    /* ── 캔버스 ───────────────────────────────── */
    hCanvas = CreateWindowExW(WS_EX_CLIENTEDGE, CANVAS_CLS, NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, hw, (HMENU)IDC_CANVAS, hi, NULL);
    DragAcceptFiles(hCanvas, TRUE);

    /* ── 상태/배지 ────────────────────────────── */
    hStatus = CreateWindowExW(0, L"STATIC", L"두 파일을 로드하세요",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 0, 0, hw, (HMENU)IDC_STATUS, hi, NULL);

    hBadge = CreateWindowExW(0, L"STATIC", L"—",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, 0, 0, 0, hw, (HMENU)IDC_DIFF_BADGE, hi, NULL);

    /* 폰트 일괄 적용 */
    HWND ctrls[] = { hBtnCompare,
                     hThreshLabel, hBtnColor,
                     hBtnSavePNG, hBtnSaveCSV,
                     hStatus, hBadge };
    for (int i = 0; i < (int)(sizeof(ctrls)/sizeof(ctrls[0])); i++)
        SendMessage(ctrls[i], WM_SETFONT, (WPARAM)hfUI, TRUE);
}

/* ══════════════════════════════════════════════════════════════════════════
   레이아웃 재배치 (WM_SIZE 시)
   ══════════════════════════════════════════════════════════════════════════ */
static void RelayoutControls(HWND hw) {
    RECT rc;
    GetClientRect(hw, &rc);
    int W = rc.right;

    int pad = 8;
    int y0  = pad;

    /* ── 상단 바: 드롭존 A / 비교 버튼 / 드롭존 B ── */
    int zoneW  = (W - pad * 4 - 120) / 2;  /* 존 너비 = 나머지 반씩 */
    if (zoneW < 160) zoneW = 160;
    int centerX = W / 2;

    MoveWindow(hZoneA,      pad,                     y0, zoneW, ZONE_H, TRUE);
    MoveWindow(hBtnCompare, centerX - 55,            y0 + (ZONE_H - BTN_H)/2, 110, BTN_H, TRUE);
    MoveWindow(hZoneB,      W - pad - zoneW,         y0, zoneW, ZONE_H, TRUE);

    y0 += ZONE_H + pad;

    /* ── 툴바 ─────────────────────────────────── */
    int tx = pad;
    int th = SMALL_H;
    int ty = y0 + (TOOLBAR_H - th) / 2;

    /* 차이 토글 버튼 (강조 배경 표시를 위해 약간 넓게) */
    MoveWindow(hBtnDiffToggle, tx, ty, 72, th, TRUE); tx += 76;

    /* 수직 구분선 역할: 약간의 공백 */
    tx += 6;

    /* 뷰 모드 3개 */
    for (int i = 0; i < 3; i++) {
        MoveWindow(hModeBtn[i], tx, ty, 84, th, TRUE);
        tx += 86;
    }
    tx += 8;

    MoveWindow(hThreshLabel,  tx, ty,     80, th, TRUE); tx += 82;
    MoveWindow(hThreshSlider, tx, ty,    120, th, TRUE); tx += 124;
    MoveWindow(hBtnColor,     tx, ty,     64, th, TRUE); tx += 68;

    /* 우측 정렬: PNG CSV 배지 */
    MoveWindow(hBtnSaveCSV,  W - pad - 48,              ty, 46, th, TRUE);
    MoveWindow(hBtnSavePNG,  W - pad - 48 - 50,         ty, 46, th, TRUE);
    MoveWindow(hBadge,       W - pad - 48 - 50 - 74,    ty, 70, th, TRUE);

    y0 += TOOLBAR_H;

    /* ── 캔버스 ───────────────────────────────── */
    int canvasH = rc.bottom - y0 - STATUS_H - pad;
    MoveWindow(hCanvas, 0, y0, W, canvasH, TRUE);
    y0 += canvasH;

    /* ── 상태바 ───────────────────────────────── */
    MoveWindow(hStatus, pad, y0 + 4, W - pad * 2, STATUS_H - 4, TRUE);
}

/* ══════════════════════════════════════════════════════════════════════════
   캔버스 WndProc
   ══════════════════════════════════════════════════════════════════════════ */
LRESULT CALLBACK CanvasProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        PaintCanvas(hw, hdc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;  /* 깜빡임 방지 */

    case WM_LBUTTONDOWN:
        if (gMode == MODE_SLIDER && gCompared) {
            RECT rc; GetClientRect(hw, &rc);
            gSliderX = (float)LOWORD(lp) / rc.right;
            if (gSliderX < 0.f) gSliderX = 0.f;
            if (gSliderX > 1.f) gSliderX = 1.f;
            gSliderDrag = TRUE;
            SetCapture(hw);
            InvalidateRect(hw, NULL, FALSE);
        }
        break;

    case WM_MOUSEMOVE:
        if (gSliderDrag) {
            RECT rc; GetClientRect(hw, &rc);
            gSliderX = (float)LOWORD(lp) / rc.right;
            if (gSliderX < 0.f) gSliderX = 0.f;
            if (gSliderX > 1.f) gSliderX = 1.f;
            InvalidateRect(hw, NULL, FALSE);
        }
        break;

    case WM_LBUTTONUP:
        if (gSliderDrag) { gSliderDrag = FALSE; ReleaseCapture(); }
        break;

    /* 캔버스로 드래그&드롭 — 두 파일을 한 번에 A/B 자동 배정 */
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT cnt = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        WCHAR p[MAX_PATH];
        for (UINT i = 0; i < cnt && i < 2; i++) {
            DragQueryFileW(hd, i, p, MAX_PATH);
            if (i == 0) { LoadSlot(&gA, p); InvalidateRect(hZoneA, NULL, FALSE); }
            else        { LoadSlot(&gB, p); InvalidateRect(hZoneB, NULL, FALSE); }
        }
        DragFinish(hd);
        EnableWindow(hBtnCompare, gA.loaded && gB.loaded);
        break;
    }
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ══════════════════════════════════════════════════════════════════════════
   메인 WndProc
   ══════════════════════════════════════════════════════════════════════════ */
LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hw, ((CREATESTRUCT*)lp)->hInstance);
        DragAcceptFiles(hw, TRUE);
        return 0;

    case WM_SIZE:
        RelayoutControls(hw);
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        switch (id) {
        case IDC_BTN_COMPARE:
            DoCompare();
            break;

        case IDC_BTN_DIFF_TOGGLE: SetDiffToggle(!gDiffOn);      break;
        case IDC_BTN_SIDE:        SetMode(MODE_SIDEBYSIDE);    break;
        case IDC_BTN_OVERLAY:     SetMode(MODE_OVERLAY);       break;
        case IDC_BTN_SLIDER:      SetMode(MODE_SLIDER);        break;

        case IDC_BTN_COLOR: {
            static COLORREF customs[16] = {0};
            CHOOSECOLORW cc = {0};
            cc.lStructSize  = sizeof(cc);
            cc.hwndOwner    = hw;
            cc.rgbResult    = RGB(gHL_R, gHL_G, gHL_B);
            cc.lpCustColors = customs;
            cc.Flags        = CC_RGBINIT | CC_FULLOPEN;
            if (ChooseColorW(&cc)) {
                gHL_R = GetRValue(cc.rgbResult);
                gHL_G = GetGValue(cc.rgbResult);
                gHL_B = GetBValue(cc.rgbResult);
                if (gCompared && hCanvas) InvalidateRect(hCanvas, NULL, FALSE);
            }
            break;
        }
        case IDC_BTN_SAVE_PNG: SavePNG(); break;
        case IDC_BTN_SAVE_CSV: SaveCSV(); break;
        }
        return 0;
    }

    case WM_HSCROLL: {
        /* 임계값 슬라이더 */
        if ((HWND)lp == hThreshSlider) {
            gThresh = (int)SendMessageW(hThreshSlider, TBM_GETPOS, 0, 0);
            WCHAR buf[32];
            _snwprintf(buf, 32, L"임계값: %d", gThresh);
            SetWindowTextW(hThreshLabel, buf);
            /* 임계값 변경 시 자동 재비교 */
            if (gA.loaded && gB.loaded) DoCompare();
        }
        return 0;
    }

    /* 메인 창으로 드래그&드롭 (두 파일 동시) */
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT cnt = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
        WCHAR p[MAX_PATH];
        if (cnt >= 1) {
            DragQueryFileW(hd, 0, p, MAX_PATH);
            LoadSlot(&gA, p);
            InvalidateRect(hZoneA, NULL, FALSE);
        }
        if (cnt >= 2) {
            DragQueryFileW(hd, 1, p, MAX_PATH);
            LoadSlot(&gB, p);
            InvalidateRect(hZoneB, NULL, FALSE);
        }
        DragFinish(hd);
        EnableWindow(hBtnCompare, gA.loaded && gB.loaded);
        return 0;
    }

    case WM_DESTROY:
        FreeSlot(&gA);
        FreeSlot(&gB);
        free(gMask);
        free(gLog);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hw, msg, wp, lp);
}

/* ══════════════════════════════════════════════════════════════════════════
   WinMain
   ══════════════════════════════════════════════════════════════════════════ */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow) {
    (void)hPrev; (void)cmdLine;

    /* 공통 컨트롤 초기화 (Trackbar 포함) */
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    /* 드롭존 윈도우 클래스 등록 */
    WNDCLASSEXW wzc = {0};
    wzc.cbSize        = sizeof(wzc);
    wzc.lpfnWndProc   = ZoneProc;
    wzc.hInstance     = hInst;
    wzc.hCursor       = LoadCursor(NULL, IDC_HAND);
    wzc.hbrBackground = NULL;
    wzc.lpszClassName = ZONE_CLS;
    RegisterClassExW(&wzc);

    /* 캔버스 윈도우 클래스 등록 */
    WNDCLASSEXW wcc = {0};
    wcc.cbSize        = sizeof(wcc);
    wcc.lpfnWndProc   = CanvasProc;
    wcc.hInstance     = hInst;
    wcc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcc.lpszClassName = CANVAS_CLS;
    RegisterClassExW(&wcc);

    /* 메인 윈도우 클래스 등록 */
    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"VideoDiffMain";
    RegisterClassExW(&wc);

    /* 메인 윈도우 생성 */
    hWnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"VideoDiffMain", APP_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        NULL, NULL, hInst, NULL);
    if (!hWnd) return 1;

    ShowWindow(hWnd, nShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
