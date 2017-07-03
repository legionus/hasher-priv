#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <errno.h>
#include <syslog.h>

#include "logging.h"

int log_priority = -1;

int logging_level(const char *name)
{
	if (!strcasecmp(name, "debug"))
		return LOG_DEBUG;

	if (!strcasecmp(name, "info"))
		return LOG_INFO;

	if (!strcasecmp(name, "warning"))
		return LOG_WARNING;

	if (!strcasecmp(name, "error"))
		return LOG_ERR;

	return 0;
}

void logging_init(int loglevel, int stderr)
{
	int options = LOG_PID;
	if (stderr)
		options |= LOG_PERROR;
	log_priority = loglevel;
	openlog(program_invocation_short_name, options, LOG_DAEMON);
}

void logging_close(void)
{
	closelog();
}

void __attribute__((format(printf, 2, 3)))
message(int priority, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (priority <= log_priority)
		vsyslog(priority, fmt, ap);
	else if (log_priority < 0) {
		vfprintf(stderr, fmt, ap);
		fprintf(stderr, "\n");
	}
	va_end(ap);
}
