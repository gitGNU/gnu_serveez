/*
 * irc-server.c - IRC server connection routine
 *
 * Copyright (C) 2000 Stefan Jahn <stefan@lkcc.org>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this package; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 *
 * $Id: irc-server.c,v 1.5 2000/06/19 15:24:50 ela Exp $
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if ENABLE_IRC_PROTO

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef __MINGW32__
# include <winsock.h>
#endif

#ifndef __MINGW32__
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
# include <arpa/inet.h>
#endif

#include "alloc.h"
#include "util.h"
#include "socket.h"
#include "snprintf.h"
#include "irc-proto.h"
#include "irc-event.h"
#include "irc-server.h"
#include "connect.h"
#include "coserver/coserver.h"

#define DEFAULT_PORT 6667

irc_server_t  *irc_server_list;  /* server list root */

#define MAX_HOST_LEN 256
#define MAX_PASS_LEN 256
#define MAX_NAME_LEN 256

/*
 * Parse one of the config lines in the IRC configuration.
 * This function has exactly the same syntax as sscanf() but
 * recognizes only %s and %d for string and integers. Strings
 * will be parsed until the next character in the format string.
 */
int
irc_parse_line(char *line, char *fmt, ...)
{
  va_list args;
  int *i;
  char *s;
  int ret;

  va_start(args, fmt);
  ret = 0;

  while(*fmt && *line)
    {
      /* next arg */
      if(*fmt == '%')
	{
	  fmt++;

	  /* a decimal */
	  if(*fmt == 'd')
	    {
	      i = va_arg(args, int *);
	      *i = 0;
	      fmt++;
	      while(*line && *line >= '0' && *line <= '9')
		{
		  *i *= 10;
		  *i += *line - '0';
		  line++;
		}
	    }
	  /* a string */
	  else if(*fmt == 's')
	    {
	      s = va_arg(args, char *);
	      fmt++;
	      while(*line && *line != *fmt)
		{
		  *s = *line;
		  s++;
		  line++;
		}
	      *s = 0;
	    }
	  ret++;
	}
      /* not an arg */
      else if(*fmt != *line)
	{
	  break;
	}
      fmt++;
      line++;
    }

  va_end(args);
  return ret;
}

#if ENABLE_DNS_LOOKUP
/*
 * This will be called if a DNS lookup for a remote irc server has
 * been done. Here we connect to this server then. Return non-zero on
 * errors.
 */
int
irc_connect_server (irc_server_t *server, char *ip)
{
  irc_config_t *cfg = server->cfg;
  socket_t sock;
  irc_client_t **cl;
  irc_channel_t **ch;
  char nicklist[MAX_MSG_LEN];
  int n, i;

  /* check if dns lookup was successful */
  if (!ip)
    {
      log_printf (LOG_ERROR, "irc: cannot connect to %s\n", server->realhost);
      return -1;
    }
  
  /* try connecting */
  server->addr = inet_addr (ip);
  if (!(sock = sock_connect (server->addr, server->port)))
    {
      return -1;
    }

  log_printf (LOG_NOTICE, "irc: connecting to %s\n", server->realhost);
  sock->data = server;
  server->id = sock->socket_id;
  sock->userflags |= IRC_FLAG_SERVER;
  sock->check_request = irc_check_request;

  /* send initial requests introducing this server */
#ifndef ENABLE_TIMESTAMP
  irc_printf (sock, "PASS %s\n", server->pass);
#else
  irc_printf (sock, "PASS %s %s\n", server->pass, TS_PASS);
#endif
  irc_printf (sock, "SERVER %s 1 :%s\n", cfg->host, cfg->info);

#if ENABLE_TIMESTAMP
  irc_printf (sock, "SVINFO %d %d %d :%d\n",
	      TS_CURRENT, TS_MIN, 0, time(NULL) + cfg->tsdelta);
#endif

  /* now propagate user information to this server */
  if ((cl = (irc_client_t **) hash_values (cfg->clients)) != NULL)
    {
      for (n = 0; n < hash_size (cfg->clients); n++)
	{
#if ENABLE_TIMESTAMP
	  irc_printf (sock, "NICK %s %d %d %s %s %s %s :%s\n",
		      cl[n]->nick, cl[n]->hopcount, cl[n]->since, 
		      get_client_flags (cl[n]), 
		      cl[n]->user, cl[n]->host,
		      cl[n]->server, "EFNet?");
#else
	  irc_printf (sock, "NICK %s\n", cl[n]->nick);
	  irc_printf (sock, "USER %s %s %s %s\n", 
		      cl[n]->user, cl[n]->host, 
		      cl[n]->server, cl[n]->real);
	  irc_printf (sock, "MODE %s %s\n", 
		      cl[n]->nick, get_client_flags(cl[n]));
#endif
	}
      xfree (cl);
    }

  /* propagate all channel information to the server */
  if ((ch = (irc_channel_t **) hash_values (cfg->channels)) != NULL)
    {
      for (i = 0; i < hash_size (cfg->channels); i++)
	{
#if ENABLE_TIMESTAMP
	  /* create nick list */
	  for (nicklist[0] = 0, n = 0; n < ch[n]->clients; n++)
	    {
	      if(ch[i]->cflag[n] & MODE_OPERATOR)
		strcat (nicklist, "@");
	      else if (ch[i]->cflag[n] & MODE_VOICE)
		strcat (nicklist, "+");
	      strcat (nicklist, ch[i]->client[n]->nick);
	      strcat (nicklist, " ");
	    }
	}
      /* propagate one channel in one request */
      irc_printf (sock, "SJOIN %d %s %s :%s\n",
		  ch[i]->since, ch[i]->name, get_channel_flags(ch[i]),
		  nicklist);
#else
      for (n = 0; n < ch[i]->clients; n++)
	{
	  irc_printf (sock, ":%s JOIN %s\n", 
		      ch[i]->client[n], ch[i]->name);
	}
      irc_printf (sock, "MODE %s %s\n", 
		  ch[i]->name, get_channel_flags(ch[i]));
#endif
    }
  xfree (ch);
  
  return 0;
}
#endif /* ENABLE_DNS_LOOKUP */

