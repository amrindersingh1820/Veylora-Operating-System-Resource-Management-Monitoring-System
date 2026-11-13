#include "gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  Process *p;
  int ok, done;
  HWND ed[5];              /* name, arrival, burst, priority, memory */
  HWND mx[NRES], hd[NRES]; /* max need, currently held */
} Dlg;

static HWND child(HWND parent, const char *cls, const char *text, DWORD style,
                  DWORD ex, int x, int y, int w, int h, int id) {
  HWND c =
      CreateWindowExA(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                      parent, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
  SendMessageA(c, WM_SETFONT, (WPARAM)g.font, TRUE);
  return c;
}

static HWND edit(HWND parent, int x, int y, int w, int v, int is_text,
                 const char *txt) {
  char b[48];
  if (is_text)
    snprintf(b, sizeof b, "%s", txt);
  else
    snprintf(b, sizeof b, "%d", v);
  return child(parent, "EDIT", b,
               WS_TABSTOP | ES_AUTOHSCROLL | (is_text ? 0 : ES_NUMBER),
               WS_EX_CLIENTEDGE, x, y, w, 22, 0);
}

static int read_int(HWND dlg, HWND ed, const char *what, int lo, int hi,
                    int *out) {
  char b[32];
  GetWindowTextA(ed, b, sizeof b);
  char *end;
  long v = strtol(b, &end, 10);
  if (b[0] == 0 || *end != 0 || v < lo || v > hi) {
    char m[160];
    snprintf(m, sizeof m, "%s must be a whole number from %d to %d.", what, lo,
             hi);
    MessageBoxA(dlg, m, "Check the value", MB_ICONWARNING);
    SetFocus(ed);
    return 0;
  }
  *out = (int)v;
  return 1;
}

static int collect(HWND h, Dlg *d) {
  Process p = *d->p;
  char name[NAME_LEN + 8];
  GetWindowTextA(d->ed[0], name, sizeof name);
  char *s = name;
  while (*s == ' ')
    s++;
  size_t len = strlen(s);
  while (len && s[len - 1] == ' ')
    s[--len] = 0;
  if (len == 0 || len >= NAME_LEN) {
    MessageBoxA(h, "Enter a process name (1 to 31 characters).",
                "Check the value", MB_ICONWARNING);
    SetFocus(d->ed[0]);
    return 0;
  }
  snprintf(p.name, sizeof p.name, "%s", s);
  if (!read_int(h, d->ed[1], "Arrival time", 0, 500, &p.arrival))
    return 0;
  if (!read_int(h, d->ed[2], "Burst time", 1, 200, &p.burst))
    return 0;
  if (!read_int(h, d->ed[3], "Priority", 1, 99, &p.priority))
    return 0;
  if (!read_int(h, d->ed[4], "Memory size", 1, 100000, &p.mem_kb))
    return 0;
  for (int j = 0; j < NRES; j++) {
    char lab[48];
    snprintf(lab, sizeof lab, "Maximum %s", RES_NAMES[j]);
    if (!read_int(h, d->mx[j], lab, 0, 1000, &p.max[j]))
      return 0;
    snprintf(lab, sizeof lab, "Held %s", RES_NAMES[j]);
    if (!read_int(h, d->hd[j], lab, 0, 1000, &p.held[j]))
      return 0;
    if (p.held[j] > p.max[j]) {
      char m[160];
      snprintf(m, sizeof m,
               "Held %s (%d) cannot be more than its maximum need (%d).",
               RES_NAMES[j], p.held[j], p.max[j]);
      MessageBoxA(h, m, "Check the value", MB_ICONWARNING);
      SetFocus(d->hd[j]);
      return 0;
    }
  }
  *d->p = p;
  return 1;
}

static LRESULT CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Dlg *d = (Dlg *)GetWindowLongPtrA(h, GWLP_USERDATA);
  switch (m) {
  case WM_CREATE: {
    d = (Dlg *)((CREATESTRUCTA *)l)->lpCreateParams;
    SetWindowLongPtrA(h, GWLP_USERDATA, (LONG_PTR)d);
    const Process *p = d->p;
    child(h, "STATIC", "Process name", 0, 0, 14, 17, 130, 18, 0);
    child(h, "STATIC", "Arrival time (0-500)", 0, 0, 14, 47, 130, 18, 0);
    child(h, "STATIC", "Burst time (1-200)", 0, 0, 14, 77, 130, 18, 0);
    child(h, "STATIC", "Priority (1 = highest)", 0, 0, 14, 107, 130, 18, 0);
    child(h, "STATIC", "Memory needed (KB)", 0, 0, 14, 137, 130, 18, 0);
    d->ed[0] = edit(h, 150, 14, 150, 0, 1, p->name);
    d->ed[1] = edit(h, 150, 44, 70, p->arrival, 0, NULL);
    d->ed[2] = edit(h, 150, 74, 70, p->burst, 0, NULL);
    d->ed[3] = edit(h, 150, 104, 70, p->priority, 0, NULL);
    d->ed[4] = edit(h, 150, 134, 70, p->mem_kb, 0, NULL);
    child(h, "STATIC", "Resource", 0, 0, 330, 17, 60, 18, 0);
    child(h, "STATIC", "Max need", 0, 0, 408, 17, 60, 18, 0);
    child(h, "STATIC", "Held now", 0, 0, 478, 17, 60, 18, 0);
    for (int j = 0; j < NRES; j++) {
      child(h, "STATIC", RES_NAMES[j], 0, 0, 330, 47 + 30 * j, 70, 18, 0);
      d->mx[j] = edit(h, 408, 44 + 30 * j, 55, p->max[j], 0, NULL);
      d->hd[j] = edit(h, 478, 44 + 30 * j, 55, p->held[j], 0, NULL);
    }
    child(h, "BUTTON", "OK", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 360, 176, 80, 28,
          IDOK);
    child(h, "BUTTON", "Cancel", WS_TABSTOP | BS_PUSHBUTTON, 0, 452, 176, 80,
          28, IDCANCEL);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(w) == IDOK) {
      if (collect(h, d)) {
        d->ok = 1;
        d->done = 1;
      }
    } else if (LOWORD(w) == IDCANCEL)
      d->done = 1;
    return 0;
  case WM_CLOSE:
    d->done = 1;
    return 0;
  }
  return DefWindowProcA(h, m, w, l);
}

