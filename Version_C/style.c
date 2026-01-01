#include "app.h"

// -- Draw Functions for Icons --

static void draw_sort_icon(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
  // Green Bar Chart Icon
  cairo_set_source_rgb(cr, 0.18, 0.8, 0.44); // #2ecc71

  int vals[] = {30, 60, 45, 80, 20};
  int n = 5;
  int bar_w = 15;
  int gap = 8;
  int total_w = n * bar_w + (n - 1) * gap;
  int start_x = (width - total_w) / 2;

  for (int i = 0; i < n; i++) {
    cairo_rectangle(cr, start_x + i * (bar_w + gap), height - vals[i] - 10,
                    bar_w, vals[i]);
    cairo_fill(cr);
  }
}

static void draw_list_icon(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
  // Blue Linked List Icon
  cairo_set_source_rgb(cr, 0.2, 0.6, 0.86); // #3498db
  cairo_set_line_width(cr, 2);

  int box_w = 25;
  int box_h = 25;
  int gap = 20;
  int n = 3;
  int total_w = n * box_w + (n - 1) * gap;
  int start_x = (width - total_w) / 2;
  int y = height / 2 - box_h / 2;

  for (int i = 0; i < n; i++) {
    int x = start_x + i * (box_w + gap);
    // Box
    cairo_rectangle(cr, x, y, box_w, box_h);
    cairo_stroke(cr);

    // Number
    cairo_move_to(cr, x + 8, y + 17);
    char buf[2];
    sprintf(buf, "%d", i + 1);
    cairo_show_text(cr, buf);

    // Arrow
    if (i < n - 1) {
      cairo_move_to(cr, x + box_w, y + box_h / 2);
      cairo_line_to(cr, x + box_w + gap, y + box_h / 2);
      cairo_stroke(cr);
      // Arrowhead
      cairo_move_to(cr, x + box_w + gap - 5, y + box_h / 2 - 3);
      cairo_line_to(cr, x + box_w + gap, y + box_h / 2);
      cairo_line_to(cr, x + box_w + gap - 5, y + box_h / 2 + 3);
      cairo_stroke(cr);
    }
  }
}

static void draw_tree_icon(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
  // Purple Tree Icon
  cairo_set_source_rgb(cr, 0.61, 0.35, 0.71); // #9b59b6
  cairo_set_line_width(cr, 2);

  int radius = 8;
  int center_x = width / 2;
  int top_y = height / 2 - 20;

  // Edges
  cairo_move_to(cr, center_x, top_y);
  cairo_line_to(cr, center_x - 30, top_y + 40);
  cairo_stroke(cr);

  cairo_move_to(cr, center_x, top_y);
  cairo_line_to(cr, center_x + 30, top_y + 40);
  cairo_stroke(cr);

  // Nodes (Fill)
  cairo_arc(cr, center_x, top_y, radius, 0, 2 * M_PI);
  cairo_fill(cr);

  cairo_arc(cr, center_x - 30, top_y + 40, radius, 0, 2 * M_PI);
  cairo_fill(cr);

  cairo_arc(cr, center_x + 30, top_y + 40, radius, 0, 2 * M_PI);
  cairo_fill(cr);
}

static void draw_graph_icon(GtkDrawingArea *area, cairo_t *cr, int width,
                            int height, gpointer data) {
  // Orange Graph Icon
  cairo_set_source_rgb(cr, 0.9, 0.49, 0.13); // #e67e22
  cairo_set_line_width(cr, 2);

  int r = 6;
  // Positions
  double p[4][2] = {
      {width / 2.0 - 20, height / 2.0 - 20}, // TL
      {width / 2.0 + 20, height / 2.0 - 20}, // TR
      {width / 2.0 - 20, height / 2.0 + 20}, // BL
      {width / 2.0 + 20, height / 2.0 + 20}  // BR
  };

  // Edges
  cairo_move_to(cr, p[0][0], p[0][1]);
  cairo_line_to(cr, p[1][0], p[1][1]);
  cairo_line_to(cr, p[3][0], p[3][1]);
  cairo_line_to(cr, p[0][0], p[0][1]); // Triangle T-R-B
  cairo_stroke(cr);

  cairo_move_to(cr, p[0][0], p[0][1]);
  cairo_line_to(cr, p[2][0], p[2][1]);
  cairo_stroke(cr);

  // Nodes (Hollow)
  for (int i = 0; i < 4; i++) {
    cairo_set_source_rgb(cr, 1, 1, 1); // White fill
    cairo_arc(cr, p[i][0], p[i][1], r, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.9, 0.49, 0.13); // Orange Border
    cairo_arc(cr, p[i][0], p[i][1], r, 0, 2 * M_PI);
    cairo_stroke(cr);
  }
}

