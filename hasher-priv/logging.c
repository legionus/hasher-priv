#include <stdarg.h>
#include <strings.h>
#include <errno.h>
#include <syslog.h>

#include "logging.h"

int log_priority = 0;

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

void logging_init(int loglevel)
{
	log_priority = loglevel;
	openlog(program_invocation_short_name, LOG_PID | LOG_PERROR, LOG_DAEMON);
}

void logging_close(void)
{
	closelog();
}

void __attribute__((format(printf, 2, 3)))
message(int priority, const char *fmt, ...)
{
	va_list ap;

	if (priority > log_priority)
		return;

	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
}
