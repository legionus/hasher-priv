
/* unet-logging.h
 *
 * This file is part of unet.
 *
 * Copyright (C) 2012  Alexey Gladkov <gladkov.alexey@gmail.com>
 *
 * This file is covered by the GNU General Public License,
 * which should be included with unet as the file COPYING.
 */

#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <syslog.h>
#include <stdlib.h>

void logging_init(int);
void logging_close(void);
int logging_level(const char *lvl);

void message(int priority, const char *fmt, ...);

#define fatal(format, arg...)                             \
	do {                                              \
		message(LOG_CRIT,                         \
		        "%s(%d): %s: " format,            \
		        __FILE__, __LINE__, __FUNCTION__, \
		        ##arg);                           \
		exit(EXIT_FAILURE);                       \
	} while (0)

#define err(format, arg...)                               \
	do {                                              \
		message(LOG_ERR,                          \
		        "%s(%d): %s: " format,            \
		        __FILE__, __LINE__, __FUNCTION__, \
		        ##arg);                           \
	} while (0)

#define info(format, arg...)                              \
	do {                                              \
		message(LOG_INFO,                         \
		        "%s(%d): %s: " format,            \
		        __FILE__, __LINE__, __FUNCTION__, \
		        ##arg);                           \
	} while (0)

#define dbg(format, arg...)                               \
	do {                                              \
		message(LOG_DEBUG,                        \
		        "%s(%d): %s: " format,            \
		        __FILE__, __LINE__, __FUNCTION__, \
		        ##arg);                           \
	} while (0)

#endif /* _LOGGING_H_ */
