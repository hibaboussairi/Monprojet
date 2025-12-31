#include "app.h"
#include <ctype.h>
#include <string.h>

// --- Types ---
typedef enum { TYPE_INT, TYPE_DOUBLE, TYPE_STRING, TYPE_CHAR } DataType;
typedef enum { LIST_SINGLE, LIST_DOUBLE } ListType;

typedef struct Node {
  void *data;
  struct Node *next;
  struct Node *prev; // For doubly linked
} Node;

// --- State ---
static Node *head = NULL;
static DataType current_dtype = TYPE_INT;
static ListType current_ltype = LIST_SINGLE;
static gboolean is_manual_mode = FALSE;

// Animation System
typedef enum { ANIM_IDLE, ANIM_INSERT, ANIM_DELETE } AnimationType;
typedef struct {
  AnimationType type;
  double progress;          // 0.0 to 1.0
  int target_index;         // Index being inserted/deleted
  double *node_x_positions; // Array of x positions for smooth interpolation
  int node_count;
  guint timer_id;
} AnimationState;

static AnimationState anim_state = {ANIM_IDLE, 0.0, -1, NULL, 0, 0};

// Generation Animation State
typedef struct {
  int target_count;
  int current_count;
  guint timer_id;
} GenState;
static GenState gen_state = {0, 0, 0};

// UI Controls
static GtkWidget *combo_ltype;
static GtkWidget *combo_dtype;
static GtkWidget *combo_sort;
static GtkWidget *radio_rand;
static GtkWidget *radio_manual;
static GtkWidget *entry_manual; // New manual input
static GtkWidget *entry_val;
static GtkWidget *entry_pos;
static GtkWidget *drawing_area;
static GtkWidget *text_log;
static GtkWidget *label_res_count;

// Forward Declarations
static void update_drawing_area_size();
static gboolean generation_tick(gpointer user_data);

// --- Helpers ---

static void free_node_data(Node *node) {
  if (node->data)
    free(node->data);
}

static void free_list() {
  Node *curr = head;
  while (curr) {
    Node *next = curr->next;
    free_node_data(curr);
    free(curr);
    curr = next;
  }
  head = NULL;
}

static int get_list_size() {
  int c = 0;
  Node *cur = head;
  while (cur) {
    c++;
    cur = cur->next;
  }
  return c;
}

static void update_res_count() {
  char buf[64];
  sprintf(buf, "Resultats: [%d elements]", get_list_size());
  gtk_label_set_text(GTK_LABEL(label_res_count), buf);
}

// Smooth easing function (ease-in-out)
static double ease_in_out(double t) {
  if (t < 0.5)
    return 2.0 * t * t;
  else
    return -1.0 + (4.0 - 2.0 * t) * t;
}

static void cleanup_animation() {
  if (anim_state.timer_id > 0) {
    g_source_remove(anim_state.timer_id);
    anim_state.timer_id = 0;
  }
  if (anim_state.node_x_positions) {
    free(anim_state.node_x_positions);
    anim_state.node_x_positions = NULL;
  }
  anim_state.type = ANIM_IDLE;
  anim_state.progress = 0.0;
  anim_state.target_index = -1;
  anim_state.node_count = 0;
}

// Animation Timer Callback
static gboolean animation_tick(gpointer user_data) {
  anim_state.progress += 0.05; // 20 frames @ 16ms = ~320ms animation

  if (anim_state.progress >= 1.0) {
    cleanup_animation();
    gtk_widget_queue_draw(drawing_area);
    return G_SOURCE_REMOVE; // Stop timer
  }

  gtk_widget_queue_draw(drawing_area);
  return G_SOURCE_CONTINUE; // Continue animation
}

// Start Animation Helper
static void start_animation(AnimationType type, int target_idx) {
  cleanup_animation(); // Clear any existing animation
  anim_state.type = type;
  anim_state.target_index = target_idx;
  anim_state.progress = 0.0;
  anim_state.node_count = get_list_size();

  // 60 FPS animation
  anim_state.timer_id = g_timeout_add(16, animation_tick, NULL);
}

