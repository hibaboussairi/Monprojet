#include "app.h"
#include <ctype.h>
#include <string.h>

// --- Data Structures ---
typedef enum { TREE_BINARY, TREE_NARY } TreeType;
typedef enum { TYPE_INT, TYPE_DOUBLE, TYPE_STRING } DataType;

#define MAX_CHILDREN 10

typedef struct TNode {
  void *data; // Generic data pointer
  struct TNode *children[MAX_CHILDREN];
  int child_count;
  // Layout info
  double x, y;
  int index;      // For animation
  int anim_state; // 0=Normal, 1=Visited, 2=Active
} TNode;

// --- State ---
static TNode *root = NULL;
static TreeType current_ttype = TREE_BINARY;
static DataType current_dtype = TYPE_INT;

// Limit children for N-ary visual clutter
static int max_children_limit = 2;

// Animation Globals
static int visible_count = 0;
static guint animation_timer_id = 0;
static int total_nodes_count = 0;

static gboolean on_animation_tick(gpointer data); // Forward decl

// Traversal Animation State
typedef struct {
  TNode *path[500];
  int count;
  int current_idx;
  guint timer_id;
} TraversalAnim;
static TraversalAnim trav_anim = {0};

static gboolean traversal_tick(gpointer data);

// UI Controls
static GtkWidget *combo_ttype;
static GtkWidget *combo_dtype;
static GtkWidget *entry_size;
static GtkWidget *combo_mode;      // Aléatoire / Manuel
static GtkWidget *entry_manual;    // Manual input string
static GtkWidget *combo_traversal; // Profondeur/Largeur
static GtkWidget *combo_order;     // Pre/In/Post
static GtkWidget *drawing_area;
static GtkWidget *text_log;

// Ops Controls
static GtkWidget *entry_op_val;
static GtkWidget *entry_op_new; // New value for modify

// --- Helper Functions ---

static void free_node(TNode *node) {
  if (!node)
    return;
  for (int i = 0; i < node->child_count; i++) {
    free_node(node->children[i]);
  }
  if (node->data)
    free(node->data);
  free(node);
}

static void log_msg_tree(const char *fmt, ...) {
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

static void log_part_tree(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char buffer[256];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_log));
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(buf, &end);
  // No newline, no prefix
  gtk_text_buffer_insert(buf, &end, buffer, -1);
}

static void *parse_val_tree(const char *txt) {
  if (!txt || strlen(txt) == 0)
    return NULL;

  // Clean whitespace if needed, but simple atoi handles it mostly
  // For strings, we might want to trim
  if (current_dtype == TYPE_INT) {
    int *v = malloc(sizeof(int));
    *v = atoi(txt);
    return v;
  } else if (current_dtype == TYPE_DOUBLE) {
    double *v = malloc(sizeof(double));
    *v = atof(txt);
    return v;
  } else {
    // Trim?
    while (isspace(*txt))
      txt++;
    char *dup = strdup(txt);
    // trim end
    int l = strlen(dup);
    while (l > 0 && isspace(dup[l - 1]))
      dup[--l] = '\0';
    return dup;
  }
}

static char *val_to_str_tree(void *data) {
  static char buf[64];
  if (!data)
    return "null";
  if (current_dtype == TYPE_INT)
    sprintf(buf, "%d", *(int *)data);
  else if (current_dtype == TYPE_DOUBLE)
    sprintf(buf, "%.2f", *(double *)data);
  else
    snprintf(buf, 63, "%s", (char *)data);
  return buf;
}

static TNode *create_node_tree(void *data) {
  TNode *n = calloc(1, sizeof(TNode));
  n->data = data;
  n->index = -1;
  n->anim_state = 0;
  return n;
}

static void assign_indices_bfs(TNode *start_node) {
  if (!start_node)
    return;
  // Simple BFS to assign indices 0, 1, 2...
  TNode *q[500];
  int f = 0, b = 0;
  q[b++] = start_node;
  int idx = 0;

  while (f < b) {
    TNode *curr = q[f++];
    curr->index = idx++;
    for (int i = 0; i < curr->child_count; i++) {
      q[b++] = curr->children[i];
    }
  }
  total_nodes_count = idx;
}

static gboolean on_animation_tick(gpointer data) {
  if (visible_count < total_nodes_count) {
    visible_count++;
    gtk_widget_queue_draw(drawing_area);
    return TRUE; // Continue
  } else {
    animation_timer_id = 0;
    return FALSE; // Stop
  }
}

