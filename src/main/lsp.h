#ifndef _MAIN_LSP_H
#define _MAIN_LSP_H

struct reactor;
struct buffers;

void lang_servers_init(struct reactor *reactor, struct buffers *buffers);
void lang_servers_update(void);
void lang_servers_teardown(void);

#endif
