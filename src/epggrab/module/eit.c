/*
 *  Electronic Program Guide - eit grabber
 *  Copyright (C) 2012 Adam Sutton
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

#include "tvheadend.h"
#include "channels.h"
#include "service.h"
#include "epg.h"
#include "epggrab.h"
#include "epggrab/private.h"
#include "input.h"
#include "input/mpegts/dvb_charset.h"
#include "intlconv.h"
#include "dvr/dvr.h"

/* ************************************************************************
 * Status handling
 * ***********************************************************************/

typedef struct eit_event
{
  char              uri[257];
  char              suri[257];
  
  lang_str_t       *title;
  lang_str_t       *summary;
  lang_str_t       *desc;

  const char       *default_charset;

  htsmsg_t         *extra;

  epg_genre_list_t *genre;

  uint8_t           hd, ws;
  uint8_t           ad, st, ds;
  uint8_t           bw;

  uint8_t           parental;

#if ENABLE_ISDB
  int               gdesc_len;
  const uint8_t    *group_descriptor;

  int               epnum;
  int               epcount;
  uint16_t          series_id;

  /* stream component props */
  struct {
    uint8_t         tag;
    uint8_t         lang[4], lang_sub[4];
    uint8_t         is_dmono;
    uint8_t         channels; /* unused */
    uint8_t         sri;      /* unused */

    uint16_t        width;
    uint16_t        height;
    uint16_t        aspect_num;
    uint16_t        aspect_den;
  }                 sct_props;
#endif
} eit_event_t;

/* ************************************************************************
 * Diagnostics
 * ***********************************************************************/

// Dump a descriptor tag for debug (looking for new tags etc...)
static void
_eit_dtag_dump 
  ( epggrab_module_t *mod, uint8_t dtag, uint8_t dlen, const uint8_t *buf )
{
#if APS_DEBUG
  int i = 0, j = 0;
  char tmp[100];
  tvhlog(LOG_DEBUG, mod->id, "  dtag 0x%02X len %d", dtag, dlen);
  while (i < dlen) {
    j += sprintf(tmp+j, "%02X ", buf[i]);
    i++;
    if ((i % 8) == 0 || (i == dlen)) {
      tvhlog(LOG_DEBUG, mod->id, "    %s", tmp);
      j = 0;
    }
  }
#endif
}

/* ************************************************************************
 * EIT Event descriptors
 * ***********************************************************************/

static dvb_string_conv_t _eit_freesat_conv[2] = {
  { 0x1f, freesat_huffman_decode },
  { 0x00, NULL }
};

/*
 * Get string
 */
static int _eit_get_string_with_len
  ( epggrab_module_t *m,
    char *dst, size_t dstlen, 
		const uint8_t *src, size_t srclen, const char *charset )
{
  dvb_string_conv_t *cptr = NULL;

  /* Enable huffman decode (for freeview and/or freesat) */
  m = epggrab_module_find_by_id("uk_freesat");
  if (m && m->enabled) {
    cptr = _eit_freesat_conv;
  } else {
    m = epggrab_module_find_by_id("uk_freeview");
    if (m && m->enabled) cptr = _eit_freesat_conv;
  }

  /* Convert */
  return dvb_get_string_with_len(dst, dstlen, src, srclen, charset, cptr);
}

#if ENABLE_ISDB
#include <unistr.h>
#include <unictype.h>

