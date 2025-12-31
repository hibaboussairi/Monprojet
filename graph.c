#include "app.h"
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h> // For va_list, va_start, va_end
#include <stdio.h>  // For vsnprintf, sprintf
#include <string.h>

#define MAX_NODES 20
#define INF 999999

typedef struct Node {
  double x, y;
  char label[16];
  int id;
} Node;

typedef struct Edge {
  int u, v; // Indices in nodes array
  int weight;
} Edge;

// --- State ---
static Node nodes[MAX_NODES];
static int node_count = 0;
static Edge edges[MAX_NODES * MAX_NODES];
static int edge_count = 0;

static int selected_node_idx = -1; // Removed usage, kept for safety or reuse
// Drag state
static int drag_start_node_idx = -1;
static double drag_start_x = 0;
static double drag_start_y = 0;
static double drag_curr_x = 0;
static double drag_curr_y = 0;
static int is_dragging = 0;

// Dialog state
static int pending_u = -1;
static int pending_v = -1;

static int path_nodes[MAX_NODES]; // Indices of nodes in path
static int path_len = 0;

// UI
static GtkWidget *entry_count;
static GtkWidget *combo_dtype;
static GtkWidget *combo_graph_type; // GO or GNO
static GtkWidget *combo_algo;
static GtkWidget *entry_start;
static GtkWidget *entry_end;
static GtkWidget *drawing_area;
static GtkWidget *text_log;

// --- Helpers ---

static void log_msg_graph(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_log));
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(buf, &end);
  gtk_text_buffer_insert(buf, &end, "> ", -1);
  gtk_text_buffer_insert(buf, &end, buffer, -1);
  gtk_text_buffer_insert(buf, &end, "\n", -1);
}

static int get_node_idx_by_label(const char *lbl) {
  if (!lbl)
    return -1;
  for (int i = 0; i < node_count; i++) {
    if (strcasecmp(nodes[i].label, lbl) == 0)
      return i;
  }
  return -1;
}

static int get_node_at(double x, double y) {
  for (int i = 0; i < node_count; i++) {
    double dx = nodes[i].x - x;
    double dy = nodes[i].y - y;
    if (dx * dx + dy * dy <= 20 * 20)
      return i;
  }
  return -1;
}

// --- Logic ---

static void generate_graph() {
  node_count = 0;
  edge_count = 0;
  memset(edges, 0, sizeof(edges)); // Force clear memory
  log_msg_graph("Reset Edges. Count=%d", edge_count);
  path_len = 0;
  const char *cnt_str = gtk_editable_get_text(GTK_EDITABLE(entry_count));
  int n = atoi(cnt_str);
  if (n <= 0)
    n = 5;
  if (n > MAX_NODES)
    n = MAX_NODES;

  int w = gtk_widget_get_width(drawing_area);
  int h = gtk_widget_get_height(drawing_area);
  if (w < 100)
    w = 600;
  if (h < 100)
    h = 400;

  double cx = w / 2.0, cy = h / 2.0;
  double r = (w < h ? w : h) / 2.0 - 50;

  int type_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo_dtype));
  // 0=Int, 1=Double, 2=Char, 3=String (Matching Python UI order approx)

  for (int i = 0; i < n; i++) {
    nodes[i].id = i;
    double angle = 2 * M_PI * i / n;
    nodes[i].x = cx + r * cos(angle);
    nodes[i].y = cy + r * sin(angle);

    if (type_idx == 0)
      sprintf(nodes[i].label, "%d", rand() % 100);
    else if (type_idx == 1)
      sprintf(nodes[i].label, "%.1f", (rand() % 1000) / 10.0);
    else if (type_idx == 2)
      sprintf(nodes[i].label, "%c", 'A' + i);
    else
      sprintf(nodes[i].label, "S%d", i);
  }
  node_count = n;
  log_msg_graph("Genere %d noeuds (sans liens).", n);
  gtk_widget_queue_draw(drawing_area);
}

static void add_edge(int u, int v, int w) {
  // Update existing
  for (int i = 0; i < edge_count; i++) {
    if (edges[i].u == u && edges[i].v == v) {
      edges[i].weight = w;
      return;
    }
  }
  // Add new
  edges[edge_count].u = u;
  edges[edge_count].v = v;
  edges[edge_count].weight = w;
  edge_count++;
}

