/*
 * "$Id$"
 *
 * Client routines for the CUPS scheduler.
 *
 * Copyright 2007-2013 by Apple Inc.
 * Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 * This file contains Kerberos support code, copyright 2006 by
 * Jelmer Vernooij.
 *
 * These coded instructions, statements, and computer programs are the
 * property of Apple Inc. and are protected by Federal copyright
 * law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 * which should have been included with this file.  If this file is
 * file is missing or damaged, see the license at "http://www.cups.org/".
 */

/*
 * Include necessary headers...
 */

#define _CUPS_NO_DEPRECATED
//#define _HTTP_NO_PRIVATE
#include "cupsd.h"

#ifdef __APPLE__
#  include <libproc.h>
#endif /* __APPLE__ */
#ifdef HAVE_TCPD_H
#  include <tcpd.h>
#endif /* HAVE_TCPD_H */


/*
 * Local globals...
 */

static const char 	* const ipp_states[] =
			{		/* IPP state strings */
			  "IPP_IDLE",
			  "IPP_HEADER",
			  "IPP_ATTRIBUTE",
			  "IPP_STATE_DATA"
			};


/*
 * Local functions...
 */

static int		check_if_modified(cupsd_client_t *con,
			                  struct stat *filestats);
static int		compare_clients(cupsd_client_t *a, cupsd_client_t *b,
			                void *data);
static char		*get_file(cupsd_client_t *con, struct stat *filestats,
			          char *filename, int len);
static http_status_t	install_cupsd_conf(cupsd_client_t *con);
static int		is_cgi(cupsd_client_t *con, const char *filename,
		               struct stat *filestats, mime_type_t *type);
static int		is_path_absolute(const char *path);
static int		pipe_command(cupsd_client_t *con, int infile, int *outfile,
			             char *command, char *options, int root);
static int		valid_host(cupsd_client_t *con);
static int		write_file(cupsd_client_t *con, http_status_t code,
		        	   char *filename, char *type,
				   struct stat *filestats);
static void		write_pipe(cupsd_client_t *con);


/*
 * 'cupsdAcceptClient()' - Accept a new client.
 */

void
cupsdAcceptClient(cupsd_listener_t *lis)/* I - Listener socket */
{
  char			name[256];	/* Hostname of client */
  int			count;		/* Count of connections on a host */
//  int			val;		/* Parameter value */
  cupsd_client_t	*con,		/* New client pointer */
			*tempcon;	/* Temporary client pointer */
//  http_addrlist_t	*addrlist,	/* List of adddresses for host */
//			*addr;		/* Current address */
  socklen_t		addrlen;	/* Length of address */
//  char			*hostname;	/* Hostname for address */
  http_addr_t		temp;		/* Temporary address variable */
  static time_t		last_dos = 0;	/* Time of last DoS attack */
#ifdef HAVE_TCPD_H
  struct request_info	wrap_req;	/* TCP wrappers request information */
#endif /* HAVE_TCPD_H */


  cupsdLogMessage(CUPSD_LOG_DEBUG2,
                  "cupsdAcceptClient(lis=%p(%d)) Clients=%d",
                  lis, lis->fd, cupsArrayCount(Clients));

 /*
  * Make sure we don't have a full set of clients already...
  */

  if (cupsArrayCount(Clients) == MaxClients)
    return;

 /*
  * Get a pointer to the next available client...
  */

  if (!Clients)
    Clients = cupsArrayNew(NULL, NULL);

  if (!Clients)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to allocate memory for clients array!");
    cupsdPauseListening();
    return;
  }

  if (!ActiveClients)
    ActiveClients = cupsArrayNew((cups_array_func_t)compare_clients, NULL);

  if (!ActiveClients)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "Unable to allocate memory for active clients array!");
    cupsdPauseListening();
    return;
  }

  if ((con = calloc(1, sizeof(cupsd_client_t))) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to allocate memory for client!");
    cupsdPauseListening();
    return;
  }

 /*
  * Accept the client and get the remote address...
  */

  con->number = ++ LastClientNumber;
  con->file   = -1;
//  con->http->activity   = time(NULL);
//  httpGetAddress(con->http)   = &(con->clientaddr);
//  con->http->wait_value = 10000;
//  con->http->mode       = _HTTP_MODE_SERVER;

  if ((con->http = httpAcceptConnection(lis->fd, 0)) == NULL)
  {
    if (errno == ENFILE || errno == EMFILE)
      cupsdPauseListening();

    cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to accept client connection - %s.",
                    strerror(errno));
    free(con);

    return;
  }

 /*
  * Save the connected address and port number...
  */

  con->clientaddr = lis->address;

#if 0 /* ifdef AF_INET6 */
 /* FIXME: I don't believe this is recommended any longer, and we specifically
  *        disable IPv4-over-IPv6 when we listen...
  */
 /*
  * Convert IPv4 over IPv6 addresses (::ffff:n.n.n.n) to IPv4 forms we
  * can more easily use...
  */

  if (lis->address.addr.sa_family == AF_INET6 &&
      httpGetAddress(con->http)->ipv6.sin6_addr.s6_addr32[0] == 0 &&
      httpGetAddress(con->http)->ipv6.sin6_addr.s6_addr32[1] == 0 &&
      ntohl(httpGetAddress(con->http)->ipv6.sin6_addr.s6_addr32[2]) == 0xffff)
    httpGetAddress(con->http)->ipv6.sin6_addr.s6_addr32[2] = 0;
#endif /* AF_INET6 */

 /*
  * Check the number of clients on the same address...
  */

  for (count = 0, tempcon = (cupsd_client_t *)cupsArrayFirst(Clients);
       tempcon;
       tempcon = (cupsd_client_t *)cupsArrayNext(Clients))
    if (httpAddrEqual(httpGetAddress(tempcon->http), httpGetAddress(con->http)))
    {
      count ++;
      if (count >= MaxClientsPerHost)
	break;
    }

  if (count >= MaxClientsPerHost)
  {
    if ((time(NULL) - last_dos) >= 60)
    {
      last_dos = time(NULL);
      cupsdLogMessage(CUPSD_LOG_WARN,
                      "Possible DoS attack - more than %d clients connecting "
		      "from %s.",
	              MaxClientsPerHost,
		      httpGetHostname(con->http, name, sizeof(name)));
    }

    httpClose(con->http);
    free(con);
    return;
  }

#if 0 /* Decide if we keep this, and where to store the name */
 /*
  * Get the hostname or format the IP address as needed...
  */

  if (httpAddrLocalhost(httpGetAddress(con->http)))
  {
   /*
    * Map accesses from the loopback interface to "localhost"...
    */

    strlcpy(httpGetHostname(con->http, NULL, 0), "localhost", sizeof(httpGetHostname(con->http, NULL, 0)));
    hostname = httpGetHostname(con->http, NULL, 0);
  }
  else
  {
   /*
    * Map accesses from the same host to the server name.
    */

    if (HostNameLookups)
      hostname = httpAddrLookup(httpGetAddress(con->http), httpGetHostname(con->http, NULL, 0),
                                sizeof(httpGetHostname(con->http, NULL, 0)));
    else
    {
      hostname = NULL;
      httpAddrString(httpGetAddress(con->http), httpGetHostname(con->http, NULL, 0),
                     sizeof(httpGetHostname(con->http, NULL, 0)));
    }
  }

  if (hostname == NULL && HostNameLookups == 2)
  {
   /*
    * Can't have an unresolved IP address with double-lookups enabled...
    */

    httpClose(con->http);

    cupsdLogClient(con, CUPSD_LOG_WARN,
                    "Name lookup failed - connection from %s closed!",
                    httpGetHostname(con->http, NULL, 0));

    free(con);
    return;
  }

  if (HostNameLookups == 2)
  {
   /*
    * Do double lookups as needed...
    */

    if ((addrlist = httpAddrGetList(httpGetHostname(con->http, NULL, 0), AF_UNSPEC, NULL))
            != NULL)
    {
     /*
      * See if the hostname maps to the same IP address...
      */

      for (addr = addrlist; addr; addr = addr->next)
        if (httpAddrEqual(httpGetAddress(con->http), &(addr->addr)))
          break;
    }
    else
      addr = NULL;

    httpAddrFreeList(addrlist);

    if (!addr)
    {
     /*
      * Can't have a hostname that doesn't resolve to the same IP address
      * with double-lookups enabled...
      */

      httpClose(con->http);

      cupsdLogClient(con, CUPSD_LOG_WARN,
                      "IP lookup failed - connection from %s closed!",
                      httpGetHostname(con->http, NULL, 0));
      free(con);
      return;
    }
  }
#endif /* 0 */

#ifdef HAVE_TCPD_H
 /*
  * See if the connection is denied by TCP wrappers...
  */

  request_init(&wrap_req, RQ_DAEMON, "cupsd", RQ_FILE, httpGetFd(con->http),
               NULL);
  fromhost(&wrap_req);

  if (!hosts_access(&wrap_req))
  {
    httpClose(con->http);

    cupsdLogClient(con, CUPSD_LOG_WARN,
                    "Connection from %s refused by /etc/hosts.allow and "
		    "/etc/hosts.deny rules.", httpGetHostname(con->http, NULL, 0));
    free(con);
    return;
  }
#endif /* HAVE_TCPD_H */

#ifdef AF_LOCAL
  if (httpAddrFamily(httpGetAddress(con->http)) == AF_LOCAL)
  {
#  ifdef __APPLE__
    socklen_t	peersize;		/* Size of peer credentials */
    pid_t	peerpid;		/* Peer process ID */
    char	peername[256];		/* Name of process */

    peersize = sizeof(peerpid);
    if (!getsockopt(con->number, SOL_LOCAL, LOCAL_PEERPID, &peerpid,
                    &peersize))
    {
      if (!proc_name(peerpid, peername, sizeof(peername)))
	cupsdLogClient(con, CUPSD_LOG_DEBUG,
	               "Accepted from %s (Domain ???[%d])",
                       httpGetHostname(con->http, NULL, 0), (int)peerpid);
      else
	cupsdLogClient(con, CUPSD_LOG_DEBUG,
                       "Accepted from %s (Domain %s[%d])",
                       httpGetHostname(con->http, NULL, 0), name, (int)peerpid);
    }
    else
#  endif /* __APPLE__ */

    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Accepted from %s (Domain)",
                   httpGetHostname(con->http, NULL, 0));
  }
  else
#endif /* AF_LOCAL */
  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Accepted from %s:%d (IPv%d)",
                 httpGetHostname(con->http, NULL, 0),
		 httpAddrPort(httpGetAddress(con->http)),
		 httpAddrFamily(httpGetAddress(con->http)) == AF_INET ? 4 : 6);

 /*
  * Get the local address the client connected to...
  */

  addrlen = sizeof(temp);
  if (getsockname(httpGetFd(con->http), (struct sockaddr *)&temp, &addrlen))
  {
    cupsdLogClient(con, CUPSD_LOG_ERROR, "Unable to get local address - %s",
                   strerror(errno));

    strlcpy(con->servername, "localhost", sizeof(con->servername));
    con->serverport = LocalPort;
  }
#ifdef AF_LOCAL
  else if (httpAddrFamily(&temp) == AF_LOCAL)
  {
    strlcpy(con->servername, "localhost", sizeof(con->servername));
    con->serverport = LocalPort;
  }
#endif /* AF_LOCAL */
  else
  {
    if (httpAddrLocalhost(&temp))
      strlcpy(con->servername, "localhost", sizeof(con->servername));
    else if (HostNameLookups)
      httpAddrLookup(&temp, con->servername, sizeof(con->servername));
    else
      httpAddrString(&temp, con->servername, sizeof(con->servername));

    con->serverport = httpAddrPort(&(lis->address));
  }

 /*
  * Add the connection to the array of active clients...
  */

  cupsArrayAdd(Clients, con);

 /*
  * Add the socket to the server select.
  */

  cupsdAddSelect(httpGetFd(con->http), (cupsd_selfunc_t)cupsdReadClient, NULL,
                 con);

  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Waiting for request.");

 /*
  * Temporarily suspend accept()'s until we lose a client...
  */

  if (cupsArrayCount(Clients) == MaxClients)
    cupsdPauseListening();

#ifdef HAVE_SSL
 /*
  * See if we are connecting on a secure port...
  */

  if (lis->encryption == HTTP_ENCRYPTION_ALWAYS)
  {
   /*
    * https connection; go secure...
    */

    if (!cupsdStartTLS(con))
      cupsdCloseClient(con);
  }
  else
    con->auto_ssl = 1;
#endif /* HAVE_SSL */
}


/*
 * 'cupsdCloseAllClients()' - Close all remote clients immediately.
 */

void
cupsdCloseAllClients(void)
{
  cupsd_client_t	*con;		/* Current client */


  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdCloseAllClients() Clients=%d",
                  cupsArrayCount(Clients));

  for (con = (cupsd_client_t *)cupsArrayFirst(Clients);
       con;
       con = (cupsd_client_t *)cupsArrayNext(Clients))
    if (cupsdCloseClient(con))
      cupsdCloseClient(con);
}


/*
 * 'cupsdCloseClient()' - Close a remote client.
 */

int					/* O - 1 if partial close, 0 if fully closed */
cupsdCloseClient(cupsd_client_t *con)	/* I - Client to close */
{
  int		partial;		/* Do partial close for SSL? */
#ifdef HAVE_LIBSSL
#elif defined(HAVE_GNUTLS)
#  elif defined(HAVE_CDSASSL)
#endif /* HAVE_LIBSSL */


  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Closing connection.");

 /*
  * Flush pending writes before closing...
  */

  httpFlushWrite(con->http);

  partial = 0;

  if (con->pipe_pid != 0)
  {
   /*
    * Stop any CGI process...
    */

    cupsdEndProcess(con->pipe_pid, 1);
    con->pipe_pid = 0;
  }

  if (con->file >= 0)
  {
    cupsdRemoveSelect(con->file);

    close(con->file);
    con->file = -1;
  }

 /*
  * Close the socket and clear the file from the input set for select()...
  */

  if (httpGetFd(con->http) >= 0)
  {
    cupsArrayRemove(ActiveClients, con);
    cupsdSetBusyState();

#ifdef HAVE_SSL
   /*
    * Shutdown encryption as needed...
    */

    if (httpIsEncrypted(con->http))
      partial = 1;
#endif /* HAVE_SSL */

    if (partial)
    {
     /*
      * Only do a partial close so that the encrypted client gets everything.
      */

      httpShutdown(con->http);
      cupsdAddSelect(httpGetFd(con->http), (cupsd_selfunc_t)cupsdReadClient,
                     NULL, con);

      cupsdLogClient(con, CUPSD_LOG_DEBUG, "Waiting for socket close.");
    }
    else
    {
     /*
      * Shut the socket down fully...
      */

      cupsdRemoveSelect(httpGetFd(con->http));
      httpClose(con->http);
      con->http = NULL;
    }
  }

  if (!partial)
  {
   /*
    * Free memory...
    */

    cupsdRemoveSelect(httpGetFd(con->http));

    httpClose(con->http);

    cupsdClearString(&con->filename);
    cupsdClearString(&con->command);
    cupsdClearString(&con->options);
    cupsdClearString(&con->query_string);

    if (con->request)
    {
      ippDelete(con->request);
      con->request = NULL;
    }

    if (con->response)
    {
      ippDelete(con->response);
      con->response = NULL;
    }

    if (con->language)
    {
      cupsLangFree(con->language);
      con->language = NULL;
    }

#ifdef HAVE_AUTHORIZATION_H
    if (con->authref)
    {
      AuthorizationFree(con->authref, kAuthorizationFlagDefaults);
      con->authref = NULL;
    }
#endif /* HAVE_AUTHORIZATION_H */

   /*
    * Re-enable new client connections if we are going back under the
    * limit...
    */

    if (cupsArrayCount(Clients) == MaxClients)
      cupsdResumeListening();

   /*
    * Compact the list of clients as necessary...
    */

    cupsArrayRemove(Clients, con);

    free(con);
  }

  return (partial);
}


