/*
 * "$Id: mime.h 4970 2006-01-24 14:05:45Z mike $"
 *
 *   MIME type/conversion database definitions for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2006 by Easy Software Products, all rights reserved.
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

#ifndef _CUPS_MIME_H_
#  define _CUPS_MIME_H_

#  include <cups/array.h>
#  include <cups/ipp.h>
#  include <cups/file.h>


/*
 * C++ magic...
 */

#  ifdef _cplusplus
extern "C" {
#  endif /* _cplusplus */


/*
 * Constants...
 */

#  define MIME_MAX_SUPER	16		/* Maximum size of supertype name */
#  define MIME_MAX_TYPE		IPP_MAX_NAME	/* Maximum size of type name */
#  define MIME_MAX_FILTER	256		/* Maximum size of filter pathname */
#  define MIME_MAX_BUFFER	8192		/* Maximum size of file buffer */


/*
 * Types/structures...
 */

typedef enum
{
  MIME_MAGIC_NOP,			/* No operation */
  MIME_MAGIC_AND,			/* Logical AND of all children */
  MIME_MAGIC_OR,			/* Logical OR of all children */
  MIME_MAGIC_MATCH,			/* Filename match */
  MIME_MAGIC_ASCII,			/* ASCII characters in range */
  MIME_MAGIC_PRINTABLE,			/* Printable characters (32-255) in range */
  MIME_MAGIC_STRING,			/* String matches */
  MIME_MAGIC_CHAR,			/* Character/byte matches */
  MIME_MAGIC_SHORT,			/* Short/16-bit word matches */
  MIME_MAGIC_INT,			/* Integer/32-bit word matches */
  MIME_MAGIC_LOCALE,			/* Current locale matches string */
  MIME_MAGIC_CONTAINS,			/* File contains a string */
  MIME_MAGIC_ISTRING			/* Case-insensitive string matches */
} mime_op_t;

typedef struct mime_magic_str		/**** MIME Magic Data ****/
{
  struct mime_magic_str	*prev,		/* Previous rule */
			*next,		/* Next rule */
			*parent,	/* Parent rules */
			*child;		/* Child rules */
  short		op,			/* Operation code (see above) */
		invert;			/* Invert the result */
  int		offset,			/* Offset in file */
		region,			/* Region length */
		length;			/* Length of data */
  union
  {
    char	matchv[64];		/* Match value */
    char	localev[64];		/* Locale value */
    char	stringv[64];		/* String value */
    char	charv;			/* Byte value */
    short	shortv;			/* Short value */
    int		intv;			/* Integer value */
  }		value;
} mime_magic_t;

typedef struct				/**** MIME Type Data ****/
{
  mime_magic_t	*rules;			/* Rules used to detect this type */
  char		super[MIME_MAX_SUPER],	/* Super-type name ("image", "application", etc.) */
		type[MIME_MAX_TYPE];	/* Type name ("png", "postscript", etc.) */
} mime_type_t;

typedef struct				/**** MIME Conversion Filter Data ****/
{
  mime_type_t	*src,			/* Source type */
		*dst;			/* Destination type */
  int		cost;			/* Relative cost */
  char		filter[MIME_MAX_FILTER];/* Filter program to use */
} mime_filter_t;

typedef struct				/**** MIME Database ****/
{
  cups_array_t	*types;			/* File types */
  cups_array_t	*filters;		/* Type conversion filters */
} mime_t;


/*
 * Functions...
 */

extern void		mimeDelete(mime_t *mime);
extern mime_t		*mimeLoad(const char *pathname, const char *filterpath);
extern mime_t		*mimeMerge(mime_t *mime, const char *pathname,
			           const char *filterpath);
extern mime_t		*mimeNew(void);

extern mime_type_t	*mimeAddType(mime_t *mime, const char *super,
			             const char *type);
extern int		mimeAddTypeRule(mime_type_t *mt, const char *rule);
extern void		mimeDeleteType(mime_t *mime, mime_type_t *mt);
extern mime_type_t	*mimeFileType(mime_t *mime, const char *pathname,
			              int *compression);
extern mime_type_t	*mimeFirstType(mime_t *mime);
extern mime_type_t	*mimeNextType(mime_t *mime);
extern int		mimeNumTypes(mime_t *mime);
extern mime_type_t	*mimeType(mime_t *mime, const char *super,
				  const char *type);

extern mime_filter_t	*mimeAddFilter(mime_t *mime, mime_type_t *src,
			               mime_type_t *dst, int cost,
				       const char *filter);
extern void		mimeDeleteFilter(mime_t *mime, mime_filter_t *filter);
extern cups_array_t	*mimeFilter(mime_t *mime, mime_type_t *src,
			            mime_type_t *dst, int *cost, int max_depth);
extern mime_filter_t	*mimeFirstFilter(mime_t *mime);
extern mime_filter_t	*mimeNextFilter(mime_t *mime);
extern int		mimeNumFilters(mime_t *mime);

#  ifdef _cplusplus
}
#  endif /* _cplusplus */
#endif /* !_CUPS_MIME_H_ */

/*
 * End of "$Id: mime.h 4970 2006-01-24 14:05:45Z mike $".
 */
