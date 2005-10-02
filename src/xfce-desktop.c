/*
 *  xfdesktop - xfce4's desktop manager
 *
 *  Copyright (c) 2004-2005 Brian Tarricone, <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  Random portions taken from or inspired by the original xfdesktop for xfce4:
 *     Copyright (C) 2002-2003 Jasper Huijsmans (huysmans@users.sourceforge.net)
 *     Copyright (C) 2003 Benedikt Meurer <benedikt.meurer@unix-ag.uni-siegen.de>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#include <ctype.h>
#include <errno.h>

#ifdef HAVE_TIME_H
#include <time.h>
#endif

#include <glib.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <pango/pango.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include "xfce-desktop.h"
#include "xfdesktop-common.h"
#include "main.h"


/********
 * TODO *
 ********
 * + screen size changes
 * + theme/font changes
 * + DnD to move stuff around
 */


#ifdef ENABLE_WINDOW_ICONS

#define ICON_SIZE     32
#define CELL_SIZE     112
#define TEXT_WIDTH    100
#define CELL_PADDING  6
#define SPACING       8
#define SCREEN_MARGIN 16

typedef struct _XfceDesktopIcon
{
    guint16 row;
    guint16 col;
    GdkPixbuf *pix;
    gchar *label;
    GdkRectangle extents;
    NetkWindow *window;
} XfceDesktopIcon;

typedef struct _XfceDesktopIconWorkspace
{
    GHashTable *icons;
    XfceDesktopIcon *selected_icon;
    gint xorigin,
         yorigin,
         width,
         height;
    guint16 nrows;
    guint16 ncols;
    guint16 lowest_free_row;
    guint16 lowest_free_col;
} XfceDesktopIconWorkspace;

typedef struct
{
    XfceDesktop *desktop;
    gpointer data;
} IconForeachData;

#endif /* defined(ENABLE_WINDOW_ICONS) */

struct _XfceDesktopPriv
{
    GdkScreen *gscreen;
    McsClient *mcs_client;
    
    GdkPixmap *bg_pixmap;
    
    guint nbackdrops;
    XfceBackdrop **backdrops;
    
#ifdef ENABLE_WINDOW_ICONS
    gboolean use_window_icons;
    
    NetkScreen *netk_screen;
    gint cur_ws_num;
    PangoLayout *playout;
    
    XfceDesktopIconWorkspace **icon_workspaces;
#endif
};

#ifdef ENABLE_WINDOW_ICONS
static void xfce_desktop_icon_paint(XfceDesktop *desktop, XfceDesktopIcon *icon);
static void xfce_desktop_icon_add(XfceDesktop *desktop, NetkWindow *window, guint idx);
static void xfce_desktop_icon_remove(XfceDesktop *desktop, XfceDesktopIcon *icon, NetkWindow *window, guint idx);
static void xfce_desktop_icon_free(XfceDesktopIcon *icon);
static void xfce_desktop_setup_icons(XfceDesktop *desktop);
static void xfce_desktop_unsetup_icons(XfceDesktop *desktop);
static void xfce_desktop_paint_icons(XfceDesktop *desktop, GdkRectangle *area);
static gboolean xfce_desktop_button_press(GtkWidget *widget, GdkEventButton *evt, gpointer user_data);
static gboolean xfce_desktop_button_release(GtkWidget *widget, GdkEventButton *evt, gpointer user_data);
/* utility funcs */
static gboolean get_next_free_position(XfceDesktop *desktop, guint idx, guint16 *row, guint16 *col);
static gboolean grid_is_free_position(XfceDesktop *desktop, gint idx, guint16 row, guint16 col);
static void determine_next_free_position(XfceDesktop *desktop, guint idx);
#endif

static void xfce_desktop_class_init(XfceDesktopClass *klass);
static void xfce_desktop_init(XfceDesktop *desktop);
static void xfce_desktop_finalize(GObject *object);
static void xfce_desktop_realize(GtkWidget *widget);
static void xfce_desktop_unrealize(GtkWidget *widget);

static gboolean xfce_desktop_expose(GtkWidget *w, GdkEventExpose *evt, gpointer user_data);

static void load_initial_settings(XfceDesktop *desktop, McsClient *mcs_client);


GtkWindowClass *parent_class = NULL;


#ifdef ENABLE_WINDOW_ICONS

static void
xfce_desktop_icon_free(XfceDesktopIcon *icon)
{
    if(icon->pix)
        g_object_unref(icon->pix);
    if(icon->label)
        g_free(icon->label);
    g_free(icon);
}

static void
xfce_desktop_icon_add(XfceDesktop *desktop,
                      NetkWindow *window,
                      guint idx)
{
    XfceDesktopIcon *icon = g_new0(XfceDesktopIcon, 1);
    gchar data_name[256];
    guint16 old_row, old_col;
    gboolean got_pos = FALSE;
    NetkWorkspace *active_ws;
            
    /* check for availability of old position (if any) */
    g_snprintf(data_name, 256, "xfdesktop-last-row-%d", idx);
    old_row = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                                 data_name));
    g_snprintf(data_name, 256, "xfdesktop-last-col-%d", idx);
    old_col = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                                 data_name));
    if(old_row && old_col) {
        icon->row = old_row - 1;
        icon->col = old_col - 1;
    
        if(grid_is_free_position(desktop, idx, icon->row, icon->col)) {
            DBG("old position (%d,%d) is free", icon->row, icon->col);
            got_pos = TRUE;
        }
    }
    
    if(!got_pos) {
       if(!get_next_free_position(desktop, idx, &icon->row, &icon->col)) {
           g_free(icon);
           return;
       } else {
           DBG("old position didn't exist or isn't free, got (%d,%d) instead", icon->row, icon->col);
       }
    }

    determine_next_free_position(desktop, idx);
    
    DBG("set next free position to (%d,%d)",
        desktop->priv->icon_workspaces[idx]->lowest_free_row,
        desktop->priv->icon_workspaces[idx]->lowest_free_col);
    
    icon->pix = netk_window_get_icon(window);
    if(icon->pix) {
        if(gdk_pixbuf_get_width(icon->pix) != ICON_SIZE) {
            icon->pix = gdk_pixbuf_scale_simple(icon->pix,
                                                ICON_SIZE,
                                                ICON_SIZE,
                                                GDK_INTERP_BILINEAR);
        }
        g_object_ref(G_OBJECT(icon->pix));
    }
    icon->label = g_strdup(netk_window_get_name(window));
    icon->window = window;
    g_hash_table_insert(desktop->priv->icon_workspaces[idx]->icons,
                        window, icon);
    
    active_ws = netk_screen_get_active_workspace(desktop->priv->netk_screen);
    if(idx == netk_workspace_get_number(active_ws))
        xfce_desktop_icon_paint(desktop, icon);
}

static void
xfce_desktop_icon_remove(XfceDesktop *desktop,
                         XfceDesktopIcon *icon,
                         NetkWindow *window,
                         guint idx)
{
    GdkRectangle area;
    guint16 row, col;
    gchar data_name[256];
    NetkWorkspace *active_ws;
    gint active_ws_num;
    
    active_ws = netk_screen_get_active_workspace(desktop->priv->netk_screen);
    active_ws_num = netk_workspace_get_number(active_ws);
    
    row = icon->row;
    col = icon->col;
    memcpy(&area, &icon->extents, sizeof(area));
    
    if(desktop->priv->icon_workspaces[idx]->selected_icon == icon)
        desktop->priv->icon_workspaces[idx]->selected_icon = NULL;
    
    g_hash_table_remove(desktop->priv->icon_workspaces[idx]->icons,
                        window);
    
    if(idx == active_ws_num) {
        DBG("clearing %dx%d+%d+%d", area.width, area.height,
            area.x, area.y);
        gdk_window_clear_area(GTK_WIDGET(desktop)->window,
                              area.x, area.y,
                              area.width, area.height);
    }
    
    /* save the old positions for later */
    g_snprintf(data_name, 256, "xfdesktop-last-row-%d", idx);
    g_object_set_data(G_OBJECT(window), data_name,
                      GUINT_TO_POINTER(row+1));
    g_snprintf(data_name, 256, "xfdesktop-last-col-%d", idx);
    g_object_set_data(G_OBJECT(window), data_name,
                      GUINT_TO_POINTER(col+1));
    
    if((desktop->priv->icon_workspaces[idx]->lowest_free_col == col
        && row < desktop->priv->icon_workspaces[idx]->lowest_free_row)
       || col < desktop->priv->icon_workspaces[idx]->lowest_free_col)
    {
        desktop->priv->icon_workspaces[idx]->lowest_free_row = row;
        desktop->priv->icon_workspaces[idx]->lowest_free_col = col;
    }
}

static gboolean
find_icon_below_from_hash(gpointer key,
                          gpointer value,
                          gpointer user_data)
{
    XfceDesktopIcon *icon = user_data;
    XfceDesktopIcon *icon_maybe_below = value;
    
    if(icon_maybe_below->row == icon->row + 1)
        return TRUE;
    else
        return FALSE;
}

static XfceDesktopIcon *
find_icon_below(XfceDesktop *desktop,
                XfceDesktopIcon *icon)
{
    XfceDesktopIcon *icon_below = NULL;
    NetkWorkspace *active_ws;
    gint active_ws_num;
    
    active_ws = netk_screen_get_active_workspace(desktop->priv->netk_screen);
    active_ws_num = netk_workspace_get_number(active_ws);
    
    if(icon->row == desktop->priv->icon_workspaces[active_ws_num]->nrows - 1)
        return NULL;
    
    icon_below = g_hash_table_find(desktop->priv->icon_workspaces[active_ws_num]->icons,
                                   find_icon_below_from_hash, icon);
    
    return icon_below;
}