/*
 * 'cupsdFlushHeader()' - Flush the header fields to the client.
 */

int					/* I - Bytes written or -1 on error */
cupsdFlushHeader(cupsd_client_t *con)	/* I - Client to flush to */
{
  int bytes = httpFlushWrite(con->http);

  // TODO: Need to use httpSendResponse
  con->http->data_encoding = HTTP_ENCODING_LENGTH;

  return (bytes);
}


/*
 * 'cupsdReadClient()' - Read data from a client.
 */

void
cupsdReadClient(cupsd_client_t *con)	/* I - Client to read from */
{
  char			line[32768],	/* Line from client... */
			operation[64],	/* Operation code from socket */
			version[64],	/* HTTP version number string */
			locale[64],	/* Locale */
			*ptr;		/* Pointer into strings */
  int			major, minor;	/* HTTP version numbers */
  http_status_t		status;		/* Transfer status */
  ipp_state_t		ipp_state;	/* State of IPP transfer */
  int			bytes;		/* Number of bytes to POST */
  char			*filename;	/* Name of file for GET/HEAD */
  char			buf[1024];	/* Buffer for real filename */
  struct stat		filestats;	/* File information */
  mime_type_t		*type;		/* MIME type of file */
  cupsd_printer_t	*p;		/* Printer */
  static unsigned	request_id = 0;	/* Request ID for temp files */


  status = HTTP_STATUS_CONTINUE;

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		 "cupsdReadClient "
		 "error=%d, "
		 "used=%d, "
		 "state=%s, "
		 "data_encoding=HTTP_ENCODING_%s, "
		 "data_remaining=" CUPS_LLFMT ", "
		 "request=%p(%s), "
		 "file=%d",
		 httpError(con->http), (int)httpGetReady(con->http),
		 httpStateString(httpGetState(con->http)),
		 httpIsChunked(con->http) ? "CHUNKED" : "LENGTH",
		 CUPS_LLCAST httpGetRemaining(con->http),
		 con->request,
		 con->request ? ipp_states[con->request->state] : "",
		 con->file);

#ifdef HAVE_SSL
  if (con->auto_ssl)
  {
   /*
    * Automatically check for a SSL/TLS handshake...
    */

    con->auto_ssl = 0;

    if (recv(httpGetFd(con->http), buf, 1, MSG_PEEK) == 1 &&
        (!buf[0] || !strchr("DGHOPT", buf[0])))
    {
     /*
      * Encrypt this connection...
      */

      cupsdLogClient(con, CUPSD_LOG_DEBUG2,
                     "Saw first byte %02X, auto-negotiating "
		     "SSL/TLS session.", buf[0] & 255);

      if (!cupsdStartTLS(con))
        cupsdCloseClient(con);

      return;
    }
  }
#endif /* HAVE_SSL */

  switch (httpGetState(con->http))
  {
    case HTTP_STATE_WAITING :
       /*
        * See if we've received a request line...
	*/

        if (httpGets(line, sizeof(line) - 1, con->http) == NULL)
	{
	  if (httpError(con->http) && httpError(con->http) != EPIPE)
	    cupsdLogClient(con, CUPSD_LOG_DEBUG,
			   "HTTP_STATE_WAITING Closing for error %d (%s)",
			   httpError(con->http), strerror(httpError(con->http)));
	  else
	    cupsdLogClient(con, CUPSD_LOG_DEBUG,
	                   "HTTP_STATE_WAITING Closing on EOF.");

	  cupsdCloseClient(con);
	  return;
	}

       /*
        * Ignore blank request lines...
	*/

        if (line[0] == '\0')
	  break;

       /*
        * Clear other state variables...
	*/

        httpClearFields(con->http);

        con->http->activity        = time(NULL);
        con->http->version         = HTTP_VERSION_1_0;
	httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);
	con->http->data_encoding   = HTTP_ENCODING_LENGTH;
	con->http->data_remaining  = 0;
	con->http->_data_remaining = 0;
	con->operation            = HTTP_STATE_WAITING;
	con->bytes                = 0;
	con->file                 = -1;
	con->file_ready           = 0;
	con->pipe_pid             = 0;
	con->username[0]          = '\0';
	con->password[0]          = '\0';
	con->uri[0]               = '\0';

	cupsdClearString(&con->command);
	cupsdClearString(&con->options);
	cupsdClearString(&con->query_string);

	if (con->request)
	{
	  ippDelete(con->request);
	  con->request = NULL;
	}

	if (con->response)
	{
	  ippDelete(con->response);
	  con->response = NULL;
	}

	if (con->language)
	{
	  cupsLangFree(con->language);
	  con->language = NULL;
	}

#ifdef HAVE_GSSAPI
        con->have_gss = 0;
	con->gss_uid  = 0;
