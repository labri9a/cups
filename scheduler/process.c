/*
 * "$Id: process.c 7256 2008-01-25 00:48:54Z mike $"
 *
 *   Process management routines for the CUPS scheduler.
 *
 *   Copyright 2007-2011 by Apple Inc.
 *   Copyright 1997-2007 by Easy Software Products, all rights reserved.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 * Contents:
 *
 *   cupsdCreateProfile()  - Create an execution profile for a subprocess.
 *   cupsdDestroyProfile() - Delete an execution profile.
 *   cupsdEndProcess()     - End a process.
 *   cupsdFinishProcess()  - Finish a process and get its name.
 *   cupsdStartProcess()   - Start a process.
 *   compare_procs()       - Compare two processes.
 *   cupsd_requote()       - Make a regular-expression version of a string.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#include <grp.h>
#ifdef __APPLE__
#  include <libgen.h>
#endif /* __APPLE__ */


/*
 * Process structure...
 */

typedef struct
{
  int	pid,				/* Process ID */
	job_id;				/* Job associated with process */
  char	name[1];			/* Name of process */
} cupsd_proc_t;


/*
 * Local globals...
 */

static cups_array_t	*process_array = NULL;


/*
 * Local functions...
 */

static int	compare_procs(cupsd_proc_t *a, cupsd_proc_t *b);
#ifdef HAVE_SANDBOX_H
static char	*cupsd_requote(char *dst, const char *src, size_t dstsize);
#endif /* HAVE_SANDBOX_H */


/*
 * 'cupsdCreateProfile()' - Create an execution profile for a subprocess.
 */

void *					/* O - Profile or NULL on error */
cupsdCreateProfile(int job_id)		/* I - Job ID or 0 for none */
{
#ifdef HAVE_SANDBOX_H
  cups_file_t	*fp;			/* File pointer */
  char		profile[1024],		/* File containing the profile */
		cache[1024],		/* Quoted CacheDir */
		request[1024],		/* Quoted RequestRoot */
		root[1024],		/* Quoted ServerRoot */
		temp[1024];		/* Quoted TempDir */


  if (!UseProfiles)
  {
   /*
    * Only use sandbox profiles as root...
    */

    cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdCreateProfile(job_id=%d) = NULL",
                    job_id);

    return (NULL);
  }

  if ((fp = cupsTempFile2(profile, sizeof(profile))) == NULL)
  {
    cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdCreateProfile(job_id=%d) = NULL",
                    job_id);
    cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to create security profile: %s",
                    strerror(errno));
    return (NULL);
  }

  fchown(cupsFileNumber(fp), RunUser, Group);
  fchmod(cupsFileNumber(fp), 0640);

  cupsd_requote(cache, CacheDir, sizeof(cache));
  cupsd_requote(request, RequestRoot, sizeof(request));
  cupsd_requote(root, ServerRoot, sizeof(root));
  cupsd_requote(temp, TempDir, sizeof(temp));

  cupsFilePuts(fp, "(version 1)\n");
  if (LogLevel >= CUPSD_LOG_DEBUG)
    cupsFilePuts(fp, "(debug deny)\n");
  cupsFilePuts(fp, "(allow default)\n");
  cupsFilePrintf(fp,
                 "(deny file-write* file-read-data file-read-metadata\n"
                 "  (regex"
		 " #\"^%s$\""		/* RequestRoot */
		 " #\"^%s/\""		/* RequestRoot/... */
		 "))\n",
		 request, request);
  if (!RunUser)
    cupsFilePuts(fp,
		 "(deny file-write* file-read-data file-read-metadata\n"
		 "  (regex"
		 " #\"^/Users$\""
		 " #\"^/Users/\""
		 "))\n");
  cupsFilePrintf(fp,
                 "(deny file-write*\n"
                 "  (regex"
		 " #\"^%s$\""		/* ServerRoot */
		 " #\"^%s/\""		/* ServerRoot/... */
		 " #\"^/private/etc$\""
		 " #\"^/private/etc/\""
		 " #\"^/usr/local/etc$\""
		 " #\"^/usr/local/etc/\""
		 " #\"^/Library$\""
		 " #\"^/Library/\""
		 " #\"^/System$\""
		 " #\"^/System/\""
		 "))\n",
		 root, root);
  /* Specifically allow applications to stat RequestRoot */
  cupsFilePrintf(fp,
                 "(allow file-read-metadata\n"
                 "  (regex"
		 " #\"^%s$\""		/* RequestRoot */
		 "))\n",
		 request);
  cupsFilePrintf(fp,
                 "(allow file-write* file-read-data file-read-metadata\n"
                 "  (regex"
		 " #\"^%s$\""		/* TempDir */
		 " #\"^%s/\""		/* TempDir/... */
		 " #\"^%s$\""		/* CacheDir */
		 " #\"^%s/\""		/* CacheDir/... */
		 " #\"^%s/Library$\""	/* RequestRoot/Library */
		 " #\"^%s/Library/\""	/* RequestRoot/Library/... */
		 " #\"^/Library/Application Support/\""
		 " #\"^/Library/Caches/\""
		 " #\"^/Library/Preferences/\""
		 " #\"^/Library/Printers/.*/\""
		 " #\"^/Users/Shared/\""
		 "))\n",
		 temp, temp, cache, cache, request, request);
  cupsFilePuts(fp,
	       "(deny file-write*\n"
	       "  (regex"
	       " #\"^/Library/Printers/PPDs$\""
	       " #\"^/Library/Printers/PPDs/\""
	       " #\"^/Library/Printers/PPD Plugins$\""
	       " #\"^/Library/Printers/PPD Plugins/\""
	       "))\n");
  if (job_id)
  {
   /*
    * Allow job filters to read the spool file(s)...
    */

    cupsFilePrintf(fp,
                   "(allow file-read-data file-read-metadata\n"
                   "  (regex #\"^%s/([ac]%05d|d%05d-[0-9][0-9][0-9])$\"))\n",
		   request, job_id, job_id);
  }
  else
  {
   /*
    * Allow email notifications from notifiers...
    */

    cupsFilePuts(fp,
		 "(allow process-exec\n"
		 "  (literal \"/usr/sbin/sendmail\")\n"
		 "  (with no-sandbox)\n"
		 ")\n");
  }

  cupsFileClose(fp);

  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdCreateProfile(job_id=%d) = \"%s\"",
                  job_id, profile);
  return ((void *)strdup(profile));

