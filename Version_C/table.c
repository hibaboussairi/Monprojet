#include "app.h"
#include <ctype.h>
#include <string.h>
#include <time.h>

// Generic Data Wrapper
typedef enum { TYPE_INT, TYPE_DOUBLE, TYPE_CHAR, TYPE_STRING } DataType;
static DataType current_dtype = TYPE_INT;

// Data for Text Views
static void **data_array = NULL;
static int data_size = 0;

// Benchmarking State
static double *perf_times[4];
static int perf_samples = 4;
static int perf_benchmark_max_n = 2000;

// UI Widgets
static GtkWidget *entry_size;
static GtkWidget *combo_type;
static GtkWidget *combo_algo;
static GtkWidget *text_before;
static GtkWidget *text_after;
static GtkWidget *label_stats;
static GtkWidget *drawing_area;
static GtkWidget *radio_asc;
static GtkWidget *radio_desc;

static gboolean graph_ready = FALSE;

// --- Helper Functions ---

static void free_data() {
  if (!data_array)
    return;
  for (int i = 0; i < data_size; i++) {
    if (data_array[i])
      free(data_array[i]);
  }
  free(data_array);
  data_array = NULL;
  data_size = 0;
}

static char *rand_string(int len) {
  char *s = malloc(len + 1);
  for (int i = 0; i < len; i++)
    s[i] = 'a' + rand() % 26;
  s[len] = '\0';
  return s;
}

// Comparator for Generic Data
static int cmp_generic(const void *a, const void *b) {
  // a and b are void* (pointers to data)
  int res = 0;
  if (current_dtype == TYPE_INT)
    res = *(int *)a - *(int *)b;
  else if (current_dtype == TYPE_DOUBLE)
    res = (*(double *)a > *(double *)b) - (*(double *)a < *(double *)b);
  else if (current_dtype == TYPE_CHAR)
    res = *(char *)a - *(char *)b;
  else
    res = strcmp((char *)a, (char *)b);

  if (gtk_check_button_get_active(GTK_CHECK_BUTTON(radio_desc)))
    return -res;
  return res;
}

// Wrapper for qsort (expects pointers to pointers)
static int cmp_ptr_wrapper(const void *a, const void *b) {
  return cmp_generic(*(void **)a, *(void **)b);
}

// --- VISIBLE GENERIC SORTS (For Text View) ---

static void swap_ptr(void **a, void **b) {
  void *temp = *a;
  *a = *b;
  *b = temp;
}

// 1. Bubble Generic
static void bubble_sort_generic(void **arr, int n) {
  for (int i = 0; i < n - 1; i++)
    for (int j = 0; j < n - i - 1; j++)
      if (cmp_generic(arr[j], arr[j + 1]) > 0) // Swap if j > j+1
        swap_ptr(&arr[j], &arr[j + 1]);
}

// 2. Insertion Generic
static void insertion_sort_generic(void **arr, int n) {
  for (int i = 1; i < n; i++) {
    void *key = arr[i];
    int j = i - 1;
    // Move elements of arr[0..i-1], that are greater than key
    while (j >= 0 && cmp_generic(arr[j], key) > 0) {
      arr[j + 1] = arr[j];
      j = j - 1;
    }
    arr[j + 1] = key;
  }
}

// 3. Shell Generic
static void shell_sort_generic(void **arr, int n) {
  for (int gap = n / 2; gap > 0; gap /= 2) {
    for (int i = gap; i < n; i++) {
      void *temp = arr[i];
      int j;
      for (j = i; j >= gap && cmp_generic(arr[j - gap], temp) > 0; j -= gap)
        arr[j] = arr[j - gap];
      arr[j] = temp;
    }
  }
}

