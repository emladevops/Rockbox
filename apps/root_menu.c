/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id: main.c 12101 2007-01-24 02:19:22Z jdgordon $
 *
 * Copyright (C) 2007 Jonathan Gordon
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "config.h"
#include "menu.h"
#include "root_menu.h"
#include "lang.h"
#include "settings.h"
#include "kernel.h"
#include "debug.h"
#include "misc.h"
#include "rolo.h"
#include "powermgmt.h"

#if LCD_DEPTH > 1
#include "backdrop.h"
#endif
#include "talk.h"
#include "audio.h"

/* gui api */
#include "list.h"
#include "statusbar.h"
#include "splash.h"
#include "buttonbar.h"
#include "textarea.h"
#include "action.h"
#include "yesno.h"

#include "main_menu.h"
#include "tree.h"
#if CONFIG_TUNER
#include "radio.h"
#endif
#ifdef HAVE_RECORDING
#include "recording.h"
#endif
#include "gwps-common.h"
#include "bookmark.h"
#include "tagtree.h"
#include "menus/exported_menus.h"
#ifdef HAVE_RTC_ALARM
#include "rtc.h"
#endif

struct root_items {
    int (*function)(void* param);
    void* param;
};
static int last_screen = GO_TO_ROOT; /* unfortunatly needed so we can resume
                                        or goto current track based on previous
                                        screen */
static int browser(void* param)
{
    int ret_val;
    struct tree_context* tc = tree_get_context();
    int filter = SHOW_SUPPORTED;
    char folder[MAX_PATH] = "/";
    /* stuff needed to remember position in file browser */
    static char last_folder[MAX_PATH] = "/";
    /* and stuff for the database browser */
    static int last_db_dirlevel = 0;
    
    switch ((intptr_t)param)
    {
        case GO_TO_FILEBROWSER:
            filter = global_settings.dirfilter;
            if (global_settings.browse_current && 
                    last_screen == GO_TO_WPS && audio_status() &&
                    wps_state.current_track_path[0] != '\0')
            {
                strcpy(folder, wps_state.current_track_path);
            }
            else
                strcpy(folder, last_folder);
        break;
        case GO_TO_DBBROWSER:
            if ((last_screen != GO_TO_ROOT) && !tagcache_is_usable())
            {
                gui_syncsplash(HZ, true, str(LANG_TAGCACHE_BUSY));
                return GO_TO_PREVIOUS;
            }
            filter = SHOW_ID3DB;
            tc->dirlevel = last_db_dirlevel;
        break;
        case GO_TO_BROWSEPLUGINS:
            filter = SHOW_PLUGINS;
            snprintf(folder, MAX_PATH, "%s/", PLUGIN_DIR);
        break;
    }
    ret_val = rockbox_browse(folder, filter);
    switch ((intptr_t)param)
    {
        case GO_TO_FILEBROWSER:
            get_current_file(last_folder, MAX_PATH);
        break;
        case GO_TO_DBBROWSER:
            last_db_dirlevel = tc->dirlevel;
        break;
    }
    /* hopefully only happens trying to go back into the WPS
       from plugins, if music is stopped... */
    if ((ret_val == GO_TO_PREVIOUS) && (last_screen == (intptr_t)param))
        ret_val = GO_TO_ROOT;

    return ret_val;
}  

static int menu(void* param)
{
    (void)param;
    return main_menu();
    
}
#ifdef HAVE_RECORDING
static int recscrn(void* param)
{
    (void)param;
    recording_screen(false);
    return GO_TO_ROOT;
}
#endif
static int wpsscrn(void* param)
{
    int ret_val = GO_TO_PREVIOUS;
    (void)param;
    if (audio_status())
    {
        ret_val = gui_wps_show();
    }
    else if ( global_status.resume_index != -1 )
    {
        DEBUGF("Resume index %X offset %X\n",
               global_status.resume_index,
               global_status.resume_offset);

#ifdef HAVE_RTC_ALARM
        if ( rtc_check_alarm_started(true) ) {
           rtc_enable_alarm(false);
        }
#endif

        if (playlist_resume() != -1)
        {
            playlist_start(global_status.resume_index,
                global_status.resume_offset);
            ret_val = gui_wps_show();
        }
    }
    else 
    {
        gui_syncsplash(HZ*2, true, str(LANG_NOTHING_TO_RESUME));
    }
#if LCD_DEPTH > 1
    show_main_backdrop();
#endif
    return ret_val;
}
#if CONFIG_TUNER
static int radio(void* param)
{
    (void)param;
    radio_screen();
    return GO_TO_ROOT;
}
#endif