#endif /* HAVE_GSSAPI */

       /*
        * Grab the request line...
	*/

        switch (sscanf(line, "%63s%1023s%63s", operation, con->uri, version))
	{
	  case 1 :
	      if (line[0])
	      {
		cupsdLogClient(con, CUPSD_LOG_ERROR,
                               "Bad request line \"%s\" from %s.",
                               _httpEncodeURI(buf, line, sizeof(buf)),
                               httpGetHostname(con->http, NULL, 0));
		cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
		cupsdCloseClient(con);
              }
	      return;
	  case 2 :
	      con->http->version = HTTP_VERSION_0_9;
	      break;
	  case 3 :
	      if (sscanf(version, "HTTP/%d.%d", &major, &minor) != 2)
	      {
		cupsdLogClient(con, CUPSD_LOG_ERROR,
                               "Bad request line \"%s\" from %s.",
                               _httpEncodeURI(buf, line, sizeof(buf)),
                               httpGetHostname(con->http, NULL, 0));
		cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
		cupsdCloseClient(con);
		return;
	      }

	      if (major < 2)
	      {
	        con->http->version = (http_version_t)(major * 100 + minor);
		if (con->http->version == HTTP_VERSION_1_1 && KeepAlive)
		  httpSetKeepAlive(con->http, HTTP_KEEPALIVE_ON);
		else
		  httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);
	      }
	      else
	      {
		cupsdLogClient(con, CUPSD_LOG_ERROR,
                               "Unsupported request line \"%s\" from %s.",
                               _httpEncodeURI(buf, line, sizeof(buf)),
                               httpGetHostname(con->http, NULL, 0));
	        cupsdSendError(con, HTTP_STATUS_NOT_SUPPORTED, CUPSD_AUTH_NONE);
		cupsdCloseClient(con);
		return;
	      }
	      break;
	}

       /*
        * Handle full URLs in the request line...
	*/

        if (strcmp(con->uri, "*"))
	{
	  char	scheme[HTTP_MAX_URI],	/* Method/scheme */
		userpass[HTTP_MAX_URI],	/* Username:password */
		hostname[HTTP_MAX_URI],	/* Hostname */
		resource[HTTP_MAX_URI];	/* Resource path */
          int	port;			/* Port number */


         /*
	  * Separate the URI into its components...
	  */

          httpSeparateURI(HTTP_URI_CODING_MOST, con->uri,
	                  scheme, sizeof(scheme),
	                  userpass, sizeof(userpass),
			  hostname, sizeof(hostname), &port,
			  resource, sizeof(resource));

         /*
	  * Only allow URIs with the servername, localhost, or an IP
	  * address...
	  */

	  if (strcmp(scheme, "file") &&
	      _cups_strcasecmp(hostname, ServerName) &&
	      _cups_strcasecmp(hostname, "localhost") &&
	      !isdigit(hostname[0]) && hostname[0] != '[')
	  {
	   /*
	    * Nope, we don't do proxies...
	    */

	    cupsdLogClient(con, CUPSD_LOG_ERROR, "Bad URI \"%s\" in request.",
                           con->uri);
	    cupsdSendError(con, HTTP_STATUS_METHOD_NOT_ALLOWED, CUPSD_AUTH_NONE);
	    cupsdCloseClient(con);
	    return;
	  }

         /*
	  * Copy the resource portion back into the URI; both resource and
	  * con->uri are HTTP_MAX_URI bytes in size...
	  */

          strlcpy(con->uri, resource, sizeof(con->uri));
	}

       /*
        * Process the request...
	*/

        if (!strcmp(operation, "GET"))
	  con->http->state = HTTP_STATE_GET;
        else if (!strcmp(operation, "PUT"))
	  con->http->state = HTTP_STATE_PUT;
        else if (!strcmp(operation, "POST"))
	  con->http->state = HTTP_STATE_POST;
        else if (!strcmp(operation, "DELETE"))
	  con->http->state = HTTP_STATE_DELETE;
        else if (!strcmp(operation, "TRACE"))
	  con->http->state = HTTP_STATE_TRACE;
        else if (!strcmp(operation, "OPTIONS"))
	  con->http->state = HTTP_STATE_OPTIONS;
        else if (!strcmp(operation, "HEAD"))
	  con->http->state = HTTP_STATE_HEAD;
	else
	{
	  cupsdLogClient(con, CUPSD_LOG_ERROR, "Bad operation \"%s\".",
	                 operation);
	  cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
	  cupsdCloseClient(con);
	  return;
	}

        gettimeofday(&(con->start), NULL);
        con->operation = httpGetState(con->http);

        cupsdLogClient(con, CUPSD_LOG_DEBUG, "%s %s HTTP/%d.%d",
	               operation, con->uri, con->http->version / 100,
                       con->http->version % 100);

	con->http->status = HTTP_STATUS_OK;

        if (!cupsArrayFind(ActiveClients, con))
	{
	  cupsArrayAdd(ActiveClients, con);
          cupsdSetBusyState();
        }

    case HTTP_STATE_OPTIONS :
    case HTTP_STATE_DELETE :
    case HTTP_STATE_GET :
    case HTTP_STATE_HEAD :
    case HTTP_STATE_POST :
    case HTTP_STATE_PUT :
    case HTTP_STATE_TRACE :
       /*
        * Parse incoming parameters until the status changes...
	*/

        while ((status = httpUpdate(con->http)) == HTTP_STATUS_CONTINUE)
	  if (!httpGetReady(con->http))
	    break;

	if (status != HTTP_STATUS_OK && status != HTTP_STATUS_CONTINUE)
	{
	  if (httpError(con->http) && httpError(con->http) != EPIPE)
	    cupsdLogClient(con, CUPSD_LOG_DEBUG,
                           "Closing for error %d (%s) while reading headers.",
                           httpError(con->http), strerror(httpError(con->http)));
	  else
	    cupsdLogClient(con, CUPSD_LOG_DEBUG,
	                   "Closing on EOF while reading headers.");

	  cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
	  cupsdCloseClient(con);
	  return;
	}
	break;

    default :
        if (!httpGetReady(con->http) && recv(httpGetFd(con->http), buf, 1, MSG_PEEK) < 1)
	{
	 /*
	  * Connection closed...
	  */

	  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Closing on EOF.");
          cupsdCloseClient(con);
	  return;
	}
        break; /* Anti-compiler-warning-code */
  }

 /*
  * Handle new transfers...
  */

  if (status == HTTP_STATUS_OK)
  {
    if (httpGetField(con->http, HTTP_FIELD_ACCEPT_LANGUAGE)[0])
    {
     /*
      * Figure out the locale from the Accept-Language and Content-Type
      * fields...
      */

      if ((ptr = strchr(httpGetField(con->http, HTTP_FIELD_ACCEPT_LANGUAGE),
                        ',')) != NULL)
        *ptr = '\0';

      if ((ptr = strchr(httpGetField(con->http, HTTP_FIELD_ACCEPT_LANGUAGE),
                        ';')) != NULL)
        *ptr = '\0';

      if ((ptr = strstr(httpGetField(con->http, HTTP_FIELD_CONTENT_TYPE),
                        "charset=")) != NULL)
      {
       /*
        * Combine language and charset, and trim any extra params in the
	* content-type.
	*/

        snprintf(locale, sizeof(locale), "%s.%s",
	         httpGetField(con->http, HTTP_FIELD_ACCEPT_LANGUAGE), ptr + 8);

	if ((ptr = strchr(locale, ',')) != NULL)
	  *ptr = '\0';
      }
      else
        snprintf(locale, sizeof(locale), "%s.UTF-8",
	         httpGetField(con->http, HTTP_FIELD_ACCEPT_LANGUAGE));

      con->language = cupsLangGet(locale);
    }
    else
      con->language = cupsLangGet(DefaultLocale);

    cupsdAuthorize(con);

    if (!_cups_strncasecmp(httpGetField(con->http, HTTP_FIELD_CONNECTION),
                           "Keep-Alive", 10) && KeepAlive)
      httpSetKeepAlive(con->http, HTTP_KEEPALIVE_ON);
    else if (!_cups_strncasecmp(httpGetField(con->http, HTTP_FIELD_CONNECTION),
                                "close", 5))
      httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);

    if (!httpGetField(con->http, HTTP_FIELD_HOST)[0] &&
        httpGetVersion(con->http) >= HTTP_VERSION_1_1)
    {
     /*
      * HTTP/1.1 and higher require the "Host:" field...
      */

      if (!cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE))
      {
        cupsdLogClient(con, CUPSD_LOG_ERROR, "Missing Host: field in request.");
	cupsdCloseClient(con);
	return;
      }
    }
    else if (!valid_host(con))
    {
     /*
      * Access to localhost must use "localhost" or the corresponding IPv4
      * or IPv6 values in the Host: field.
      */

      cupsdLogClient(con, CUPSD_LOG_ERROR,
                     "Request from \"%s\" using invalid Host: field \"%s\".",
                     httpGetHostname(con->http, NULL, 0), httpGetField(con->http, HTTP_FIELD_HOST));

      if (!cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE))
      {
	cupsdCloseClient(con);
	return;
      }
    }
    else if (con->operation == HTTP_STATE_OPTIONS)
    {
     /*
      * Do OPTIONS command...
      */

      if (con->best && con->best->type != CUPSD_AUTH_NONE)
      {
	if (!cupsdSendHeader(con, HTTP_STATUS_UNAUTHORIZED, NULL, CUPSD_AUTH_NONE))
	{
	  cupsdCloseClient(con);
	  return;
	}
      }

      if (!_cups_strcasecmp(httpGetField(con->http, HTTP_FIELD_CONNECTION), "Upgrade") &&
	  !httpIsEncrypted(con->http))
      {
#ifdef HAVE_SSL
       /*
        * Do encryption stuff...
	*/

	if (!cupsdSendHeader(con, HTTP_STATUS_SWITCHING_PROTOCOLS, NULL, CUPSD_AUTH_NONE))
	{
	  cupsdCloseClient(con);
	  return;
	}

	httpPrintf(con->http, "Connection: Upgrade\r\n");
	httpPrintf(con->http, "Upgrade: TLS/1.2,TLS/1.1,TLS/1.0\r\n");
	httpPrintf(con->http, "Content-Length: 0\r\n");
	httpPrintf(con->http, "\r\n");

	if (cupsdFlushHeader(con) < 0)
        {
	  cupsdCloseClient(con);
	  return;
	}

        if (!cupsdStartTLS(con))
        {
	  cupsdCloseClient(con);
	  return;
	}
#else
	if (!cupsdSendError(con, HTTP_NOT_IMPLEMENTED, CUPSD_AUTH_NONE))
	{
	  cupsdCloseClient(con);
	  return;
	}
#endif /* HAVE_SSL */
      }

      if (!cupsdSendHeader(con, HTTP_STATUS_OK, NULL, CUPSD_AUTH_NONE))
      {
	cupsdCloseClient(con);
	return;
      }

      httpPrintf(con->http, "Allow: GET, HEAD, OPTIONS, POST, PUT\r\n");
      httpPrintf(con->http, "Content-Length: 0\r\n");
      httpPrintf(con->http, "\r\n");

      if (cupsdFlushHeader(con) < 0)
      {
	cupsdCloseClient(con);
	return;
      }
    }
    else if (!is_path_absolute(con->uri))
    {
     /*
      * Protect against malicious users!
      */

      cupsdLogClient(con, CUPSD_LOG_ERROR,
                     "Request for non-absolute resource \"%s\".", con->uri);

      if (!cupsdSendError(con, HTTP_STATUS_FORBIDDEN, CUPSD_AUTH_NONE))
      {
	cupsdCloseClient(con);
	return;
      }
    }
    else
    {
      if (!_cups_strcasecmp(httpGetField(con->http, HTTP_FIELD_CONNECTION),
                            "Upgrade") && con->http->tls == NULL)
      {
#ifdef HAVE_SSL
       /*
        * Do encryption stuff...
	*/

	if (!cupsdSendHeader(con, HTTP_STATUS_SWITCHING_PROTOCOLS, NULL,
	                     CUPSD_AUTH_NONE))
	{
	  cupsdCloseClient(con);
	  return;
	}

	httpPrintf(con->http, "Connection: Upgrade\r\n");
	httpPrintf(con->http, "Upgrade: TLS/1.2,TLS/1.1,TLS/1.0\r\n");
	httpPrintf(con->http, "Content-Length: 0\r\n");
	httpPrintf(con->http, "\r\n");

	if (cupsdFlushHeader(con) < 0)
        {
	  cupsdCloseClient(con);
	  return;
	}

        if (!cupsdStartTLS(con))
        {
	  cupsdCloseClient(con);
	  return;
	}
#else
	if (!cupsdSendError(con, HTTP_NOT_IMPLEMENTED, CUPSD_AUTH_NONE))
	{
	  cupsdCloseClient(con);
	  return;
	}
#endif /* HAVE_SSL */
      }

      if ((status = cupsdIsAuthorized(con, NULL)) != HTTP_STATUS_OK)
      {
	cupsdSendError(con, status, CUPSD_AUTH_NONE);
	cupsdCloseClient(con);
	return;
      }

      if (con->http->expect &&
          (con->operation == HTTP_STATE_POST || con->operation == HTTP_STATE_PUT))
      {
        if (con->http->expect == HTTP_STATUS_CONTINUE)
	{
	 /*
	  * Send 100-continue header...
	  */

	  if (!cupsdSendHeader(con, HTTP_STATUS_CONTINUE, NULL, CUPSD_AUTH_NONE))
	  {
	    cupsdCloseClient(con);
	    return;
	  }
	}
	else
	{
	 /*
	  * Send 417-expectation-failed header...
	  */

	  if (!cupsdSendHeader(con, HTTP_STATUS_EXPECTATION_FAILED, NULL,
	                       CUPSD_AUTH_NONE))
	  {
	    cupsdCloseClient(con);
	    return;
	  }

	  httpPrintf(con->http, "Content-Length: 0\r\n");
	  httpPrintf(con->http, "\r\n");

	  if (cupsdFlushHeader(con) < 0)
          {
	    cupsdCloseClient(con);
	    return;
	  }
	}
      }

      switch (httpGetState(con->http))
      {
	case HTTP_STATE_GET_SEND :
            if ((!strncmp(con->uri, "/ppd/", 5) ||
		 !strncmp(con->uri, "/printers/", 10) ||
		 !strncmp(con->uri, "/classes/", 9)) &&
		!strcmp(con->uri + strlen(con->uri) - 4, ".ppd"))
	    {
	     /*
	      * Send PPD file - get the real printer name since printer
	      * names are not case sensitive but filenames can be...
	      */

              con->uri[strlen(con->uri) - 4] = '\0';	/* Drop ".ppd" */

	      if (!strncmp(con->uri, "/ppd/", 5))
		p = cupsdFindPrinter(con->uri + 5);
	      else if (!strncmp(con->uri, "/printers/", 10))
		p = cupsdFindPrinter(con->uri + 10);
	      else
	      {
		p = cupsdFindClass(con->uri + 9);

		if (p)
		{
		  int i;		/* Looping var */

		  for (i = 0; i < p->num_printers; i ++)
		  {
		    if (!(p->printers[i]->type & CUPS_PRINTER_CLASS))
		    {
		      char ppdname[1024];/* PPD filename */

		      snprintf(ppdname, sizeof(ppdname), "%s/ppd/%s.ppd",
		               ServerRoot, p->printers[i]->name);
		      if (!access(ppdname, 0))
		      {
		        p = p->printers[i];
		        break;
		      }
		    }
		  }

                  if (i >= p->num_printers)
                    p = NULL;
		}
	      }

	      if (p)
	      {
		snprintf(con->uri, sizeof(con->uri), "/ppd/%s.ppd", p->name);
	      }
	      else
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }
	    }
            else if ((!strncmp(con->uri, "/icons/", 7) ||
		      !strncmp(con->uri, "/printers/", 10) ||
		      !strncmp(con->uri, "/classes/", 9)) &&
		     !strcmp(con->uri + strlen(con->uri) - 4, ".png"))
	    {
	     /*
	      * Send icon file - get the real queue name since queue names are
	      * not case sensitive but filenames can be...
	      */

	      con->uri[strlen(con->uri) - 4] = '\0';	/* Drop ".png" */

              if (!strncmp(con->uri, "/icons/", 7))
                p = cupsdFindPrinter(con->uri + 7);
              else if (!strncmp(con->uri, "/printers/", 10))
                p = cupsdFindPrinter(con->uri + 10);
              else
              {
		p = cupsdFindClass(con->uri + 9);

		if (p)
		{
		  int i;		/* Looping var */

		  for (i = 0; i < p->num_printers; i ++)
		  {
		    if (!(p->printers[i]->type & CUPS_PRINTER_CLASS))
		    {
		      char ppdname[1024];/* PPD filename */

		      snprintf(ppdname, sizeof(ppdname), "%s/ppd/%s.ppd",
		               ServerRoot, p->printers[i]->name);
		      if (!access(ppdname, 0))
		      {
		        p = p->printers[i];
		        break;
		      }
		    }
		  }

                  if (i >= p->num_printers)
                    p = NULL;
		}
	      }

              if (p)
		snprintf(con->uri, sizeof(con->uri), "/icons/%s.png", p->name);
	      else
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }
	    }
	    else if (!WebInterface)
	    {
	     /*
	      * Web interface is disabled. Show an appropriate message...
	      */

	      if (!cupsdSendError(con, HTTP_STATUS_CUPS_WEBIF_DISABLED, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }

	    if ((!strncmp(con->uri, "/admin", 6) &&
		  strncmp(con->uri, "/admin/conf/", 12) &&
		  strncmp(con->uri, "/admin/log/", 11)) ||
		 !strncmp(con->uri, "/printers", 9) ||
		 !strncmp(con->uri, "/classes", 8) ||
		 !strncmp(con->uri, "/help", 5) ||
		 !strncmp(con->uri, "/jobs", 5))
	    {
	     /*
	      * Send CGI output...
	      */

              if (!strncmp(con->uri, "/admin", 6))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/admin.cgi",
		                ServerBin);

		cupsdSetString(&con->options, strchr(con->uri + 6, '?'));
	      }
              else if (!strncmp(con->uri, "/printers", 9))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/printers.cgi",
		                ServerBin);

                if (con->uri[9] && con->uri[10])
		  cupsdSetString(&con->options, con->uri + 9);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else if (!strncmp(con->uri, "/classes", 8))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/classes.cgi",
		                ServerBin);

                if (con->uri[8] && con->uri[9])
		  cupsdSetString(&con->options, con->uri + 8);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else if (!strncmp(con->uri, "/jobs", 5))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/jobs.cgi",
		                ServerBin);

                if (con->uri[5] && con->uri[6])
		  cupsdSetString(&con->options, con->uri + 5);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/help.cgi",
		                ServerBin);

                if (con->uri[5] && con->uri[6])
		  cupsdSetString(&con->options, con->uri + 5);
		else
		  cupsdSetString(&con->options, NULL);
	      }

              if (!cupsdSendCommand(con, con->command, con->options, 0))
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}
              }
	      else
        	cupsdLogRequest(con, HTTP_STATUS_OK);

	      if (con->http->version <= HTTP_VERSION_1_0)
		httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);
	    }
            else if ((!strncmp(con->uri, "/admin/conf/", 12) &&
	              (strchr(con->uri + 12, '/') ||
		       strlen(con->uri) == 12)) ||
		     (!strncmp(con->uri, "/admin/log/", 11) &&
	              (strchr(con->uri + 11, '/') ||
		       strlen(con->uri) == 11)))
	    {
	     /*
	      * GET can only be done to configuration files directly under
	      * /admin/conf...
	      */

	      cupsdLogClient(con, CUPSD_LOG_ERROR,
			      "Request for subdirectory \"%s\"!", con->uri);

	      if (!cupsdSendError(con, HTTP_STATUS_FORBIDDEN, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }
	    else
	    {
	     /*
	      * Serve a file...
	      */

              if ((filename = get_file(con, &filestats, buf,
	                               sizeof(buf))) == NULL)
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }

	      type = mimeFileType(MimeDatabase, filename, NULL, NULL);

              if (is_cgi(con, filename, &filestats, type))
	      {
	       /*
	        * Note: con->command and con->options were set by
		* is_cgi()...
		*/

        	if (!cupsdSendCommand(con, con->command, con->options, 0))
		{
		  if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		  {
		    cupsdCloseClient(con);
		    return;
		  }
        	}
		else
        	  cupsdLogRequest(con, HTTP_STATUS_OK);

		if (con->http->version <= HTTP_VERSION_1_0)
		  httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);
	        break;
	      }

	      if (!check_if_modified(con, &filestats))
              {
        	if (!cupsdSendError(con, HTTP_STATUS_NOT_MODIFIED, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}
	      }
	      else
              {
		if (type == NULL)
	          strlcpy(line, "text/plain", sizeof(line));
		else
	          snprintf(line, sizeof(line), "%s/%s", type->super, type->type);

        	if (!write_file(con, HTTP_STATUS_OK, filename, line, &filestats))
		{
		  cupsdCloseClient(con);
		  return;
		}
	      }
	    }
            break;

	case HTTP_STATE_POST_RECV :
           /*
	    * See if the POST request includes a Content-Length field, and if
	    * so check the length against any limits that are set...
	    */

            if (httpGetField(con->http, HTTP_FIELD_CONTENT_LENGTH)[0] &&
		MaxRequestSize > 0 &&
		con->http->data_remaining > MaxRequestSize)
	    {
	     /*
	      * Request too large...
	      */

              if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
            }
	    else if (con->http->data_remaining < 0 ||
	             (!httpGetField(con->http, HTTP_FIELD_CONTENT_LENGTH)[0] &&
		      con->http->data_encoding == HTTP_ENCODING_LENGTH))
	    {
	     /*
	      * Negative content lengths are invalid!
	      */

              if (!cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }

           /*
	    * See what kind of POST request this is; for IPP requests the
	    * content-type field will be "application/ipp"...
	    */

	    if (!strcmp(httpGetField(con->http, HTTP_FIELD_CONTENT_TYPE),
	                "application/ipp"))
              con->request = ippNew();
            else if (!WebInterface)
	    {
	     /*
	      * Web interface is disabled. Show an appropriate message...
	      */

	      if (!cupsdSendError(con, HTTP_STATUS_CUPS_WEBIF_DISABLED, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }
	    else if ((!strncmp(con->uri, "/admin", 6) &&
	              strncmp(con->uri, "/admin/conf/", 12) &&
	              strncmp(con->uri, "/admin/log/", 11)) ||
	             !strncmp(con->uri, "/printers", 9) ||
	             !strncmp(con->uri, "/classes", 8) ||
	             !strncmp(con->uri, "/help", 5) ||
	             !strncmp(con->uri, "/jobs", 5))
	    {
	     /*
	      * CGI request...
	      */

              if (!strncmp(con->uri, "/admin", 6))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/admin.cgi",
		                ServerBin);

		cupsdSetString(&con->options, strchr(con->uri + 6, '?'));
	      }
              else if (!strncmp(con->uri, "/printers", 9))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/printers.cgi",
		                ServerBin);

                if (con->uri[9] && con->uri[10])
		  cupsdSetString(&con->options, con->uri + 9);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else if (!strncmp(con->uri, "/classes", 8))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/classes.cgi",
		                ServerBin);

                if (con->uri[8] && con->uri[9])
		  cupsdSetString(&con->options, con->uri + 8);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else if (!strncmp(con->uri, "/jobs", 5))
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/jobs.cgi",
		                ServerBin);

                if (con->uri[5] && con->uri[6])
		  cupsdSetString(&con->options, con->uri + 5);
		else
		  cupsdSetString(&con->options, NULL);
	      }
	      else
	      {
		cupsdSetStringf(&con->command, "%s/cgi-bin/help.cgi",
		                ServerBin);

                if (con->uri[5] && con->uri[6])
		  cupsdSetString(&con->options, con->uri + 5);
		else
		  cupsdSetString(&con->options, NULL);
	      }

	      if (con->http->version <= HTTP_VERSION_1_0)
		httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);
	    }
	    else
	    {
	     /*
	      * POST to a file...
	      */

              if ((filename = get_file(con, &filestats, buf,
	                               sizeof(buf))) == NULL)
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }

	      type = mimeFileType(MimeDatabase, filename, NULL, NULL);

              if (!is_cgi(con, filename, &filestats, type))
	      {
	       /*
	        * Only POST to CGI's...
		*/

		if (!cupsdSendError(con, HTTP_STATUS_UNAUTHORIZED, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}
	      }
	    }
	    break;

	case HTTP_STATE_PUT_RECV :
	   /*
	    * Validate the resource name...
	    */

            if (strcmp(con->uri, "/admin/conf/cupsd.conf"))
	    {
	     /*
	      * PUT can only be done to the cupsd.conf file...
	      */

	      cupsdLogClient(con, CUPSD_LOG_ERROR,
			     "Disallowed PUT request for \"%s\".", con->uri);

	      if (!cupsdSendError(con, HTTP_STATUS_FORBIDDEN, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }

           /*
	    * See if the PUT request includes a Content-Length field, and if
	    * so check the length against any limits that are set...
	    */

            if (httpGetField(con->http, HTTP_FIELD_CONTENT_LENGTH)[0] &&
		MaxRequestSize > 0 &&
		con->http->data_remaining > MaxRequestSize)
	    {
	     /*
	      * Request too large...
	      */

              if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
            }
	    else if (con->http->data_remaining < 0)
	    {
	     /*
	      * Negative content lengths are invalid!
	      */

              if (!cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }

           /*
	    * Open a temporary file to hold the request...
	    */

            cupsdSetStringf(&con->filename, "%s/%08x", RequestRoot,
	                    request_id ++);
	    con->file = open(con->filename, O_WRONLY | O_CREAT | O_TRUNC, 0640);

	    if (con->file < 0)
	    {
	      cupsdLogClient(con, CUPSD_LOG_ERROR,
	                     "Unable to create request file \"%s\": %s",
                             con->filename, strerror(errno));

	      if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }
	    }

	    fchmod(con->file, 0640);
	    fchown(con->file, RunUser, Group);
	    fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);
	    break;

	case HTTP_STATE_DELETE :
	case HTTP_STATE_TRACE :
            cupsdSendError(con, HTTP_STATUS_NOT_IMPLEMENTED, CUPSD_AUTH_NONE);
	    cupsdCloseClient(con);
	    return;

	case HTTP_STATE_HEAD :
            if (!strncmp(con->uri, "/printers/", 10) &&
		!strcmp(con->uri + strlen(con->uri) - 4, ".ppd"))
	    {
	     /*
	      * Send PPD file - get the real printer name since printer
	      * names are not case sensitive but filenames can be...
	      */

              con->uri[strlen(con->uri) - 4] = '\0';	/* Drop ".ppd" */

              if ((p = cupsdFindPrinter(con->uri + 10)) != NULL)
		snprintf(con->uri, sizeof(con->uri), "/ppd/%s.ppd", p->name);
	      else
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }
	    }
            else if (!strncmp(con->uri, "/printers/", 10) &&
		     !strcmp(con->uri + strlen(con->uri) - 4, ".png"))
	    {
	     /*
	      * Send PNG file - get the real printer name since printer
	      * names are not case sensitive but filenames can be...
	      */

              con->uri[strlen(con->uri) - 4] = '\0';	/* Drop ".ppd" */

              if ((p = cupsdFindPrinter(con->uri + 10)) != NULL)
		snprintf(con->uri, sizeof(con->uri), "/icons/%s.png", p->name);
	      else
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}

		break;
	      }
	    }
	    else if (!WebInterface)
	    {
              if (!cupsdSendHeader(con, HTTP_STATUS_OK, NULL, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (httpPrintf(con->http, "\r\n") < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (cupsdFlushHeader(con) < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

	      con->http->state = HTTP_STATE_WAITING;
	      DEBUG_puts("cupsdReadClient: Set state to HTTP_STATE_WAITING "
	                 "after HEAD.");
	      break;
	    }

	    if ((!strncmp(con->uri, "/admin", 6) &&
	         strncmp(con->uri, "/admin/conf/", 12) &&
	         strncmp(con->uri, "/admin/log/", 11)) ||
		!strncmp(con->uri, "/printers", 9) ||
		!strncmp(con->uri, "/classes", 8) ||
		!strncmp(con->uri, "/help", 5) ||
		!strncmp(con->uri, "/jobs", 5))
	    {
	     /*
	      * CGI output...
	      */

              if (!cupsdSendHeader(con, HTTP_STATUS_OK, "text/html", CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (httpPrintf(con->http, "\r\n") < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (cupsdFlushHeader(con) < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

              cupsdLogRequest(con, HTTP_STATUS_OK);
	    }
            else if ((!strncmp(con->uri, "/admin/conf/", 12) &&
	              (strchr(con->uri + 12, '/') ||
		       strlen(con->uri) == 12)) ||
		     (!strncmp(con->uri, "/admin/log/", 11) &&
	              (strchr(con->uri + 11, '/') ||
		       strlen(con->uri) == 11)))
	    {
	     /*
	      * HEAD can only be done to configuration files under
	      * /admin/conf...
	      */

	      cupsdLogClient(con, CUPSD_LOG_ERROR,
			     "Request for subdirectory \"%s\".", con->uri);

	      if (!cupsdSendError(con, HTTP_STATUS_FORBIDDEN, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      break;
	    }
	    else if ((filename = get_file(con, &filestats, buf,
	                                  sizeof(buf))) == NULL)
	    {
	      if (!cupsdSendHeader(con, HTTP_STATUS_NOT_FOUND, "text/html",
	                           CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

              cupsdLogRequest(con, HTTP_STATUS_NOT_FOUND);
	    }
	    else if (!check_if_modified(con, &filestats))
            {
              if (!cupsdSendError(con, HTTP_STATUS_NOT_MODIFIED, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

              cupsdLogRequest(con, HTTP_STATUS_NOT_MODIFIED);
	    }
	    else
	    {
	     /*
	      * Serve a file...
	      */

	      type = mimeFileType(MimeDatabase, filename, NULL, NULL);
	      if (type == NULL)
		strlcpy(line, "text/plain", sizeof(line));
	      else
		snprintf(line, sizeof(line), "%s/%s", type->super, type->type);

              if (!cupsdSendHeader(con, HTTP_STATUS_OK, line, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (httpPrintf(con->http, "Last-Modified: %s\r\n",
	                     httpGetDateString(filestats.st_mtime)) < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

	      if (httpPrintf(con->http, "Content-Length: %lu\r\n",
	                     (unsigned long)filestats.st_size) < 0)
	      {
		cupsdCloseClient(con);
		return;
	      }

              cupsdLogRequest(con, HTTP_STATUS_OK);
	    }

            if (httpPrintf(con->http, "\r\n") < 0)
	    {
	      cupsdCloseClient(con);
	      return;
	    }

	    if (cupsdFlushHeader(con) < 0)
            {
	      cupsdCloseClient(con);
	      return;
	    }

            con->http->state = HTTP_STATE_WAITING;
	    DEBUG_puts("cupsdReadClient: Set state to HTTP_STATE_WAITING "
		       "after HEAD.");
            break;

	default :
            break; /* Anti-compiler-warning-code */
      }
    }
  }

 /*
  * Handle any incoming data...
  */

  switch (httpGetState(con->http))
  {
    case HTTP_STATE_PUT_RECV :
        do
	{
          if ((bytes = httpRead2(con->http, line, sizeof(line))) < 0)
	  {
	    if (httpError(con->http) && httpError(con->http) != EPIPE)
	      cupsdLogClient(con, CUPSD_LOG_DEBUG,
                             "HTTP_STATE_PUT_RECV Closing for error %d (%s)",
                             httpError(con->http), strerror(httpError(con->http)));
	    else
	      cupsdLogClient(con, CUPSD_LOG_DEBUG,
			     "HTTP_STATE_PUT_RECV Closing on EOF.");

	    cupsdCloseClient(con);
	    return;
	  }
	  else if (bytes > 0)
	  {
	    con->bytes += bytes;

            if (write(con->file, line, bytes) < bytes)
	    {
              cupsdLogClient(con, CUPSD_LOG_ERROR,
	                     "Unable to write %d bytes to \"%s\": %s", bytes,
                             con->filename, strerror(errno));

	      close(con->file);
	      con->file = -1;
	      unlink(con->filename);
	      cupsdClearString(&con->filename);

              if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }
	    }
	  }
        }
	while (httpGetState(con->http) == HTTP_STATE_PUT_RECV && httpGetReady(con->http));

        if (httpGetState(con->http) == HTTP_STATE_STATUS)
	{
	 /*
	  * End of file, see how big it is...
	  */

	  fstat(con->file, &filestats);

	  close(con->file);
	  con->file = -1;

          if (filestats.st_size > MaxRequestSize &&
	      MaxRequestSize > 0)
	  {
	   /*
	    * Request is too big; remove it and send an error...
	    */

	    unlink(con->filename);
	    cupsdClearString(&con->filename);

            if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	    {
	      cupsdCloseClient(con);
	      return;
	    }
	  }

         /*
	  * Install the configuration file...
	  */

          status = install_cupsd_conf(con);

         /*
	  * Return the status to the client...
	  */

          if (!cupsdSendError(con, status, CUPSD_AUTH_NONE))
	  {
	    cupsdCloseClient(con);
	    return;
	  }
	}
        break;

    case HTTP_STATE_POST_RECV :
        do
	{
          if (con->request && con->file < 0)
	  {
	   /*
	    * Grab any request data from the connection...
	    */

	    if ((ipp_state = ippRead(con->http, con->request)) == IPP_STATE_ERROR)
	    {
              cupsdLogClient(con, CUPSD_LOG_ERROR, "IPP read error: %s",
                             cupsLastErrorString());

	      cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
	      cupsdCloseClient(con);
	      return;
	    }
	    else if (ipp_state != IPP_STATE_DATA)
	    {
              if (httpGetState(con->http) == HTTP_STATE_POST_SEND)
	      {
		cupsdSendError(con, HTTP_STATUS_BAD_REQUEST, CUPSD_AUTH_NONE);
		cupsdCloseClient(con);
		return;
	      }

	      if (httpGetReady(con->http))
	        continue;
	      break;
            }
	    else
	    {
	      cupsdLogClient(con, CUPSD_LOG_DEBUG, "%d.%d %s %d",
			      con->request->request.op.version[0],
			      con->request->request.op.version[1],
			      ippOpString(con->request->request.op.operation_id),
			      con->request->request.op.request_id);
	      con->bytes += ippLength(con->request);
	    }
	  }

          if (con->file < 0 && httpGetState(con->http) != HTTP_STATE_POST_SEND)
	  {
           /*
	    * Create a file as needed for the request data...
	    */

            cupsdSetStringf(&con->filename, "%s/%08x", RequestRoot,
	                    request_id ++);
	    con->file = open(con->filename, O_WRONLY | O_CREAT | O_TRUNC, 0640);

	    if (con->file < 0)
	    {
	      cupsdLogClient(con, CUPSD_LOG_ERROR,
	                     "Unable to create request file \"%s\": %s",
                             con->filename, strerror(errno));

	      if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }
	    }

	    fchmod(con->file, 0640);
	    fchown(con->file, RunUser, Group);
            fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);
	  }

	  if (httpGetState(con->http) != HTTP_STATE_POST_SEND)
	  {
	    if (!httpWait(con->http, 0))
	      return;
            else if ((bytes = httpRead2(con->http, line, sizeof(line))) < 0)
	    {
	      if (httpError(con->http) && httpError(con->http) != EPIPE)
		cupsdLogClient(con, CUPSD_LOG_DEBUG,
			       "HTTP_STATE_POST_SEND Closing for error %d (%s)",
                               httpError(con->http), strerror(httpError(con->http)));
	      else
		cupsdLogClient(con, CUPSD_LOG_DEBUG,
			       "HTTP_STATE_POST_SEND Closing on EOF.");

	      cupsdCloseClient(con);
	      return;
	    }
	    else if (bytes > 0)
	    {
	      con->bytes += bytes;

              if (write(con->file, line, bytes) < bytes)
	      {
        	cupsdLogClient(con, CUPSD_LOG_ERROR,
	                       "Unable to write %d bytes to \"%s\": %s",
                               bytes, con->filename, strerror(errno));

		close(con->file);
		con->file = -1;
		unlink(con->filename);
		cupsdClearString(&con->filename);

        	if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE,
		                    CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}
	      }
	    }
	    else if (httpGetState(con->http) == HTTP_STATE_POST_RECV)
              return;
	    else if (httpGetState(con->http) != HTTP_STATE_POST_SEND)
	    {
	      cupsdLogClient(con, CUPSD_LOG_DEBUG,
	                     "Closing on unexpected state %s.",
			     httpStateString(httpGetState(con->http)));
	      cupsdCloseClient(con);
	      return;
	    }
	  }
        }
	while (httpGetState(con->http) == HTTP_STATE_POST_RECV && httpGetReady(con->http));

	if (httpGetState(con->http) == HTTP_STATE_POST_SEND)
	{
	  if (con->file >= 0)
	  {
	    fstat(con->file, &filestats);

	    close(con->file);
	    con->file = -1;

            if (filestats.st_size > MaxRequestSize &&
	        MaxRequestSize > 0)
	    {
	     /*
	      * Request is too big; remove it and send an error...
	      */

	      unlink(con->filename);
	      cupsdClearString(&con->filename);

	      if (con->request)
	      {
	       /*
	        * Delete any IPP request data...
		*/

	        ippDelete(con->request);
		con->request = NULL;
              }

              if (!cupsdSendError(con, HTTP_STATUS_REQUEST_TOO_LARGE, CUPSD_AUTH_NONE))
	      {
		cupsdCloseClient(con);
		return;
	      }
	    }
	    else if (filestats.st_size == 0)
	    {
	     /*
	      * Don't allow empty file...
	      */

	      unlink(con->filename);
	      cupsdClearString(&con->filename);
	    }

	    if (con->command)
	    {
	      if (!cupsdSendCommand(con, con->command, con->options, 0))
	      {
		if (!cupsdSendError(con, HTTP_STATUS_NOT_FOUND, CUPSD_AUTH_NONE))
		{
		  cupsdCloseClient(con);
		  return;
		}
              }
	      else
        	cupsdLogRequest(con, HTTP_STATUS_OK);
            }
	  }

          if (con->request)
	  {
	    cupsdProcessIPPRequest(con);

	    if (con->filename)
	    {
	      unlink(con->filename);
	      cupsdClearString(&con->filename);
	    }

	    return;
	  }
	}
        break;

    default :
        break; /* Anti-compiler-warning-code */
  }

  if (httpGetState(con->http) == HTTP_STATE_WAITING)
  {
    if (!httpGetKeepAlive(con->http))
    {
      cupsdLogClient(con, CUPSD_LOG_DEBUG,
                     "Closing because Keep-Alive is disabled.");
      cupsdCloseClient(con);
    }
    else
    {
      cupsArrayRemove(ActiveClients, con);
      cupsdSetBusyState();
    }
  }
}


/*
 * 'cupsdSendCommand()' - Send output from a command via HTTP.
 */

int					/* O - 1 on success, 0 on failure */
cupsdSendCommand(
    cupsd_client_t *con,		/* I - Client connection */
    char           *command,		/* I - Command to run */
    char           *options,		/* I - Command-line options */
    int            root)		/* I - Run as root? */
{
  int	fd;				/* Standard input file descriptor */


  if (con->filename)
  {
    fd = open(con->filename, O_RDONLY);

    if (fd < 0)
    {
      cupsdLogClient(con, CUPSD_LOG_ERROR,
                     "Unable to open \"%s\" for reading: %s",
                     con->filename ? con->filename : "/dev/null",
	             strerror(errno));
      return (0);
    }

    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }
  else
    fd = -1;

  con->pipe_pid = pipe_command(con, fd, &(con->file), command, options, root);

  if (fd >= 0)
    close(fd);

  cupsdLogClient(con, CUPSD_LOG_INFO, "Started \"%s\" (pid=%d, file=%d)",
                 command, con->pipe_pid, con->file);

  if (con->pipe_pid == 0)
    return (0);

  fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);

  cupsdAddSelect(con->file, (cupsd_selfunc_t)write_pipe, NULL, con);

  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Waiting for CGI data.");

  con->sent_header = 0;
  con->file_ready  = 0;
  con->got_fields  = 0;
  con->header_used = 0;

  return (1);
}


/*
 * 'cupsdSendError()' - Send an error message via HTTP.
 */

int					/* O - 1 if successful, 0 otherwise */
cupsdSendError(cupsd_client_t *con,	/* I - Connection */
               http_status_t  code,	/* I - Error code */
	       int            auth_type)/* I - Authentication type */
{
  cupsdLogClient(con, CUPSD_LOG_DEBUG2, "cupsdSendError code=%d, auth_type=%d",
		 code, auth_type);

#ifdef HAVE_SSL
 /*
  * Force client to upgrade for authentication if that is how the
  * server is configured...
  */

  if (code == HTTP_STATUS_UNAUTHORIZED &&
      DefaultEncryption == HTTP_ENCRYPTION_REQUIRED &&
      _cups_strcasecmp(httpGetHostname(con->http, NULL, 0), "localhost") &&
      !httpIsEncrypted(con->http))
  {
    code = HTTP_STATUS_UPGRADE_REQUIRED;
  }
#endif /* HAVE_SSL */

 /*
  * Put the request in the access_log file...
  */

  cupsdLogRequest(con, code);

 /*
  * To work around bugs in some proxies, don't use Keep-Alive for some
  * error messages...
  *
  * Kerberos authentication doesn't work without Keep-Alive, so
  * never disable it in that case.
  */

  if (code >= HTTP_STATUS_BAD_REQUEST && con->type != CUPSD_AUTH_NEGOTIATE)
    httpSetKeepAlive(con->http, HTTP_KEEPALIVE_OFF);

 /*
  * Send an error message back to the client.  If the error code is a
  * 400 or 500 series, make sure the message contains some text, too!
  */

  if (!cupsdSendHeader(con, code, NULL, auth_type))
    return (0);

#ifdef HAVE_SSL
  if (code == HTTP_STATUS_UPGRADE_REQUIRED)
    if (httpPrintf(con->http, "Connection: Upgrade\r\n") < 0)
      return (0);

  if (httpPrintf(con->http, "Upgrade: TLS/1.2,TLS/1.1,TLS/1.0\r\n") < 0)
    return (0);
#endif /* HAVE_SSL */

  if (con->http->version >= HTTP_VERSION_1_1 &&
      httpGetKeepAlive(con->http) == HTTP_KEEPALIVE_OFF)
  {
    if (httpPrintf(con->http, "Connection: close\r\n") < 0)
      return (0);
  }

  if (code >= HTTP_STATUS_BAD_REQUEST)
  {
   /*
    * Send a human-readable error message.
    */

    char	message[4096],		/* Message for user */
		urltext[1024],		/* URL redirection text */
		redirect[1024];		/* Redirection link */
    const char	*text;			/* Status-specific text */


    redirect[0] = '\0';

    if (code == HTTP_STATUS_UNAUTHORIZED)
      text = _cupsLangString(con->language,
                             _("Enter your username and password or the "
			       "root username and password to access this "
			       "page. If you are using Kerberos authentication, "
			       "make sure you have a valid Kerberos ticket."));
    else if (code == HTTP_STATUS_UPGRADE_REQUIRED)
    {
      text = urltext;

      snprintf(urltext, sizeof(urltext),
               _cupsLangString(con->language,
                               _("You must access this page using the URL "
			         "<A HREF=\"https://%s:%d%s\">"
				 "https://%s:%d%s</A>.")),
               con->servername, con->serverport, con->uri,
	       con->servername, con->serverport, con->uri);

      snprintf(redirect, sizeof(redirect),
               "<META HTTP-EQUIV=\"Refresh\" "
	       "CONTENT=\"3;URL=https://%s:%d%s\">\n",
	       con->servername, con->serverport, con->uri);
    }
    else if (code == HTTP_STATUS_CUPS_WEBIF_DISABLED)
      text = _cupsLangString(con->language,
                             _("The web interface is currently disabled. Run "
			       "\"cupsctl WebInterface=yes\" to enable it."));
    else
      text = "";

    snprintf(message, sizeof(message),
             "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" "
	     "\"http://www.w3.org/TR/html4/loose.dtd\">\n"
	     "<HTML>\n"
	     "<HEAD>\n"
             "\t<META HTTP-EQUIV=\"Content-Type\" "
	     "CONTENT=\"text/html; charset=utf-8\">\n"
	     "\t<TITLE>%s - " CUPS_SVERSION "</TITLE>\n"
	     "\t<LINK REL=\"STYLESHEET\" TYPE=\"text/css\" "
	     "HREF=\"/cups.css\">\n"
	     "%s"
	     "</HEAD>\n"
             "<BODY>\n"
	     "<H1>%s</H1>\n"
	     "<P>%s</P>\n"
	     "</BODY>\n"
	     "</HTML>\n",
	     _httpStatus(con->language, code), redirect,
	     _httpStatus(con->language, code), text);

    if (httpPrintf(con->http, "Content-Type: text/html; charset=utf-8\r\n") < 0)
      return (0);
    if (httpPrintf(con->http, "Content-Length: %d\r\n",
                   (int)strlen(message)) < 0)
      return (0);
    if (httpPrintf(con->http, "\r\n") < 0)
      return (0);
    if (httpPrintf(con->http, "%s", message) < 0)
      return (0);
  }
  else if (httpPrintf(con->http, "\r\n") < 0)
    return (0);

  if (cupsdFlushHeader(con) < 0)
    return (0);

  con->http->state = HTTP_STATE_WAITING;

  DEBUG_puts("cupsdSendError: Set state to HTTP_STATE_WAITING.");

  return (1);
}


/*
 * 'cupsdSendHeader()' - Send an HTTP request.
 */

int					/* O - 1 on success, 0 on failure */
cupsdSendHeader(
    cupsd_client_t *con,		/* I - Client to send to */
    http_status_t  code,		/* I - HTTP status code */
    char           *type,		/* I - MIME type of document */
    int            auth_type)		/* I - Type of authentication */
{
  char		auth_str[1024];		/* Authorization string */


 /*
  * Send the HTTP status header...
  */

  if (code == HTTP_STATUS_CONTINUE)
  {
   /*
    * 100-continue doesn't send any headers...
    */

    return (httpPrintf(con->http, "HTTP/%d.%d 100 Continue\r\n\r\n",
		       con->http->version / 100, con->http->version % 100) > 0);
  }
  else if (code == HTTP_STATUS_CUPS_WEBIF_DISABLED)
  {
   /*
    * Treat our special "web interface is disabled" status as "200 OK" for web
    * browsers.
    */

    code = HTTP_STATUS_OK;
  }

  httpFlushWrite(con->http);

  con->http->data_encoding = HTTP_ENCODING_FIELDS;

  if (httpPrintf(con->http, "HTTP/%d.%d %d %s\r\n", con->http->version / 100,
                 con->http->version % 100, code, httpStatus(code)) < 0)
    return (0);
  if (httpPrintf(con->http, "Date: %s\r\n", httpGetDateString(time(NULL))) < 0)
    return (0);
  if (ServerHeader)
    if (httpPrintf(con->http, "Server: %s\r\n", ServerHeader) < 0)
      return (0);
  if (httpGetKeepAlive(con->http) && con->http->version >= HTTP_VERSION_1_0)
  {
    if (httpPrintf(con->http, "Connection: Keep-Alive\r\n") < 0)
      return (0);
    if (httpPrintf(con->http, "Keep-Alive: timeout=%d\r\n",
                   KeepAliveTimeout) < 0)
      return (0);
  }
  else if (httpPrintf(con->http, "Connection: close\r\n") < 0)
    return (0);

  if (code == HTTP_STATUS_METHOD_NOT_ALLOWED)
    if (httpPrintf(con->http, "Allow: GET, HEAD, OPTIONS, POST, PUT\r\n") < 0)
      return (0);

  if (code == HTTP_STATUS_UNAUTHORIZED)
  {
    if (auth_type == CUPSD_AUTH_NONE)
    {
      if (!con->best || con->best->type <= CUPSD_AUTH_NONE)
	auth_type = cupsdDefaultAuthType();
      else
	auth_type = con->best->type;
    }

    auth_str[0] = '\0';

    if (auth_type == CUPSD_AUTH_BASIC || auth_type == CUPSD_AUTH_BASICDIGEST)
      strlcpy(auth_str, "Basic realm=\"CUPS\"", sizeof(auth_str));
    else if (auth_type == CUPSD_AUTH_DIGEST)
      snprintf(auth_str, sizeof(auth_str), "Digest realm=\"CUPS\", nonce=\"%s\"",
	       httpGetHostname(con->http, NULL, 0));
#ifdef HAVE_GSSAPI
    else if (auth_type == CUPSD_AUTH_NEGOTIATE)
    {
#  ifdef AF_LOCAL
      if (httpAddrFamily(httpGetAddress(con->http)) == AF_LOCAL)
        strlcpy(auth_str, "Basic realm=\"CUPS\"", sizeof(auth_str));
      else
#  endif /* AF_LOCAL */
      strlcpy(auth_str, "Negotiate", sizeof(auth_str));
    }
#endif /* HAVE_GSSAPI */

    if (con->best && auth_type != CUPSD_AUTH_NEGOTIATE &&
        !_cups_strcasecmp(httpGetHostname(con->http, NULL, 0), "localhost"))
    {
     /*
      * Add a "trc" (try root certification) parameter for local non-Kerberos
      * requests when the request requires system group membership - then the
      * client knows the root certificate can/should be used.
      *
      * Also, for OS X we also look for @AUTHKEY and add an "authkey"
      * parameter as needed...
      */

      char	*name,			/* Current user name */
		*auth_key;		/* Auth key buffer */
      size_t	auth_size;		/* Size of remaining buffer */

      auth_key  = auth_str + strlen(auth_str);
      auth_size = sizeof(auth_str) - (auth_key - auth_str);

      for (name = (char *)cupsArrayFirst(con->best->names);
           name;
	   name = (char *)cupsArrayNext(con->best->names))
      {
#ifdef HAVE_AUTHORIZATION_H
	if (!_cups_strncasecmp(name, "@AUTHKEY(", 9))
	{
	  snprintf(auth_key, auth_size, ", authkey=\"%s\"", name + 9);
	  /* end parenthesis is stripped in conf.c */
	  break;
        }
	else
#endif /* HAVE_AUTHORIZATION_H */
	if (!_cups_strcasecmp(name, "@SYSTEM"))
	{
#ifdef HAVE_AUTHORIZATION_H
	  if (SystemGroupAuthKey)
	    snprintf(auth_key, auth_size,
	             ", authkey=\"%s\"",
		     SystemGroupAuthKey);
          else
#else
	  strlcpy(auth_key, ", trc=\"y\"", auth_size);
#endif /* HAVE_AUTHORIZATION_H */
	  break;
	}
      }
    }

    if (auth_str[0])
    {
      cupsdLogClient(con, CUPSD_LOG_DEBUG, "WWW-Authenticate: %s", auth_str);

      if (httpPrintf(con->http, "WWW-Authenticate: %s\r\n", auth_str) < 0)
        return (0);
    }
  }

  if (con->language && strcmp(con->language->language, "C"))
  {
    if (httpPrintf(con->http, "Content-Language: %s\r\n",
                   con->language->language) < 0)
      return (0);
  }

  if (type)
  {
    if (!strcmp(type, "text/html"))
    {
      if (httpPrintf(con->http,
                     "Content-Type: text/html; charset=utf-8\r\n") < 0)
        return (0);
    }
    else if (httpPrintf(con->http, "Content-Type: %s\r\n", type) < 0)
      return (0);
  }

  return (1);
}


/*
 * 'cupsdUpdateCGI()' - Read status messages from CGI scripts and programs.
 */

void
cupsdUpdateCGI(void)
{
  char		*ptr,			/* Pointer to end of line in buffer */
		message[1024];		/* Pointer to message text */
  int		loglevel;		/* Log level for message */


  while ((ptr = cupsdStatBufUpdate(CGIStatusBuffer, &loglevel,
                                   message, sizeof(message))) != NULL)
  {
    if (loglevel == CUPSD_LOG_INFO)
      cupsdLogMessage(CUPSD_LOG_INFO, "%s", message);

    if (!strchr(CGIStatusBuffer->buffer, '\n'))
      break;
  }

  if (ptr == NULL && !CGIStatusBuffer->bufused)
  {
   /*
    * Fatal error on pipe - should never happen!
    */

    cupsdLogMessage(CUPSD_LOG_CRIT,
                    "cupsdUpdateCGI: error reading from CGI error pipe - %s",
                    strerror(errno));
  }
}


/*
 * 'cupsdWriteClient()' - Write data to a client as needed.
 */

void
cupsdWriteClient(cupsd_client_t *con)	/* I - Client connection */
{
  int		bytes,			/* Number of bytes written */
		field_col;		/* Current column */
  char		*bufptr,		/* Pointer into buffer */
		*bufend;		/* Pointer to end of buffer */
  ipp_state_t	ipp_state;		/* IPP state value */


  cupsdLogClient(con, CUPSD_LOG_DEBUG, "con->http=%p", con->http);
  cupsdLogClient(con, CUPSD_LOG_DEBUG,
		 "cupsdWriteClient "
		 "error=%d, "
		 "used=%d, "
		 "state=%s, "
		 "data_encoding=HTTP_ENCODING_%s, "
		 "data_remaining=" CUPS_LLFMT ", "
		 "response=%p(%s), "
		 "pipe_pid=%d, "
		 "file=%d",
		 httpError(con->http), (int)httpGetReady(con->http),
		 httpStateString(httpGetState(con->http)),
		 con->http->data_encoding == HTTP_ENCODING_CHUNKED ?
		     "CHUNKED" : "LENGTH",
		 CUPS_LLCAST con->http->data_remaining,
		 con->response,
		 con->response ? ipp_states[con->response->state] : "",
		 con->pipe_pid, con->file);

  if (httpGetState(con->http) != HTTP_STATE_GET_SEND &&
      httpGetState(con->http) != HTTP_STATE_POST_SEND)
  {
   /*
    * If we get called in the wrong state, then something went wrong with the
    * connection and we need to shut it down...
    */

    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Closing on unexpected HTTP state %s.",
		   httpStateString(httpGetState(con->http)));
    cupsdCloseClient(con);
    return;
  }

  if (con->pipe_pid)
  {
   /*
    * Make sure we select on the CGI output...
    */

    cupsdAddSelect(con->file, (cupsd_selfunc_t)write_pipe, NULL, con);

    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Waiting for CGI data.");

    if (!con->file_ready)
    {
     /*
      * Try again later when there is CGI output available...
      */

      cupsdRemoveSelect(httpGetFd(con->http));
      return;
    }

    con->file_ready = 0;
  }

  if (con->response && con->response->state != IPP_STATE_DATA)
  {
    int wused = con->http->wused;	/* Previous write buffer use */

    do
    {
     /*
      * Write a single attribute or the IPP message header...
      */

      ipp_state = ippWrite(con->http, con->response);

     /*
      * If the write buffer has been flushed, stop buffering up attributes...
      */

      if (con->http->wused <= wused)
        break;
    }
    while (ipp_state != IPP_STATE_DATA && ipp_state != IPP_STATE_ERROR);

    cupsdLogClient(con, CUPSD_LOG_DEBUG,
                   "Writing IPP response, ipp_state=%s, old "
                   "wused=%d, new wused=%d",
                   ipp_state == IPP_STATE_ERROR ? "ERROR" :
                       ipp_state == IPP_STATE_IDLE ? "IDLE" :
                       ipp_state == IPP_STATE_HEADER ? "HEADER" :
                       ipp_state == IPP_STATE_ATTRIBUTE ? "ATTRIBUTE" : "DATA",
		   wused, con->http->wused);

    if (con->http->wused > 0)
      httpFlushWrite(con->http);

    bytes = ipp_state != IPP_STATE_ERROR &&
	    (con->file >= 0 || ipp_state != IPP_STATE_DATA);

    cupsdLogClient(con, CUPSD_LOG_DEBUG,
                   "bytes=%d, http_state=%d, data_remaining=" CUPS_LLFMT,
                   (int)bytes, httpGetState(con->http),
                   CUPS_LLCAST con->http->data_remaining);
  }
  else if ((bytes = read(con->file, con->header + con->header_used,
			 sizeof(con->header) - con->header_used)) > 0)
  {
    con->header_used += bytes;

    if (con->pipe_pid && !con->got_fields)
    {
     /*
      * Inspect the data for Content-Type and other fields.
      */

      for (bufptr = con->header, bufend = con->header + con->header_used,
               field_col = 0;
           !con->got_fields && bufptr < bufend;
	   bufptr ++)
      {
        if (*bufptr == '\n')
	{
	 /*
	  * Send line to client...
	  */

	  if (bufptr > con->header && bufptr[-1] == '\r')
	    bufptr[-1] = '\0';
	  *bufptr++ = '\0';

          cupsdLogClient(con, CUPSD_LOG_DEBUG, "Script header: %s", con->header);

          if (!con->sent_header)
	  {
	   /*
	    * Handle redirection and CGI status codes...
	    */

            if (!_cups_strncasecmp(con->header, "Location:", 9))
	    {
  	      if (!cupsdSendHeader(con, HTTP_STATUS_SEE_OTHER, NULL, CUPSD_AUTH_NONE))
	      {
	        cupsdCloseClient(con);
		return;
	      }

	      con->sent_header = 2;

	      if (httpPrintf(con->http, "Content-Length: 0\r\n") < 0)
		return;
	    }
	    else if (!_cups_strncasecmp(con->header, "Status:", 7))
	    {
  	      cupsdSendError(con, (http_status_t)atoi(con->header + 7),
	                     CUPSD_AUTH_NONE);
	      con->sent_header = 2;
	    }
	    else
	    {
  	      if (!cupsdSendHeader(con, HTTP_STATUS_OK, NULL, CUPSD_AUTH_NONE))
	      {
	        cupsdCloseClient(con);
		return;
	      }

	      con->sent_header = 1;

	      if (con->http->version == HTTP_VERSION_1_1)
	      {
		if (httpPrintf(con->http, "Transfer-Encoding: chunked\r\n") < 0)
		  return;
	      }
            }
	  }

	  if (_cups_strncasecmp(con->header, "Status:", 7))
	    httpPrintf(con->http, "%s\r\n", con->header);

         /*
	  * Update buffer...
	  */

	  con->header_used -= bufptr - con->header;

	  if (con->header_used > 0)
	    memmove(con->header, bufptr, con->header_used);

	  bufptr = con->header - 1;

         /*
	  * See if the line was empty...
	  */

	  if (field_col == 0)
	  {
	    con->got_fields = 1;

            if (cupsdFlushHeader(con) < 0)
	    {
	      cupsdCloseClient(con);
	      return;
	    }

	    if (con->http->version == HTTP_VERSION_1_1)
	      con->http->data_encoding = HTTP_ENCODING_CHUNKED;
          }
	  else
	    field_col = 0;
	}
	else if (*bufptr != '\r')
	  field_col ++;
      }

      if (!con->got_fields)
      {
        con->http->activity = time(NULL);
        return;
      }
    }

    if (con->header_used > 0)
    {
      if (httpWrite2(con->http, con->header, con->header_used) < 0)
      {
	cupsdLogClient(con, CUPSD_LOG_DEBUG, "Closing for error %d (%s)",
		       httpError(con->http), strerror(httpError(con->http)));
	cupsdCloseClient(con);
	return;
      }

      if (con->http->data_encoding == HTTP_ENCODING_CHUNKED)
        httpFlushWrite(con->http);

      con->bytes += con->header_used;

      if (httpGetState(con->http) == HTTP_STATE_WAITING)
	bytes = 0;
      else
        bytes = con->header_used;

      con->header_used = 0;
    }
  }

  if (bytes <= 0 ||
      (httpGetState(con->http) != HTTP_STATE_GET_SEND &&
       httpGetState(con->http) != HTTP_STATE_POST_SEND))
  {
    if (!con->sent_header && con->pipe_pid)
      cupsdSendError(con, HTTP_STATUS_SERVER_ERROR, CUPSD_AUTH_NONE);
    else
    {
      cupsdLogRequest(con, HTTP_STATUS_OK);

      httpFlushWrite(con->http);

      if (con->http->data_encoding == HTTP_ENCODING_CHUNKED &&
          con->sent_header == 1)
      {
	if (httpWrite2(con->http, "", 0) < 0)
	{
	  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Closing for error %d (%s)",
			 httpError(con->http), strerror(httpError(con->http)));
	  cupsdCloseClient(con);
	  return;
	}
      }
    }

    con->http->state = HTTP_STATE_WAITING;

    cupsdAddSelect(httpGetFd(con->http), (cupsd_selfunc_t)cupsdReadClient, NULL, con);

    cupsdLogClient(con, CUPSD_LOG_DEBUG, "Waiting for request.");

    if (con->file >= 0)
    {
      cupsdRemoveSelect(con->file);

      if (con->pipe_pid)
	cupsdEndProcess(con->pipe_pid, 0);

      close(con->file);
      con->file     = -1;
      con->pipe_pid = 0;
    }

    if (con->filename)
    {
      unlink(con->filename);
      cupsdClearString(&con->filename);
    }

    if (con->request)
    {
      ippDelete(con->request);
      con->request = NULL;
    }

    if (con->response)
    {
      ippDelete(con->response);
      con->response = NULL;
    }

    cupsdClearString(&con->command);
    cupsdClearString(&con->options);
    cupsdClearString(&con->query_string);

    if (!httpGetKeepAlive(con->http))
    {
      cupsdLogClient(con, CUPSD_LOG_DEBUG,
		     "Closing because Keep-Alive is disabled.");
      cupsdCloseClient(con);
      return;
    }
    else
    {
      cupsArrayRemove(ActiveClients, con);
      cupsdSetBusyState();
    }
  }

  con->http->activity = time(NULL);
}


/*
 * 'check_if_modified()' - Decode an "If-Modified-Since" line.
 */

static int				/* O - 1 if modified since */
check_if_modified(
    cupsd_client_t *con,		/* I - Client connection */
    struct stat    *filestats)		/* I - File information */
{
  const char	*ptr;			/* Pointer into field */
  time_t	date;			/* Time/date value */
  off_t		size;			/* Size/length value */


  size = 0;
  date = 0;
  ptr  = httpGetField(con->http, HTTP_FIELD_IF_MODIFIED_SINCE);

  if (*ptr == '\0')
    return (1);

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
                 "check_if_modified "
		 "filestats=%p(" CUPS_LLFMT ", %d)) If-Modified-Since=\"%s\"",
                 filestats, CUPS_LLCAST filestats->st_size,
		 (int)filestats->st_mtime, ptr);

  while (*ptr != '\0')
  {
    while (isspace(*ptr) || *ptr == ';')
      ptr ++;

    if (_cups_strncasecmp(ptr, "length=", 7) == 0)
    {
      ptr += 7;
      size = strtoll(ptr, NULL, 10);

      while (isdigit(*ptr))
        ptr ++;
    }
    else if (isalpha(*ptr))
    {
      date = httpGetDateTime(ptr);
      while (*ptr != '\0' && *ptr != ';')
        ptr ++;
    }
    else
      ptr ++;
  }

  return ((size != filestats->st_size && size != 0) ||
          (date < filestats->st_mtime && date != 0) ||
	  (size == 0 && date == 0));
}


/*
 * 'compare_clients()' - Compare two client connections.
 */

static int				/* O - Result of comparison */
compare_clients(cupsd_client_t *a,	/* I - First client */
                cupsd_client_t *b,	/* I - Second client */
                void           *data)	/* I - User data (not used) */
{
  (void)data;

  if (a == b)
    return (0);
  else if (a < b)
    return (-1);
  else
    return (1);
}


/*
 * 'get_file()' - Get a filename and state info.
 */

static char *				/* O  - Real filename */
get_file(cupsd_client_t *con,		/* I  - Client connection */
         struct stat    *filestats,	/* O  - File information */
         char           *filename,	/* IO - Filename buffer */
         int            len)		/* I  - Buffer length */
{
  int		status;			/* Status of filesystem calls */
  char		*ptr;			/* Pointer info filename */
  int		plen;			/* Remaining length after pointer */
  char		language[7];		/* Language subdirectory, if any */


 /*
  * Figure out the real filename...
  */

  language[0] = '\0';

  if (!strncmp(con->uri, "/ppd/", 5) && !strchr(con->uri + 5, '/'))
    snprintf(filename, len, "%s%s", ServerRoot, con->uri);
  else if (!strncmp(con->uri, "/icons/", 7) && !strchr(con->uri + 7, '/'))
  {
    snprintf(filename, len, "%s/%s", CacheDir, con->uri + 7);
    if (access(filename, F_OK) < 0)
      snprintf(filename, len, "%s/images/generic.png", DocumentRoot);
  }
  else if (!strncmp(con->uri, "/rss/", 5) && !strchr(con->uri + 5, '/'))
    snprintf(filename, len, "%s/rss/%s", CacheDir, con->uri + 5);
  else if (!strncmp(con->uri, "/admin/conf/", 12))
    snprintf(filename, len, "%s%s", ServerRoot, con->uri + 11);
  else if (!strncmp(con->uri, "/admin/log/", 11))
  {
    if (!strncmp(con->uri + 11, "access_log", 10) && AccessLog[0] == '/')
      strlcpy(filename, AccessLog, len);
    else if (!strncmp(con->uri + 11, "error_log", 9) && ErrorLog[0] == '/')
      strlcpy(filename, ErrorLog, len);
    else if (!strncmp(con->uri + 11, "page_log", 8) && PageLog[0] == '/')
      strlcpy(filename, PageLog, len);
    else
      return (NULL);
  }
  else if (con->language)
  {
    snprintf(language, sizeof(language), "/%s", con->language->language);
    snprintf(filename, len, "%s%s%s", DocumentRoot, language, con->uri);
  }
  else
    snprintf(filename, len, "%s%s", DocumentRoot, con->uri);

  if ((ptr = strchr(filename, '?')) != NULL)
    *ptr = '\0';

 /*
  * Grab the status for this language; if there isn't a language-specific file
  * then fallback to the default one...
  */

  if ((status = stat(filename, filestats)) != 0 && language[0] &&
      strncmp(con->uri, "/icons/", 7) &&
      strncmp(con->uri, "/ppd/", 5) &&
      strncmp(con->uri, "/rss/", 5) &&
      strncmp(con->uri, "/admin/conf/", 12) &&
      strncmp(con->uri, "/admin/log/", 11))
  {
   /*
    * Drop the country code...
    */

    language[3] = '\0';
    snprintf(filename, len, "%s%s%s", DocumentRoot, language, con->uri);

    if ((ptr = strchr(filename, '?')) != NULL)
      *ptr = '\0';

    if ((status = stat(filename, filestats)) != 0)
    {
     /*
      * Drop the language prefix and try the root directory...
      */

      language[0] = '\0';
      snprintf(filename, len, "%s%s", DocumentRoot, con->uri);

      if ((ptr = strchr(filename, '?')) != NULL)
	*ptr = '\0';

      status = stat(filename, filestats);
    }
  }

 /*
  * If we're found a directory, get the index.html file instead...
  */

  if (!status && S_ISDIR(filestats->st_mode))
  {
   /*
    * Make sure the URI ends with a slash...
    */

    if (con->uri[strlen(con->uri) - 1] != '/')
      strlcat(con->uri, "/", sizeof(con->uri));

   /*
    * Find the directory index file, trying every language...
    */

    do
    {
      if (status && language[0])
      {
       /*
        * Try a different language subset...
	*/

	if (language[3])
	  language[0] = '\0';		/* Strip country code */
	else
	  language[0] = '\0';		/* Strip language */
      }

     /*
      * Look for the index file...
      */

      snprintf(filename, len, "%s%s%s", DocumentRoot, language, con->uri);

      if ((ptr = strchr(filename, '?')) != NULL)
	*ptr = '\0';

      ptr  = filename + strlen(filename);
      plen = len - (ptr - filename);

      strlcpy(ptr, "index.html", plen);
      status = stat(filename, filestats);

#ifdef HAVE_JAVA
      if (status)
      {
	strlcpy(ptr, "index.class", plen);
	status = stat(filename, filestats);
      }
#endif /* HAVE_JAVA */

#ifdef HAVE_PERL
      if (status)
      {
	strlcpy(ptr, "index.pl", plen);
	status = stat(filename, filestats);
      }
#endif /* HAVE_PERL */

#ifdef HAVE_PHP
      if (status)
      {
	strlcpy(ptr, "index.php", plen);
	status = stat(filename, filestats);
      }
#endif /* HAVE_PHP */

#ifdef HAVE_PYTHON
      if (status)
      {
	strlcpy(ptr, "index.pyc", plen);
	status = stat(filename, filestats);
      }

      if (status)
      {
	strlcpy(ptr, "index.py", plen);
	status = stat(filename, filestats);
      }
#endif /* HAVE_PYTHON */

    }
    while (status && language[0]);
  }

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
                 "get_file filestats=%p, filename=%p, len=%d, "
		 "returning \"%s\".", filestats, filename, len,
		 status ? "(null)" : filename);

  if (status)
    return (NULL);
  else
    return (filename);
}


/*
 * 'install_cupsd_conf()' - Install a configuration file.
 */

static http_status_t			/* O - Status */
install_cupsd_conf(cupsd_client_t *con)	/* I - Connection */
{
  char		filename[1024];		/* Configuration filename */
  cups_file_t	*in,			/* Input file */
		*out;			/* Output file */
  char		buffer[16384];		/* Copy buffer */
  ssize_t	bytes;			/* Number of bytes */


 /*
  * Open the request file...
  */

  if ((in = cupsFileOpen(con->filename, "rb")) == NULL)
  {
    cupsdLogClient(con, CUPSD_LOG_ERROR, "Unable to open request file \"%s\": %s",
                    con->filename, strerror(errno));
    return (HTTP_STATUS_SERVER_ERROR);
  }

 /*
  * Open the new config file...
  */

  if ((out = cupsdCreateConfFile(ConfigurationFile, ConfigFilePerm)) == NULL)
  {
    cupsFileClose(in);
    return (HTTP_STATUS_SERVER_ERROR);
  }

  cupsdLogClient(con, CUPSD_LOG_INFO, "Installing config file \"%s\"...",
                  ConfigurationFile);

 /*
  * Copy from the request to the new config file...
  */

  while ((bytes = cupsFileRead(in, buffer, sizeof(buffer))) > 0)
    if (cupsFileWrite(out, buffer, bytes) < bytes)
    {
      cupsdLogClient(con, CUPSD_LOG_ERROR,
                      "Unable to copy to config file \"%s\": %s",
        	      ConfigurationFile, strerror(errno));

      cupsFileClose(in);
      cupsFileClose(out);

      snprintf(filename, sizeof(filename), "%s.N", ConfigurationFile);
      cupsdUnlinkOrRemoveFile(filename);

      return (HTTP_STATUS_SERVER_ERROR);
    }

 /*
  * Close the files...
  */

  cupsFileClose(in);

  if (cupsdCloseCreatedConfFile(out, ConfigurationFile))
    return (HTTP_STATUS_SERVER_ERROR);

 /*
  * Remove the request file...
  */

  cupsdUnlinkOrRemoveFile(con->filename);
  cupsdClearString(&con->filename);

 /*
  * Set the NeedReload flag...
  */

  NeedReload = RELOAD_CUPSD;
  ReloadTime = time(NULL);

 /*
  * Return that the file was created successfully...
  */

  return (HTTP_STATUS_CREATED);
}


/*
 * 'is_cgi()' - Is the resource a CGI script/program?
 */

static int				/* O - 1 = CGI, 0 = file */
is_cgi(cupsd_client_t *con,		/* I - Client connection */
       const char     *filename,	/* I - Real filename */
       struct stat    *filestats,	/* I - File information */
       mime_type_t    *type)		/* I - MIME type */
{
  const char	*options;		/* Options on URL */


 /*
  * Get the options, if any...
  */

  if ((options = strchr(con->uri, '?')) != NULL)
  {
    options ++;
    cupsdSetStringf(&(con->query_string), "QUERY_STRING=%s", options);
  }

 /*
  * Check for known types...
  */

  if (!type || _cups_strcasecmp(type->super, "application"))
  {
    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 0", filename,
		   filestats, type ? type->super : "unknown",
		   type ? type->type : "unknown");
    return (0);
  }

  if (!_cups_strcasecmp(type->type, "x-httpd-cgi") &&
      (filestats->st_mode & 0111))
  {
   /*
    * "application/x-httpd-cgi" is a CGI script.
    */

    cupsdSetString(&con->command, filename);

    if (options)
      cupsdSetStringf(&con->options, " %s", options);

    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 1", filename,
		   filestats, type->super, type->type);
    return (1);
  }
#ifdef HAVE_JAVA
  else if (!_cups_strcasecmp(type->type, "x-httpd-java"))
  {
   /*
    * "application/x-httpd-java" is a Java servlet.
    */

    cupsdSetString(&con->command, CUPS_JAVA);

    if (options)
      cupsdSetStringf(&con->options, " %s %s", filename, options);
    else
      cupsdSetStringf(&con->options, " %s", filename);

    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 1", filename,
		   filestats, type->super, type->type);
    return (1);
  }
#endif /* HAVE_JAVA */
#ifdef HAVE_PERL
  else if (!_cups_strcasecmp(type->type, "x-httpd-perl"))
  {
   /*
    * "application/x-httpd-perl" is a Perl page.
    */

    cupsdSetString(&con->command, CUPS_PERL);

    if (options)
      cupsdSetStringf(&con->options, " %s %s", filename, options);
    else
      cupsdSetStringf(&con->options, " %s", filename);

    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 1", filename,
		   filestats, type->super, type->type);
    return (1);
  }
#endif /* HAVE_PERL */
#ifdef HAVE_PHP
  else if (!_cups_strcasecmp(type->type, "x-httpd-php"))
  {
   /*
    * "application/x-httpd-php" is a PHP page.
    */

    cupsdSetString(&con->command, CUPS_PHP);

    if (options)
      cupsdSetStringf(&con->options, " %s %s", filename, options);
    else
      cupsdSetStringf(&con->options, " %s", filename);

    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 1", filename,
		   filestats, type->super, type->type);
    return (1);
  }
