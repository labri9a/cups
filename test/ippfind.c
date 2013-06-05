/*
 * "$Id$"
 *
 *   Utility to find IPP printers via Bonjour/DNS-SD and optionally run
 *   commands such as IPP and Bonjour conformance tests.  This tool is
 *   inspired by the UNIX "find" command, thus its name.
 *
 *   Copyright 2008-2013 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 *
 * Usage:
 *
 *   ./ippfind [options] regtype[,subtype][.domain.] ... [expression]
 *   ./ippfind [options] name[.regtype[.domain.]] ... [expression]
 *   ./ippfind --help
 *   ./ippfind --version
 *
 *  Supported regtypes are:
 *
 *    _http._tcp                      - HTTP (RFC 2616)
 *    _https._tcp                     - HTTPS (RFC 2818)
 *    _ipp._tcp                       - IPP (RFC 2911)
 *    _ipps._tcp                      - IPPS (pending)
 *    _printer._tcp                   - LPD (RFC 1179)
 *
 *  Exit Codes:
 *
 *    0 if result for all processed expressions is true
 *    1 if result of any processed expression is false
 *    2 if browsing or any query or resolution failed
 *    3 if an undefined option or invalid expression was specified
 *
 *  Options:
 *
 *    --help                          - Show program help
 *    --version                       - Show program version
 *    -4                              - Use IPv4 when listing
 *    -6                              - Use IPv6 when listing
 *    -T seconds                      - Specify browse timeout (default 10
 *                                      seconds)
 *    -V version                      - Specify IPP version (1.1, 2.0, 2.1, 2.2)
 *
 *  "expression" is any of the following:
 *
 *   -d regex
 *   --domain regex                   - True if the domain matches the given
 *                                      regular expression.
 *
 *   -e utility [argument ...] ;
 *   --exec utility [argument ...] ;  - Executes the specified program; "{}"
 *                                      does a substitution (see below)
 *
 *   -l
 *   --ls                             - Lists attributes returned by
 *                                      Get-Printer-Attributes for IPP printers,
 *                                      ???? of HEAD request for HTTP URLs)
 *                                      True if resource is accessible, false
 *                                      otherwise.
 *
 *   --local                          - True if the service is local to this
 *                                      computer.
 *
 *   -n regex
 *   --name regex                     - True if the name matches the given
 *                                      regular expression.
 *
 *   --path regex                     - True if the URI resource path matches
 *                                      the given regular expression.
 *
 *   -p
 *   --print                          - Prints the URI of found printers (always
 *                                      true, default if -e, -l, -p, -q, or -s
 *                                      is not specified.
 *
 *   -q
 *   --quiet                          - Quiet mode (just return exit code)
 *
 *   -r
 *   --remote                         - True if the service is not local to this
 *                                      computer.
 *
 *   -s
 *   --print-name                     - Prints the service name of found
 *                                      printers.
 *
 *   -t key
 *   --txt key                        - True if the TXT record contains the
 *                                      named key
 *
 *   --txt-* regex                    - True if the TXT record contains the
 *                                      named key and matches the given regular
 *                                      expression.
 *
 *   -u regex
 *   --uri regex                      - True if the URI matches the given
 *                                      regular expression.
 *
 * Expressions may also contain modifiers:
 *
 *   ( expression )                  - Group the result of expressions.
 *
 *   ! expression
 *   --not expression                - Unary NOT
 *
 *   --false                         - Always false
 *   --true                          - Always true
 *
 *   expression expression
 *   expression --and expression     - Logical AND.
 *
 *   expression --or expression      - Logical OR.
 *
 * The substitutions for {} are:
 *
 *   {}                              - URI
 *   {service_domain}                - Domain name
 *   {service_hostname}              - FQDN
 *   {service_name}                  - Service name
 *   {service_port}                  - Port number
 *   {service_regtype}               - DNS-SD registration type
 *   {service_scheme}                - URI scheme for DNS-SD registration type
 *   {service_uri}                   - URI
 *   {txt_*}                         - Value of TXT record key
 *
 * These variables are also set in the environment for executed programs:
 *
 *   IPPFIND_SERVICE_DOMAIN          - Domain name
 *   IPPFIND_SERVICE_HOSTNAME        - FQDN
 *   IPPFIND_SERVICE_NAME            - Service name
 *   IPPFIND_SERVICE_PORT            - Port number
 *   IPPFIND_SERVICE_REGTYPE         - DNS-SD registration type
 *   IPPFIND_SERVICE_SCHEME          - URI scheme for DNS-SD registration type
 *   IPPFIND_SERVICE_URI             - URI
 *   IPPFIND_TXT_*                   - Values of TXT record key (uppercase)
 *
 * Contents:
 *
 */

/*
 * Include necessary headers.
 */

#include <cups/cups-private.h>
#include <regex.h>
#ifdef HAVE_DNSSD
#  include <dns_sd.h>
#elif defined(HAVE_AVAHI)
#  include <avahi-client/client.h>
#  include <avahi-client/lookup.h>
#  include <avahi-common/simple-watch.h>
#  include <avahi-common/domain.h>
#  include <avahi-common/error.h>
#  include <avahi-common/malloc.h>
#  define kDNSServiceMaxDomainName AVAHI_DOMAIN_NAME_MAX
#endif /* HAVE_DNSSD */


/*
 * Structures...
 */

typedef enum ippfind_exit_e		/* Exit codes */
{
  IPPFIND_EXIT_TRUE = 0,		/* OK and result is true */
  IPPFIND_EXIT_FALSE,			/* OK but result is false*/
  IPPFIND_EXIT_BONJOUR,			/* Browse/resolve failure */
  IPPFIND_EXIT_SYNTAX,			/* Bad option or syntax error */
  IPPFIND_EXIT_MEMORY			/* Out of memory */
} ippfind_exit_t;

typedef enum ippfind_op_e		/* Operations for expressions */
{
  IPPFIND_OP_NONE,			/* No operation */
  IPPFIND_OP_AND,			/* Logical AND of all children */
  IPPFIND_OP_OR,			/* Logical OR of all children */
  IPPFIND_OP_TRUE,			/* Always true */
  IPPFIND_OP_FALSE,			/* Always false */
  IPPFIND_OP_IS_LOCAL,			/* Is a local service */
  IPPFIND_OP_IS_REMOTE,			/* Is a remote service */
  IPPFIND_OP_DOMAIN_REGEX,		/* Domain matches regular expression */
  IPPFIND_OP_NAME_REGEX,		/* Name matches regular expression */
  IPPFIND_OP_PATH_REGEX,		/* Path matches regular expression */
  IPPFIND_OP_TXT_EXISTS,		/* TXT record key exists */
  IPPFIND_OP_TXT_REGEX,			/* TXT record key matches regular expression */
  IPPFIND_OP_URI_REGEX,			/* URI matches regular expression */

  IPPFIND_OP_OUTPUT = 100,		/* Output operations */
  IPPFIND_OP_EXEC,			/* Execute when true */
  IPPFIND_OP_LIST,			/* List when true */
  IPPFIND_OP_PRINT_NAME,		/* Print URI when true */
  IPPFIND_OP_PRINT_URI,			/* Print name when true */
  IPPFIND_OP_QUIET,			/* No output when true */
} ippfind_op_t;