// --- Generation Logic ---

static void generate_tree() {
  free_node(root);
  root = NULL;

  // Get Settings
  int t_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_ttype));
  current_ttype = (t_idx == 0) ? TREE_BINARY : TREE_NARY;
  max_children_limit = (current_ttype == TREE_BINARY) ? 2 : 4;

  int d_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_dtype));
  current_dtype = (d_idx == 0)   ? TYPE_INT
                  : (d_idx == 1) ? TYPE_DOUBLE
                                 : TYPE_STRING;

  int mode_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_mode));

  TNode **nodes = NULL;
  int size = 0;

  if (mode_idx == 1) { // Manuel
    const char *raw = gtk_editable_get_text(GTK_EDITABLE(entry_manual));
    char *input_str = strdup(raw);
    char *token = strtok(input_str, ",");
    TNode *temp_nodes[100];
    int count = 0;

    while (token && count < 100) {
      temp_nodes[count++] = create_node_tree(parse_val_tree(token));
      token = strtok(NULL, ",");
    }
    free(input_str);

    if (count == 0) {
      log_msg_tree("Aucune valeur saisie.");
      return;
    }
    size = count;
    nodes = malloc(size * sizeof(TNode *));
    for (int i = 0; i < size; i++)
      nodes[i] = temp_nodes[i];

  } else { // Aléatoire
    size = atoi(gtk_editable_get_text(GTK_EDITABLE(entry_size)));
    if (size <= 0)
      size = 15;
    nodes = malloc(size * sizeof(TNode *));
    for (int i = 0; i < size; i++) {
      void *d;
      if (current_dtype == TYPE_INT) {
        int *v = malloc(sizeof(int));
        *v = rand() % 100 + 1;
        d = v;
      } else if (current_dtype == TYPE_DOUBLE) {
        double *v = malloc(sizeof(double));
        *v = rand() % 100 + ((rand() % 10) / 10.0);
        d = v;
      } else {
        char b[10];
        sprintf(b, "N%d", i);
        d = strdup(b);
      }
      nodes[i] = create_node_tree(d);
    }
  }

  root = nodes[0];

  // Connect nodes randomly but structurally valid
  // Simple BFS fill or Random parent attachment
  // For 'Manual', we might want sequential fill to be predictable, or random?
  // Python does: connected=[root], shuffle connected, attach to one.

  // Let's stick to the Python logic for connectivity
  TNode **connected = malloc(size * sizeof(TNode *));
  int conn_count = 0;
  connected[conn_count++] = root;

  for (int i = 1; i < size; i++) {
    // Shuffle connected candidates to simulate randomness
    // Or just pick one that has space
    // To match Python exactly:
    /*
        candidates = list(connected)
        random.shuffle(candidates)
        for parent in candidates:
            if len(parent.children) >= limit: continue
            parent.children.append(node)
            connected.append(node)
            break
    */
    int added = 0;
    // Simple shuffle by iterating with offset
    int start_chk = rand() % conn_count;
    for (int k = 0; k < conn_count; k++) {
      int idx = (start_chk + k) % conn_count;
      TNode *p = connected[idx];
      if (p->child_count < max_children_limit) {
        p->children[p->child_count++] = nodes[i];
        connected[conn_count++] = nodes[i];
        added = 1;
        break;
      }
    }
    if (!added) {
      // Should not happen if size logic is fine, but safety:
      free_node(nodes[i]);
    }
  }

  free(connected);
  free(nodes);
  assign_indices_bfs(root);
  log_msg_tree("Arbre genere (%d noeuds).", size);

  // Start Animation
  visible_count = 0;
  if (animation_timer_id > 0) {
    g_source_remove(animation_timer_id);
  }
  animation_timer_id =
      g_timeout_add(300, on_animation_tick, NULL); // 300ms per node
}

// --- Traversals ---

static void dfs_pre(TNode *n, GString *bs) {
  if (!n)
    return;
  g_string_append_printf(bs, "%s -> ", val_to_str_tree(n->data));
  for (int i = 0; i < n->child_count; i++)
    dfs_pre(n->children[i], bs);
}

static void dfs_in(TNode *n, GString *bs) {
  if (!n)
    return;
  if (n->child_count > 0)
    dfs_in(n->children[0], bs);
  g_string_append_printf(bs, "%s -> ", val_to_str_tree(n->data));
  for (int i = 1; i < n->child_count; i++)
    dfs_in(n->children[i], bs);
}

