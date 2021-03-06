/*
 * Copyright © 2009 CNRS
 * Copyright © 2009-2016 Inria.  All rights reserved.
 * Copyright © 2009-2010, 2012 Université Bordeaux
 * Copyright © 2011 Cisco Systems, Inc.  All rights reserved.
 * See COPYING in top-level directory.
 */

#include <private/autogen/config.h>
#include <hwloc.h>

#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>

#include <windows.h>
#include <windowsx.h>

#include "lstopo.h"

/* windows back-end.  */

static struct color {
  int r, g, b;
  HGDIOBJ brush;
} *colors;

struct lstopo_windows_output {
  struct lstopo_output loutput; /* must be at the beginning */
  PAINTSTRUCT ps;
  HWND toplevel;
};

static int numcolors;

static HGDIOBJ
rgb_to_brush(int r, int g, int b)
{
  int i;

  for (i = 0; i < numcolors; i++)
    if (colors[i].r == r && colors[i].g == g && colors[i].b == b)
      return colors[i].brush;

  fprintf(stderr, "color #%02x%02x%02x not declared\n", r, g, b);
  exit(EXIT_FAILURE);
}

struct draw_methods windows_draw_methods;

static struct lstopo_windows_output the_output;
static int state, control;
static int x, y, x_delta, y_delta;
static int finish;
static int the_width, the_height;
static int win_width, win_height;
static unsigned int the_fontsize, the_gridsize;
static float the_scale;

static void
windows_box(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned width, unsigned y, unsigned height);

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
  int redraw = 0;
  switch (message) {
    case WM_CHAR:  {
      switch (wparam) {
      case '+':
	the_scale *= 1.2f;
	redraw = 1;
	break;
      case '-':
	the_scale /= 1.2f;
	redraw = 1;
	break;
      case 'f':
      case 'F': {
	float wscale, hscale;
	wscale = win_width / (float)the_width;
	hscale = win_height / (float)the_height;
	the_scale *= wscale > hscale ? hscale : wscale;
	redraw = 1;
	break;
      }
      case '1':
	the_scale = 1.0;
	redraw = 1;
	break;
      case 'q':
      case 'Q':
	finish = 1;
	break;
      }
      break;
    }

    case WM_PAINT: {
      HFONT font;
      BeginPaint(hwnd, &the_output.ps);
      font = CreateFont(the_output.loutput.fontsize, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, NULL);
      SelectObject(the_output.ps.hdc, (HGDIOBJ) font);
      windows_box(&the_output, 0xff, 0xff, 0xff, 0, 0, win_width, 0, win_height);
      the_output.loutput.drawing = LSTOPO_DRAWING_PREPARE;
      output_draw(&the_output.loutput);
      the_width = the_output.loutput.width;
      the_height = the_output.loutput.height;
      the_output.loutput.drawing = LSTOPO_DRAWING_DRAW;
      output_draw(&the_output.loutput);
      DeleteObject(font);
      EndPaint(hwnd, &the_output.ps);
      break;
    }
    case WM_LBUTTONDOWN:
      state = 1;
      x = GET_X_LPARAM(lparam);
      y = GET_Y_LPARAM(lparam);
      break;
    case WM_LBUTTONUP:
      state = 0;
      break;
    case WM_MOUSEMOVE:
      if (!(wparam & MK_LBUTTON))
        state = 0;
      if (state) {
        int new_x = GET_X_LPARAM(lparam);
        int new_y = GET_Y_LPARAM(lparam);
        x_delta -= new_x - x;
        y_delta -= new_y - y;
        x = new_x;
        y = new_y;
        redraw = 1;
      }
      break;
    case WM_KEYDOWN:
      switch (wparam) {
      case VK_ESCAPE:
        finish = 1;
        break;
      case VK_LEFT:
        x_delta -= win_width/10;
        redraw = 1;
        break;
      case VK_RIGHT:
        x_delta += win_width/10;
        redraw = 1;
        break;
      case VK_UP:
        y_delta -= win_height/10;
        redraw = 1;
        break;
      case VK_DOWN:
        y_delta += win_height/10;
        redraw = 1;
        break;
      case VK_PRIOR:
        if (control) {
          x_delta -= win_width;
          redraw = 1;
        } else {
          y_delta -= win_height;
          redraw = 1;
        }
        break;
      case VK_NEXT:
        if (control) {
          x_delta += win_width;
          redraw = 1;
        } else {
          y_delta += win_height;
          redraw = 1;
        }
        break;
      case VK_HOME:
        x_delta = 0;
        y_delta = 0;
        redraw = 1;
        break;
      case VK_END:
        x_delta = INT_MAX;
        y_delta = INT_MAX;
        redraw = 1;
        break;
      case VK_CONTROL:
        control = 1;
        break;
      }
      break;
    case WM_KEYUP:
      switch (wparam) {
      case VK_CONTROL:
        control = 0;
        break;
      }
      break;
    case WM_DESTROY:
      /* only kill the program if closing the actual toplevel, not the fake one */
      if (hwnd == the_output.toplevel)
	PostQuitMessage(0);
      return 0;
    case WM_SIZE: {
      float wscale, hscale;
      win_width = LOWORD(lparam);
      win_height = HIWORD(lparam);
      wscale = win_width / (float)the_width;
      hscale = win_height / (float)the_height;
      the_scale *= wscale > hscale ? hscale : wscale;
      if (the_scale < 1.0f)
	the_scale = 1.0f;
      redraw = 1;
      break;
    }
  }
  if (redraw) {
    if (x_delta > the_width - win_width)
      x_delta = the_width - win_width;
    if (y_delta > the_height - win_height)
      y_delta = the_height - win_height;
    if (x_delta < 0)
      x_delta = 0;
    if (y_delta < 0)
      y_delta = 0;
    the_output.loutput.fontsize = (unsigned)(the_fontsize * the_scale);
    the_output.loutput.gridsize = (unsigned)(the_gridsize * the_scale);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE);
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