static void
xfce_desktop_icon_paint(XfceDesktop *desktop,
                        XfceDesktopIcon *icon)
{
    GtkWidget *widget = GTK_WIDGET(desktop);
    gint pix_w, pix_h, pix_x, pix_y, text_x, text_y, text_w, text_h,
         cell_x, cell_y, state, active_ws_num;
    PangoLayout *playout;
    GdkRectangle area;
    
    TRACE("entering");
    
    active_ws_num = netk_workspace_get_number(netk_screen_get_active_workspace(desktop->priv->netk_screen));
    
    if(icon->extents.width > 0 && icon->extents.height > 0) {
        /* FIXME: this is really only needed when going from selected <-> not
         * selected.  should fix for optimisation. */
        gdk_window_clear_area(widget->window, icon->extents.x, icon->extents.y,
                              icon->extents.width, icon->extents.height);
        
        /* check and make sure we didn't used to be too large for the cell.
         * if so, repaint the one below it first. */
        if(icon->extents.height + 3 * CELL_PADDING > CELL_SIZE) {
            XfceDesktopIcon *icon_below = find_icon_below(desktop, icon);
            if(icon_below)
                xfce_desktop_icon_paint(desktop, icon_below);
        }
    }
    
    if(desktop->priv->icon_workspaces[desktop->priv->cur_ws_num]->selected_icon == icon)
        state = GTK_STATE_SELECTED;
    else
        state = GTK_STATE_NORMAL;
    
    pix_w = gdk_pixbuf_get_width(icon->pix);
    pix_h = gdk_pixbuf_get_height(icon->pix);
    
    playout = desktop->priv->playout;
    pango_layout_set_alignment(playout, PANGO_ALIGN_CENTER);
    pango_layout_set_width(playout, -1);
    pango_layout_set_text(playout, icon->label, -1);
    pango_layout_get_size(playout, &text_w, &text_h);
    if(text_w > TEXT_WIDTH * PANGO_SCALE) {
        pango_layout_set_width(playout, TEXT_WIDTH * PANGO_SCALE);
        if(state == GTK_STATE_NORMAL) {
#if GTK_CHECK_VERSION(2, 6, 0)  /* can't find a way to get pango version info */
            pango_layout_set_ellipsize(playout, PANGO_ELLIPSIZE_END);
#endif
        } else {
            pango_layout_set_wrap(playout, PANGO_WRAP_WORD_CHAR);
#if GTK_CHECK_VERSION(2, 6, 0)  /* can't find a way to get pango version info */
            pango_layout_set_ellipsize(playout, PANGO_ELLIPSIZE_NONE);
#endif
        }
    }
    pango_layout_get_pixel_size(playout, &text_w, &text_h);
    
    cell_x = desktop->priv->icon_workspaces[active_ws_num]->xorigin;
    cell_x += icon->col * CELL_SIZE + CELL_PADDING;
    cell_y = desktop->priv->icon_workspaces[active_ws_num]->yorigin;
    cell_y += icon->row * CELL_SIZE + CELL_PADDING;
    
    pix_x = cell_x + ((CELL_SIZE - 2 * CELL_PADDING) - pix_w) / 2;
    pix_y = cell_y + 2 * CELL_PADDING;
    
    /*
    DBG("computing text_x:\n\tcell_x=%d\n\tcell width: %d\n\ttext_w: %d\n\tnon-text space: %d\n\tdiv 2: %d",
        cell_x,
        CELL_SIZE - 2 * CELL_PADDING,
        text_w,
        ((CELL_SIZE - 2 * CELL_PADDING) - text_w),
        ((CELL_SIZE - 2 * CELL_PADDING) - text_w) / 2);
    */
    
    text_x = cell_x + ((CELL_SIZE - 2 * CELL_PADDING) - text_w) / 2;
    text_y = cell_y + 2 * CELL_PADDING + pix_h + SPACING + 2;
    
    DBG("drawing pixbuf at (%d,%d)", pix_x, pix_y);
    
    gdk_draw_pixbuf(GDK_DRAWABLE(widget->window), widget->style->black_gc,
                    icon->pix, 0, 0, pix_x, pix_y, pix_w, pix_h,
                    GDK_RGB_DITHER_NORMAL, 0, 0);
    
    DBG("painting layout: area: %dx%d+%d+%d", text_w, text_h, text_x, text_y);
    
    area.x = text_x - 2;
    area.y = text_y - 2;
    area.width = text_w + 4;
    area.height = text_h + 4;
    
    gtk_paint_box(widget->style, widget->window, state,
                  GTK_SHADOW_IN, &area, widget, "background",
                  area.x, area.y, area.width, area.height);
    
    gtk_paint_layout(widget->style, widget->window, state, FALSE,
                     &area, widget, "label", text_x, text_y, playout);
    
#if 0 /* debug */
    gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                       widget->style->white_gc,
                       FALSE,
                       cell_x - CELL_PADDING,
                       cell_y - CELL_PADDING,
                       CELL_SIZE,
                       CELL_SIZE);
#endif
    
    icon->extents.x = (pix_w > text_w + 4 ? pix_x : text_x - 2);
    icon->extents.y = cell_y + (2 * CELL_PADDING);
    icon->extents.width = (pix_w > text_w + 4 ? pix_w : text_w + 4);
    icon->extents.height = (text_y + text_h + 2) - icon->extents.y;
}

static void
xfce_desktop_icon_paint_delayed(NetkWindow *window,
                                NetkWindowState changed_mask,
                                NetkWindowState new_state,
                                gpointer user_data)
{
    IconForeachData *ifed = user_data;
    
    DBG("repainting under icon");
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(window),
                                         G_CALLBACK(xfce_desktop_icon_paint_delayed),
                                         ifed);
    
    xfce_desktop_icon_paint(ifed->desktop, (XfceDesktopIcon *)ifed->data);
    g_free(ifed);
}

static gboolean
xfce_desktop_icon_paint_idled(gpointer user_data)
{
    IconForeachData *ifed = user_data;
    
    xfce_desktop_icon_paint(ifed->desktop, (XfceDesktopIcon *)ifed->data);
    g_free(ifed);
    
    return FALSE;
}

#endif /* defined(ENABLE_WINDOW_ICONS) */

/* private functions */
    
static void
set_imgfile_root_property(XfceDesktop *desktop, const gchar *filename,
        gint monitor)
{
    gchar property_name[128];
    
    g_snprintf(property_name, 128, XFDESKTOP_IMAGE_FILE_FMT, monitor);
    gdk_property_change(gdk_screen_get_root_window(desktop->priv->gscreen),
                gdk_atom_intern(property_name, FALSE),
                gdk_x11_xatom_to_atom(XA_STRING), 8, GDK_PROP_MODE_REPLACE,
                (guchar *)filename, strlen(filename)+1);
}

