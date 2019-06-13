/*
Simple statusbar for dwm (dynamic window manager):
keyboard layout, sound volume (ALSA), battery capacity, link status, date-time.
Thanks to everyone for their help and code examples.
Andrey Shashanov (2019)
Set your: IFACE, BATTERY, SND_CTL_NAME, MIXER_PART_NAME
Dependences (Debian):
libxcb1, libxcb1-dev, libxcb-xkb1, libxcb-xkb-dev, libasound2, libasound2-dev
gcc -O2 -s -lpthread -lxcb -lxcb-xkb -lasound -lm -o dwmbar dwmbar.c
*/

/* struct timespec, localtime_r() */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#else
#if (_POSIX_C_SOURCE < 200809L)
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include <ctype.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <alsa/asoundlib.h>
#include <xcb/xkb.h>
#include <xcb/xcb.h>

#define IFACE "wlp3s0"
#define BATTERY "BAT0"
#define SND_CTL_NAME "default" /* possible format "hw:0" */
#define MIXER_PART_NAME "Master"

#define PATH_LNK "/sys/class/net/" IFACE "/operstate"
#define PATH_CAPACITY "/sys/class/power_supply/" BATTERY "/capacity"
#define DATETIME_FORMAT "  %Y-%m-%d %a %H:%M"
#define SLEEP_SEC 5
#define SLEEP_1 2  /* SLEEP_SEC * SLEEP_1 */
#define SLEEP_2 24 /* SLEEP_SEC * SLEEP_2 */
#define KBLAYOUT_NUM_CHARS 3
#define NUMLOCK_SZ " NL"

static xcb_connection_t *c;
static xcb_window_t w;
static char layout[32];
static char volume[32];
static char capacity[32];
static char lnk[32];
static char datetime[32];
static char buf[32 +
                sizeof layout +
                sizeof volume +
                sizeof capacity +
                sizeof lnk +
                sizeof datetime];

static void settitle(void)
{
    strcpy(buf, " ");
    strcat(buf, layout);
    strcat(buf, "  " MIXER_PART_NAME ":");
    strcat(buf, volume);
    strcat(buf, "  " BATTERY ":");
    strcat(buf, capacity);
    strcat(buf, "%  " IFACE ":");
    strcat(buf, lnk);
    strcat(buf, datetime);

    xcb_change_property(c,
                        XCB_PROP_MODE_REPLACE,
                        w,
                        XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING,
                        8,
                        (uint32_t)strlen(buf),
                        buf);
    xcb_flush(c);
}

static void getkblayout(void)
{
    xcb_xkb_get_state_cookie_t state_c;
    xcb_xkb_get_state_reply_t *state_r;
    xcb_xkb_get_names_cookie_t names_c;
    xcb_xkb_get_names_reply_t *names_r;
    void *buffer;
    xcb_xkb_get_names_value_list_t aux;
    xcb_get_atom_name_cookie_t atom_name_c;
    xcb_get_atom_name_reply_t *atom_name_r;
    char *name;
    int name_length;

    names_c = xcb_xkb_get_names_unchecked(c, XCB_XKB_ID_USE_CORE_KBD,
                                          XCB_XKB_NAME_DETAIL_GROUP_NAMES);

    state_c = xcb_xkb_get_state_unchecked(c, XCB_XKB_ID_USE_CORE_KBD);

    names_r = xcb_xkb_get_names_reply(c, names_c, NULL);

    buffer = xcb_xkb_get_names_value_list(names_r);
    xcb_xkb_get_names_value_list_unpack(buffer,
                                        names_r->nTypes,
                                        names_r->indicators,
                                        names_r->virtualMods,
                                        names_r->groupNames,
                                        names_r->nKeys,
                                        names_r->nKeyAliases,
                                        names_r->nRadioGroups,
                                        names_r->which,
                                        &aux);

    state_r = xcb_xkb_get_state_reply(c, state_c, NULL);

    atom_name_c = xcb_get_atom_name_unchecked(c, aux.groups[state_r->group]);
    atom_name_r = xcb_get_atom_name_reply(c, atom_name_c, NULL);

    name = xcb_get_atom_name_name(atom_name_r);
    name_length = xcb_get_atom_name_name_length(atom_name_r);

    if (name_length > KBLAYOUT_NUM_CHARS)
        name_length = KBLAYOUT_NUM_CHARS;

    layout[name_length] = '\0';

    do
    {
        --name_length;
        /* Caps Lock */
        if (state_r->lockedMods & XCB_MOD_MASK_LOCK)
            layout[name_length] = (char)toupper(name[name_length]);
        else
            layout[name_length] = name[name_length];
    } while (name_length);

    /* Num Lock */
    if (state_r->lockedMods & XCB_MOD_MASK_2)
        strcat(layout, NUMLOCK_SZ);

    free(atom_name_r);
    free(state_r);
    free(names_r);
}

static void *thread_kblayout(void *arg)
{
    (void)arg;

    xcb_xkb_select_events(c, XCB_XKB_ID_USE_CORE_KBD,
                          XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
                              XCB_XKB_EVENT_TYPE_INDICATOR_STATE_NOTIFY,
                          0,
                          XCB_XKB_EVENT_TYPE_STATE_NOTIFY |
                              XCB_XKB_EVENT_TYPE_INDICATOR_STATE_NOTIFY,
                          0,
                          0,
                          NULL);
    xcb_flush(c);

    for (;;)
    {
        xcb_generic_event_t *e;
        if ((e = xcb_wait_for_event(c)) != NULL)
        {
            if (e->pad0 == XCB_XKB_STATE_NOTIFY)
            {
                if (((xcb_xkb_state_notify_event_t *)e)->changed &
                        XCB_XKB_STATE_PART_MODIFIER_BASE ||
                    ((xcb_xkb_state_notify_event_t *)e)->changed &
                        XCB_XKB_STATE_PART_GROUP_STATE)
                {
                    getkblayout();
                    settitle();
                }
            }
            free(e);
        }
    }

    /* code will never be executed */
    xcb_xkb_select_events(c, XCB_XKB_ID_USE_CORE_KBD, 0, 0, 0, 0, 0, NULL);
    return NULL;
}