#endif /* HAVE_PHP */
#ifdef HAVE_PYTHON
  else if (!_cups_strcasecmp(type->type, "x-httpd-python"))
  {
   /*
    * "application/x-httpd-python" is a Python page.
    */

    cupsdSetString(&con->command, CUPS_PYTHON);

    if (options)
      cupsdSetStringf(&con->options, " %s %s", filename, options);
    else
      cupsdSetStringf(&con->options, " %s", filename);

    cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		   "is_cgi filename=\"%s\", filestats=%p, "
		   "type=%s/%s, returning 1", filename,
		   filestats, type->super, type->type);
    return (1);
  }
#endif /* HAVE_PYTHON */

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
		 "is_cgi filename=\"%s\", filestats=%p, "
		 "type=%s/%s, returning 0", filename,
		 filestats, type->super, type->type);
  return (0);
}


/*
 * 'is_path_absolute()' - Is a path absolute and free of relative elements (i.e. "..").
 */

static int				/* O - 0 if relative, 1 if absolute */
is_path_absolute(const char *path)	/* I - Input path */
{
 /*
  * Check for a leading slash...
  */

  if (path[0] != '/')
    return (0);

 /*
  * Check for "/.." in the path...
  */

  while ((path = strstr(path, "/..")) != NULL)
  {
    if (!path[3] || path[3] == '/')
      return (0);

    path ++;
  }

 /*
  * If we haven't found any relative paths, return 1 indicating an
  * absolute path...
  */

  return (1);
}