static void
save_list_file_minus_one(const gchar *filename, const gchar **files, gint badi)
{
    FILE *fp;
    gint fd, i;

#ifdef O_EXLOCK
    if((fd = open (filename, O_CREAT|O_EXLOCK|O_TRUNC|O_WRONLY, 0640)) < 0) {
#else
    if((fd = open (filename, O_CREAT| O_TRUNC|O_WRONLY, 0640)) < 0) {
#endif
        xfce_err (_("Could not save file %s: %s\n\n"
                "Please choose another location or press "
                "cancel in the dialog to discard your changes"),
                filename, g_strerror(errno));
        return;
    }

    if((fp = fdopen (fd, "w")) == NULL) {
        g_warning ("Unable to fdopen(%s). This should not happen!\n", filename);
        close(fd);
        return;
    }

    fprintf (fp, "%s\n", LIST_TEXT);
    
    for(i = 0; files[i] && *files[i] && *files[i] != '\n'; i++) {
        if(i != badi)
            fprintf(fp, "%s\n", files[i]);
    }
    
    fclose(fp);
}

inline gint
count_elements(const gchar **list)
{
    gint i, c = 0;
    
    for(i = 0; list[i]; i++) {
        if(*list[i] && *list[i] != '\n')
            c++;
    }
    
    return c;
}

static const gchar **
get_listfile_contents(const gchar *listfile)
{
    static gchar *prevfile = NULL;
    static gchar **files = NULL;
    static time_t mtime = 0;
    struct stat st;
    
    if(!listfile) {
        if(prevfile) {
            g_free(prevfile);
            prevfile = NULL;
        }
        return NULL;
    }
    
    if(stat(listfile, &st) < 0) {
        if(prevfile) {
            g_free(prevfile);
            prevfile = NULL;
        }
        mtime = 0;
        return NULL;
    }
    
    if(!prevfile || strcmp(listfile, prevfile) || mtime < st.st_mtime) {
        if(files)
            g_strfreev(files);
        if(prevfile)
            g_free(prevfile);
    
        files = get_list_from_file(listfile);
        prevfile = g_strdup(listfile);
        mtime = st.st_mtime;
    }
    
    return (const gchar **)files;
}

static const gchar *
get_path_from_listfile(const gchar *listfile)
{
    static gboolean __initialized = FALSE;
    static gint previndex = -1;
    gint i, n;
    const gchar **files;
    
    /* NOTE: 4.3BSD random()/srandom() are a) stronger and b) faster than
    * ANSI-C rand()/srand(). So we use random() if available
    */
    if (!__initialized)    {
        guint seed = time(NULL) ^ (getpid() + (getpid() << 15));
#ifdef HAVE_SRANDOM
        srandom(seed);
#else
        srand(seed);
#endif
        __initialized = TRUE;
    }
    
    do {
        /* get the contents of the list file */
        files = get_listfile_contents(listfile);
        
        /* if zero or one item, return immediately */
        n = count_elements(files);
        if(!n)
            return NULL;
        else if(n == 1)
            return (const gchar *)files[0];
        
        /* pick a random item */
        do {
#ifdef HAVE_SRANDOM
            i = random() % n;
#else
            i = rand() % n;
#endif
            if(i != previndex) /* same as last time? */
                break;
        } while(1);
        
        g_print("picked i=%d, %s\n", i, files[i]);
        /* validate the image; if it's good, return it */
        if(xfdesktop_check_image_file(files[i]))
            break;
        
        g_print("file not valid, ditching\n");
        
        /* bad image: remove it from the list and write it out */
        save_list_file_minus_one(listfile, files, i);
        previndex = -1;
        /* loop and try again */
    } while(1);
    
    return (const gchar *)files[(previndex = i)];
}

static void
backdrop_changed_cb(XfceBackdrop *backdrop, gpointer user_data)
{
    GtkWidget *desktop = user_data;
    GdkPixbuf *pix;
    GdkPixmap *pmap = NULL;
    GdkColormap *cmap;
    GdkScreen *gscreen;
    GdkRectangle rect;
    Pixmap xid;
    GdkWindow *groot;
    
    TRACE("dummy");
    
    g_return_if_fail(XFCE_IS_DESKTOP(desktop));
    
    /* create/get the composited backdrop pixmap */
    pix = xfce_backdrop_get_pixbuf(backdrop);
    if(!pix)
        return;
    
    gscreen = XFCE_DESKTOP(desktop)->priv->gscreen;
    cmap = gdk_drawable_get_colormap(GDK_DRAWABLE(GTK_WIDGET(desktop)->window));
    
    if(XFCE_DESKTOP(desktop)->priv->nbackdrops == 1) {    
        /* optimised for single monitor: just dump the pixbuf into a pixmap */
        gdk_pixbuf_render_pixmap_and_mask_for_colormap(pix, cmap, &pmap, NULL, 0);
        g_object_unref(G_OBJECT(pix));
        if(!pmap)
            return;
        rect.x = rect.y = 0;
        rect.width = gdk_screen_get_width(gscreen);
        rect.height = gdk_screen_get_height(gscreen);
    } else {
        /* multiple monitors (xinerama): download the current backdrop, paint
         * over the correct area, and upload it back.  this is slow, but
         * probably still faster than redoing the whole thing. */
        GdkPixmap *cur_pmap = NULL;
        GdkPixbuf *cur_pbuf = NULL;
        gint i, n = -1, swidth, sheight;
        
        for(i = 0; i < XFCE_DESKTOP(desktop)->priv->nbackdrops; i++) {
            if(backdrop == XFCE_DESKTOP(desktop)->priv->backdrops[i]) {
                n = i;
                break;
            }
        }
        if(n == -1) {
            g_object_unref(G_OBJECT(pix));
            return;
        }
        
        swidth = gdk_screen_get_width(gscreen);
        sheight = gdk_screen_get_height(gscreen);
        
        cur_pmap = XFCE_DESKTOP(desktop)->priv->bg_pixmap;
        if(cur_pmap) {
            gint pw, ph;
            gdk_drawable_get_size(GDK_DRAWABLE(cur_pmap), &pw, &ph);
            if(pw == swidth && ph == sheight) {
                cur_pbuf = gdk_pixbuf_get_from_drawable(NULL, 
                        GDK_DRAWABLE(cur_pmap), cmap, 0, 0, 0, 0, swidth,
                        sheight);
            } else
                cur_pmap = NULL;
        }
        /* if the style's bg_pixmap was empty, or the above failed... */
        if(!cur_pmap) {
            cur_pbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8,
                    swidth, sheight);
        }
        
        gdk_screen_get_monitor_geometry(gscreen, n, &rect);
        gdk_pixbuf_copy_area(pix, 0, 0, gdk_pixbuf_get_width(pix),
                gdk_pixbuf_get_height(pix), cur_pbuf, rect.x, rect.y);
        g_object_unref(G_OBJECT(pix));
        pmap = NULL;
        gdk_pixbuf_render_pixmap_and_mask_for_colormap(cur_pbuf, cmap,
                &pmap, NULL, 0);
        g_object_unref(G_OBJECT(cur_pbuf));
        if(!pmap)
            return;
    }
    
    xid = GDK_DRAWABLE_XID(pmap);
    groot = gdk_screen_get_root_window(XFCE_DESKTOP(desktop)->priv->gscreen);
    
    gdk_error_trap_push();
    
    /* set root property for transparent Eterms */
    gdk_property_change(groot,
            gdk_atom_intern("_XROOTPMAP_ID", FALSE),
            gdk_atom_intern("PIXMAP", FALSE), 32,
            GDK_PROP_MODE_REPLACE, (guchar *)&xid, 1);
    /* set this other property because someone might need it sometime. */
    gdk_property_change(groot,
            gdk_atom_intern("ESETROOT_PMAP_ID", FALSE),
            gdk_atom_intern("PIXMAP", FALSE), 32,
            GDK_PROP_MODE_REPLACE, (guchar *)&xid, 1);
    /* and set the root window's BG pixmap, because aterm is somewhat lame. */
    gdk_window_set_back_pixmap(groot, pmap, FALSE);
    /* there really should be a standard for this crap... */
    
    /* clear the old pixmap, if any */
    if(XFCE_DESKTOP(desktop)->priv->bg_pixmap)
        g_object_unref(G_OBJECT(XFCE_DESKTOP(desktop)->priv->bg_pixmap));
    
    /* set the new pixmap and tell gtk to redraw it */
    XFCE_DESKTOP(desktop)->priv->bg_pixmap = pmap;
    gdk_window_set_back_pixmap(desktop->window, pmap, FALSE);
    gtk_widget_queue_draw_area(desktop, rect.x, rect.y, rect.width, rect.height);
    
    gdk_error_trap_pop();
}

static void
screen_size_changed_cb(GdkScreen *gscreen, gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    gint w, h, i;
    GdkRectangle rect;
    
    g_return_if_fail(XFCE_IS_DESKTOP(desktop));
    
    w = gdk_screen_get_width(gscreen);
    h = gdk_screen_get_height(gscreen);
    gtk_widget_set_size_request(GTK_WIDGET(desktop), w, h);
    gtk_window_resize(GTK_WINDOW(desktop), w, h);
    
    /* clear out the old pixmap so we don't use its size anymore */
    gtk_widget_set_style(GTK_WIDGET(desktop), NULL);
    
    /* special case for 1 backdrop to handle xinerama stretching properly.
     * this is broken if it ever becomes possible to change the number of
     * monitors on the fly. */
    if(desktop->priv->nbackdrops == 1) {
        xfce_backdrop_set_size(desktop->priv->backdrops[0], w, h);
        backdrop_changed_cb(desktop->priv->backdrops[0], desktop);
    } else {
        for(i = 0; i < desktop->priv->nbackdrops; i++) {
            gdk_screen_get_monitor_geometry(gscreen, i, &rect);
            xfce_backdrop_set_size(desktop->priv->backdrops[i], rect.width,
                                   rect.height);
            backdrop_changed_cb(desktop->priv->backdrops[i], desktop);
        }
    }
    
#ifdef ENABLE_WINDOW_ICONS
    
    /* TODO: make sure icons don't fall off the edge! */
    
#endif
}

static void
handle_xinerama_stretch(XfceDesktop *desktop)
{
    XfceBackdrop *backdrop0;
    gint i;
    
    for(i = 1; i < desktop->priv->nbackdrops; i++)
        g_object_unref(G_OBJECT(desktop->priv->backdrops[i]));
    
    backdrop0 = desktop->priv->backdrops[0];
    g_free(desktop->priv->backdrops);
    desktop->priv->backdrops = g_new(XfceBackdrop *, 1);
    desktop->priv->backdrops[0] = backdrop0;
    desktop->priv->nbackdrops = 1;
    
    xfce_backdrop_set_size(backdrop0,
            gdk_screen_get_width(desktop->priv->gscreen),
            gdk_screen_get_height(desktop->priv->gscreen));
}

static void
handle_xinerama_unstretch(XfceDesktop *desktop)
{
    XfceBackdrop *backdrop0 = desktop->priv->backdrops[0];
    GdkRectangle rect;
    GdkVisual *visual;
    gint i;
    
    desktop->priv->nbackdrops = gdk_screen_get_n_monitors(desktop->priv->gscreen);
    g_free(desktop->priv->backdrops);
    desktop->priv->backdrops = g_new(XfceBackdrop *, desktop->priv->nbackdrops);
    
    desktop->priv->backdrops[0] = backdrop0;
    gdk_screen_get_monitor_geometry(desktop->priv->gscreen, 0, &rect);
    xfce_backdrop_set_size(backdrop0, rect.width, rect.height);
    
    visual = gtk_widget_get_visual(GTK_WIDGET(desktop));
    for(i = 1; i < desktop->priv->nbackdrops; i++) {
        gdk_screen_get_monitor_geometry(desktop->priv->gscreen, i, &rect);
        desktop->priv->backdrops[i] = xfce_backdrop_new_with_size(visual,
                rect.width, rect.height);
    }
    
    if(desktop->priv->mcs_client)
        load_initial_settings(desktop, desktop->priv->mcs_client);
    
    backdrop_changed_cb(backdrop0, desktop);
    for(i = 1; i < desktop->priv->nbackdrops; i++) {
        g_signal_connect(G_OBJECT(desktop->priv->backdrops[i]), "changed",
                G_CALLBACK(backdrop_changed_cb), desktop);
        backdrop_changed_cb(desktop->priv->backdrops[i], desktop);
    }
}

static void
load_initial_settings(XfceDesktop *desktop, McsClient *mcs_client)
{
    gchar setting_name[64];
    McsSetting *setting = NULL;
    gint screen, i;
    XfceBackdrop *backdrop;
    GdkColor color;
    
    screen = gdk_screen_get_number(desktop->priv->gscreen);
    
    if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, "xineramastretch",
            BACKDROP_CHANNEL, &setting))
    {
        if(setting->data.v_int)
            handle_xinerama_stretch(desktop);
        mcs_setting_free(setting);
        setting = NULL;
    }
    
#ifdef ENABLE_WINDOW_ICONS
    if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, "usewindowicons",
            BACKDROP_CHANNEL, &setting))
    {
        if(setting->data.v_int)
            desktop->priv->use_window_icons = TRUE;
        else
            desktop->priv->use_window_icons = FALSE;
        mcs_setting_free(setting);
        setting = NULL;
    } else
        desktop->priv->use_window_icons = TRUE;
    
    gtk_window_set_accept_focus(GTK_WINDOW(desktop),
                                desktop->priv->use_window_icons);
    if(desktop->priv->use_window_icons && GTK_WIDGET_REALIZED(GTK_WIDGET(desktop)))
        xfce_desktop_setup_icons(desktop);