// 4. Quick Generic
static int partition_generic(void **arr, int low, int high) {
  void *pivot = arr[high];
  int i = (low - 1);
  for (int j = low; j <= high - 1; j++) {
    if (cmp_generic(arr[j], pivot) < 0) {
      i++;
      swap_ptr(&arr[i], &arr[j]);
    }
  }
  swap_ptr(&arr[i + 1], &arr[high]);
  return (i + 1);
}
static void quick_sort_generic(void **arr, int low, int high) {
  if (low < high) {
    int pi = partition_generic(arr, low, high);
    quick_sort_generic(arr, low, pi - 1);
    quick_sort_generic(arr, pi + 1, high);
  }
}

static void generate_text_data(int n) {
  free_data();
  data_size = n;
  data_array = malloc(n * sizeof(void *));

  for (int i = 0; i < n; i++) {
    if (current_dtype == TYPE_INT) {
      int *v = malloc(sizeof(int));
      *v = rand() % 10000;
      data_array[i] = v;
    } else if (current_dtype == TYPE_DOUBLE) {
      double *v = malloc(sizeof(double));
      *v = (double)(rand() % 10000) / 100.0;
      data_array[i] = v;
    } else if (current_dtype == TYPE_CHAR) {
      char *v = malloc(sizeof(char));
      *v = 'A' + rand() % 26;
      data_array[i] = v;
    } else {
      data_array[i] = rand_string(4);
    }
  }
}

static void update_text_view(GtkWidget *view, void **arr, int n) {
  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  GString *s = g_string_new("");
  int limit = (n > 500) ? 500 : n;

  for (int i = 0; i < limit; i++) {
    if (i > 0)
      g_string_append(s, ", ");
    if (current_dtype == TYPE_INT)
      g_string_append_printf(s, "%d", *(int *)arr[i]);
    else if (current_dtype == TYPE_DOUBLE)
      g_string_append_printf(s, "%.1f", *(double *)arr[i]);
    else if (current_dtype == TYPE_CHAR)
      g_string_append_printf(s, "'%c'", *(char *)arr[i]);
    else
      g_string_append_printf(s, "\"%s\"", (char *)arr[i]);
  }
  if (n > limit)
    g_string_append_printf(s, " ... (+%d)", n - limit);

  gtk_text_buffer_set_text(buf, s->str, -1);
  g_string_free(s, TRUE);
}

// --- Benchmark Algos (Int Only for Graph speed) ---
static void swap_int(int *a, int *b) {
  int t = *a;
  *a = *b;
  *b = t;
}

static void bubble_bench(int *arr, int n) {
  for (int k = 0; k < n - 1; k++)
    for (int l = 0; l < n - k - 1; l++)
      if (arr[l] > arr[l + 1])
        swap_int(&arr[l], &arr[l + 1]);
}
static void insertion_bench(int *arr, int n) {
  for (int k = 1; k < n; k++) {
    int key = arr[k];
    int l = k - 1;
    while (l >= 0 && arr[l] > key) {
      arr[l + 1] = arr[l];
      l--;
    }
    arr[l + 1] = key;
  }
}
static void shell_bench(int *arr, int n) {
  for (int gap = n / 2; gap > 0; gap /= 2) {
    for (int k = gap; k < n; k++) {
      int temp = arr[k];
      int l;
      for (l = k; l >= gap && arr[l - gap] > temp; l -= gap)
        arr[l] = arr[l - gap];
      arr[l] = temp;
    }
  }
}
static int partition(int *arr, int l, int h) {
  int p = arr[h];
  int i = (l - 1);
  for (int j = l; j <= h - 1; j++) {
    if (arr[j] < p) {
      i++;
      swap_int(&arr[i], &arr[j]);
    }
  }
  swap_int(&arr[i + 1], &arr[h]);
  return (i + 1);
}
static void quick_bench(int *arr, int l, int h) {
  if (l < h) {
    int pi = partition(arr, l, h);
    quick_bench(arr, l, pi - 1);
    quick_bench(arr, pi + 1, h);
  }
}

