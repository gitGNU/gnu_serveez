/*
 * server-core.c - server core implementation
 *
 * Copyright (C) 2011 Thien-Thi Nguyen
 * Copyright (C) 2000, 2001, 2002, 2003 Stefan Jahn <stefan@lkcc.org>
 * Copyright (C) 2000 Raimund Jacob <raimi@lkcc.org>
 * Copyright (C) 1999 Martin Grabmueller <mgrabmue@cs.tu-berlin.de>
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>

#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#if HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#if HAVE_SYS_RESOURCE_H && !defined (__MINGW32__)
# include <sys/resource.h>
#endif

#include "networking-headers.h"
#include "woe-wait.h"

#ifdef __MINGW32__
# include <process.h>
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netdb.h>
# if HAVE_WAIT_H
#  include <wait.h>
# endif
# if HAVE_SYS_WAIT_H
#  include <sys/wait.h>
# endif
# if HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif

#include "libserveez/alloc.h"
#include "libserveez/util.h"
#include "libserveez/array.h"
#include "libserveez/socket.h"
#include "libserveez/core.h"
#include "libserveez/pipe-socket.h"
#include "libserveez/portcfg.h"
#include "libserveez/interface.h"
#include "libserveez/coserver/coserver.h"
#include "libserveez/server.h"
#include "libserveez/server-core.h"
#include "misc-macros.h"

/*
 * When set to a non-zero value, the server
 * will terminate its main loop.
 */
int svz_nuke_happened = 0;

/*
 * When set to a non-zero value, the server
 * will try to re-initialize itself on the next execution of the main
 * loop.
 */
static int svz_reset_happened;

/*
 * Set to a non-zero value whenever
 * the server receives a SIGPIPE signal.
 */
static int svz_pipe_broke;

/**
 * Set to a non-zero value whenever the server
 * receives a SIGCHLD signal.
 */
svz_t_handle svz_child_died;

/*
 * Set to a value greater or
 * equal zero when the server receives a signal which is not handled.
 */
static int svz_uncaught_signal = -1;

/*
 * Set to a value greater or equal
 * zero for every received signal.
 */
static int svz_signal = -1;

/*
 * This holds the time on which the next call to @code{svz_periodic_tasks}
 * should occur.
 */
time_t svz_notify;

/*
 * Pointer to the head of the list of sockets,
 * which are handled by the server loop.
 */
svz_socket_t *svz_sock_root = NULL;

/*
 * Points to the last structure in the socket queue,
 * or @var{NULL} when the queue is empty.
 */
static svz_socket_t *svz_sock_last = NULL;

/*
 * Array used to speed up references to
 * socket structures by socket's id.
 */
static svz_socket_t **svz_sock_lookup_table = NULL;
static int svz_sock_id = 0;
static int svz_sock_version = 0;
static int svz_sock_limit = 1024;       /* Must be binary size!  */

/**
 * Return non-zero if the core is in the process of shutting down
 * (typically as a result of a signal).
 */
int
svz_shutting_down_p (void)
{
  return svz_nuke_happened;
}

#ifdef SIGSEGV
#define SIGSEGV_TEXT                                                          \
  "\nFatal error (access violation)."                                         \
  "\nPlease report this bug to <bug-serveez@gnu.org>."                        \
  "\nIf possible, please try to obtain a C stack backtrace via\n"             \
  "\n  $ gdb %s core"                                                         \
  "\n  $ (gdb) where\n"                                                       \
  "\nand include this info into your bug report. If you do not have gdb"      \
  "\ninstalled you can also try dbx. Also tell us your architecture and"      \
  "\noperating system you are currently working on.\n\n"
#endif

#if defined (SIGSEGV)
/*
 * Segmentation fault exception handler.
 */
static void
svz_segfault_exception (int sig)
{
#if HAVE_GETRLIMIT
  struct rlimit rlim;

  rlim.rlim_max = RLIM_INFINITY;
  rlim.rlim_cur = RLIM_INFINITY;
  setrlimit (RLIMIT_CORE, &rlim);
#endif /* HAVE_GETRLIMIT */

  signal (sig, SIG_DFL);
  fprintf (stderr, SIGSEGV_TEXT, THE (client));
  raise (sig);
}
#endif /* SIGSEGV */

/*
 * Handle some signals to handle server resets (SIGHUP), to ignore
 * broken pipes (SIGPIPE) and to exit gracefully if requested by the
 * user (SIGINT, SIGTERM).
 */
