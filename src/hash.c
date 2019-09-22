/************************************************************************
 *   Unreal Internet Relay Chat Daemon, src/hash.c
 *   Copyright (C) 1991 Darren Reed
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "unrealircd.h"

/* Next #define's, the siphash_raw() and siphash_nocase() functions are based
 * on the SipHash reference C implementation to which the following applies:
 * Copyright (c) 2012-2016 Jean-Philippe Aumasson
 *  <jeanphilippe.aumasson@gmail.com>
 * Copyright (c) 2012-2014 Daniel J. Bernstein <djb@cr.yp.to>
 * Further enhancements were made by:
 * Copyright (c) 2017 Salvatore Sanfilippo <antirez@gmail.com>
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.
 *
 * In addition to above, Bram Matthys (Syzop), did some minor enhancements,
 * such as dropping the uint8_t stuff (in UnrealIRCd char is always unsigned)
 * and getting rid of the length argument.
 *
 * The end result are simple functions for API end-users and we encourage
 * everyone to use these two hash functions everywhere in UnrealIRCd.
 */

#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define U32TO8_LE(p, v)                                                        \
    (p)[0] = (char)((v));                                                   \
    (p)[1] = (char)((v) >> 8);                                              \
    (p)[2] = (char)((v) >> 16);                                             \
    (p)[3] = (char)((v) >> 24);

#define U64TO8_LE(p, v)                                                        \
    U32TO8_LE((p), (uint32_t)((v)));                                           \
    U32TO8_LE((p) + 4, (uint32_t)((v) >> 32));