#endif
    
    for(i = 0; i < desktop->priv->nbackdrops; i++) {
        backdrop = desktop->priv->backdrops[i];
        
        g_snprintf(setting_name, 64, "showimage_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            xfce_backdrop_set_show_image(backdrop, setting->data.v_int);
            mcs_setting_free(setting);
            setting = NULL;
        }
        
        g_snprintf(setting_name, 64, "imagepath_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            if(is_backdrop_list(setting->data.v_string)) {
                const gchar *imgfile = get_path_from_listfile(setting->data.v_string);
                xfce_backdrop_set_image_filename(backdrop, imgfile);
                set_imgfile_root_property(desktop, imgfile, i);
            } else {
                xfce_backdrop_set_image_filename(backdrop, setting->data.v_string);
                set_imgfile_root_property(desktop, setting->data.v_string, i);
            }
            mcs_setting_free(setting);
            setting = NULL;
        } else
            xfce_backdrop_set_image_filename(backdrop, DEFAULT_BACKDROP);
        
        g_snprintf(setting_name, 64, "imagestyle_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            xfce_backdrop_set_image_style(backdrop, setting->data.v_int);
            mcs_setting_free(setting);
            setting = NULL;
        } else
            xfce_backdrop_set_image_style(backdrop, XFCE_BACKDROP_IMAGE_STRETCHED);
        
        g_snprintf(setting_name, 64, "color1_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            color.red = setting->data.v_color.red;
            color.green = setting->data.v_color.green;
            color.blue = setting->data.v_color.blue;
            xfce_backdrop_set_first_color(backdrop, &color);
            mcs_setting_free(setting);
            setting = NULL;
        } else {
            /* default color1 is #6985b7 */
            color.red = (guint16)0x6900;
            color.green = (guint16)0x8500;
            color.blue = (guint16)0xb700;
            xfce_backdrop_set_first_color(backdrop, &color);
        }
        
        g_snprintf(setting_name, 64, "color2_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            color.red = setting->data.v_color.red;
            color.green = setting->data.v_color.green;
            color.blue = setting->data.v_color.blue;
            xfce_backdrop_set_second_color(backdrop, &color);
            mcs_setting_free(setting);
            setting = NULL;
        } else {
            /* default color2 is #dbe8ff */
            color.red = (guint16)0xdb00;
            color.green = (guint16)0xe800;
            color.blue = (guint16)0xff00;
            xfce_backdrop_set_second_color(backdrop, &color);
        }
        
        g_snprintf(setting_name, 64, "colorstyle_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            xfce_backdrop_set_color_style(backdrop, setting->data.v_int);
            mcs_setting_free(setting);
            setting = NULL;
        } else
            xfce_backdrop_set_color_style(backdrop, XFCE_BACKDROP_COLOR_HORIZ_GRADIENT);
        
        g_snprintf(setting_name, 64, "brightness_%d_%d", screen, i);
        if(MCS_SUCCESS == mcs_client_get_setting(mcs_client, setting_name,
                BACKDROP_CHANNEL, &setting))
        {
            xfce_backdrop_set_brightness(backdrop, setting->data.v_int);
            mcs_setting_free(setting);
            setting = NULL;
        } else
            xfce_backdrop_set_brightness(backdrop, 0);
    }
}

static void
screen_set_selection(XfceDesktop *desktop)
{
    Window xwin;
    gint xscreen;
    gchar selection_name[100];
    Atom selection_atom, manager_atom;
    
    xwin = GDK_WINDOW_XID(GTK_WIDGET(desktop)->window);
    xscreen = gdk_screen_get_number(desktop->priv->gscreen);
    
    g_snprintf(selection_name, 100, XFDESKTOP_SELECTION_FMT, xscreen);
    selection_atom = XInternAtom(GDK_DISPLAY(), selection_name, False);
    manager_atom = XInternAtom(GDK_DISPLAY(), "MANAGER", False);

    XSelectInput(GDK_DISPLAY(), xwin, PropertyChangeMask | ButtonPressMask);
    XSetSelectionOwner(GDK_DISPLAY(), selection_atom, xwin, GDK_CURRENT_TIME);

    /* listen for client messages */
    g_signal_connect(G_OBJECT(desktop), "client-event",
            G_CALLBACK(client_message_received), NULL);

    /* Check to see if we managed to claim the selection. If not,
     * we treat it as if we got it then immediately lost it */
    if(XGetSelectionOwner(GDK_DISPLAY(), selection_atom) == xwin) {
        XClientMessageEvent xev;
        Window xroot = GDK_WINDOW_XID(gdk_screen_get_root_window(desktop->priv->gscreen));
        
        xev.type = ClientMessage;
        xev.window = xroot;
        xev.message_type = manager_atom;
        xev.format = 32;
        xev.data.l[0] = GDK_CURRENT_TIME;
        xev.data.l[1] = selection_atom;
        xev.data.l[2] = xwin;
        xev.data.l[3] = 0;    /* manager specific data */
        xev.data.l[4] = 0;    /* manager specific data */

        XSendEvent(GDK_DISPLAY(), xroot, False, StructureNotifyMask, (XEvent *)&xev);
    } else {
        g_error("%s: could not set selection ownership", PACKAGE);
        exit(1);
    }
}

static void
desktop_style_set_cb(GtkWidget *w, GtkStyle *old, gpointer user_data)
{
    XfceDesktop *desktop = XFCE_DESKTOP(w);
    
    g_return_if_fail(XFCE_IS_DESKTOP(desktop));
    
    if(desktop->priv->bg_pixmap) {
        gdk_window_set_back_pixmap(w->window, desktop->priv->bg_pixmap, FALSE);
        gtk_widget_queue_draw(w);
    }
    
#ifdef ENABLE_WINDOW_ICONS
    
    /* TODO: see if the font has changed and redraw text */
    
#endif
}

#ifdef ENABLE_WINDOW_ICONS

static gboolean
desktop_get_workarea_single(XfceDesktop *desktop,
                            guint ws_num,
                            gint *xorigin,
                            gint *yorigin,
                            gint *width,
                            gint *height)
{
    gboolean ret = FALSE;
    Display *dpy;
    Window root;
    Atom property, actual_type = None;
    gint actual_format = 0, first_id;
    gulong nitems = 0, bytes_after = 0, offset = 0;
    unsigned char *data_p = NULL;
    
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop) && xorigin && yorigin
                         && width && height, FALSE);
    
    dpy = GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(GTK_WIDGET(desktop)));
    root = GDK_WINDOW_XID(gdk_screen_get_root_window(desktop->priv->gscreen));
    property = XInternAtom(dpy, "_NET_WORKAREA", False);
    
    first_id = ws_num * 4;
    
    gdk_error_trap_push();
    
    do {
        if(Success == XGetWindowProperty(dpy, root, property, offset,
                                         G_MAXULONG, False, XA_CARDINAL,
                                         &actual_type, &actual_format, &nitems,
                                         &bytes_after, &data_p))
        {
            gint i;
            gulong *data = (gulong *)data_p;
            
            if(actual_format != 32 || actual_type != XA_CARDINAL) {
                XFree(data);
                break;
            }
            
            i = offset / 32;  /* first element id in this batch */
            
            /* there's probably a better way to do this. */
            if(i + nitems >= first_id && first_id - offset >= 0)
                *xorigin = data[first_id - offset] + SCREEN_MARGIN;
            if(i + nitems >= first_id + 1 && first_id - offset + 1 >= 0)
                *yorigin = data[first_id - offset + 1] + SCREEN_MARGIN;
            if(i + nitems >= first_id + 2 && first_id - offset + 2 >= 0)
                *width = data[first_id - offset + 2] - 2 * SCREEN_MARGIN;
            if(i + nitems >= first_id + 3 && first_id - offset + 3 >= 0) {
                *height = data[first_id - offset + 3] - 2 * SCREEN_MARGIN;
                ret = TRUE;
                break;
            }
            
            offset += actual_format * nitems;
        } else
            break;
    } while(bytes_after > 0);
    
    gdk_error_trap_pop();
    
    return ret;
}

static gboolean
desktop_get_workarea(XfceDesktop *desktop,
                     guint nworkspaces,
                     gint *xorigins,
                     gint *yorigins,
                     gint *widths,
                     gint *heights)
{
    gboolean ret = FALSE;
    Display *dpy;
    Window root;
    Atom property, actual_type = None;
    gint actual_format = 0;
    gulong nitems = 0, bytes_after = 0, *data = NULL, offset = 0;
    gint *full_data, i = 0, j;
    unsigned char *data_p = NULL;
    
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop) && xorigins && yorigins
                         && widths && heights, FALSE);
    
    full_data = g_new0(gint, nworkspaces * 4);
    
    dpy = GDK_DISPLAY_XDISPLAY(gtk_widget_get_display(GTK_WIDGET(desktop)));
    root = GDK_WINDOW_XID(gdk_screen_get_root_window(desktop->priv->gscreen));
    property = XInternAtom(dpy, "_NET_WORKAREA", False);
    
    gdk_error_trap_push();
    
    do {
        if(Success == XGetWindowProperty(dpy, root, property, offset,
                                         G_MAXULONG, False, XA_CARDINAL,
                                         &actual_type, &actual_format, &nitems,
                                         &bytes_after, &data_p))
        {
            if(actual_format != 32 || actual_type != XA_CARDINAL) {
                XFree(data);
                break;
            }
            
            data = (gulong *)data_p;
            for(j = 0; j < nitems; j++, i++)
                full_data[i] = data[j];
            XFree(data);
            
            if(i == nworkspaces * 4)
                ret = TRUE;
            
            offset += actual_format * nitems;
        } else
            break;
    } while(bytes_after > 0);
    
    gdk_error_trap_pop();
    
    if(ret) {
        for(i = 0; i < nworkspaces*4; i += 4) {
            xorigins[i/4] = full_data[i] + SCREEN_MARGIN;
            yorigins[i/4] = full_data[i+1] + SCREEN_MARGIN;
            widths[i/4] = full_data[i+2] - 2 * SCREEN_MARGIN;
            heights[i/4] = full_data[i+3] - 2 * SCREEN_MARGIN;
        }
    }
    
    g_free(full_data);
    
    return ret;
}