static void log_msg(const char *fmt, ...) {
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

static void *parse_val(const char *txt) {
  if (!txt || strlen(txt) == 0)
    return NULL;

  if (current_dtype == TYPE_INT) {
    int *v = malloc(sizeof(int));
    *v = atoi(txt);
    return v;
  } else if (current_dtype == TYPE_DOUBLE) {
    double *v = malloc(sizeof(double));
    *v = atof(txt);
    return v;
  } else if (current_dtype == TYPE_CHAR) {
    char *v = malloc(sizeof(char));
    *v = txt[0];
    return v;
  } else {
    return strdup(txt);
  }
}

static char *val_to_str(void *data) {
  static char buf[64];
  if (current_dtype == TYPE_INT)
    sprintf(buf, "%d", *(int *)data);
  else if (current_dtype == TYPE_DOUBLE)
    sprintf(buf, "%.2f", *(double *)data);
  else if (current_dtype == TYPE_CHAR)
    sprintf(buf, "%c", *(char *)data);
  else
    snprintf(buf, 63, "%s", (char *)data);
  return buf;
}

// Comparator for sorting/sorted insert
static int compare_vals(void *a, void *b) {
  if (current_dtype == TYPE_INT)
    return *(int *)a - *(int *)b;
  if (current_dtype == TYPE_DOUBLE)
    return (*(double *)a > *(double *)b) - (*(double *)a < *(double *)b);
  if (current_dtype == TYPE_CHAR)
    return *(char *)a - *(char *)b;
  return strcmp((char *)a, (char *)b);
}

// --- Operations ---

static Node *create_node(void *data) {
  Node *n = calloc(1, sizeof(Node));
  n->data = data;
  return n;
}

static void append_node(void *data) {
  Node *n = create_node(data);
  if (!head) {
    head = n;
    return;
  }
  Node *curr = head;
  while (curr->next)
    curr = curr->next;
  curr->next = n;
  n->prev = curr;
  update_drawing_area_size();
}

static void prepend_node(void *data) {
  Node *n = create_node(data);
  n->next = head;
  if (head)
    head->prev = n;
  head = n;
  update_drawing_area_size();
}

static void insert_at(int idx, void *data) {
  if (idx <= 0) {
    prepend_node(data);
    return;
  }

  Node *curr = head;
  int c = 0;
  while (curr && c < idx - 1) {
    curr = curr->next;
    c++;
  }

  if (!curr) {
    append_node(data);
    return;
  }

  Node *n = create_node(data);
  n->next = curr->next;
  n->prev = curr;
  if (curr->next)
    curr->next->prev = n;
  curr->next = n;
}

static void insert_sorted(void *data) {
  if (!head || compare_vals(data, head->data) < 0) {
    prepend_node(data);
    return;
  }
  Node *curr = head;
  while (curr->next && compare_vals(curr->next->data, data) < 0) {
    curr = curr->next;
  }

  Node *n = create_node(data);
  n->next = curr->next;
  n->prev = curr;
  if (curr->next)
    curr->next->prev = n;
  curr->next = n;
}

static void delete_node(int idx) {
  if (!head)
    return;
  if (idx == 0) {
    Node *tmp = head;
    head = head->next;
    if (head)
      head->prev = NULL;
    free_node_data(tmp);
    free(tmp);
    return;
  }
  Node *curr = head;
  int c = 0;
  while (curr && c < idx) {
    curr = curr->next;
    c++;
  }
  if (curr) {
    if (curr->prev)
      curr->prev->next = curr->next;
    if (curr->next)
      curr->next->prev = curr->prev;
    free_node_data(curr);
    free(curr);
  } else {
    log_msg("Index %d introuvable.", idx);
  }
}

static void modify_pos(int idx) {
  const char *val_txt = gtk_editable_get_text(GTK_EDITABLE(entry_val));
  void *new_val = parse_val(val_txt);
  if (!new_val)
    return;

  Node *curr = head;
  int c = 0;
  while (curr && c < idx) {
    curr = curr->next;
    c++;
  }
  if (curr) {
    free_node_data(curr);
    curr->data = new_val;
    log_msg("Modifie Pos %d -> %s", idx, val_txt);
  }
}

// --- Sorting Algorithms (Data Swap) ---

static void swap_data(Node *a, Node *b) {
  void *t = a->data;
  a->data = b->data;
  b->data = t;
}

static void bubble_sort() {
  if (!head)
    return;
  int swapped;
  Node *ptr1;
  Node *lptr = NULL;
  do {
    swapped = 0;
    ptr1 = head;
    while (ptr1->next != lptr) {
      if (compare_vals(ptr1->data, ptr1->next->data) > 0) {
        swap_data(ptr1, ptr1->next);
        swapped = 1;
      }
      ptr1 = ptr1->next;
    }
    lptr = ptr1;
  } while (swapped);
}

static void insertion_sort() {
  if (!head || !head->next)
    return;
  Node *sorted = NULL;
  Node *curr = head;

  // Detach list
  head = NULL;

  while (curr) {
    Node *next = curr->next;
    // Insert 'curr' into 'sorted'
    if (!sorted || compare_vals(curr->data, sorted->data) < 0) {
      curr->next = sorted;
      if (sorted)
        sorted->prev = curr; // DList
      sorted = curr;
      sorted->prev = NULL;
    } else {
      Node *s = sorted;
      while (s->next && compare_vals(s->next->data, curr->data) < 0) {
        s = s->next;
      }
      curr->next = s->next;
      if (s->next)
        s->next->prev = curr;
      s->next = curr;
      curr->prev = s;
    }
    curr = next;
  }
  head = sorted;
}

// Shell Sort on Linked List (value swap using array for simplicity)
static void shell_sort() {
  int n = get_list_size();
  if (n < 2)
    return;

  // Convert to Array
  void **arr = malloc(n * sizeof(void *));
  Node *tk = head;
  for (int z = 0; z < n; z++) {
    arr[z] = tk->data;
    tk = tk->next;
  }

  // Shell Sort Array
  for (int gap = n / 2; gap > 0; gap /= 2) {
    for (int i = gap; i < n; i++) {
      void *temp = arr[i];
      int j;
      for (j = i; j >= gap && compare_vals(arr[j - gap], temp) > 0; j -= gap) {
        arr[j] = arr[j - gap];
      }
      arr[j] = temp;
    }
  }

  // Write back
  tk = head;
  for (int z = 0; z < n; z++) {
    tk->data = arr[z];
    tk = tk->next;
  }
  free(arr);
}

static void quick_sort() {
  int n = get_list_size();
  if (n < 2)
    return;

  void **arr = malloc(n * sizeof(void *));
  Node *tk = head;
  for (int z = 0; z < n; z++) {
    arr[z] = tk->data;
    tk = tk->next;
  }

  void swap_ptr(void **a, void **b) {
    void *t = *a;
    *a = *b;
    *b = t;
  }

  int partition(void **arr, int low, int high) {
    void *pivot = arr[high];
    int i = (low - 1);
    for (int j = low; j <= high - 1; j++) {
      if (compare_vals(arr[j], pivot) < 0) {
        i++;
        swap_ptr(&arr[i], &arr[j]);
      }
    }
    swap_ptr(&arr[i + 1], &arr[high]);
    return (i + 1);
  }

  void qs(void **arr, int low, int high) {
    if (low < high) {
      int pi = partition(arr, low, high);
      qs(arr, low, pi - 1);
      qs(arr, pi + 1, high);
    }
  }

  qs(arr, 0, n - 1);

  tk = head;
  for (int z = 0; z < n; z++) {
    tk->data = arr[z];
    tk = tk->next;
  }
  free(arr);
}

// --- Callbacks ---

static void on_mode_toggled(GtkCheckButton *btn, gpointer data) {
  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(radio_manual))) {
    gtk_widget_set_sensitive(entry_manual, TRUE);
    gtk_widget_set_visible(entry_manual, TRUE);
    is_manual_mode = TRUE;
  } else {
    gtk_widget_set_sensitive(entry_manual, FALSE);
    gtk_widget_set_visible(entry_manual, FALSE);
    is_manual_mode = FALSE;
  }
}

