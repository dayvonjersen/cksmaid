#include "util.h"

/* djb2 string hash function */
unsigned long
hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

void
handle_bux()
{
    puts("[ (!) ] awarding bux");
    llist *chans = malloc(sizeof(llist));
    db_list(db_entry("db", "channels"), chans);

    while(chans->head != NULL && 
            strlen(chans->head->name) > 1 &&
            chans->head->name[0] == '#') {

        while (strcmp(db_file(db_entry("db", chans->head->name, "active")), 
                    "thisdirectoryisdefinitelyemptyforsure")) {

            /* add bux to active user */
            int old_bux = db_exists(db_entry("db", chans->head->name, "bux", 
                        db_file(db_entry("db", chans->head->name, "active")))) ?
                        db_getnum(db_entry("db", chans->head->name, "bux", 
                        db_file(db_entry("db", chans->head->name, "active")))) : 0;

            db_setnum(db_entry("db", chans->head->name, "bux", 
                        db_file(db_entry("db", chans->head->name, "active"))),
                        old_bux + BUX_AMOUNT);

            /* move on to next user */
            db_del(db_entry("db", chans->head->name, "active",
                        db_file(db_entry("db", chans->head->name, "active"))));
        }

        /* move on to next channel */
        pop(chans, chans->head->name);
    }
    free(chans);
    alarm(BUX_INTERVAL);
}

