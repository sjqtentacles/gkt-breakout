#include <gtk/gtk.h>
#include <stdbool.h>
#include <math.h>
#include <cairo.h>
#include <time.h>

#define MAX_BRICKS 40
#define BRICK_ROWS 4
#define BRICK_COLS 10

typedef struct {
    GtkWidget *widget;
    bool active;
    int x;
    int y;
    int width;
    int height;
    int hits_required;
    GdkRGBA color;
} Brick;

typedef struct {
    GtkWidget *window;
GtkWidget *ball;
GtkWidget *paddle;
GtkWidget *fixed;
    GtkWidget *score_label;
    GtkWidget *level_label;
    int ball_x;
    int ball_y;
    int paddle_x;
    int paddle_y;
    int paddle_width;
    int paddle_height;
    int dx;
    int dy;
    int width;
    int height;
    bool game_running;
    bool key_left;
    bool key_right;
    int score;
    int level;
    double paddle_glow;
    bool paddle_hit;
    Brick bricks[MAX_BRICKS];
    int brick_count;
    int bricks_remaining;
} GameState;

static GameState game;

// Custom drawing for the paddle
static gboolean draw_paddle(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GameState *g = (GameState *)data;
    
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    double width = allocation.width;
    double height = allocation.height;
    
    // Draw paddle with metallic gradient
    cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, 0, height);
    
    // Metallic blue color with highlight based on paddle_glow
    double glow_factor = g->paddle_hit ? g->paddle_glow : 0.0;
    
    // Base color
    cairo_pattern_add_color_stop_rgb(gradient, 0.0, 0.2 + glow_factor, 0.4 + glow_factor, 0.8 + glow_factor * 0.2);
    cairo_pattern_add_color_stop_rgb(gradient, 0.3, 0.3 + glow_factor, 0.5 + glow_factor, 0.9 + glow_factor * 0.1);
    cairo_pattern_add_color_stop_rgb(gradient, 0.6, 0.2 + glow_factor, 0.4 + glow_factor, 0.7 + glow_factor * 0.3);
    cairo_pattern_add_color_stop_rgb(gradient, 1.0, 0.1 + glow_factor, 0.3 + glow_factor, 0.6 + glow_factor * 0.4);
    
    // Draw rounded rectangle
    double radius = height / 2.0;
    double x = 0;
    double y = 0;
    
    // Top-left corner
    cairo_move_to(cr, x + radius, y);
    
    // Top-right corner
    cairo_arc(cr, x + width - radius, y + radius, radius, M_PI * 1.5, M_PI * 2);
    
    // Bottom-right corner
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, M_PI * 0.5);
    
    // Bottom-left corner
    cairo_arc(cr, x + radius, y + height - radius, radius, M_PI * 0.5, M_PI);
    
    // Back to start
    cairo_arc(cr, x + radius, y + radius, radius, M_PI, M_PI * 1.5);
    
    // Fill with gradient
    cairo_set_source(cr, gradient);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(gradient);
    
    // Add a subtle border
    cairo_set_source_rgba(cr, 0.8, 0.9, 1.0, 0.6);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
    
    // Add a highlight
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, x + radius + 5, y + 2);
    cairo_line_to(cr, x + width - radius - 5, y + 2);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.4 + glow_factor * 0.6);
    cairo_stroke(cr);
    
    return FALSE;
}

static gboolean update_paddle_glow(gpointer data) {
    GameState *g = (GameState *)data;
    
    if (!g || !g->game_running)
        return G_SOURCE_REMOVE;
        
    if (g->paddle_hit) {
        // Fade out glow effect
        g->paddle_glow -= 0.05;
        if (g->paddle_glow <= 0.0) {
            g->paddle_glow = 0.0;
            g->paddle_hit = false;
        }
        
        // Redraw paddle
        if (GTK_IS_WIDGET(g->paddle))
            gtk_widget_queue_draw(g->paddle);
    }
    
    return G_SOURCE_CONTINUE;
}

