#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

/* required */
#include "../modtape.h"
#include "../../core/modules.h"
#include "../../core/irc.h"

#include "../../ini_rw/ini_rw.h"

INI *lookup;
char **urls;

static void
handle_privmsg(irc_conn *s, char *index, char *chan, char *user, char *msg)
{
	/* copy msg */
	bool url_fixed = false;
	char buf[BUFSIZE] = {'\0'};
	strcpy(buf, msg);

	/* handle all the onions */
	for (int i = 0; urls[i]; ++i) {
		/* check match */
		char *match = strstr(buf, urls[i]);

		/* replace occurrences and send fixed result */
		while (match) {
			url_fixed = true;
			char tmp[BUFSIZE] = {'\0'};
			strncpy(tmp, buf, match-buf);
			strcat(tmp, ini_read(lookup, "onions", urls[i]));
			strcat(tmp, match+strlen(urls[i]));
			strcpy(buf, tmp);
			match = strstr(buf, urls[i]);
		}
	}

	/* send if altered */
	if (url_fixed) {
		send_fprivmsg("%s\r\n", buf); 
	}
}

static void
handle_cmdmsg(
		irc_conn *s, char *index, char *chan, char *user, char *msg, bool mod)
{
	/* admin only */
	if (!mod) {
		return;
	}

	if (!strncmp(msg, "onion ", 6)) {
		char onion[BUFSIZE];
		strcpy(onion, strchr(msg, ' ')+1);
		strchr(onion, ' ')[0] = '\0'; 

		char *clearnet = strchr(strchr(msg, ' ')+1, ' ')+1;

		char tmp[BUFSIZE];
		sprintf(tmp, "http://%s", onion);
		ini_write(lookup, "onions", tmp, clearnet);
		sprintf(tmp, "https://%s", onion);
		ini_write(lookup, "onions", tmp, clearnet);
		send_privmsg("sure\r\n"); 
	}
}

void
scallions_init()
{
	mods_new("scallions", true);
	mods_privmsg_handler(handle_privmsg);
	mods_cmdmsg_handler(handle_cmdmsg);
	lookup = ini_load("mods/scallions/lookup.ini");
	urls = ini_list_items(lookup, "onions");
}
