/*
 * portcfg.h - port configuration interface
 *
 * Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * $Id: portcfg.h,v 1.2 2001/04/06 15:32:35 raimi Exp $
 *
 */

#ifndef __PORTCFG_H__
#define __PORTCFG_H__ 1

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "libserveez/defines.h"
#include "libserveez/array.h"
#include "libserveez/hash.h"
#include "libserveez/pipe-socket.h"

/* Port configuration items. */
#define PORTCFG_PORT    "port"
#define PORTCFG_PROTO   "proto"
#define PORTCFG_INPIPE  "inpipe"
#define PORTCFG_OUTPIPE "outpipe"
#define PORTCFG_TCP     "tcp"
#define PORTCFG_UDP     "udp"
#define PORTCFG_ICMP    "icmp"
#define PORTCFG_RAW     "raw"
#define PORTCFG_PIPE    "pipe"
#define PORTCFG_IP      "ipaddr"
#define PORTCFG_NOIP    "*"

/*
 * Definition of a single port configuration reflecting either a network
 * (TCP, UDP, ICMP or RAW) or filesystem configuration (PIPE).
 */
typedef struct svz_portcfg
{
  /* the symbolic name of this port configuration */
  char *name;

  /* one of the PROTO_ flags defined in <core.h> */
  int proto;

  /* unified structure for each type of port */
  union protocol_t
  {
    /* tcp port */
    struct tcp_t
    {
      unsigned short port;      /* TCP/IP port */
      char *ipaddr;             /* dotted decimal or "*" for any address */
      struct sockaddr_in *addr; /* converted from the above 2 values */
      int backlog;              /* backlog argument for listen() */
    } tcp;

    /* udp port */
    struct udp_t
    {
      unsigned short port;      /* UDP port */
      char *ipaddr;             /* dotted decimal or "*" */
      struct sockaddr_in *addr; /* converted from the above 2 values */
    } udp;

    /* icmp port */
    struct icmp_t
    {
      char *ipaddr;             /* dotted decimal or "*" */
      struct sockaddr_in *addr; /* converted from the above value */
      unsigned char type;       /* message type */
    } icmp;

    /* raw ip port */
    struct raw_t
    {
      char *ipaddr;             /* dotted decimal or "*" */
      struct sockaddr_in *addr; /* converted from the above value */
    } raw;

    /* pipe port */
    struct pipe_t
    {
      svz_pipe_t recv;         /* pipe for sending data into serveez */
      svz_pipe_t send;         /* pipe serveez sends responses out on */
    } pipe;
  }
  protocol;

  /* maximum number of bytes for protocol identification */
  int detection_fill;

  /* maximum seconds for protocol identification */
  int detection_wait;

  /* initial buffer sizes */
  int send_buffer_size;
  int recv_buffer_size;

  /* allowed number of connects per second (hammer protection) */
  int connect_freq;

  /* remembers connect frequency for each ip */
  svz_hash_t *accepted;

  /* denied and allowed access list (ip based) */
  svz_array_t *deny;
  svz_array_t *allow;
}
svz_portcfg_t;

/* 
 * Accessor definitions for each type of protocol. 
 */
#define tcp_port protocol.tcp.port
#define tcp_addr protocol.tcp.addr
#define tcp_ipaddr protocol.tcp.ipaddr

#define udp_port protocol.udp.port
#define udp_addr protocol.udp.addr
#define udp_ipaddr protocol.udp.ipaddr

#define icmp_addr protocol.icmp.addr
#define icmp_ipaddr protocol.icmp.ipaddr
#define icmp_type protocol.icmp.type

#define raw_addr protocol.raw.addr
#define raw_ipaddr protocol.raw.ipaddr

#define pipe_recv protocol.pipe.recv
#define pipe_send protocol.pipe.send

#define svz_portcfg_create() \
  (svz_portcfg_t *) svz_calloc (sizeof (svz_portcfg_t))

__BEGIN_DECLS

SERVEEZ_API int svz_portcfg_equal __P ((svz_portcfg_t *, svz_portcfg_t *));
SERVEEZ_API svz_portcfg_t *svz_portcfg_add __P ((char *, svz_portcfg_t *));
SERVEEZ_API svz_portcfg_t *svz_portcfg_del __P ((char *));
SERVEEZ_API svz_portcfg_t *svz_portcfg_get __P ((char *));
SERVEEZ_API void svz_portcfg_destroy __P ((svz_portcfg_t *port));
SERVEEZ_API void svz_portcfg_finalize __P ((void));

__END_DECLS

#endif /* __PORTCFG_H__ */