// Callback for size dialog OK button
static void on_size_dialog_ok(GtkButton *btn, gpointer user_data) {
  GtkWidget *dialog = GTK_WIDGET(user_data);
  GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "entry");

  // Get size from entry
  int n = 5;
  const char *size_txt = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (size_txt && strlen(size_txt) > 0) {
    n = atoi(size_txt);
    if (n <= 0)
      n = 5;
    if (n > 50)
      n = 50;
  }

  // Close dialog first
  gtk_window_destroy(GTK_WINDOW(dialog));

  // Start Animation for N elements
  if (gen_state.timer_id > 0)
    g_source_remove(gen_state.timer_id);

  gen_state.target_count = n;
  gen_state.current_count = 0;
  gen_state.timer_id =
      g_timeout_add(100, generation_tick, NULL); // 100ms per node

  log_msg("Demarrage generation: %d elements...", n);
}

static void on_gen(GtkButton *btn, gpointer data) {
  free_list();
  current_ltype = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_ltype));
  // DType: 0=Int, 1=Double, 2=String, 3=Char
  int dt = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_dtype));
  if (dt == 0)
    current_dtype = TYPE_INT;
  else if (dt == 1)
    current_dtype = TYPE_DOUBLE;
  else if (dt == 2)
    current_dtype = TYPE_STRING;
  else
    current_dtype = TYPE_CHAR;

  if (is_manual_mode) {
    // Parse CSV from entry_manual
    const char *txt = gtk_editable_get_text(GTK_EDITABLE(entry_manual));
    char *dup = strdup(txt);
    char *token = strtok(dup, ",; ");
    int count = 0;
    while (token) {
      append_node(parse_val(token));
      count++;
      token = strtok(NULL, ",; ");
    }
    free(dup);
    log_msg("Generation Manuelle: %d elements.", count);
    update_drawing_area_size();
  } else {
    // Show dialog to ask for size
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Générer Liste");
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 150);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_window_set_child(GTK_WINDOW(dialog), box);

    GtkWidget *label = gtk_label_new("Taille de la liste (max 50):");
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), "5");
    gtk_box_append(GTK_BOX(box), entry);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_set_homogeneous(GTK_BOX(btn_box), TRUE);
    gtk_box_append(GTK_BOX(box), btn_box);

    GtkWidget *btn_ok = gtk_button_new_with_label("OK");
    GtkWidget *btn_cancel = gtk_button_new_with_label("Annuler");
    gtk_box_append(GTK_BOX(btn_box), btn_ok);
    gtk_box_append(GTK_BOX(btn_box), btn_cancel);

    // Store entry reference
    g_object_set_data(G_OBJECT(dialog), "entry", entry);

    // Connect callbacks
    g_signal_connect(btn_ok, "clicked", G_CALLBACK(on_size_dialog_ok), dialog);
    g_signal_connect_swapped(btn_cancel, "clicked",
                             G_CALLBACK(gtk_window_destroy), dialog);

    // Show dialog
    gtk_window_present(GTK_WINDOW(dialog));
    return; // Exit early, generation happens in callback
  }
  update_res_count();
  gtk_widget_queue_draw(drawing_area);
  gtk_widget_queue_draw(drawing_area);
}