static void
svz_signal_handler (int sig)
{
  switch (sig)
    {
#ifdef SIGHUP
    case SIGHUP:
      svz_reset_happened = 1;
      signal (SIGHUP, svz_signal_handler);
      break;
#endif
#ifdef SIGPIPE
    case SIGPIPE:
      svz_pipe_broke = 1;
      signal (SIGPIPE, svz_signal_handler);
      break;
#endif
#ifdef SIGCHLD
    case SIGCHLD:
#if HAVE_WAITPID
      {
        int status, pid;
        /* check if the child has been just stopped */
        if ((pid = waitpid (-1, &status, WNOHANG | WUNTRACED)) != -1)
          {
            if (!WIFSTOPPED (status))
              svz_child_died = pid;
          }
      }
#else /* HAVE_WAITPID */
      if ((svz_child_died = wait (NULL)) == -1)
        svz_child_died = 0;
#endif /* not HAVE_WAITPID */
      signal (SIGCHLD, svz_signal_handler);
      break;
#endif
#ifdef SIGBREAK
    case SIGBREAK:
      /*
       * reset signal handlers to the default, so the server
       * can get killed on second try
       */
      svz_nuke_happened = 1;
      signal (SIGBREAK, SIG_DFL);
      break;
#endif
#ifdef SIGTERM
    case SIGTERM:
      svz_nuke_happened = 1;
      signal (SIGTERM, SIG_DFL);
      break;
#endif
#ifdef SIGINT
    case SIGINT:
      svz_nuke_happened = 1;
      signal (SIGINT, SIG_DFL);
      break;
#endif
#ifdef SIGQUIT
    case SIGQUIT:
      svz_nuke_happened = 1;
      signal (SIGQUIT, SIG_DFL);
      break;
#endif
    default:
      svz_uncaught_signal = sig;
      break;
    }

  /* save current signal */
  svz_signal = sig;
}

/* 65 is hopefully a safe bet, kill(1) accepts 0..64, *sigh* */
#define SVZ_NUMBER_OF_SIGNALS 65

/* Cached results of strsignal calls.  */
static svz_array_t *svz_signal_strings = NULL;

/* On some platforms ‘strsignal’ can be resolved but is nowhere declared.  */
#if defined (HAVE_STRSIGNAL) && !HAVE_DECL_STRSIGNAL
extern char * strsignal (int);
#endif

/*
 * Prepare library so that @code{svz_strsignal} works.
 */
static void
svz_strsignal_init (void)
{
  int i;
  char *str;
  const char *format = "Signal %d";

  if (svz_signal_strings != NULL)
    return;

  svz_signal_strings = svz_array_create (SVZ_NUMBER_OF_SIGNALS, svz_free);
  for (i = 0; i < SVZ_NUMBER_OF_SIGNALS; i++)
    {
#if HAVE_STRSIGNAL
      if (NULL == (str = (char *) strsignal (i)))
        {
          str = svz_malloc (128);
          snprintf (str, 128, format, i);
          svz_array_add (svz_signal_strings, svz_strdup (str));
          svz_free (str);
        }
      else
        {
          svz_array_add (svz_signal_strings, svz_strdup (str));
        }
#else /* not HAVE_STRSIGNAL */
      str = svz_malloc (128);
      snprintf (str, 128, format, i);
      svz_array_add (svz_signal_strings, svz_strdup (str));
      svz_free (str);
#endif /* HAVE_STRSIGNAL */
    }
}

/*
 * The function @code{svz_strsignal} does not work afterwards anymore.
 */
static void
svz_strsignal_destroy (void)
{
  svz_array_destroy (svz_signal_strings);
  svz_signal_strings = NULL;
}

/*
 * Resolve the given signal number to form a describing string.
 * This function is reentrant, use it from the signal handler.
 * It does not return NULL.  The returned pointer is shared amongst
 * all users.  For unknown signals (which is not supposed to happen)
 * the returned string points to a statically allocated buffer (which
 * destroys the reentrance, of course) [who cares :-].
 */
static char *
svz_strsignal (int sig)
{
  static char fallback[128];

  if (sig >= 0 && sig < SVZ_NUMBER_OF_SIGNALS)
    return (char *) svz_array_get (svz_signal_strings, sig);
  else
    {
      snprintf (fallback, 128, "No such signal %d", sig);
      return fallback;
    }
}