typedef struct ippfind_expr_s		/* Expression */
{
  struct ippfind_expr_s
		*prev,			/* Previous expression */
		*next,			/* Next expression */
		*parent,		/* Parent expressions */
		*child;			/* Child expressions */
  ippfind_op_t	op;			/* Operation code (see above) */
  int		invert;			/* Invert the result */
  char		*key;			/* TXT record key */
  regex_t	re;			/* Regular expression for matching */
  int		num_args;		/* Number of arguments for exec */
  char		**args;			/* Arguments for exec */
} ippfind_expr_t;

typedef struct ippfind_srv_s		/* Service information */
{
#ifdef HAVE_DNSSD
  DNSServiceRef	ref;			/* Service reference for query */
#elif defined(HAVE_AVAHI)
  AvahiServiceResolver *ref;		/* Resolver */
#endif /* HAVE_DNSSD */
  char		*name,			/* Service name */
		*domain,		/* Domain name */
		*regtype,		/* Registration type */
		*fullName,		/* Full name */
		*host,			/* Hostname */
		*uri;			/* URI */
  int		num_txt;		/* Number of TXT record keys */
  cups_option_t	*txt;			/* TXT record keys */
  int		port,			/* Port number */
		is_local,		/* Is a local service? */
		is_processed,		/* Did we process the service? */
		is_resolved;		/* Got the resolve data? */
} ippfind_srv_t;


/*
 * Local globals...
 */

#ifdef HAVE_DNSSD
static DNSServiceRef dnssd_ref;		/* Master service reference */
#elif defined(HAVE_AVAHI)
static AvahiClient *avahi_client = NULL;/* Client information */
static int	avahi_got_data = 0;	/* Got data from poll? */
static AvahiSimplePoll *avahi_poll = NULL;
					/* Poll information */
#endif /* HAVE_DNSSD */

static int	address_family = AF_UNSPEC;
					/* Address family for LIST */
static int	bonjour_error = 0;	/* Error browsing/resolving? */
static double	bonjour_timeout = 1.0;	/* Timeout in seconds */
static int	ipp_version = 20;	/* IPP version for LIST */


/*
 * Local functions...
 */

#ifdef HAVE_DNSSD
static void		browse_callback(DNSServiceRef sdRef,
			                DNSServiceFlags flags,
				        uint32_t interfaceIndex,
				        DNSServiceErrorType errorCode,
				        const char *serviceName,
				        const char *regtype,
				        const char *replyDomain, void *context)
					__attribute__((nonnull(1,5,6,7,8)));
static void		browse_local_callback(DNSServiceRef sdRef,
					      DNSServiceFlags flags,
					      uint32_t interfaceIndex,
					      DNSServiceErrorType errorCode,
					      const char *serviceName,
					      const char *regtype,
					      const char *replyDomain,
					      void *context)
					      __attribute__((nonnull(1,5,6,7,8)));
#elif defined(HAVE_AVAHI)
static void		browse_callback(AvahiServiceBrowser *browser,
					AvahiIfIndex interface,
					AvahiProtocol protocol,
					AvahiBrowserEvent event,
					const char *serviceName,
					const char *regtype,
					const char *replyDomain,
					AvahiLookupResultFlags flags,
					void *context);
static void		client_callback(AvahiClient *client,
					AvahiClientState state,
					void *context);
#endif /* HAVE_AVAHI */

static int		compare_services(ippfind_srv_t *a, ippfind_srv_t *b);
static const char	*dnssd_error_string(int error);
static int		eval_expr(ippfind_srv_t *service,
			          ippfind_expr_t *expressions);
static ippfind_srv_t	*get_service(cups_array_t *services,
				     const char *serviceName,
				     const char *regtype,
				     const char *replyDomain)
				     __attribute__((nonnull(1,2,3,4)));
static double		get_time(void);
static ippfind_expr_t	*new_expr(ippfind_op_t op, int invert, const char *key,
			          const char *regex, char **args);
#ifdef HAVE_DNSSD
static void		resolve_callback(DNSServiceRef sdRef,
			                 DNSServiceFlags flags,
				         uint32_t interfaceIndex,
				         DNSServiceErrorType errorCode,
				         const char *fullName,
				         const char *hostTarget, uint16_t port,
				         uint16_t txtLen,
				         const unsigned char *txtRecord,
				         void *context)
				         __attribute__((nonnull(1,5,6,9, 10)));
#elif defined(HAVE_AVAHI)
static int		poll_callback(struct pollfd *pollfds,
			              unsigned int num_pollfds, int timeout,
			              void *context);
static void		resolve_callback(AvahiServiceResolver *res,
					 AvahiIfIndex interface,
					 AvahiProtocol protocol,
					 AvahiBrowserEvent event,
					 const char *serviceName,
					 const char *regtype,
					 const char *replyDomain,
					 const char *host_name,
					 uint16_t port,
					 AvahiStringList *txt,
					 AvahiLookupResultFlags flags,
					 void *context);
#endif /* HAVE_DNSSD */
static void		set_service_uri(ippfind_srv_t *service);
static void		show_usage(void) __attribute__((noreturn));
static void		show_version(void) __attribute__((noreturn));