static void
windows_init(void *output)
{
  struct lstopo_windows_output *woutput = output;
  WNDCLASS wndclass;
  HWND toplevel, faketoplevel;
  unsigned width, height;
  HFONT font;

  /* make sure WM_DESTROY on the faketoplevel won't kill the program */
  woutput->toplevel = NULL;

  /* create the toplevel window, with random size for now */
  memset(&wndclass, 0, sizeof(wndclass));
  wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
  wndclass.hCursor = LoadCursor(NULL, IDC_SIZEALL);
  wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wndclass.lpfnWndProc = WndProc;
  wndclass.lpszClassName = "lstopo";

  RegisterClass(&wndclass);

  /* recurse once for preparing sizes and positions using a fake top level window */
  woutput->loutput.drawing = LSTOPO_DRAWING_PREPARE;
  faketoplevel = CreateWindow("lstopo", "lstopo", WS_OVERLAPPEDWINDOW,
			      CW_USEDEFAULT, CW_USEDEFAULT,
			      10, 10, NULL, NULL, NULL, NULL);
  BeginPaint(faketoplevel, &woutput->ps);
  font = CreateFont(woutput->loutput.fontsize, 0, 0, 0, 0, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, NULL);
  SelectObject(woutput->ps.hdc, (HGDIOBJ) font);
  output_draw(&woutput->loutput);
  DeleteObject(font);
  EndPaint(faketoplevel, &woutput->ps);
  DestroyWindow(faketoplevel);
  woutput->loutput.drawing = LSTOPO_DRAWING_DRAW;

  /* now create the actual toplevel with the sizes */
  width = woutput->loutput.width;
  height = woutput->loutput.height;

  win_width = width + 2*GetSystemMetrics(SM_CXSIZEFRAME);
  win_height = height + 2*GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CYCAPTION);

  if (win_width > GetSystemMetrics(SM_CXFULLSCREEN))
    win_width = GetSystemMetrics(SM_CXFULLSCREEN);

  if (win_height > GetSystemMetrics(SM_CYFULLSCREEN))
    win_height = GetSystemMetrics(SM_CYFULLSCREEN);

  toplevel = CreateWindow("lstopo", "lstopo", WS_OVERLAPPEDWINDOW,
		  CW_USEDEFAULT, CW_USEDEFAULT,
		  win_width, win_height, NULL, NULL, NULL, NULL);
  woutput->toplevel = toplevel;

  the_width = width;
  the_height = height;

  the_scale = 1.0f;

  the_fontsize = woutput->loutput.fontsize;
  the_gridsize = woutput->loutput.gridsize;

  /* and display the window */
  ShowWindow(toplevel, SW_SHOWDEFAULT);

  printf("\n");
  printf("Keyboard shortcuts:\n");
  printf(" Zoom-in or out .................... + -\n");
  printf(" Try to fit scale to window ........ f F\n");
  printf(" Reset scale to default ............ 1\n");
  printf(" Scroll vertically ................. Up Down PageUp PageDown\n");
  printf(" Scroll horizontally ............... Left Right Ctrl+PageUp/Down\n");
  printf(" Scroll to the top-left corner ..... Home\n");
  printf(" Scroll to the bottom-right corner . End\n");
  printf(" Exit .............................. q Q Esc\n");
  printf("\n\n");
}