/*
 * 'pipe_command()' - Pipe the output of a command to the remote client.
 */

static int				/* O - Process ID */
pipe_command(cupsd_client_t *con,	/* I - Client connection */
             int            infile,	/* I - Standard input for command */
             int            *outfile,	/* O - Standard output for command */
	     char           *command,	/* I - Command to run */
	     char           *options,	/* I - Options for command */
	     int            root)	/* I - Run as root? */
{
  int		i;			/* Looping var */
  int		pid;			/* Process ID */
  char		*commptr,		/* Command string pointer */
		commch;			/* Command string character */
  char		*uriptr;		/* URI string pointer */
  int		fds[2];			/* Pipe FDs */
  int		argc;			/* Number of arguments */
  int		envc;			/* Number of environment variables */
  char		argbuf[10240],		/* Argument buffer */
		*argv[100],		/* Argument strings */
		*envp[MAX_ENV + 20];	/* Environment variables */
  char		auth_type[256],		/* AUTH_TYPE environment variable */
		content_length[1024],	/* CONTENT_LENGTH environment variable */
		content_type[1024],	/* CONTENT_TYPE environment variable */
		http_cookie[32768],	/* HTTP_COOKIE environment variable */
		http_referer[1024],	/* HTTP_REFERER environment variable */
		http_user_agent[1024],	/* HTTP_USER_AGENT environment variable */
		lang[1024],		/* LANG environment variable */
		path_info[1024],	/* PATH_INFO environment variable */
		remote_addr[1024],	/* REMOTE_ADDR environment variable */
		remote_host[1024],	/* REMOTE_HOST environment variable */
		remote_user[1024],	/* REMOTE_USER environment variable */
		script_filename[1024],	/* SCRIPT_FILENAME environment variable */
		script_name[1024],	/* SCRIPT_NAME environment variable */
		server_name[1024],	/* SERVER_NAME environment variable */
		server_port[1024];	/* SERVER_PORT environment variable */
  ipp_attribute_t *attr;		/* attributes-natural-language attribute */


 /*
  * Parse a copy of the options string, which is of the form:
  *
  *     argument+argument+argument
  *     ?argument+argument+argument
  *     param=value&param=value
  *     ?param=value&param=value
  *     /name?argument+argument+argument
  *     /name?param=value&param=value
  *
  * If the string contains an "=" character after the initial name,
  * then we treat it as a HTTP GET form request and make a copy of
  * the remaining string for the environment variable.
  *
  * The string is always parsed out as command-line arguments, to
  * be consistent with Apache...
  */

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
                 "pipe_command infile=%d, outfile=%p, "
		 "command=\"%s\", options=\"%s\", root=%d",
                 infile, outfile, command,
		 options ? options : "(null)", root);

  argv[0] = command;

  if (options)
  {
    commptr = options;
    if (*commptr == ' ')
      commptr ++;
    strlcpy(argbuf, commptr, sizeof(argbuf));
  }
  else
    argbuf[0] = '\0';

  if (argbuf[0] == '/')
  {
   /*
    * Found some trailing path information, set PATH_INFO...
    */

    if ((commptr = strchr(argbuf, '?')) == NULL)
      commptr = argbuf + strlen(argbuf);

    commch   = *commptr;
    *commptr = '\0';
    snprintf(path_info, sizeof(path_info), "PATH_INFO=%s", argbuf);
    *commptr = commch;
  }
  else
  {
    commptr      = argbuf;
    path_info[0] = '\0';

    if (*commptr == ' ')
      commptr ++;
  }

  if (*commptr == '?' && con->operation == HTTP_STATE_GET && !con->query_string)
  {
    commptr ++;
    cupsdSetStringf(&(con->query_string), "QUERY_STRING=%s", commptr);
  }

  argc = 1;

  if (*commptr)
  {
    argv[argc ++] = commptr;

    for (; *commptr && argc < 99; commptr ++)
    {
     /*
      * Break arguments whenever we see a + or space...
      */

      if (*commptr == ' ' || *commptr == '+')
      {
	while (*commptr == ' ' || *commptr == '+')
	  *commptr++ = '\0';

       /*
	* If we don't have a blank string, save it as another argument...
	*/

	if (*commptr)
	{
	  argv[argc] = commptr;
	  argc ++;
	}
	else
	  break;
      }
      else if (*commptr == '%' && isxdigit(commptr[1] & 255) &&
               isxdigit(commptr[2] & 255))
      {
       /*
	* Convert the %xx notation to the individual character.
	*/

	if (commptr[1] >= '0' && commptr[1] <= '9')
          *commptr = (commptr[1] - '0') << 4;
	else
          *commptr = (tolower(commptr[1]) - 'a' + 10) << 4;

	if (commptr[2] >= '0' && commptr[2] <= '9')
          *commptr |= commptr[2] - '0';
	else
          *commptr |= tolower(commptr[2]) - 'a' + 10;

	_cups_strcpy(commptr + 1, commptr + 3);

       /*
	* Check for a %00 and break if that is the case...
	*/

	if (!*commptr)
          break;
      }
    }
  }

  argv[argc] = NULL;

 /*
  * Setup the environment variables as needed...
  */

  if (con->username[0])
  {
    snprintf(auth_type, sizeof(auth_type), "AUTH_TYPE=%s",
             httpGetField(con->http, HTTP_FIELD_AUTHORIZATION));

    if ((uriptr = strchr(auth_type + 10, ' ')) != NULL)
      *uriptr = '\0';
  }
  else
    auth_type[0] = '\0';

  if (con->request &&
      (attr = ippFindAttribute(con->request, "attributes-natural-language",
                               IPP_TAG_LANGUAGE)) != NULL)
  {
    switch (strlen(attr->values[0].string.text))
    {
      default :
	 /*
	  * This is an unknown or badly formatted language code; use
	  * the POSIX locale...
	  */

	  strlcpy(lang, "LANG=C", sizeof(lang));
	  break;

      case 2 :
	 /*
	  * Just the language code (ll)...
	  */

	  snprintf(lang, sizeof(lang), "LANG=%s.UTF8",
		   attr->values[0].string.text);
	  break;

      case 5 :
	 /*
	  * Language and country code (ll-cc)...
	  */

	  snprintf(lang, sizeof(lang), "LANG=%c%c_%c%c.UTF8",
		   attr->values[0].string.text[0],
		   attr->values[0].string.text[1],
		   toupper(attr->values[0].string.text[3] & 255),
		   toupper(attr->values[0].string.text[4] & 255));
	  break;
    }
  }
  else if (con->language)
    snprintf(lang, sizeof(lang), "LANG=%s.UTF8", con->language->language);
  else
    strlcpy(lang, "LANG=C", sizeof(lang));

  strlcpy(remote_addr, "REMOTE_ADDR=", sizeof(remote_addr));
  httpAddrString(httpGetAddress(con->http), remote_addr + 12,
                 sizeof(remote_addr) - 12);

  snprintf(remote_host, sizeof(remote_host), "REMOTE_HOST=%s",
           httpGetHostname(con->http, NULL, 0));

  snprintf(script_name, sizeof(script_name), "SCRIPT_NAME=%s", con->uri);
  if ((uriptr = strchr(script_name, '?')) != NULL)
    *uriptr = '\0';

  snprintf(script_filename, sizeof(script_filename), "SCRIPT_FILENAME=%s%s",
           DocumentRoot, script_name + 12);

  sprintf(server_port, "SERVER_PORT=%d", con->serverport);

  if (httpGetField(con->http, HTTP_FIELD_HOST)[0])
  {
    char *nameptr;			/* Pointer to ":port" */

    snprintf(server_name, sizeof(server_name), "SERVER_NAME=%s",
	     httpGetField(con->http, HTTP_FIELD_HOST));
    if ((nameptr = strrchr(server_name, ':')) != NULL && !strchr(nameptr, ']'))
      *nameptr = '\0';			/* Strip trailing ":port" */
  }
  else
    snprintf(server_name, sizeof(server_name), "SERVER_NAME=%s",
	     con->servername);

  envc = cupsdLoadEnv(envp, (int)(sizeof(envp) / sizeof(envp[0])));

  if (auth_type[0])
    envp[envc ++] = auth_type;

  envp[envc ++] = lang;
  envp[envc ++] = "REDIRECT_STATUS=1";
  envp[envc ++] = "GATEWAY_INTERFACE=CGI/1.1";
  envp[envc ++] = server_name;
  envp[envc ++] = server_port;
  envp[envc ++] = remote_addr;
  envp[envc ++] = remote_host;
  envp[envc ++] = script_name;
  envp[envc ++] = script_filename;

  if (path_info[0])
    envp[envc ++] = path_info;

  if (con->username[0])
  {
    snprintf(remote_user, sizeof(remote_user), "REMOTE_USER=%s", con->username);

    envp[envc ++] = remote_user;
  }

  if (con->http->version == HTTP_VERSION_1_1)
    envp[envc ++] = "SERVER_PROTOCOL=HTTP/1.1";
  else if (con->http->version == HTTP_VERSION_1_0)
    envp[envc ++] = "SERVER_PROTOCOL=HTTP/1.0";
  else
    envp[envc ++] = "SERVER_PROTOCOL=HTTP/0.9";

  if (con->http->cookie)
  {
    snprintf(http_cookie, sizeof(http_cookie), "HTTP_COOKIE=%s",
             con->http->cookie);
    envp[envc ++] = http_cookie;
  }

  if (httpGetField(con->http, HTTP_FIELD_USER_AGENT)[0])
  {
    snprintf(http_user_agent, sizeof(http_user_agent), "HTTP_USER_AGENT=%s",
             httpGetField(con->http, HTTP_FIELD_USER_AGENT));
    envp[envc ++] = http_user_agent;
  }

  if (httpGetField(con->http, HTTP_FIELD_REFERER)[0])
  {
    snprintf(http_referer, sizeof(http_referer), "HTTP_REFERER=%s",
             httpGetField(con->http, HTTP_FIELD_REFERER));
    envp[envc ++] = http_referer;
  }

  if (con->operation == HTTP_STATE_GET)
  {
    envp[envc ++] = "REQUEST_METHOD=GET";

    if (con->query_string)
    {
     /*
      * Add GET form variables after ?...
      */

      envp[envc ++] = con->query_string;
    }
    else
      envp[envc ++] = "QUERY_STRING=";
  }
  else
  {
    sprintf(content_length, "CONTENT_LENGTH=" CUPS_LLFMT,
            CUPS_LLCAST con->bytes);
    snprintf(content_type, sizeof(content_type), "CONTENT_TYPE=%s",
             httpGetField(con->http, HTTP_FIELD_CONTENT_TYPE));

    envp[envc ++] = "REQUEST_METHOD=POST";
    envp[envc ++] = content_length;
    envp[envc ++] = content_type;
  }

 /*
  * Tell the CGI if we are using encryption...
  */

  if (con->http->tls)
    envp[envc ++] = "HTTPS=ON";

 /*
  * Terminate the environment array...
  */

  envp[envc] = NULL;

  if (LogLevel >= CUPSD_LOG_DEBUG)
  {
    for (i = 0; i < argc; i ++)
      cupsdLogMessage(CUPSD_LOG_DEBUG,
                      "[CGI] argv[%d] = \"%s\"", i, argv[i]);
    for (i = 0; i < envc; i ++)
      cupsdLogMessage(CUPSD_LOG_DEBUG,
                      "[CGI] envp[%d] = \"%s\"", i, envp[i]);
  }

 /*
  * Create a pipe for the output...
  */

  if (cupsdOpenPipe(fds))
  {
    cupsdLogMessage(CUPSD_LOG_ERROR, "[CGI] Unable to create pipe for %s - %s",
                    argv[0], strerror(errno));
    return (0);
  }

 /*
  * Then execute the command...
  */

  if (cupsdStartProcess(command, argv, envp, infile, fds[1], CGIPipes[1],
			-1, -1, root, DefaultProfile, NULL, &pid) < 0)
  {
   /*
    * Error - can't fork!
    */

    cupsdLogMessage(CUPSD_LOG_ERROR, "[CGI] Unable to start %s - %s", argv[0],
                    strerror(errno));

    cupsdClosePipe(fds);
    pid = 0;
  }
  else
  {
   /*
    * Fork successful - return the PID...
    */

    if (con->username[0])
      cupsdAddCert(pid, con->username, con->type);

    cupsdLogMessage(CUPSD_LOG_DEBUG, "[CGI] Started %s (PID %d)", command, pid);

    *outfile = fds[0];
    close(fds[1]);
  }

  return (pid);
}