// -- Callbacks --

static void on_click_sort(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "sorting");
}

static void on_click_list(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "list");
}

static void on_click_tree(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "tree");
}

static void on_click_graph(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "graph");
}

// -- Helper to Create Card --

static GtkWidget *create_card(const char *title, const char *desc,
                              const char *css_class,
                              GtkDrawingAreaDrawFunc draw_func,
                              GCallback callback, AppContext *ctx) {
  GtkWidget *btn = gtk_button_new();
  gtk_widget_add_css_class(btn, "card");
  gtk_widget_add_css_class(btn, css_class); // Add specific color class

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_button_set_child(GTK_BUTTON(btn), box);
  // Center content
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);

  // Icon Area
  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_size_request(area, 120, 80);
  gtk_widget_set_halign(area, GTK_ALIGN_CENTER);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), draw_func, NULL, NULL);
  gtk_box_append(GTK_BOX(box), area);

  // Labels
  GtkWidget *lbl_t = gtk_label_new(title);
  gtk_widget_add_css_class(lbl_t, "card-title");
  gtk_box_append(GTK_BOX(box), lbl_t);

  GtkWidget *lbl_d = gtk_label_new(desc);
  gtk_widget_add_css_class(lbl_d, "card-desc");
  gtk_box_append(GTK_BOX(box), lbl_d);

  g_signal_connect(btn, "clicked", callback, ctx);
  return btn;
}

GtkWidget *create_menu_view(AppContext *ctx) {
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  // 1. Header
  GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_add_css_class(header, "header-bar");
  gtk_widget_set_halign(header, GTK_ALIGN_FILL);

  GtkWidget *lbl_head =
      gtk_label_new("Exploration des Algorithmes & Structures");
  gtk_widget_add_css_class(lbl_head, "header-title");
  gtk_widget_set_hexpand(lbl_head, TRUE);
  gtk_box_append(GTK_BOX(header), lbl_head);

  gtk_box_append(GTK_BOX(main_box), header);

  // 2. Grid of Cards
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 30);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 30);
  gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(grid, GTK_ALIGN_CENTER);
  gtk_widget_set_vexpand(grid, TRUE); // Center vertically in remaining space

  // Card 1: Sorting (Green)
  GtkWidget *card1 =
      create_card("Tableaux", "Comparaison Bubble, Merge, Quick Sort...",
                  "card-green", draw_sort_icon, G_CALLBACK(on_click_sort), ctx);
  gtk_widget_set_size_request(card1, 300, 200); // Fixed card size
  gtk_grid_attach(GTK_GRID(grid), card1, 0, 0, 1, 1);

  // Card 2: Lists (Blue)
  GtkWidget *card2 =
      create_card("Listes Chaînées", "Manipulation de nœuds et pointeurs",
                  "card-blue", draw_list_icon, G_CALLBACK(on_click_list), ctx);
  gtk_widget_set_size_request(card2, 300, 200);
  gtk_grid_attach(GTK_GRID(grid), card2, 1, 0, 1, 1);

  // Card 3: Trees (Purple)
  GtkWidget *card3 =
      create_card("Arbres", "Arbres Binaires, N-aires, Parcours", "card-purple",
                  draw_tree_icon, G_CALLBACK(on_click_tree), ctx);
  gtk_widget_set_size_request(card3, 300, 200);
  gtk_grid_attach(GTK_GRID(grid), card3, 0, 1, 1, 1);

  // Card 4: Graphs (Orange)
  GtkWidget *card4 = create_card(
      "Graphes", "Dijkstra, Bellman-Ford, Connexions", "card-orange",
      draw_graph_icon, G_CALLBACK(on_click_graph), ctx);
  gtk_widget_set_size_request(card4, 300, 200);
  gtk_grid_attach(GTK_GRID(grid), card4, 1, 1, 1, 1);

  gtk_box_append(GTK_BOX(main_box), grid);

  return main_box;
}