static void _eit_isdb_split_title
  ( uint8_t *buf, const uint8_t **title, const uint8_t **sub,
    const uint8_t **extbuf )
{
  /* Character icons for broadcast/program property:
   * ARIB B24 characters of row:90 col:48-82 (like "[HD]", "[SD]", ...).
   */
  static const uint8_t ISDB_EPG_CHARS[] = {
    0xf0, 0x9f, 0x85, 0x8a, /* U+1f14a */ 0xf0, 0x9f, 0x85, 0x8c, /* U+1f14c */
    0xf0, 0x9f, 0x84, 0xbf, /* U+1f13f */ 0xf0, 0x9f, 0x85, 0x86, /* U+1f146 */
    0xf0, 0x9f, 0x85, 0x8b, /* U+1f14b */ 0xf0, 0x9f, 0x88, 0x90, /* U+1f210 */
    0xf0, 0x9f, 0x88, 0x91, /* U+1f211 */ 0xf0, 0x9f, 0x88, 0x92, /* U+1f212 */
    0xf0, 0x9f, 0x88, 0x93, /* U+1f213 */ 0xf0, 0x9f, 0x85, 0x82, /* U+1f142 */
    0xf0, 0x9f, 0x88, 0x94, /* U+1f214 */ 0xf0, 0x9f, 0x88, 0x95, /* U+1f215 */
    0xf0, 0x9f, 0x88, 0x96, /* U+1f216 */ 0xf0, 0x9f, 0x85, 0x8d, /* U+1f14d */
    0xf0, 0x9f, 0x84, 0xb1, /* U+1f131 */ 0xf0, 0x9f, 0x84, 0xbd, /* U+1f13d */
    0xe2, 0xac, 0x9b,       /* U+2b1b */  0xe2, 0xac, 0xa4,       /* U+2b24 */
    0xf0, 0x9f, 0x88, 0x97, /* U+1f217 */ 0xf0, 0x9f, 0x88, 0x98, /* U+1f218 */
    0xf0, 0x9f, 0x88, 0x99, /* U+1f219 */ 0xf0, 0x9f, 0x88, 0x9a, /* U+1f21a */
    0xf0, 0x9f, 0x88, 0x9b, /* U+1f21b */ 0xe2, 0x9a, 0xbf,       /* U+26bf */
    0xf0, 0x9f, 0x88, 0x9c, /* U+1f21c */ 0xf0, 0x9f, 0x88, 0x9d, /* U+1f21d */
    0xf0, 0x9f, 0x88, 0x9e, /* U+1f21e */ 0xf0, 0x9f, 0x88, 0x9f, /* U+1f21f */
    0xf0, 0x9f, 0x88, 0xa0, /* U+1f220 */ 0xf0, 0x9f, 0x88, 0xa1, /* U+1f221 */
    0xf0, 0x9f, 0x88, 0xa2, /* U+1f222 */ 0xf0, 0x9f, 0x88, 0xa3, /* U+1f223 */
    0xf0, 0x9f, 0x88, 0xa4, /* U+1f224 */ 0xf0, 0x9f, 0x88, 0xa5, /* U+1f225 */
    0xf0, 0x9f, 0x85, 0x8e, /* U+1f14e */ 0xe3, 0x8a, 0x99,       /* U+3299 */
    0xf0, 0x9f, 0x88, 0x80, /* U+1f200 */
    0
  };
  /* open punctuations (but excluding those used for describing title) */
  static const uint32_t ISDB_EPG_OPEN_PUNC[] = {
    /* « 【 〖 */
    0x00ab, 0x3010, 0x3016,
    /* – — 〜 */
    0x2013, 0x2014, 0x301c,
    0
  };
  static const uint32_t ISDB_EPG_CLOSE_PUNC[] = {
    0x00bb, 0x3011, 0x3017,
    0x2013, 0x2014, 0x301c,
    0
  };
  static const uint8_t ISDB_EPG_DELIM_CHARS[] = {
    0xe2, 0x96, 0xbc, /* U+25bc ▼ */ 0xe2, 0x96, 0xbd, /* U+25bd ▽ */
    0xe2, 0x97, 0x86, /* U+25c6 ◆ */ 0xe2, 0x97, 0x87, /* U+25c7 ◇ */
    0
  };

  static uint8_t extra[128];

  const uint8_t *p;
  size_t l;
  int ext_idx;
  ucs4_t uc;

  *title = *sub = NULL;
  *extbuf = extra;

  ext_idx = 0;
  extra[ext_idx] = '\0';

  /* save & skip leading EPG chars */
  l = u8_strspn(buf, ISDB_EPG_CHARS);
  if (l > 0 && ext_idx + l < sizeof(extra)) {
    memcpy(extra + ext_idx, buf, l);
    ext_idx += l;
    extra[ext_idx] = '\0';
  }
  *title = buf + l;
  if (**title == '\0')
    return;

  /* remove another EPG chars in the middle or end of text */
  p = u8_strpbrk(*title, ISDB_EPG_CHARS);
  if (p) {
    l = u8_strspn(p, ISDB_EPG_CHARS);
    if (l > 0 && ext_idx + l < sizeof(extra)) {
      memcpy(extra + ext_idx, title, l);
      ext_idx += l;
      extra[ext_idx] = '\0';
    }
    * (uint8_t *) p = '\0';
    p += l;
    /* if EPG chars are in the midst of text, they must be a delimiter */
    if (*p != '\0') {
      *sub = p;
      return;
    }
  }

  /* if the last char is in ISDB_EPG_CLOSE_PUNC.. */
  /* firstly, find the last char */
  for (p = *title; p && *p; p = u8_next(&uc, p)) ;
  if (!p || uc == 0xfffd)
    return;

  if (u32_strchr(ISDB_EPG_CLOSE_PUNC, uc)) {
    int i;
    const uint8_t *q;

    i = u32_strchr(ISDB_EPG_CLOSE_PUNC, uc) - ISDB_EPG_CLOSE_PUNC;
    p = u8_prev(&uc, p, *title);
    while (p != *title) {
      p = u8_prev(&uc, p, *title);
      if (uc == ISDB_EPG_OPEN_PUNC[i])
        break;
    }
    if (p == *title)
      return;

    /* if a special delimiter char is preceding the opening punc char,
     * prefer special delimiter.
     */
    q = u8_strpbrk(*title, ISDB_EPG_DELIM_CHARS);
    if (q && q != *title && q < p)
      p = q;
    *sub = p;
    return;
  }

  /* special delimiters */
  p = u8_strpbrk(*title, ISDB_EPG_DELIM_CHARS);
  if (p && p != *title)
    *sub = p;

  return;
}
#endif /* ENABLE_ISDB */

/*
 * Short Event - 0x4d
 */
static int _eit_desc_short_event
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  int r;
  char lang[4];
  char buf[512];
  char *extra;

  if ( len < 5 ) return -1;

  /* Language */
  memcpy(lang, ptr, 3);
  lang[3] = '\0';
  len -= 3;
  ptr += 3;

  /* Title */
  if ( (r = _eit_get_string_with_len(mod, buf, sizeof(buf),
                                     ptr, len, ev->default_charset)) < 0 ) {
    return -1;
  } else if ( r > 1 ) {
#if ENABLE_ISDB
    char *title, *sub;

    _eit_isdb_split_title((uint8_t *) buf, (const uint8_t **) &title,
                          (const uint8_t **) &sub, (const uint8_t **) &extra);
    if (!title || !*title)
      return -1;

    if (!strempty(sub)) {
      if (!ev->summary) ev->summary = lang_str_create();
      lang_str_add(ev->summary, sub, "jpn", 0);
      *sub = '\0';
    }
    if (!ev->title) ev->title = lang_str_create();
    lang_str_add(ev->title, title, lang, 0);
#else
    if (!ev->title) ev->title = lang_str_create();
    lang_str_add(ev->title, buf, lang, 0);
#endif /* !ENABLE_ISDB */
  }

  len -= r;
  ptr += r;
  if ( len < 1 ) return -1;

  /* Summary */
  if ( (r = _eit_get_string_with_len(mod, buf, sizeof(buf),
                                     ptr, len, ev->default_charset)) < 0 ) {
    return -1;
  } else if ( r > 1 ) {
    if (!ev->summary) ev->summary = lang_str_create();
#if ENABLE_ISDB
    if (!lang_str_empty(ev->summary)) {
      lang_str_append(ev->summary, "\n", lang);
      lang_str_append(ev->summary, buf, lang);
    } else
      lang_str_add(ev->summary, buf, lang, 0);

    if (!strempty(extra)) {
      lang_str_append(ev->summary, "\n", lang);
      lang_str_append(ev->summary, extra, lang);
    }
#else
    lang_str_add(ev->summary, buf, lang, 0);
#endif /* !ENABLE_ISDB */
  }

  return 0;
}

/*
 * Extended Event - 0x4e
 */