/*
 * Abort the process, printing the error message @var{msg} first.
 */
static int
svz_abort (char *msg)
{
  svz_log (SVZ_LOG_FATAL, "list validation failed: %s\n", msg);
  abort ();
  return 0;
}

/**
 * Call @var{func} for each socket, passing additionally the second arg
 * @var{closure}.  If @var{func} returns a negative value, return immediately
 * with that value (breaking out of the loop), otherwise, return 0.
 */
int
svz_foreach_socket (svz_socket_do_t *func, void *closure)
{
  svz_socket_t *sock = svz_sock_root;

  while (sock)
    {
      int rv = func (sock, closure);

      if (0 > rv)
        return rv;
      sock = sock->next;
    }
  return 0;
}

#if ENABLE_DEBUG
/*
 * Check if a given socket is still valid.  Return non-zero if it is
 * not.
 */
static int
svz_sock_valid (svz_socket_t *sock)
{
  if (!(sock->flags & (SVZ_SOFLG_LISTENING |
                       SVZ_SOFLG_CONNECTED | SVZ_SOFLG_CONNECTING)))
    return -1;

  if (sock->sock_desc == INVALID_SOCKET)
    return -1;

  return 0;
}

/*
 * Go through the socket list and check if it is still consistent.
 * Abort the program with an error message, if it is not.
 */
static int
svz_sock_validate_list (void)
{
  svz_socket_t *sock, *prev;

#if ENABLE_SOCK_PRINT_LIST
  sock = svz_sock_root;
  while (sock)
    {
      fprintf (stdout, "id: %04d, sock: %p == %p, prev: %p, next: %p\n",
               sock->id, (void *) sock,
               (void *) svz_sock_lookup_table[sock->id],
               (void *) sock->prev, (void *) sock->next);
      sock = sock->next;
    }
  fprintf (stdout, "\n");
#endif

  prev = NULL;
  sock = svz_sock_root;
  while (sock)
    {
      /* check if the descriptors are valid */
      if (sock->flags & SVZ_SOFLG_SOCK)
        {
          if (svz_sock_valid (sock) == -1)
            {
              svz_abort ("invalid socket descriptor");
            }
        }
      if (sock->flags & SVZ_SOFLG_PIPE)
        {
          if (svz_pipe_valid (sock) == -1)
            {
              svz_abort ("invalid pipe descriptor");
            }
        }

      /* check socket list structure */
      if (svz_sock_lookup_table[sock->id] != sock)
        {
          svz_abort ("lookup table corrupted");
        }
      if (prev != sock->prev)
        {
          svz_abort ("list structure invalid (sock->prev)");
        }
      prev = sock;
      sock = sock->next;
    }

  if (prev != svz_sock_last)
    {
      svz_abort ("list structure invalid (last socket)");
    }
  return 0;
}
#endif /* ENABLE_DEBUG */

/*
 * Rechain the socket list to prevent sockets from starving at the end
 * of this list.  We will call it every time when a @code{select} or
 * @code{poll} has returned.  Listeners are kept at the beginning of the
 * chain anyway.
 */
static void
svz_sock_rechain_list (void)
{
  svz_socket_t *sock;
  svz_socket_t *last_listen;
  svz_socket_t *end_socket;

  sock = svz_sock_last;
  if (sock && sock->prev)
    {
      end_socket = sock->prev;
      for (last_listen = svz_sock_root; last_listen && last_listen != sock &&
             last_listen->flags & (SVZ_SOFLG_LISTENING | SVZ_SOFLG_PRIORITY) &&
             !(sock->flags & SVZ_SOFLG_LISTENING);
           last_listen = last_listen->next);

      /* just listeners in the list, return */
      if (!last_listen)
        return;

      /* sock is the only non-listening (connected) socket */
      if (sock == last_listen)
        return;

      /* one step back unless we are at the socket root */
      if (last_listen->prev)
        {
          last_listen = last_listen->prev;

          /* put sock in front of chain behind listeners */
          sock->next = last_listen->next;
          sock->next->prev = sock;

          /* put sock behind last listener */
          last_listen->next = sock;
          sock->prev = last_listen;
        }
      else
        {
          /* enqueue at root */
          sock->next = svz_sock_root;
          sock->prev = NULL;
          sock->next->prev = sock;
          svz_sock_root = sock;
        }

      /* mark the new end of chain */
      end_socket->next = NULL;
      svz_sock_last = end_socket;
    }
}

