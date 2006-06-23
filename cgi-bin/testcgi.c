/*
 * "$Id: testcgi.c 177 2006-06-21 00:20:03Z jlovell $"
 *
 *   CGI test program for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2005 by Easy Software Products.
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
 *
 * Contents:
 *
 *   main()       - Test the help index code.
 *   list_nodes() - List nodes in an array...
 */

/*
 * Include necessary headers...
 */

#include "cgi.h"


/*
 * 'main()' - Test the CGI code.
 */

int					/* O - Exit status */
main(int  argc,				/* I - Number of command-line arguments */
     char *argv[])			/* I - Command-line arguments */
{
 /*
  * Test file upload/multi-part submissions...
  */

  freopen("multipart.dat", "rb", stdin);

  putenv("CONTENT_TYPE=multipart/form-data; "
         "boundary=---------------------------1977426492562745908748943111");
  putenv("REQUEST_METHOD=POST");

  printf("cgiInitialize: ");
  if (cgiInitialize())
  {
    const cgi_file_t	*file;		/* Upload file */

    if ((file = cgiGetFile()) != NULL)
    {
      puts("PASS");
      printf("    tempfile=\"%s\"\n", file->tempfile);
      printf("    name=\"%s\"\n", file->name);
      printf("    filename=\"%s\"\n", file->filename);
      printf("    mimetype=\"%s\"\n", file->mimetype);
    }
    else
      puts("FAIL (no file!)");
  }
  else
    puts("FAIL (init)");
    
 /*
  * Return with no errors...
  */

  return (0);
}


/*
 * End of "$Id: testcgi.c 177 2006-06-21 00:20:03Z jlovell $".
 */