static void on_insert(GtkButton *btn, gpointer data) {
  const char *val_txt = gtk_editable_get_text(GTK_EDITABLE(entry_val));
  void *val = parse_val(val_txt);
  if (!val)
    return;

  // Sort Check?
  // If Combo is set to Insert Sorted? user has separate button for that in code
  // The "Inserer" button here uses Position Entry
  const char *pos_txt = gtk_editable_get_text(GTK_EDITABLE(entry_pos));
  int idx = atoi(pos_txt);
  insert_at(idx, val);
  log_msg("Insere %s a pos %d", val_txt, idx);
  update_res_count();
  update_drawing_area_size();
  gtk_widget_queue_draw(drawing_area);
}

static void on_sort_btn(GtkButton *btn, gpointer data) {
  int method = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_sort));
  // 0=Insertion, 1=Bubble (Existing) -> Add 2=Shell, 3=Quick

  if (method == 0)
    insertion_sort();
  else if (method == 1)
    bubble_sort();
  else if (method == 2)
    shell_sort();
  else if (method == 3)
    quick_sort();

  log_msg("Liste triee.");
  gtk_widget_queue_draw(drawing_area);
}

static void on_delete_btn(GtkButton *btn, gpointer data) {
  const char *pos_txt = gtk_editable_get_text(GTK_EDITABLE(entry_pos));
  int idx = atoi(pos_txt);
  if (get_list_size() > 0) {
    delete_node(idx);
    update_res_count();
    update_drawing_area_size();
    start_animation(ANIM_DELETE, idx);
  }
}

static void on_modify_btn(GtkButton *btn, gpointer data) {
  const char *pos_txt = gtk_editable_get_text(GTK_EDITABLE(entry_pos));
  int idx = atoi(pos_txt);
  modify_pos(idx);
  gtk_widget_queue_draw(drawing_area);
}

static void on_reset(GtkButton *btn, gpointer data) {
  free_list();
  update_res_count();
  update_drawing_area_size();
  gtk_widget_queue_draw(drawing_area);
}

// --- Dynamic Resizing ---
static void update_drawing_area_size() {
  int node_count = get_list_size();
  // Width = Margin + (NodeWidth + Gap) * Count
  // 80 + (70 + 60) * count = 80 + 130 * count
  int needed_width = 100 + node_count * 130;
  if (needed_width < 800)
    needed_width = 800;

  // Resize drawing area
  gtk_widget_set_size_request(drawing_area, needed_width, -1);
}

// --- Generation Animation ---
static gboolean generation_tick(gpointer user_data) {
  if (gen_state.current_count >= gen_state.target_count) {
    gen_state.timer_id = 0;
    log_msg("Generation terminee.");
    return G_SOURCE_REMOVE;
  }

  // Add one node
  if (current_dtype == TYPE_INT) {
    int *v = malloc(sizeof(int));
    *v = rand() % 100;
    append_node(v);
  } else if (current_dtype == TYPE_DOUBLE) {
    double *v = malloc(sizeof(double));
    *v = (double)(rand() % 1000) / 10.0;
    append_node(v);
  } else if (current_dtype == TYPE_STRING) {
    append_node(strdup("RND"));
  } else {
    char *v = malloc(sizeof(char));
    *v = 'A' + rand() % 26;
    append_node(v);
  }

  gen_state.current_count++;
  update_res_count();
  update_drawing_area_size(); // Resize as we grow

  // Scroll to end (simple approximation by resizing)
  gtk_widget_queue_draw(drawing_area);

  // Optional: Auto-scroll
  // This requires access to the adjustment, which we can get if we store the
  // scrolled window or traverse parents. simplified: just drawing update is
  // enough for "appearing" effect.

  return G_SOURCE_CONTINUE;
}

static void on_back(GtkButton *btn, AppContext *ctx) {
  if (gen_state.timer_id > 0) {
    g_source_remove(gen_state.timer_id);
    gen_state.timer_id = 0;
  }
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "menu");
}

// --- Callbacks for specific insertions ---