/**
 * Enqueue the socket @var{sock} into the list of sockets handled by
 * the server loop.
 */
int
svz_sock_enqueue (svz_socket_t *sock)
{
  /* check for validity of pipe descriptors */
  if (sock->flags & SVZ_SOFLG_PIPE)
    {
      if (svz_pipe_valid (sock) == -1)
        {
          svz_log (SVZ_LOG_FATAL, "cannot enqueue invalid pipe\n");
          return -1;
        }
    }

  /* check for validity of socket descriptors */
  if (sock->flags & SVZ_SOFLG_SOCK)
    {
      if (svz_sock_valid (sock) == -1)
        {
          svz_log (SVZ_LOG_FATAL, "cannot enqueue invalid socket\n");
          return -1;
        }
    }

  /* check lookup table */
  if (svz_sock_lookup_table[sock->id] || sock->flags & SVZ_SOFLG_ENQUEUED)
    {
      svz_log (SVZ_LOG_FATAL, "socket id %d has been already enqueued\n",
               sock->id);
      return -1;
    }

  /* really enqueue socket */
  sock->next = NULL;
  sock->prev = NULL;
  if (!svz_sock_root)
    {
      svz_sock_root = sock;
    }
  else
    {
      svz_sock_last->next = sock;
      sock->prev = svz_sock_last;
    }

  svz_sock_last = sock;
  sock->flags |= SVZ_SOFLG_ENQUEUED;
  svz_sock_lookup_table[sock->id] = sock;

  return 0;
}

/*
 * Remove the socket @var{sock} from the list of sockets handled by
 * the server loop.
 */
static int
svz_sock_dequeue (svz_socket_t *sock)
{
  /* check for validity of pipe descriptors */
  if (sock->flags & SVZ_SOFLG_PIPE)
    {
      if (svz_pipe_valid (sock) == -1)
        {
          svz_log (SVZ_LOG_FATAL, "cannot dequeue invalid pipe\n");
          return -1;
        }
    }

  /* check for validity of socket descriptors */
  if (sock->flags & SVZ_SOFLG_SOCK)
    {
      if (svz_sock_valid (sock) == -1)
        {
          svz_log (SVZ_LOG_FATAL, "cannot dequeue invalid socket\n");
          return -1;
        }
    }

  /* check lookup table */
  if (!svz_sock_lookup_table[sock->id] || !(sock->flags & SVZ_SOFLG_ENQUEUED))
    {
      svz_log (SVZ_LOG_FATAL, "socket id %d has been already dequeued\n",
               sock->id);
      return -1;
    }

  /* really dequeue socket */
  if (sock->next)
    sock->next->prev = sock->prev;
  else
    svz_sock_last = sock->prev;

  if (sock->prev)
    sock->prev->next = sock->next;
  else
    svz_sock_root = sock->next;

  sock->flags &= ~SVZ_SOFLG_ENQUEUED;
  svz_sock_lookup_table[sock->id] = NULL;

  return 0;
}

/*
 * This function returns zero if the @var{child} socket is allowed to
 * connect to the port configuration of the @var{parent} socket structure
 * which needs to be a listener therefore.
 */
int
svz_sock_check_access (svz_socket_t *parent, svz_socket_t *child)
{
  svz_portcfg_t *port;
  char *ip;
  size_t n;
  int ret;
  char *remote;

  /* Check arguments and return if this function cannot work.  */
  if (parent == NULL || child == NULL || parent->port == NULL)
    return 0;

  /* Get port configuration and remote address.  */
  port = parent->port;
  remote = svz_inet_ntoa (child->remote_addr);

  /* Check the deny IP addresses.  */
  if (port->deny)
    {
      svz_array_foreach (port->deny, ip, n)
        {
          if (!strcmp (ip, remote))
            {
              svz_log (SVZ_LOG_NOTICE, "denying access from %s\n", ip);
              return -1;
            }
        }
    }

  /* Check allowed IP addresses.  */
  if (port->allow)
    {
      ret = -1;
      svz_array_foreach (port->allow, ip, n)
        {
          if (!strcmp (ip, remote))
            {
              svz_log (SVZ_LOG_NOTICE, "allowing access from %s\n", ip);
              ret = 0;
            }
        }
      if (ret)
        {
          svz_log (SVZ_LOG_NOTICE, "denying unallowed access from %s\n", remote);
          return ret;
        }
    }

  return 0;
}