static void on_weight_dialog_response(GtkDialog *dialog, int response_id,
                                      gpointer user_data) {
  if (response_id == GTK_RESPONSE_OK) {
    // GtkWidget *content_area = gtk_dialog_get_content_area(dialog); // unused
    // We know the entry is the first child of the first child (box) or similar.
    // Better to pass the entry as user_data or find it.
    // Let's passed the entry as user_data.
    GtkEntry *entry = GTK_ENTRY(user_data);
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    int w = atoi(text);
    if (w == 0 && strcmp(text, "0") != 0)
      w = 1; // Default if invalid

    if (pending_u != -1 && pending_v != -1) {
      add_edge(pending_u, pending_v, w);
      log_msg_graph("Lien %s->%s (poids %d)", nodes[pending_u].label,
                    nodes[pending_v].label, w);
      gtk_widget_queue_draw(drawing_area);
    }
  }
  gtk_window_destroy(GTK_WINDOW(dialog));
  pending_u = -1;
  pending_v = -1;
}

static void show_weight_dialog(int u, int v) {
  pending_u = u;
  pending_v = v;

  GtkWidget *dialog = gtk_dialog_new_with_buttons(
      "Poids de l'arc", GTK_WINDOW(gtk_widget_get_root(drawing_area)),
      GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Annuler",
      GTK_RESPONSE_CANCEL, "OK", GTK_RESPONSE_OK, NULL);

  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_box_append(GTK_BOX(content), box);

  char label_txt[64];
  snprintf(label_txt, sizeof(label_txt),
           "Poids de %s vers %s :", nodes[u].label, nodes[v].label);
  gtk_box_append(GTK_BOX(box), gtk_label_new(label_txt));

  GtkWidget *entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry), "1");
  gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
  gtk_box_append(GTK_BOX(box), entry);

  // Set default response
  gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

  g_signal_connect(dialog, "response", G_CALLBACK(on_weight_dialog_response),
                   entry);
  gtk_widget_show(dialog);
}

// --- Algorithms ---

static void run_dijkstra(int start, int end) {
  int dist[MAX_NODES];
  int prev[MAX_NODES];
  int visited[MAX_NODES];

  for (int i = 0; i < node_count; i++) {
    dist[i] = INF;
    prev[i] = -1;
    visited[i] = 0;
  }
  dist[start] = 0;

  for (int i = 0; i < node_count; i++) {
    int u = -1;
    int min_d = INF;
    // Find min dist node in unvisited
    for (int j = 0; j < node_count; j++) {
      if (!visited[j] && dist[j] < min_d) {
        min_d = dist[j];
        u = j;
      }
    }

    if (u == -1 || dist[u] == INF)
      break;
    visited[u] = 1;
    if (u == end)
      break;

    // Relax neighbors
    for (int k = 0; k < edge_count; k++) {
      if (edges[k].u == u) {
        int v = edges[k].v;
        int alt = dist[u] + edges[k].weight;
        if (alt < dist[v]) {
          dist[v] = alt;
          prev[v] = u;
        }
      }
    }
  }

  // Reconstruct
  path_len = 0;
  if (dist[end] != INF) {
    int curr = end;
    while (curr != -1) {
      path_nodes[path_len++] = curr;
      curr = prev[curr];
    }
    // Reverse path
    // (Implementation note: path_nodes stores reversed path, drawing logic
    // handles it)
    log_msg_graph("★ COURT CHEMIN (Dijkstra): Dist %d", dist[end]);
    // Optionally list path nodes for clarity
    char pstr[128] = "";
    // int curr = end; // Redefinition fixed
    // Reconstruct simply for printing (stored in prev array)
    // We can just iterate backwards or use path_nodes if we reversed it?
    // path_nodes has [End, Prev, ..., Start].
    for (int i = path_len - 1; i >= 0; i--) {
      strcat(pstr, nodes[path_nodes[i]].label);
      if (i > 0)
        strcat(pstr, " -> ");
    }
    log_msg_graph("Route: %s", pstr);
  } else {
    log_msg_graph("Aucun chemin.");
  }
}