#define U8TO64_LE(p)                                                           \
    (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) |                        \
     ((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) |                 \
     ((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) |                 \
     ((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56))

#define U8TO64_LE_NOCASE(p)                                                    \
    (((uint64_t)(tolower((p)[0]))) |                                           \
     ((uint64_t)(tolower((p)[1])) << 8) |                                      \
     ((uint64_t)(tolower((p)[2])) << 16) |                                     \
     ((uint64_t)(tolower((p)[3])) << 24) |                                     \
     ((uint64_t)(tolower((p)[4])) << 32) |                                              \
     ((uint64_t)(tolower((p)[5])) << 40) |                                              \
     ((uint64_t)(tolower((p)[6])) << 48) |                                              \
     ((uint64_t)(tolower((p)[7])) << 56))

#define SIPROUND                                                               \
    do {                                                                       \
        v0 += v1;                                                              \
        v1 = ROTL(v1, 13);                                                     \
        v1 ^= v0;                                                              \
        v0 = ROTL(v0, 32);                                                     \
        v2 += v3;                                                              \
        v3 = ROTL(v3, 16);                                                     \
        v3 ^= v2;                                                              \
        v0 += v3;                                                              \
        v3 = ROTL(v3, 21);                                                     \
        v3 ^= v0;                                                              \
        v2 += v1;                                                              \
        v1 = ROTL(v1, 17);                                                     \
        v1 ^= v2;                                                              \
        v2 = ROTL(v2, 32);                                                     \
    } while (0)

/** Generic hash function in UnrealIRCd - raw version.
 * Note that you probably want siphash() or siphash_nocase() instead.
 * @param in    The data to hash
 * @param inlen The length of the data
 * @param k     The key to use for hashing (SIPHASH_KEY_LENGTH bytes,
 *              which is actually 16, not NUL terminated)
 * @returns Hash result as a 64 bit unsigned integer.
 * @notes The key (k) should be random and must stay the same for
 *        as long as you use the function for your specific hash table.
 *        Simply use the following on boot: siphash_generate_key(k);
 *
 *        This siphash_raw() version is meant for non-strings,
 *        such as raw IP address structs and such.
 */
uint64_t siphash_raw(const char *in, size_t inlen, const char *k)
{
    uint64_t hash;
    char *out = (char*) &hash;
    uint64_t v0 = 0x736f6d6570736575ULL;
    uint64_t v1 = 0x646f72616e646f6dULL;
    uint64_t v2 = 0x6c7967656e657261ULL;
    uint64_t v3 = 0x7465646279746573ULL;
    uint64_t k0 = U8TO64_LE(k);
    uint64_t k1 = U8TO64_LE(k + 8);
    uint64_t m;
    const char *end = in + inlen - (inlen % sizeof(uint64_t));
    const int left = inlen & 7;
    uint64_t b = ((uint64_t)inlen) << 56;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    for (; in != end; in += 8) {
        m = U8TO64_LE(in);
        v3 ^= m;

        SIPROUND;
        SIPROUND;

        v0 ^= m;
    }

    switch (left) {
    case 7: b |= ((uint64_t)in[6]) << 48; /* fallthrough */
    case 6: b |= ((uint64_t)in[5]) << 40; /* fallthrough */
    case 5: b |= ((uint64_t)in[4]) << 32; /* fallthrough */
    case 4: b |= ((uint64_t)in[3]) << 24; /* fallthrough */
    case 3: b |= ((uint64_t)in[2]) << 16; /* fallthrough */
    case 2: b |= ((uint64_t)in[1]) << 8;  /* fallthrough */
    case 1: b |= ((uint64_t)in[0]); break;
    case 0: break;
    }

    v3 ^= b;

    SIPROUND;
    SIPROUND;

    v0 ^= b;
    v2 ^= 0xff;

    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);

    return hash;
}

/** Generic hash function in UnrealIRCd - case insensitive.
 * This deals with IRC case-insensitive matches, which is
 * what you need for things like nicks and channels.
 * @param str   The string to hash (NUL-terminated)
 * @param k     The key to use for hashing (SIPHASH_KEY_LENGTH bytes,
 *              which is actually 16, not NUL terminated)
 * @returns Hash result as a 64 bit unsigned integer.
 * @notes The key (k) should be random and must stay the same for
 *        as long as you use the function for your specific hash table.
 *        Simply use the following on boot: siphash_generate_key(k);
 */
uint64_t siphash_nocase(const char *in, const char *k)
{
    uint64_t hash;
    char *out = (char*) &hash;
    size_t inlen = strlen(in);
    uint64_t v0 = 0x736f6d6570736575ULL;
    uint64_t v1 = 0x646f72616e646f6dULL;
    uint64_t v2 = 0x6c7967656e657261ULL;
    uint64_t v3 = 0x7465646279746573ULL;
    uint64_t k0 = U8TO64_LE(k);
    uint64_t k1 = U8TO64_LE(k + 8);
    uint64_t m;
    const char *end = in + inlen - (inlen % sizeof(uint64_t));
    const int left = inlen & 7;
    uint64_t b = ((uint64_t)inlen) << 56;
    v3 ^= k1;
    v2 ^= k0;
    v1 ^= k1;
    v0 ^= k0;

    for (; in != end; in += 8) {
        m = U8TO64_LE_NOCASE(in);
        v3 ^= m;

        SIPROUND;
        SIPROUND;

        v0 ^= m;
    }

    switch (left) {
    case 7: b |= ((uint64_t)tolower(in[6])) << 48; /* fallthrough */
    case 6: b |= ((uint64_t)tolower(in[5])) << 40; /* fallthrough */
    case 5: b |= ((uint64_t)tolower(in[4])) << 32; /* fallthrough */
    case 4: b |= ((uint64_t)tolower(in[3])) << 24; /* fallthrough */
    case 3: b |= ((uint64_t)tolower(in[2])) << 16; /* fallthrough */
    case 2: b |= ((uint64_t)tolower(in[1])) << 8;  /* fallthrough */
    case 1: b |= ((uint64_t)tolower(in[0])); break;
    case 0: break;
    }

    v3 ^= b;

    SIPROUND;
    SIPROUND;

    v0 ^= b;
    v2 ^= 0xff;

    SIPROUND;
    SIPROUND;
    SIPROUND;
    SIPROUND;

    b = v0 ^ v1 ^ v2 ^ v3;
    U64TO8_LE(out, b);

    return hash;
}

/* End of imported code */

/** Generic hash function in UnrealIRCd.
 * @param str   The string to hash (NUL-terminated)
 * @param k     The key to use for hashing (SIPHASH_KEY_LENGTH bytes,
 *              which is actually 16, not NUL terminated)
 * @returns Hash result as a 64 bit unsigned integer.
 * @notes The key (k) should be random and must stay the same for
 *        as long as you use the function for your specific hash table.
 *        Simply use the following on boot: siphash_generate_key(k);
 */
uint64_t siphash(const char *in, const char *k)
{
    size_t inlen = strlen(in);

    return siphash_raw(in, inlen, k);
}
/** Generate a key that is used by siphash() and siphash_nocase().
 * @param k   The key, this must be a char array of size 16.
 */
void siphash_generate_key(char *k)
{
	int i;
	for (i = 0; i < 16; i++)
		k[i] = getrandom8();
}

static struct list_head clientTable[NICK_HASH_TABLE_SIZE];
static struct list_head idTable[NICK_HASH_TABLE_SIZE];
static Channel *channelTable[CHAN_HASH_TABLE_SIZE];
static Watch *watchTable[WATCH_HASH_TABLE_SIZE];

static char siphashkey_nick[SIPHASH_KEY_LENGTH];
static char siphashkey_chan[SIPHASH_KEY_LENGTH];
static char siphashkey_watch[SIPHASH_KEY_LENGTH];
static char siphashkey_whowas[SIPHASH_KEY_LENGTH];
static char siphashkey_throttling[SIPHASH_KEY_LENGTH];

extern char unreallogo[];

/** Initialize all hash tables */
void init_hash(void)
{
	int i;

	siphash_generate_key(siphashkey_nick);
	siphash_generate_key(siphashkey_chan);
	siphash_generate_key(siphashkey_watch);
	siphash_generate_key(siphashkey_whowas);
	siphash_generate_key(siphashkey_throttling);

	for (i = 0; i < NICK_HASH_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&clientTable[i]);

	for (i = 0; i < NICK_HASH_TABLE_SIZE; i++)
		INIT_LIST_HEAD(&idTable[i]);

	memset(channelTable, 0, sizeof(channelTable));
	memset(watchTable, 0, sizeof(watchTable));

	memset(ThrottlingHash, 0, sizeof(ThrottlingHash));
	/* do not call init_throttling() here, as
	 * config file has not been read yet.
	 * The hash table is ready, anyway.
	 */

	if (strcmp(BASE_VERSION, &unreallogo[337]))
		loop.tainted = 1;
}

uint64_t hash_client_name(const char *name)
{
	return siphash_nocase(name, siphashkey_nick) % NICK_HASH_TABLE_SIZE;
}

uint64_t hash_channel_name(const char *name)
{
	return siphash_nocase(name, siphashkey_chan) % CHAN_HASH_TABLE_SIZE;
}

uint64_t hash_watch_nick_name(const char *name)
{
	return siphash_nocase(name, siphashkey_watch) % WATCH_HASH_TABLE_SIZE;
}

uint64_t hash_whowas_name(const char *name)
{
	return siphash_nocase(name, siphashkey_whowas) % WHOWAS_HASH_TABLE_SIZE;
}

/*
 * add_to_client_hash_table
 */
int add_to_client_hash_table(char *name, Client *cptr)
{
	unsigned int hashv;
	/*
	 * If you see this, you have probably found your way to why changing the 
	 * base version made the IRCd become weird. This has been the case in all
	 * Unreal versions since 3.0. I'm sick of people ripping the IRCd off and 
	 * just slapping on some random <theirnet> BASE_VERSION while not changing
	 * a single bit of code. YOU DID NOT WRITE ALL OF THIS THEREFORE YOU DO NOT
	 * DESERVE TO BE ABLE TO DO THAT. If you found this however, I'm OK with you 
	 * removing the checks. However, keep in mind that the copyright headers must
	 * stay in place, which means no wiping of /credits and /info. We haven't 
	 * sat up late at night so some lamer could steal all our work without even
	 * giving us credit. Remember to follow all regulations in LICENSE.
	 * -Stskeeps
	*/
	if (loop.tainted)
		return 0;
	hashv = hash_client_name(name);
	list_add(&cptr->client_hash, &clientTable[hashv]);
	return 0;
}

/*
 * add_to_client_hash_table
 */
int add_to_id_hash_table(char *name, Client *cptr)
{
	unsigned int hashv;
	hashv = hash_client_name(name);
	list_add(&cptr->id_hash, &idTable[hashv]);
	return 0;
}

/*
 * add_to_channel_hash_table
 */
int add_to_channel_hash_table(char *name, Channel *chptr)
{
	unsigned int hashv;

	hashv = hash_channel_name(name);
	chptr->hnextch = channelTable[hashv];
	channelTable[hashv] = chptr;
	return 0;
}
/*
 * del_from_client_hash_table
 */
int del_from_client_hash_table(char *name, Client *cptr)
{
	if (!list_empty(&cptr->client_hash))
		list_del(&cptr->client_hash);

	INIT_LIST_HEAD(&cptr->client_hash);

	return 0;
}

int del_from_id_hash_table(char *name, Client *cptr)
{
	if (!list_empty(&cptr->id_hash))
		list_del(&cptr->id_hash);

	INIT_LIST_HEAD(&cptr->id_hash);

	return 0;
}

/*
 * del_from_channel_hash_table
 */
void del_from_channel_hash_table(char *name, Channel *chptr)
{
	Channel *tmp, *prev = NULL;
	unsigned int hashv;

	hashv = hash_channel_name(name);
	for (tmp = channelTable[hashv]; tmp; tmp = tmp->hnextch)
	{
		if (tmp == chptr)
		{
			if (prev)
				prev->hnextch = tmp->hnextch;
			else
				channelTable[hashv] = tmp->hnextch;
			tmp->hnextch = NULL;
			return; /* DONE */
		}
		prev = tmp;
	}
	return; /* NOTFOUND */
}

/*
 * hash_find_client
 */
Client *hash_find_client(const char *name, Client *cptr)
{
	Client *tmp;
	unsigned int hashv;

	hashv = hash_client_name(name);
	list_for_each_entry(tmp, &clientTable[hashv], client_hash)
	{
		if (smycmp(name, tmp->name) == 0)
			return tmp;
	}

	return cptr;
}

Client *hash_find_id(const char *name, Client *cptr)
{
	Client *tmp;
	unsigned int hashv;

	hashv = hash_client_name(name);
	list_for_each_entry(tmp, &idTable[hashv], id_hash)
	{
		if (smycmp(name, tmp->id) == 0)
			return tmp;
	}

	return cptr;
}

/*
 * hash_find_nickatserver
 */
Client *hash_find_nickatserver(const char *str, Client *cptr)
{
	char *serv;
	char nick[NICKLEN+HOSTLEN+1];
	Client *acptr;
	
	strlcpy(nick, str, sizeof(nick)); /* let's work on a copy */

	serv = strchr(nick, '@');
	if (serv)
		*serv++ = '\0';

	acptr = find_client(nick, NULL);
	if (!acptr)
		return NULL; /* client not found */
	
	if (!serv)
		return acptr; /* validated: was just 'nick' and not 'nick@serv' */

	/* Now validate the server portion */
	if (acptr->user && !smycmp(serv, acptr->user->server))
		return acptr; /* validated */
	
	return cptr;
}
/*
 * hash_find_server
 */
Client *hash_find_server(const char *server, Client *cptr)
{
	Client *tmp;
	unsigned int hashv;

	hashv = hash_client_name(server);
	list_for_each_entry(tmp, &clientTable[hashv], client_hash)
	{
		if (!IsServer(tmp) && !IsMe(tmp))
			continue;
		if (smycmp(server, tmp->name) == 0)
		{
			return tmp;
		}
	}

	return cptr;
}

/** Find a client by name.
 * @param name   The name to search for (eg: "nick" or "irc.example.net")
 * @param cptr   The client that is searching for this name
 * @notes If 'cptr' is a server or NULL, then we also check
 *        the ID table, otherwise not.
 */
Client *find_client(char *name, Client *cptr)
{
	if (cptr == NULL || IsServer(cptr))
	{
		Client *acptr;

		if ((acptr = hash_find_id(name, NULL)) != NULL)
			return acptr;
	}

	return hash_find_client(name, NULL);
}

/** Find a server by name.
 * @param name   The server name to search for (eg: 'irc.example.net'
 *               or '001')
 * @param cptr   The client searching for the name.
 * @notes If 'cptr' is a server or NULL, then we also check
 *        the ID table, otherwise not.
 */
Client *find_server(char *name, Client *cptr)
{
	if (name)
	{
		Client *acptr;

		if ((acptr = find_client(name, NULL)) != NULL && (IsServer(acptr) || IsMe(acptr)))
			return acptr;
	}

	return NULL;
}

/** Find a person.
 * @param name   The name to search for (eg: "nick" or "001ABCDEFG")
 * @param cptr   The client that is searching for this name
 * @notes If 'cptr' is a server or NULL, then we also check
 *        the ID table, otherwise not.
 */
Client *find_person(char *name, Client *cptr)
{
	Client *c2ptr;

	c2ptr = find_client(name, cptr);

	if (c2ptr && IsUser(c2ptr) && c2ptr->user)
		return c2ptr;

	return NULL;
}


/*
 * hash_find_channel
 */
Channel *hash_find_channel(char *name, Channel *chptr)
{
	unsigned int hashv;
	Channel *tmp;

	hashv = hash_channel_name(name);

	for (tmp = channelTable[hashv]; tmp; tmp = tmp->hnextch)
	{
		if (smycmp(name, tmp->chname) == 0)
			return tmp;
	}
	return chptr;
}

Channel *hash_get_chan_bucket(uint64_t hashv)
{
	if (hashv > CHAN_HASH_TABLE_SIZE)
		return NULL;
	return channelTable[hashv];
}

void  count_watch_memory(int *count, u_long *memory)
{
	int i = WATCH_HASH_TABLE_SIZE;
	Watch *anptr;

	while (i--)
	{
		anptr = watchTable[i];
		while (anptr)
		{
			(*count)++;
			(*memory) += sizeof(Watch)+strlen(anptr->nick);
			anptr = anptr->hnext;
		}
	}
}

/*
 * add_to_watch_hash_table
 */
int add_to_watch_hash_table(char *nick, Client *cptr, int awaynotify)
{
	unsigned int hashv;
	Watch  *anptr;
	Link  *lp;
	
	
	/* Get the right bucket... */
	hashv = hash_watch_nick_name(nick);
	
	/* Find the right nick (header) in the bucket, or NULL... */
	if ((anptr = (Watch *)watchTable[hashv]))
	  while (anptr && mycmp(anptr->nick, nick))
		 anptr = anptr->hnext;
	
	/* If found NULL (no header for this nick), make one... */
	if (!anptr) {
		anptr = (Watch *)safe_alloc(sizeof(Watch)+strlen(nick));
		anptr->lasttime = timeofday;
		strcpy(anptr->nick, nick);
		
		anptr->watch = NULL;
		
		anptr->hnext = watchTable[hashv];
		watchTable[hashv] = anptr;
	}
	/* Is this client already on the watch-list? */
	if ((lp = anptr->watch))
	  while (lp && (lp->value.cptr != cptr))
		 lp = lp->next;
	
	/* No it isn't, so add it in the bucket and client addint it */
	if (!lp) {
		lp = anptr->watch;
		anptr->watch = make_link();
		anptr->watch->value.cptr = cptr;
		anptr->watch->flags = awaynotify;
		anptr->watch->next = lp;
		
		lp = make_link();
		lp->next = cptr->local->watch;
		lp->value.wptr = anptr;
		lp->flags = awaynotify;
		cptr->local->watch = lp;
		cptr->local->watches++;
	}
	
	return 0;
}

/*
 *  hash_check_watch
 */
int hash_check_watch(Client *cptr, int reply)
{
	unsigned int hashv;
	Watch  *anptr;
	Link  *lp;
	int awaynotify = 0;
	
	if ((reply == RPL_GONEAWAY) || (reply == RPL_NOTAWAY) || (reply == RPL_REAWAY))
		awaynotify = 1;
	
	
	/* Get us the right bucket */
	hashv = hash_watch_nick_name(cptr->name);
	
	/* Find the right header in this bucket */
	if ((anptr = (Watch *)watchTable[hashv]))
	  while (anptr && mycmp(anptr->nick, cptr->name))
		 anptr = anptr->hnext;
	if (!anptr)
	  return 0;   /* This nick isn't on watch */
	
	/* Update the time of last change to item */
	anptr->lasttime = TStime();
	
	/* Send notifies out to everybody on the list in header */
	for (lp = anptr->watch; lp; lp = lp->next)
	{
		if (!awaynotify)
		{
			sendnumeric(lp->value.cptr, reply,
			    cptr->name,
			    (IsUser(cptr) ? cptr->user->username : "<N/A>"),
			    (IsUser(cptr) ?
			    (IsHidden(cptr) ? cptr->user->virthost : cptr->
			    user->realhost) : "<N/A>"), anptr->lasttime, cptr->info);
		}
		else
		{
			/* AWAY or UNAWAY */
			if (!lp->flags)
				continue; /* skip away/unaway notification for users not interested in them */

			if (reply == RPL_NOTAWAY)
				sendnumeric(lp->value.cptr, reply,
				    cptr->name,
				    (IsUser(cptr) ? cptr->user->username : "<N/A>"),
				    (IsUser(cptr) ?
				    (IsHidden(cptr) ? cptr->user->virthost : cptr->
				    user->realhost) : "<N/A>"), cptr->user->lastaway);
			else /* RPL_GONEAWAY / RPL_REAWAY */
				sendnumeric(lp->value.cptr, reply,
				    cptr->name,
				    (IsUser(cptr) ? cptr->user->username : "<N/A>"),
				    (IsUser(cptr) ?
				    (IsHidden(cptr) ? cptr->user->virthost : cptr->
				    user->realhost) : "<N/A>"), cptr->user->lastaway, cptr->user->away);
		}
	}
	
	return 0;
}

/*
 * hash_get_watch
 */
Watch  *hash_get_watch(char *nick)
{
	unsigned int hashv;
	Watch  *anptr;
	
	hashv = hash_watch_nick_name(nick);
	
	if ((anptr = (Watch *)watchTable[hashv]))
	  while (anptr && mycmp(anptr->nick, nick))
		 anptr = anptr->hnext;
	
	return anptr;
}

/*
 * del_from_watch_hash_table
 */
int del_from_watch_hash_table(char *nick, Client *cptr)
{
	unsigned int hashv;
	Watch  *anptr, *nlast = NULL;
	Link  *lp, *last = NULL;

	/* Get the bucket for this nick... */
	hashv = hash_watch_nick_name(nick);
	
	/* Find the right header, maintaining last-link pointer... */
	if ((anptr = (Watch *)watchTable[hashv]))
	  while (anptr && mycmp(anptr->nick, nick)) {
		  nlast = anptr;
		  anptr = anptr->hnext;
	  }
	if (!anptr)
	  return 0;   /* No such watch */
	
	/* Find this client from the list of notifies... with last-ptr. */
	if ((lp = anptr->watch))
	  while (lp && (lp->value.cptr != cptr)) {
		  last = lp;
		  lp = lp->next;
	  }
	if (!lp)
	  return 0;   /* No such client to watch */
	
	/* Fix the linked list under header, then remove the watch entry */
	if (!last)
	  anptr->watch = lp->next;
	else
	  last->next = lp->next;
	free_link(lp);
	
	/* Do the same regarding the links in client-record... */
	last = NULL;
	if ((lp = cptr->local->watch))
	  while (lp && (lp->value.wptr != anptr)) {
		  last = lp;
		  lp = lp->next;
	  }
	
	/*
	 * Give error on the odd case... probobly not even neccessary
	 * No error checking in ircd is unneccessary ;) -Cabal95
	 */
	if (!lp)
	  sendto_ops("WATCH debug error: del_from_watch_hash_table "
					 "found a watch entry with no client "
					 "counterpoint processing nick %s on client %p!",
					 nick, cptr->user);
	else {
		if (!last) /* First one matched */
		  cptr->local->watch = lp->next;
		else
		  last->next = lp->next;
		free_link(lp);
	}
	/* In case this header is now empty of notices, remove it */
	if (!anptr->watch) {
		if (!nlast)
		  watchTable[hashv] = anptr->hnext;
		else
		  nlast->hnext = anptr->hnext;
		safe_free(anptr);
	}
	
	/* Update count of notifies on nick */
	cptr->local->watches--;
	
	return 0;
}

/*
 * hash_del_watch_list
 */
int   hash_del_watch_list(Client *cptr)
{
	unsigned int   hashv;
	Watch  *anptr;
	Link  *np, *lp, *last;
	
	
	if (!(np = cptr->local->watch))
	  return 0;   /* Nothing to do */
	
	cptr->local->watch = NULL; /* Break the watch-list for client */
	while (np) {
		/* Find the watch-record from hash-table... */
		anptr = np->value.wptr;
		last = NULL;
		for (lp = anptr->watch; lp && (lp->value.cptr != cptr);
			  lp = lp->next)
		  last = lp;
		
		/* Not found, another "worst case" debug error */
		if (!lp)
		  sendto_ops("WATCH Debug error: hash_del_watch_list "
						 "found a WATCH entry with no table "
						 "counterpoint processing client %s!",
						 cptr->name);
		else {
			/* Fix the watch-list and remove entry */
			if (!last)
			  anptr->watch = lp->next;
			else
			  last->next = lp->next;
			free_link(lp);
			
			/*
			 * If this leaves a header without notifies,
			 * remove it. Need to find the last-pointer!
			 */
			if (!anptr->watch) {
				Watch  *np2, *nl;
				
				hashv = hash_watch_nick_name(anptr->nick);
				
				nl = NULL;
				np2 = watchTable[hashv];
				while (np2 != anptr) {
					nl = np2;
					np2 = np2->hnext;
				}
				
				if (nl)
				  nl->hnext = anptr->hnext;
				else
				  watchTable[hashv] = anptr->hnext;
				safe_free(anptr);
			}
		}
		
		lp = np; /* Save last pointer processed */
		np = np->next; /* Jump to the next pointer */
		free_link(lp); /* Free the previous */
	}
	
	cptr->local->watches = 0;
	
	return 0;
}

/* Throttling - originally by Stskeeps */

/* Note that we call this set::anti-flood::connect-flood nowadays */

struct MODVAR ThrottlingBucket *ThrottlingHash[THROTTLING_HASH_TABLE_SIZE];

void init_throttling()
{
	long v;

	if (!THROTTLING_PERIOD)
	{
		v = 120;
	} else
	{
		v = THROTTLING_PERIOD/2;
		if (v > 5)
			v = 5000; /* run at least every 5s */
		if (v < 1)
			v = 1000; /* run at max once every 1s */
	}
	EventAdd(NULL, "bucketcleaning", e_clean_out_throttling_buckets, NULL, v, 0);
}

uint64_t hash_throttling(char *ip)
{
	return siphash(ip, siphashkey_throttling) % THROTTLING_HASH_TABLE_SIZE;
}

struct ThrottlingBucket *find_throttling_bucket(Client *acptr)
{
	int hash = 0;
	struct ThrottlingBucket *p;
	hash = hash_throttling(acptr->ip);
	
	for (p = ThrottlingHash[hash]; p; p = p->next)
	{
		if (!strcmp(p->ip, acptr->ip))
			return p;
	}
	
	return NULL;
}

EVENT(e_clean_out_throttling_buckets)
{
	struct ThrottlingBucket *n, *n_next;
	int	i;
	static time_t t = 0;
		
	for (i = 0; i < THROTTLING_HASH_TABLE_SIZE; i++)
	{
		for (n = ThrottlingHash[i]; n; n = n_next)
		{
			n_next = n->next;
			if ((TStime() - n->since) > (THROTTLING_PERIOD ? THROTTLING_PERIOD : 15))
			{
				DelListItem(n, ThrottlingHash[i]);
				safe_free(n->ip);
				safe_free(n);
			}
		}
	}

	if (!t || (TStime() - t > 30))
	{
		extern Module *Modules;
		char *p = serveropts + strlen(serveropts);
		Module *mi;
		t = TStime();
		if (!Hooks[17] && strchr(serveropts, 'm'))
		{ p = strchr(serveropts, 'm'); *p = '\0'; }
		if (!Hooks[18] && strchr(serveropts, 'M'))
		{ p = strchr(serveropts, 'M'); *p = '\0'; }
		if (!Hooks[49] && !Hooks[51] && strchr(serveropts, 'R'))
		{ p = strchr(serveropts, 'R'); *p = '\0'; }
		if (Hooks[17] && !strchr(serveropts, 'm'))
			*p++ = 'm';
		if (Hooks[18] && !strchr(serveropts, 'M'))
			*p++ = 'M';
		if ((Hooks[49] || Hooks[51]) && !strchr(serveropts, 'R'))
			*p++ = 'R';
		*p = '\0';
		for (mi = Modules; mi; mi = mi->next)
			if (!(mi->options & MOD_OPT_OFFICIAL))
				tainted = 99;
	}

	return;
}

void add_throttling_bucket(Client *acptr)
{
	int hash;
	struct ThrottlingBucket *n;

	n = safe_alloc(sizeof(struct ThrottlingBucket));	
	n->next = n->prev = NULL; 
	safe_strdup(n->ip, acptr->ip);
	n->since = TStime();
	n->count = 1;
	hash = hash_throttling(acptr->ip);
	AddListItem(n, ThrottlingHash[hash]);
	return;
}

/** Checks whether the user is connect-flooding.
 * @retval 0 Denied, throttled.
 * @retval 1 Allowed, but known in the list.
 * @retval 2 Allowed, not in list or is an exception.
 * @see add_connection()
 */
int throttle_can_connect(Client *sptr)
{
	struct ThrottlingBucket *b;

	if (!THROTTLING_PERIOD || !THROTTLING_COUNT)
		return 2;

	if (!(b = find_throttling_bucket(sptr)))
		return 1;
	else
	{
		if (find_tkl_exception(TKL_CONNECT_FLOOD, sptr))
			return 2;
		if (b->count+1 > (THROTTLING_COUNT ? THROTTLING_COUNT : 3))
			return 0;
		b->count++;
		return 2;
	}
}