/**
 * Return the parent's port configuration of @var{sock},
 * or @code{NULL} if the given socket has no parent, i.e. is a listener.
 */
svz_portcfg_t *
svz_sock_portcfg (svz_socket_t *sock)
{
  svz_portcfg_t *port = NULL;
  svz_socket_t *parent;

  if ((parent = svz_sock_getparent (sock)) != NULL)
    port = parent->port;
  return port;
}

/**
 * Set the @var{child} socket's parent to @var{parent}.
 *
 * This should be called whenever a listener accepts a
 * connection and creates a new child socket.
 */
void
svz_sock_setparent (svz_socket_t *child, svz_socket_t *parent)
{
  if (parent != NULL && child != NULL)
    {
      child->parent_id = parent->id;
      child->parent_version = parent->version;
    }
}

/**
 * Return the @var{child} socket's parent socket structure, or @code{NULL}
 * if this socket does not exist anymore.  This might happen if a listener
 * dies for some reason.
 */
svz_socket_t *
svz_sock_getparent (svz_socket_t *child)
{
  if (!child)
    return NULL;
  return svz_sock_find (child->parent_id, child->parent_version);
}

/**
 * Set the referring socket structure of @var{sock} to @var{referrer}.
 * If @var{referrer} is @code{NULL} the reference will be invalidated.
 *
 * This can be used to create some relationship
 * between two socket structures.
 */
void
svz_sock_setreferrer (svz_socket_t *sock, svz_socket_t *referrer)
{
  if (referrer == NULL)
    {
      sock->referrer_version = sock->referrer_id = -1;
    }
  else
    {
      sock->referrer_id = referrer->id;
      sock->referrer_version = referrer->version;
    }
}

/**
 * Get the referrer of the socket structure @var{sock}.
 * Return @code{NULL} if there is no such socket.
 */
svz_socket_t *
svz_sock_getreferrer (svz_socket_t *sock)
{
  if (!sock)
    return NULL;
  return svz_sock_find (sock->referrer_id, sock->referrer_version);
}

/**
 * Return the socket structure for the socket id @var{id} and the version
 * @var{version}, or @code{NULL} if no such socket exists.  If @var{version}
 * is -1 it is not checked.
 */
svz_socket_t *
svz_sock_find (int id, int version)
{
  svz_socket_t *sock;

  if (id & ~(svz_sock_limit - 1))
    {
      svz_log (SVZ_LOG_WARNING, "socket id %d is invalid\n", id);
      return NULL;
    }

  sock = svz_sock_lookup_table[id];
  if (version != -1 && sock && sock->version != version)
    {
      svz_log (SVZ_LOG_WARNING, "socket version %d (id %d) is invalid\n",
               version, id);
      return NULL;
    }

  return svz_sock_lookup_table[id];
}

/*
 * Create the socket lookup table initially.
 */
static void
svz_sock_table_create (void)
{
  svz_sock_lookup_table = svz_calloc (svz_sock_limit *
                                      sizeof (svz_socket_t *));
}

/*
 * Destroy the socket lookup table finally.
 */
static void
svz_sock_table_destroy (void)
{
  svz_free_and_zero (svz_sock_lookup_table);
}

/*
 * Calculate unique socket structure id and assign a version for a
 * given @var{sock}.  The version is for validating socket structures.  It is
 * currently used in the coserver callbacks.
 */
int
svz_sock_unique_id (svz_socket_t *sock)
{
  int i;

  for (i = 0; i < svz_sock_limit; i++)
    {
      svz_sock_id++;
      svz_sock_id &= (svz_sock_limit - 1);

      if (NULL == svz_sock_lookup_table[svz_sock_id])
        break;
    }

  /* ensure global limit, resize the lookup table if necessary */
  if (i == svz_sock_limit)
    {
      svz_sock_lookup_table = svz_realloc (svz_sock_lookup_table,
                                           svz_sock_limit * 2 *
                                           sizeof (svz_socket_t *));
      memset (&svz_sock_lookup_table[svz_sock_limit], 0,
              svz_sock_limit * sizeof (svz_socket_t *));
      svz_sock_id = svz_sock_limit;
      svz_sock_limit *= 2;
      svz_log (SVZ_LOG_NOTICE, "lookup table enlarged to %d\n", svz_sock_limit);
    }

  sock->id = svz_sock_id;
  sock->version = svz_sock_version++;

  return svz_sock_id;
}