static void
windows_declare_color(void *output __hwloc_attribute_unused, int r, int g, int b)
{
  HBRUSH brush;
  COLORREF color;
  struct color *tmp;

  color = RGB(r, g, b);
  brush = CreateSolidBrush(color);
  if (!brush) {
    fprintf(stderr,"Could not allocate color %02x%02x%02x\n", r, g, b);
    exit(EXIT_FAILURE);
  }

  tmp = realloc(colors, sizeof(*colors) * (numcolors + 1));
  if (!tmp) {
    fprintf(stderr, "Failed to realloc the colors array\n");
    return;
  }
  colors = tmp;
  colors[numcolors].r = r;
  colors[numcolors].g = g;
  colors[numcolors].b = b;
  colors[numcolors].brush = (HGDIOBJ) brush;
  numcolors++;
}

static void
windows_box(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned width, unsigned y, unsigned height)
{
  struct lstopo_windows_output *woutput = output;
  PAINTSTRUCT *ps = &woutput->ps;

  SelectObject(ps->hdc, rgb_to_brush(r, g, b));
  SetBkColor(ps->hdc, RGB(r, g, b));
  Rectangle(ps->hdc, x - x_delta, y - y_delta, x + width - x_delta, y + height - y_delta);
}

static void
windows_line(void *output, int r, int g, int b, unsigned depth __hwloc_attribute_unused, unsigned x1, unsigned y1, unsigned x2, unsigned y2)
{
  struct lstopo_windows_output *woutput = output;
  PAINTSTRUCT *ps = &woutput->ps;

  SelectObject(ps->hdc, rgb_to_brush(r, g, b));
  MoveToEx(ps->hdc, x1 - x_delta, y1 - y_delta, NULL);
  LineTo(ps->hdc, x2 - x_delta, y2 - y_delta);
}

static void
windows_text(void *output, int r, int g, int b, int size __hwloc_attribute_unused, unsigned depth __hwloc_attribute_unused, unsigned x, unsigned y, const char *text)
{
  struct lstopo_windows_output *woutput = output;
  PAINTSTRUCT *ps = &woutput->ps;

  SetTextColor(ps->hdc, RGB(r, g, b));
  TextOut(ps->hdc, x - x_delta, y - y_delta, text, (int)strlen(text));
}

static void
windows_textsize(void *output, const char *text, unsigned textlength, unsigned fontsize __hwloc_attribute_unused, unsigned *width)
{
  struct lstopo_windows_output *woutput = output;
  PAINTSTRUCT *ps = &woutput->ps;
  SIZE size;

  GetTextExtentPoint32(ps->hdc, text, textlength, &size);
  *width = size.cx;
}

struct draw_methods windows_draw_methods = {
  windows_init,
  windows_declare_color,
  windows_box,
  windows_line,
  windows_text,
  windows_textsize,
};

void
output_windows (struct lstopo_output *loutput)
{
  MSG msg;

  memset(&the_output, 0, sizeof(the_output));
  memcpy(&the_output.loutput, loutput, sizeof(*loutput));
  the_output.loutput.methods = &windows_draw_methods;

  output_draw_start(&the_output.loutput);
  UpdateWindow(the_output.toplevel);
  while (!finish && GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}