static void
workspace_changed_cb(NetkScreen *netk_screen,
                     gpointer user_data)
{
    XfceDesktop *desktop = XFCE_DESKTOP(user_data);
    gint cur_col = 0, cur_row = 0, n;
    NetkWorkspace *ws;
    
    ws = netk_screen_get_active_workspace(desktop->priv->netk_screen);
    desktop->priv->cur_ws_num = n = netk_workspace_get_number(ws);
    if(!desktop->priv->icon_workspaces[n]->icons) {
        GList *windows, *l;
        
        desktop->priv->icon_workspaces[n]->icons =
            g_hash_table_new_full(g_direct_hash,
                                  g_direct_equal,
                                  NULL,
                                  (GDestroyNotify)xfce_desktop_icon_free);
        
        windows = netk_screen_get_windows(desktop->priv->netk_screen);
        for(l = windows; l; l = l->next) {
            NetkWindow *window = l->data;
            
            if((ws == netk_window_get_workspace(window)
                || netk_window_is_pinned(window))
               && netk_window_is_minimized(window)
               && !netk_window_is_skip_tasklist(window))
            {
                XfceDesktopIcon *icon;
                
                icon = g_new0(XfceDesktopIcon, 1);
                icon->row = cur_row;
                icon->col = cur_col;
                icon->pix = netk_window_get_icon(window);
                if(icon->pix) {
                    if(gdk_pixbuf_get_width(icon->pix) != ICON_SIZE) {
                        icon->pix = gdk_pixbuf_scale_simple(icon->pix,
                                                            ICON_SIZE,
                                                            ICON_SIZE,
                                                            GDK_INTERP_BILINEAR);
                    }
                    g_object_ref(G_OBJECT(icon->pix));
                }
                icon->label = g_strdup(netk_window_get_name(window));
                icon->window = window;
                g_hash_table_insert(desktop->priv->icon_workspaces[n]->icons,
                                    window, icon);
                
                cur_row++;
                if(cur_row >= desktop->priv->icon_workspaces[n]->nrows) {
                    cur_col++;
                    if(cur_col >= desktop->priv->icon_workspaces[n]->ncols)
                        break;
                    cur_row = 0;
                }
            }
        }
        
        /* save next available row/col */
        desktop->priv->icon_workspaces[n]->lowest_free_row = cur_row;
        desktop->priv->icon_workspaces[n]->lowest_free_col = cur_col;
    }
    
    gtk_widget_queue_draw(GTK_WIDGET(desktop));
}

static void
workspace_created_cb(NetkScreen *netk_screen,
                     NetkWorkspace *workspace,
                     gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    gint ws_num, n_ws, xo, yo, w, h;
    
    n_ws = netk_screen_get_workspace_count(netk_screen);
    ws_num = netk_workspace_get_number(workspace);
    
    desktop->priv->icon_workspaces = g_realloc(desktop->priv->icon_workspaces,
                                               sizeof(XfceDesktopIconWorkspace *) * n_ws);
    
    if(ws_num != n_ws - 1) {
        g_memmove(desktop->priv->icon_workspaces + ws_num + 1,
                  desktop->priv->icon_workspaces + ws_num,
                  sizeof(XfceDesktopIconWorkspace *) * (n_ws - ws_num - 1));
    }
    
    desktop->priv->icon_workspaces[ws_num] = g_new0(XfceDesktopIconWorkspace, 1);
    
    if(desktop_get_workarea_single(desktop, ws_num, &xo, &yo, &w, &h)) {
        DBG("got workarea: %dx%d+%d+%d", w, h, xo, yo);
        desktop->priv->icon_workspaces[ws_num]->xorigin = xo;
        desktop->priv->icon_workspaces[ws_num]->yorigin = yo;
        desktop->priv->icon_workspaces[ws_num]->width = w;
        desktop->priv->icon_workspaces[ws_num]->height = h;
    } else {
        desktop->priv->icon_workspaces[ws_num]->xorigin = 0;
        desktop->priv->icon_workspaces[ws_num]->yorigin = 0;
        desktop->priv->icon_workspaces[ws_num]->width =
                gdk_screen_get_width(desktop->priv->gscreen);
        desktop->priv->icon_workspaces[ws_num]->height =
                gdk_screen_get_height(desktop->priv->gscreen);
    }
    
    desktop->priv->icon_workspaces[ws_num]->nrows = 
        desktop->priv->icon_workspaces[ws_num]->height / CELL_SIZE;
    desktop->priv->icon_workspaces[ws_num]->ncols = 
        desktop->priv->icon_workspaces[ws_num]->width / CELL_SIZE;
}

static void
workspace_destroyed_cb(NetkScreen *netk_screen,
                       NetkWorkspace *workspace,
                       gpointer user_data)
{
    /* TODO: check if we get workspace-destroyed before or after all the
     * windows on that workspace were moved and we got workspace-changed
     * for each one.  preferably that is the case. */
    
    XfceDesktop *desktop = user_data;
    gint ws_num, n_ws;
    
    n_ws = netk_screen_get_workspace_count(netk_screen);
    ws_num = netk_workspace_get_number(workspace);
    
    if(desktop->priv->icon_workspaces[ws_num]->icons)
        g_hash_table_destroy(desktop->priv->icon_workspaces[ws_num]->icons);
    g_free(desktop->priv->icon_workspaces[ws_num]);
    
    if(ws_num != n_ws) {
        g_memmove(desktop->priv->icon_workspaces + ws_num,
                  desktop->priv->icon_workspaces + ws_num + 1,
                  sizeof(XfceDesktopIconWorkspace *) * (n_ws - ws_num));
    }
    
    desktop->priv->icon_workspaces = g_realloc(desktop->priv->icon_workspaces,
                                               sizeof(XfceDesktopIconWorkspace *) * n_ws);
}

static gboolean
check_position_really_free(gpointer key,
                           gpointer value,
                           gpointer user_data)
{
    XfceDesktopIcon *icon = value;
    guint32 pos = GPOINTER_TO_UINT(user_data);
    
    if(icon->row == ((pos >> 16) & 0xffff) && icon->col == (pos & 0xffff))
        return TRUE;
    else
        return FALSE;
}

static gboolean
grid_is_free_position(XfceDesktop *desktop,
                      gint idx,
                      guint16 row,
                      guint16 col)
{
    /* this is somewhat evil, probably. */
    guint32 pos = ((row << 16) & 0xffff0000) | (col & 0xffff);
    
    if(g_hash_table_find(desktop->priv->icon_workspaces[idx]->icons,
                         check_position_really_free,
                         GUINT_TO_POINTER(pos)))
    {
        return FALSE;
    } else
        return TRUE;
}

static void
determine_next_free_position(XfceDesktop *desktop,
                             guint idx)
{
    /* FIXME: this is horribly horribly slow and inefficient.  however, the only
     * alternatives i can come up right now with waste a lot of RAM. */
    for(;;) {            
        desktop->priv->icon_workspaces[idx]->lowest_free_row++;
        if(desktop->priv->icon_workspaces[idx]->lowest_free_row >= desktop->priv->icon_workspaces[idx]->nrows) {
            desktop->priv->icon_workspaces[idx]->lowest_free_row = 0;
            desktop->priv->icon_workspaces[idx]->lowest_free_col++;
            if(desktop->priv->icon_workspaces[idx]->lowest_free_col >= desktop->priv->icon_workspaces[idx]->ncols)
                break;
        }
        
        if(g_hash_table_size(desktop->priv->icon_workspaces[idx]->icons) == 0
           || grid_is_free_position(desktop, idx,
                                    desktop->priv->icon_workspaces[idx]->lowest_free_row,
                                    desktop->priv->icon_workspaces[idx]->lowest_free_col))
        {
            break;
        }
    }
}

static gboolean
get_next_free_position(XfceDesktop *desktop,
                       guint idx,
                       guint16 *row,
                       guint16 *col)
{
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop) && row && col, FALSE);
    
    if(desktop->priv->icon_workspaces[idx]->lowest_free_col >= desktop->priv->icon_workspaces[idx]->ncols
       || (desktop->priv->icon_workspaces[idx]->lowest_free_col == desktop->priv->icon_workspaces[idx]->ncols - 1
           && desktop->priv->icon_workspaces[idx]->lowest_free_row >= desktop->priv->icon_workspaces[idx]->nrows))
    {
        return FALSE;
    }
    
    *row = desktop->priv->icon_workspaces[idx]->lowest_free_row;
    *col = desktop->priv->icon_workspaces[idx]->lowest_free_col;
    
    return TRUE;
}

static void
window_state_changed_cb(NetkWindow *window,
                        NetkWindowState changed_mask,
                        NetkWindowState new_state,
                        gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    NetkWorkspace *ws;
    gint ws_num = -1, i, max_i;
    gboolean is_add = FALSE;
    XfceDesktopIcon *icon;
    
    TRACE("entering");
    
    if(!(changed_mask & (NETK_WINDOW_STATE_MINIMIZED |
                         NETK_WINDOW_STATE_SKIP_TASKLIST)))
    {
        return;
    }
    
    ws = netk_window_get_workspace(window);
    if(ws)
        ws_num = netk_workspace_get_number(ws);
    
    if(   (changed_mask & NETK_WINDOW_STATE_MINIMIZED
           && new_state & NETK_WINDOW_STATE_MINIMIZED)
       || (changed_mask & NETK_WINDOW_STATE_SKIP_TASKLIST
           && !(new_state & NETK_WINDOW_STATE_SKIP_TASKLIST)))
    {
        is_add = TRUE;
    }
    
    /* this is a cute way of handling adding/removing from *all* workspaces
     * when we're dealing with a sticky windows, and just adding/removing
     * from a single workspace otherwise, without duplicating code */
    if(netk_window_is_pinned(window)) {
        i = 0;
        max_i = netk_screen_get_workspace_count(desktop->priv->netk_screen);
    } else {
        g_return_if_fail(ws_num != -1);
        i = ws_num;
        max_i = i + 1;
    }
    
    if(is_add) {
        for(; i < max_i; i++) {
            if(!desktop->priv->icon_workspaces[i]->icons
               || g_hash_table_lookup(desktop->priv->icon_workspaces[i]->icons,
                                      window))
            {
                continue;
            }
            
            xfce_desktop_icon_add(desktop, window, i);
        }
    } else {
        for(; i < max_i; i++) {
            if(!desktop->priv->icon_workspaces[i]->icons)
                continue;
            
            icon = g_hash_table_lookup(desktop->priv->icon_workspaces[i]->icons,
                                       window);
            if(icon)
                xfce_desktop_icon_remove(desktop, icon, window, i);
        }
    }
}

