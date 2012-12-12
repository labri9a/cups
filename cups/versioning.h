/*
 * "$Id$"
 *
 *   API versioning definitions for CUPS.
 *
 *   Copyright 2007-2012 by Apple Inc.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Apple Inc. and are protected by Federal copyright
 *   law.  Distribution and use rights are outlined in the file "LICENSE.txt"
 *   which should have been included with this file.  If this file is
 *   file is missing or damaged, see the license at "http://www.cups.org/".
 *
 *   This file is subject to the Apple OS-Developed Software exception.
 */

#ifndef _CUPS_VERSIONING_H_
#  define _CUPS_VERSIONING_H_

/*
 * This header defines several constants - _CUPS_DEPRECATED,
 * _CUPS_API_1_1, _CUPS_API_1_1_19, _CUPS_API_1_1_20, _CUPS_API_1_1_21,
 * _CUPS_API_1_2, _CUPS_API_1_3, _CUPS_API_1_4, _CUPS_API_1_5, _CUPS_API_1_6,
 * and _CUPS_API_1_7 - which add compiler-specific attributes that flag
 * functions that are deprecated or added in particular releases.
 *
 * On OS X, the _CUPS_API_* constants are defined based on the values of
 * the MAC_OS_X_VERSION_MIN_ALLOWED and MAC_OS_X_VERSION_MAX_ALLOWED constants
 * provided by the compiler.
 */

#  if defined(__APPLE__) && !defined(_CUPS_SOURCE)
#    include <AvailabilityMacros.h>
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_3_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_3_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_3_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_7_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_7_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_7_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_8_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_8_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_8_AND_LATER */
#    ifndef AVAILABLE_MAC_OS_X_VERSION_10_9_AND_LATER
#      define AVAILABLE_MAC_OS_X_VERSION_10_9_AND_LATER __attribute__((unavailable))
#    endif /* !AVAILABLE_MAC_OS_X_VERSION_10_9_AND_LATER */
#    define _CUPS_API_1_1_19 AVAILABLE_MAC_OS_X_VERSION_10_3_AND_LATER
#    define _CUPS_API_1_1_20 AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER
#    define _CUPS_API_1_1_21 AVAILABLE_MAC_OS_X_VERSION_10_4_AND_LATER
#    define _CUPS_API_1_2 AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#    define _CUPS_API_1_3 AVAILABLE_MAC_OS_X_VERSION_10_5_AND_LATER
#    define _CUPS_API_1_4 AVAILABLE_MAC_OS_X_VERSION_10_6_AND_LATER
#    define _CUPS_API_1_5 AVAILABLE_MAC_OS_X_VERSION_10_7_AND_LATER
#    define _CUPS_API_1_6 AVAILABLE_MAC_OS_X_VERSION_10_8_AND_LATER
#    define _CUPS_API_1_7 AVAILABLE_MAC_OS_X_VERSION_10_9_AND_LATER
#  else
#    define _CUPS_API_1_1_19
#    define _CUPS_API_1_1_20
#    define _CUPS_API_1_1_21
#    define _CUPS_API_1_2
#    define _CUPS_API_1_3
#    define _CUPS_API_1_4
#    define _CUPS_API_1_5
#    define _CUPS_API_1_6
#    define _CUPS_API_1_7
#  endif /* __APPLE__ && !_CUPS_SOURCE */

/*
 * With GCC 3.0 and higher, we can mark old APIs "deprecated" so you get
 * a warning at compile-time.
 */

#  if defined(__GNUC__) && __GNUC__ > 2
#    ifndef __has_extension
#      define __has_extension(x) 0
#    endif /* !__has_extension */
#    if __has_extension(attribute_unavailable_with_message) && defined(_CUPS_NO_DEPRECATED)
#      define _CUPS_DEPRECATED __attribute__ ((unavailable))
#      define _CUPS_DEPRECATED_MSG(m) __attribute__ ((unavailable(m)))
#    elif !defined(_CUPS_SOURCE) || defined(_CUPS_NO_DEPRECATED)
#      define _CUPS_DEPRECATED __attribute__ ((deprecated))
#      define _CUPS_DEPRECATED_MSG(m) __attribute__ ((deprecated(m)))
#    else
#      define _CUPS_DEPRECATED
#      define _CUPS_DEPRECATED_MSG(m)
#    endif /* !_CUPS_SOURCE || _CUPS_NO_DEPRECATED */
#  else
#    define _CUPS_DEPRECATED
#    define _CUPS_DEPRECATED_MSG(m)
#  endif /* __GNUC__ && __GNUC__ > 2 */

#  ifndef __GNUC__
#    define __attribute__(x)
#  endif /* !__GNUC__ */

#endif /* !_CUPS_VERSIONING_H_ */

/*
 * End of "$Id$".
 */