static void getvolume(void)
{
    snd_mixer_t *mixer;
    snd_mixer_selem_id_t *sid;
    snd_mixer_elem_t *elem;
    long min, max, value, range;
    int active, percentage;

    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, SND_CTL_NAME);
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);

    snd_mixer_selem_id_malloc(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, MIXER_PART_NAME);
    elem = snd_mixer_find_selem(mixer, sid);
    snd_mixer_selem_id_free(sid);
    if (elem == NULL)
    {
        snd_mixer_close(mixer);
        volume[0] = '\0';
        return;
    }

    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &value);
    snd_mixer_selem_get_playback_switch(elem, SND_MIXER_SCHN_MONO, &active);

    snd_mixer_close(mixer);

    range = max - min;

    if (range == 0)
        percentage = 0;
    else
        percentage = (int)rint(((double)(value - min)) / (double)range * 100);

    sprintf(volume, "%d%%%s", percentage, active ? "" : "M");
}

static void *thread_volume(void *arg)
{
    (void)arg;

    snd_ctl_t *ctl;
    struct pollfd pfds;
    snd_ctl_event_t *event;

    snd_ctl_event_malloc(&event);

initialize:
    if (snd_ctl_open(&ctl, SND_CTL_NAME, SND_CTL_READONLY) < 0)
    {
        snd_ctl_event_free(event);
        volume[0] = '\0';
        settitle();
        return NULL;
    }

    if (snd_ctl_subscribe_events(ctl, 1) < 0)
    {
        snd_ctl_close(ctl);
        snd_ctl_event_free(event);
        volume[0] = '\0';
        settitle();
        return NULL;
    }

    snd_ctl_poll_descriptors(ctl, &pfds, 1);
    pfds.events = POLLIN;

    for (;;)
    {
        if (poll(&pfds, 1, -1) < 0 || snd_ctl_read(ctl, event) < 0)
        {
            volume[0] = '-';
            volume[1] = '\0';
            settitle();
            snd_ctl_subscribe_events(ctl, 0);
            snd_ctl_close(ctl);
            snd_config_update_free_global();
            goto initialize;
        }

        if (snd_ctl_event_get_type(event) == SND_CTL_EVENT_ELEM &&
            snd_ctl_event_elem_get_mask(event) & SND_CTL_EVENT_MASK_VALUE)
        {
            getvolume();
            settitle();
        }
    }

    /* code will never be executed */
    snd_ctl_subscribe_events(ctl, 0);
    snd_ctl_close(ctl);
    snd_ctl_event_free(event);
    snd_config_update_free_global();
    return NULL;
}

int main(void)
{
    xcb_xkb_use_extension_cookie_t extension_c;
    xcb_xkb_use_extension_reply_t *extension_r;
    pthread_attr_t pthread_attr;
    pthread_t pthread;
    int fd;
    ssize_t nr;
    time_t timer;
    struct tm tp;
    struct timespec ts;
    size_t count_1, count_2;

    /* setlocale(LC_TIME, "ru_RU.UTF-8"); */

    c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c))
        return EXIT_FAILURE;

    pthread_attr_init(&pthread_attr);
    pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_DETACHED);

    extension_c = xcb_xkb_use_extension_unchecked(c,
                                                  XCB_XKB_MAJOR_VERSION,
                                                  XCB_XKB_MINOR_VERSION);

    w = xcb_setup_roots_iterator(xcb_get_setup(c)).data->root;

    getvolume();
    pthread_create(&pthread, &pthread_attr, thread_volume, NULL);

    extension_r = xcb_xkb_use_extension_reply(c, extension_c, NULL);
    if (extension_r != NULL && extension_r->supported)
    {
        getkblayout();
        pthread_create(&pthread, &pthread_attr, thread_kblayout, NULL);
    }
    free(extension_r);

    ts.tv_sec = SLEEP_SEC;
    ts.tv_nsec = 0;

    count_1 = 1;
    count_2 = 1;

    for (;; nanosleep(&ts, NULL))
    {
        if (!(--count_1))
        {
            count_1 = SLEEP_1;

            if ((fd = open(PATH_LNK, O_RDONLY)) != -1)
            {
                if ((nr = read(fd, lnk, sizeof lnk)) > 0)
                    /* truncate newline */
                    lnk[--nr] = '\0';
                else
                    lnk[0] = '\0';
                close(fd);
            }
            else
                lnk[0] = '\0';
        }

        if (!(--count_2))
        {
            count_2 = SLEEP_2;

            if ((fd = open(PATH_CAPACITY, O_RDONLY)) != -1)
            {
                if ((nr = read(fd, capacity, sizeof capacity)) > 0)
                    /* truncate newline */
                    capacity[--nr] = '\0';
                else
                    capacity[0] = '\0';
                close(fd);
            }
            else
                capacity[0] = '\0';
        }

        timer = time(NULL);
        localtime_r(&timer, &tp);
        strftime(datetime, sizeof datetime, DATETIME_FORMAT, &tp);

        settitle();
    }

    /* code will never be executed */
    xcb_disconnect(c);
    return EXIT_SUCCESS;
}