/*
 * Go through all C lines in the IRC server configuration
 * and resolve all hosts.
 */
void
irc_connect_servers (irc_config_t *cfg)
{
  char realhost[MAX_HOST_LEN];
  char pass[MAX_PASS_LEN];
  char host[MAX_NAME_LEN];
  int port;
  int class;
  irc_server_t *ircserver;
  char *cline;
  int n;

  /* any C lines at all ? */
  if (!cfg->CLine) return;

  /* go through all C lines */
  n = 0;
  while ((cline = cfg->CLine[n++]) != NULL)
    {
      /* scan the actual C line */
      irc_parse_line (cline, "C:%s:%s:%s:%d:%d", 
		      realhost, pass, host, &port, &class);
      
      /* create new IRC server structure */
      ircserver = xmalloc (sizeof (irc_server_t));
      ircserver->port = htons (port);
      ircserver->id = -1;
      ircserver->realhost = xmalloc (strlen (realhost) + 1);
      strcpy (ircserver->realhost, realhost);
      ircserver->host = xmalloc (strlen (host) + 1);
      strcpy (ircserver->host, host);
      ircserver->pass = xmalloc (strlen (pass) + 1);
      strcpy (ircserver->pass, pass);
      ircserver->cfg = cfg;
      ircserver->next = NULL;

      /* add this server to the server list */
      log_printf (LOG_NOTICE, "irc: enqueuing %s\n", ircserver->realhost);
      irc_add_server (cfg, ircserver);
      coserver_dns (realhost, (coserver_handle_result_t) irc_connect_server,
		    ircserver);

    }
}

/*
 * Add an IRC server to the server list.
 */
irc_server_t *
irc_add_server (irc_config_t *cfg, irc_server_t *server)
{
  server->next = cfg->servers;
  cfg->servers = server;
    
  return cfg->servers;
}

/*
 * Delete all IRC servers.
 */
void
irc_delete_servers (irc_config_t *cfg)
{
  while (cfg->servers)
    {
      irc_del_server (cfg, cfg->servers);
    }
}

/*
 * Delete an IRC server of the current list.
 */
void
irc_del_server (irc_config_t *cfg, irc_server_t *server)
{
  irc_server_t *srv;
  irc_server_t *prev;

  prev = srv = cfg->servers;
  while (srv)
    {
      if (srv == server)
	{
	  xfree (server->realhost);
	  xfree (server->host);
	  xfree (server->pass);
	  if (prev == srv)
	    cfg->servers = server->next;
	  else
	    prev->next = server->next;
	  xfree (server);
	  return;
	}
      prev = srv;
      srv = srv->next;
    }
}

/*
 * Find an IRC server in the current list.
 */
irc_server_t *
irc_find_server(void)
{
  return NULL;
}

#else /* not ENABLE_IRC_PROTO */

int irc_server_dummy;

#endif /* ENABLE_IRC_PROTO */