int dialog_process(HWND owner, Process *p, int editing) {
  static int registered = 0;
  if (!registered) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DlgProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "VOSProcDlg";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);
    registered = 1;
  }
  Dlg d;
  memset(&d, 0, sizeof d);
  d.p = p;
  RECT r = {0, 0, 548, 218};
  DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  AdjustWindowRect(&r, style, FALSE);
  RECT o;
  GetWindowRect(owner, &o);
  int W = r.right - r.left, H = r.bottom - r.top;
  int x = o.left + ((o.right - o.left) - W) / 2,
      y = o.top + ((o.bottom - o.top) - H) / 2;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  HWND h = CreateWindowExA(WS_EX_DLGMODALFRAME, "VOSProcDlg",
                           editing ? "Edit process" : "Add process", style, x,
                           y, W, H, owner, NULL, GetModuleHandle(NULL), &d);
  if (!h)
    return 0;
  EnableWindow(owner, FALSE);
  ShowWindow(h, SW_SHOW);
  SetFocus(d.ed[0]);
  SendMessageA(d.ed[0], EM_SETSEL, 0, -1);
  MSG msg;
  while (!d.done && GetMessageA(&msg, NULL, 0, 0) > 0) {
    if (!IsDialogMessageA(h, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
  }
  EnableWindow(owner, TRUE);
  DestroyWindow(h);
  SetForegroundWindow(owner);
  return d.ok;
}