static void on_ins_head(GtkButton *btn, gpointer data) {
  const char *val_txt = gtk_editable_get_text(GTK_EDITABLE(entry_val));
  void *val = parse_val(val_txt);
  if (!val) {
    log_msg("Valeur vide!");
    return;
  }
  prepend_node(val);
  log_msg("Insere Debut: %s", val_txt);
  update_res_count();
  update_drawing_area_size();
  start_animation(ANIM_INSERT, 0);
}

static void on_ins_tail(GtkButton *btn, gpointer data) {
  const char *val_txt = gtk_editable_get_text(GTK_EDITABLE(entry_val));
  void *val = parse_val(val_txt);
  if (!val) {
    log_msg("Valeur vide!");
    return;
  }
  int old_size = get_list_size();
  append_node(val);
  log_msg("Insere Fin: %s", val_txt);
  update_res_count();
  update_drawing_area_size();
  start_animation(ANIM_INSERT, old_size);
}

static void on_ins_pos(GtkButton *btn, gpointer data) {
  const char *pos_txt = gtk_editable_get_text(GTK_EDITABLE(entry_pos));
  const char *val_txt = gtk_editable_get_text(GTK_EDITABLE(entry_val));
  void *val = parse_val(val_txt);
  if (!val) {
    log_msg("Valeur vide!");
    return;
  }
  int idx = atoi(pos_txt);
  insert_at(idx, val);
  log_msg("Insere Pos %d: %s", idx, val_txt);
  update_res_count();
  update_drawing_area_size();
  start_animation(ANIM_INSERT, idx);
}

// --- Drawing ---