static gboolean update_paddle(gpointer data) {
    GameState *g = (GameState *)data;
    
    if (!g || !g->game_running || !GTK_IS_WIDGET(g->paddle) || !GTK_IS_FIXED(g->fixed)) {
        return G_SOURCE_REMOVE;
    }
    
    // Calculate acceleration-based movement
    static double velocity = 0;
    double max_speed = 12.0;
    double acceleration = 1.0;
    double friction = 0.9;
    
    if (g->key_left && !g->key_right) {
        velocity -= acceleration;
        if (velocity < -max_speed) velocity = -max_speed;
    } else if (g->key_right && !g->key_left) {
        velocity += acceleration;
        if (velocity > max_speed) velocity = max_speed;
    } else {
        // Apply friction to slow down
        velocity *= friction;
        if (fabs(velocity) < 0.1) velocity = 0;
    }
    
    // Apply velocity
    g->paddle_x += (int)velocity;
    
    // Boundary checks
    if (g->paddle_x < 0) {
        g->paddle_x = 0;
        velocity = 0; // Stop when hitting wall
    } else if (g->paddle_x > g->width - g->paddle_width) {
        g->paddle_x = g->width - g->paddle_width;
        velocity = 0; // Stop when hitting wall
    }
    
    // Update paddle position
    gtk_fixed_move(GTK_FIXED(g->fixed), g->paddle, g->paddle_x, g->paddle_y);
    
    return G_SOURCE_CONTINUE;
}

static void generate_level(GameState *g) {
    // Clear old bricks
    for (int i = 0; i < g->brick_count; i++) {
        if (GTK_IS_WIDGET(g->bricks[i].widget)) {
            gtk_widget_destroy(g->bricks[i].widget);
        }
    }
    
    // Reset brick count
    g->brick_count = 0;
    g->bricks_remaining = 0;
    
    // Brick dimensions and spacing
    int spacing_x = 8;  // Increased horizontal spacing
    int spacing_y = 10; // Increased vertical spacing
    int brick_width = (g->width - 40 - (BRICK_COLS - 1) * spacing_x) / BRICK_COLS; // Adjusted width calculation
    int brick_height = 25;
    int start_y = 60; // Adjusted start position slightly
    
    // Set up colors for different rows
    GdkRGBA colors[BRICK_ROWS] = {
        {1.0, 0.8, 0.2, 1.0}, // Yellow
        {1.0, 0.8, 0.2, 1.0}, // Yellow
        {1.0, 0.8, 0.2, 1.0}, // Yellow
        {1.0, 0.8, 0.2, 1.0}  // Yellow
    };
    
    // Create bricks
    for (int row = 0; row < BRICK_ROWS; row++) {
        int y = start_y + row * (brick_height + spacing_y);
        
        for (int col = 0; col < BRICK_COLS; col++) {
            int x = 20 + col * (brick_width + spacing_x);
            
            // Create brick button
            char brick_name[20];
            sprintf(brick_name, "brick_%d_%d", row, col);
            
            g->bricks[g->brick_count].widget = gtk_button_new();
            g->bricks[g->brick_count].active = true;
            g->bricks[g->brick_count].x = x;
            g->bricks[g->brick_count].y = y;
            g->bricks[g->brick_count].width = brick_width;
            g->bricks[g->brick_count].height = brick_height;
            g->bricks[g->brick_count].color = colors[row];
            
            // More hits required for higher rows
            g->bricks[g->brick_count].hits_required = g->level > 1 ? BRICK_ROWS - row : 1;
            
            // Style the brick
            gtk_widget_set_name(g->bricks[g->brick_count].widget, brick_name);
            gtk_widget_set_size_request(g->bricks[g->brick_count].widget, brick_width, brick_height);
            
            // Set brick color using CSS
            GtkCssProvider *provider = gtk_css_provider_new();
            char css[250]; // Increased size for shadow
            sprintf(css, 
                "#%s { background-color: rgba(%.1f, %.1f, %.1f, %.1f); "
                "border-radius: 3px; border: none; "
                "box-shadow: 2px 2px 4px rgba(0, 0, 0, 0.4); }",
                brick_name, 
                colors[row].red, colors[row].green, colors[row].blue, colors[row].alpha);
            
            gtk_css_provider_load_from_data(provider, css, -1, NULL);
            GtkStyleContext *context = gtk_widget_get_style_context(g->bricks[g->brick_count].widget);
            gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
            g_object_unref(provider);
            
            // Add brick to layout
            gtk_fixed_put(GTK_FIXED(g->fixed), g->bricks[g->brick_count].widget, x, y);
            
            g->brick_count++;
            g->bricks_remaining++;
        }
    }
    
    // Update level label
    char level_text[20];
    sprintf(level_text, "Level: %d", g->level);
    gtk_label_set_text(GTK_LABEL(g->level_label), level_text);
    
    // Show all bricks
    gtk_widget_show_all(g->fixed);
}