static void run_bellman(int start, int end) {
  int dist[MAX_NODES];
  int prev[MAX_NODES];
  for (int i = 0; i < node_count; i++) {
    dist[i] = INF;
    prev[i] = -1;
  }
  dist[start] = 0;

  for (int i = 0; i < node_count - 1; i++) {
    for (int j = 0; j < edge_count; j++) {
      int u = edges[j].u;
      int v = edges[j].v;
      int w = edges[j].weight;
      if (dist[u] != INF && dist[u] + w < dist[v]) {
        dist[v] = dist[u] + w;
        prev[v] = u;
      }
    }
  }
  // (Negative cycle check omitted for brevity)

  path_len = 0;
  if (dist[end] != INF) {
    int curr = end;
    while (curr != -1) {
      path_nodes[path_len++] = curr;
      curr = prev[curr];
    }
    log_msg_graph("★ COURT CHEMIN (Bellman): Dist %d", dist[end]);
    char pstr[128] = "";
    for (int i = path_len - 1; i >= 0; i--) {
      strcat(pstr, nodes[path_nodes[i]].label);
      if (i > 0)
        strcat(pstr, " -> ");
    }
    log_msg_graph("Route: %s", pstr);
  } else {
    log_msg_graph("Aucun chemin.");
  }
}

static void run_floyd() {
  // Just computes matrix and logs it
  int mat[MAX_NODES][MAX_NODES];
  for (int i = 0; i < node_count; i++)
    for (int j = 0; j < node_count; j++)
      mat[i][j] = (i == j) ? 0 : INF;

  for (int i = 0; i < edge_count; i++)
    mat[edges[i].u][edges[i].v] = edges[i].weight;

  for (int k = 0; k < node_count; k++)
    for (int i = 0; i < node_count; i++)
      for (int j = 0; j < node_count; j++)
        if (mat[i][k] + mat[k][j] < mat[i][j])
          mat[i][j] = mat[i][k] + mat[k][j];

  log_msg_graph("Floyd-Warshall Termine. Voir console pour matrice (simule).");
  g_print("\n--- Matrice F-W ---\n");
  for (int i = 0; i < node_count; i++) {
    for (int j = 0; j < node_count; j++) {
      if (mat[i][j] == INF)
        g_print("INF ");
      else
        g_print("%3d ", mat[i][j]);
    }
    g_print("\n");
  }
  // Path visualization for FW isn't specific unless we stored 'next' pointers.
  // We'll just leave path empty.
  path_len = 0;
}

// --- Callbacks ---

static void on_generate(GtkButton *btn, gpointer data) { generate_graph(); }

static void on_clear(GtkButton *btn, gpointer data) {
  node_count = 0;
  edge_count = 0;
  path_len = 0;

  // Clear Log
  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_log));
  gtk_text_buffer_set_text(buf, "", 0);

  gtk_widget_queue_draw(drawing_area);
  log_msg_graph("Efface.");
}

// --- DFS All Paths ---

static void findAllPathsUtil(int u, int d, int visited[], int path[],
                             int path_index, int total_weight,
                             int is_directed) {
  visited[u] = 1;
  path[path_index] = u;
  path_index++;

  if (u == d) {
    // Path found - print it to log
    char path_str[256] = "";
    for (int i = 0; i < path_index; i++) {
      strcat(path_str, nodes[path[i]].label);
      if (i < path_index - 1)
        strcat(path_str, " -> ");
    }
    log_msg_graph("%s (Poids: %d)", path_str, total_weight);
  } else {
    // Recurse neighbors
    for (int i = 0; i < edge_count; i++) {
      int neighbor = -1;
      int w = edges[i].weight;

      if (edges[i].u == u) {
        neighbor = edges[i].v;
      } else if (!is_directed && edges[i].v == u) {
        neighbor = edges[i].u;
      }

      if (neighbor != -1 && !visited[neighbor]) {
        findAllPathsUtil(neighbor, d, visited, path, path_index,
                         total_weight + w, is_directed);
      }
    }
  }

  // Backtrack
  path_index--;
  visited[u] = 0;
}

static void printAllPaths(int s, int d) {
  int visited[MAX_NODES];
  int path[MAX_NODES];
  for (int i = 0; i < MAX_NODES; i++)
    visited[i] = 0;

  int is_directed =
      (gtk_drop_down_get_selected(GTK_DROP_DOWN(combo_graph_type)) == 0);

  log_msg_graph("--- Recherche de TOUS les chemins (%s -> %s) ---",
                nodes[s].label, nodes[d].label);
  findAllPathsUtil(s, d, visited, path, 0, 0, is_directed);
  log_msg_graph("-------------------------------------");
}