static int _eit_desc_ext_event
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  int r, ilen;
  char ikey[512], ival[512];
  char buf[512], lang[4];
  const uint8_t *iptr;
  uint8_t desc_num;
  int is_first_item;

  if (len < 6) return -1;

  /* Descriptor numbering (skip) */
  desc_num = ptr[0] >> 4;
  len -= 1;
  ptr += 1;

  /* Language */
  memcpy(lang, ptr, 3);
  lang[3] = '\0';
  len -= 3;
  ptr += 3;

  /* Key/Value items */
  ilen  = *ptr;
  len  -= 1;
  ptr  += 1;
  iptr  = ptr;
  if (len < ilen) return -1;

  /* Skip past */
  ptr += ilen;
  len -= ilen;

  /* Process */
  is_first_item = 1;
  while (ilen) {
#if ENABLE_ISDB
    /*
     * The 1st item value can be a part of long text
     * that is split among consecutive descriptors.
     * If so, it must be converted WITHOUT resetting the conversion state,
     * and concatenated to the last item value in the previous desc.
     * (note that _eit_get_string_with_len() resets conversion state)
     */
    if (iptr[0] == 0 && desc_num > 0 && is_first_item) {
      size_t l;

      ikey[0] = 0;
      ilen --;
      iptr ++;

      if (!*iptr || *iptr > ilen - 1)
        break;
      l = intlconv_to_utf8(ival, sizeof(ival),
                           intlconv_charset_id("ARIB-STD-B24", 1, 1),
                           (const char *) iptr + 1, *iptr);
      if (l < 0)
        break;
      if (l >= sizeof(ival))
        ival[sizeof(ival) - 1] = 0;
      else
        ival[l] = 0;

      ilen -= *iptr + 1;
      iptr += *iptr + 1;

      goto append_to_desc;
    }
#endif  /* ENABLE_ISDB */

    /* Key */
    if ( (r = _eit_get_string_with_len(mod, ikey, sizeof(ikey),
                                       iptr, ilen, ev->default_charset)) < 0 )
      break;
    
    ilen -= r;
    iptr += r;

    /* Value */
    if ( (r = _eit_get_string_with_len(mod, ival, sizeof(ival),
                                       iptr, ilen, ev->default_charset)) < 0 )
      break;

    ilen -= r;
    iptr += r;

    /* Store */
    // TODO: extend existing?
#if TODO_ADD_EXTRA
    if (*ikey && *ival) {
      if (!ev->extra) ev->extra = htsmsg_create_map();
      htsmsg_add_str(ev->extra, ikey, ival);
    }
#endif

#if ENABLE_ISDB
append_to_desc:
    is_first_item = 0;
    if (*ikey) {
      if (!ev->desc) ev->desc = lang_str_create();
      if (!lang_str_empty(ev->desc)) lang_str_append(ev->desc, "\n\n", lang);
      lang_str_append(ev->desc, ikey, lang);
      lang_str_append(ev->desc, ":\n", lang);
    }
    if (*ival) {
      if (!ev->desc) ev->desc = lang_str_create();
      lang_str_append(ev->desc, ival, lang);
    }
#endif  /* ENABLE_ISDB */
  }

  /* Description */
  if ( _eit_get_string_with_len(mod,
                                buf, sizeof(buf),
                                ptr, len,
                                ev->default_charset) > 1 ) {
    if (!ev->desc) ev->desc = lang_str_create();
    lang_str_append(ev->desc, buf, lang);
  }

  return 0;
}

/*
 * Component Descriptor - 0x50
 */

static int _eit_desc_component
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  uint8_t c, t;

  if (len < 6) return -1;

  /* Stream Content and Type */
  c = *ptr & 0x0f;
  t = ptr[1];

#if ENABLE_ISDB
  /* TODO: support multiple video streams(components) per service */
  if (c != 0x01) return -1;
  memset(&ev->sct_props, 0, sizeof(ev->sct_props));
  ev->sct_props.tag = ptr[2];

  switch (t & 0xf0) {
  case 0:
  case 0xa0: ev->sct_props.height =  480; break;
  case 0xb0: ev->sct_props.height = 1080; break;
  case 0xc0: ev->sct_props.height =  720; break;
  case 0xd0: ev->sct_props.height =  240; break;
  default:   return -1;
  }

  switch (t & 0x0f) {
  case 1:
    ev->sct_props.aspect_num = 4;
    ev->sct_props.aspect_den = 3;
    ev->sct_props.width = ev->sct_props.height * 4 / 3;
    break;
  case 3:
    ev->sct_props.aspect_num = 16;
    ev->sct_props.aspect_den = 9;
    ev->sct_props.width = ev->sct_props.height * 16 / 9;
    break;
  case 4:
    /* > 16:9. assume 2.4:1 for now (ES will update it later) */
    ev->sct_props.aspect_num = 12;
    ev->sct_props.aspect_den = 5;
    break;
  default:
    return -1;
  }

  memcpy(ev->sct_props.lang, &ptr[3], 3);

  if (ptr[2] == 0) { /* tag == 0: main video ES */
    ev->ws = (ev->sct_props.aspect_num != 4);
    ev->hd = (ev->sct_props.height >= 720);
  }

  return 0;
#else  /* ENABLE_ISDB */

  /* MPEG2 (video) */
  if (c == 0x1) {
    if (t > 0x08 && t < 0x11) {
      ev->hd = 1;
      if ( t != 0x09 && t != 0x0d )
        ev->ws = 1;
    } else if (t == 0x02 || t == 0x03 || t == 0x04 ||
               t == 0x06 || t == 0x07 || t == 0x08 ) {
      ev->ws = 1;
    }

  /* MPEG2 (audio) */
  } else if (c == 0x2) {

    /* Described */
    if (t == 0x40 || t == 0x41)
      ev->ad = 1;

  /* Misc */
  } else if (c == 0x3) {
    if (t == 0x1 || (t >= 0x10 && t <= 0x14) || (t >= 0x20 && t <= 0x24))
      ev->st = 1;
    else if (t == 0x30 || t == 0x31)
      ev->ds = 1;

  /* H264 */
  } else if (c == 0x5) {
    if (t == 0x0b || t == 0x0c || t == 0x10)
      ev->hd = ev->ws = 1;
    else if (t == 0x03 || t == 0x04 || t == 0x07 || t == 0x08)
      ev->ws = 1;

  /* AAC */
  } else if ( c == 0x6 ) {

    /* Described */
    if (t == 0x40 || t == 0x44)
      ev->ad = 1;

  /* HEVC */
  } else if ( c == 0x9 ) {

    ev->ws = 1;
    if (t > 3)
      ev->hd = 2;

  }

  return 0;
#endif  /* !ENABLE_ISDB */
}

/*
 * Content Descriptor - 0x54
 */