static void
reset_internal (svz_server_t *server, SVZ_UNUSED void *closure)
{
  if (server->reset)
    server->reset (server);
}

/*
 * This gets called when the server receives a SIGHUP, which means
 * that the server should be reset.
 */
static int
svz_reset (void)
{
  svz_foreach_server (reset_internal, NULL);
  svz_interface_check ();
  return 0;
}

/*
 * Do everything to shut down the socket @var{sock}.  The socket structure
 * gets removed from the socket queue, the file descriptor is closed
 * and all memory used by the socket gets freed.  Note that this
 * function calls the @var{sock}'s disconnect handler if defined.
 */
int
svz_sock_shutdown (svz_socket_t *sock)
{
#if ENABLE_DEBUG
  svz_log (SVZ_LOG_DEBUG, "shutting down socket id %d\n", sock->id);
#endif

  if (sock->disconnected_socket)
    sock->disconnected_socket (sock);

  svz_sock_dequeue (sock);

  if (sock->flags & SVZ_SOFLG_SOCK)
    svz_sock_disconnect (sock);
  if (sock->flags & SVZ_SOFLG_PIPE)
    svz_pipe_disconnect (sock);

  svz_sock_free (sock);

  return 0;
}

/**
 * Mark socket @var{sock} as killed.  That means that no further operations
 * except disconnecting and freeing are allowed.  All marked sockets will be
 * deleted once the server loop is through.
 */
int
svz_sock_schedule_for_shutdown (svz_socket_t *sock)
{
  if (!(sock->flags & SVZ_SOFLG_KILLED))
    {
#if ENABLE_DEBUG
      svz_log (SVZ_LOG_DEBUG, "scheduling socket id %d for shutdown\n", sock->id);
#endif /* ENABLE_DEBUG */

      sock->flags |= SVZ_SOFLG_KILLED;

      /* Shutdown each child for listeners.  */
      if (sock->flags & SVZ_SOFLG_LISTENING)
        {
          svz_socket_t *child;
          svz_sock_foreach (child)
            if (svz_sock_getparent (child) == sock)
              svz_sock_schedule_for_shutdown (child);
        }
    }
  return 0;
}

static void
notify_internal (svz_server_t *server, SVZ_UNUSED void *closure)
{
  if (server->notify)
    server->notify (server);
}

/*
 * This routine gets called once a second and is supposed to perform any
 * task that has to get scheduled periodically.  It checks all sockets'
 * timers and calls their timer functions when necessary.
 */
int
svz_periodic_tasks (void)
{
  svz_socket_t *sock;

  svz_notify += 1;

  sock = svz_sock_root;
  while (sock)
    {
#if ENABLE_FLOOD_PROTECTION
      if (sock->flood_points > 0)
        {
          sock->flood_points--;
        }
#endif /* ENABLE_FLOOD_PROTECTION */

      if (sock->idle_func && sock->idle_counter > 0)
        {
          if (--sock->idle_counter <= 0)
            {
              if (sock->idle_func (sock))
                {
                  svz_log (SVZ_LOG_ERROR,
                           "idle function for socket id %d "
                           "returned error\n", sock->id);
                  svz_sock_schedule_for_shutdown (sock);
                }
            }
        }
      sock = sock->next;
    }

  /* check regularly for internal coserver responses and keep coservers
     alive */
  svz_coserver_check ();

  /* run the server instance timer routines */
  svz_foreach_server (notify_internal, NULL);

  return 0;
}

/*
 * Goes through all socket and shuts invalid ones down.
 */
