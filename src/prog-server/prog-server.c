/*
 * prog-server.c - passthrough server implementation
 *
 * Copyright (C) 2001 Raimund Jacob <raimi@lkcc.org>
 * Copyright (C) 2001 Stefan Jahn <stefan@lkcc.org>
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
 * $Id: prog-server.c,v 1.6 2001/11/25 15:51:16 ela Exp $
 *
 */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if ENABLE_PROG_SERVER

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "libserveez.h"
#include "prog-server.h"

/*
 * Default configuration definition.
 */
prog_config_t prog_config = 
{
  NULL,
  NULL,
  NULL,
  NULL,
  1,
  1,
  NULL
};

/*
 * Defining configuration file associations by key-value-pairs.
 */
svz_key_value_pair_t prog_config_prototype [] = 
{
  SVZ_REGISTER_STR ("binary", prog_config.bin, SVZ_ITEM_NOTDEFAULTABLE),
  SVZ_REGISTER_STR ("directory", prog_config.dir, SVZ_ITEM_DEFAULTABLE),
  SVZ_REGISTER_STR ("user", prog_config.user, SVZ_ITEM_DEFAULTABLE),
  SVZ_REGISTER_STRARRAY ("argv", prog_config.argv, SVZ_ITEM_DEFAULTABLE),
  SVZ_REGISTER_BOOL ("do-fork", prog_config.fork, SVZ_ITEM_DEFAULTABLE),
  SVZ_REGISTER_BOOL ("single-threaded", 
		     prog_config.single_threaded, SVZ_ITEM_DEFAULTABLE),
  SVZ_REGISTER_END ()
};

/*
 * Definition of this server.
 */
svz_servertype_t prog_server_definition =
{
  "program passthrough server",
  "prog",
  prog_global_init,
  prog_init,
  prog_detect_proto,
  prog_connect_socket,
  prog_finalize,
  prog_global_finalize,
  prog_info_client,
  prog_info_server,
  prog_notify,
  prog_handle_request,
  SVZ_DEFINE_CONFIG (prog_config, prog_config_prototype)
};

/* 
 * Handle request callback. Not yet used.
 */
int 
prog_handle_request (svz_socket_t *sock, char *request, int len)
{
  return -1;
}

/*
 * Protocol detection callback. Always returns success, because there is
 * no use to detect a client.
 */
int
prog_detect_proto (svz_server_t *server, svz_socket_t *sock)
{
  return -1;
}

/*
 * Wrapper function for Serveez's API passthrough call depending on the
 * server configuration.
 */
static int
prog_passthrough (svz_socket_t *sock)
{
  prog_config_t *cfg = sock->cfg;
  char **argv = (char **) svz_array_values (cfg->argv);
  int pid;

  if ((pid = svz_sock_process (sock, cfg->bin, cfg->dir, argv, NULL,
			       cfg->fork ? SVZ_PROCESS_FORK :
			       SVZ_PROCESS_SHUFFLE_SOCK,
			       cfg->user ? cfg->user :	SVZ_PROCESS_NONE)) < 0)
    {
      svz_log (LOG_ERROR, "prog: cannot execute `%s'\n", cfg->bin);
      svz_free (argv);
      return -1;
    }
  sock->pid = (HANDLE) pid;
  svz_free (argv);
  return 0;
}

/*
 * The connect callback is invoked when the above detection routine returned
 * success. This means, the routine will be called immediately after the
 * the connection has been accepted. 
 */
int
prog_connect_socket (svz_server_t *server, svz_socket_t *sock)
{
  prog_config_t *cfg = server->cfg;

  /* Passthrough the connection. */
  if (prog_passthrough (sock))
    return -1;

  if (cfg->fork)
    {
      /* Prevent anything being read from the socket. */
      sock->read_socket = NULL;

      /* Just close() this end of socket, not shutdown(). fork() makes the 
	 socket available in the child process. When we shutdown() it here, 
	 it dies in the child, too. When we just close() it, it still works
	 in the child. */ 
      sock->flags |= SOCK_FLAG_NOSHUTDOWN;
      return -1;
    }

  return 0;
}