static void run_benchmark_graph(int max_n) {
  perf_benchmark_max_n = max_n;
  if (!perf_times[0]) {
    for (int a = 0; a < 4; a++)
      perf_times[a] = malloc(perf_samples * sizeof(double));
  }

  int step = perf_benchmark_max_n / perf_samples;
  if (step < 1)
    step = 1;

  GString *stats_str = g_string_new("Temps Final (ms):\n");

  for (int s = 0; s < perf_samples; s++) {
    int n = (s + 1) * step;
    int *temp = malloc(n * sizeof(int));
    for (int a = 0; a < 4; a++) {
      for (int k = 0; k < n; k++)
        temp[k] = rand() % 1000;

      gint64 start = g_get_monotonic_time();
      if (a == 0)
        bubble_bench(temp, n);
      else if (a == 1)
        insertion_bench(temp, n);
      else if (a == 2)
        shell_bench(temp, n);
      else
        quick_bench(temp, 0, n - 1);
      gint64 end = g_get_monotonic_time();

      perf_times[a][s] = (double)(end - start) / 1000.0; // ms
    }
    free(temp);
  }

  g_string_append_printf(
      stats_str, "Bulle: %.3f\nInsertion: %.3f\nShell: %.3f\nRapide: %.3f",
      perf_times[0][perf_samples - 1], perf_times[1][perf_samples - 1],
      perf_times[2][perf_samples - 1], perf_times[3][perf_samples - 1]);

  gtk_label_set_text(GTK_LABEL(label_stats), stats_str->str);
  g_string_free(stats_str, TRUE);

  graph_ready = TRUE;
}

// --- Callbacks ---

static void on_gen(GtkButton *btn, gpointer data) {
  const char *sz_txt = gtk_editable_get_text(GTK_EDITABLE(entry_size));
  int n = atoi(sz_txt);
  if (n <= 0)
    n = 50;

  int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_type));
  if (idx == 0)
    current_dtype = TYPE_INT;
  else if (idx == 1)
    current_dtype = TYPE_DOUBLE;
  else if (idx == 2)
    current_dtype = TYPE_CHAR;
  else
    current_dtype = TYPE_STRING;

  generate_text_data(n);
  update_text_view(text_before, data_array, n);

  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_after));
  gtk_text_buffer_set_text(buf, "", -1);
}

static void on_sort_text_only(GtkButton *btn, gpointer data) {
  if (!data_array)
    return;

  int algo_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_algo));
  // 0=Bulle, 1=Insertion, 2=Shell, 3=Rapide

  gint64 start = g_get_monotonic_time();

  // Call the specific GENERIC sort
  if (algo_idx == 0)
    bubble_sort_generic(data_array, data_size);
  else if (algo_idx == 1)
    insertion_sort_generic(data_array, data_size);
  else if (algo_idx == 2)
    shell_sort_generic(data_array, data_size);
  else
    quick_sort_generic(data_array, 0, data_size - 1);

  gint64 end = g_get_monotonic_time();

  update_text_view(text_after, data_array, data_size);

  // Log
  char buf[128];
  sprintf(buf, "Tri Texte (%s): %.3f ms",
          (algo_idx == 0
               ? "Bulle"
               : (algo_idx == 1 ? "Insertion"
                                : (algo_idx == 2 ? "Shell" : "Rapide"))),
          (end - start) / 1000.0);
  gtk_label_set_text(GTK_LABEL(label_stats), buf);
}