static void dfs_post(TNode *n, GString *bs) {
  if (!n)
    return;
  for (int i = 0; i < n->child_count; i++)
    dfs_post(n->children[i], bs);
  g_string_append_printf(bs, "%s -> ", val_to_str_tree(n->data));
}

// --- Collection Logic for Animation ---

static void collect_dfs_pre(TNode *n, TraversalAnim *anim) {
  if (!n || anim->count >= 500)
    return;
  anim->path[anim->count++] = n;
  for (int i = 0; i < n->child_count; i++)
    collect_dfs_pre(n->children[i], anim);
}

static void collect_dfs_in(TNode *n, TraversalAnim *anim) {
  if (!n || anim->count >= 500)
    return;
  if (n->child_count > 0)
    collect_dfs_in(n->children[0], anim);
  anim->path[anim->count++] = n;
  for (int i = 1; i < n->child_count; i++)
    collect_dfs_in(n->children[i], anim);
}

static void collect_dfs_post(TNode *n, TraversalAnim *anim) {
  if (!n || anim->count >= 500)
    return;
  for (int i = 0; i < n->child_count; i++)
    collect_dfs_post(n->children[i], anim);
  anim->path[anim->count++] = n;
}

static void collect_bfs(TNode *root_node, TraversalAnim *anim) {
  if (!root_node)
    return;
  TNode *q[500];
  int f = 0, b = 0;
  q[b++] = root_node;

  while (f < b && anim->count < 500) {
    TNode *curr = q[f++];
    anim->path[anim->count++] = curr;
    for (int i = 0; i < curr->child_count; i++)
      q[b++] = curr->children[i];
  }
}

static void reset_anim_states(TNode *n) {
  if (!n)
    return;
  n->anim_state = 0;
  for (int i = 0; i < n->child_count; i++)
    reset_anim_states(n->children[i]);
}

static gboolean traversal_tick(gpointer data) {
  if (trav_anim.current_idx >= trav_anim.count) {
    trav_anim.timer_id = 0;
    log_msg_tree("Parcours termine.");
    // Revert last active to visited
    if (trav_anim.count > 0)
      trav_anim.path[trav_anim.count - 1]->anim_state = 1;
    gtk_widget_queue_draw(drawing_area);
    return G_SOURCE_REMOVE;
  }

  // Previous becomes Visited
  if (trav_anim.current_idx > 0) {
    trav_anim.path[trav_anim.current_idx - 1]->anim_state = 1; // Visited
  }

  // Current becomes Active
  TNode *curr = trav_anim.path[trav_anim.current_idx];
  curr->anim_state = 2; // Active

  // Horizontal Log
  log_part_tree("%s -> ", val_to_str_tree(curr->data));

  trav_anim.current_idx++;
  gtk_widget_queue_draw(drawing_area);

  return G_SOURCE_CONTINUE;
}

// --- Operations Logic ---

static int delete_node_rec(TNode *parent, const char *val_str) {
  for (int i = 0; i < parent->child_count; i++) {
    TNode *child = parent->children[i];
    if (strcmp(val_to_str_tree(child->data), val_str) == 0) {
      free_node(child);
      for (int j = i; j < parent->child_count - 1; j++) {
        parent->children[j] = parent->children[j + 1];
      }
      parent->child_count--;
      return 1;
    }
    if (delete_node_rec(child, val_str))
      return 1;
  }
  return 0;
}

static int modify_node_rec(TNode *node, const char *old_str, void *new_val) {
  if (!node)
    return 0;
  if (strcmp(val_to_str_tree(node->data), old_str) == 0) {
    // Free old data? Depends on if we own it.
    // Simple swap for now.
    if (node->data)
      free(node->data);
    node->data = new_val;
    return 1;
  }
  for (int i = 0; i < node->child_count; i++) {
    if (modify_node_rec(node->children[i], old_str, new_val))
      return 1;
  }
  return 0;
}

static void collect_values(TNode *n, void **arr, int *idx) {
  if (!n)
    return;
  arr[(*idx)++] = n->data;
  for (int i = 0; i < n->child_count; i++)
    collect_values(n->children[i], arr, idx);
}