static void
window_workspace_changed_cb(NetkWindow *window,
                            gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    NetkWorkspace *new_ws;
    gint i, new_ws_num = -1, n_ws;
    XfceDesktopIcon *icon;
    
    TRACE("entering");
    
    if(!netk_window_is_minimized(window))
        return;
    
    n_ws = netk_screen_get_workspace_count(desktop->priv->netk_screen);
    
    new_ws = netk_window_get_workspace(window);
    if(new_ws)
        new_ws_num = netk_workspace_get_number(new_ws);
    
    for(i = 0; i < n_ws; i++) {
        if(!desktop->priv->icon_workspaces[i]->icons)
            continue;
        
        icon = g_hash_table_lookup(desktop->priv->icon_workspaces[i]->icons,
                                   window);
        
        if(new_ws) {
            /* window is not sticky */
            if(i != new_ws_num && icon)
                xfce_desktop_icon_remove(desktop, icon, window, i);
            else if(i == new_ws_num && !icon)
                xfce_desktop_icon_add(desktop, window, i);
        } else {
            /* window is sticky */
            if(!icon)
                xfce_desktop_icon_add(desktop, window, i);
        }
    }
}

static void
window_destroyed_cb(gpointer data,
                    GObject *where_the_object_was)
{
    XfceDesktop *desktop = data;
    NetkWindow *window = (NetkWindow *)where_the_object_was;
    gint nws, i;
    XfceDesktopIcon *icon;
    
    nws = netk_screen_get_workspace_count(desktop->priv->netk_screen);
    for(i = 0; i < nws; i++) {
        if(!desktop->priv->icon_workspaces[i]->icons)
            continue;
        
        icon = g_hash_table_lookup(desktop->priv->icon_workspaces[i]->icons,
                                   window);
        if(icon)
            xfce_desktop_icon_remove(desktop, icon, window, i);
    }
}

static void
window_created_cb(NetkScreen *netk_screen,
                  NetkWindow *window,
                  gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    
    g_signal_connect(G_OBJECT(window), "state-changed",
                     G_CALLBACK(window_state_changed_cb), desktop);
    g_signal_connect(G_OBJECT(window), "workspace-changed",
                     G_CALLBACK(window_workspace_changed_cb), desktop);
    g_object_weak_ref(G_OBJECT(window), window_destroyed_cb, desktop);
}

#endif  /* defined(ENABLE_WINDOW_ICONS) */

/* gobject-related functions */

GType
xfce_desktop_get_type()
{
    static GType desktop_type = 0;
    
    if(!desktop_type) {
        static const GTypeInfo desktop_info = {
            sizeof(XfceDesktopClass),
            NULL,
            NULL,
            (GClassInitFunc)xfce_desktop_class_init,
            NULL,
            NULL,
            sizeof(XfceDesktop),
            0,
            (GInstanceInitFunc)xfce_desktop_init
        };
        
        desktop_type = g_type_register_static(GTK_TYPE_WINDOW, "XfceDesktop",
                &desktop_info, 0);
    }
    
    return desktop_type;
}

static void
xfce_desktop_class_init(XfceDesktopClass *klass)
{
    GObjectClass *gobject_class;
    GtkWidgetClass *widget_class;
    
    gobject_class = (GObjectClass *)klass;
    widget_class = (GtkWidgetClass *)klass;
    
    parent_class = g_type_class_peek_parent(klass);
    
    gobject_class->finalize = xfce_desktop_finalize;
    
    widget_class->realize = xfce_desktop_realize;
    widget_class->unrealize = xfce_desktop_unrealize;
}

static void
xfce_desktop_init(XfceDesktop *desktop)
{
    desktop->priv = g_new0(XfceDesktopPriv, 1);
    GTK_WINDOW(desktop)->type = GTK_WINDOW_TOPLEVEL;
    
    gtk_widget_add_events(GTK_WIDGET(desktop), GDK_EXPOSURE_MASK);
    gtk_window_set_type_hint(GTK_WINDOW(desktop), GDK_WINDOW_TYPE_HINT_DESKTOP);
    
#ifdef ENABLE_WINDOW_ICONS
    gtk_widget_add_events(GTK_WIDGET(desktop),
                          GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
#else
    /* if we don't have window icons, there's no reason to focus the desktop */
    gtk_window_set_accept_focus(GTK_WINDOW(desktop), FALSE);
#endif
}

static void
xfce_desktop_finalize(GObject *object)
{
    XfceDesktop *desktop = XFCE_DESKTOP(object);
    
    g_return_if_fail(XFCE_IS_DESKTOP(desktop));
    
    g_free(desktop->priv);
    desktop->priv = NULL;
    
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
xfce_desktop_realize(GtkWidget *widget)
{
    XfceDesktop *desktop = XFCE_DESKTOP(widget);
    GdkAtom atom;
    gint i;
    Window xid;
    GdkDisplay *gdpy;
    GdkWindow *groot;
    GdkVisual *visual;
    
    /* chain up */
    GTK_WIDGET_CLASS(parent_class)->realize(widget);
    
    gtk_window_set_title(GTK_WINDOW(desktop), _("Desktop"));
    if(GTK_WIDGET_DOUBLE_BUFFERED(GTK_WIDGET(desktop)))
        gtk_widget_set_double_buffered(GTK_WIDGET(desktop), FALSE);
    
    gtk_widget_set_size_request(GTK_WIDGET(desktop),
                      gdk_screen_get_width(desktop->priv->gscreen),
                      gdk_screen_get_height(desktop->priv->gscreen));
    gtk_window_move(GTK_WINDOW(desktop), 0, 0);
    
    atom = gdk_atom_intern("_NET_WM_WINDOW_TYPE_DESKTOP", FALSE);
    gdk_property_change(GTK_WIDGET(desktop)->window,
            gdk_atom_intern("_NET_WM_WINDOW_TYPE", FALSE),
            gdk_atom_intern("ATOM", FALSE), 32,
            GDK_PROP_MODE_REPLACE, (guchar *)&atom, 1);
    
    gdpy = gdk_screen_get_display(desktop->priv->gscreen);
    xid = GDK_WINDOW_XID(GTK_WIDGET(desktop)->window);
    groot = gdk_screen_get_root_window(desktop->priv->gscreen);
    
    gdk_property_change(groot,
            gdk_atom_intern("XFCE_DESKTOP_WINDOW", FALSE),
            gdk_atom_intern("WINDOW", FALSE), 32,
            GDK_PROP_MODE_REPLACE, (guchar *)&xid, 1);
    
    gdk_property_change(groot,
            gdk_atom_intern("NAUTILUS_DESKTOP_WINDOW_ID", FALSE),
            gdk_atom_intern("WINDOW", FALSE), 32,
            GDK_PROP_MODE_REPLACE, (guchar *)&xid, 1);
    
    screen_set_selection(desktop);
    
    visual = gtk_widget_get_visual(GTK_WIDGET(desktop));
    desktop->priv->nbackdrops = gdk_screen_get_n_monitors(desktop->priv->gscreen);
    desktop->priv->backdrops = g_new(XfceBackdrop *, desktop->priv->nbackdrops);
    for(i = 0; i < desktop->priv->nbackdrops; i++) {
        GdkRectangle rect;
        gdk_screen_get_monitor_geometry(desktop->priv->gscreen, i, &rect);
        desktop->priv->backdrops[i] = xfce_backdrop_new_with_size(visual,
                rect.width, rect.height);
    }
    
    if(desktop->priv->mcs_client)
        load_initial_settings(desktop, desktop->priv->mcs_client);
    
    for(i = 0; i < desktop->priv->nbackdrops; i++) {
        g_signal_connect(G_OBJECT(desktop->priv->backdrops[i]), "changed",
                G_CALLBACK(backdrop_changed_cb), desktop);
        backdrop_changed_cb(desktop->priv->backdrops[i], desktop);
    }
    
    g_signal_connect(G_OBJECT(desktop->priv->gscreen), "size-changed",
            G_CALLBACK(screen_size_changed_cb), desktop);
    
    gtk_widget_add_events(GTK_WIDGET(desktop), GDK_EXPOSURE_MASK);
    g_signal_connect(G_OBJECT(desktop), "expose-event",
            G_CALLBACK(xfce_desktop_expose), NULL);
    
    g_signal_connect(G_OBJECT(desktop), "style-set",
            G_CALLBACK(desktop_style_set_cb), NULL);
    
#ifdef ENABLE_WINDOW_ICONS
    if(desktop->priv->use_window_icons)
        xfce_desktop_setup_icons(desktop);
#endif
}

static void
xfce_desktop_unrealize(GtkWidget *widget)
{
    XfceDesktop *desktop = XFCE_DESKTOP(widget);
    gint i;
    GdkWindow *groot;
    gchar property_name[128];
    GdkColor c;
    
    g_return_if_fail(XFCE_IS_DESKTOP(desktop));
    
    if(GTK_WIDGET_MAPPED(widget))
        gtk_widget_unmap(widget);
    GTK_WIDGET_UNSET_FLAGS(widget, GTK_MAPPED);
    
#ifdef ENABLE_WINDOW_ICONS
    if(desktop->priv->use_window_icons)
        xfce_desktop_unsetup_icons(desktop);
#endif
    
    gtk_container_forall(GTK_CONTAINER(widget),
                         (GtkCallback)gtk_widget_unrealize,
                         NULL);
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop),
            G_CALLBACK(desktop_style_set_cb), NULL);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop),
            G_CALLBACK(xfce_desktop_expose), NULL);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop->priv->gscreen),
            G_CALLBACK(screen_size_changed_cb), desktop);
    
    groot = gdk_screen_get_root_window(desktop->priv->gscreen);
    gdk_property_delete(groot, gdk_atom_intern("XFCE_DESKTOP_WINDOW", FALSE));
    gdk_property_delete(groot, gdk_atom_intern("NAUTILUS_DESKTOP_WINDOW_ID", FALSE));
    gdk_property_delete(groot, gdk_atom_intern("_XROOTPMAP_ID", FALSE));
    gdk_property_delete(groot, gdk_atom_intern("ESETROOT_PMAP_ID", FALSE));
    
    if(desktop->priv->backdrops) {
        for(i = 0; i < desktop->priv->nbackdrops; i++) {
            g_snprintf(property_name, 128, XFDESKTOP_IMAGE_FILE_FMT, i);
            gdk_property_delete(groot, gdk_atom_intern(property_name, FALSE));
            g_object_unref(G_OBJECT(desktop->priv->backdrops[i]));
        }
        g_free(desktop->priv->backdrops);
        desktop->priv->backdrops = NULL;
    }
    
    if(desktop->priv->bg_pixmap) {
        g_object_unref(G_OBJECT(desktop->priv->bg_pixmap));
        desktop->priv->bg_pixmap = NULL;
    }
    
    gtk_window_set_icon(GTK_WINDOW(widget), NULL);
    
    gtk_style_detach(widget->style);
    g_object_unref(G_OBJECT(widget->window));
    widget->window = NULL;
    
    gtk_selection_remove_all(widget);
    
    /* blank out the root window */
    gdk_window_set_back_pixmap(groot, NULL, FALSE);
    c.red = c.blue = c.green = 0;
    gdk_window_set_background(groot, &c);
    gdk_window_clear(groot);
    GdkRectangle rect;
    rect.x = rect.y = 0;
    gdk_drawable_get_size(GDK_DRAWABLE(groot), &rect.x, &rect.y);
    gdk_window_invalidate_rect(groot, &rect, FALSE);
    
    GTK_WIDGET_UNSET_FLAGS(widget, GTK_REALIZED);
}