static void on_calc(GtkButton *btn, gpointer data) {
  const char *s_lbl = gtk_editable_get_text(GTK_EDITABLE(entry_start));
  const char *e_lbl = gtk_editable_get_text(GTK_EDITABLE(entry_end));
  int s = get_node_idx_by_label(s_lbl);
  int e = get_node_idx_by_label(e_lbl);

  if (s == -1 || e == -1) {
    log_msg_graph("Deb/Fin invalide.");
    return;
  }

  int algo = gtk_drop_down_get_selected(GTK_DROP_DOWN(combo_algo));

  // Clear log before starting
  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_log));
  gtk_text_buffer_set_text(buf, "", 0);

  // 1. Show ALL paths first
  printAllPaths(s, e);

  // 2. Run selected algo for shortest path highlighting
  if (algo == 0)
    run_dijkstra(s, e);
  else if (algo == 1)
    run_bellman(s, e);
  else
    run_floyd(); // FW doesn't use S/E per se for path calc in visualizer
                 // usually

  gtk_widget_queue_draw(drawing_area);
}

static void on_back(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "menu");
}

// Input handling with Drag
static void on_drag_begin(GtkGestureDrag *gesture, double start_x,
                          double start_y, gpointer user_data) {
  int idx = get_node_at(start_x, start_y);
  if (idx != -1) {
    drag_start_node_idx = idx;
    drag_start_x = start_x;
    drag_start_y = start_y;
    drag_curr_x = start_x;
    drag_curr_y = start_y;
    is_dragging = 1;
    gtk_widget_queue_draw(drawing_area);
  }
}

static void on_drag_update(GtkGestureDrag *gesture, double offset_x,
                           double offset_y, gpointer user_data) {
  if (is_dragging) {
    drag_curr_x = drag_start_x + offset_x;
    drag_curr_y = drag_start_y + offset_y;
    gtk_widget_queue_draw(drawing_area);
  }
}

static void on_drag_end(GtkGestureDrag *gesture, double offset_x,
                        double offset_y, gpointer user_data) {
  if (is_dragging) {
    double end_x = drag_start_x + offset_x;
    double end_y = drag_start_y + offset_y;
    int end_idx = get_node_at(end_x, end_y);

    if (end_idx != -1 && end_idx != drag_start_node_idx) {
      // Valid connection
      show_weight_dialog(drag_start_node_idx, end_idx);
    }

    is_dragging = 0;
    drag_start_node_idx = -1;
    gtk_widget_queue_draw(drawing_area);
  }
}

// --- Drawing ---

static void draw_arrow(cairo_t *cr, double x1, double y1, double x2, double y2,
                       const char *w_str, int highlight) {
  // Arrow logic
  cairo_set_source_rgb(cr, highlight ? 1 : 0, 0,
                       0); // Red if highlight, Black else
  cairo_set_line_width(cr, highlight ? 3 : 2);

  // Shorten line to not overlap node circles (r=20)
  double angle = atan2(y2 - y1, x2 - x1);
  double start_x = x1 + 20 * cos(angle);
  double start_y = y1 + 20 * sin(angle);
  double end_x = x2 - 20 * cos(angle);
  double end_y = y2 - 20 * sin(angle);

  cairo_move_to(cr, start_x, start_y);
  cairo_line_to(cr, end_x, end_y);
  cairo_stroke(cr);

  // Head
  cairo_move_to(cr, end_x - 10 * cos(angle - M_PI / 6),
                end_y - 10 * sin(angle - M_PI / 6));
  cairo_line_to(cr, end_x, end_y);
  cairo_line_to(cr, end_x - 10 * cos(angle + M_PI / 6),
                end_y - 10 * sin(angle + M_PI / 6));
  cairo_stroke(cr);

  // Weight text
  double mid_x = (x1 + x2) / 2;
  double mid_y = (y1 + y2) / 2;
  cairo_set_source_rgb(cr, 0, 0, 1); // Blue
  cairo_move_to(cr, mid_x, mid_y - 5);
  cairo_show_text(cr, w_str);
}

