#include "app.h"

static void activate(GtkApplication *app, gpointer user_data) {
  AppContext *ctx = g_new0(AppContext, 1);
  ctx->app = app;

  ctx->window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(ctx->window), "Menu Principal-SD");
  gtk_window_set_default_size(GTK_WINDOW(ctx->window), 1280, 800);
  gtk_window_maximize(
      GTK_WINDOW(ctx->window)); // Maximized window with close button

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, CSS_STYLE, -1);
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Main Stack (View Switcher)
  ctx->stack = gtk_stack_new();
  gtk_stack_set_transition_type(GTK_STACK(ctx->stack),
                                GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);

  // Create Views
  ctx->menu_view = create_menu_view(ctx);
  ctx->sorting_view = create_sorting_view(ctx);
  ctx->list_view = create_list_view(ctx);
  ctx->tree_view = create_tree_view(ctx);
  ctx->graph_view = create_graph_view(ctx);

  gtk_stack_add_named(GTK_STACK(ctx->stack), ctx->menu_view, "menu");
  gtk_stack_add_named(GTK_STACK(ctx->stack), ctx->sorting_view, "sorting");
  gtk_stack_add_named(GTK_STACK(ctx->stack), ctx->list_view, "list");
  gtk_stack_add_named(GTK_STACK(ctx->stack), ctx->tree_view, "tree");
  gtk_stack_add_named(GTK_STACK(ctx->stack), ctx->graph_view, "graph");

  gtk_window_set_child(GTK_WINDOW(ctx->window), ctx->stack);
  gtk_window_present(GTK_WINDOW(ctx->window));
}

int main(int argc, char **argv) {
  GtkApplication *app;
  int status;

  srand(time(NULL)); // Init Random

  app = gtk_application_new("com.example.visualizer", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);

  return status;
}