/*
 * 'valid_host()' - Is the Host: field valid?
 */

static int				/* O - 1 if valid, 0 if not */
valid_host(cupsd_client_t *con)		/* I - Client connection */
{
  cupsd_alias_t	*a;			/* Current alias */
  cupsd_netif_t	*netif;			/* Current network interface */
  const char	*end;			/* End character */
  char		*ptr;			/* Pointer into host value */


 /*
  * Copy the Host: header for later use...
  */

  strlcpy(con->clientname, httpGetField(con->http, HTTP_FIELD_HOST),
          sizeof(con->clientname));
  if ((ptr = strrchr(con->clientname, ':')) != NULL && !strchr(ptr, ']'))
  {
    *ptr++ = '\0';
    con->clientport = atoi(ptr);
  }
  else
    con->clientport = con->serverport;

 /*
  * Then validate...
  */

  if (httpAddrLocalhost(httpGetAddress(con->http)))
  {
   /*
    * Only allow "localhost" or the equivalent IPv4 or IPv6 numerical
    * addresses when accessing CUPS via the loopback interface...
    */

    return (!_cups_strcasecmp(con->clientname, "localhost") ||
	    !_cups_strcasecmp(con->clientname, "localhost.") ||
#ifdef __linux
	    !_cups_strcasecmp(con->clientname, "localhost.localdomain") ||
#endif /* __linux */
            !strcmp(con->clientname, "127.0.0.1") ||
	    !strcmp(con->clientname, "[::1]"));
  }

#if defined(HAVE_DNSSD) || defined(HAVE_AVAHI)
 /*
  * Check if the hostname is something.local (Bonjour); if so, allow it.
  */

  if ((end = strrchr(con->clientname, '.')) != NULL && end > con->clientname &&
      !end[1])
  {
   /*
    * "." on end, work back to second-to-last "."...
    */

    for (end --; end > con->clientname && *end != '.'; end --);
  }

  if (end && (!_cups_strcasecmp(end, ".local") ||
	      !_cups_strcasecmp(end, ".local.")))
    return (1);
#endif /* HAVE_DNSSD || HAVE_AVAHI */

 /*
  * Check if the hostname is an IP address...
  */

  if (isdigit(con->clientname[0] & 255) || con->clientname[0] == '[')
  {
   /*
    * Possible IPv4/IPv6 address...
    */

    http_addrlist_t *addrlist;		/* List of addresses */


    if ((addrlist = httpAddrGetList(con->clientname, AF_UNSPEC, NULL)) != NULL)
    {
     /*
      * Good IPv4/IPv6 address...
      */

      httpAddrFreeList(addrlist);
      return (1);
    }
  }

 /*
  * Check for (alias) name matches...
  */

  for (a = (cupsd_alias_t *)cupsArrayFirst(ServerAlias);
       a;
       a = (cupsd_alias_t *)cupsArrayNext(ServerAlias))
  {
   /*
    * "ServerAlias *" allows all host values through...
    */

    if (!strcmp(a->name, "*"))
      return (1);

    if (!_cups_strncasecmp(con->clientname, a->name, a->namelen))
    {
     /*
      * Prefix matches; check the character at the end - it must be "." or nul.
      */

      end = con->clientname + a->namelen;

      if (!*end || (*end == '.' && !end[1]))
        return (1);
    }
  }

#if defined(HAVE_DNSSD) || defined(HAVE_AVAHI)
  for (a = (cupsd_alias_t *)cupsArrayFirst(DNSSDAlias);
       a;
       a = (cupsd_alias_t *)cupsArrayNext(DNSSDAlias))
  {
   /*
    * "ServerAlias *" allows all host values through...
    */

    if (!strcmp(a->name, "*"))
      return (1);

    if (!_cups_strncasecmp(con->clientname, a->name, a->namelen))
    {
     /*
      * Prefix matches; check the character at the end - it must be "." or nul.
      */

      end = con->clientname + a->namelen;

      if (!*end || (*end == '.' && !end[1]))
        return (1);
    }
  }
#endif /* HAVE_DNSSD || HAVE_AVAHI */

 /*
  * Check for interface hostname matches...
  */

  for (netif = (cupsd_netif_t *)cupsArrayFirst(NetIFList);
       netif;
       netif = (cupsd_netif_t *)cupsArrayNext(NetIFList))
  {
    if (!_cups_strncasecmp(con->clientname, netif->hostname, netif->hostlen))
    {
     /*
      * Prefix matches; check the character at the end - it must be "." or nul.
      */

      end = con->clientname + netif->hostlen;

      if (!*end || (*end == '.' && !end[1]))
        return (1);
    }
  }

  return (0);
}