static void draw_line(cairo_t *cr, double x1, double y1, double x2, double y2,
                      const char *w_str, int highlight) {
  // Simple line without arrow (for undirected graphs)
  cairo_set_source_rgb(cr, highlight ? 1 : 0, 0, 0);
  cairo_set_line_width(cr, highlight ? 3 : 2);

  // Shorten line to not overlap node circles (r=20)
  double angle = atan2(y2 - y1, x2 - x1);
  double start_x = x1 + 20 * cos(angle);
  double start_y = y1 + 20 * sin(angle);
  double end_x = x2 - 20 * cos(angle);
  double end_y = y2 - 20 * sin(angle);

  cairo_move_to(cr, start_x, start_y);
  cairo_line_to(cr, end_x, end_y);
  cairo_stroke(cr);

  // Weight text
  double mid_x = (x1 + x2) / 2;
  double mid_y = (y1 + y2) / 2;
  cairo_set_source_rgb(cr, 0, 0, 1);
  cairo_move_to(cr, mid_x, mid_y - 5);
  cairo_show_text(cr, w_str);
}

static void draw_graph_func(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                            gpointer data) {
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);

  // Debug log (only once to avoid spam, maybe check if count > 0)
  if (edge_count > 0 && is_dragging == 0) {
    // log_msg_graph("Draw: Edge count %d", edge_count); // Commented to avoid
    // loop spam, but good for check
  }

  // Drag line
  if (is_dragging && drag_start_node_idx != -1) {
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 1.5);
    double dashes[] = {4.0, 2.0};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_move_to(cr, nodes[drag_start_node_idx].x,
                  nodes[drag_start_node_idx].y);
    cairo_line_to(cr, drag_curr_x, drag_curr_y);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0); // Reset dash
  }

  // Grid
  cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
  for (int i = 0; i < w; i += 50) {
    cairo_move_to(cr, i, 0);
    cairo_line_to(cr, i, h);
  }
  for (int i = 0; i < h; i += 50) {
    cairo_move_to(cr, 0, i);
    cairo_line_to(cr, w, i);
  }
  cairo_stroke(cr);

  // Edges
  for (int i = 0; i < edge_count; i++) {
    int u = edges[i].u;
    int v = edges[i].v;
    char buf[16];
    sprintf(buf, "%d", edges[i].weight);

    // Highlight if in path
    int is_path = 0;
    // Check if u->v is in path_nodes[] sequence
    // path_nodes is reversed list of nodes indices. e.g. [End, Prev, ...,
    // Start]
    for (int k = 0; k < path_len - 1; k++) {
      if (path_nodes[k + 1] == u && path_nodes[k] == v) {
        is_path = 1;
        break;
      }
    }

    // Check graph type: GO = directed (arrows), GNO = undirected (lines)
    int graph_type =
        gtk_drop_down_get_selected(GTK_DROP_DOWN(combo_graph_type));
    if (graph_type == 0) { // GO - Graphe Orienté
      draw_arrow(cr, nodes[u].x, nodes[u].y, nodes[v].x, nodes[v].y, buf,
                 is_path);
    } else { // GNO - Graphe Non Orienté
      draw_line(cr, nodes[u].x, nodes[u].y, nodes[v].x, nodes[v].y, buf,
                is_path);
    }
  }

  // Nodes
  for (int i = 0; i < node_count; i++) {
    cairo_new_path(cr); // Explicitly clear path to avoid connecting lines
    cairo_arc(cr, nodes[i].x, nodes[i].y, 20, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, 0.2, 0.6, 0.86);
    if (is_dragging && i == drag_start_node_idx)
      cairo_set_source_rgb(cr, 1, 0.6, 0.2); // Orange highlight source
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, nodes[i].label, &ext);
    cairo_move_to(cr, nodes[i].x - ext.width / 2, nodes[i].y + ext.height / 2);
    cairo_show_text(cr, nodes[i].label);
  }
}