#else
  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdCreateProfile(job_id=%d) = NULL",
                  job_id);

  return (NULL);
#endif /* HAVE_SANDBOX_H */
}


/*
 * 'cupsdDestroyProfile()' - Delete an execution profile.
 */

void
cupsdDestroyProfile(void *profile)	/* I - Profile */
{
  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdDeleteProfile(profile=\"%s\")",
		  profile ? (char *)profile : "(null)");

#ifdef HAVE_SANDBOX_H
  if (profile)
  {
    unlink((char *)profile);
    free(profile);
  }
#endif /* HAVE_SANDBOX_H */
}


/*
 * 'cupsdEndProcess()' - End a process.
 */

int					/* O - 0 on success, -1 on failure */
cupsdEndProcess(int pid,		/* I - Process ID */
                int force)		/* I - Force child to die */
{
  cupsdLogMessage(CUPSD_LOG_DEBUG2, "cupsdEndProcess(pid=%d, force=%d)", pid,
                  force);

  if (!pid)
    return (0);
  else if (force)
    return (kill(pid, SIGKILL));
  else
    return (kill(pid, SIGTERM));
}


/*
 * 'cupsdFinishProcess()' - Finish a process and get its name.
 */

const char *				/* O - Process name */
cupsdFinishProcess(int  pid,		/* I - Process ID */
                   char *name,		/* I - Name buffer */
		   int  namelen,	/* I - Size of name buffer */
		   int  *job_id)	/* O - Job ID pointer or NULL */
{
  cupsd_proc_t	key,			/* Search key */
		*proc;			/* Matching process */


  key.pid = pid;

  if ((proc = (cupsd_proc_t *)cupsArrayFind(process_array, &key)) != NULL)
  {
    if (job_id)
      *job_id = proc->job_id;

    strlcpy(name, proc->name, namelen);
    cupsArrayRemove(process_array, proc);
    free(proc);
  }
  else
  {
    if (job_id)
      *job_id = 0;

    strlcpy(name, "unknown", namelen);
  }

  cupsdLogMessage(CUPSD_LOG_DEBUG2,
		  "cupsdFinishProcess(pid=%d, name=%p, namelen=%d, "
		  "job_id=%p(%d)) = \"%s\"", pid, name, namelen, job_id,
		  job_id ? *job_id : 0, name);

  return (name);
}