static int _eit_desc_content
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  while (len > 1) {
#if ENABLE_ISDB
    if ((*ptr & 0xf0) == 0xe0) {
      /* TODO: handle program attributes */
    } else if (*ptr < 0xe0) {
#else  /* ENABLE_ISDB */
    if (*ptr == 0xb1)
      ev->bw = 1;
    else if (*ptr < 0xb0) {
#endif  /* !ENABLE_ISDB */
      if (!ev->genre) ev->genre = calloc(1, sizeof(epg_genre_list_t));
      epg_genre_list_add_by_eit(ev->genre, *ptr);
    }
    len -= 2;
    ptr += 2;
  }
  return 0;
}

/*
 * Parental rating Descriptor - 0x55
 */
static int _eit_desc_parental
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  int cnt = 0, sum = 0, i = 0;
  while (len > 3) {
    if ( ptr[i] && ptr[i] < 0x10 ) {
      cnt++;
      sum += (ptr[i] + 3);
    }
    len -= 4;
    i   += 4;
  }
  // Note: we ignore the country code and average the lot!
  if (cnt)
    ev->parental = (uint8_t)(sum / cnt);

  return 0;
}

/*
 * Content ID - 0x76
 */
static int _eit_desc_crid
  ( epggrab_module_t *mod, const uint8_t *ptr, int len,
    eit_event_t *ev, mpegts_service_t *svc )
{
  int r;
  uint8_t type;
  char buf[512], *crid;
  int clen;

  while (len > 3) {

    /* Explicit only */
    if ( (*ptr & 0x3) == 0 ) {
      crid = NULL;
      type = *ptr >> 2;

      r = _eit_get_string_with_len(mod, buf, sizeof(buf),
                                   ptr+1, len-1,
                                   ev->default_charset);
      if (r < 0) return -1;
      if (r == 0) continue;

      /* Episode */
      if (type == 0x1 || type == 0x31) {
        crid = ev->uri;
        clen = sizeof(ev->uri);

      /* Season */
      } else if (type == 0x2 || type == 0x32) {
        crid = ev->suri;
        clen = sizeof(ev->suri);
      }
    
      if (crid) {
        if (strstr(buf, "crid://") == buf) {
          strncpy(crid, buf, clen);
          crid[clen-1] = '\0';
        } else if ( *buf != '/' ) {
          snprintf(crid, clen, "crid://%s", buf);
        } else {
          const char *defauth = svc->s_dvb_cridauth;
          if (!defauth)
            defauth = svc->s_dvb_mux->mm_crid_authority;
          if (defauth)
            snprintf(crid, clen, "crid://%s%s", defauth, buf);
          else
            snprintf(crid, clen, "crid://onid-%d%s", svc->s_dvb_mux->mm_onid, buf);
        }
      }

      /* Next */
      len -= 1 + r;
      ptr += 1 + r;
    }
  }

  return 0;
}

#if ENABLE_ISDB
/*
 * Audio Component Descriptor - 0xC4
 */
static int _eit_desc_audio_component
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  uint8_t c;
  uint8_t is_multi_lingual;
  uint8_t s;

  if (len < 9) return -1;

  memset(&ev->sct_props, 0, sizeof(ev->sct_props));

  /* Stream Content and Type */
  c = *ptr & 0x0f;
  ev->sct_props.tag = ptr[2];
  if (c != 0x02 || ptr[3] != 0x0f) return -1;

  ev->sct_props.is_dmono = (ptr[1] == 0x02);
  switch (ptr[1]) {
  case 1:
    ev->sct_props.channels = 1;
    break;
  case 2:
  case 3:
    ev->sct_props.channels = 2;
    break;
  case 7:
  case 8:
  case 9:
    ev->sct_props.channels = ptr[1] - 3;
    break;
  default:
    ev->sct_props.channels = 0;
  }

  s = (ptr[6] & 0x0e) >> 1;
  ev->sct_props.sri = (s == 3) ? 6 : (s == 5) ? 5 : (s == 7) ? 3 : 0x0f;

  is_multi_lingual = !!(ptr[6] & 0x80);
  if (is_multi_lingual && len < 12)
    return -1;

  memcpy(ev->sct_props.lang, &ptr[7], 3);
  if (is_multi_lingual)
    memcpy(ev->sct_props.lang_sub, &ptr[9], 3);
  return 0;
}

/*
 * Series Descriptor - 0xD5
 */
static int _eit_desc_series
  ( epggrab_module_t *mod, const uint8_t *ptr, int len, eit_event_t *ev )
{
  if (len < 8) return -1;

  ev->series_id = ptr[0] << 8 | ptr[1];
  ev->epnum = ptr[5] << 4 | ((ptr[6] & 0xf0) >> 4);
  ev->epcount = (ptr[6] & 0x0f) << 4 | ptr[7];

  len -= 8;
  ptr += 8;
#if TODO_ADD_EXTRA
  if (len > 0) {
    int r;
    char title[256];

    r = dvb_get_string(title, sizeof(title), ptr, len, "ARIB-STR-B24", NULL);
    if (r < 0) return -1;

    if (!ev->extra) ev->extra = htsmsg_create_map();
    htsmsg_add_str(ev->extra, "series_title", title);
  }
#endif
  return 0;
}

static epg_broadcast_t *_eit_egroup_get_peerbc
  ( const uint8_t *ptr, mpegts_mux_t *mm, channel_t **ch, uint16_t *eid)
{
  uint16_t sid;
  mpegts_service_t *peer_svc;
  idnode_list_mapping_t *ilm;

  *ch = NULL;
  *eid = 0;

  if (!mm)
    return NULL;

  sid = ptr[0] << 8 | ptr[1];
  *eid = ptr[2] << 8 | ptr[3];

  peer_svc = mpegts_mux_find_service(mm, sid);
  if (!peer_svc)
    return NULL;

  LIST_FOREACH(ilm, &peer_svc->s_channels, ilm_in1_link) {
    channel_t *peer_ch = (channel_t *)ilm->ilm_in2;

    if (!peer_ch->ch_enabled || peer_ch->ch_epg_parent) continue;
    *ch = peer_ch;
    return epg_broadcast_find_by_eid(peer_ch, *eid);
  }
  return NULL;
}

/*
 * Event Group Descriptor - 0xD6
 */