GtkWidget *create_graph_view(AppContext *ctx) {
  GtkWidget *all = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  // Left
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_size_request(left, 160, -1);
  gtk_widget_set_margin_start(left, 10);
  gtk_widget_set_margin_top(left, 10);
  gtk_box_append(GTK_BOX(all), left);

  // Settings
  GtkWidget *f1 = gtk_frame_new("Generation");
  GtkWidget *b1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_frame_set_child(GTK_FRAME(f1), b1);
  gtk_box_append(GTK_BOX(left), f1);

  gtk_box_append(GTK_BOX(b1), gtk_label_new("Nombre de Noeuds:"));
  entry_count = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_count), "5");
  gtk_box_append(GTK_BOX(b1), entry_count);

  gtk_box_append(GTK_BOX(b1), gtk_label_new("Type de Donnee:"));
  const char *dtypes[] = {"Entiers", "Reels", "Caracteres", "Strings", NULL};
  combo_dtype = gtk_drop_down_new_from_strings(dtypes);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_dtype), 2); // Default 'A'
  gtk_box_append(GTK_BOX(b1), combo_dtype);

  gtk_box_append(GTK_BOX(b1), gtk_label_new("Type de Graphe:"));
  const char *gtypes[] = {"GO", "GNO", NULL};
  combo_graph_type = gtk_drop_down_new_from_strings(gtypes);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_graph_type),
                             0); // Default GO
  g_signal_connect_swapped(combo_graph_type, "notify::selected",
                           G_CALLBACK(gtk_widget_queue_draw), drawing_area);
  gtk_box_append(GTK_BOX(b1), combo_graph_type);

  GtkWidget *btn_gen = gtk_button_new_with_label("Generer (Noeuds)");
  gtk_widget_add_css_class(btn_gen, "btn-primary"); // Blue style (Custom)
  g_signal_connect(btn_gen, "clicked", G_CALLBACK(on_generate), NULL);
  gtk_box_append(GTK_BOX(b1), btn_gen);

  GtkWidget *btn_clr = gtk_button_new_with_label("Effacer");
  gtk_widget_add_css_class(btn_clr, "btn-danger"); // Red style (Custom)
  g_signal_connect(btn_clr, "clicked", G_CALLBACK(on_clear), NULL);
  gtk_box_append(GTK_BOX(b1), btn_clr);

  // Algo
  GtkWidget *f2 = gtk_frame_new("Algorithmes");
  GtkWidget *b2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_frame_set_child(GTK_FRAME(f2), b2);
  gtk_box_append(GTK_BOX(left), f2);

  const char *algos[] = {"Dijkstra", "Bellman-Ford", "Floyd-Warshall", NULL};
  combo_algo = gtk_drop_down_new_from_strings(algos);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(combo_algo), 0);
  gtk_box_append(GTK_BOX(b2), combo_algo);

  gtk_box_append(GTK_BOX(b2), gtk_label_new("Start (Label):"));
  entry_start = gtk_entry_new();
  gtk_box_append(GTK_BOX(b2), entry_start);

  gtk_box_append(GTK_BOX(b2), gtk_label_new("End (Label):"));
  entry_end = gtk_entry_new();
  gtk_box_append(GTK_BOX(b2), entry_end);

  GtkWidget *btn_calc = gtk_button_new_with_label("Calculer Chemin");
  gtk_widget_add_css_class(btn_calc, "btn-action"); // Green style (Custom)
  g_signal_connect(btn_calc, "clicked", G_CALLBACK(on_calc), NULL);
  gtk_box_append(GTK_BOX(b2), btn_calc);

  GtkWidget *btn_bk = gtk_button_new_with_label("⬅ Retour Menu");
  gtk_widget_add_css_class(btn_bk, "btn-action");
  gtk_widget_set_margin_top(btn_bk, 20);
  g_signal_connect(btn_bk, "clicked", G_CALLBACK(on_back), ctx);
  gtk_box_append(GTK_BOX(left), btn_bk);

  // Right Panel (Vertical)
  GtkWidget *right_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_hexpand(right_panel, TRUE);
  gtk_widget_set_vexpand(right_panel, TRUE);
  gtk_box_append(GTK_BOX(all), right_panel);

  // Center Canvas (Top of Right Panel)
  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(drawing_area, TRUE);
  gtk_widget_set_vexpand(drawing_area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area),
                                 draw_graph_func, NULL, NULL);
  gtk_box_append(GTK_BOX(right_panel), drawing_area);

  // Log (Bottom of Right Panel)
  GtkWidget *frame_log = gtk_frame_new("Journal d'Activite");
  gtk_widget_set_size_request(frame_log, -1, 150); // Fixed height for log
  gtk_box_append(GTK_BOX(right_panel), frame_log);

  GtkWidget *scr = gtk_scrolled_window_new();
  gtk_frame_set_child(GTK_FRAME(frame_log), scr);

  text_log = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_log), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), text_log);

  // Drag Controller
  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), NULL);
  g_signal_connect(drag, "drag-end", G_CALLBACK(on_drag_end), NULL);
  gtk_widget_add_controller(drawing_area, GTK_EVENT_CONTROLLER(drag));

  return all;
}