void
handle_raw(int *sock, bool *reconnect, char *line)
{
    /* use total msg length for all temporary strings,
     * possible to do more efficient but good enough */
    int msglen = strlen(line);

    char *raw_msg = strchr(line, ' ') + 1;
    char *msgtype = strchr(raw_msg, ' ') + 1;

    if (!strncmp(msgtype, "PRIVMSG", 7)) {
        /* filter out info from raw PRIVMSG string */        
        char msg[msglen]; 
        char user[msglen]; 
        char *channel = strchr(msgtype, ' ') + 1;
        strcpy(user, raw_msg+1);
        strchr(user, '!')[0] = '\0';
        strcpy(msg, strchr(channel, ':')+1);
        strchr(channel, ' ')[0] = '\0';

        /* pull chaninfo from db */
        char *chandb = db_getdb("db", channel);
        char prefix = db_getchr(db_entry(chandb, "prefix"));

        /* handle activity */
        db_mkitem(db_entry(chandb, "active", user));

        /* catch bits */
        /* TODO: actually test this */
        char *bitstr = strchr(line, ';') + 1;
        if (!strncmp(bitstr, "bits=", 5)) {
            int bits = 0;
            sscanf(bitstr, "bits=%d", &bits);
            printf("[ (!) ] %s gave %d bits in %s\n", user, bits, channel);
        }

        /* ctcp */
        if (!strncmp(msg, "VERSION", 9)) {
            puts("[ (!) ] ctcp version"); 
            send_raw(sock, 0, "NOTICE %s :VERSION socket.h\r\n", DEST);
        } else if (!strncmp(msg, "PING ", 6)) {
            puts("[ (!) ] ctcp ping"); 
            send_raw(sock, 0, "NOTICE %s :PING %u\r\n", DEST, 88 );
        } else if (!strncmp(msg, "TIME", 6)) {
            puts("[ (!) ] ctcp time"); 
            send_raw(sock, 0, "NOTICE %s :TIME %u\r\n", DEST, (unsigned)time(NULL));
        }

        /* greeting if enabled */
        if (db_exists(db_entry(chandb, "settings", "greeting"))) {
            if (!db_exists(db_entry(chandb, "greeted", user))) {
                db_mkitem(db_entry(chandb, "greeted", user));
                char *greeting_format = malloc(sizeof(char) *666);
                strcpy(greeting_format, "PRIVMSG %s :");
                strcat(greeting_format, db_getstr(db_entry(chandb, "greeting_format")));
                strcat(greeting_format, "\r\n");
                send_raw(sock, 0, greeting_format, DEST, user);
            }
        }

        /* mail checking */
        if (db_exists(db_entry("db", "mail", user))) {
            if (!strcmp(db_file(db_entry("db", "mail", user)), 
                        "thisdirectoryisdefinitelyemptyforsure")) {
                db_del(db_entry("db", "mail", user));
            } else {
                send_raw(sock, 0, "PRIVMSG %s :%s: mail from %s: %s\r\n", DEST, user,
                        db_file(db_entry("db", "mail", user)), 
                        db_getstr(db_entry("db", "mail", user, 
                                db_file(db_entry("db", "mail", user)))));

                db_del(db_entry("db", "mail", user, 
                            db_file(db_entry("db", "mail", user))));
            }
        }

        /* afk checking */
        if (db_exists(db_entry(chandb, "afk", user))) {
            send_raw(sock, 0, "PRIVMSG %s :%s is no longer afk\r\n", DEST, user);
            db_del(db_entry(chandb, "afk", user));
        }

        /* commands */
        if (msg[0] == prefix) {
            printf("[ (!) ] '%c' prefixed command detected\n", prefix);
            strchr(msg, '\r')[0] = '\0';

            /* admin commands */
            if (!strncmp(raw_msg, ADMINSTR, strlen(ADMINSTR))) {
                if (!strncmp(msg+1, "join ", 5)) {
                    join_chan(sock, strchr(msg, ' ') + 1);
                } else if (!strncmp(msg+1, "part ", 5)) {
                    part_chan(sock, strchr(msg, ' ') + 1);
                } else if (!strncmp(msg+1, "title ", 6)) {
                    /* push to title string */
                    send_raw(sock, 0, "TOPIC %s\r\n", channel);
                } else if (!strncmp(msg+1, "prefix ", 7)) {
                    send_raw(sock, 0, "PRIVMSG %s :updating prefix in %s from '%c' to '%c'\r\n",
                            channel, channel, prefix, msg[8]);
                    db_setchr(db_entry(chandb, "prefix"), msg[8]);
                } else if (!strncmp(msg+1, "reconnect", 9)) {
                    puts("[ (!) ] reconnect notice received");
                    *reconnect = true;
                } else if (!strncmp(msg+1, "enable ", 7)) {
                    db_mkitem(db_entry(chandb, "settings", strchr(msg, ' ') + 1));
                    send_raw(sock, 0, "PRIVMSG %s :enabled %s\r\n", DEST, strchr(msg, ' ') + 1);
                } else if (!strncmp(msg+1, "disable ", 8)) {
                    if (db_exists(db_entry(chandb, "settings", strchr(msg, ' ') + 1))) {
                        db_del(db_entry(chandb, "settings", strchr(msg, ' ') + 1));
                        send_raw(sock, 0, "PRIVMSG %s :disabled %s\r\n", DEST, strchr(msg, ' ') + 1);
                    } else {
                        send_raw(sock, 0, "PRIVMSG %s :setting not enabled\r\n", DEST);
                    }
                } else if (!strncmp(msg+1, "checkdb", 7)) {
                    send_raw(sock, 0, "PRIVMSG %s :%s\r\n", DEST, 
                            check_db(chandb) ? "db fixed" : "db is fine");
                } else if (!strncmp(msg+1, "names", 5)) {
                    send_raw(sock, 0, "NAMES %s\r\n", channel);
                    send_raw(sock, 0, "USERS\r\n");
                } else if (!strncmp(msg+1, "testbux", 6)) {
                    handle_bux();
                } else if (!strncmp(msg+1, "regreet", 7)) {
                    while (strcmp(db_file(db_entry(chandb, "greeted")), 
                                "thisdirectoryisdefinitelyemptyforsure")) {
                        db_del(db_entry(chandb, "greeted", db_file(db_entry(chandb, "greeted"))));
                    }
                    send_raw(sock, 0, "PRIVMSG %s :greeted cache cleared, regreeting everyone\r\n", 
                            DEST);
                } else if (!strncmp(msg+1, "setgreeting ", 12)) {
                    db_setstr(db_entry(chandb, "greeting_format"), msg+13);
                    send_raw(sock, 0, "PRIVMSG %s :set greeting in %s to: '%s'\r\n", DEST,
                            channel, db_getstr(db_entry(chandb, "greeting_format")));
                }
            }

            /* public commands */
            if (!strncmp(msg+1, "checkme", 7)) {
                if (!strncmp(raw_msg, ADMINSTR, strlen(ADMINSTR))) {
                    send_raw(sock, 0, "PRIVMSG %s :user status: admin\r\n", DEST);
                } else {
                    send_raw(sock, 0, "PRIVMSG %s :user status: user\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "vanish", 6)) {
                if (!db_exists(db_entry(chandb, "settings", "novanish"))) {
                    send_raw(sock, 0, "PRIVMSG %s :.timeout %s 1\r\n", DEST, user);
                }
            } else if (!strncmp(msg+1, "t ", 2)) {
                char tag[msglen];
                sscanf(msg+3, "%s", tag);
                printf("tag: '%s'\n", tag);

                send_raw(sock, 0, "PRIVMSG %s :%s\r\n", DEST, 
                        db_exists(db_entry(chandb, "tags", tag))
                        ? db_getstr(db_entry(chandb, "tags", tag))
                        : "tag not found");
            } else if (!strncmp(msg+1, "tnew ", 5)) {
                char tag[msglen];
                char content[msglen];
                sscanf(msg+6, "%s", tag);
                strcpy(content, strchr(strchr(msg+1, ' ')+1, ' ') 
                        ? strchr(strchr(msg+1, ' ')+1, ' ')+1
                        : "");

                if (db_exists(db_entry(chandb, "tags", tag))) {
                    send_raw(sock, 0, "PRIVMSG %s :tag already exists\r\n", DEST);
                } else if (strlen(content) > 0) {
                    db_setstr(db_entry(chandb, "tags", tag), content);
                    send_raw(sock, 0, "PRIVMSG %s :tag created\r\n", DEST);
                } else {
                    send_raw(sock, 0, "PRIVMSG %s :no tag content given\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "tedit ", 6)) {
                char tag[msglen];
                char content[msglen];
                sscanf(msg+7, "%s", tag);
                strcpy(content, strchr(strchr(msg+1, ' ')+1, ' ') 
                        ? strchr(strchr(msg+1, ' ')+1, ' ')+1
                        : "");

                if (!db_exists(db_entry(chandb, "tags", tag))) {
                    send_raw(sock, 0, "PRIVMSG %s :tag not found\r\n", DEST);
                } else if (strlen(content) > 0) {
                    db_setstr(db_entry(chandb, "tags", tag), content);
                    send_raw(sock, 0, "PRIVMSG %s :tag updated\r\n", DEST);
                } else {
                    send_raw(sock, 0, "PRIVMSG %s :no tag content given\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "c ", 2)) {
                char counter[msglen];
                sscanf(msg+3, "%s", counter);
                printf("counter: '%s'\n", counter);

                if (db_exists(db_entry(chandb, "counters", counter))) {
                    int c = db_getnum(db_entry(chandb, "counters", counter))+1;
                    send_raw(sock, 0, "PRIVMSG %s :%s counter: %d\r\n", DEST,
                            counter, c);
                    db_setnum(db_entry(chandb, "counters", counter), c);
                } else {
                    send_raw(sock, 0, "PRIVMSG %s :counter not found\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "cnew ", 5)) {
                char counter[msglen];
                sscanf(msg+6, "%s", counter);

                if (db_exists(db_entry(chandb, "counters", counter))) {
                    send_raw(sock, 0, "PRIVMSG %s :counter already exists\r\n", DEST);
                } else {
                    db_setnum(db_entry(chandb, "counters", counter), 0);
                    send_raw(sock, 0, "PRIVMSG %s :counter created\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "cedit ", 6)) {
                char counter[msglen];
                int content;
                sscanf(msg+7, "%s %d", counter, &content);

                if (!db_exists(db_entry(chandb, "counters", counter))) {
                    send_raw(sock, 0, "PRIVMSG %s :counter not found\r\n", DEST);
                } else {
                    db_setnum(db_entry(chandb, "counters", counter), content);
                    send_raw(sock, 0, "PRIVMSG %s :counter updated\r\n", DEST);
                }
            } else if (!strncmp(msg+1, "afk", 3)) {
                char content[msglen];
                strcpy(content, strchr(msg+1, ' ') 
                        ? strchr(msg+1, ' ')+1
                        : "no context");
                send_raw(sock, 0, "PRIVMSG %s :%s is now afk: %s\r\n", DEST,
                            user, content);
                db_setstr(db_entry(chandb, "afk", user), content);
            } else if (!strncmp(msg+1, "whereis ", 8)) {
                char quser[msglen];
                sscanf(msg+9, "%s", quser);

                /* make sure nick is lowercase before looking it up */
                TOLOWER(quser);

                if (db_exists(db_entry(chandb, "afk", quser))) {
                    send_raw(sock, 0, "PRIVMSG %s :%s is afk: %s\r\n", DEST, quser,
                            db_getstr(db_entry(chandb, "afk", quser)));
                } else {
                    send_raw(sock, 0, "PRIVMSG %s :¯\\_(ツ)_/¯\r\n", DEST);
                }
            } else if(!strncmp(msg+1, "mail ", 5)) {
                char *muser = malloc(sizeof(char)*msglen);
                char *pmuser = muser;
                char content[msglen];
                sscanf(msg+6, "%s", muser);
                strcpy(content, strchr(strchr(msg+1, ' ')+1, ' ') 
                        ? strchr(strchr(msg+1, ' ')+1, ' ')+1
                        : "");

                /* make sure nick is lowercase before storing it */
                TOLOWER(muser);
                if (muser[0] == '@') {
                    muser = muser+1;
                }

                db_init(db_entry("db", "mail", muser));
                db_setstr(db_entry("db", "mail", muser, user), content);
                send_raw(sock, 0, "PRIVMSG %s :will pass that on to %s when I spot them\r\n", DEST, muser);
                free(pmuser);
            } else if(!strncmp(msg+1, "8ball ", 6)) {
                char *ball[] = {
                    "it is certain",
                    "it is decidedly so",
                    "without a doubt",
                    "yes definitely",
                    "you may rely on it",
                    "as I see it, yes",
                    "most likely",
                    "outlook good",
                    "yes",
                    "signs point to yes",
                    "reply hazy try again",
                    "ask again later",
                    "idk kev 4Head",
                    "better not tell you now",
                    "cannot predict now",
                    "concentrate and ask again later",
                    "don't count on it",
                    "my reply is no",
                    "my sources say no",
                    "outlook not so good",
                    "my sources say no",
                    "very doubtful",
                    "maybe Kappa",
                    "like I care LUL",
                    "asking a scuffed irc bot for advice LUL"
                };

                srand(time(NULL));
                send_raw(sock, 0, "PRIVMSG %s :%s\r\n", DEST, ball[rand()%25]);
            } else if(!strncmp(msg+1, "bux", 3)) {
                char *buser = malloc(msglen * sizeof(char));
                char *pbuser = buser;
                sscanf(msg+5, "%s", buser);

                if (msg[4] == ' ') {
                    if (buser[0] == '@') {
                        buser = buser+1;
                    }

                    /* make sure nick is lowercase before checking */
                    TOLOWER(buser);

                    if (db_exists(db_entry("db", channel, "bux", buser))) {
                        send_raw(sock, 0, "PRIVMSG %s :%s's %sbux: %d\r\n", DEST, 
                                buser, channel+1, 
                                db_getnum(db_entry(chandb, "bux", buser)));
                    }
                } else {
                    if (db_exists(db_entry("db", channel, "bux", user))) {
                        send_raw(sock, 0, "PRIVMSG %s :%s's %sbux: %d\r\n", DEST, 
                                user, channel+1, 
                                db_getnum(db_entry(chandb, "bux", user)));
                    }
                }
                free(pbuser);

            } else if(!strncmp(msg+1, "rate ", 5)) {
                char *hash_str = malloc(sizeof(char) * 100);

                sprintf(hash_str, "%s%s", user, msg+6);

                srand(hash(hash_str));
                send_raw(sock, 0, "PRIVMSG %s :%s is %d%% %s\r\n", DEST, user,
                        rand()%100+1, msg+6);

                free(hash_str);
            } else if(!strncmp(msg+1, "$timeout ", 9)) {
                int user_bux = 0;
                char tuser[msglen];
                sscanf(msg+10, "%s", tuser);

                if (db_exists(db_entry(chandb, "bux", user)) &&
                        (user_bux = db_getnum(db_entry(chandb, "bux", user))) 
                        > TIMEOUT_COST+1) {
                    db_setnum(db_entry(chandb, "bux", user), user_bux-TIMEOUT_COST);
                    send_raw(sock, 0, "PRIVMSG %s :%s wasted %d %sbux to timeout %s LUL\r\n", DEST,
                            user, TIMEOUT_COST, channel+1, tuser);
                    send_raw(sock, 0, "PRIVMSG %s :.timeout %s %d\r\n", DEST,
                            tuser, TIMEOUT_DURATION);
                }
            } else if(!strncmp(msg+1, "gamble ", 7)) {
                int user_bux = 0;
                int gamble_amount = 0;
                char gamble_all[4];
                sscanf(msg+8, "%d", &gamble_amount);
                sscanf(msg+8, "%3s", gamble_all);
                
                srand(time(NULL));
                gamble_amount = abs(gamble_amount);

                if ((!db_exists(db_entry(chandb, "bux", user)) || 
                        (user_bux = db_getnum(db_entry(chandb, "bux", user))) < gamble_amount) 
                        && strcmp("all", gamble_all)) {
                    send_raw(sock, 0, "PRIVMSG %s :insufficient bux\r\n", DEST);
                } else if (rand()%2) {
                    gamble_amount = !strcmp("all", gamble_all) ? user_bux : gamble_amount;
                    user_bux -= gamble_amount;

                    db_setnum(db_entry(chandb, "bux", user), 
                            user_bux + (int) (gamble_amount * GAMBLE_PROFIT));
                    send_raw(sock, 0, "PRIVMSG %s :%s just won %d %sbux, now has %d %sbux\r\n", DEST,
                            user, (int)(gamble_amount*GAMBLE_PROFIT-gamble_amount), channel+1,
                            db_getnum(db_entry(chandb, "bux", user)), channel+1);
                } else {
                    gamble_amount = !strcmp("all", gamble_all) ? user_bux : gamble_amount;
                    db_setnum(db_entry(chandb, "bux", user), 
                            user_bux - gamble_amount);
                    send_raw(sock, 0, "PRIVMSG %s :%s just lost %d %sbux, now has %d %sbux\r\n", 
                            DEST, user, gamble_amount, channel+1,
                            db_getnum(db_entry(chandb, "bux", user)), channel+1);
                }
            } else if(!strncmp(msg+1, "give ", 5)) {
                char *guser = malloc(msglen * sizeof(char));
                char *pguser = guser;
                int user_bux;
                int give_amount;
                sscanf(msg+6, "%s %d", guser, &give_amount);

                /* make sure nick is lowercase before storing it */
                TOLOWER(guser);
                if (guser[0] == '@') {
                    guser = guser+1;
                }

                give_amount = abs(give_amount);
                
                if (!db_exists(db_entry(chandb, "bux", guser))) {
                    send_raw(sock, 0, "PRIVMSG %s :%s hasn't been active enough to deserve bux\r\n", 
                            DEST, guser);
                } else if (!db_exists(db_entry(chandb, "bux", user)) || 
                        (user_bux = db_getnum(db_entry(chandb, "bux", user))) < give_amount) {
                    send_raw(sock, 0, "PRIVMSG %s :insufficient bux\r\n", DEST);
                } else {
                    db_setnum(db_entry(db_entry(chandb, "bux", guser)), 
                            db_getnum(db_entry(chandb, "bux", guser)) + give_amount);
                    db_setnum(db_entry(db_entry(chandb, "bux", user)), 
                            db_getnum(db_entry(chandb, "bux", user)) - give_amount);
                    send_raw(sock, 0, "PRIVMSG %s :%s gave %s %d %sbux\r\n", DEST,
                            user, guser, give_amount, channel+1);
                }

                free(pguser);
            }

        } else if (!strncmp(msg, ".bots", 5)) {
            send_raw(sock, 0, "PRIVMSG %s :Reporting in! [C]\r\n", DEST);
        } 

        /* start monad */
        if (!strncmp(msg, "!enation", 8)) {
            send_raw(sock, 0, "PRIVMSG %s :weeb nation TehePelo ~ <3\r\n", DEST);
        }
        /* end monad */

        /* free db vars */
        free(chandb);
    } else if (!strncmp(msgtype, "USERNOTICE", 10)) { /* TODO: actually test this */
        char channel[msglen];
        strcpy(channel, strchr(msgtype, ' ')+1);
        if (strchr(channel, ' ') != NULL) {
            puts("[ (!) ] not an empty sub message");
            strchr(channel, ' ')[0] = '\0';
        }

        char user[msglen]; 
        strcpy(user, strchr(strchr(strchr(line, ';')+1, ';')+1, '=')+1);
        strchr(user, ';')[0] = '\0';

        printf("[ (!) ] %s did a nice thing in %s\n", user, channel);
    }
}

void
send_raw(int *sock, bool silent, char *msgformat, ...)
{
    char buf[2000];
    va_list args;
    va_start(args, msgformat);
    vsnprintf(buf, 2000, msgformat, args);
    va_end(args);

    /* hackishly overwrite PRIVMSG with WHISPER for twitch pms */
    if (!strncmp(buf, "PRIVMSG", 7) && buf[8] != '#') {
        char whisper[] = "WHISPER";
        for (int i = 0; i < 7; ++i) {
            buf[i] = whisper[i];
        }
    }

    if (send(*sock, buf, strlen(buf), 0) == -1 && !silent) {
        fprintf(stderr, "[ !!! ] failed to send: '%s'", buf);
    } else if (!silent) {
        printf("[ >>> ] %s", buf);
    }
}

void
join_chan(int *sock, char *chan)
{
    /* check if db is alright */
    char *chandb = db_getdb("db", chan);
    db_init(chandb);
    check_db(chandb);
    free(chandb);

    db_mkitem(db_entry("db", "channels", chan));
    send_raw(sock, 0, "JOIN %s\r\n", chan);
}

void
part_chan(int *sock, char *chan)
{
    db_del(db_entry("db", "channels", chan));
    send_raw(sock, 0, "PART %s\r\n", chan);
}

bool
check_db(char *chandb)
{
    int fixed = 0;
    char *dbs[] = {
        db_getdb(chandb, "greeted"), 
        db_getdb(chandb, "bux"), 
        db_getdb(chandb, "afk"), 
        db_getdb(chandb, "tags"), 
        db_getdb(chandb, "counters"), 
        db_getdb(chandb, "settings"),
        db_getdb(chandb, "active"),
        db_getdb("db", "mail"),
        db_getdb("db", "channels")
    };

    for (int i = 0; i < 9; ++i) {
        if (!db_exists(dbs[i])) {
            printf("[ (!) ] added db: %s\n", dbs[i]);
            db_init(dbs[i]);
            fixed++;
        }
        free(dbs[i]);
    }
    if (!db_exists(db_entry(chandb, "prefix"))) {
        db_setchr(db_entry(chandb, "prefix"), ';');
        fixed++;
    }
    if (!db_exists(db_entry(chandb, "greeting_format"))) {
        db_setstr(db_entry(chandb, "greeting_format"), "%s VoHiYo");
        fixed++;
    }
    return (fixed > 0);
}

int
strpos(char *match, char *str)
{
    if (strlen(match) > strlen(str)) {
        return -1;
    }

    int match_len = strlen(match);
    int str_len = strlen(str);

    for (int i = 0; i < str_len - match_len+1; ++i) {
        if (match[0] == str[i]) {
            int ii = 0;
            while (match[ii] == str[i + ii]) {
                if (ii+1 == match_len) {
                    return i;
                }
                ++ii;
            }
        }
    }
    return -1;
}
