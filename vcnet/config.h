/*
 * "$Id: config.h 6189 2007-01-10 16:30:34Z mike $"
 *
 *   Configuration file for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2007 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636 USA
 *
 *       Voice: (301) 373-9600
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 */

#ifndef _CUPS_CONFIG_H_
#define _CUPS_CONFIG_H_

/*
 * Compiler stuff...
 */

#undef const
#undef __CHAR_UNSIGNED__


/*
 * Version of software...
 */

#define CUPS_SVERSION		"CUPS v1.2.8"
#define CUPS_MINIMAL		"CUPS/1.2.8"


/*
 * Default user and groups...
 */

#define CUPS_DEFAULT_USER "lp"
#define CUPS_DEFAULT_GROUP "lp"
#define CUPS_DEFAULT_SYSTEM_GROUPS "sys root"


/*
 * Default file permissions...
 */

#define CUPS_DEFAULT_CONFIG_FILE_PERM 0640
#define CUPS_DEFAULT_LOG_FILE_PERM 0644


/*
 * Default browsing settings...
 */

#define CUPS_DEFAULT_BROWSING 1
#define CUPS_DEFAULT_BROWSE_LOCAL_PROTOCOLS "CUPS"
#define CUPS_DEFAULT_BROWSE_REMOTE_PROTOCOLS "CUPS"
#define CUPS_DEFAULT_BROWSE_SHORT_NAMES 1
#define CUPS_DEFAULT_DEFAULT_SHARED 1
#define CUPS_DEFAULT_IMPLICIT_CLASSES 1
#define CUPS_DEFAULT_USE_NETWORK_DEFAULT 1


/*
 * Default IPP port...
 */

#define CUPS_DEFAULT_IPP_PORT	631


/*
 * Maximum number of file descriptors to support.
 */

#define CUPS_MAX_FDS		4096


/*
 * Do we have domain socket support?
 */

#undef CUPS_DEFAULT_DOMAINSOCKET


/*
 * Where are files stored?
 *
 * Note: These are defaults, which can be overridden by environment
 *       variables at run-time...
 */

#define CUPS_CACHEDIR	"C:/CUPS/cache"
#define CUPS_DATADIR    "C:/CUPS/share"
#define CUPS_DOCROOT	"C:/CUPS/share/doc"
#define CUPS_FONTPATH	"C:/CUPS/share/fonts"
#define CUPS_LOCALEDIR	"C:/CUPS/locale"
#define CUPS_LOGDIR	"C:/CUPS/logs"
#define CUPS_REQUESTS	"C:/CUPS/spool"
#define CUPS_SERVERBIN	"C:/CUPS/lib"
#define CUPS_SERVERROOT	"C:/CUPS/etc"
#define CUPS_STATEDIR	"C:/CUPS/run"


/*
 * Do we have various image libraries?
 */

#undef HAVE_LIBPNG
#undef HAVE_LIBZ
#undef HAVE_LIBJPEG
#undef HAVE_LIBTIFF


/*
 * Do we have PAM stuff?
 */

#ifndef HAVE_LIBPAM
#define HAVE_LIBPAM 0
#endif /* !HAVE_LIBPAM */

#undef HAVE_PAM_PAM_APPL_H


/*
 * Do we have <shadow.h>?
 */

#undef HAVE_SHADOW_H


/*
 * Do we have <crypt.h>?
 */

#undef HAVE_CRYPT_H


/*
 * Do we have <scsi/sg.h>?
 */

#undef HAVE_SCSI_SG_H


/*
 * Use <string.h>, <strings.h>, and/or <bstring.h>?
 */

#define HAVE_STRING_H
#undef HAVE_STRINGS_H
#undef HAVE_BSTRING_H


/*
 * Do we have the long long type?
 */

#undef HAVE_LONG_LONG

#ifdef HAVE_LONG_LONG
#  define CUPS_LLFMT	"%lld"
#  define CUPS_LLCAST	(long long)
#else
#  define CUPS_LLFMT	"%ld"
#  define CUPS_LLCAST	(long)
#endif /* HAVE_LONG_LONG */


/*
 * Do we have the strtoll() function?
 */

#undef HAVE_STRTOLL

#ifndef HAVE_STRTOLL
#  define strtoll(nptr,endptr,base) strtol((nptr), (endptr), (base))
#endif /* !HAVE_STRTOLL */


/*
 * Do we have the strXXX() functions?
 */

#define HAVE_STRDUP
#define HAVE_STRCASECMP
#define HAVE_STRNCASECMP
#undef HAVE_STRLCAT
#undef HAVE_STRLCPY


/*
 * Do we have the geteuid() function?
 */

#undef HAVE_GETEUID


/*
 * Do we have the vsyslog() function?
 */