static int cmp_tree_vals(const void *a, const void *b) {
  void *va = *(void **)a;
  void *vb = *(void **)b;
  if (current_dtype == TYPE_INT)
    return *(int *)va - *(int *)vb;
  if (current_dtype == TYPE_DOUBLE)
    return (*(double *)va > *(double *)vb) - (*(double *)va < *(double *)vb);
  return strcmp((char *)va, (char *)vb);
}

static TNode *build_bst(void **arr, int start, int end) {
  if (start > end)
    return NULL;
  int mid = (start + end) / 2;
  TNode *n = create_node_tree(arr[mid]);
  TNode *left = build_bst(arr, start, mid - 1);
  TNode *right = build_bst(arr, mid + 1, end);
  if (left)
    n->children[n->child_count++] = left;
  if (right)
    n->children[n->child_count++] = right;
  return n;
}

// --- Callbacks ---

static void on_mode_changed(GtkComboBox *widget, gpointer data) {
  int idx = gtk_combo_box_get_active(widget);
  if (idx == 1) { // Manuel
    gtk_widget_set_visible(entry_manual, TRUE);
  } else {
    gtk_widget_set_visible(entry_manual, FALSE);
  }
}

static void on_create(GtkButton *btn, gpointer data) {
  generate_tree();
  gtk_widget_queue_draw(drawing_area);
}

static void on_traverse(GtkButton *btn, gpointer data) {
  if (!root) {
    log_msg_tree("Arbre vide.");
    return;
  }

  // Stop existing animation
  if (trav_anim.timer_id > 0) {
    g_source_remove(trav_anim.timer_id);
    trav_anim.timer_id = 0;
  }

  // Reset States
  reset_anim_states(root);
  trav_anim.count = 0;
  trav_anim.current_idx = 0;

  int method = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_traversal));
  const char *name = "";

  if (method == 1) { // Largeur
    collect_bfs(root, &trav_anim);
    name = "Largeur";
  } else {
    int order = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_order));
    if (order == 0) {
      collect_dfs_pre(root, &trav_anim);
      name = "Profondeur Pre-Ordre";
    } else if (order == 1) {
      collect_dfs_in(root, &trav_anim);
      name = "Profondeur In-Ordre";
    } else {
      collect_dfs_post(root, &trav_anim);
      name = "Profondeur Post-Ordre";
    }
  }

  log_msg_tree("Demarrage Parcours %s (%d noeuds).", name, trav_anim.count);
  log_part_tree("Resultat: ");

  // Start Animation
  trav_anim.timer_id = g_timeout_add(500, traversal_tick, NULL); // 500ms delay
}

static void on_insert_node(GtkButton *btn, gpointer data) {
  if (!root)
    return;
  const char *txt = gtk_editable_get_text(GTK_EDITABLE(entry_op_val));
  void *val = parse_val_tree(txt);
  if (!val)
    return;

  TNode *queue[500];
  int f = 0, b = 0;
  queue[b++] = root;
  while (f < b) {
    TNode *curr = queue[f++];
    if (curr->child_count < max_children_limit) {
      curr->children[curr->child_count++] = create_node_tree(val);
      log_msg_tree("Insere %s sous %s", txt, val_to_str_tree(curr->data));
      gtk_widget_queue_draw(drawing_area);
      return;
    }
    for (int i = 0; i < curr->child_count; i++)
      queue[b++] = curr->children[i];
  }
  log_msg_tree("Arbre plein (visuellement).");
}

static void on_modify_node(GtkButton *btn, gpointer data) {
  if (!root)
    return;
  const char *old_txt = gtk_editable_get_text(GTK_EDITABLE(entry_op_val));
  const char *new_txt = gtk_editable_get_text(GTK_EDITABLE(entry_op_new));

  if (strlen(new_txt) == 0) {
    log_msg_tree("Veuillez saisir la Nouvelle Valeur.");
    return;
  }

  // Create new val
  void *new_val = parse_val_tree(new_txt);

  // Need copy of new_val because if we fail we might leak, but simpl pass
  // ownership
  if (modify_node_rec(root, old_txt, new_val)) {
    log_msg_tree("Noeud %s modifie en %s.", old_txt, new_txt);
    gtk_widget_queue_draw(drawing_area);
  } else {
    log_msg_tree("Noeud %s non trouve.", old_txt);
    // Clean up new_val
    if (new_val)
      free(new_val);
  }
}