/*
 * 'write_file()' - Send a file via HTTP.
 */

static int				/* O - 0 on failure, 1 on success */
write_file(cupsd_client_t *con,		/* I - Client connection */
           http_status_t  code,		/* I - HTTP status */
	   char           *filename,	/* I - Filename */
	   char           *type,	/* I - File type */
	   struct stat    *filestats)	/* O - File information */
{
  con->file = open(filename, O_RDONLY);

  cupsdLogClient(con, CUPSD_LOG_DEBUG2,
                 "write_file code=%d, filename=\"%s\" (%d), "
		 "type=\"%s\", filestats=%p",
		 code, filename, con->file, type ? type : "(null)", filestats);

  if (con->file < 0)
    return (0);

  fcntl(con->file, F_SETFD, fcntl(con->file, F_GETFD) | FD_CLOEXEC);

  con->pipe_pid = 0;

  if (!cupsdSendHeader(con, code, type, CUPSD_AUTH_NONE))
    return (0);

  if (httpPrintf(con->http, "Last-Modified: %s\r\n",
                 httpGetDateString(filestats->st_mtime)) < 0)
    return (0);
  if (httpPrintf(con->http, "Content-Length: " CUPS_LLFMT "\r\n",
                 CUPS_LLCAST filestats->st_size) < 0)
    return (0);
  if (httpPrintf(con->http, "\r\n") < 0)
    return (0);