static void check_brick_collision(GameState *g) {
    for (int i = 0; i < g->brick_count; i++) {
        Brick *brick = &g->bricks[i];
        
        if (!brick->active || !GTK_IS_WIDGET(brick->widget))
            continue;
        
        // Check if ball collides with this brick
        if (g->ball_x + 20 >= brick->x && g->ball_x <= brick->x + brick->width &&
            g->ball_y + 20 >= brick->y && g->ball_y <= brick->y + brick->height) {
            
            // Determine collision direction (top/bottom vs left/right)
            int center_x = brick->x + brick->width / 2;
            int center_y = brick->y + brick->height / 2;
            int ball_center_x = g->ball_x + 10;
            int ball_center_y = g->ball_y + 10;
            
            float dx = abs(center_x - ball_center_x) / (brick->width / 2.0);
            float dy = abs(center_y - ball_center_y) / (brick->height / 2.0);
            
            // Reduce hits remaining
            brick->hits_required--;
            
            if (brick->hits_required <= 0) {
                // Brick destroyed
                brick->active = false;
                g->bricks_remaining--;
                gtk_widget_hide(brick->widget);
                
                // Add points based on position (higher rows = more points)
                int row = i / BRICK_COLS;
                g->score += (BRICK_ROWS - row) * 10 * g->level;
            } else {
                // Brick damaged - make it more transparent
                GtkCssProvider *provider = gtk_css_provider_new();
                char brick_name[20];
                const gchar *name = gtk_widget_get_name(brick->widget);
                strncpy(brick_name, name, sizeof(brick_name)-1);
                brick_name[sizeof(brick_name)-1] = '\0';
                
                char css[250]; // Increased size for shadow
                float alpha = 0.5f + (0.5f * brick->hits_required / (float)BRICK_ROWS);
                sprintf(css, 
                    "#%s { background-color: rgba(%.1f, %.1f, %.1f, %.1f); "
                    "border-radius: 3px; border: none; "
                    "box-shadow: 2px 2px 4px rgba(0, 0, 0, 0.4); }",
                    brick_name, 
                    brick->color.red, brick->color.green, brick->color.blue, alpha);
                
                gtk_css_provider_load_from_data(provider, css, -1, NULL);
                GtkStyleContext *context = gtk_widget_get_style_context(brick->widget);
                gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider), 
                    GTK_STYLE_PROVIDER_PRIORITY_USER);
                g_object_unref(provider);
            }
            
            // Update score display
            char score_text[32];
            snprintf(score_text, sizeof(score_text), "Score: %d", g->score);
            gtk_label_set_text(GTK_LABEL(g->score_label), score_text);
            
            // Bounce based on collision direction
            if (dx > dy) {
                // Left or right collision
                g->dx = -g->dx;
            } else {
                // Top or bottom collision
                g->dy = -g->dy;
            }
            
            // To prevent multiple brick collisions in same frame
            break;
        }
    }
    
    // Check if level completed
    if (g->bricks_remaining <= 0) {
        // Level up!
        g->level++;
        
        // Reset ball position
        g->ball_x = g->width / 2;
        g->ball_y = g->height / 2;
        g->dx = 4 + g->level;  // Increase speed with level
        g->dy = 4 + g->level;
        
        // Generate new level
        generate_level(g);
    }
}