/*
 * 'cupsdStartProcess()' - Start a process.
 */

int					/* O - Process ID or 0 */
cupsdStartProcess(
    const char  *command,		/* I - Full path to command */
    char        *argv[],		/* I - Command-line arguments */
    char        *envp[],		/* I - Environment */
    int         infd,			/* I - Standard input file descriptor */
    int         outfd,			/* I - Standard output file descriptor */
    int         errfd,			/* I - Standard error file descriptor */
    int         backfd,			/* I - Backchannel file descriptor */
    int         sidefd,			/* I - Sidechannel file descriptor */
    int         root,			/* I - Run as root? */
    void        *profile,		/* I - Security profile to use */
    cupsd_job_t *job,			/* I - Job associated with process */
    int         *pid)			/* O - Process ID */
{
  int		i;			/* Looping var */
  const char	*exec_path = command;	/* Command to be exec'd */
  char		*real_argv[103],	/* Real command-line arguments */
		cups_exec[1024];	/* Path to "cups-exec" program */
  int		user;			/* Command UID */
  struct stat	commandinfo;		/* Command file information */
  cupsd_proc_t	*proc;			/* New process record */
#if defined(HAVE_SIGACTION) && !defined(HAVE_SIGSET)
  struct sigaction action;		/* POSIX signal handler */
#endif /* HAVE_SIGACTION && !HAVE_SIGSET */
#if defined(__APPLE__)
  char		processPath[1024],	/* CFProcessPath environment variable */
		linkpath[1024];		/* Link path for symlinks... */
  int		linkbytes;		/* Bytes for link path */
#endif /* __APPLE__ */


 /*
  * Figure out the UID for the child process...
  */

  if (RunUser)
    user = RunUser;
  else if (root)
    user = 0;
  else
    user = User;

 /*
  * Check the permissions of the command we are running...
  */

  if (stat(command, &commandinfo))
  {
    *pid = 0;

    cupsdLogMessage(CUPSD_LOG_DEBUG2,
		    "cupsdStartProcess(command=\"%s\", argv=%p, envp=%p, "
		    "infd=%d, outfd=%d, errfd=%d, backfd=%d, sidefd=%d, root=%d, "
		    "profile=%p, job=%p(%d), pid=%p) = %d",
		    command, argv, envp, infd, outfd, errfd, backfd, sidefd,
		    root, profile, job, job ? job->id : 0, pid, *pid);
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "%s%s \"%s\" not available: %s",
		    job && job->printer ? job->printer->name : "",
		    job && job->printer ? ": Printer driver" : "Program",
		    command, strerror(errno));

    if (job && job->printer)
    {
      if (cupsdSetPrinterReasons(job->printer, "+cups-missing-filter-warning"))
	cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, job->printer, NULL,
		      "Printer driver \"%s\" not available.", command);
    }

    return (0);
  }
  else if (!RunUser &&
           ((commandinfo.st_mode & (S_ISUID | S_IWOTH)) ||
            commandinfo.st_uid))
  {
    *pid = 0;

    cupsdLogMessage(CUPSD_LOG_DEBUG2,
		    "cupsdStartProcess(command=\"%s\", argv=%p, envp=%p, "
		    "infd=%d, outfd=%d, errfd=%d, backfd=%d, sidefd=%d, root=%d, "
		    "profile=%p, job=%p(%d), pid=%p) = %d",
		    command, argv, envp, infd, outfd, errfd, backfd, sidefd,
		    root, profile, job, job ? job->id : 0, pid, *pid);
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "%s%s \"%s\" has insecure permissions (%d/0%o).",
		    job && job->printer ? job->printer->name : "",
		    job && job->printer ? ": Printer driver" : "Program",
		    command, (int)commandinfo.st_uid, commandinfo.st_mode);

    if (job && job->printer)
    {
      if (cupsdSetPrinterReasons(job->printer, "+cups-insecure-filter-warning"))
	cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, job->printer, NULL,
		      "Printer driver \"%s\" has insecure permissions "
		      "(%d/0%o).", command,
		      (int)commandinfo.st_uid, commandinfo.st_mode);
    }

    errno = EPERM;

    return (0);
  }
  else if ((commandinfo.st_uid != user || !(commandinfo.st_mode & S_IXUSR)) &&
           (commandinfo.st_gid != Group || !(commandinfo.st_mode & S_IXGRP)) &&
           !(commandinfo.st_mode & S_IXOTH))
  {
    *pid = 0;

    cupsdLogMessage(CUPSD_LOG_DEBUG2,
		    "cupsdStartProcess(command=\"%s\", argv=%p, envp=%p, "
		    "infd=%d, outfd=%d, errfd=%d, backfd=%d, sidefd=%d, root=%d, "
		    "profile=%p, job=%p(%d), pid=%p) = %d",
		    command, argv, envp, infd, outfd, errfd, backfd, sidefd,
		    root, profile, job, job ? job->id : 0, pid, *pid);
    cupsdLogMessage(CUPSD_LOG_ERROR,
                    "%s%s \"%s\" does not have execute permissions (%d/0%o).",
		    job && job->printer ? job->printer->name : "",
		    job && job->printer ? ": Printer driver" : "Program",
		    command, (int)commandinfo.st_uid, commandinfo.st_mode);

    errno = EPERM;
    return (0);
  }
  else if (!RunUser && (commandinfo.st_mode & S_IWGRP))
  {
    cupsdLogMessage(CUPSD_LOG_WARN,
                    "%s%s \"%s\" has insecure permissions (%d/0%o).",
		    job && job->printer ? job->printer->name : "",
		    job && job->printer ? ": Printer driver" : "Program",
		    command, (int)commandinfo.st_uid, commandinfo.st_mode);

    if (job && job->printer)
    {
      if (cupsdSetPrinterReasons(job->printer, "+cups-insecure-filter-warning"))
	cupsdAddEvent(CUPSD_EVENT_PRINTER_STATE, job->printer, NULL,
		      "Printer driver \"%s\" has insecure permissions "
		      "(%d/0%o).", command, (int)commandinfo.st_uid,
		      commandinfo.st_mode);
    }
  }