  if (cupsdFlushHeader(con) < 0)
    return (0);

  con->http->data_encoding  = HTTP_ENCODING_LENGTH;
  con->http->data_remaining = filestats->st_size;

  if (con->http->data_remaining <= INT_MAX)
    con->http->_data_remaining = con->http->data_remaining;
  else
    con->http->_data_remaining = INT_MAX;

  cupsdAddSelect(httpGetFd(con->http), (cupsd_selfunc_t)cupsdReadClient,
                 (cupsd_selfunc_t)cupsdWriteClient, con);

  cupsdLogClient(con, CUPSD_LOG_DEBUG, "Sending file.");

  return (1);
}


/*
 * 'write_pipe()' - Flag that data is available on the CGI pipe.
 */

static void
write_pipe(cupsd_client_t *con)		/* I - Client connection */
{
  cupsdLogClient(con, CUPSD_LOG_DEBUG2, "write_pipe CGI output on fd %d",
                 con->file);

  con->file_ready = 1;

  cupsdRemoveSelect(con->file);
  cupsdAddSelect(httpGetFd(con->http), NULL, (cupsd_selfunc_t)cupsdWriteClient, con);

  cupsdLogClient(con, CUPSD_LOG_DEBUG, "CGI data ready to be sent.");
}


/*
 * End of "$Id$".
 */