static void on_compare(GtkButton *btn, gpointer data) {
  const char *sz_txt = gtk_editable_get_text(GTK_EDITABLE(entry_size));
  int max_n = atoi(sz_txt);
  if (max_n <= 0)
    max_n = 500;
  if (max_n > 5000)
    max_n = 5000; // Cap for sanity

  // Run benchmark (updates graph)
  run_benchmark_graph(max_n);

  // ALSO update the "After Sort" text view with a sorted version of the CURRENT
  // data so the user sees something happened in the text box too.
  if (data_array && data_size > 0) {
    // Create a shallow copy for sorting (don't free the actual
    // strings/pointers, just the array) Note: We need a DEEP copy if we were to
    // modify content, but sorting just swaps pointers. However, we can't mess
    // up the original 'data_array' if the user wants to run other sorts?
    // Actually, 'Sort' button modifies 'data_array'. Compare usually runs on
    // random data generated internally? Wait, 'run_benchmark_graph' generates
    // its OWN random data. The user expectation is likely: "Compare runs on the
    // data I generated or new data?" Usually Compare generates its own range of
    // sizes.

    // BUT, the specific request is: "il afficher les donnees pas trier juste
    // qu'on j'ai clique sur trier au moment il fonctionne" implying they want
    // to see *some* result.

    // Let's sort the CURRENT visible data using Quick Sort as a "Demo" of the
    // result.
    void **copy = malloc(data_size * sizeof(void *));
    memcpy(copy, data_array, data_size * sizeof(void *));

    quick_sort_generic(copy, 0, data_size - 1);
    update_text_view(text_after, copy, data_size);

    free(copy);
    // We don't free the elements because they are shared with data_array!
  }

  gtk_widget_queue_draw(drawing_area);
}

static void on_reset(GtkButton *btn, gpointer data) {
  free_data();
  graph_ready = FALSE;
  GtkTextBuffer *b1 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_before));
  gtk_text_buffer_set_text(b1, "", -1);
  GtkTextBuffer *b2 = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_after));
  gtk_text_buffer_set_text(b2, "", -1);
  gtk_label_set_text(GTK_LABEL(label_stats), "");
  gtk_widget_queue_draw(drawing_area);
}

static void on_back(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "menu");
}

// --- Drawing ---
static void draw_viz(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                     gpointer data) {
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);

  if (graph_ready && perf_times[0]) {
    // Draw Curves
    double max_t = 0.0001;
    for (int a = 0; a < 4; a++)
      for (int s = 0; s < perf_samples; s++)
        if (perf_times[a][s] > max_t)
          max_t = perf_times[a][s];

    int m = 60; // Margin
    int gw = w - 2 * m;
    int gh = h - 2 * m;

    // Grid & Scale
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_set_line_width(cr, 1);

    // Y Grid
    for (int i = 0; i <= 5; i++) {
      double y = (h - m) - (i * gh / 5.0);
      cairo_move_to(cr, m, y);
      cairo_line_to(cr, w - m, y);
      cairo_stroke(cr);

      // Label Y
      char buf[32];
      sprintf(buf, "%.2f", (i * max_t / 5.0));
      cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
      cairo_move_to(cr, 10, y + 4);
      cairo_show_text(cr, buf);
      cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    }

    // X Grid
    for (int s = 0; s < perf_samples; s++) {
      double x = m + s * (double)gw / (perf_samples - 1);
      cairo_move_to(cr, x, m);
      cairo_line_to(cr, x, h - m);
      cairo_stroke(cr);

      // Label X (Size)
      int n = (s + 1) * (perf_benchmark_max_n / perf_samples);
      char buf[32];
      sprintf(buf, "%d", n);
      cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
      cairo_move_to(cr, x - 10, h - m + 20);
      cairo_show_text(cr, buf);
      cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    }

    // Axes
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, m, h - m);
    cairo_line_to(cr, w - m, h - m); // X
    cairo_move_to(cr, m, h - m);
    cairo_line_to(cr, m, m); // Y
    cairo_stroke(cr);

    // Labels
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, w / 2 - 40, h - 20);
    cairo_show_text(cr, "Taille de tableau");

    cairo_save(cr);
    cairo_move_to(cr, 20, h / 2 + 40); // Position for Y-axis label
    cairo_rotate(cr, -M_PI / 2);       // Rotate 90 degrees counter-clockwise
    cairo_show_text(cr, "Temps (ms)");
    cairo_restore(cr);

    // Title
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_font_size(cr, 16);
    cairo_move_to(cr, w / 2 - 140, 30);
    cairo_show_text(cr, "Temps d'execution vs Taille");

    double col[4][3] = {
        {0, 0, 1}, {1, 0.5, 0}, {0, 0.8, 0}, {1, 0, 0}}; // B, Org, Grn, Red
    const char *nms[] = {"Bulle", "Insertion", "Shell", "Rapide"};

    for (int a = 0; a < 4; a++) {
      cairo_set_source_rgb(cr, col[a][0], col[a][1], col[a][2]);
      // Increased line width for visibility
      cairo_set_line_width(cr, 4);

      // Legend
      cairo_rectangle(cr, w - 120, 50 + a * 25, 15, 15);
      cairo_fill(cr);
      cairo_move_to(cr, w - 90, 62 + a * 25);
      cairo_show_text(cr, nms[a]);

      // Curve
      cairo_new_path(cr);
      for (int s = 0; s < perf_samples; s++) {
        double x = m + s * (double)gw / (perf_samples - 1);
        double y = (h - m) - (perf_times[a][s] / max_t) * gh;
        if (s == 0)
          cairo_move_to(cr, x, y);
        else
          cairo_line_to(cr, x, y);
      }
      cairo_stroke(cr);

      // Points (More visible)
      for (int s = 0; s < perf_samples; s++) {
        double x = m + s * (double)gw / (perf_samples - 1);
        double y = (h - m) - (perf_times[a][s] / max_t) * gh;
        cairo_arc(cr, x, y, 6, 0, 2 * M_PI); // Larger radius 6
        cairo_fill(cr);
      }
    }
  } else {
    cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, w / 2 - 150, h / 2);
    cairo_show_text(cr, "Appuyez sur 'Comparer'...");
  }
}