static int load_bmarks(void* param)
{
    (void)param;
    bookmark_mrb_load();
    return GO_TO_PREVIOUS;
}

static const struct root_items items[] = {
    [GO_TO_FILEBROWSER] =   { browser, (void*)GO_TO_FILEBROWSER },
    [GO_TO_DBBROWSER] =     { browser, (void*)GO_TO_DBBROWSER },
    [GO_TO_WPS] =           { wpsscrn, NULL },
    [GO_TO_MAINMENU] =      { menu, NULL },
    
#ifdef HAVE_RECORDING
    [GO_TO_RECSCREEN] =     {  recscrn, NULL },
#endif
    
#if CONFIG_TUNER
    [GO_TO_FM] =            { radio, NULL },
#endif
    
    [GO_TO_RECENTBMARKS] =  { load_bmarks, NULL }, 
    [GO_TO_BROWSEPLUGINS] = { browser, (void*)GO_TO_BROWSEPLUGINS }, 
    
};
static const int nb_items = sizeof(items)/sizeof(*items);

#ifdef BOOTFILE
extern bool boot_changed; /* from tree.c */
static void check_boot(void)
{
    if (boot_changed) {
        char *lines[]={str(LANG_BOOT_CHANGED), str(LANG_REBOOT_NOW)};
        struct text_message message={lines, 2};
        if(gui_syncyesno_run(&message, NULL, NULL)==YESNO_YES)
            rolo_load("/" BOOTFILE);
        boot_changed = false;
    }
}
#else
# define check_boot()
#endif
int item_callback(int action, const struct menu_item_ex *this_item) ;

MENUITEM_RETURNVALUE(file_browser, ID2P(LANG_DIR_BROWSER), GO_TO_FILEBROWSER,
                        NULL, Icon_file_view_menu);
MENUITEM_RETURNVALUE(db_browser, ID2P(LANG_TAGCACHE), GO_TO_DBBROWSER, 
                        NULL, Icon_Audio);
MENUITEM_RETURNVALUE(rocks_browser, ID2P(LANG_PLUGINS), GO_TO_BROWSEPLUGINS, 
                        NULL, Icon_Plugin);
char *get_wps_item_name(int selected_item, void * data, char *buffer)
{
    (void)selected_item; (void)data; (void)buffer;
    if (audio_status())
        return ID2P(LANG_NOW_PLAYING);
    return ID2P(LANG_RESUME_PLAYBACK);
}
MENUITEM_RETURNVALUE_DYNTEXT(wps_item, GO_TO_WPS, NULL, get_wps_item_name, 
                                NULL, Icon_Playback_menu);
#ifdef HAVE_RECORDING
MENUITEM_RETURNVALUE(rec, ID2P(LANG_RECORDING_MENU), GO_TO_RECSCREEN,  
                        NULL, Icon_Recording);
#endif
#if CONFIG_TUNER
MENUITEM_RETURNVALUE(fm, ID2P(LANG_FM_RADIO), GO_TO_FM,  
                        item_callback, Icon_Radio_screen);
#endif
MENUITEM_RETURNVALUE(menu_, ID2P(LANG_SETTINGS_MENU), GO_TO_MAINMENU,  
                        NULL, Icon_Submenu_Entered);