static void draw_list(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                      gpointer data) {
  // Modern gradient background
  cairo_pattern_t *bg_gradient = cairo_pattern_create_linear(0, 0, 0, h);
  cairo_pattern_add_color_stop_rgb(bg_gradient, 0, 0.97, 0.98, 1.0);
  cairo_pattern_add_color_stop_rgb(bg_gradient, 1, 0.93, 0.95, 0.98);
  cairo_set_source(cr, bg_gradient);
  cairo_paint(cr);
  cairo_pattern_destroy(bg_gradient);

  int base_x = 80;
  int y = h / 2 - 30;
  int node_w = 70;
  int node_h = 50;
  int gap = 60;

  Node *curr = head;
  int node_count = get_list_size();

  // Calculate positions with animation
  double *positions = malloc(node_count * sizeof(double));
  int base_pos = base_x;
  for (int i = 0; i < node_count; i++) {
    positions[i] = base_pos;
    base_pos += node_w + gap;
  }

  // Apply animation offset if active
  if (anim_state.type != ANIM_IDLE && anim_state.progress < 1.0) {
    double ease_progress = ease_in_out(anim_state.progress);

    if (anim_state.type == ANIM_INSERT && anim_state.target_index >= 0) {
      for (int i = anim_state.target_index; i < node_count; i++) {
        double offset = (node_w + gap) * (1.0 - ease_progress);
        positions[i] += offset;
      }
    } else if (anim_state.type == ANIM_DELETE && anim_state.target_index >= 0) {
      for (int i = anim_state.target_index; i < node_count; i++) {
        double offset = (node_w + gap) * ease_progress;
        positions[i] -= offset;
      }
    }
  }

  // Only show HEAD and NULL if list is not empty
  if (!curr) {
    // Empty list - show nothing
    free(positions);
    return;
  }

  // Header "HEAD"
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  cairo_set_source_rgb(cr, 0.9, 0.2, 0.3);
  cairo_move_to(cr, base_x + 10, y - 30);
  cairo_show_text(cr, "HEAD");

  // Stylish arrow down
  cairo_set_source_rgb(cr, 0.9, 0.2, 0.3);
  cairo_set_line_width(cr, 2.5);
  cairo_move_to(cr, base_x + 30, y - 25);
  cairo_line_to(cr, base_x + 30, y - 5);
  cairo_stroke(cr);
  cairo_move_to(cr, base_x + 25, y - 10);
  cairo_line_to(cr, base_x + 30, y - 5);
  cairo_line_to(cr, base_x + 35, y - 10);
  cairo_stroke(cr);

  int idx = 0;
  while (curr) {
    double x = positions[idx];

    // Highlight active node during animation
    double highlight =
        (anim_state.type != ANIM_IDLE && idx == anim_state.target_index)
            ? (1.0 - anim_state.progress)
            : 0.0;

    // Shadow for depth
    cairo_set_source_rgba(cr, 0, 0, 0, 0.15);
    cairo_rectangle(cr, x + 3, y + 3, node_w, node_h);
    cairo_fill(cr);

    // Data Box with vibrant gradient
    cairo_pattern_t *data_grad =
        cairo_pattern_create_linear(x, y, x, y + node_h);
    cairo_pattern_add_color_stop_rgb(data_grad, 0, 0.2 + highlight * 0.3,
                                     0.4 + highlight * 0.2, 0.9);
    cairo_pattern_add_color_stop_rgb(data_grad, 1, 0.4 + highlight * 0.2,
                                     0.2 + highlight * 0.3, 0.8);
    cairo_set_source(cr, data_grad);
    cairo_rectangle(cr, x, y, node_w * 0.75, node_h);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(data_grad);

    cairo_set_source_rgb(cr, 0.1, 0.1, 0.3);
    cairo_set_line_width(cr, 2.5);
    cairo_stroke(cr);

    // Pointer Box with gradient
    cairo_pattern_t *ptr_grad = cairo_pattern_create_linear(
        x + node_w * 0.75, y, x + node_w, y + node_h);
    cairo_pattern_add_color_stop_rgb(ptr_grad, 0, 1.0, 0.5 + highlight * 0.2,
                                     0.2);
    cairo_pattern_add_color_stop_rgb(ptr_grad, 1, 0.9 + highlight * 0.1,
                                     0.3 + highlight * 0.2, 0.1);
    cairo_set_source(cr, ptr_grad);
    cairo_rectangle(cr, x + node_w * 0.75, y, node_w * 0.25, node_h);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(ptr_grad);

    cairo_set_source_rgb(cr, 0.1, 0.1, 0.3);
    cairo_stroke(cr);

    // Value text - larger and bolder
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_font_size(cr, 18);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    char *s = val_to_str(curr->data);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s, &ext);
    cairo_move_to(cr, x + (node_w * 0.75) / 2 - ext.width / 2,
                  y + node_h / 2 + ext.height / 2);
    cairo_show_text(cr, s);

    // Animated index - color coded
    cairo_set_source_rgb(cr, 0.3, 0.6, 0.9);
    cairo_set_font_size(cr, 13);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    char idx_buf[16];
    sprintf(idx_buf, "[%d]", idx);
    cairo_text_extents(cr, idx_buf, &ext);
    cairo_move_to(cr, x + node_w / 2 - ext.width / 2, y + node_h + 20);
    cairo_show_text(cr, idx_buf);

    // Stylish arrows
    if (curr->next) {
      double next_x = positions[idx + 1];

      // Forward Arrow
      cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
      cairo_set_line_width(cr, 3.0);
      int y_fwd = y + node_h / 3;
      cairo_move_to(cr, x + node_w, y_fwd);
      cairo_line_to(cr, next_x - 5, y_fwd);
      cairo_stroke(cr);

      cairo_move_to(cr, next_x - 10, y_fwd - 4);
      cairo_line_to(cr, next_x - 5, y_fwd);
      cairo_line_to(cr, next_x - 10, y_fwd + 4);
      cairo_stroke(cr);

      // Backward Arrow for doubly linked
      if (current_ltype == LIST_DOUBLE) {
        int y_bwd = y + 2 * node_h / 3;
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_set_line_width(cr, 2.5);
        cairo_move_to(cr, next_x - 5, y_bwd);
        cairo_line_to(cr, x + node_w, y_bwd);
        cairo_stroke(cr);

        cairo_move_to(cr, x + node_w + 5, y_bwd - 4);
        cairo_line_to(cr, x + node_w, y_bwd);
        cairo_line_to(cr, x + node_w + 5, y_bwd + 4);
        cairo_stroke(cr);
      }
    } else {
      // Stylish NULL indicator
      cairo_set_source_rgb(cr, 0.6, 0.1, 0.1);
      cairo_set_font_size(cr, 14);
      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_ITALIC,
                             CAIRO_FONT_WEIGHT_BOLD);
      cairo_move_to(cr, x + node_w + 15, y + node_h / 2 + 5);
      cairo_show_text(cr, "NULL");
    }

    curr = curr->next;
    idx++;
  }

  free(positions);
}

// --- Layout ---