#if defined(__APPLE__)
  if (envp)
  {
   /*
    * Add special voodoo magic for Mac OS X - this allows Mac OS X
    * programs to access their bundle resources properly...
    */

    if ((linkbytes = readlink(command, linkpath, sizeof(linkpath) - 1)) > 0)
    {
     /*
      * Yes, this is a symlink to the actual program, nul-terminate and
      * use it...
      */

      linkpath[linkbytes] = '\0';

      if (linkpath[0] == '/')
	snprintf(processPath, sizeof(processPath), "CFProcessPath=%s",
		 linkpath);
      else
	snprintf(processPath, sizeof(processPath), "CFProcessPath=%s/%s",
		 dirname((char *)command), linkpath);
    }
    else
      snprintf(processPath, sizeof(processPath), "CFProcessPath=%s", command);

    envp[0] = processPath;		/* Replace <CFProcessPath> string */
  }
#endif	/* __APPLE__ */

 /*
  * Use helper program when we have a sandbox profile...
  */

  if (profile)
  {
    snprintf(cups_exec, sizeof(cups_exec), "%s/daemon/cups-exec", ServerBin);

    real_argv[0] = cups_exec;
    real_argv[1] = profile;
    real_argv[2] = (char *)command;

    for (i = 0;
         i < (int)(sizeof(real_argv) / sizeof(real_argv[0]) - 4) && argv[i];
	 i ++)
      real_argv[i + 3] = argv[i];

    real_argv[i + 3] = NULL;

    argv      = real_argv;
    exec_path = cups_exec;
  }

 /*
  * Block signals before forking...
  */

  cupsdHoldSignals();

  if ((*pid = fork()) == 0)
  {
   /*
    * Child process goes here...
    *
    * Update stdin/stdout/stderr as needed...
    */

    if (infd != 0)
    {
      if (infd < 0)
        infd = open("/dev/null", O_RDONLY);

      if (infd != 0)
      {
        dup2(infd, 0);
	close(infd);
      }
    }

    if (outfd != 1)
    {
      if (outfd < 0)
        outfd = open("/dev/null", O_WRONLY);

      if (outfd != 1)
      {
        dup2(outfd, 1);
	close(outfd);
      }
    }

    if (errfd != 2)
    {
      if (errfd < 0)
        errfd = open("/dev/null", O_WRONLY);

      if (errfd != 2)
      {
        dup2(errfd, 2);
	close(errfd);
      }
    }

    if (backfd != 3 && backfd >= 0)
    {
      dup2(backfd, 3);
      close(backfd);
      fcntl(3, F_SETFL, O_NDELAY);
    }

    if (sidefd != 4 && sidefd >= 0)
    {
      dup2(sidefd, 4);
      close(sidefd);
      fcntl(4, F_SETFL, O_NDELAY);
    }

   /*
    * Change the priority of the process based on the FilterNice setting.
    * (this is not done for root processes...)
    */

    if (!root)
      nice(FilterNice);

   /*
    * Change user to something "safe"...
    */

    if (!root && !RunUser)
    {
     /*
      * Running as root, so change to non-priviledged user...
      */

      if (setgid(Group))
	exit(errno);

      if (setgroups(1, &Group))
	exit(errno);

      if (setuid(User))
        exit(errno);
    }
    else
    {
     /*
      * Reset group membership to just the main one we belong to.
      */

      if (setgid(Group) && !RunUser)
        exit(errno);

      if (setgroups(1, &Group) && !RunUser)
        exit(errno);
    }

   /*
    * Change umask to restrict permissions on created files...
    */

    umask(077);

   /*
    * Unblock signals before doing the exec...
    */

#ifdef HAVE_SIGSET
    sigset(SIGTERM, SIG_DFL);
    sigset(SIGCHLD, SIG_DFL);
    sigset(SIGPIPE, SIG_DFL);
#elif defined(HAVE_SIGACTION)
    memset(&action, 0, sizeof(action));

    sigemptyset(&action.sa_mask);
    action.sa_handler = SIG_DFL;

    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGCHLD, &action, NULL);
    sigaction(SIGPIPE, &action, NULL);
