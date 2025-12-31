#ifndef APP_H
#define APP_H

#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

// Global Application Structure
typedef struct {
  GtkApplication *app;
  GtkWidget *window;
  GtkWidget *stack; // To switch between views
  GtkWidget *menu_view;
  GtkWidget *sorting_view;
  GtkWidget *list_view;
  GtkWidget *tree_view;
  GtkWidget *graph_view;
} AppContext;

// Function Prototypes

// menu_view.c
GtkWidget *create_menu_view(AppContext *app_ctx);

// sorting_view.c
GtkWidget *create_sorting_view(AppContext *app_ctx);

// list_view.c
GtkWidget *create_list_view(AppContext *app_ctx);

// tree_view.c
GtkWidget *create_tree_view(AppContext *app_ctx);

// graph_view.c
GtkWidget *create_graph_view(AppContext *app_ctx);

// Styles
static const char *CSS_STYLE =
    "window { font-family: 'Segoe UI', Sans, sans-serif; }"
    "label.title { font-size: 24px; font-weight: 800; color: #34495e; margin: "
    "10px; }"
    "label.subtitle { font-size: 16px; font-weight: bold; color: #7f8c8d; }"

    // Cards (Menu)
    "button.card { background: white; border-radius: 12px; padding: 20px; "
    "transition: all 200ms; box-shadow: 0 4px 6px rgba(0,0,0,0.1); border: "
    "none; }"
    "button.card:hover { transform: translateY(-3px); box-shadow: 0 8px 12px "
    "rgba(0,0,0,0.15); background: #f0faff; }"
    "label.card-title { font-size: 18px; font-weight: bold; color: #2c3e50; }"
    "label.card-desc { font-size: 13px; color: #95a5a6; }"

    // Sidebar
    "box.sidebar { background-color: #f8f9fa; border-right: 1px solid #e1e8ed; "
    "padding: 15px; }"
    "frame { border-radius: 8px; background: white; border: 1px solid #ecf0f1; "
    "box-shadow: 0 2px 4px rgba(0,0,0,0.05); margin-bottom: 10px; }"
    "frame > label { font-weight: bold; color: #3498db; padding: 5px; }"

    // Buttons
    "button { border-radius: 6px; font-weight: 600; padding: 8px 16px; }"
    "button.btn-primary { background: linear-gradient(135deg, #3498db 0%, "
    "#2980b9 100%); color: white; border: none; }"
    "button.btn-primary:hover { opacity: 0.9; box-shadow: 0 2px 8px rgba(52, "
    "152, 219, 0.4); }"
    "button.btn-action { background: linear-gradient(135deg, #2ecc71 0%, "
    "#27ae60 100%); color: white; border: none; }"
    "button.btn-action:hover { opacity: 0.9; box-shadow: 0 2px 8px rgba(46, "
    "204, 113, 0.4); }"
    "button.btn-danger { background: linear-gradient(135deg, #e74c3c 0%, "
    "#c0392b 100%); color: white; border: none; }"
    "button.btn-danger:hover { opacity: 0.9; box-shadow: 0 2px 8px rgba(231, "
    "76, 60, 0.4); }"

    // Black buttons - High priority styling
    "button.btn-black { "
    "  background-image: none !important; "
    "  background-color: #000000 !important; "
    "  color: #ffffff !important; "
    "  border: 1px solid #000000 !important; "
    "  font-weight: bold; "
    "  min-height: 32px; "
    "  padding: 8px 16px; "
    "}"
    "button.btn-black:hover { "
    "  background-color: #1a1a1a !important; "
    "  opacity: 0.9; "
    "}"
    "button.btn-black:active { "
    "  background-color: #000000 !important; "
    "}"

    // Warning buttons (Orange)
    "button.btn-warning { "
    "  background: linear-gradient(135deg, #f39c12 0%, #e67e22 100%) "
    "!important; "
    "  color: white !important; "
    "  border: none !important; "
    "  font-weight: bold; "
    "}"
    "button.btn-warning:hover { "
    "  opacity: 0.9; "
    "  box-shadow: 0 2px 8px rgba(243, 156, 18, 0.4); "
    "}"

    // Info buttons (Cyan/Teal)
    "button.btn-info { "
    "  background: linear-gradient(135deg, #1abc9c 0%, #16a085 100%) "
    "!important; "
    "  color: white !important; "
    "  border: none !important; "
    "  font-weight: bold; "
    "}"
    "button.btn-info:hover { "
    "  opacity: 0.9; "
    "  box-shadow: 0 2px 8px rgba(26, 188, 156, 0.4); "
    "}"

    // Secondary buttons (Gray)
    "button.btn-secondary { "
    "  background: linear-gradient(135deg, #95a5a6 0%, #7f8c8d 100%) "
    "!important; "
    "  color: white !important; "
    "  border: none !important; "
    "  font-weight: bold; "
    "}"
    "button.btn-secondary:hover { "
    "  opacity: 0.9; "
    "  box-shadow: 0 2px 8px rgba(149, 165, 166, 0.4); "
    "}"

    // Header
    "box.header-bar { background-color: #2c3e50; padding: 20px; }"
    "label.header-title { font-size: 28px; font-weight: bold; color: white; }"

    // Menu Cards
    "button.card-green { border-top: 5px solid #2ecc71; }"
    "button.card-blue { border-top: 5px solid #3498db; }"
    "button.card-purple { border-top: 5px solid #9b59b6; }"
    "button.card-orange { border-top: 5px solid #e67e22; }"

    // Graph Area
    "drawingarea { background-color: white; border-radius: 8px; border: 1px "
    "solid #ecf0f1; }"

    // Typography
    "label.stat { font-family: 'Consolas', monospace; font-size: 12px; color: "
    "#555; }";

#endif