/*
 * 'main()' - Browse for printers.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line args */
     char *argv[])			/* I - Command-line arguments */
{
  int			i,		/* Looping var */
			have_output = 0,/* Have output expression */
			status = IPPFIND_EXIT_TRUE;
					/* Exit status */
  const char		*opt,		/* Option character */
			*search;	/* Current browse/resolve string */
  cups_array_t		*searches;	/* Things to browse/resolve */
  cups_array_t		*services;	/* Service array */
  ippfind_srv_t		*service;	/* Current service */
  ippfind_expr_t	*expressions = NULL,
					/* Expression tree */
			*temp = NULL,	/* New expression */
			*parent = NULL,	/* Parent expression */
			*current = NULL;/* Current expression */
  ippfind_op_t		logic = IPPFIND_OP_AND;
					/* Logic for next expression */
  int			invert = 0;	/* Invert expression? */
  int			err;		/* DNS-SD error */
#ifdef HAVE_DNSSD
  fd_set		sinput;		/* Input set for select() */
  struct timeval	stimeout;	/* Timeout for select() */
#endif /* HAVE_DNSSD */
  double		endtime;	/* End time */


 /*
  * Initialize the locale...
  */

  _cupsSetLocale(argv);

 /*
  * Create arrays to track services and things we want to browse/resolve...
  */

  searches = cupsArrayNew(NULL, NULL);
  services = cupsArrayNew((cups_array_func_t)compare_services, NULL);

 /*
  * Parse command-line...
  */

  for (i = 1; i < argc; i ++)
  {
    if (argv[i][0] == '-')
    {
      if (argv[i][1] == '-')
      {
       /*
        * Parse --option options...
        */

        if (!strcmp(argv[i], "--and"))
        {
	  if (logic != IPPFIND_OP_AND && current && current->prev)
	  {
	   /*
	    * OK, we have more than 1 rule in the current tree level.
	    * Make a new group tree and move the previous rule to it...
	    */

	    if ((temp = new_expr(IPPFIND_OP_AND, 0, NULL, NULL, NULL)) == NULL)
	      return (IPPFIND_EXIT_MEMORY);

	    temp->child         = current;
	    temp->parent        = current->parent;
	    current->prev->next = temp;
	    temp->prev          = current->prev;

	    current->prev   = NULL;
	    current->parent = temp;
	    parent          = temp;
	  }
	  else if (parent)
	    parent->op = IPPFIND_OP_AND;

	  logic = IPPFIND_OP_AND;
	  temp  = NULL;
        }
        else if (!strcmp(argv[i], "--domain"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_DOMAIN_REGEX, invert, NULL, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--exec"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_EXEC, invert, NULL, NULL,
                               argv + i)) == NULL)
            return (IPPFIND_EXIT_MEMORY);

          while (i < argc)
            if (!strcmp(argv[i], ";"))
              break;
            else
              i ++;

          if (i >= argc)
            show_usage();

          have_output = 1;
        }
        else if (!strcmp(argv[i], "--false"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_FALSE, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--help"))
        {
          show_usage();
        }
        else if (!strcmp(argv[i], "--ls"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_LIST, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);

          have_output = 1;
        }
        else if (!strcmp(argv[i], "--local"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_IS_LOCAL, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--name"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_NAME_REGEX, invert, NULL, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--not"))
        {
          invert = 1;
        }
        else if (!strcmp(argv[i], "--or"))
        {
	  if (logic != IPPFIND_OP_OR && current)
	  {
	   /*
	    * OK, we have two possibilities; either this is the top-level
	    * rule or we have a bunch of AND rules at this level.
	    */

	    if (!parent)
	    {
	     /*
	      * This is the top-level rule; we have to group *all* of the AND
	      * rules down a level, as AND has precedence over OR.
	      */

	      if ((temp = new_expr(IPPFIND_OP_AND, 0, NULL, NULL,
	                           NULL)) == NULL)
		return (IPPFIND_EXIT_MEMORY);

	      while (current->prev)
	      {
		current->parent = temp;
		current         = current->prev;
	      }

	      current->parent = temp;
	      temp->child     = current;

	      expressions = current = temp;
	    }
	    else
	    {
	     /*
	      * This isn't the top rule, so go up one level...
	      */

	      current = parent;
	      parent  = current->parent;
	    }
	  }

	  logic = IPPFIND_OP_OR;
	  temp  = NULL;
        }
        else if (!strcmp(argv[i], "--path"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_PATH_REGEX, invert, NULL, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--print"))
        {
          if ((temp = new_expr(IPPFIND_OP_PRINT_URI, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);

          have_output = 1;
        }
        else if (!strcmp(argv[i], "--print-name"))
        {
          if ((temp = new_expr(IPPFIND_OP_PRINT_NAME, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);

          have_output = 1;
        }
        else if (!strcmp(argv[i], "--quiet"))
        {
          if ((temp = new_expr(IPPFIND_OP_QUIET, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);

          have_output = 1;
        }
        else if (!strcmp(argv[i], "--remote"))
        {
          if ((temp = new_expr(IPPFIND_OP_IS_REMOTE, invert, NULL, NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--true"))
        {
          if ((temp = new_expr(IPPFIND_OP_TRUE, invert, NULL, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--txt"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_TXT_EXISTS, invert, argv[i], NULL,
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strncmp(argv[i], "--txt-", 5))
        {
          const char *key = argv[i] + 5;/* TXT key */

          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_TXT_REGEX, invert, key, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--uri"))
        {
          i ++;
          if (i >= argc)
            show_usage();

          if ((temp = new_expr(IPPFIND_OP_URI_REGEX, invert, NULL, argv[i],
                               NULL)) == NULL)
            return (IPPFIND_EXIT_MEMORY);
        }
        else if (!strcmp(argv[i], "--version"))
        {
          show_version();
        }
        else
        {
	  _cupsLangPrintf(stderr, _("%s: Unknown option \"%s\"."),
			  "ippfind", argv[i]);
	  show_usage();
	}

        if (temp)
        {
         /*
          * Add new expression...
          */

	  if (current)
	  {
	    temp->parent  = parent;
	    current->next = temp;
	  }
	  else if (parent)
	    parent->child = temp;
	  else
	    expressions = temp;

	  temp->prev = current;
	  current    = temp;
          invert     = 0;
          temp       = NULL;
        }
      }
      else
      {
       /*
        * Parse -o options
        */

        for (opt = argv[i] + 1; *opt; opt ++)
        {
          switch (*opt)
          {
            case '4' :
                address_family = AF_INET;
                break;

            case '6' :
                address_family = AF_INET6;
                break;

            case 'T' :
                i ++;
                if (i >= argc)
                  show_usage();

                bonjour_timeout = atof(argv[i]);
                break;

            case 'V' :
                i ++;
                if (i >= argc)
                  show_usage();

                if (!strcmp(argv[i], "1.1"))
                  ipp_version = 11;
                else if (!strcmp(argv[i], "2.0"))
                  ipp_version = 20;
                else if (!strcmp(argv[i], "2.1"))
                  ipp_version = 21;
                else if (!strcmp(argv[i], "2.2"))
                  ipp_version = 22;
                else
                  show_usage();
                break;

            case 'd' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_DOMAIN_REGEX, invert, NULL,
		                     argv[i], NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);
                break;

            case 'e' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_EXEC, invert, NULL, NULL,
				     argv + i)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);

		while (i < argc)
		  if (!strcmp(argv[i], ";"))
		    break;
		  else
		    i ++;

		if (i >= argc)
		  show_usage();

		have_output = 1;
                break;

            case 'l' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_LIST, invert, NULL, NULL,
				     NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);

		have_output = 1;
                break;

            case 'n' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_NAME_REGEX, invert, NULL,
		                     argv[i], NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);
                break;

            case 'p' :
		if ((temp = new_expr(IPPFIND_OP_PRINT_URI, invert, NULL, NULL,
				     NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);

		have_output = 1;
                break;

            case 'q' :
		if ((temp = new_expr(IPPFIND_OP_QUIET, invert, NULL, NULL,
				     NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);

		have_output = 1;
                break;

            case 'r' :
		if ((temp = new_expr(IPPFIND_OP_IS_REMOTE, invert, NULL, NULL,
				     NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);
                break;

            case 's' :
		if ((temp = new_expr(IPPFIND_OP_PRINT_NAME, invert, NULL, NULL,
				     NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);

		have_output = 1;
                break;

            case 't' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_TXT_EXISTS, invert, argv[i],
		                     NULL, NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);
                break;

            case 'u' :
		i ++;
		if (i >= argc)
		  show_usage();

		if ((temp = new_expr(IPPFIND_OP_URI_REGEX, invert, NULL,
		                     argv[i], NULL)) == NULL)
		  return (IPPFIND_EXIT_MEMORY);
                break;

            default :
                _cupsLangPrintf(stderr, _("%s: Unknown option \"-%c\"."),
                                "ippfind", *opt);
                show_usage();
                break;
          }

	  if (temp)
	  {
	   /*
	    * Add new expression...
	    */

	    if (current)
	    {
	      temp->parent  = parent;
	      current->next = temp;
	    }
	    else if (parent)
	      parent->child = temp;
	    else
	      expressions = temp;

	    temp->prev = current;
	    current    = temp;
	    invert     = 0;
	    temp       = NULL;
	  }
        }
      }
    }
    else if (!strcmp(argv[i], "("))
    {
      if ((temp = new_expr(IPPFIND_OP_AND, invert, NULL, NULL, NULL)) == NULL)
	return (IPPFIND_EXIT_MEMORY);

      if (current)
      {
	temp->parent  = current->parent;
	current->next = temp;
      }
      else
	expressions = temp;

      temp->prev = current;
      parent     = temp;
      current    = NULL;
      invert     = 0;
      logic      = IPPFIND_OP_AND;
    }
    else if (!strcmp(argv[i], ")"))
    {
      if (!parent)
      {
        _cupsLangPuts(stderr, _("ippfind: Missing open parenthesis."));
        show_usage();
      }

      current = parent;
      parent  = current->parent;

      if (!parent)
        logic = IPPFIND_OP_AND;
      else
        logic = parent->op;
    }
    else if (!strcmp(argv[i], "!"))
    {
      invert = 1;
    }
    else
    {
     /*
      * _regtype._tcp[,subtype][.domain]
      *
      *   OR
      *
      * service-name[._regtype._tcp[.domain]]
      */

      cupsArrayAdd(searches, argv[i]);
    }
  }

  if (parent)
  {
    _cupsLangPuts(stderr, _("ippfind: Missing close parenthesis."));
    show_usage();
  }

  if (cupsArrayCount(searches) == 0)
  {
   /*
    * Add an implicit browse for IPP printers ("_ipp._tcp")...
    */

    cupsArrayAdd(searches, "_ipp._tcp");
  }

  if (!have_output)
  {
   /*
    * Add an implicit --print-uri to the end...
    */

    if ((temp = new_expr(IPPFIND_OP_PRINT_URI, 0, NULL, NULL, NULL)) == NULL)
      return (IPPFIND_EXIT_MEMORY);

    if (current)
    {
      temp->parent  = parent;
      current->next = temp;
    }
    else
      expressions = temp;

    temp->prev = current;
  }

 /*
  * Start up browsing/resolving...
  */

#ifdef HAVE_DNSSD
  if ((err = DNSServiceCreateConnection(&dnssd_ref)) != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("ippfind: Unable to use Bonjour: %s"),
                    dnssd_error_string(err));
    return (IPPFIND_EXIT_BONJOUR);
  }

#elif defined(HAVE_AVAHI)
  if ((avahi_poll = avahi_simple_poll_new()) == NULL)
  {
    _cupsLangPrintError(stderr, _("ippfind: Unable to use Bonjour: %s"),
                        strerror(errno));
    return (IPPFIND_EXIT_BONJOUR);
  }

  avahi_simple_poll_set_func(avahi_poll, poll_callback, NULL);

  avahi_client = avahi_client_new(avahi_simple_poll_get(avahi_poll),
			          0, client_callback, avahi_poll, &err);
  if (!client)
  {
    _cupsLangPrintError(stderr, _("ippfind: Unable to use Bonjour: %s"),
                        dnssd_error_string(err));
    return (IPPFIND_EXIT_BONJOUR);
  }
#endif /* HAVE_DNSSD */

  for (search = (const char *)cupsArrayFirst(searches);
       search;
       search = (const char *)cupsArrayNext(searches))
  {
    char		buf[1024],	/* Full name string */
			*name = NULL,	/* Service instance name */
			*regtype,	/* Registration type */
			*domain;	/* Domain, if any */

    strlcpy(buf, search, sizeof(buf));
    if (buf[0] == '_')
    {
      regtype = buf;
    }
    else if ((regtype = strstr(buf, "._")) != NULL)
    {
      name = buf;
      *regtype++ = '\0';
    }
    else
    {
      name    = buf;
      regtype = "_ipp._tcp";
    }

    for (domain = regtype; *domain; domain ++)
      if (*domain == '.' && domain[1] != '_')
      {
        *domain++ = '\0';
        break;
      }

    if (!*domain)
      domain = NULL;

    if (name)
    {
     /*
      * Resolve the given service instance name, regtype, and domain...
      */

      if (!domain)
        domain = "local.";

      service = get_service(services, name, regtype, domain);

#ifdef HAVE_DNSSD
      service->ref = dnssd_ref;
      err          = DNSServiceResolve(&(service->ref),
                                       kDNSServiceFlagsShareConnection, 0, name,
				       regtype, domain, resolve_callback,
				       service);

#elif defined(HAVE_AVAHI)
      service->ref = avahi_service_resolver_new(avahi_client, AVAHI_IF_UNSPEC,
                                                AVAHI_PROTO_UNSPEC, name,
                                                regtype, domain,
                                                AVAHI_PROTO_UNSPEC, 0,
                                                resolve_callback, service);
      if (service->ref)
        err = 0;
      else
        err = avahi_client_get_errno(avahi_client);
#endif /* HAVE_DNSSD */
    }
    else
    {
     /*
      * Browse for services of the given type...
      */

#ifdef HAVE_DNSSD
      DNSServiceRef	ref;		/* Browse reference */

      ref = dnssd_ref;
      err = DNSServiceBrowse(&ref, kDNSServiceFlagsShareConnection, 0, regtype,
                             domain, browse_callback, services);

      if (!err)
      {
	ref = dnssd_ref;
	err = DNSServiceBrowse(&ref, kDNSServiceFlagsShareConnection,
			       kDNSServiceInterfaceIndexLocalOnly, regtype,
			       domain, browse_local_callback, services);
      }

#elif defined(HAVE_AVAHI)
      if (avahi_service_browser_new(avahi_client, AVAHI_IF_UNSPEC,
                                    AVAHI_PROTO_UNSPEC, regtype, domain, 0,
                                    browse_callback, services))
        err = 0;
      else
        err = avahi_client_get_errno(avahi_client);
#endif /* HAVE_DNSSD */
    }

    if (err)
    {
      _cupsLangPrintf(stderr, _("ippfind: Unable to browse or resolve: %s"),
                      dnssd_error_string(err));

      if (name)
        printf("name=\"%s\"\n", name);

      printf("regtype=\"%s\"\n", regtype);

      if (domain)
        printf("domain=\"%s\"\n", domain);

      return (IPPFIND_EXIT_BONJOUR);
    }
  }

 /*
  * Process browse/resolve requests...
  */

  if (bonjour_timeout > 1.0)
    endtime = get_time() + bonjour_timeout;
  else
    endtime = get_time() + 300.0;

  while (get_time() < endtime)
  {
    int		process = 0;		/* Process services? */

#ifdef HAVE_DNSSD
    int fd = DNSServiceRefSockFD(dnssd_ref);
					/* File descriptor for DNS-SD */

    FD_ZERO(&sinput);
    FD_SET(fd, &sinput);

    stimeout.tv_sec  = 0;
    stimeout.tv_usec = 500000;

    if (select(fd + 1, &sinput, NULL, NULL, &stimeout) < 0)
      continue;

    if (FD_ISSET(fd, &sinput))
    {
     /*
      * Process responses...
      */

      DNSServiceProcessResult(dnssd_ref);
    }
    else
    {
     /*
      * Time to process services...
      */

      process = 1;
    }

#elif defined(HAVE_AVAHI)
    avahi_got_data = 0;

    if (avahi_simple_poll_iterate(avahi_poll, 500) > 0)
    {
     /*
      * We've been told to exit the loop.  Perhaps the connection to
      * Avahi failed.
      */

      return (IPPFIND_EXIT_BONJOUR);
    }

    if (!avahi_got_data)
    {
     /*
      * Time to process services...
      */

      process = 1;
    }
#endif /* HAVE_DNSSD */

    if (process)
    {
     /*
      * Process any services that we have found...
      */

      int	active = 0,		/* Number of active resolves */
		resolved = 0,		/* Number of resolved services */
		processed = 0;		/* Number of processed services */

      for (service = (ippfind_srv_t *)cupsArrayFirst(services);
           service;
           service = (ippfind_srv_t *)cupsArrayNext(services))
      {
        if (service->is_processed)
          processed ++;

        if (service->is_resolved)
          resolved ++;

        if (!service->ref && !service->is_resolved)
        {
         /*
          * Found a service, now resolve it (but limit to 50 active resolves...)
          */

          if (active < 50)
          {
#ifdef HAVE_DNSSD
	    service->ref = dnssd_ref;
	    err          = DNSServiceResolve(&(service->ref),
					     kDNSServiceFlagsShareConnection, 0,
					     service->name, service->regtype,
					     service->domain, resolve_callback,
					     service);

#elif defined(HAVE_AVAHI)
	    service->ref = avahi_service_resolver_new(avahi_client,
						      AVAHI_IF_UNSPEC,
						      AVAHI_PROTO_UNSPEC,
						      service->name,
						      service->regtype,
						      service->domain,
						      AVAHI_PROTO_UNSPEC, 0,
						      resolve_callback,
						      service);
	    if (service->ref)
	      err = 0;
	    else
	      err = avahi_client_get_errno(avahi_client);
#endif /* HAVE_DNSSD */

	    if (err)
	    {
	      _cupsLangPrintf(stderr,
	                      _("ippfind: Unable to browse or resolve: %s"),
			      dnssd_error_string(err));
	      return (IPPFIND_EXIT_BONJOUR);
	    }

	    active ++;
          }
        }
        else if (service->is_resolved && !service->is_processed)
        {
	 /*
	  * Resolved, not process this service against the expressions...
	  */

          if (service->ref)
          {
#ifdef HAVE_DNSSD
	    DNSServiceRefDeallocate(service->ref);
#else
            avahi_record_browser_free(service->ref);
#endif /* HAVE_DNSSD */

	    service->ref = NULL;
	  }

          if (!eval_expr(service, expressions))
            status = IPPFIND_EXIT_FALSE;

          service->is_processed = 1;
        }
        else if (service->ref)
          active ++;
      }

     /*
      * If we have processed all services we have discovered, then we are done.
      */

      if (processed == cupsArrayCount(services) && bonjour_timeout <= 1.0)
        break;
    }
  }

  if (bonjour_error)
    return (IPPFIND_EXIT_BONJOUR);
  else
    return (status);
}


#ifdef HAVE_DNSSD
/*
 * 'browse_callback()' - Browse devices.
 */

static void
browse_callback(
    DNSServiceRef       sdRef,		/* I - Service reference */
    DNSServiceFlags     flags,		/* I - Option flags */
    uint32_t            interfaceIndex,	/* I - Interface number */
    DNSServiceErrorType errorCode,	/* I - Error, if any */
    const char          *serviceName,	/* I - Name of service/device */
    const char          *regtype,	/* I - Type of service */
    const char          *replyDomain,	/* I - Service domain */
    void                *context)	/* I - Services array */
{
 /*
  * Only process "add" data...
  */

  if (errorCode != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd))
    return;

 /*
  * Get the device...
  */

  get_service((cups_array_t *)context, serviceName, regtype, replyDomain);
}


/*
 * 'browse_local_callback()' - Browse local devices.
 */

static void
browse_local_callback(
    DNSServiceRef       sdRef,		/* I - Service reference */
    DNSServiceFlags     flags,		/* I - Option flags */
    uint32_t            interfaceIndex,	/* I - Interface number */
    DNSServiceErrorType errorCode,	/* I - Error, if any */
    const char          *serviceName,	/* I - Name of service/device */
    const char          *regtype,	/* I - Type of service */
    const char          *replyDomain,	/* I - Service domain */
    void                *context)	/* I - Services array */
{
  ippfind_srv_t	*service;		/* Service */


 /*
  * Only process "add" data...
  */

  if (errorCode != kDNSServiceErr_NoError || !(flags & kDNSServiceFlagsAdd))
    return;

 /*
  * Get the device...
  */

  service = get_service((cups_array_t *)context, serviceName, regtype,
                        replyDomain);
  service->is_local = 1;
}
#endif /* HAVE_DNSSD */


#ifdef HAVE_AVAHI
/*
 * 'browse_callback()' - Browse devices.
 */

static void
browse_callback(
    AvahiServiceBrowser    *browser,	/* I - Browser */
    AvahiIfIndex           interface,	/* I - Interface index (unused) */
    AvahiProtocol          protocol,	/* I - Network protocol (unused) */
    AvahiBrowserEvent      event,	/* I - What happened */
    const char             *name,	/* I - Service name */
    const char             *type,	/* I - Registration type */
    const char             *domain,	/* I - Domain */
    AvahiLookupResultFlags flags,	/* I - Flags */
    void                   *context)	/* I - Services array */
{
  AvahiClient	*client = avahi_service_browser_get_client(browser);
					/* Client information */
  ippfind_srv_t	*service;		/* Service information */


  (void)interface;
  (void)protocol;
  (void)context;

  switch (event)
  {
    case AVAHI_BROWSER_FAILURE:
	fprintf(stderr, "DEBUG: browse_callback: %s\n",
		avahi_strerror(avahi_client_errno(client)));
	bonjour_error = 1;
	avahi_simple_poll_quit(simple_poll);
	break;

    case AVAHI_BROWSER_NEW:
       /*
	* This object is new on the network. Create a device entry for it if
	* it doesn't yet exist.
	*/

	service = get_service((cups_array_t *)context, name, type, domain);

	if (flags & AVAHI_LOOKUP_RESULT_LOCAL)
	  service->is_local = 1;
	break;

    case AVAHI_BROWSER_REMOVE:
    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
        break;
  }
}


/*
 * 'client_callback()' - Avahi client callback function.
 */

static void
client_callback(
    AvahiClient      *client,		/* I - Client information (unused) */
    AvahiClientState state,		/* I - Current state */
    void             *context)		/* I - User data (unused) */
{
  (void)client;
  (void)context;

 /*
  * If the connection drops, quit.
  */

  if (state == AVAHI_CLIENT_FAILURE)
  {
    fputs("DEBUG: Avahi connection failed.\n", stderr);
    bonjour_error = 1;
    avahi_simple_poll_quit(avahi_poll);
  }
}
#endif /* HAVE_AVAHI */


/*
 * 'compare_services()' - Compare two devices.
 */

static int				/* O - Result of comparison */
compare_services(ippfind_srv_t *a,	/* I - First device */
                 ippfind_srv_t *b)	/* I - Second device */
{
  return (strcmp(a->name, b->name));
}


/*
 * 'dnssd_error_string()' - Return an error string for an error code.
 */

static const char *			/* O - Error message */
dnssd_error_string(int error)		/* I - Error number */
{
#  ifdef HAVE_DNSSD
  switch (error)
  {
    case kDNSServiceErr_NoError :
        return ("OK.");

    default :
    case kDNSServiceErr_Unknown :
        return ("Unknown error.");

    case kDNSServiceErr_NoSuchName :
        return ("Service not found.");

    case kDNSServiceErr_NoMemory :
        return ("Out of memory.");

    case kDNSServiceErr_BadParam :
        return ("Bad parameter.");

    case kDNSServiceErr_BadReference :
        return ("Bad service reference.");

    case kDNSServiceErr_BadState :
        return ("Bad state.");

    case kDNSServiceErr_BadFlags :
        return ("Bad flags.");

    case kDNSServiceErr_Unsupported :
        return ("Unsupported.");

    case kDNSServiceErr_NotInitialized :
        return ("Not initialized.");

    case kDNSServiceErr_AlreadyRegistered :
        return ("Already registered.");

    case kDNSServiceErr_NameConflict :
        return ("Name conflict.");

    case kDNSServiceErr_Invalid :
        return ("Invalid name.");

    case kDNSServiceErr_Firewall :
        return ("Firewall prevents registration.");

    case kDNSServiceErr_Incompatible :
        return ("Client library incompatible.");

    case kDNSServiceErr_BadInterfaceIndex :
        return ("Bad interface index.");

    case kDNSServiceErr_Refused :
        return ("Server prevents registration.");

    case kDNSServiceErr_NoSuchRecord :
        return ("Record not found.");

    case kDNSServiceErr_NoAuth :
        return ("Authentication required.");

    case kDNSServiceErr_NoSuchKey :
        return ("Encryption key not found.");

    case kDNSServiceErr_NATTraversal :
        return ("Unable to traverse NAT boundary.");

    case kDNSServiceErr_DoubleNAT :
        return ("Unable to traverse double-NAT boundary.");

    case kDNSServiceErr_BadTime :
        return ("Bad system time.");

    case kDNSServiceErr_BadSig :
        return ("Bad signature.");

    case kDNSServiceErr_BadKey :
        return ("Bad encryption key.");

    case kDNSServiceErr_Transient :
        return ("Transient error occurred - please try again.");

    case kDNSServiceErr_ServiceNotRunning :
        return ("Server not running.");

    case kDNSServiceErr_NATPortMappingUnsupported :
        return ("NAT doesn't support NAT-PMP or UPnP.");

    case kDNSServiceErr_NATPortMappingDisabled :
        return ("NAT supports NAT-PNP or UPnP but it is disabled.");

    case kDNSServiceErr_NoRouter :
        return ("No Internet/default router configured.");

    case kDNSServiceErr_PollingMode :
        return ("Service polling mode error.");

    case kDNSServiceErr_Timeout :
        return ("Service timeout.");
  }

#  elif defined(HAVE_AVAHI)
  return (avahi_strerror(error));
#  endif /* HAVE_DNSSD */
}


/*
 * 'eval_expr()' - Evaluate the expressions against the specified service.
 *
 * Returns 1 for true and 0 for false.
 */

static int				/* O - Result of evaluation */
eval_expr(ippfind_srv_t  *service,	/* I - Service */
	  ippfind_expr_t *expressions)	/* I - Expressions */
{
  (void)expressions;

  puts(service->uri);
  return (1);
}


/*
 * 'get_service()' - Create or update a device.
 */

static ippfind_srv_t *			/* O - Service */
get_service(cups_array_t *services,	/* I - Service array */
	    const char   *serviceName,	/* I - Name of service/device */
	    const char   *regtype,	/* I - Type of service */
	    const char   *replyDomain)	/* I - Service domain */
{
  ippfind_srv_t	key,			/* Search key */
		*service;		/* Service */
  char		fullName[kDNSServiceMaxDomainName];
					/* Full name for query */


 /*
  * See if this is a new device...
  */

  key.name    = (char *)serviceName;
  key.regtype = (char *)regtype;

  for (service = cupsArrayFind(services, &key);
       service;
       service = cupsArrayNext(services))
    if (_cups_strcasecmp(service->name, key.name))
      break;
    else if (!strcmp(service->regtype, key.regtype))
      return (service);

 /*
  * Yes, add the service...
  */

  service           = calloc(sizeof(ippfind_srv_t), 1);
  service->name     = strdup(serviceName);
  service->domain   = strdup(replyDomain);
  service->regtype  = strdup(regtype);

  cupsArrayAdd(services, service);

 /*
  * Set the "full name" of this service, which is used for queries and
  * resolves...
  */

#ifdef HAVE_DNSSD
  DNSServiceConstructFullName(fullName, serviceName, regtype, replyDomain);
#else /* HAVE_AVAHI */
  avahi_service_name_join(fullName, kDNSServiceMaxDomainName, serviceName,
                          regtype, replyDomain);
#endif /* HAVE_DNSSD */

  service->fullName = strdup(fullName);

  return (service);
}


/*
 * 'get_time()' - Get the current time-of-day in seconds.
 */

static double
get_time(void)
{
#ifdef WIN32
  struct _timeb curtime;		/* Current Windows time */

  _ftime(&curtime);

  return (curtime.time + 0.001 * curtime.millitm);

#else
  struct timeval	curtime;	/* Current UNIX time */

  if (gettimeofday(&curtime, NULL))
    return (0.0);
  else
    return (curtime.tv_sec + 0.000001 * curtime.tv_usec);
#endif /* WIN32 */
}


/*
 * 'new_expr()' - Create a new expression.
 */

static ippfind_expr_t *			/* O - New expression */
new_expr(ippfind_op_t op,		/* I - Operation */
         int          invert,		/* I - Invert result? */
         const char   *key,		/* I - TXT key */
	 const char   *regex,		/* I - Regular expression */
	 char         **args)		/* I - Pointer to argument strings */
{
  ippfind_expr_t	*temp;		/* New expression */


  if ((temp = calloc(1, sizeof(ippfind_expr_t))) == NULL)
    return (NULL);

  temp->op = op;
  temp->invert = invert;
  temp->key    = (char *)key;

  if (regex)
  {
    int err = regcomp(&(temp->re), regex, REG_NOSUB | REG_EXTENDED);

    if (err)
    {
      char	message[256];		/* Error message */

      regerror(err, &(temp->re), message, sizeof(message));
      _cupsLangPrintf(stderr, _("ippfind: Bad regular expression: %s"),
                      message);
      exit(IPPFIND_EXIT_SYNTAX);
    }
  }

  if (args)
  {
    int	num_args;			/* Number of arguments */

    for (num_args = 1; args[num_args]; num_args ++)
      if (!strcmp(args[num_args], ";"))
        break;

     temp->args = malloc(num_args * sizeof(char *));
     memcpy(temp->args, args, num_args * sizeof(char *));
  }

  return (temp);
}


#ifdef HAVE_AVAHI
/*
 * 'poll_callback()' - Wait for input on the specified file descriptors.
 *
 * Note: This function is needed because avahi_simple_poll_iterate is broken
 *       and always uses a timeout of 0 (!) milliseconds.
 *       (Avahi Ticket #364)
 */

static int				/* O - Number of file descriptors matching */
poll_callback(
    struct pollfd *pollfds,		/* I - File descriptors */
    unsigned int  num_pollfds,		/* I - Number of file descriptors */
    int           timeout,		/* I - Timeout in milliseconds (unused) */
    void          *context)		/* I - User data (unused) */
{
  int	val;				/* Return value */


  (void)timeout;
  (void)context;

  val = poll(pollfds, num_pollfds, 500);

  if (val < 0)
    fprintf(stderr, "DEBUG: poll_callback: %s\n", strerror(errno));
  else if (val > 0)
    got_data = 1;

  return (val);
}
#endif /* HAVE_AVAHI */


/*
 * 'resolve_callback()' - Process resolve data.
 */

#ifdef HAVE_DNSSD
static void
resolve_callback(
    DNSServiceRef       sdRef,		/* I - Service reference */
    DNSServiceFlags     flags,		/* I - Data flags */
    uint32_t            interfaceIndex,	/* I - Interface */
    DNSServiceErrorType errorCode,	/* I - Error, if any */
    const char          *fullName,	/* I - Full service name */
    const char          *hostTarget,	/* I - Hostname */
    uint16_t            port,		/* I - Port number (network byte order) */
    uint16_t            txtLen,		/* I - Length of TXT record data */
    const unsigned char *txtRecord,	/* I - TXT record data */
    void                *context)	/* I - Service */
{
  char			key[256],	/* TXT key value */
			*value;		/* Value from TXT record */
  const unsigned char	*txtEnd;	/* End of TXT record */
  uint8_t		valueLen;	/* Length of value */
  ippfind_srv_t		*service = (ippfind_srv_t *)context;
					/* Service */


 /*
  * Only process "add" data...
  */

  if (errorCode != kDNSServiceErr_NoError)
  {
    _cupsLangPrintf(stderr, _("ippfind: Unable to browse or resolve: %s"),
		    dnssd_error_string(errorCode));
    bonjour_error = 1;
    return;
  }

  service->is_resolved = 1;
  service->host        = strdup(hostTarget);
  service->port        = ntohs(port);

 /*
  * Loop through the TXT key/value pairs and add them to an array...
  */

  for (txtEnd = txtRecord + txtLen; txtRecord < txtEnd; txtRecord += valueLen)
  {
   /*
    * Ignore bogus strings...
    */

    valueLen = *txtRecord++;

    memcpy(key, txtRecord, valueLen);
    key[valueLen] = '\0';

    if ((value = strchr(key, '=')) == NULL)
      continue;

    *value++ = '\0';

   /*
    * Add to array of TXT values...
    */

    service->num_txt = cupsAddOption(key, value, service->num_txt,
                                     &(service->txt));
  }

  set_service_uri(service);
}


#elif defined(HAVE_AVAHI)
static void
resolve_callback(
    AvahiServiceResolver   *resolver,	/* I - Resolver */
    AvahiIfIndex           interface,	/* I - Interface */
    AvahiProtocol          protocol,	/* I - Address protocol */
    AvahiBrowserEvent      event,	/* I - Event */
    const char             *serviceName,/* I - Service name */
    const char             *regtype,	/* I - Registration type */
    const char             *replyDomain,/* I - Domain name */
    const char             *hostTarget,	/* I - FQDN */
    uint16_t               port,	/* I - Port number */
    AvahiStringList        *txt,	/* I - TXT records */
    AvahiLookupResultFlags flags,	/* I - Lookup flags */
    void                   *context)	/* I - Service */
{
  char		uri[1024];		/* URI */
		key[256],		/* TXT key */
		*value;			/* TXT value */
  ippfind_srv_t	*service = (ippfind_srv_t *)context;
					/* Service */
  AvahiStringList *current;		/* Current TXT key/value pair */


  if (event != AVAHI_RESOLVER_FOUND)
  {
    bonjour_error = 1;

    avahi_service_resolver_free(resolver);
    avahi_simple_poll_quit(uribuf->poll);
    return;
  }

  service->is_resolved = 1;
  service->host        = strdup(hostTarget);
  service->port        = ntohs(port);

 /*
  * Loop through the TXT key/value pairs and add them to an array...
  */

  for (current = txt; current; current = current->next)
  {
   /*
    * Ignore bogus strings...
    */

    if (current->size > (sizeof(key) - 1))
      continue;

    memcpy(key, current->text, current->size);
    key[current->size] = '\0';

    if ((value = strchr(key, '=')) == NULL)
      continue;

    *value++ = '\0';

   /*
    * Add to array of TXT values...
    */

    service->num_txt = cupsAddOption(key, value, service->num_txt,
                                     &(service->txt));
  }

  set_service_uri(service);
}
#endif /* HAVE_DNSSD */


/*
 * 'set_service_uri()' - Set the URI of the service.
 */

static void
set_service_uri(ippfind_srv_t *service)	/* I - Service */
{
  char		uri[1024];		/* URI */
  const char	*path,			/* Resource path */
		*scheme;		/* URI scheme */


  if (!strncmp(service->regtype, "_http.", 6))
  {
    scheme = "http";
    path   = cupsGetOption("path", service->num_txt, service->txt);
  }
  else if (!strncmp(service->regtype, "_https.", 7))
  {
    scheme = "https";
    path   = cupsGetOption("path", service->num_txt, service->txt);
  }
  else if (!strncmp(service->regtype, "_ipp.", 5))
  {
    scheme = "ipp";
    path   = cupsGetOption("rp", service->num_txt, service->txt);
  }
  else if (!strncmp(service->regtype, "_ipps.", 6))
  {
    scheme = "ipps";
    path   = cupsGetOption("rp", service->num_txt, service->txt);
  }
  else if (!strncmp(service->regtype, "_printer.", 9))
  {
    scheme = "lpd";
    path   = cupsGetOption("rp", service->num_txt, service->txt);
  }
  else
    return;

  if (!path || !*path)
    path = "/";

  if (*path == '/')
    httpAssembleURI(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, NULL,
                    service->host, service->port, path);
  else
    httpAssembleURIf(HTTP_URI_CODING_ALL, uri, sizeof(uri), scheme, NULL,
                     service->host, service->port, "/%s", path);

  service->uri = strdup(uri);
}


/*
 * 'show_usage()' - Show program usage.
 */

static void
show_usage(void)
{
  _cupsLangPuts(stderr, _("Usage: ippfind [options] regtype[,subtype]"
                          "[.domain.] ... [expression]\n"
                          "       ippfind [options] name[.regtype[.domain.]] "
                          "... [expression]\n"
                          "       ippfind --help\n"
                          "       ippfind --version"));
  _cupsLangPuts(stderr, "");
  _cupsLangPuts(stderr, _("Options:"));
  _cupsLangPuts(stderr, _("  -4                      Connect using IPv4."));
  _cupsLangPuts(stderr, _("  -6                      Connect using IPv6."));
  _cupsLangPuts(stderr, _("  -T seconds              Set the browse timeout in "
                          "seconds."));
  _cupsLangPuts(stderr, _("  -V version              Set default IPP "
                          "version."));
  _cupsLangPuts(stderr, _("  --help                  Show this help."));
  _cupsLangPuts(stderr, _("  --version               Show program version."));
  _cupsLangPuts(stderr, "");
  _cupsLangPuts(stderr, _("Expressions:"));
  _cupsLangPuts(stderr, _("  -d regex                Match domain to regular expression."));
  _cupsLangPuts(stderr, _("  -e utility [argument ...] ;\n"
                          "                          Execute program if true."));
  _cupsLangPuts(stderr, _("  -l                      List attributes."));
  _cupsLangPuts(stderr, _("  -n regex                Match service name to regular expression."));
  _cupsLangPuts(stderr, _("  -p                      Print URI if true."));
  _cupsLangPuts(stderr, _("  -q                      Quietly report match via exit code."));
  _cupsLangPuts(stderr, _("  -r                      True if service is remote."));
  _cupsLangPuts(stderr, _("  -s                      Print service name if true."));
  _cupsLangPuts(stderr, _("  -t key                  True if the TXT record contains the key."));
  _cupsLangPuts(stderr, _("  -u regex                Match URI to regular expression."));
  _cupsLangPuts(stderr, _("  --domain regex          Match domain to regular expression."));
  _cupsLangPuts(stderr, _("  --exec utility [argument ...] ;\n"
                          "                          Execute program if true."));
  _cupsLangPuts(stderr, _("  --ls                    List attributes."));
  _cupsLangPuts(stderr, _("  --local                 True if service is local."));
  _cupsLangPuts(stderr, _("  --name regex            Match service name to regular expression."));
  _cupsLangPuts(stderr, _("  --path regex            Match resource path to regular expression."));
  _cupsLangPuts(stderr, _("  --print                 Print URI if true."));
  _cupsLangPuts(stderr, _("  --print-name            Print service name if true."));
  _cupsLangPuts(stderr, _("  --quiet                 Quietly report match via exit code."));
  _cupsLangPuts(stderr, _("  --remote                True if service is remote."));
  _cupsLangPuts(stderr, _("  --txt key               True if the TXT record contains the key."));
  _cupsLangPuts(stderr, _("  --txt-* regex           Match TXT record key to regular expression."));
  _cupsLangPuts(stderr, _("  --uri regex             Match URI to regular expression."));
  _cupsLangPuts(stderr, "");
  _cupsLangPuts(stderr, _("Modifiers:"));
  _cupsLangPuts(stderr, _("  ( expressions )         Group expressions."));
  _cupsLangPuts(stderr, _("  ! expression            Unary NOT of expression."));
  _cupsLangPuts(stderr, _("  --not expression        Unary NOT of expression."));
  _cupsLangPuts(stderr, _("  --false                 Always false."));
  _cupsLangPuts(stderr, _("  --true                  Always true."));
  _cupsLangPuts(stderr, _("  expression expression   Logical AND."));
  _cupsLangPuts(stderr, _("  expression --and expression\n"
                          "                          Logical AND."));
  _cupsLangPuts(stderr, _("  expression --or expression\n"
                          "                          Logical OR."));
  _cupsLangPuts(stderr, "");
  _cupsLangPuts(stderr, _("Substitutions:"));
  _cupsLangPuts(stderr, _("  {}                      URI"));
  _cupsLangPuts(stderr, _("  {service_domain}        Domain name"));
  _cupsLangPuts(stderr, _("  {service_hostname}      Fully-qualified domain name"));
  _cupsLangPuts(stderr, _("  {service_name}          Service instance name"));
  _cupsLangPuts(stderr, _("  {service_port}          Port number"));
  _cupsLangPuts(stderr, _("  {service_regtype}       DNS-SD registration type"));
  _cupsLangPuts(stderr, _("  {service_scheme}        URI scheme"));
  _cupsLangPuts(stderr, _("  {service_uri}           URI"));
  _cupsLangPuts(stderr, _("  {txt_*}                 Value of TXT record key"));
  _cupsLangPuts(stderr, "");
  _cupsLangPuts(stderr, _("Environment Variables:"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_DOMAIN  Domain name"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_HOSTNAME\n"
                          "                          Fully-qualified domain name"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_NAME    Service instance name"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_PORT    Port number"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_REGTYPE DNS-SD registration type"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_SCHEME  URI scheme"));
  _cupsLangPuts(stderr, _("  IPPFIND_SERVICE_URI     URI"));
  _cupsLangPuts(stderr, _("  IPPFIND_TXT_*           Value of TXT record key"));

  exit(IPPFIND_EXIT_TRUE);
}


/*
 * 'show_version()' - Show program version.
 */

static void
show_version(void)
{
  _cupsLangPuts(stderr, CUPS_SVERSION);

  exit(IPPFIND_EXIT_TRUE);
}


/*
 * End of "$Id$".
 */