/*
 * Global initializer. Not used yet.
 */
int
prog_global_init (svz_servertype_t *server)
{
  return 0;
}

/*
 * Global finalizer. Not used yet.
 */
int
prog_global_finalize (svz_servertype_t *server)
{
  return 0;
}

/*
 * Server finalizer. Not used yet.
 */
int
prog_finalize (svz_server_t *server)
{
  return 0;
}

/*
 * This is the @code{child_died} callback for UDP and ICMP versions of the 
 * server. Reassigns the @code{read_socket} callback in order to accept new
 * incoming packets.
 */
int
prog_child_died (svz_socket_t *sock)
{
  sock->read_socket = prog_read_socket;
  sock->pid = INVALID_HANDLE;
  return 0;
}

/*
 * This is the @code{read_socket} callback for UDP and ICMP versions of the 
 * server. It does not read anything from the underlying socket in order to
 * pass the data directly to the child program.
 */
int
prog_read_socket (svz_socket_t *sock)
{
  prog_config_t *cfg = sock->cfg;

  /* Passthrough the connection. */
  if (prog_passthrough (sock))
    return -1;

  if (cfg->single_threaded)
    {
      /* Disable the read callback and wait for the child process to die. */
      sock->child_died = prog_child_died;
      sock->read_socket = NULL;
    }

  return 0;
}

/*
 * This is the @code{check_request} callback for the shuffling UDP and ICMP
 * versions of the server. It restores the old @code{check_request} callback
 * and runs it.
 */
int
prog_check_request (svz_socket_t *sock)
{
  prog_config_t *cfg = sock->cfg;

  /* Passthrough the connection. */
  if (prog_passthrough (sock))
    return -1;

  /* Restore old handler and run it. */
  sock->check_request = cfg->check_request;
  return sock->check_request (sock);
}

/*
 * Server initializer. Checks its configuration.
 */
int
prog_init (svz_server_t *server)
{
  prog_config_t *cfg = server->cfg;
  svz_array_t *listeners;
  svz_socket_t *sock;
  int i, ret = 0;

  /* Check for listeners. */
  if ((listeners = svz_server_listeners (server)) != NULL)
    {
      /* Check each listener. */
      svz_array_foreach (listeners, sock, i)
	{
	  /* Is it a UPD or ICMP port (packet oriented) ? */
	  if (sock->proto & (PROTO_UDP | PROTO_ICMP))
	    {
	      /* Require non-shared listener. */
	      if (!svz_server_single_listener (server, sock))
		{
		  svz_log (LOG_ERROR, 
			   "prog: refusing to initialize shared listener "
			   "on port `%s'\n", 
			   ((svz_portcfg_t *) (sock->port))->name);
		  ret = -1;
		}
	      /* Prepare callbacks for packet oriented servers. */
	      else
		{
		  /* Save the server configuration. */
		  sock->cfg = cfg;
		  if (cfg->fork)
		    {
		      /* Direct fork()'s do not need to receive. */
		      sock->read_socket = prog_read_socket;
		    }
		  else
		    {
		      /* Save old handler and set one. */
		      cfg->check_request = sock->check_request;
		      sock->check_request = prog_check_request;
		    }
		}
	    }
	}
      svz_array_destroy (listeners);
    }

  /* Create default argument array. */
  if (cfg->argv == NULL)
    {
      cfg->argv = svz_array_create (1, svz_free);
      svz_array_add (cfg->argv, NULL);
    }

  return ret;
}

/*
 * Notify callback. Not used yet.
 */
int
prog_notify (svz_server_t *server)
{
  return 0;
}

/*
 * Info client callback. Not used yet.
 */
char *
prog_info_client (svz_server_t *server, svz_socket_t *sock)
{
  return NULL;
}

/*
 * Info server callback. Not used yet.
 */
char *
prog_info_server (svz_server_t *server)
{
  return NULL;
}

#else /* not ENABLE_PROG_SERVER */

static int have_prog = 0;

#endif /* not ENABLE_PROG_SERVER */