static void on_delete_node(GtkButton *btn, gpointer data) {
  if (!root)
    return;
  const char *txt = gtk_editable_get_text(GTK_EDITABLE(entry_op_val));
  if (strcmp(val_to_str_tree(root->data), txt) == 0) {
    log_msg_tree("Impossible de supprimer la racine directement ici.");
    return;
  }
  if (delete_node_rec(root, txt)) {
    log_msg_tree("Noeud %s supprime.", txt);
    gtk_widget_queue_draw(drawing_area);
  } else {
    log_msg_tree("Noeud %s non trouve.", txt);
  }
}

static void on_ordonner(GtkButton *btn, gpointer data) {
  if (!root)
    return;
  void *vals[500];
  int count = 0;
  collect_values(root, vals, &count);
  qsort(vals, count, sizeof(void *), cmp_tree_vals);
  root = build_bst(vals, 0, count - 1);
  current_ttype = TREE_BINARY;
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ttype), 0);
  max_children_limit = 2;
  log_msg_tree("Arbre Ordonne (BST).");
  gtk_widget_queue_draw(drawing_area);
}

static void on_transform_binary(GtkButton *btn, gpointer data) {
  if (!root)
    return;
  if (current_ttype == TREE_BINARY) {
    log_msg_tree("Deja binaire.");
    return;
  }

  void *vals[500];
  int count = 0;
  // BFS collect to preserve level order roughly or simple collect
  // Python code does BFS collection
  TNode *queue[500];
  int f = 0, b = 0;
  queue[b++] = root;
  while (f < b) {
    TNode *n = queue[f++];
    vals[count++] = n->data; // Just pointer copy
    for (int i = 0; i < n->child_count; i++)
      queue[b++] = n->children[i];
  }

  // Rebuild as visual binary tree (Root + arbitrary left/right fill)
  // Not BST, just Binary Structure
  // Root is vals[0]
  TNode *new_root = create_node_tree(vals[0]);
  TNode *conn[500];
  int c_count = 0;
  conn[c_count++] = new_root;

  int v_idx = 1;
  int p_idx = 0; // parent index in conn

  while (v_idx < count) {
    TNode *p = conn[p_idx];
    if (p->child_count < 2) {
      TNode *node = create_node_tree(vals[v_idx++]);
      p->children[p->child_count++] = node;
      conn[c_count++] = node;
    } else {
      p_idx++;
    }
  }

  root = new_root;
  current_ttype = TREE_BINARY;
  max_children_limit = 2;
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ttype), 0);

  log_msg_tree("Transforme en Arbre Binaire.");
  gtk_widget_queue_draw(drawing_area);
}

static void on_reset(GtkButton *btn, gpointer data) {
  free_node(root);
  root = NULL;
  gtk_widget_queue_draw(drawing_area);
  log_msg_tree("Reinitialise.");
}

static void on_back(GtkButton *btn, AppContext *ctx) {
  gtk_stack_set_visible_child_name(GTK_STACK(ctx->stack), "menu");
}

// --- Drawing ---

static void layout_nary(TNode *node, double x, double y, double available_w) {
  if (!node)
    return;
  node->x = x;
  node->y = y;
  if (node->child_count == 0)
    return;
  double step =
      available_w / max_children_limit; // Use limit for spacing stability
  // Or dynamic: available_w / node->child_count
  // Python uses: available_w / max(2, num_children + 1)

  int num = node->child_count;
  if (num == 0)
    return;

  double child_step = available_w / num;
  double start = x - (available_w / 2) + child_step / 2;

  for (int i = 0; i < num; i++) {
    layout_nary(node->children[i], start + i * child_step, y + 120, child_step);
  }
}

static void add_connections_to_path(cairo_t *cr, TNode *node) {
  if (!node || node->index >= visible_count)
    return;

  // Add connections to visible children to the current path
  for (int i = 0; i < node->child_count; i++) {
    if (node->children[i]->index < visible_count) {
      // Calculate angle between parent and child
      double dx = node->children[i]->x - node->x;
      double dy = node->children[i]->y - node->y;
      double angle = atan2(dy, dx);
      double distance = sqrt(dx * dx + dy * dy);

      // Node radius
      double radius = 28.0;

      // Start point: at the edge of parent node
      double start_x = node->x + radius * cos(angle);
      double start_y = node->y + radius * sin(angle);

      // End point: at the edge of child node
      double end_x = node->children[i]->x - radius * cos(angle);
      double end_y = node->children[i]->y - radius * sin(angle);

      // Draw line from edge to edge
      cairo_move_to(cr, start_x, start_y);
      cairo_line_to(cr, end_x, end_y);

      // Recursively add connections for children
      add_connections_to_path(cr, node->children[i]);
    }
  }
}