static int _eit_desc_event_group
  ( epggrab_module_t *mod, const uint8_t *ptr, int len,
    epg_broadcast_t *bc, mpegts_service_t *svc, uint32_t *changes )
{
  enum {
    EVENT_GROUP_TYPE_NONE,
    EVENT_GROUP_TYPE_SHARE,
    EVENT_GROUP_TYPE_RELAY,
    EVENT_GROUP_TYPE_MOVE,
    EVENT_GROUP_TYPE_RELAY_X,
    EVENT_GROUP_TYPE_MOVE_X,
  } egtype;
  int save;
  int ecount;
  uint16_t onid, tsid, eid;
  epg_broadcast_t *peer_bc;
  channel_t *ch;
  mpegts_mux_t *mm;

  if (len < 5)
    return 0;

  save = 0;
  egtype = (ptr[0] >> 4) & 0x0f;
  ecount = ptr[0] && 0x0f;
  len --;
  ptr ++;

  switch (egtype) {
  case EVENT_GROUP_TYPE_SHARE:
    if (ecount == 0 || len < ecount * 4)
      return 0;

    len = ecount * 4;
    while (len >= 4) {
      epg_broadcast_t *src, *dest;

      peer_bc = _eit_egroup_get_peerbc(ptr, svc->s_dvb_mux, &ch, &eid);
      len -= 4;
      ptr += 4;

      if (!peer_bc || peer_bc->start != bc->start)
        continue;

      if (ecount == 1) {
        src = peer_bc;
        dest = bc;
      } else {
        src = bc;
        dest = peer_bc;
        changes = NULL;
      }

      tvhtrace("eit", "shared events. copy(eid:%5d), orig(eid:%5d) on %s @%"PRItime_t,
                  src->dvb_eid, dest->dvb_eid,
                  ch ? channel_get_name(ch) : "(null)", dest->start);

      /* Copy metadata */
      save |= epg_broadcast_set_is_widescreen(dest, src->is_widescreen, changes);
      save |= epg_broadcast_set_is_hd(dest, src->is_hd, changes);
      save |= epg_broadcast_set_lines(dest, src->lines, changes);
      save |= epg_broadcast_set_aspect(dest, src->aspect, changes);
      save |= epg_broadcast_set_is_deafsigned(dest, src->is_deafsigned, changes);
      save |= epg_broadcast_set_is_subtitled(dest, src->is_subtitled, changes);
      save |= epg_broadcast_set_is_audio_desc(dest, src->is_audio_desc, changes);
      save |= epg_broadcast_set_is_new(dest, src->is_new, changes);
      save |= epg_broadcast_set_is_repeat(dest, src->is_repeat, changes);
      save |= epg_broadcast_set_summary(dest, src->summary, changes);
      save |= epg_broadcast_set_description(dest, src->description, changes);
      save |= epg_broadcast_set_serieslink(dest, src->serieslink, changes);
      save |= epg_broadcast_set_episode(dest, src->episode, changes);
      save |= epg_broadcast_set_relay_dest(dest, src->relay_to_id, changes);
    }
    break;

  case EVENT_GROUP_TYPE_RELAY_X:
    if (ecount != 0 || len < 8)
      return 0;
    onid = ptr[0] << 8 | ptr[1];
    tsid = ptr[2] << 8 | ptr[2];
    len -= 4;
    ptr += 4;
    mm = mpegts_network_find_mux(svc->s_dvb_mux->mm_network, onid, tsid);
    /* fall through */

  case EVENT_GROUP_TYPE_RELAY:
    if (len < 4)
      return 0;
    if (egtype == EVENT_GROUP_TYPE_RELAY)
      mm = svc->s_dvb_mux;
    peer_bc = _eit_egroup_get_peerbc(ptr, mm, &ch, &eid);
    len -= 4;
    ptr += 4;

    if (!peer_bc) {
      /* create temporal dummy program which should be updated by EPG later. */
      time_t start, stop;

      if (ISDB_BC_DUR_UNDEFP(bc)) return 0;
      start = bc->stop;
      stop = start + ISDB_EPG_UNDEF_DUR;
      /* check if another program (!=eid) already exists at the start time */
      if (epg_broadcast_find_by_eid(ch, eid))
        return 0;

      peer_bc  = epg_broadcast_find_by_time(ch, mod, start, stop, 1, &save, changes);
      if (!peer_bc) return 0;
      save |= epg_broadcast_set_dvb_eid(peer_bc, eid, changes);
    }

    save |= epg_broadcast_set_relay_dest(bc, ((epg_object_t *)peer_bc)->id, changes);
    break;

  case EVENT_GROUP_TYPE_MOVE:
    if (ecount < 1 || len < ecount * 4)
      return 0;

    len = ecount * 4;
    while (len >= 4) {
      peer_bc = _eit_egroup_get_peerbc(ptr, svc->s_dvb_mux, &ch, &eid);
      len -= 4;
      ptr += 4;

      if (!peer_bc)
        continue;
      dvr_event_moved(peer_bc, bc);
    }
    break;

  case EVENT_GROUP_TYPE_MOVE_X:
    if (ecount != 0)
      return 0;
    /* target/source may be a shared event */
    while (len >= 8) {
      onid = ptr[0] << 8 | ptr[1];
      tsid = ptr[2] << 8 | ptr[3];
      len -= 4;
      ptr += 4;

      mm = mpegts_network_find_mux(svc->s_dvb_mux->mm_network, onid, tsid);
      peer_bc = _eit_egroup_get_peerbc(ptr, mm, &ch, &eid);
      len -= 4;
      ptr += 4;

      if (!peer_bc)
        continue;
      dvr_event_moved(peer_bc, bc);
    }
    break;

  default:
    return 0;
  }
  return save;
}
#endif /* ENABLE_ISDB */

/* ************************************************************************
 * EIT Event
 * ***********************************************************************/