#undef HAVE_VSYSLOG


/*
 * Do we have the (v)snprintf() functions?
 */

#undef HAVE_SNPRINTF
#undef HAVE_VSNPRINTF


/*
 * What signal functions to use?
 */

#undef HAVE_SIGSET
#undef HAVE_SIGACTION


/*
 * What wait functions to use?
 */

#undef HAVE_WAITPID
#undef HAVE_WAIT3


/*
 * Do we have the mallinfo function and malloc.h?
 */

#undef HAVE_MALLINFO
#undef HAVE_MALLOC_H


/*
 * Do we have the POSIX ACL functions?
 */

#undef HAVE_ACL_INIT


/*
 * Do we have the langinfo.h header file?
 */

#undef HAVE_LANGINFO_H


/*
 * Which encryption libraries do we have?
 */

#undef HAVE_CDSASSL
#undef HAVE_GNUTLS
#define HAVE_LIBSSL 1
#define HAVE_SSL 1


/*
 * Do we have the OpenSLP library?
 */

#undef HAVE_LIBSLP


/*
 * Do we have an LDAP library?
 */

#undef HAVE_LDAP
#undef HAVE_OPENLDAP


/*
 * Do we have libpaper?
 */

#undef HAVE_LIBPAPER


/*
 * Do we have <sys/ioctl.h>?
 */

#undef HAVE_SYS_IOCTL_H


/*
 * Do we have mkstemp() and/or mkstemps()?
 */

#undef HAVE_MKSTEMP
#undef HAVE_MKSTEMPS


/*
 * Does the "tm" structure contain the "tm_gmtoff" member?
 */

#undef HAVE_TM_GMTOFF


/*
 * Do we have rresvport_af()?
 */

#undef HAVE_RRESVPORT_AF


/*
 * Do we have getaddrinfo()?
 */

#define HAVE_GETADDRINFO


/*
 * Do we have getnameinfo()?
 */

#define HAVE_GETNAMEINFO


/*
 * Do we have getifaddrs()?
 */

#undef HAVE_GETIFADDRS


/*
 * Do we have hstrerror()?
 */

#undef HAVE_HSTRERROR


/*
 * Do we have the <sys/sockio.h> header file?
 */

#undef HAVE_SYS_SOCKIO_H


/*
 * Does the sockaddr structure contain an sa_len parameter?
 */

#undef HAVE_STRUCT_SOCKADDR_SA_LEN


/*
 * Do we have the AIX usersec.h header file?
 */

#undef HAVE_USERSEC_H

/*
 * Do we have pthread support?
 */

#undef HAVE_PTHREAD_H


/*
 * Do we have launchd support?
 */

/* #undef HAVE_LAUNCH_H */
/* #undef HAVE_LAUNCHD */
#define CUPS_DEFAULT_LAUNCHD_CONF ""


/*
 * Various scripting languages...
 */

#undef HAVE_JAVA
#define CUPS_JAVA	"/usr/bin/java"
#undef HAVE_PERL
#define CUPS_PERL	"/usr/bin/perl"
#undef HAVE_PHP
#define CUPS_PHP	"/usr/bin/php"
#undef HAVE_PYTHON
#define CUPS_PYTHON	"/usr/bin/python"


/*
 * Do we have Darwin's CoreFoundation and SystemConfiguration frameworks?
 */

/* #undef HAVE_COREFOUNDATION */
/* #undef HAVE_SYSTEMCONFIGURATION */


/*
 * Do we have CoreFoundation public and private headers?
 */

/* #undef HAVE_COREFOUNDATION_H */
/* #undef HAVE_CFPRIV_H */
/* #undef HAVE_CFBUNDLEPRIV_H */


/*
 * Do we have CFLocaleCreateCanonicalLocaleIdentifierFromString()?
 */

/* #undef HAVE_CF_LOCALE_ID */


/*
 * Do we have MacOSX 10.4's mbr_XXX functions()?
 */

/* #undef HAVE_MEMBERSHIP_H */
/* #undef HAVE_MBR_UID_TO_UUID */


/*
 * Do we have Darwin's notify_post() header and function?
 */

/* #undef HAVE_NOTIFY_H */
/* #undef HAVE_NOTIFY_POST */


/*
 * Do we have DBUS?
 */

/* #undef HAVE_DBUS */
/* #undef HAVE_DBUS_MESSAGE_ITER_INIT_APPEND */


/*
 * Do we have the AppleTalk/at_proto.h header?
 */

/* #undef HAVE_APPLETALK_AT_PROTO_H */


#endif /* !_CUPS_CONFIG_H_ */

/*
 * End of "$Id: config.h 6189 2007-01-10 16:30:34Z mike $".
 */