static void draw_nodes_only(cairo_t *cr, TNode *node) {
  if (!node || node->index >= visible_count)
    return;

  // Draw Node
  cairo_new_path(cr); // Explicitly start new path for this node
  cairo_arc(cr, node->x, node->y, 28, 0, 2 * M_PI);

  // Color based on Animation State
  if (node->anim_state == 2) {               // Active
    cairo_set_source_rgb(cr, 0.9, 0.1, 0.1); // Red
  } else if (node->anim_state == 1) {        // Visited
    cairo_set_source_rgb(cr, 0.2, 0.8, 0.2); // Green
  } else {
    cairo_set_source_rgb(cr, 0.9, 0.49, 0.13); // Orange (Default)
    if (node->child_count > 0)
      cairo_set_source_rgb(cr, 0.2, 0.6, 0.86); // Blue (Default Parent)
  }

  cairo_fill_preserve(cr);

  if (node->anim_state == 2) { // Active Ring
    cairo_set_line_width(cr, 4.0);
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0); // Bright Red Border
  } else {
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 0, 0, 0);
  }

  cairo_stroke(cr);

  // Extra Ring for active
  if (node->anim_state == 2) {
    cairo_new_path(cr);
    cairo_arc(cr, node->x, node->y, 34, 0, 2 * M_PI); // Larger radius
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgb(cr, 1.0, 0.2, 0.2);
    cairo_stroke(cr);
  }

  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);
  cairo_set_font_size(cr, 14);
  char *s = val_to_str_tree(node->data);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, s, &ext);
  cairo_move_to(cr, node->x - ext.width / 2, node->y + ext.height / 2);
  cairo_show_text(cr, s);

  // Recursively draw child nodes
  for (int i = 0; i < node->child_count; i++) {
    draw_nodes_only(cr, node->children[i]);
  }
}

static void draw_func_tree(GtkDrawingArea *area, cairo_t *cr, int w, int h,
                           gpointer d) {
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);
  if (root) {
    layout_nary(root, w / 2, 50, w - 50);

    // Draw all connections as a SINGLE path, then stroke ONCE
    cairo_save(cr); // Save state
    cairo_new_path(cr);
    add_connections_to_path(cr, root);
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_set_line_width(cr, 1.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke(cr);
    cairo_restore(cr); // Restore state

    // Then draw all nodes on top
    draw_nodes_only(cr, root);
  } else {
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "Aucun Arbre", &ext);
    cairo_move_to(cr, w / 2 - ext.width / 2, h / 2);
    cairo_show_text(cr, "Aucun Arbre.");
  }
}

// --- Init UI ---

// Animation Globals (Moved to top)

// --- Dialog Helpers ---

typedef struct {
  int op_type; // 1=Insert, 2=Modify, 3=Delete
  GtkWidget *window;
  GtkWidget *entry1;
  GtkWidget *entry2;
} OpDialogData;

static void on_op_confirm(GtkButton *btn, gpointer data) {
  OpDialogData *d = (OpDialogData *)data;
  const char *txt1 = gtk_editable_get_text(GTK_EDITABLE(d->entry1));

  if (d->op_type == 1) { // Insert
    gtk_editable_set_text(GTK_EDITABLE(entry_op_val), txt1);
    on_insert_node(NULL, NULL);
  } else if (d->op_type == 2) { // Modify
    const char *txt2 = gtk_editable_get_text(GTK_EDITABLE(d->entry2));
    gtk_editable_set_text(GTK_EDITABLE(entry_op_val), txt1);
    gtk_editable_set_text(GTK_EDITABLE(entry_op_new), txt2);
    on_modify_node(NULL, NULL);
  } else if (d->op_type == 3) { // Delete
    gtk_editable_set_text(GTK_EDITABLE(entry_op_val), txt1);
    on_delete_node(NULL, NULL);
  }

  gtk_window_destroy(GTK_WINDOW(d->window));
  free(d);
}