void
svz_sock_check_bogus (void)
{
#ifdef __MINGW32__
  unsigned long readBytes;
#endif
  svz_socket_t *sock;

#if ENABLE_DEBUG
  svz_log (SVZ_LOG_DEBUG, "checking for bogus sockets\n");
#endif /* ENABLE_DEBUG */

  svz_sock_foreach (sock)
    {
      if (sock->flags & SVZ_SOFLG_SOCK)
        {
#ifdef __MINGW32__
          if (ioctlsocket (sock->sock_desc, FIONREAD, &readBytes) ==
              SOCKET_ERROR)
#else /* not __MINGW32__ */
          if (fcntl (sock->sock_desc, F_GETFL) < 0)
#endif /* not __MINGW32__ */
            {
              svz_log (SVZ_LOG_ERROR, "socket %d has gone\n", sock->sock_desc);
              svz_sock_schedule_for_shutdown (sock);
            }
        }

#ifndef __MINGW32__
      if (sock->flags & SVZ_SOFLG_RECV_PIPE)
        {
          if (fcntl (sock->pipe_desc[SVZ_READ], F_GETFL) < 0)
            {
              svz_log (SVZ_LOG_ERROR, "pipe %d has gone\n",
                       sock->pipe_desc[SVZ_READ]);
              svz_sock_schedule_for_shutdown (sock);
            }
        }
      if (sock->flags & SVZ_SOFLG_SEND_PIPE)
        {
          if (fcntl (sock->pipe_desc[SVZ_WRITE], F_GETFL) < 0)
            {
              svz_log (SVZ_LOG_ERROR, "pipe %d has gone\n",
                       sock->pipe_desc[SVZ_WRITE]);
              svz_sock_schedule_for_shutdown (sock);
            }
        }
#endif /* not __MINGW32__ */
    }
}

/*
 * Setup signaling for the core library.
 */
static void
svz_signal_up (void)
{
#ifdef SIGTERM
  signal (SIGTERM, svz_signal_handler);
#endif
#ifdef SIGQUIT
  signal (SIGQUIT, svz_signal_handler);
#endif
#ifdef SIGINT
  signal (SIGINT, svz_signal_handler);
#endif
#ifdef SIGBREAK
  signal (SIGBREAK, svz_signal_handler);
#endif
#ifdef SIGCHLD
  signal (SIGCHLD, svz_signal_handler);
#endif
#ifdef SIGHUP
  signal (SIGHUP, svz_signal_handler);
#endif
#ifdef SIGPIPE
  signal (SIGPIPE, svz_signal_handler);
#endif
#ifdef SIGURG
  signal (SIGURG, svz_signal_handler);
#endif
#ifdef SIGSEGV
  signal (SIGSEGV, svz_segfault_exception);
#endif
}

/*
 * Deinstall signaling for the core library.
 */
static void
svz_signal_dn (void)
{
#ifdef SIGTERM
  signal (SIGTERM, SIG_DFL);
#endif
#ifdef SIGQUIT
  signal (SIGQUIT, SIG_DFL);
#endif
#ifdef SIGINT
  signal (SIGINT, SIG_DFL);
#endif
#ifdef SIGBREAK
  signal (SIGBREAK, SIG_DFL);
#endif
#ifdef SIGCHLD
  signal (SIGCHLD, SIG_DFL);
#endif
#ifdef SIGHUP
  signal (SIGHUP, SIG_DFL);
#endif
#ifdef SIGPIPE
  signal (SIGPIPE, SIG_DFL);
#endif
#ifdef SIGURG
  signal (SIGURG, SIG_DFL);
#endif
#ifdef SIGSEGV
  signal (SIGSEGV, SIG_DFL);
#endif
}

/*
 * This routine checks whether the child process specified by the @code{pid}
 * handle stored in the socket structure @var{sock} is still alive.  It
 * returns zero if so, otherwise (when the child process died) non-zero.
 */
static int
svz_sock_child_died (svz_socket_t *sock)
{
#ifdef __MINGW32__

  DWORD result;

  result = WOE_WAIT_1 (sock->pid);
  if (result == WAIT_FAILED)
    {
      WOE_WAIT_LOG_ERROR_ANONYMOUSLY ();
      return 0;
    }
  else if (result != WAIT_TIMEOUT)
    {
      if (svz_closehandle (sock->pid) == -1)
        svz_log_sys_error ("CloseHandle");
      return -1;
    }

#else /* !__MINGW32__ */

  if (svz_child_died == sock->pid)
    return -1;

#if HAVE_WAITPID
  if (waitpid (sock->pid, NULL, WNOHANG) == -1 && errno == ECHILD)
    return -1;
#endif /* HAVE_WAITPID */

#endif /* !__MINGW32__ */
  return 0;
}

/*
 * Goes through the list of socket structures and checks whether each
 * @code{pid} stored in a socket structure has died.  If so, the
 * @code{child_died} callback is called.  If this callback returned non-zero
 * the appropriate socket structure gets scheduled for shutdown.
 */