#else
    signal(SIGTERM, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
#endif /* HAVE_SIGSET */

    cupsdReleaseSignals();

   /*
    * Execute the command; if for some reason this doesn't work, log an error
    * exit with a non-zero value...
    */

    if (envp)
      execve(exec_path, argv, envp);
    else
      execv(exec_path, argv);

    perror(command);

    exit(1);
  }
  else if (*pid < 0)
  {
   /*
    * Error - couldn't fork a new process!
    */

    cupsdLogMessage(CUPSD_LOG_ERROR, "Unable to fork %s - %s.", command,
                    strerror(errno));

    *pid = 0;
  }
  else
  {
    if (!process_array)
      process_array = cupsArrayNew((cups_array_func_t)compare_procs, NULL);
 
    if (process_array)
    {
      if ((proc = calloc(1, sizeof(cupsd_proc_t) + strlen(command))) != NULL)
      {
        proc->pid    = *pid;
	proc->job_id = job ? job->id : 0;
	_cups_strcpy(proc->name, command);

	cupsArrayAdd(process_array, proc);
      }
    }
  }

  cupsdReleaseSignals();

  cupsdLogMessage(CUPSD_LOG_DEBUG2,
		  "cupsdStartProcess(command=\"%s\", argv=%p, envp=%p, "
		  "infd=%d, outfd=%d, errfd=%d, backfd=%d, sidefd=%d, root=%d, "
		  "profile=%p, job=%p(%d), pid=%p) = %d",
		  command, argv, envp, infd, outfd, errfd, backfd, sidefd,
		  root, profile, job, job ? job->id : 0, pid, *pid);

  return (*pid);
}


/*
 * 'compare_procs()' - Compare two processes.
 */

static int				/* O - Result of comparison */
compare_procs(cupsd_proc_t *a,		/* I - First process */
              cupsd_proc_t *b)		/* I - Second process */
{
  return (a->pid - b->pid);
}


#ifdef HAVE_SANDBOX_H
/*
 * 'cupsd_requote()' - Make a regular-expression version of a string.
 */

static char *				/* O - Quoted string */
cupsd_requote(char       *dst,		/* I - Destination buffer */
              const char *src,		/* I - Source string */
	      size_t     dstsize)	/* I - Size of destination buffer */
{
  int	ch;				/* Current character */
  char	*dstptr,			/* Current position in buffer */
	*dstend;			/* End of destination buffer */


  dstptr = dst;
  dstend = dst + dstsize - 2;

  while (*src && dstptr < dstend)
  {
    ch = *src++;

    if (strchr(".?*()[]^$\\", ch))
      *dstptr++ = '\\';

    *dstptr++ = ch;
  }

  *dstptr = '\0';

  return (dst);
}
#endif /* HAVE_SANDBOX_H */


/*
 * End of "$Id: process.c 7256 2008-01-25 00:48:54Z mike $".
 */