static gboolean update_ball(gpointer data) {
    GameState *g = (GameState *)data;
    
    // Safety checks
    if (!g || !g->game_running || !GTK_IS_WIDGET(g->ball) || !GTK_IS_WIDGET(g->paddle) || !GTK_IS_FIXED(g->fixed)) {
        return G_SOURCE_REMOVE;
    }
    
    // Calculate current velocity (for speed maintenance)
    double current_speed = sqrt(g->dx * g->dx + g->dy * g->dy);
    
    g->ball_x += g->dx;
    g->ball_y += g->dy;

    // Bounce off walls
    if (g->ball_x <= 0 || g->ball_x >= g->width - 20) {
        g->dx = -g->dx;
        // Ensure ball stays in bounds
        g->ball_x = CLAMP(g->ball_x, 0, g->width - 20);
    }
    
    if (g->ball_y <= 0) {
        g->dy = -g->dy;
        g->ball_y = 0;
    }

    // Bounce off paddle (improved collision detection)
    if (g->ball_y >= g->paddle_y - 20 && g->ball_y <= g->paddle_y &&
        g->ball_x >= g->paddle_x && g->ball_x <= g->paddle_x + g->paddle_width) {
        
        // Create paddle hit visual effect
        g->paddle_hit = true;
        g->paddle_glow = 0.4; // Start glow effect
        
        g->dy = -g->dy;
        g->ball_y = g->paddle_y - 20; // Position just above paddle
        
        // Add some angle based on where the ball hits the paddle
        float relative_pos = (float)(g->ball_x - g->paddle_x) / g->paddle_width;
        
        // More dynamic angle calculation
        if (relative_pos < 0.2) {
            // Left edge - sharp angle
            g->dx = -5 - (rand() % 3);
        } else if (relative_pos < 0.4) {
            // Left side - moderate angle
            g->dx = -3 - (rand() % 3);
        } else if (relative_pos > 0.8) {
            // Right edge - sharp angle
            g->dx = 5 + (rand() % 3);
        } else if (relative_pos > 0.6) {
            // Right side - moderate angle
            g->dx = 3 + (rand() % 3);
        } else {
            // Center - ensure ball doesn't slow down too much
            g->dx = ((rand() % 5) - 2);
            // Ensure minimum horizontal velocity for center hits
            if (abs(g->dx) < 2) {
                g->dx = (g->dx < 0) ? -2 : 2;
            }
        }
        
        // Ensure overall speed is maintained at minimum threshold
        double new_speed = sqrt(g->dx * g->dx + g->dy * g->dy);
        if (new_speed < 5.0) {
            // Scale up to minimum speed while keeping direction
            double scale = 5.0 / new_speed;
            g->dx *= scale;
            g->dy *= scale;
        }
    }

    // Check for brick collisions
    check_brick_collision(g);

    // Reset if ball falls off
    if (g->ball_y > g->height) {
        g->ball_x = g->width / 2;
        g->ball_y = g->height / 2;
        
        // Better randomization with minimum speed guarantee
        g->dx = 4 - (rand() % 9); // Random starting direction
        
        // Ensure minimum horizontal velocity
        if (abs(g->dx) < 2) {
            g->dx = (g->dx < 0) ? -2 : 2;
        }
        
        g->dy = 4;
    }

    // Update ball position safely
    if (GTK_IS_FIXED(g->fixed) && GTK_IS_WIDGET(g->ball)) {
        gtk_fixed_move(GTK_FIXED(g->fixed), g->ball, g->ball_x, g->ball_y);
    }
    
    return G_SOURCE_CONTINUE;
}

static gboolean key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    GameState *g = (GameState *)data;
    
    switch (event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_a:
            g->key_left = true;
            break;
            
        case GDK_KEY_Right:
        case GDK_KEY_d:
            g->key_right = true;
            break;
            
        case GDK_KEY_space:
            // Could be used for pause/resume
            break;
            
        case GDK_KEY_Escape:
            // Let GtkApplication handle quit when window is destroyed
            if (GTK_IS_WINDOW(g->window)) {
                gtk_widget_destroy(g->window);
            }
            break;
    }
    
    return TRUE;
}

static gboolean key_release_event(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    GameState *g = (GameState *)data;
    
    switch (event->keyval) {
        case GDK_KEY_Left:
        case GDK_KEY_a:
            g->key_left = false;
            break;
            
        case GDK_KEY_Right:
        case GDK_KEY_d:
            g->key_right = false;
            break;
    }
    
    return TRUE;
}