GtkWidget *create_list_view(AppContext *ctx) {
  GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  // --- LEFT SIDEBAR ---
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_size_request(left, 320, -1);
  gtk_widget_add_css_class(left, "sidebar");
  gtk_box_append(GTK_BOX(main_box), left);

  GtkWidget *lbl_title = gtk_label_new("Controles");
  gtk_widget_add_css_class(lbl_title, "title");
  gtk_box_append(GTK_BOX(left), lbl_title);

  // -- SECTION 1: Config & Gen --
  GtkWidget *f1 = gtk_frame_new("Configuration et Generation");
  GtkWidget *b1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(b1, 5);
  gtk_widget_set_margin_end(b1, 5);
  gtk_widget_set_margin_top(b1, 5);
  gtk_widget_set_margin_bottom(b1, 5);
  gtk_frame_set_child(GTK_FRAME(f1), b1);
  gtk_box_append(GTK_BOX(left), f1);

  // Grid for Labels/Inputs
  GtkWidget *g1 = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(g1), 5);
  gtk_grid_set_column_spacing(GTK_GRID(g1), 10);
  gtk_box_append(GTK_BOX(b1), g1);

  // Type Liste
  gtk_grid_attach(GTK_GRID(g1), gtk_label_new("Type de Liste:"), 0, 0, 1, 1);
  combo_ltype = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ltype),
                                 "Chainee Simple");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ltype),
                                 "Chainee Double");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ltype), 0);
  gtk_grid_attach(GTK_GRID(g1), combo_ltype, 1, 0, 1, 1);

  // Type Données
  gtk_grid_attach(GTK_GRID(g1), gtk_label_new("Type de Donnees:"), 0, 1, 1, 1);
  combo_dtype = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Entier");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Reel");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Chaine");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Caractere");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_dtype), 0);
  gtk_grid_attach(GTK_GRID(g1), combo_dtype, 1, 1, 1, 1);

  // Methode Tri
  gtk_grid_attach(GTK_GRID(g1), gtk_label_new("Methode de Tri:"), 0, 2, 1, 1);
  combo_sort = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_sort),
                                 "Tri par Insertion");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_sort),
                                 "Tri a Bulles");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_sort),
                                 "Tri Shell"); // ADDED
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_sort),
                                 "Tri Rapide"); // ADDED
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_sort), 0);
  gtk_grid_attach(GTK_GRID(g1), combo_sort, 1, 2, 1, 1);

  // Generation Mode
  gtk_grid_attach(GTK_GRID(g1), gtk_label_new("Mode Generation:"), 0, 3, 1, 1);
  GtkWidget *box_rad = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  radio_rand = gtk_check_button_new_with_label("Aleatoire");
  radio_manual = gtk_check_button_new_with_label("Manuel");
  gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_manual),
                             GTK_CHECK_BUTTON(radio_rand));
  gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_rand), TRUE);

  // Toggle callback
  g_signal_connect(radio_manual, "toggled", G_CALLBACK(on_mode_toggled), NULL);

  gtk_box_append(GTK_BOX(box_rad), radio_rand);
  gtk_box_append(GTK_BOX(box_rad), radio_manual);
  gtk_grid_attach(GTK_GRID(g1), box_rad, 1, 3, 1, 1);

  // Manual Entry (Initially hidden)
  entry_manual = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_manual), "10,20,30");
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_manual),
                                 "Ex: 10, 20, 30"); // FIXED
  gtk_widget_set_visible(entry_manual, FALSE);      // Start hidden
  gtk_box_append(GTK_BOX(b1), entry_manual);

  // Generate Button (Blue gradient)
  GtkWidget *btn_gen = gtk_button_new_with_label("🎲 Generer Liste");
  gtk_widget_add_css_class(btn_gen, "btn-primary");
  g_signal_connect(btn_gen, "clicked", G_CALLBACK(on_gen), NULL);
  gtk_box_append(GTK_BOX(b1), btn_gen);

  // -- SECTION 2: Ops --
  GtkWidget *f2 = gtk_frame_new("Operations de Manipulation et Tri");
  gtk_box_append(GTK_BOX(left), f2);

  GtkWidget *b2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_start(b2, 10);
  gtk_widget_set_margin_end(b2, 10);
  gtk_widget_set_margin_top(b2, 10);
  gtk_widget_set_margin_bottom(b2, 10);
  gtk_frame_set_child(GTK_FRAME(f2), b2);

  GtkWidget *g2 = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(g2), 8);
  gtk_grid_set_column_spacing(GTK_GRID(g2), 8);
  gtk_box_append(GTK_BOX(b2), g2);

  // Value
  gtk_grid_attach(GTK_GRID(g2), gtk_label_new("Valeur:"), 0, 0, 1, 1);
  entry_val = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(g2), entry_val, 0, 1, 2, 1); // Full width

  // Position
  gtk_grid_attach(GTK_GRID(g2), gtk_label_new("Position (Index/Tri):"), 0, 2, 1,
                  1);
  entry_pos = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_pos), "0");
  gtk_grid_attach(GTK_GRID(g2), entry_pos, 0, 3, 2, 1); // Full width

  // Insert Menu Button with Dropdown (Green gradient)
  GtkWidget *menu_insert = gtk_menu_button_new();
  gtk_button_set_label(GTK_BUTTON(menu_insert), "➕ Inserer ▼");
  gtk_widget_add_css_class(menu_insert, "btn-action");

  // Create popover menu
  GtkWidget *popover = gtk_popover_new();
  gtk_widget_set_parent(popover, menu_insert);

  GtkWidget *menu_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start(menu_box, 5);
  gtk_widget_set_margin_end(menu_box, 5);
  gtk_widget_set_margin_top(menu_box, 5);
  gtk_widget_set_margin_bottom(menu_box, 5);

  // Menu items with colors
  GtkWidget *item_head = gtk_button_new_with_label("⬆️ Au Début");
  gtk_widget_add_css_class(item_head, "btn-primary");
  g_signal_connect(item_head, "clicked", G_CALLBACK(on_ins_head), NULL);
  g_signal_connect_swapped(item_head, "clicked",
                           G_CALLBACK(gtk_popover_popdown), popover);
  gtk_box_append(GTK_BOX(menu_box), item_head);

  GtkWidget *item_tail = gtk_button_new_with_label("⬇️ À la Fin");
  gtk_widget_add_css_class(item_tail, "btn-primary");
  g_signal_connect(item_tail, "clicked", G_CALLBACK(on_ins_tail), NULL);
  g_signal_connect_swapped(item_tail, "clicked",
                           G_CALLBACK(gtk_popover_popdown), popover);
  gtk_box_append(GTK_BOX(menu_box), item_tail);

  GtkWidget *item_pos = gtk_button_new_with_label("📍 À une Position...");
  gtk_widget_add_css_class(item_pos, "btn-primary");
  g_signal_connect(item_pos, "clicked", G_CALLBACK(on_ins_pos), NULL);
  g_signal_connect_swapped(item_pos, "clicked", G_CALLBACK(gtk_popover_popdown),
                           popover);
  gtk_box_append(GTK_BOX(menu_box), item_pos);

  gtk_popover_set_child(GTK_POPOVER(popover), menu_box);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_insert), popover);

  gtk_box_append(GTK_BOX(b2), menu_insert);

  // Delete (Red gradient)
  GtkWidget *btn_del = gtk_button_new_with_label("🗑️ Supprimer (Pos/Valeur)");
  gtk_widget_add_css_class(btn_del, "btn-danger");
  g_signal_connect(btn_del, "clicked", G_CALLBACK(on_delete_btn), NULL);
  gtk_box_append(GTK_BOX(b2), btn_del);

  // -- Bottom Buttons --
  GtkWidget *bb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_set_homogeneous(GTK_BOX(bb), TRUE);

  GtkWidget *btn_mod = gtk_button_new_with_label("✏️ Modifier");
  gtk_widget_add_css_class(btn_mod, "btn-warning");
  g_signal_connect(btn_mod, "clicked", G_CALLBACK(on_modify_btn), NULL);
  gtk_box_append(GTK_BOX(bb), btn_mod);

  GtkWidget *btn_sort = gtk_button_new_with_label("🔄 Trier");
  gtk_widget_add_css_class(btn_sort, "btn-info");
  g_signal_connect(btn_sort, "clicked", G_CALLBACK(on_sort_btn), NULL);
  gtk_box_append(GTK_BOX(bb), btn_sort);

  gtk_box_append(GTK_BOX(left), bb);

  GtkWidget *bb2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_set_homogeneous(GTK_BOX(bb2), TRUE);

  GtkWidget *btn_rst = gtk_button_new_with_label("🔄 Reinitialiser");
  gtk_widget_add_css_class(btn_rst, "btn-secondary");
  g_signal_connect(btn_rst, "clicked", G_CALLBACK(on_reset), NULL);
  gtk_box_append(GTK_BOX(bb2), btn_rst);

  GtkWidget *btn_bk = gtk_button_new_with_label("⬅️ Retour Menu");
  gtk_widget_add_css_class(btn_bk, "btn-action");
  g_signal_connect(btn_bk, "clicked", G_CALLBACK(on_back), ctx);
  gtk_box_append(GTK_BOX(bb2), btn_bk);

  gtk_box_append(GTK_BOX(left), bb2);

  // --- RIGHT VISUALIZATION ---
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_box_append(GTK_BOX(main_box), right);

  // Top Label
  label_res_count = gtk_label_new("Resultats: [0 elements]");
  gtk_widget_set_halign(label_res_count, GTK_ALIGN_START);
  gtk_widget_set_margin_start(label_res_count, 10);
  gtk_box_append(GTK_BOX(right), label_res_count);

  // Separator
  gtk_box_append(GTK_BOX(right), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

  // Scrolled Window for Drawing Area
  GtkWidget *scroll_draw = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_draw),
                                 GTK_POLICY_AUTOMATIC, // Horizontal scroll
                                 GTK_POLICY_NEVER);    // No vertical scroll
  gtk_widget_set_vexpand(scroll_draw, TRUE);

  // Drawing Area
  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(drawing_area, 2000,
                              -1); // Wide enough for many nodes
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_list,
                                 NULL, NULL);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll_draw), drawing_area);
  gtk_box_append(GTK_BOX(right), scroll_draw);

  // Log Area
  GtkWidget *fr_log = gtk_frame_new("Journal d'activite");
  gtk_widget_set_size_request(fr_log, -1, 150);

  GtkWidget *scr = gtk_scrolled_window_new();
  text_log = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_log), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), text_log);
  gtk_frame_set_child(GTK_FRAME(fr_log), scr);
  gtk_box_append(GTK_BOX(right), fr_log);

  return main_box;
}