static int _eit_process_event_one
  ( epggrab_module_t *mod, int tableid, int sect,
    mpegts_service_t *svc, channel_t *ch,
    const uint8_t *ptr, int len,
    int local, int *resched, int *save )
{
  int dllen, save2 = 0;
  time_t start, stop;
  uint16_t eid;
  uint8_t dtag, dlen, running;
  epg_broadcast_t *ebc;
  epg_episode_t *ee = NULL;
  epg_serieslink_t *es;
  epg_running_t run;
  eit_event_t ev;
  uint32_t changes2 = 0, changes3 = 0, changes4 = 0;

#if ENABLE_ISDB
  /* skip events with start time undefined. (maybe delayed and set later) */
  if (!memcmp(&ptr[2], "\xff\xff\xff\xff\xff", 5)) {
    if ( !(tableid == 0x4e && sect == 1 && memcmp(&ptr[7], "\xff\xff\xff", 3)) )
      return -1;
    return 0;
  }

  if (!memcmp(&ptr[7], "\xff\xff\xff", 3) && tableid >= 0x50)
    return -1;
#endif

  /* Core fields */
  eid   = ptr[0] << 8 | ptr[1];
  if (eid == 0) {
    tvhwarn("eit", "found eid==0");
    return -1;
  }
  start = dvb_convert_date(&ptr[2], local);
  stop  = start + bcdtoint(ptr[7] & 0xff) * 3600 +
                  bcdtoint(ptr[8] & 0xff) * 60 +
                  bcdtoint(ptr[9] & 0xff);
  running = (ptr[10] >> 5) & 0x07;

#if ENABLE_ISDB
  /* Set running status for the current event. (since ISDB doesn't set it) */
  if (tableid < 0x50 && sect == 0)
    running = 4; /* running */

  /* prefer EITp over EITsched */
  if (((tableid & 0xf7) == 0x50 || (tableid & 0xf7) == 0x60) &&
      ch->ch_epg_now && ch->ch_epg_now->running == EPG_RUNNING_NOW &&
      start <= ch->ch_epg_now->start)
    return 0;

#endif

  dllen = ((ptr[10] & 0x0f) << 8) | ptr[11];

  len -= 12;
  ptr += 12;
  if ( len < dllen ) return -1;

  /* Find broadcast */
  ebc  = epg_broadcast_find_by_time(ch, mod, start, stop, 1, &save2, &changes2);
  tvhtrace("eit", "svc='%s', ch='%s', eid=%5d, start=%"PRItime_t","
                  " stop=%"PRItime_t", ebc=%p",
           svc->s_dvb_svcname ?: "(null)", ch ? channel_get_name(ch) : "(null)",
           eid, start, stop, ebc);
  if (!ebc) return 0;

  /* Mark re-schedule detect (only now/next) */
  if (save2 && tableid < 0x50) *resched = 1;
  *save |= save2;

  /* Process tags */
  memset(&ev, 0, sizeof(ev));
  ev.default_charset = dvb_charset_find(NULL, NULL, svc);

  while (dllen > 2) {
    int r;
    dtag = ptr[0];
    dlen = ptr[1];

    dllen -= 2;
    ptr   += 2;
    if (dllen < dlen) break;

    tvhtrace(mod->id, "  dtag %02X dlen %d", dtag, dlen);
    tvhlog_hexdump(mod->id, ptr, dlen);

    switch (dtag) {
      case DVB_DESC_SHORT_EVENT:
        r = _eit_desc_short_event(mod, ptr, dlen, &ev);
        break;
      case DVB_DESC_EXT_EVENT:
        r = _eit_desc_ext_event(mod, ptr, dlen, &ev);
        break;
      case DVB_DESC_CONTENT:
        r = _eit_desc_content(mod, ptr, dlen, &ev);
        break;
      case DVB_DESC_COMPONENT:
        r = _eit_desc_component(mod, ptr, dlen, &ev);
#if ENABLE_ISDB
        if (r >= 0 && tableid <= 0x4f && sect == 0) {
          service_t *sv;
          elementary_stream_t *es;

          sv = (service_t *)svc;
          pthread_mutex_lock(&sv->s_stream_mutex);
          es = service_stream_find_tag(sv, ev.sct_props.tag);
          pthread_mutex_unlock(&sv->s_stream_mutex);
          if (es) {
            es->es_stream_tag = ev.sct_props.tag;
            es->es_aspect_num = ev.sct_props.aspect_num;
            es->es_aspect_den = ev.sct_props.aspect_den;
            es->es_height = ev.sct_props.height;
            es->es_width = ev.sct_props.width;
          }
        }
#endif
        break;
      case DVB_DESC_PARENTAL_RAT:
        r = _eit_desc_parental(mod, ptr, dlen, &ev);
        break;
      case DVB_DESC_CRID:
        r = _eit_desc_crid(mod, ptr, dlen, &ev, svc);
        break;
#if ENABLE_ISDB
      case ISDB_DESC_EVENT_GROUP:
        /* process later */
        ev.gdesc_len = dlen;
        ev.group_descriptor = ptr;
        break;
      case ISDB_DESC_SERIES:
        r = _eit_desc_series(mod, ptr, dlen, &ev);
        break;
      case ISDB_DESC_AUDIO_COMPONENT:
        r = _eit_desc_audio_component(mod, ptr, dlen, &ev);
        if (r >= 0 && tableid <= 0x4f && sect == 0) {
          service_t *sv;
          elementary_stream_t *es;

          sv = (service_t *)svc;
          pthread_mutex_lock(&sv->s_stream_mutex);
          es = service_stream_find_tag(sv, ev.sct_props.tag);
          pthread_mutex_unlock(&sv->s_stream_mutex);
          if (es) {
            es->es_stream_tag = ev.sct_props.tag;
            es->es_is_dmono = ev.sct_props.is_dmono;
            memcpy(es->es_lang_sub, ev.sct_props.lang_sub, 4);
          }
        }
        break;
#endif
      default:
        r = 0;
        _eit_dtag_dump(mod, dtag, dlen, ptr);
        break;
    }

    if (r < 0) break;
    dllen -= dlen;
    ptr   += dlen;
  }

  /*
   * Broadcast
   */

  uint16_t prev_eid = ebc->dvb_eid;
  if (epg_broadcast_set_dvb_eid(ebc, eid, &changes2) && prev_eid != 0) {
    tvhinfo("eit", "event %u (ev-id:%u, %s) on %s @ %"PRItime_t
            " was replaced by another(ev-id:%u)", ebc->id, prev_eid,
            epg_broadcast_get_title(ebc, NULL), channel_get_name(ch),
            ebc->start, eid);
    dvr_event_replaced(ebc, (epg_broadcast_t *) 1 /* dummy */);
    *save |= 1;
  }

  /* Summary/Description */
  if (ev.summary)
    *save |= epg_broadcast_set_summary(ebc, ev.summary, &changes2);
  if (ev.desc)
    *save |= epg_broadcast_set_description(ebc, ev.desc, &changes2);

  /* Broadcast Metadata */
  *save |= epg_broadcast_set_is_hd(ebc, ev.hd, &changes2);
#if ENABLE_ISDB
  /* video resolution can change dynamically in ISDB-T */
  if (tableid <= 0x4f && sect == 0) {
    int new_stype;

    new_stype = ev.hd ? ST_HDTV : ST_SDTV;
    if (svc->s_servicetype != new_stype) {
      svc->s_servicetype = new_stype;
      idnode_changed(&svc->s_id);
    }
  }
#endif
  *save |= epg_broadcast_set_is_widescreen(ebc, ev.ws, &changes2);
  *save |= epg_broadcast_set_is_audio_desc(ebc, ev.ad, &changes2);
  *save |= epg_broadcast_set_is_subtitled(ebc, ev.st, &changes2);
  *save |= epg_broadcast_set_is_deafsigned(ebc, ev.ds, &changes2);

  /*
   * Series link
   */

  if (*ev.suri) {
    if ((es = epg_serieslink_find_by_uri(ev.suri, mod, 1, save, &changes3))) {
      *save |= epg_broadcast_set_serieslink(ebc, es, &changes2);
      *save |= epg_serieslink_change_finish(es, changes3, 0);
    }
  }

  /*
   * Episode
   */

  /* Find episode */
  if (*ev.uri) {
    ee = epg_episode_find_by_uri(ev.uri, mod, 1, save, &changes4);
  } else {
    ee = epg_episode_find_by_broadcast(ebc, mod, 1, save, &changes4);
  }

  /* Update Episode */
  if (ee) {
    *save |= epg_broadcast_set_episode(ebc, ee, &changes2);
    *save |= epg_episode_set_is_bw(ee, ev.bw, &changes4);
    if (ev.title)
      *save |= epg_episode_set_title(ee, ev.title, &changes4);
    if (ev.genre)
      *save |= epg_episode_set_genre(ee, ev.genre, &changes4);
    if (ev.parental)
      *save |= epg_episode_set_age_rating(ee, ev.parental, &changes4);
    if (ev.summary)
      *save |= epg_episode_set_summary(ee, ev.summary, &changes4);

#if ENABLE_ISDB
    if ( ev.epnum || ev.epcount ) {
      epg_episode_num_t epnum;

      memset(&epnum, 0, sizeof(epnum));
      epnum.e_num = ev.epnum;
      epnum.e_cnt = ev.epcount;
      *save |= epg_episode_set_epnum(ee, &epnum, &changes4);
    }
#endif

#if TODO_ADD_EXTRA
    if (ev.extra)
      *save |= epg_episode_set_extra(ee, extra, &changes4);
#endif
    /* EITsched_ext contains only extended event desc.
     * so just forcibly "merge" here.
     */
    *save |= epg_episode_change_finish(ee, changes4, 1);
  }

#if ENABLE_ISDB
  /* event group desc is processed last,
   * as it copies some meta info of ebc in event sharing */
  if (ev.gdesc_len > 0)
    *save |= _eit_desc_event_group(mod, ev.group_descriptor, ev.gdesc_len,
                                   ebc, svc, &changes2);
#endif

  /* EIT may describe just a part of an event,
   * so just forcibly "merge" here.
   */
  *save |= epg_broadcast_change_finish(ebc, changes2, 1);

  /* Tidy up */
#if TODO_ADD_EXTRA
  if (ev.extra)   htsmsg_destroy(ev.extra);
#endif
  if (ev.genre)   epg_genre_list_destroy(ev.genre);
  if (ev.title)   lang_str_destroy(ev.title);
  if (ev.summary) lang_str_destroy(ev.summary);
  if (ev.desc)    lang_str_destroy(ev.desc);

  /* use running flag only for current broadcast */
  if (running && tableid == 0x4e) {
    if (sect == 0) {
      switch (running) {
      case 2:  run = EPG_RUNNING_WARM;  break;
      case 3:  run = EPG_RUNNING_PAUSE; break;
      case 4:  run = EPG_RUNNING_NOW;   break;
      default: run = EPG_RUNNING_STOP;  break;
      }
      epg_broadcast_notify_running(ebc, EPG_SOURCE_EIT, run);
    } else if (sect == 1 && running != 2 && running != 3 && running != 4) {
      epg_broadcast_notify_running(ebc, EPG_SOURCE_EIT, EPG_RUNNING_STOP);
    }
  }

  return 0;
}

