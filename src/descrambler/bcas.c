/*
 * bcas.c - libdemulti2 caclient for BCAS in ISDB-T/-S
 * 
 * Copyright 2016 0p1pp1 <0p1pp1@fms.freenet>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */

#include <pthread.h>

#include "tvheadend.h"

#include "caclient.h"
#include "caid.h"
#include "input.h"
#include "service.h"
#include "tvhcsa.h"

/* API for libyakisoba */
/**
 * \brief Decrypt an ECM payload
 * 
 * @param[in]  Payload  Pointer to the head of the (encrypted) ECM payload.
 * @param[in]  Size     Length of the payload, including MAC field.
 * @param[out] Keys     Decrypted scramble keys (odd, even), must be >= u8[2][8].
 * @param[out] VarPart  Decrypted variable part in the ECM body if they exit.
 *                      NULL or ( @a Payload + 26 ) is allowed.
 *                      if !NULL, must be >= (@a Size - 30).
 * @return              0 on success, < 0 on error.
 * @retval -EINVAL      @a Paylod is NULL, or @a Size too small
 * @retval -ENOKEY      Missing the work key for BroadcasterGroupD and WorkKeyID
 * @retval -EILSEQ      MAC mismatch
 */
extern int
bcas_decodeECM(const uint8_t *Payload, uint32_t Size, uint8_t *Keys, uint8_t *VarPart);


const idclass_t caclient_bcas_class =
{
  .ic_super      = &caclient_class,
  .ic_class      = "caclient_bcas",
  .ic_caption    = N_("BCAS (MULTI2)"),
};


typedef struct bcas_ecm_st {
  uint16_t pid;
  uint8_t  to_be_removed;

  SLIST_ENTRY(bcas_ecm_st) link;
} bcas_ecm_st_t;


typedef struct bcas_desrambler {
  th_descrambler_t;

  SLIST_HEAD(, bcas_ecm_st) ecm_streams;

  tvhlog_limit_t err_log;
} bcas_descrambler_t;



static void
bcas_table_input(void *opaque, int pid, const uint8_t *data, int len, int emm)
{
  bcas_descrambler_t *bd = opaque;
  int ret;
  uint8_t keys[2][8];

  if (emm || len < 38 || data[0] != 0x82)
    return;

  ret = bcas_decodeECM(data + 8, len - 8 - 4, (uint8_t *)keys, NULL);
  if (ret) {
    if (tvhlog_limit(&bd->err_log, 10))
      tvhinfo("bcas", "failed to decode ECM. errno:%d", -ret);
    bd->td_keystate = DS_FORBIDDEN;
    return;
  }

  descrambler_keys(opaque, DESCRAMBLER_MULTI2, keys[1], keys[0]);
}


static int
bcas_ecm_reset(th_descrambler_t *td)
{
  td->td_keystate = DS_UNKNOWN;
  return 0;
}


static void
bcas_service_stop(th_descrambler_t *td)
{
  bcas_descrambler_t *bd;
  mpegts_mux_t *mux;
  bcas_ecm_st_t *est;

  bd = (bcas_descrambler_t *) td;
  mux = ((mpegts_service_t *) td->td_service)->s_dvb_mux;

  while ((est = SLIST_FIRST(&bd->ecm_streams))) {
    descrambler_close_pid(mux, bd, est->pid);
    SLIST_REMOVE_HEAD(&bd->ecm_streams, link);
    free(est);
  }

  LIST_REMOVE(td, td_service_link);
  free(bd);
}


static void
bcas_caid_change(th_descrambler_t *td)
{
  bcas_descrambler_t *bd;
  mpegts_service_t *t;
  mpegts_mux_t *mux;
  bcas_ecm_st_t *est;
  elementary_stream_t *st;

  bd = (bcas_descrambler_t *) td;
  t = (mpegts_service_t *) td->td_service;
  mux = t->s_dvb_mux;

  SLIST_FOREACH(est, &bd->ecm_streams, link) {
    est->to_be_removed = 1;
  }

  TAILQ_FOREACH(st, &t->s_filt_components, es_filt_link) {
    caid_t *c;

    if (st->es_type != SCT_CA)
      continue;
    if (t->s_dvb_prefcapid_lock == PREFCAPID_FORCE &&
        t->s_dvb_prefcapid != st->es_pid)
      continue;

    LIST_FOREACH(c, &st->es_caids, link) {
      if(c == NULL || c->use == 0 || c->caid != CAID_BCAS)
        continue;
      if (t->s_dvb_forcecaid && t->s_dvb_forcecaid != c->caid)
        continue;

      SLIST_FOREACH(est, &bd->ecm_streams, link) {
        if (est->pid == c->pid)
          break;
      }
      if (est) {
        est->to_be_removed = 0;
        break;
      }

      est = calloc(1, sizeof(*est));
      est->pid = c->pid;
      SLIST_INSERT_HEAD(&bd->ecm_streams, est, link);
      descrambler_open_pid(mux, bd, DESCRAMBLER_ECM_PID(est->pid),
                           bcas_table_input, td->td_service);
    }
  }

  for(est = SLIST_FIRST(&bd->ecm_streams); est;) {
    bcas_ecm_st_t *next = SLIST_NEXT(est, link);

    if (est->to_be_removed) {
      descrambler_close_pid(mux, bd, est->pid);
      SLIST_REMOVE(&bd->ecm_streams, est, bcas_ecm_st, link);
      free(est);
    }
    est = next;
  }
}


static void
bcas_service_start(caclient_t *cac, service_t *s)
{
  mpegts_service_t *t = (mpegts_service_t *)s;
  th_descrambler_t *td;

  extern const idclass_t mpegts_service_class;
  if (!idnode_is_instance(&t->s_id, &mpegts_service_class))
    return;

  LIST_FOREACH(td, &t->s_descramblers, td_service_link)
    if (td->td_stop == bcas_service_stop && td->td_service == s)
      break;

  if (!td) {
    bcas_descrambler_t *bd;
    char buf[128];

    tvhdebug("bcas", "creating descrambler for svc:%s", service_nicename(s));

    bd = calloc(1, sizeof(*bd));
    if (!bd) {
      tvherror("bcas", "failed to alloc descrambler for svc:%s",
               service_nicename(s));
      return;
    }
    SLIST_INIT(&bd->ecm_streams);

    td = (th_descrambler_t *)bd;

    snprintf(buf, sizeof(buf), "bcas-%d-%d-%d",
             t->s_dvb_mux->mm_onid, t->s_dvb_mux->mm_tsid, t->s_dvb_service_id);
    td->td_nicename    = strdup(buf);
    td->td_service     = s;
    td->td_stop        = bcas_service_stop;
    td->td_caid_change = bcas_caid_change;
    td->td_ecm_reset   = bcas_ecm_reset;

    LIST_INSERT_HEAD(&t->s_descramblers, td, td_service_link);
  }

  tvhdebug("bcas", "starting descrambler for svc:%s", service_nicename(s));
  bcas_caid_change(td);
}


static void
bcas_conf_changed(caclient_t *cac)
{
  if (cac->cac_enabled)
    caclient_set_status(cac, CACLIENT_STATUS_CONNECTED);
  else
    caclient_set_status(cac, CACLIENT_STATUS_NONE);
}


caclient_t *
bcas_create(void)
{
  caclient_t *cac = calloc(1, sizeof(*cac));

  if (!cac) {
    tvherror("bcas", "failed to alloc caclient.");
    return NULL;
  }

  cac->cac_start = bcas_service_start;
  cac->cac_conf_changed = bcas_conf_changed;

  return cac;
}
