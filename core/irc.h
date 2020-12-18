#ifndef IRC_H
#define IRC_H
/* required for types */
#include <openssl/ssl.h>
#include <stdbool.h>
#include "../ini_rw/ini_rw.h"

typedef struct irc_conn {
	INI *globalconf;
	char index[256];
	char addr[256];
	char port[6];
	char nick[32];
	char *pass;
	int *fd;
	SSL *sock;
	SSL_CTX *ctx;
	bool init;
	time_t heartbeat;
	time_t init_time;
} irc_conn;

int init_conn(irc_conn *conn);
void reconnect_conn(irc_conn *conn);
void destroy_conn(irc_conn *conn);
void join_chans(irc_conn *conn, char *chans);
void part_chans(irc_conn *conn, char *chans);
void send_raw(irc_conn *conn, char silent, char *msgformat, ...);

/* bufsize is this oversized to deal with twitch's "500 characters" bs */
#define BUFSIZE 2000

/* terrible macro wrappers to hide how bad the send function works */
#define DEST mi->chan[0] == '#' ? mi->chan : mi->user
#define send_privmsg(msgformat) send_raw(mi->conn, 0, "PRIVMSG %s :" msgformat, DEST)
#define send_fprivmsg(msgformat, ...) send_raw(mi->conn, 0, "PRIVMSG %s :" msgformat, DEST, __VA_ARGS__)
#define send_notice(msgformat) send_raw(mi->conn, 0, "NOTICE %s :" msgformat, DEST)
#define send_fnotice(msgformat, ...) send_raw(mi->conn, 0, "NOTICE %s :" msgformat, DEST, __VA_ARGS__)

#endif