static void
svz_sock_check_children (void)
{
  svz_socket_t *sock;

  svz_sock_foreach (sock)
    if (! svz_invalid_handle_p (sock->pid) && svz_sock_child_died (sock))
      {
        svz_invalidate_handle (&sock->pid);
#if ENABLE_DEBUG
        svz_log (SVZ_LOG_DEBUG, "child of socket id %d died\n", sock->id);
#endif /* ENABLE_DEBUG */
        if (sock->child_died)
          if (sock->child_died (sock))
            svz_sock_schedule_for_shutdown (sock);
      }
}

/* This is defined in server-loop.c, and used only in this file.  */
SBO int svz_check_sockets (void);

/**
 * Handle all things once.
 *
 * This function is called regularly by @code{svz_loop}.
 */
void
svz_loop_one (void)
{
  svz_socket_t *sock, *next;
  static int rechain = 0;

  /*
   * FIXME: Remove this once the server is stable.
   */
#if ENABLE_DEBUG
  svz_sock_validate_list ();
#endif /* ENABLE_DEBUG */

  if (svz_reset_happened)
    {
      /* SIGHUP received.  */
      svz_log (SVZ_LOG_NOTICE, "resetting server\n");
      svz_reset ();
      svz_reset_happened = 0;
    }

  if (svz_pipe_broke)
    {
      /* SIGPIPE received.  */
      svz_log (SVZ_LOG_ERROR, "broken pipe, continuing\n");
      svz_pipe_broke = 0;
    }

  /*
   * Check for new connections on server port, incoming data from
   * clients and process queued output data.
   */
  svz_check_sockets ();

  /* Check if a child died.  Checks all socket structures.  */
  svz_sock_check_children ();

  if (svz_child_died)
    {
      /* SIGCHLD received.  */
      svz_log (SVZ_LOG_NOTICE, "child pid %d died\n", (int) svz_child_died);
      svz_child_died = 0;
    }

  if (svz_signal != -1)
    {
      /* Log the current signal.  */
      svz_log (SVZ_LOG_WARNING, "signal: %s\n", svz_strsignal (svz_signal));
      svz_signal = -1;
    }

  if (svz_uncaught_signal != -1)
    {
      /* Uncaught signal received.  */
      svz_log (SVZ_LOG_DEBUG, "uncaught signal %d\n", svz_uncaught_signal);
      svz_uncaught_signal = -1;
    }

  /*
   * Reorder the socket chain every 16 select loops.  We do not do it
   * every time for performance reasons.
   */
  if (rechain++ & 16)
    svz_sock_rechain_list ();

  /*
   * Shut down all sockets that have been scheduled for closing.
   */
  sock = svz_sock_root;
  while (sock)
    {
      next = sock->next;
      if (sock->flags & SVZ_SOFLG_KILLED)
        svz_sock_shutdown (sock);
      sock = next;
    }
}

/**
 * Initialize top-of-cycle state.
 *
 * Call this function once before using @code{svz_loop_one}.
 */
void
svz_loop_pre (void)
{
  /*
   * Setting up control variables.  These get set either in the signal
   * handler or from a command processing routine.
   */
  svz_reset_happened = 0;
  svz_child_died = 0;
  svz_pipe_broke = 0;
  svz_notify = time (NULL);

  /* Run the server loop.  */
  svz_log (SVZ_LOG_NOTICE, "entering server loop\n");
}

/**
 * Clean up bottom-of-cycle state.
 *
 * Call this function once after using @code{svz_loop_one}.
 */
void
svz_loop_post (void)
{
  svz_log (SVZ_LOG_NOTICE, "leaving server loop\n");

  /* Shutdown all socket structures.  */
  while (svz_sock_root)
    svz_sock_shutdown (svz_sock_root);
  svz_sock_last = NULL;
}

/**
 * Loop, serving.  In other words, handle all signals, incoming and outgoing
 * connections and listening server sockets.
 */
void
svz_loop (void)
{
  svz_loop_pre ();
  while (!svz_nuke_happened)
    {
      svz_loop_one ();
    }
  svz_loop_post ();
}


void
svz__strsignal_updn (int direction)
{
  (direction
   ? svz_strsignal_init
   : svz_strsignal_destroy)
    ();
}

void
svz__sock_table_updn (int direction)
{
  (direction
   ? svz_sock_table_create
   : svz_sock_table_destroy)
    ();
}

void
svz__signal_updn (int direction)
{
  (direction
   ? svz_signal_up
   : svz_signal_dn)
    ();
}