#ifdef ENABLE_WINDOW_ICONS

static inline gboolean
xfce_desktop_rectangle_contains_point(GdkRectangle *rect, gint x, gint y)
{
    if(x > rect->x + rect->width
            || x < rect->x
            || y > rect->y + rect->height
            || y < rect->y)
    {
        return FALSE;
    }
    
    return TRUE;
}

static gboolean
check_icon_clicked(gpointer key,
                   gpointer value,
                   gpointer user_data)
{
    XfceDesktopIcon *icon = value;
    GdkEventButton *evt = user_data;
    
    if(xfce_desktop_rectangle_contains_point(&icon->extents, evt->x, evt->y))
        return TRUE;
    else
        return FALSE;
}

static gboolean
xfce_desktop_button_press(GtkWidget *widget,
                          GdkEventButton *evt,
                          gpointer user_data)
{
    XfceDesktop *desktop = XFCE_DESKTOP(widget);
    XfceDesktopPriv *priv = desktop->priv;
    XfceDesktopIcon *icon;
    gint cur_ws_num = desktop->priv->cur_ws_num;
    
    TRACE("entering, type is %s", evt->type == GDK_BUTTON_PRESS ? "GDK_BUTTON_PRESS" : (evt->type == GDK_2BUTTON_PRESS ? "GDK_2BUTTON_PRESS" : "i dunno"));
    
    if(!desktop->priv->use_window_icons)
        return FALSE;
    
    if(evt->type == GDK_BUTTON_PRESS) {
        g_return_val_if_fail(desktop->priv->icon_workspaces[cur_ws_num]->icons,
                             FALSE);
        
        icon = g_hash_table_find(desktop->priv->icon_workspaces[cur_ws_num]->icons,
                                 check_icon_clicked, evt);
        if(icon) {
            /* check old selected icon, paint it as normal */
            if(priv->icon_workspaces[priv->cur_ws_num]->selected_icon) {
                XfceDesktopIcon *old_sel = priv->icon_workspaces[priv->cur_ws_num]->selected_icon;
                priv->icon_workspaces[priv->cur_ws_num]->selected_icon = NULL;
                xfce_desktop_icon_paint(desktop, old_sel);
            }
            
            priv->icon_workspaces[priv->cur_ws_num]->selected_icon = icon;
            xfce_desktop_icon_paint(desktop, icon);
        } else {
            /* unselect previously selected icon if we didn't click one */
            XfceDesktopIcon *old_sel = desktop->priv->icon_workspaces[cur_ws_num]->selected_icon;
            if(old_sel) {
                desktop->priv->icon_workspaces[cur_ws_num]->selected_icon = NULL;
                xfce_desktop_icon_paint(desktop, old_sel);
            }
        }
    } else if(evt->type == GDK_2BUTTON_PRESS) {
        g_return_val_if_fail(desktop->priv->icon_workspaces[cur_ws_num]->icons,
                             FALSE);
        
        icon = g_hash_table_find(desktop->priv->icon_workspaces[cur_ws_num]->icons,
                                 check_icon_clicked, evt);
        if(icon) {
            XfceDesktopIcon *icon_below = NULL;
            
            if(icon->extents.height + 3 * CELL_PADDING > CELL_SIZE)
                icon_below = find_icon_below(desktop, icon);
            netk_window_activate(icon->window);
            if(icon_below) {
                /* delay repaint of below icon to avoid visual bugs */
                IconForeachData *ifed = g_new(IconForeachData, 1);
                ifed->desktop = desktop;
                ifed->data = icon_below;
                g_signal_connect_after(G_OBJECT(icon->window), "state-changed",
                     G_CALLBACK(xfce_desktop_icon_paint_delayed), ifed);
            }
        }
    }
    
    return FALSE;
}

static gboolean
xfce_desktop_button_release(GtkWidget *widget,
                            GdkEventButton *evt,
                            gpointer user_data)
{
    
    return FALSE;
}

#endif

static gboolean
xfce_desktop_expose(GtkWidget *w,
                    GdkEventExpose *evt,
                    gpointer user_data)
{
    TRACE("entering");
    
    if(evt->count != 0)
        return FALSE;
    
    gdk_window_clear_area(w->window, evt->area.x, evt->area.y,
            evt->area.width, evt->area.height);
    
#ifdef ENABLE_WINDOW_ICONS
    if(XFCE_DESKTOP(w)->priv->use_window_icons)
        xfce_desktop_paint_icons(XFCE_DESKTOP(w), &evt->area);
#endif
    
    return TRUE;
}

#ifdef ENABLE_WINDOW_ICONS

static void
check_icon_needs_repaint(gpointer key,
                         gpointer value,
                         gpointer user_data)
{
    XfceDesktopIcon *icon = (XfceDesktopIcon *)value;
    IconForeachData *ifed = (IconForeachData *)user_data;
    GdkRectangle *area = ifed->data, dummy;
    
    if(icon->extents.width == 0 || icon->extents.height == 0
       || gdk_rectangle_intersect(area, &icon->extents, &dummy))
    {
        if(icon == ifed->desktop->priv->icon_workspaces[ifed->desktop->priv->cur_ws_num]->selected_icon) {
            /* save it for last */
            IconForeachData *ifed1 = g_new(IconForeachData, 1);
            ifed1->desktop = ifed->desktop;
            ifed1->data = icon;
            g_idle_add(xfce_desktop_icon_paint_idled, ifed1);
        } else
            xfce_desktop_icon_paint(ifed->desktop, icon);
    }
}

static void
xfce_desktop_paint_icons(XfceDesktop *desktop, GdkRectangle *area)
{
    IconForeachData ifed;
    
    TRACE("entering");
    
    g_return_if_fail(desktop->priv->icon_workspaces[desktop->priv->cur_ws_num]->icons);
    
    ifed.desktop = desktop;
    ifed.data = area;
    g_hash_table_foreach(desktop->priv->icon_workspaces[desktop->priv->cur_ws_num]->icons,
                         check_icon_needs_repaint, &ifed);
}

static void
xfce_desktop_setup_icons(XfceDesktop *desktop)
{
    PangoContext *pctx;
    GList *windows, *l;
    gint nws, i, *xorigins, *yorigins, *widths, *heights;
    
    if(desktop->priv->icon_workspaces)
        return;
    
    if(!desktop->priv->netk_screen) {
        gint screen = gdk_screen_get_number(desktop->priv->gscreen);
        desktop->priv->netk_screen = netk_screen_get(screen);
    }
    netk_screen_force_update(desktop->priv->netk_screen);
    g_signal_connect(G_OBJECT(desktop->priv->netk_screen),
                     "active-workspace-changed",
                     G_CALLBACK(workspace_changed_cb), desktop);
    g_signal_connect(G_OBJECT(desktop->priv->netk_screen), "window-opened",
                     G_CALLBACK(window_created_cb), desktop);
    g_signal_connect(G_OBJECT(desktop->priv->netk_screen), "workspace-created",
                     G_CALLBACK(workspace_created_cb), desktop);
    g_signal_connect(G_OBJECT(desktop->priv->netk_screen),
                     "workspace-destroyed",
                     G_CALLBACK(workspace_destroyed_cb), desktop);
    
    nws = netk_screen_get_workspace_count(desktop->priv->netk_screen);
    desktop->priv->icon_workspaces = g_new(XfceDesktopIconWorkspace *, nws);
    
    xorigins = g_new(gint, nws);
    yorigins = g_new(gint, nws);
    widths = g_new(gint, nws);
    heights = g_new(gint, nws);
    
    if(desktop_get_workarea(desktop, nws, xorigins, yorigins, widths, heights)) {
        for(i = 0; i < nws; i++) {
            desktop->priv->icon_workspaces[i] = g_new0(XfceDesktopIconWorkspace, 1);
            desktop->priv->icon_workspaces[i]->xorigin = xorigins[i];
            desktop->priv->icon_workspaces[i]->yorigin = yorigins[i];
            desktop->priv->icon_workspaces[i]->width = widths[i];
            desktop->priv->icon_workspaces[i]->height = heights[i];
            
            desktop->priv->icon_workspaces[i]->nrows = heights[i] / CELL_SIZE;
            desktop->priv->icon_workspaces[i]->ncols = widths[i] / CELL_SIZE;
        }
    } else {
        gint w = gdk_screen_get_width(desktop->priv->gscreen);
        gint h = gdk_screen_get_height(desktop->priv->gscreen);
        for(i = 0; i < nws; i++) {
            desktop->priv->icon_workspaces[i] = g_new0(XfceDesktopIconWorkspace, 1);
            desktop->priv->icon_workspaces[i]->xorigin = 0;
            desktop->priv->icon_workspaces[i]->yorigin = 0;
            desktop->priv->icon_workspaces[i]->width = w;
            desktop->priv->icon_workspaces[i]->height = h;
            
            desktop->priv->icon_workspaces[i]->nrows = h / CELL_SIZE;
            desktop->priv->icon_workspaces[i]->ncols = w / CELL_SIZE;
        }
    }
    
    g_free(xorigins);
    g_free(yorigins);
    g_free(widths);
    g_free(heights);
    
    pctx = gtk_widget_get_pango_context(GTK_WIDGET(desktop));
    desktop->priv->playout = pango_layout_new(pctx);
    
    windows = netk_screen_get_windows(desktop->priv->netk_screen);
    for(l = windows; l; l = l->next) {
        NetkWindow *window = l->data;
        
        g_signal_connect(G_OBJECT(window), "state-changed",
                         G_CALLBACK(window_state_changed_cb), desktop);
        g_signal_connect(G_OBJECT(window), "workspace-changed",
                         G_CALLBACK(window_workspace_changed_cb), desktop);
        g_object_weak_ref(G_OBJECT(window), window_destroyed_cb, desktop);
    }
    
    workspace_changed_cb(desktop->priv->netk_screen, desktop);
    
    g_signal_connect(G_OBJECT(desktop), "button-press-event",
                     G_CALLBACK(xfce_desktop_button_press), NULL);
    g_signal_connect(G_OBJECT(desktop), "button-release-event",
                     G_CALLBACK(xfce_desktop_button_release), NULL);
}