static void activate(GtkApplication *app, gpointer user_data) {
    // Set random seed
    srand(time(NULL));
    
    // Initialize game state
    game.width = 600;
    game.height = 500;
    game.ball_x = game.width / 2 - 10; // Center horizontally (minus half ball width)
    game.ball_y = game.height / 2 - 10; // Center vertically (minus half ball height)
    game.dx = 4;
    game.dy = 3;
    game.paddle_width = 100;
    game.paddle_height = 20;
    game.paddle_x = (game.width - game.paddle_width) / 2;
    game.paddle_y = game.height - 40;
    game.game_running = true;
    game.key_left = false;
    game.key_right = false;
    game.score = 0;
    game.level = 1;
    game.paddle_glow = 0.0;
    game.paddle_hit = false;
    game.brick_count = 0;
    game.bricks_remaining = 0;
    
    // Create window
    game.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(game.window), "GTK Breakout Game");
    gtk_window_set_default_size(GTK_WINDOW(game.window), game.width, game.height);
    
    // Create layout container with CSS styling for background
    game.fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(game.window), game.fixed);
    gtk_widget_set_name(game.fixed, "game_area");
    
    // Create blue gradient background via CSS
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "#game_area { background-image: linear-gradient(to bottom, #000428, #004e92); }", -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(game.fixed);
    gtk_style_context_add_provider(context,
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(provider);
    
    // Add score label
    game.score_label = gtk_label_new("Score: 0");
    gtk_widget_set_size_request(game.score_label, 100, 20);
    gtk_fixed_put(GTK_FIXED(game.fixed), game.score_label, 10, 10);
    
    // Add level label
    game.level_label = gtk_label_new("Level: 1");
    gtk_widget_set_size_request(game.level_label, 100, 20);
    gtk_fixed_put(GTK_FIXED(game.fixed), game.level_label, game.width - 110, 10);
    
    // Style the labels
    GtkCssProvider *label_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(label_provider,
        "label#score_label { color: black; font-weight: bold; }"
        "label#level_label { color: white; font-weight: bold; }", -1, NULL);
    
    // Set IDs for the labels
    gtk_widget_set_name(game.score_label, "score_label");
    gtk_widget_set_name(game.level_label, "level_label");
    
    gtk_style_context_add_provider(gtk_widget_get_style_context(game.score_label),
        GTK_STYLE_PROVIDER(label_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    gtk_style_context_add_provider(gtk_widget_get_style_context(game.level_label),
        GTK_STYLE_PROVIDER(label_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(label_provider);
    
    // Create visible ball with 'O' character
    game.ball = gtk_label_new("⚪");
    gtk_widget_set_size_request(game.ball, 20, 20);
    
    // Make the ball more visible with CSS
    gtk_widget_set_name(game.ball, "ball");
    GtkCssProvider *ball_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(ball_provider,
        "#ball { color: white; font-size: 16px; font-weight: bold; }", -1, NULL);
    GtkStyleContext *ball_context = gtk_widget_get_style_context(game.ball);
    gtk_style_context_add_provider(ball_context,
        GTK_STYLE_PROVIDER(ball_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(ball_provider);
    
    gtk_fixed_put(GTK_FIXED(game.fixed), game.ball, game.ball_x, game.ball_y);
    
    // Create a better paddle with custom drawing
    game.paddle = gtk_drawing_area_new();
    gtk_widget_set_size_request(game.paddle, game.paddle_width, game.paddle_height);
    gtk_widget_set_name(game.paddle, "paddle");
    
    // Connect custom drawing function for the paddle
    g_signal_connect(G_OBJECT(game.paddle), "draw", G_CALLBACK(draw_paddle), &game);
    
    gtk_fixed_put(GTK_FIXED(game.fixed), game.paddle, game.paddle_x, game.paddle_y);
    
    // Set up keyboard handling
    gtk_widget_add_events(game.window, GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK);
    g_signal_connect(game.window, "key-press-event", G_CALLBACK(key_press_event), &game);
    g_signal_connect(game.window, "key-release-event", G_CALLBACK(key_release_event), &game);
    
    // Handle window close event - GtkApplication handles this automatically
    // g_signal_connect(game.window, "destroy", G_CALLBACK(gtk_main_quit), NULL); // Removed
    
    // Generate bricks
    generate_level(&game);
    
    // Start the game loops
    g_timeout_add(16, update_ball, &game);          // ~60 FPS
    g_timeout_add(16, update_paddle, &game);        // Paddle update
    g_timeout_add(16, update_paddle_glow, &game);   // Paddle glow effect
    
    // Show all widgets
    gtk_widget_show_all(game.window);
}

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new("com.example.WidgetBreakout", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