// --- Layout Construction ---

GtkWidget *create_sorting_view(AppContext *ctx) {
  GtkWidget *all = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

  // --- 1. Left Sidebar (Configuration) ---
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_size_request(left, 300, -1);
  gtk_widget_add_css_class(left, "sidebar"); // CSS Class
  gtk_box_append(GTK_BOX(all), left);

  // Title
  GtkWidget *lbl_conf = gtk_label_new("Configuration");
  gtk_widget_add_css_class(lbl_conf, "title");
  gtk_box_append(GTK_BOX(left), lbl_conf);

  // Frame: Taille
  GtkWidget *f1 = gtk_frame_new("Taille du Tableau (N)");
  entry_size = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_size), "1000");
  gtk_frame_set_child(GTK_FRAME(f1), entry_size);
  gtk_box_append(GTK_BOX(left), f1);

  // Frame: Type
  GtkWidget *f2 = gtk_frame_new("Type de données");
  combo_type = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_type), "Entier");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_type), "Reel");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_type), "Caractere");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_type), "Chaine");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_type), 0);
  gtk_frame_set_child(GTK_FRAME(f2), combo_type);
  gtk_box_append(GTK_BOX(left), f2);

  // Frame: Algo Text
  GtkWidget *f4 = gtk_frame_new("Algorithme (Pour Tri Texte)");
  combo_algo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_algo), "Bulle");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_algo), "Insertion");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_algo), "Shell");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_algo), "Rapide");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_algo), 0);
  gtk_frame_set_child(GTK_FRAME(f4), combo_algo);
  gtk_box_append(GTK_BOX(left), f4);

  // Frame: Ordre
  GtkWidget *f5 = gtk_frame_new("Ordre Final");
  GtkWidget *b5 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  radio_asc = gtk_check_button_new_with_label("Croissant");
  radio_desc = gtk_check_button_new_with_label("Decroissant");
  gtk_check_button_set_group(GTK_CHECK_BUTTON(radio_desc),
                             GTK_CHECK_BUTTON(radio_asc));
  gtk_check_button_set_active(GTK_CHECK_BUTTON(radio_asc), TRUE);
  gtk_box_append(GTK_BOX(b5), radio_asc);
  gtk_box_append(GTK_BOX(b5), radio_desc);
  gtk_frame_set_child(GTK_FRAME(f5), b5);
  gtk_box_append(GTK_BOX(left), f5);

  // Frame Stats
  GtkWidget *f6 = gtk_frame_new("Comparaison de temps");
  label_stats = gtk_label_new("...");
  gtk_widget_add_css_class(label_stats, "stat");
  gtk_frame_set_child(GTK_FRAME(f6), label_stats);
  gtk_box_append(GTK_BOX(left), f6);

  // --- 2. Main Right Panel ---
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_widget_set_margin_start(right, 10);
  gtk_widget_set_margin_end(right, 10);
  gtk_widget_set_margin_top(right, 10);
  gtk_widget_set_margin_bottom(right, 10);
  gtk_box_append(GTK_BOX(all), right);

  // Top Buttons
  GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_box_append(GTK_BOX(right), top_bar);

  GtkWidget *btn_bk = gtk_button_new_with_label("⬅ Retour Menu");
  gtk_widget_add_css_class(btn_bk, "btn-action");
  gtk_widget_set_size_request(btn_bk, 150, 40);
  g_signal_connect(btn_bk, "clicked", G_CALLBACK(on_back), ctx);
  gtk_box_append(GTK_BOX(top_bar), btn_bk);

  GtkWidget *btn_1 = gtk_button_new_with_label("1. Générer");
  gtk_widget_add_css_class(btn_1, "btn-primary");
  g_signal_connect(btn_1, "clicked", G_CALLBACK(on_gen), NULL);
  gtk_box_append(GTK_BOX(top_bar), btn_1);

  GtkWidget *btn_2 = gtk_button_new_with_label("2. Trier (Texte)");
  gtk_widget_add_css_class(btn_2, "btn-primary");
  g_signal_connect(btn_2, "clicked", G_CALLBACK(on_sort_text_only), NULL);
  gtk_box_append(GTK_BOX(top_bar), btn_2);

  GtkWidget *btn_3 = gtk_button_new_with_label("3. Comparer (Stats Graph)");
  gtk_widget_add_css_class(btn_3, "btn-primary");
  g_signal_connect(btn_3, "clicked", G_CALLBACK(on_compare), NULL);
  gtk_box_append(GTK_BOX(top_bar), btn_3);

  GtkWidget *btn_rst = gtk_button_new_with_label("Réinitialiser");
  gtk_widget_add_css_class(btn_rst, "btn-danger");
  g_signal_connect(btn_rst, "clicked", G_CALLBACK(on_reset), NULL);
  gtk_box_append(GTK_BOX(top_bar), btn_rst);

  // Middle Text Views
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  gtk_widget_set_size_request(paned, -1,
                              600); // Increased text area total height
  gtk_box_append(GTK_BOX(right), paned);

  // Before
  GtkWidget *sc1 = gtk_scrolled_window_new();
  gtk_widget_set_size_request(sc1, -1, 280); // Increased individual text height
  text_before = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_before), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_before), GTK_WRAP_WORD);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc1), text_before);

  GtkWidget *fr_b = gtk_frame_new("Données Initiales");
  gtk_frame_set_child(GTK_FRAME(fr_b), sc1);
  gtk_paned_set_start_child(GTK_PANED(paned), fr_b);
  gtk_paned_set_resize_start_child(GTK_PANED(paned), TRUE);

  // After
  GtkWidget *sc2 = gtk_scrolled_window_new();
  gtk_widget_set_size_request(sc2, -1, 80);
  text_after = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_after), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_after), GTK_WRAP_WORD);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc2), text_after);

  GtkWidget *fr_a = gtk_frame_new("Après Tri");
  gtk_frame_set_child(GTK_FRAME(fr_a), sc2);
  gtk_paned_set_end_child(GTK_PANED(paned), fr_a);
  gtk_paned_set_resize_end_child(GTK_PANED(paned), TRUE);

  // Bottom Graph
  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_vexpand(drawing_area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_viz, NULL,
                                 NULL);
  gtk_box_append(GTK_BOX(right), drawing_area);

  return all;
}