static int _eit_process_event
  ( epggrab_module_t *mod, int tableid, int sect,
    mpegts_service_t *svc, const uint8_t *ptr, int len,
    int local, int *resched, int *save )
{
  idnode_list_mapping_t *ilm;
  channel_t *ch;

  if ( len < 12 ) return -1;

  LIST_FOREACH(ilm, &svc->s_channels, ilm_in1_link) {
    ch = (channel_t *)ilm->ilm_in2;
    if (!ch->ch_enabled || ch->ch_epg_parent) continue;
    if (_eit_process_event_one(mod, tableid, sect, svc, ch,
                               ptr, len, local, resched, save) < 0)
      return -1;
  }
  return 12 + (((ptr[10] & 0x0f) << 8) | ptr[11]);
}


static int
_eit_callback
  (mpegts_table_t *mt, const uint8_t *ptr, int len, int tableid)
{
  int r;
  int sect, last, ver, save, resched;
  uint8_t  seg;
  uint16_t onid, tsid, sid;
  uint32_t extraid;
  mpegts_service_t     *svc;
  mpegts_mux_t         *mm;
  epggrab_ota_map_t    *map;
  epggrab_module_t     *mod;
  epggrab_ota_mux_t    *ota = NULL;
  mpegts_psi_table_state_t *st;
  th_subscription_t    *ths;
  char ubuf[UUID_HEX_SIZE];

  if (!epggrab_ota_running)
    return -1;

  mm  = mt->mt_mux;
  map = mt->mt_opaque;
  mod = (epggrab_module_t *)map->om_module;

  /* Statistics */
  ths = mpegts_mux_find_subscription_by_name(mm, "epggrab");
  if (ths) {
    subscription_add_bytes_in(ths, len);
    subscription_add_bytes_out(ths, len);
  }

  /* Validate */
  if(tableid < 0x4e || tableid > 0x6f || len < 11) {
    if (ths)
      ths->ths_total_err++;
    return -1;
  }

  /* Basic info */
  sid     = ptr[0] << 8 | ptr[1];
  tsid    = ptr[5] << 8 | ptr[6];
  onid    = ptr[7] << 8 | ptr[8];
  seg     = ptr[9];
  extraid = ((uint32_t)tsid << 16) | sid;
  // TODO: extra ID should probably include onid

  /* Register interest */
  if (tableid == 0x4e || (tableid >= 0x50 && tableid < 0x60))
    ota = epggrab_ota_register((epggrab_module_ota_t*)mod, NULL, mm);

  /* Begin */
  r = dvb_table_begin((mpegts_psi_table_t *)mt, ptr, len,
                      tableid, extraid, 11, &st, &sect, &last, &ver);
  if (r == 0) goto complete;
  if (r < 0) return r;
  if (tableid != 0x4e && r != 1) return r;
  if (st && r > 0) {
    uint32_t mask;
    int sa = seg & 0xF8;
    int sb = 7 - (seg & 0x07);
    mask = (~(0xFF << sb) & 0xFF);
    mask <<= (24 - (sa % 32));
    st->sections[sa/32] &= ~mask;
  }

  /* Get transport stream */
  // Note: tableid=0x4f,0x60-0x6f is other TS
  //       so must find the tdmi
  if(tableid == 0x4f || tableid >= 0x60) {
    mm = mpegts_network_find_mux(mm->mm_network, onid, tsid);

  } else {
    if ((mm->mm_tsid != tsid || mm->mm_onid != onid) &&
        !mm->mm_eit_tsid_nocheck) {
      if (mm->mm_onid != MPEGTS_ONID_NONE &&
          mm->mm_tsid != MPEGTS_TSID_NONE)
        tvhtrace("eit",
                "invalid tsid found tid 0x%02X, onid:tsid %d:%d != %d:%d",
                tableid, mm->mm_onid, mm->mm_tsid, onid, tsid);
      mm = NULL;
    }
  }
  if(!mm)
    goto done;

  /* Get service */
  svc = mpegts_mux_find_service(mm, sid);
  if (!svc) {
    tvhtrace("eit", "sid %i not found", sid);
    goto done;
  }

  if (map->om_first) {
    map->om_tune_count++;
    map->om_first = 0;
  }

  /* Register this */
  if (ota)
    epggrab_ota_service_add(map, ota, idnode_uuid_as_str(&svc->s_id, ubuf), 1);

  /* No point processing */
  if (!LIST_FIRST(&svc->s_channels))
    goto done;

  if (svc->s_dvb_ignore_eit)
    goto done;

  /* Process events */
  save = resched = 0;
  len -= 11;
  ptr += 11;
  while (len) {
    int r;
    if ((r = _eit_process_event(mod, tableid, sect, svc, ptr, len,
                                mm->mm_network->mn_localtime,
                                &resched, &save)) < 0)
      break;
    assert(r > 0);
    len -= r;
    ptr += r;
  }

  /* Update EPG */
  if (resched) epggrab_resched();
  if (save)    epg_updated();
  
done:
  r = dvb_table_end((mpegts_psi_table_t *)mt, st, sect);
complete:
  if (ota && !r && (tableid == 0x4e || (tableid >= 0x50 && tableid < 0x60)))
    epggrab_ota_complete((epggrab_module_ota_t*)mod, ota);
  
  return r;
}