static void show_op_input_dialog(int op_type) {
  OpDialogData *d = malloc(sizeof(OpDialogData));
  d->op_type = op_type;

  d->window = gtk_window_new();
  gtk_window_set_title(
      GTK_WINDOW(d->window),
      (op_type == 1) ? "Inserer Noeud"
                     : (op_type == 2 ? "Modifier Noeud" : "Supprimer Noeud"));
  gtk_window_set_modal(GTK_WINDOW(d->window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(d->window), 300, 150);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_window_set_child(GTK_WINDOW(d->window), box);

  if (op_type == 2) {
    gtk_box_append(GTK_BOX(box), gtk_label_new("Ancienne Valeur:"));
  } else {
    gtk_box_append(GTK_BOX(box), gtk_label_new("Valeur:"));
  }

  d->entry1 = gtk_entry_new();
  gtk_box_append(GTK_BOX(box), d->entry1);

  if (op_type == 2) {
    gtk_box_append(GTK_BOX(box), gtk_label_new("Nouvelle Valeur:"));
    d->entry2 = gtk_entry_new();
    gtk_box_append(GTK_BOX(box), d->entry2);
  } else {
    d->entry2 = NULL;
  }

  GtkWidget *btn = gtk_button_new_with_label("Valider");
  gtk_widget_add_css_class(btn, "btn-primary");
  g_signal_connect(btn, "clicked", G_CALLBACK(on_op_confirm), d);
  gtk_box_append(GTK_BOX(box), btn);

  gtk_widget_set_visible(d->window, TRUE);
}

static void on_op_dlg_insert(GtkButton *btn, gpointer data) {
  show_op_input_dialog(1);
}
static void on_op_dlg_modify(GtkButton *btn, gpointer data) {
  show_op_input_dialog(2);
}
static void on_op_dlg_delete(GtkButton *btn, gpointer data) {
  show_op_input_dialog(3);
}

static void on_open_ops_dialog_wrapper(GtkButton *btn, gpointer data) {
  GtkWidget *win = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(win), "Inserer/Modifier/Supprimer");
  gtk_window_set_modal(GTK_WINDOW(win), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(win), 300, 250);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_bottom(box, 20);
  gtk_window_set_child(GTK_WINDOW(win), box);

  GtkWidget *b1 = gtk_button_new_with_label("1. Inserer un Noeud");
  g_signal_connect(b1, "clicked", G_CALLBACK(on_op_dlg_insert), NULL);
  gtk_box_append(GTK_BOX(box), b1);

  GtkWidget *b2 = gtk_button_new_with_label("2. Modifier un Noeud");
  g_signal_connect(b2, "clicked", G_CALLBACK(on_op_dlg_modify), NULL);
  gtk_box_append(GTK_BOX(box), b2);

  GtkWidget *b3 = gtk_button_new_with_label("3. Supprimer un Noeud");
  gtk_widget_add_css_class(b3, "btn-danger");
  g_signal_connect(b3, "clicked", G_CALLBACK(on_op_dlg_delete), NULL);
  gtk_box_append(GTK_BOX(box), b3);

  gtk_widget_set_visible(win, TRUE);
}

GtkWidget *create_tree_view(AppContext *ctx) {
  GtkWidget *all = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  // Left Sidebar - Absolute minimum width for maximum tree display area
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_size_request(left, 200, -1);
  gtk_widget_set_margin_start(left, 0);
  gtk_widget_set_margin_top(left, 0);
  gtk_box_append(GTK_BOX(all), left);

  // --- Parameters ---
  GtkWidget *fp = gtk_frame_new("Parametres de l'Arbre");
  GtkWidget *bp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_widget_set_margin_start(bp, 0);
  gtk_widget_set_margin_end(bp, 0);
  gtk_widget_set_margin_top(bp, 0);
  gtk_frame_set_child(GTK_FRAME(fp), bp);
  gtk_box_append(GTK_BOX(left), fp);

  // Row 1: Type
  GtkWidget *r1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(r1), gtk_label_new("Type:"));
  combo_ttype = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ttype), "Binaire");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_ttype), "N-aire");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ttype), 0);
  gtk_widget_set_hexpand(combo_ttype, TRUE);
  gtk_box_append(GTK_BOX(r1), combo_ttype);
  gtk_box_append(GTK_BOX(bp), r1);

  // Row 2: DataType
  GtkWidget *r2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(r2), gtk_label_new("Donnees:"));
  combo_dtype = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Entiers");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Reels");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_dtype), "Chaines");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_dtype), 0);
  gtk_widget_set_hexpand(combo_dtype, TRUE);
  gtk_box_append(GTK_BOX(r2), combo_dtype);
  gtk_box_append(GTK_BOX(bp), r2);

  // Row 3: Size & Mode
  GtkWidget *r3 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(r3), gtk_label_new("Taille:"));
  entry_size = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_size), "10");
  gtk_widget_set_size_request(entry_size, 50, -1);
  gtk_box_append(GTK_BOX(r3), entry_size);

  combo_mode = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_mode), "Aleatoire");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_mode), "Manuel");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_mode), 0);
  g_signal_connect(combo_mode, "changed", G_CALLBACK(on_mode_changed), NULL);
  gtk_box_append(GTK_BOX(r3), combo_mode);
  gtk_box_append(GTK_BOX(bp), r3);

  // Row 3.5: Manual Entry (Hidden by default)
  entry_manual = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_manual), "10, 20, 30...");
  gtk_widget_set_visible(entry_manual, FALSE);
  gtk_box_append(GTK_BOX(bp), entry_manual);

  // Row 4: Traversal
  GtkWidget *r4 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(r4), gtk_label_new("Parcours:"));
  combo_traversal = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_traversal),
                                 "Profondeur");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_traversal),
                                 "Largeur");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_traversal), 0);
  gtk_widget_set_hexpand(combo_traversal, TRUE);
  gtk_box_append(GTK_BOX(r4), combo_traversal);
  gtk_box_append(GTK_BOX(bp), r4);

  combo_order = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_order), "Pre-Ordre");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_order), "In-Ordre");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_order), "Post-Ordre");
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo_order), 0);
  gtk_box_append(GTK_BOX(bp), combo_order);

  // --- Buttons Block (Big Buttons) ---
  GtkWidget *box_btns = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(left), box_btns);