MENUITEM_RETURNVALUE(bookmarks, ID2P(LANG_BOOKMARK_MENU_RECENT_BOOKMARKS),
                        GO_TO_RECENTBMARKS,  item_callback, 
                        Icon_Bookmark);
#ifdef HAVE_LCD_CHARCELLS
static int do_shutdown(void)
{
    sys_poweroff();
    return 0;
}
MENUITEM_FUNCTION(do_shutdown_item, ID2P(LANG_SHUTDOWN), do_shutdown, NULL, Icon_NOICON);
#endif
MAKE_MENU(root_menu_, ID2P(LANG_ROCKBOX_TITLE),
            NULL, Icon_Rockbox,
            &bookmarks, &file_browser, &db_browser,
            &wps_item, &menu_, 
#ifdef HAVE_RECORDING
            &rec, 
#endif
#if CONFIG_TUNER
            &fm,
#endif
            &playlist_options, &rocks_browser,  &info_menu

#ifdef HAVE_LCD_CHARCELLS
            ,&do_shutdown_item
#endif
        );

int item_callback(int action, const struct menu_item_ex *this_item) 
{
    switch (action)
    {
        case ACTION_REQUEST_MENUITEM:
#if CONFIG_TUNER
            if (this_item == &fm)
            {
                if (radio_hardware_present() == 0)
                    return ACTION_EXIT_MENUITEM;
            }
            else 
#endif
                if (this_item == &bookmarks)
            {
                if (global_settings.usemrb == 0)
                    return ACTION_EXIT_MENUITEM;
            }
        break;
    }
    return action;
}
static int get_selection(int last_screen)
{
    unsigned int i;
    for(i=0; i< sizeof(root_menu__)/sizeof(*root_menu__); i++)
    {
        if ((root_menu__[i]->flags&MT_RETURN_VALUE) && 
            (root_menu__[i]->value == last_screen))
        {
            return i;
        }
    }
    return 0;
}

void root_menu(void)
{
    int previous_browser = GO_TO_FILEBROWSER;
    int previous_music = GO_TO_WPS;
    int ret_val = GO_TO_ROOT;
    int this_screen = GO_TO_ROOT;
    int selected = 0;

    if (global_settings.start_in_screen == 0)
        ret_val = (int)global_status.last_screen;
    else ret_val = global_settings.start_in_screen - 2;
    
    while (true)
    {
        switch (ret_val)
        {
            case GO_TO_ROOT:
                selected = get_selection(last_screen);
                ret_val = do_menu(&root_menu_, &selected);
                /* As long as MENU_ATTACHED_USB == GO_TO_ROOT this works */
                if (ret_val == MENU_ATTACHED_USB)
                {
                    check_boot();
                    continue;
                }
                else if (ret_val <= GO_TO_ROOT)
                    continue;
                last_screen = GO_TO_ROOT;
                break;

            case GO_TO_PREVIOUS:
                ret_val = last_screen;
                continue;
                break;

            case GO_TO_PREVIOUS_BROWSER:
                if ((previous_browser == GO_TO_DBBROWSER) && 
                    !tagcache_is_usable())
                    ret_val = GO_TO_FILEBROWSER;
                else 
                    ret_val = previous_browser;
                /* fall through */
            case GO_TO_FILEBROWSER:
            case GO_TO_DBBROWSER:
                previous_browser = ret_val;
                break;

            case GO_TO_PREVIOUS_MUSIC:
                ret_val = previous_music;
                /* fall through */
            case GO_TO_WPS:
#if CONFIG_TUNER
            case GO_TO_FM:
#endif
                previous_music = ret_val;
                break;
        }
        this_screen = ret_val;
        /* set the global_status.last_screen before entering,
           if we dont we will always return to the wrong screen on boot */
        global_status.last_screen = (char)this_screen;
        status_save();
        action_signalscreenchange();
        ret_val = items[this_screen].function(items[this_screen].param);
        if (ret_val != GO_TO_PREVIOUS)
            last_screen = this_screen;
    }
    return;
}