/* ************************************************************************
 * Module Setup
 * ***********************************************************************/

static int _eit_start
  ( epggrab_ota_map_t *map, mpegts_mux_t *dm )
{
  epggrab_module_ota_t *m = map->om_module;
  int pid, opts = 0;

  /* Disabled */
  if (!m->enabled && !map->om_forced) return -1;

  /* Freeview (switch to EIT, ignore if explicitly enabled) */
  // Note: do this as PID is the same
  if (!strcmp(m->id, "uk_freeview")) {
    m = (epggrab_module_ota_t*)epggrab_module_find_by_id("eit");
    if (m->enabled) return -1;
  }

  /* Freesat (3002/3003) */
  if (!strcmp("uk_freesat", m->id)) {
    mpegts_table_add(dm, 0, 0, dvb_bat_callback, NULL, "bat", MT_CRC, 3002, MPS_WEIGHT_EIT);
    pid = 3003;

  /* Viasat Baltic (0x39) */
  } else if (!strcmp("viasat_baltic", m->id)) {
    pid = 0x39;

  /* Bulsatcom 39E (0x12b) */
  } else if (!strcmp("Bulsatcom_39E", m->id)) {
    pid = 0x12b;

  /* Standard (0x12) */
  } else {
    pid  = DVB_EIT_PID;
    opts = MT_RECORD;
  }
  mpegts_table_add(dm, 0, 0, _eit_callback, map, m->id, MT_CRC | opts, pid, MPS_WEIGHT_EIT);
  // TODO: might want to limit recording to EITpf only
  tvhlog(LOG_DEBUG, m->id, "installed table handlers");
  return 0;
}

static int _eit_tune
  ( epggrab_ota_map_t *map, epggrab_ota_mux_t *om, mpegts_mux_t *mm )
{
  int r = 0;
  epggrab_module_ota_t *m = map->om_module;
  mpegts_service_t *s;
  epggrab_ota_svc_link_t *osl, *nxt;

  lock_assert(&global_lock);

  /* Disabled */
  if (!m->enabled) return 0;

  /* Have gathered enough info to decide */
  if (!om->om_complete)
    return 1;

  /* Check if any services are mapped */
  // TODO: using indirect ref's like this is inefficient, should 
  //       consider changeing it?
  for (osl = RB_FIRST(&map->om_svcs); osl != NULL; osl = nxt) {
    nxt = RB_NEXT(osl, link);
    /* rule: if 5 mux scans fail for this service, remove it */
    if (osl->last_tune_count + 5 <= map->om_tune_count ||
        !(s = mpegts_service_find_by_uuid(osl->uuid))) {
      epggrab_ota_service_del(map, om, osl, 1);
    } else {
      if (LIST_FIRST(&s->s_channels))
        r = 1;
    }
  }

  return r;
}

void eit_init ( void )
{
  static epggrab_ota_module_ops_t ops = {
    .start = _eit_start,
    .tune  = _eit_tune,
  };

  epggrab_module_ota_create(NULL, "eit", NULL, "EIT: DVB Grabber", 1, &ops);
  epggrab_module_ota_create(NULL, "uk_freesat", NULL, "UK: Freesat", 5, &ops);
  epggrab_module_ota_create(NULL, "uk_freeview", NULL, "UK: Freeview", 5, &ops);
  epggrab_module_ota_create(NULL, "viasat_baltic", NULL, "VIASAT: Baltic", 5, &ops);
  epggrab_module_ota_create(NULL, "Bulsatcom_39E", NULL, "Bulsatcom: Bula 39E", 5, &ops);
}

void eit_done ( void )
{
}

void eit_load ( void )
{
}