// Helper macro for blue buttons
#define ADD_BLUE_BTN(label, callback)                                          \
  {                                                                            \
    GtkWidget *b = gtk_button_new_with_label(label);                           \
    gtk_widget_add_css_class(b, "btn-primary");                                \
    gtk_widget_set_size_request(b, -1, 45);                                    \
    g_signal_connect(b, "clicked", G_CALLBACK(callback), NULL);                \
    gtk_box_append(GTK_BOX(box_btns), b);                                      \
  }

  ADD_BLUE_BTN("✔ Creer", on_create);
  ADD_BLUE_BTN("⚙ Editer", on_open_ops_dialog_wrapper);
  ADD_BLUE_BTN("🌪 Ordonner", on_ordonner);
  ADD_BLUE_BTN("♻ Binaire", on_transform_binary);
  ADD_BLUE_BTN("▶ Parcours", on_traverse);

  GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(box_btns), sep);

  GtkWidget *btn_reset = gtk_button_new_with_label("🗑 Reinitialiser");
  gtk_widget_add_css_class(btn_reset, "btn-danger");
  gtk_widget_set_size_request(btn_reset, -1, 40);
  g_signal_connect(btn_reset, "clicked", G_CALLBACK(on_reset), NULL);
  gtk_box_append(GTK_BOX(box_btns), btn_reset);

  // Back Button at bottom (separate from stack)
  GtkWidget *btn_back = gtk_button_new_with_label("⬅ Retour Menu");
  gtk_widget_add_css_class(btn_back, "btn-action");
  gtk_widget_set_margin_top(btn_back, 20);
  g_signal_connect(btn_back, "clicked", G_CALLBACK(on_back), ctx);
  gtk_box_append(GTK_BOX(left), btn_back);

  // Hidden Ops Controls (kept for logic compatibility)
  GtkWidget *hidden_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_visible(hidden_box, FALSE);
  gtk_box_append(GTK_BOX(left), hidden_box);

  entry_op_val = gtk_entry_new();
  gtk_box_append(GTK_BOX(hidden_box), entry_op_val);
  entry_op_new = gtk_entry_new();
  gtk_box_append(GTK_BOX(hidden_box), entry_op_new);

  // Right Side (Canvas + Log)
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_set_hexpand(right, TRUE);
  gtk_box_append(GTK_BOX(all), right);

  drawing_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(drawing_area, 800, -1);
  gtk_widget_set_vexpand(drawing_area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing_area), draw_func_tree,
                                 NULL, NULL);
  gtk_box_append(GTK_BOX(right), drawing_area);

  GtkWidget *scr = gtk_scrolled_window_new();
  gtk_widget_set_size_request(scr, -1, 120);
  text_log = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_log), FALSE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scr), text_log);
  gtk_box_append(GTK_BOX(right), scr);

  return all;
}