static void
xfce_desktop_unsetup_icons(XfceDesktop *desktop)
{
    GList *windows, *l;
    gint nws, i;
    
    if(!desktop->priv->icon_workspaces)
        return;
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop),
                                         G_CALLBACK(xfce_desktop_button_press),
                                         NULL);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop),
                                         G_CALLBACK(xfce_desktop_button_release),
                                         NULL);
    
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop->priv->netk_screen),
                                         G_CALLBACK(workspace_changed_cb),
                                         desktop);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop->priv->netk_screen),
                                         G_CALLBACK(window_created_cb),
                                         desktop);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop->priv->netk_screen),
                                         G_CALLBACK(workspace_created_cb),
                                         desktop);
    g_signal_handlers_disconnect_by_func(G_OBJECT(desktop->priv->netk_screen),
                                         G_CALLBACK(workspace_destroyed_cb),
                                         desktop);
    
    windows = netk_screen_get_windows(desktop->priv->netk_screen);
    for(l = windows; l; l = l->next) {
        g_signal_handlers_disconnect_by_func(G_OBJECT(l->data),
                                             G_CALLBACK(window_state_changed_cb),
                                             desktop);
        g_signal_handlers_disconnect_by_func(G_OBJECT(l->data),
                                             G_CALLBACK(window_workspace_changed_cb),
                                             desktop);
        g_object_weak_unref(G_OBJECT(l->data), window_destroyed_cb, desktop);
    }
    
    nws = netk_screen_get_workspace_count(desktop->priv->netk_screen);
    for(i = 0; i < nws; i++) {
        if(desktop->priv->icon_workspaces[i]->icons)
            g_hash_table_destroy(desktop->priv->icon_workspaces[i]->icons);
        g_free(desktop->priv->icon_workspaces[i]);
    }
    g_free(desktop->priv->icon_workspaces);
    desktop->priv->icon_workspaces = NULL;
    
    g_object_unref(G_OBJECT(desktop->priv->playout));
    desktop->priv->playout = NULL;
}

#endif  /* defined(ENABLE_WINDOW_ICONS) */

/* public api */

/**
 * xfce_desktop_new:
 * @gscreen: The current #GdkScreen.
 *
 * Creates a new #XfceDesktop for the specified #GdkScreen.  If @gscreen is
 * %NULL, the default screen will be used.
 *
 * Return value: A new #XfceDesktop.
 **/
GtkWidget *
xfce_desktop_new(GdkScreen *gscreen, McsClient *mcs_client)
{
    XfceDesktop *desktop = g_object_new(XFCE_TYPE_DESKTOP, NULL);
    
    if(!gscreen)
        gscreen = gdk_display_get_default_screen(gdk_display_get_default());
    desktop->priv->gscreen = gscreen;
    gtk_window_set_screen(GTK_WINDOW(desktop), gscreen);
    
    desktop->priv->mcs_client = mcs_client;
    
    return GTK_WIDGET(desktop);
}

guint
xfce_desktop_get_n_monitors(XfceDesktop *desktop)
{
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop), 0);
    
    return desktop->priv->nbackdrops;
}

XfceBackdrop *
xfce_desktop_get_backdrop(XfceDesktop *desktop, guint n)
{
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop), NULL);
    g_return_val_if_fail(n < desktop->priv->nbackdrops, NULL);
    
    return desktop->priv->backdrops[n];
}

gint
xfce_desktop_get_width(XfceDesktop *desktop)
{
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop), -1);
    
    return gdk_screen_get_width(desktop->priv->gscreen);
}

gint
xfce_desktop_get_height(XfceDesktop *desktop)
{
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop), -1);
    
    return gdk_screen_get_height(desktop->priv->gscreen);
}

gboolean
xfce_desktop_settings_changed(McsClient *client, McsAction action,
        McsSetting *setting, gpointer user_data)
{
    XfceDesktop *desktop = user_data;
    XfceBackdrop *backdrop;
    gchar *sname, *p, *q;
    gint screen, monitor;
    GdkColor color;
    gboolean handled = FALSE;
    
    TRACE("dummy");
    
    g_return_val_if_fail(XFCE_IS_DESKTOP(desktop), FALSE);
    
    if(!strcmp(setting->name, "xineramastretch")) {
        if(setting->data.v_int && desktop->priv->nbackdrops > 1) {
            handle_xinerama_stretch(desktop);
            backdrop_changed_cb(desktop->priv->backdrops[0], desktop);
        } else if(!setting->data.v_int)
            handle_xinerama_unstretch(desktop);
        return TRUE;
    }
    
#ifdef ENABLE_WINDOW_ICONS
    if(!strcmp(setting->name, "usewindowicons")) {
        if(setting->data.v_int) {
            desktop->priv->use_window_icons = TRUE;
            if(GTK_WIDGET_REALIZED(GTK_WIDGET(desktop)))
                xfce_desktop_setup_icons(desktop);
        } else {
            desktop->priv->use_window_icons = FALSE;
            if(GTK_WIDGET_REALIZED(GTK_WIDGET(desktop))) {
                xfce_desktop_unsetup_icons(desktop);
                gtk_widget_queue_draw(GTK_WIDGET(desktop));
            }
        }
        gtk_window_set_accept_focus(GTK_WINDOW(desktop),
                                    desktop->priv->use_window_icons);
        return TRUE;
    }
#endif
    
    /* get the screen and monitor number */
    sname = g_strdup(setting->name);
    q = g_strrstr(sname, "_");
    if(!q || q == sname) {
        g_free(sname);
        return FALSE;
    }
    p = strstr(sname, "_");
    if(!p || p == q) {
        g_free(sname);
        return FALSE;
    }
    *q = 0;
    screen = atoi(p+1);
    monitor = atoi(q+1);
    g_free(sname);
    
    if(screen == -1 || monitor == -1
            || screen != gdk_screen_get_number(desktop->priv->gscreen)
            || monitor >= desktop->priv->nbackdrops)
    {
        /* not ours */
        return FALSE;
    }
    
    backdrop = desktop->priv->backdrops[monitor];
    if(!backdrop)
        return FALSE;
    
    switch(action) {
        case MCS_ACTION_NEW:
        case MCS_ACTION_CHANGED:
            if(strstr(setting->name, "showimage") == setting->name) {
                xfce_backdrop_set_show_image(backdrop, setting->data.v_int);
                handled = TRUE;
            } else if(strstr(setting->name, "imagepath") == setting->name) {
                if(is_backdrop_list(setting->data.v_string)) {
                    const gchar *imgfile = get_path_from_listfile(setting->data.v_string);
                    xfce_backdrop_set_image_filename(backdrop, imgfile);
                    set_imgfile_root_property(desktop, imgfile, monitor);
                } else {
                    xfce_backdrop_set_image_filename(backdrop,
                            setting->data.v_string);
                    set_imgfile_root_property(desktop, setting->data.v_string,
                            monitor);
                }
                handled = TRUE;
            } else if(strstr(setting->name, "imagestyle") == setting->name) {
                xfce_backdrop_set_image_style(backdrop, setting->data.v_int);
                handled = TRUE;
            } else if(strstr(setting->name, "color1") == setting->name) {
                color.red = setting->data.v_color.red;
                color.blue = setting->data.v_color.blue;
                color.green = setting->data.v_color.green;
                xfce_backdrop_set_first_color(backdrop, &color);
                handled = TRUE;
            } else if(strstr(setting->name, "color2") == setting->name) {
                color.red = setting->data.v_color.red;
                color.blue = setting->data.v_color.blue;
                color.green = setting->data.v_color.green;
                xfce_backdrop_set_second_color(backdrop, &color);
                handled = TRUE;
            } else if(strstr(setting->name, "colorstyle") == setting->name) {
                xfce_backdrop_set_color_style(backdrop, setting->data.v_int);
                handled = TRUE;
            } else if(strstr(setting->name, "brightness") == setting->name) {
                xfce_backdrop_set_brightness(backdrop, setting->data.v_int);
                handled = TRUE;
            }
            
            break;
        
        case MCS_ACTION_DELETED:
            break;
    }
    
    return handled;
}
