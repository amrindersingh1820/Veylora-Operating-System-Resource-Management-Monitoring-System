/* Shared declarations for the Win32 front end. */
#ifndef VOS_GUI_H
#define VOS_GUI_H
#include <windows.h>
#include <commctrl.h>
#include "core/types.h"

enum {
    ID_RUN = 100, ID_COMPARE, ID_ADD, ID_EDIT, ID_DEL, ID_ALL, ID_NONE, ID_SAMPLE,
    ID_H_VIEW, ID_H_DEL, ID_H_REF,
    ID_LV_PROC = 200, ID_LV_RESULT, ID_LV_MEM, ID_LV_RES, ID_LV_CMP, ID_LV_HIST,
    ID_TAB, ID_STATUS, ID_CB_ALGO, ID_ED_Q, ID_CB_MEM, ID_ED_BLOCKS, ID_ED_TOTALS, ID_CHK_SAVE
};

#define NPAGES 5      /* Scheduling, Memory, Resources, Compare, History */

typedef struct {
    HWND hwnd, status, grp;
    HFONT font;
    /* settings panel */
    HWND cbAlgo, edQuantum, cbMem, edBlocks, edTotals, chkSave, btnRun, btnCompare;
    /* process list */
    HWND stProc, lvProc, btnAdd, btnEdit, btnDel, btnAll, btnNone, btnSample;
    /* tabs and pages */
    HWND tab, page[NPAGES];
    HWND stSched, gantt, lvResult;          /* page 0 */
    HWND stMem, memmap, lvMem;              /* page 1 */
    HWND stRes, lvRes;                      /* page 2 */
    HWND stCmp, lvCmp;                      /* page 3 */
    HWND btnHView, btnHDel, btnHRef, lvHist;/* page 4 */
    /* data */
    Process procs[MAX_PROC];
    int nprocs;
    int populating;                         /* ignore list notifications while refilling */
    RunBundle *cur;                         /* what the result tabs currently show */
} App;

extern App g;

/* widgets.c */
void     widgets_register(HINSTANCE hi);
COLORREF pid_color(int pid);
/* views.c */
HWND lv_create(HWND parent, int id, DWORD ex);
void lv_col(HWND lv, int i, const char *text, int w, int right);
int  lv_add(HWND lv, const char *text, LPARAM param);
void lv_set(HWND lv, int row, int col, const char *text);
void views_init_columns(void);
void views_refresh_processes(void);
int  views_get_selected(Process *out);       /* ticked processes, in list order */
void views_show_bundle(const RunBundle *b);
void views_show_empty(void);
void views_fill_compare(const SchedResult *rs, int n, int auto_idx, const char *auto_reason, int nprocs);
void views_refresh_history(void);
int  views_selected_run_id(void);            /* 0 if none */
/* dialogs.c */
int  dialog_process(HWND owner, Process *p, int editing);   /* 1 = OK pressed */
#endif
