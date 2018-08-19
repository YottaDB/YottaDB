/****************************************************************
 *								*
 * Copyright (c) 2009-2018 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2018 YottaDB LLC. and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <gpgme.h>				/* gpgme functions */
#include <gpg-error.h>				/* gcry*_err_t */
#include <libconfig.h>

#include "gtmxc_types.h"

#include "gtmcrypt_util.h"
#include "gtmcrypt_interface.h"			/* Function prototypes for gtmcrypt*.* functions */

#include "gtmcrypt_ref.h"
#include "gtmcrypt_dbk_ref.h"
#include "gtmcrypt_sym_ref.h"
#include "gtmcrypt_pk_ref.h"
#include "ydb_getenv.h"

#define	UNRES_KEY_FILE		0	/* Key is for device encryption. */
#define	UNRES_KEY_UNRES_DB	1	/* Key is for a database that does not yet exist. */
#define	UNRES_KEY_RES_DB	2	/* Key is for a database that already exists. */

#define	SEARCH_BY_KEYNAME	0	/* Searching for an unresolved key by name. */
#define	SEARCH_BY_KEYPATH	1	/* Searching for an unresolved key by path. */
#define	SEARCH_BY_HASH		2	/* Searching for an unresolved key by hash. */

#define CONFIG_FILE_UNREAD	('\0' == gc_config_filename[0])
#define GPG_MESSAGE		"Verify encrypted key file and your GNUPGHOME settings"
#define NON_GPG_MESSAGE		"Verify encryption key in configuration file pointed to by $ydb_crypt_config/$gtmcrypt_config"

/* On certain platforms the st_mtime field of the stat structure got replaced by a timespec st_mtim field, which in turn has tv_sec
 * and tv_nsec fields. For compatibility reasons, those platforms define an st_mtime macro which points to st_mtim.tv_sec. Whenever
 * we detect such a situation, we define a nanosecond flavor of that macro to point to st_mtim.tv_nsec. On HPUX Itanium and older
 * AIX boxes the stat structure simply has additional fields with the nanoseconds value, yet the names of those field are different
 * on those two architectures, so we choose our mapping accordingly.
 */
#if defined st_mtime && !defined(__APPLE__)
#  define st_nmtime				st_mtim.tv_nsec
#elif defined(_AIX)
#  define st_nmtime				st_mtime_n
#elif defined(__hpux) && defined(__ia64)
#  define st_nmtime				st_nmtime
#elif defined(__APPLE__)
#  define st_nmtime				st_mtimespec.tv_nsec
#endif

/* Insert a new gtm_keystore_xxx_link_t element in a respective tree. */
#define INSERT_KEY_LINK(ROOT, LINK, TYPE, FIELD, VALUE, LENGTH, FILL_LEN, FIXED, DUPL)	\
{											\
	int	diff;									\
	TYPE	*cur_node, **target_node;						\
											\
	target_node = &ROOT;								\
	while (cur_node = *target_node)	/* NOTE: Assignment!!! */			\
	{										\
		diff = FIXED								\
			? memcmp(cur_node->FIELD, VALUE, LENGTH)			\
			: strcmp((char *)cur_node->FIELD, (char *)VALUE);		\
		assert(DUPL || (0 != diff));						\
		if (0 >= diff)								\
			target_node = &cur_node->left;					\
		else									\
			target_node = &cur_node->right;					\
	}										\
	/* Allocate and initialize a gtm_keystore_xxx_link_t element. */		\
	*target_node = (TYPE *)MALLOC(SIZEOF(TYPE));					\
	(*target_node)->left = (*target_node)->right = NULL;				\
	(*target_node)->link = LINK;							\
	memset((*target_node)->FIELD, 0, FILL_LEN);					\
	memcpy((*target_node)->FIELD, VALUE, LENGTH);					\
}

/* Remove a link from the unresolved list (because it is now resolved or is a duplicate). */
#define REMOVE_UNRESOLVED_LINK(CUR, PREV)						\
{											\
	gtm_keystore_unres_key_link_t *next;						\
											\
	next = (CUR)->next;								\
	if (NULL != PREV)								\
		(PREV)->next = next;							\
	else										\
	{										\
		assert(CUR == keystore_by_unres_key_head);				\
		keystore_by_unres_key_head = next;					\
	}										\
	FREE(CUR);									\
	CUR = next;									\
}

STATICDEF int					n_keys;					/* Count of how many keys were loaded. */
STATICDEF char					gc_config_filename[YDB_PATH_MAX];	/* Path to the configuration file. */
STATICDEF gtm_keystore_hash_link_t		*keystore_by_hash_head = NULL;		/* Root of the binary search tree to look
											 * keys up by hash. */
STATICDEF gtm_keystore_keyname_link_t		*keystore_by_keyname_head = NULL;	/* Root of the binary search tree to look
											 * keys up by name. */
STATICDEF gtm_keystore_keypath_link_t		*keystore_by_keypath_head = NULL;	/* Root of the binary search tree to look
											 * keys up by path. */
STATICDEF gtm_keystore_unres_key_link_t		*keystore_by_unres_key_head = NULL;	/* Head of the linked list holding keys of
											 * DBs with presently unresolved paths. */
STATICDEF config_t				gtmcrypt_cfg;				/* Encryption configuration. */
STATICDEF char					path_array[YDB_PATH_MAX];		/* Array for temporary storage of keys or
											 * DBs' real path information. */
STATICDEF unsigned char				key_hash_array[GTMCRYPT_HASH_LEN];	/* Array for temporary storage of keys'
											 * hashes. */

GBLREF	passwd_entry_t				*gtmcrypt_pwent;
GBLREF	int					gtmcrypt_init_flags;

/*
 * Find the key based on its name.
 *
 * Arguments:	key_name	Name of the key.
 * 		key_path	Path to the key (optional).
 * 		entry		Address where to place the pointer to the found key.
 * 		database	Flag indicating whether a database (or device) key is being searched.
 *
 * Returns:	0 if the key with the specified name is found; -1 otherwise.
 */
int gtmcrypt_getkey_by_keyname(char *key_name, char *key_path, gtm_keystore_t **entry, int database)
{
	int error;

	if (NULL != key_path)
		*entry = keystore_lookup_by_keyname_plus(key_name, key_path, SEARCH_BY_KEYPATH);
	else
		*entry = keystore_lookup_by_keyname(key_name);
	if (NULL == *entry)
	{	/* No matches in the binary tree; trying the unresolved key list. */
		if (0 != keystore_refresh())
			return -1;
		error = 0;
		if (NULL == (*entry = keystore_lookup_by_unres_key(key_name, SEARCH_BY_KEYNAME,
					key_path, SEARCH_BY_KEYPATH, database, &error)))
		{
			if (!error)
			{
				if (NULL == key_path)
				{
					UPDATE_ERROR_STRING("%s " STR_ARG " missing in configuration file or does not exist",
						(database ? "Database file" : "Keyname"), ELLIPSIZE(key_name));
				} else
				{
					UPDATE_ERROR_STRING("%s " STR_ARG " missing in configuration file, does not exist, or is "
						"not associated with key " STR_ARG,  (database ? "Database file" : "Keyname"),
						ELLIPSIZE(key_name), ELLIPSIZE(key_path));
				}
			}
			return -1;
		}
	}
	assert(NULL != *entry);
	return 0;
}

/*
 * Find the key based on its hash.
 *
 * Arguments:	hash	Hash of the key.
 * 		db_path	Path to the key (optional).
 * 		entry	Address where to place the pointer to the found key.
 *
 * Returns:	0 if the key with the specified name is found; -1 otherwise.
 */
int gtmcrypt_getkey_by_hash(unsigned char *hash, char *db_path, gtm_keystore_t **entry)
{
	int	err_caused_by_gpg, error, errorlen;
	char	save_err[MAX_GTMCRYPT_ERR_STRLEN + 1], hex_buff[GTMCRYPT_HASH_HEX_LEN + 1];
	char	*alert_msg;

	if (NULL != db_path)
		*entry = keystore_lookup_by_keyname_plus(db_path, (char *)hash, SEARCH_BY_HASH);
	else
		*entry = keystore_lookup_by_hash(hash);
	if (NULL == *entry)
	{	/* No matches in the binary tree; trying the unresolved key list. */
		if (0 != keystore_refresh())
			return -1;
		error = 0;
		if (NULL == (*entry = keystore_lookup_by_unres_key((char *)hash, SEARCH_BY_HASH,
					db_path, SEARCH_BY_KEYNAME, TRUE, &error)))
		{
			if (!error)
			{	/* Be specific in the error as to what hash we were trying to find. */
				err_caused_by_gpg = ('\0' != gtmcrypt_err_string[0]);
				alert_msg = err_caused_by_gpg ? GPG_MESSAGE : NON_GPG_MESSAGE;
				GC_HEX(hash, hex_buff, GTMCRYPT_HASH_HEX_LEN);
				if (err_caused_by_gpg)
				{
					errorlen = STRLEN(gtmcrypt_err_string);
					if (MAX_GTMCRYPT_ERR_STRLEN < errorlen)
						errorlen = MAX_GTMCRYPT_ERR_STRLEN;
					memcpy(save_err, gtmcrypt_err_string, errorlen);
					save_err[errorlen] = '\0';
					UPDATE_ERROR_STRING("Expected hash - " STR_ARG " - %s. %s",
						ELLIPSIZE(hex_buff), save_err, alert_msg);
				} else
					UPDATE_ERROR_STRING("Expected hash - " STR_ARG ". %s", ELLIPSIZE(hex_buff), alert_msg);
			}
			return -1;
		}
	}
	assert(NULL != *entry);
	return 0;
}

/*
 * Helper function to perform the actual binary search of the key by its hash.
 *
 * Arguments:	hash	Hash of the key.
 *
 * Returns:	Pointer to the key, if found; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *keystore_lookup_by_hash(unsigned char *hash)
{
	int				diff;
	gtm_keystore_hash_link_t	*cur_node;

	cur_node = keystore_by_hash_head;
	while (cur_node)
	{
		diff = memcmp(cur_node->link->key_hash, hash, GTMCRYPT_HASH_LEN);
		if (0 < diff)
			cur_node = cur_node->right;
		else if (0 == diff)
			return cur_node->link;
		else
			cur_node = cur_node->left;
	}
	return NULL;
}

/*
 * Helper function to perform the actual binary search of the key by its path.
 *
 * Arguments:	keypath		Path to the key.
 *
 * Returns:	Pointer to the key, if found; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *keystore_lookup_by_keypath(char *keypath)
{
	int				diff;
	gtm_keystore_keypath_link_t	*cur_node;

	cur_node = keystore_by_keypath_head;
	while (cur_node)
	{
		diff = strcmp(cur_node->link->key_path, keypath);
		if (0 < diff)
			cur_node = cur_node->right;
		else if (0 == diff)
			return cur_node->link;
		else
			cur_node = cur_node->left;
	}
	return NULL;
}

/*
 * Helper function to perform the actual binary search of the key by its name.
 *
 * Arguments:	keyname	Name of the key.
 *
 * Returns:	Pointer to the key, if found; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *keystore_lookup_by_keyname(char *keyname)
{
	int				diff;
	gtm_keystore_keyname_link_t 	*cur_node;

	cur_node = keystore_by_keyname_head;
	while (cur_node)
	{
		diff = strcmp(cur_node->key_name, keyname);
		if (0 < diff)
			cur_node = cur_node->right;
		else if (0 == diff)
			return cur_node->link;
		else
			cur_node = cur_node->left;
	}
	return NULL;
}

/*
 * Helper function to perform the actual binary search of the key by its name.
 *
 * Arguments:	keyname		Name of the key.
 * 		search_field	Value of the seconds search criterion besides the key name.
 * 		search_type	Type of the second search criterion, either a key path or hash.
 *
 * Returns:	Pointer to the key, if found; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *keystore_lookup_by_keyname_plus(char *keyname, char *search_field, int search_type)
{
	int				diff, match;
	gtm_keystore_keyname_link_t 	*cur_node;
	char				*ynew_ext;
	char				lcl_keyname[YDB_PATH_MAX];
	int				keynamelen;

	assert((SEARCH_BY_KEYPATH == search_type) || (SEARCH_BY_HASH == search_type));
	assert(NULL != search_field);
	assert(keyname);
	/* Strip off EXT_NEW from autodb paths so that the key lookup works correctly */
	keynamelen = strlen(keyname);
	ynew_ext = keyname + keynamelen - STRLEN(EXT_NEW);
	if ((ynew_ext >= keyname) && (0 == strcmp(ynew_ext, EXT_NEW)))
	{	/* This is an autodb, fixup the path */
		memcpy(lcl_keyname, keyname, keynamelen - STRLEN(EXT_NEW));
		lcl_keyname[keynamelen - STRLEN(EXT_NEW)] = '\0';
		keyname = lcl_keyname;
	}
	cur_node = keystore_by_keyname_head;
	while (cur_node)
	{
		diff = strcmp(cur_node->key_name, keyname);
		if (0 < diff)
			cur_node = cur_node->right;
		else if (0 == diff)
		{
			if (SEARCH_BY_KEYPATH == search_type)
				match = (0 == strcmp(cur_node->link->key_path, search_field));
			else
				match = (0 == memcmp(cur_node->link->key_hash, search_field, GTMCRYPT_HASH_LEN));
			if (match)
				return cur_node->link;
			else if (NULL == cur_node->left)
				return NULL;
			else
				cur_node = cur_node->left;
		} else
			cur_node = cur_node->left;
	}
	return NULL;
}

/*
 * Helper function to perform a linear search of the key by its name or hash in the unresolved keys list. It attempts to resolve the
 * real path of a keyname in case it corresponds to a previously unresolved database name. If the path is resolved, the node's entry
 * is used to create (as needed) new key node as well as hash-, keyname-, and keypath-based links to it, and the unresolved entry is
 * removed from the list.
 *
 * Arguments:	search_field1		Value of the first search criterion for unresolved keys.
 * 		search_field1_type	Type of the first search criterion, either the key name or hash.
 * 		search_field2		Value of the second search criterion for unresolved keys.
 * 		search_field2_type	Type of the second search criterion, either the key path or name.
 *		database		Flag indicating whether the search is for a database or device encryption key.
 * 		error			Address where to set the flag indicating whether an error was encountered.
 *
 * Returns:	Pointer to the key, if found; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *keystore_lookup_by_unres_key(char *search_field1, int search_field1_type,
		char *search_field2, int search_field2_type, int database, int *error)
{
	gtm_keystore_unres_key_link_t	*curr, *prev;
	gtm_keystore_t			*node;
	int				name_length, path_length, search_fail;
	char				*name_search_field_ptr, *path_search_field_ptr, *ynew_ext;
	char				*lcl_key_name, lcl_key_name_buff[YDB_PATH_MAX];
	char				name_search_field_buff[YDB_PATH_MAX];
	int				search_field_len;
	int				isautodb;

	assert(NULL != search_field1);
	assert((SEARCH_BY_KEYNAME == search_field1_type) || (SEARCH_BY_HASH == search_field1_type));
	assert((SEARCH_BY_KEYPATH == search_field2_type) || (SEARCH_BY_KEYNAME == search_field2_type));
	assert(((SEARCH_BY_KEYNAME == search_field1_type) && (SEARCH_BY_KEYPATH == search_field2_type))
			|| ((SEARCH_BY_HASH == search_field1_type) && (SEARCH_BY_KEYNAME == search_field2_type)));
	/* Prepare the character array pointers to use for searching by key name or path. */
	path_search_field_ptr = NULL;
	isautodb = FALSE;
	if (SEARCH_BY_KEYNAME == search_field1_type)
	{
		name_search_field_ptr = search_field1;
		if (database && (NULL == search_field2))
		{	/* Newly created AutoDBs have EXT_NEW appended to them, but the crypt cfg doesn't have those keys */
			search_field_len = strlen(search_field1);
			ynew_ext = search_field1 + search_field_len - STRLEN(EXT_NEW);
			if (0 == strcmp(ynew_ext, EXT_NEW))
			{	/* Strip EXT_NEW off the path string for comparison later. Note that this path, minus EXT_NEW,
				 * is a fully resolved path.
				 */
				isautodb = TRUE;
				memcpy(name_search_field_buff, search_field1, search_field_len - STRLEN(EXT_NEW));
				name_search_field_buff[search_field_len - STRLEN(EXT_NEW)] = '\0';
				name_search_field_ptr = name_search_field_buff;
			}
		}
		if ((NULL != search_field2) && (SEARCH_BY_KEYPATH == search_field2_type))
			path_search_field_ptr = search_field2;
	} else if ((NULL != search_field2) && (SEARCH_BY_KEYNAME == search_field2_type))
		name_search_field_ptr = search_field2;
	else
		name_search_field_ptr = NULL;
	/* Start the main search loop. */
	prev = NULL;
	curr = keystore_by_unres_key_head;
	while (curr)
	{	/* Skip entries whose type does not match the one we are searching for. */
		if ((database && (UNRES_KEY_FILE != curr->status)) || (!database && (UNRES_KEY_FILE == curr->status)))
		{	/* If the database file has not been resolved yet, try resolving it. */
			search_fail = 0;
			if (UNRES_KEY_UNRES_DB == curr->status)
			{
				if (isautodb)
				{	/* Append EXT_NEW to see if this a matching AutoDB */
					strncpy(lcl_key_name_buff, curr->key_name, YDB_PATH_MAX);
					strcat(lcl_key_name_buff, EXT_NEW);
					lcl_key_name = lcl_key_name_buff;
				} else
					lcl_key_name = curr->key_name;
				if (NULL == realpath(lcl_key_name, path_array))
				{
					if (ENAMETOOLONG == errno)
					{
						*error = TRUE;
						UPDATE_ERROR_STRING("Real path, or a component of the path, of the database "
							STR_ARG " is too long", ELLIPSIZE(curr->key_name));
						return NULL;
					} else if (ENOENT != errno)
					{
						*error = TRUE;
						UPDATE_ERROR_STRING("Could not obtain the real path of the database " STR_ARG
							". %s", ELLIPSIZE(curr->key_name), strerror(errno));
						return NULL;
					}
					/* If we are looking by a keyname, and the database is missing, skip the entry. Otherwise,
					 * give a chance to find the key by hash.
					 */
					if (SEARCH_BY_KEYNAME == search_field1_type)
						search_fail = 1;
				} else
				{	/* Once the path has been resolved, save it to avoid future realpath()s.
					 * If this isn't an autodb, use the path as is. If it is an autodb ensure that the target
					 * path and the existing db match before storing it.
					 */
					if (!isautodb || (!strcmp(path_array, search_field1)))
					{
						if (isautodb) /* Save the resolved path minus EXT_NEW */
							strcpy(curr->key_name, name_search_field_buff);
						else
							strcpy(curr->key_name, path_array);
						curr->status = UNRES_KEY_RES_DB;
					} else
						continue;
				}
			}
			/* Do not proceed examining the current item if the file we are looking for is missing. */
			if (!search_fail)
			{	/* Next, see if the current item is a legitimate or illegitimate duplicate. */
				if (UNRES_KEY_UNRES_DB != curr->status)
				{	/* It is possible that a newly resolved realpath points to a previously seen database file,
					 * in which case we should first check whether that database has already been inserted into
					 * the tree with the same key to avoid inserting a duplicate. Alternatively, it may be a
					 * duplicate device keyname, which, unlike a database one, cannot be associated with
					 * multiple keys.
					 */
					name_length = strlen(curr->key_name);
					assert(name_length < YDB_PATH_MAX);
					if (database)
						node = keystore_lookup_by_keyname_plus(curr->key_name,
								curr->key_path, SEARCH_BY_KEYPATH);
					else
						node = keystore_lookup_by_keyname(curr->key_name);
					if (NULL != node)
					{
						if (!database && strcmp(node->key_path, curr->key_path))
						{
							*error = TRUE;
							UPDATE_ERROR_STRING("In config file " STR_ARG ", keyname in entry #%d "
								"corresponding to 'files' has already been seen but specifies "
								"a different key", ELLIPSIZE(gc_config_filename), curr->index);
							return NULL;
						} else
						{	/* This key is already found in our search trees, so simply remove it from
							 * the unresolved list.
							 */
							REMOVE_UNRESOLVED_LINK(curr, prev);
							continue;
						}
					}
				} else
				{	/* Name is unresolved; we better be searching by hash. */
					assert(SEARCH_BY_HASH == search_field1_type);
					name_length = -1;
				}
				/* If the key name and path search criteria yield a match, proceed to decrypt the key. */
				if (((NULL == name_search_field_ptr) || (!strcmp(curr->key_name, name_search_field_ptr)))
					&& ((NULL == path_search_field_ptr) || (!strcmp(curr->key_path, path_search_field_ptr))))
				{
					path_length = strlen(curr->key_path);
					node = gtmcrypt_decrypt_key(curr->key_path, path_length, curr->key_name, name_length);
					if (NULL == node)
					{
						*error = TRUE;
						return NULL;
					} else
					{	/* Remove the key from the unresolved list and see if matches by its hash. */
						REMOVE_UNRESOLVED_LINK(curr, prev);
						if ((NULL != search_field1) && (SEARCH_BY_HASH == search_field1_type)
								&& memcmp(node->key_hash, search_field1, GTMCRYPT_HASH_LEN))
							continue;
						return node;
					}
				}
			}
		}
		prev = curr;
		curr = curr->next;
	}
	return NULL;
}

/*
 * Helper function to decrypt a symmetric key, produce its hash, and store it for future discovery by key name, path, or hash.
 *
 * Arguments:	key_path	Path to the key.
 *		path_length	Length of the keypath.
 *		key_name	Name of the key.
 *		name_length	Length of the keyname.
 *
 * Returns:	Pointer to the key, if created; NULL otherwise.
 */
STATICFNDEF gtm_keystore_t *gtmcrypt_decrypt_key(char *key_path, int path_length, char *key_name, int name_length)
{
	gtm_keystore_t		*node;
	unsigned char		raw_key[SYMMETRIC_KEY_MAX];
	int			raw_key_length;
	int			gpgerr, gpg_attempt;

	/* If we have seen a key with the same path, do not re-read it. */
	if (NULL == (node = keystore_lookup_by_keypath(key_path)))
	{	/* Now that we have the name of the symmetric key file, try to decrypt it. If gc_pk_get_decrypted_key returns a
		 * non-zero status, it should have already populated the error string.
		 */
		gpg_attempt = 2;				/* Retry the libgpgme decryption request once */
		do {
			gpgerr = gc_pk_get_decrypted_key(key_path, raw_key, &raw_key_length);
			if (GPG_ERR_DECRYPT_FAILED == gpgerr)	/* Cipher is not valid, which cannot be the case. */
				gpg_attempt--;			/* Assume it's a gpg bug and retry */
			else
				gpg_attempt = 0;
		} while (gpg_attempt);
		if (0 != gpgerr)
			return NULL;
		if (0 == raw_key_length)
		{
			UPDATE_ERROR_STRING("Symmetric key " STR_ARG " found to be empty", ELLIPSIZE(key_path));
			return NULL;
		}
		/* We expect a symmetric key within a certain length. */
		assert(SYMMETRIC_KEY_MAX >= raw_key_length);
		GC_PK_COMPUTE_HASH(key_hash_array, raw_key);
		/* It is possible that while no key has been specified under this name, the same key has already been loaded
		 * for a different database or device, so look up the key by hash to avoid duplicates.
		 */
		if (NULL == (node = keystore_lookup_by_hash(key_hash_array)))
		{	/* Allocate a gtm_keystore_t element. */
			node = MALLOC(SIZEOF(gtm_keystore_t));
			node->cipher_head = NULL;
			node->db_cipher_entry = NULL;
			/* WARNING: Not doing a memset here because raw_key comes padded with NULLs from gc_pk_get_decrypted_key. */
			memcpy(node->key, raw_key, SYMMETRIC_KEY_MAX);
			/* This should take care of assigning key_hash to the node itself. */
			INSERT_KEY_LINK(keystore_by_hash_head, node, gtm_keystore_hash_link_t, link->key_hash, key_hash_array,
					GTMCRYPT_HASH_LEN, GTMCRYPT_HASH_LEN, TRUE, FALSE);
		}
		INSERT_KEY_LINK(keystore_by_keypath_head, node, gtm_keystore_keypath_link_t,
				link->key_path, key_path, path_length + 1, YDB_PATH_MAX, FALSE, FALSE);
	}
	if (-1 != name_length)
	{	/* Only inserting a keyname-based link if the keyname was passed. */
		INSERT_KEY_LINK(keystore_by_keyname_head, node, gtm_keystore_keyname_link_t,
				key_name, key_name, name_length + 1, YDB_PATH_MAX, FALSE, TRUE);
	}
	return node;
}

/*
 * Re-read the configuration file, if necessary, and store it in memory.
 *
 * Returns: 0 if succeeded re-reading the configuration file; -1 otherwise.
 */
STATICFNDEF int keystore_refresh(void)
{
	int		n_mappings, status, just_read, envvar_len;
	char		*config_env;
	struct stat	stat_info;
	boolean_t	is_ydb_env_match;
	static long	last_modified_s, last_modified_ns;

	just_read = FALSE;
	/* Check and update the value of ydb_passwd/gtm_passwd if it has changed since we last checked. This way, if the user
	 * had originally entered a wrong password, but later changed the value (possible in MUMPS using external call), we
	 * read the up-to-date value instead of issuing an error.
	 */
	if (0 != gc_update_passwd(YDBENVINDX_PASSWD, NULL_SUFFIX, &gtmcrypt_pwent, GTMCRYPT_DEFAULT_PASSWD_PROMPT,
					GTMCRYPT_OP_INTERACTIVE_MODE & gtmcrypt_init_flags))
		return -1;
	if (CONFIG_FILE_UNREAD)
	{	/* First, make sure we have a proper environment varible and a regular configuration file. */
		if (NULL != (config_env = ydb_getenv(YDBENVINDX_CRYPT_CONFIG, NULL_SUFFIX, &is_ydb_env_match)))
		{
			if (0 == (envvar_len = strlen(config_env))) /* inline assignment */
			{
				if (is_ydb_env_match)
				{
					UPDATE_ERROR_STRING(ENV_EMPTY_ERROR, ydbenvname[YDBENVINDX_CRYPT_CONFIG] + 1);
				} else
				{
					UPDATE_ERROR_STRING(ENV_EMPTY_ERROR, gtmenvname[YDBENVINDX_CRYPT_CONFIG] + 1);
				}
				return -1;
			}
			if (YDB_PATH_MAX <= envvar_len)
			{
				if (is_ydb_env_match)
				{
					UPDATE_ERROR_STRING(ENV_TOOLONG_ERROR, ydbenvname[YDBENVINDX_CRYPT_CONFIG] + 1,
														YDB_PATH_MAX);
				} else
				{
					UPDATE_ERROR_STRING(ENV_TOOLONG_ERROR, gtmenvname[YDBENVINDX_CRYPT_CONFIG] + 1,
														YDB_PATH_MAX);
				}
				return -1;
			}
			if (0 != stat(config_env, &stat_info))
			{
				UPDATE_ERROR_STRING("Cannot stat configuration file: " STR_ARG ". %s", ELLIPSIZE(config_env),
					strerror(errno));
				return -1;
			}
			if (!S_ISREG(stat_info.st_mode))
			{
				UPDATE_ERROR_STRING("Configuration file " STR_ARG " is not a regular file", ELLIPSIZE(config_env));
				return -1;
			}
		} else
		{
			UPDATE_ERROR_STRING(ENV_UNDEF_ERROR, "ydb_crypt_config/gtmcrypt_config");
			return -1;
		}
		/* The ydb_crypt_config variable is defined and accessible. Copy it to a global for future references. */
		SNPRINTF(gc_config_filename, SIZEOF(gc_config_filename), "%s", config_env);
		just_read = TRUE;
	}
	assert(!CONFIG_FILE_UNREAD);
	/* Stat the file if not done already, so that we can get the last modified date. */
	if ((!just_read) && (0 != stat(gc_config_filename, &stat_info)))
	{
		UPDATE_ERROR_STRING("Cannot stat configuration file " STR_ARG ". %s", ELLIPSIZE(gc_config_filename),
			strerror(errno));
		return -1;
	}
	/* If the config file has not been modified since the last time we checked, return right away. */
	if ((last_modified_s > (long)stat_info.st_mtime)
		|| ((last_modified_s == (long)stat_info.st_mtime)
			&& (last_modified_ns >= (long)stat_info.st_nmtime)))
		return 0;
	/* File has been modified, so re-read it. */
	if (!config_read_file(&gtmcrypt_cfg, gc_config_filename))
	{
		UPDATE_ERROR_STRING("Cannot read config file " STR_ARG ". At line# %d - %s", ELLIPSIZE(gc_config_filename),
					config_error_line(&gtmcrypt_cfg), config_error_text(&gtmcrypt_cfg))
		return -1;
	}
	/* Clear the entire unresolved keys list because it will be rebuilt. */
	gtm_keystore_cleanup_unres_key_list();
	n_keys = 0;
	if (-1 == (status = read_database_section(&gtmcrypt_cfg)))
		return -1;
	n_keys += status;
	if (-1 == (status = read_files_section(&gtmcrypt_cfg)))
		return -1;
	n_keys += status;
	last_modified_s = (long)stat_info.st_mtime;
	last_modified_ns = (long)stat_info.st_nmtime;
	if (0 == n_keys)
	{
		UPDATE_ERROR_STRING("Configuration file " STR_ARG " contains neither 'database.keys' section nor 'files' section, "
			"or both sections are empty.", ELLIPSIZE(gc_config_filename));
		return -1;
	}
	return 0;
}

/*
 * Read the 'files' section of the configuration file, storing any previously unseen key in the unresolved list.
 *
 * Arguments:	cfgp		Pointer to the configuration object as populated by libconfig.
 *
 * Returns:	0 if successfully processed the 'files' section; -1 otherwise.
 */
STATICFNDEF int read_files_section(config_t *cfgp)
{
	int			i, name_length, lcl_n_maps;
	config_setting_t	*setting, *elem;
	gtm_keystore_t		*node;
	char			*key_name, *key_path;

	if (NULL == (setting = config_lookup(cfgp, "files")))
		return 0;
	lcl_n_maps = config_setting_length(setting);
	for (i = 0; i < lcl_n_maps; i++)
	{
		elem = config_setting_get_elem(setting, i);
		assert(NULL != elem);
		if (CONFIG_TYPE_STRING != config_setting_type(elem))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", entry #%d corresponding to 'files' is not a string",
					ELLIPSIZE(gc_config_filename), i + 1);
			return -1;
		}
		if (NULL == (key_name = config_setting_name(elem)))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", entry #%d corresponding to 'files' does not have a "
					"key attribute", ELLIPSIZE(gc_config_filename), i + 1);
			return -1;
		}
		/* Length should be under YDB_PATH_MAX because that is the size of the array where the name of a key is stored. */
		name_length = strlen(key_name) + 1;
		if (YDB_PATH_MAX <= name_length)
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", 'files' entry #%d's field length exceeds %d",
					ELLIPSIZE(gc_config_filename), i + 1, YDB_PATH_MAX - 1);
			return -1;
		}
		if (NULL == (key_path = (char *)config_setting_get_string(elem)))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", cannot find the value corresponding to 'files.%s'",
						ELLIPSIZE(gc_config_filename), key_name);
			return -1;
		}
		/* Key path needs to be fully resolved before we can reliably use it, hence realpath-ing. */
		if (NULL == realpath(key_path, path_array))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", could not obtain the real path of 'files' "
				"entry #%d's key. %s", ELLIPSIZE(gc_config_filename), i + 1, strerror(errno));
			return -1;
		}
		/* Duplicate names with different keys are prohibited for files, though they are allowed for databases. */
		if (NULL != (node = keystore_lookup_by_keyname(key_name)))
		{
			if (strcmp(node->key_path, path_array))
			{
				UPDATE_ERROR_STRING("In config file " STR_ARG ", keyname in entry #%d corresponding to 'files' "
					"has already been seen but specifies a different key",
					ELLIPSIZE(gc_config_filename), i + 1);
				return -1;
			} else
				continue;
		}
		insert_unresolved_key_link(key_name, path_array, i + 1, UNRES_KEY_FILE);
	}
	return lcl_n_maps;
}

/*
 * Process the 'database' section of the configuration file, storing any previously unseen key in the unresolved list.
 *
 * Arguments:	cfgp	Pointer to the configuration object as populated by libconfig.
 *
 * Returns:	0 if successfully processed the 'database' section; -1 otherwise.
 */
STATICFNDEF int read_database_section(config_t *cfgp)
{
	int			i, name_length, lcl_n_maps;
	config_setting_t	*setting, *elem;
	gtm_keystore_t		*node;
	char			*key_name, *key_path;

	if (NULL == (setting = config_lookup(cfgp, "database.keys")))
		return 0;
	lcl_n_maps = config_setting_length(setting);
	/* The following code makes sure that having an empty last entry in the database section is not required and does not cause
	 * errors, as GTM-7948's original implementation would have it.
	 */
	if (lcl_n_maps > 1)
	{
		elem = config_setting_get_elem(setting, lcl_n_maps - 1);
		if (0 == config_setting_length(elem))
		{
			config_setting_remove_elem(setting, lcl_n_maps - 1);
			lcl_n_maps--;
		}
	}
	for (i = 0; i < lcl_n_maps; i++)
	{
		elem = config_setting_get_elem(setting, i);
		assert(NULL != elem);
		if (!config_setting_lookup_string(elem, "dat", (const char **)&key_name))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", entry #%d corresponding to "
				"'database.keys' does not have a 'dat' item", ELLIPSIZE(gc_config_filename), i + 1);
			return -1;
		}
		/* Length should be under YDB_PATH_MAX because that is the size of the array where the name of a key is stored. */
		name_length = strlen(key_name) + 1 + STR_LIT_LEN(EXT_NEW);
		if (YDB_PATH_MAX <= name_length)
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", in entry #%d corresponding to 'database.keys' "
					"file name exceeds %d", ELLIPSIZE(gc_config_filename), i + 1,
					(int)(YDB_PATH_MAX - STR_LIT_LEN(EXT_NEW) - 1));
			return -1;
		}
		if (!config_setting_lookup_string(elem, "key", (const char **)&key_path))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", entry #%d corresponding to "
				"'database.keys' does not have a 'key' item", ELLIPSIZE(gc_config_filename), i + 1);
			return -1;
		}
		/* Key path needs to be fully resolved before we can reliably use it, hence realpath-ing. */
		if (NULL == realpath(key_path, path_array))
		{
			UPDATE_ERROR_STRING("In config file " STR_ARG ", could not obtain the real path of 'database.keys' "
				"entry #%d's key. %s", ELLIPSIZE(gc_config_filename), i + 1, strerror(errno));
			return -1;
		}
		/* Duplicate names with different keys are allowed for databases, though they are prohibited for files. */
		if (NULL != (node = keystore_lookup_by_keyname_plus(key_name, path_array, SEARCH_BY_KEYPATH)))
			continue;
		insert_unresolved_key_link(key_name, path_array, i + 1, UNRES_KEY_UNRES_DB);
	}
	return lcl_n_maps;
}

/*
 * Create new encryption / decryption state object with the specified IV.
 *
 * Arguments:	entry	Pointer to the key structure to which the encryption / decryption state object will be assigned.
 * 		iv	Initialization vector to use.
 * 		length	Length of the initialization vector.
 * 		action	1 for encryption, 0 for decryption.
 *
 * Returns:	0 if successfully created a new encryption / decryption state object; -1 otherwise.
 */
int keystore_new_cipher_ctx(gtm_keystore_t *entry, char *iv, int length, int action)
{
	int			rv;
	crypt_key_t		handle = NULL;
	gtm_cipher_ctx_t	*ctx;
	unsigned char		iv_array[GTMCRYPT_IV_LEN];

	memset(iv_array, 0, GTMCRYPT_IV_LEN);
	memcpy(iv_array, iv, length);
	if (0 != (rv = gc_sym_create_cipher_handle(entry->key, iv_array, &handle, action, FALSE)))
		return rv;
	ctx = MALLOC(SIZEOF(gtm_cipher_ctx_t));
	ctx->store = entry;
	ctx->handle = handle;
	memcpy(ctx->iv, iv_array, GTMCRYPT_IV_LEN);
	if (NULL == entry->cipher_head)
	{
		ctx->next = ctx->prev = NULL;
	} else
	{
		ctx->next = entry->cipher_head;
		ctx->prev = NULL;
		entry->cipher_head->prev = ctx;
	}
	entry->cipher_head = ctx;
	return 0;
}

/*
 * Remove an encryption / decryption state object.
 *
 * Arguments:	ctx	Pointer to the encryption / decryption state object to remove.
 *
 * Returns:	0 if successfully removed the cipher context; -1 otherwise.
 */
int keystore_remove_cipher_ctx(gtm_cipher_ctx_t *ctx)
{
	gtm_cipher_ctx_t	*next, *prev;
	int			status;

	assert(NULL != ctx);
	status = 0;
	gc_sym_destroy_cipher_handle(ctx->handle);
	next = ctx->next;
	prev = ctx->prev;
	if (NULL != prev)
		prev->next = next;
	if (NULL != next)
		next->prev = prev;
	if (ctx->store->cipher_head == ctx)
		ctx->store->cipher_head = next;
	if (ctx->store->db_cipher_entry == ctx)
		ctx->store->db_cipher_entry = NULL;
	FREE(ctx);
	return status;
}

/* Insert a new gtm_keystore_unres_key_link_t element in the unresolved keys list. */
STATICFNDEF void insert_unresolved_key_link(char *keyname, char *keypath, int index, int status)
{
	gtm_keystore_unres_key_link_t *node;

	node = (gtm_keystore_unres_key_link_t *)MALLOC(SIZEOF(gtm_keystore_unres_key_link_t));
	memset(node->key_name, 0, YDB_PATH_MAX);
	SNPRINTF(node->key_name, SIZEOF(node->key_name), "%s", keyname);
	memset(node->key_path, 0, YDB_PATH_MAX);
	SNPRINTF(node->key_path, SIZEOF(node->key_path), "%s", keypath);
	node->next = keystore_by_unres_key_head;
	node->index = index;
	node->status = status;
	keystore_by_unres_key_head = node;
}

/*
 * Clean up all key and encryption / decryption state contexts.
 *
 * Returns:	0 if successfully cleaned up all encryption handle lists and trees; -1 otherwise.
 */
int gtm_keystore_cleanup_all(void)
{
	int status;

	status = 0;
	if (NULL != keystore_by_hash_head)
	{
		if (-1 == gtm_keystore_cleanup_hash_tree(keystore_by_hash_head))
			status = -1;
		keystore_by_hash_head = NULL;
	}
	if (NULL != keystore_by_keyname_head)
	{
		gtm_keystore_cleanup_keyname_tree(keystore_by_keyname_head);
		keystore_by_keyname_head = NULL;
	}
	if (NULL != keystore_by_keypath_head)
	{
		gtm_keystore_cleanup_keypath_tree(keystore_by_keypath_head);
		keystore_by_keypath_head = NULL;
	}
	gtm_keystore_cleanup_unres_key_list();
	return status;
}

/*
 * Clean up a particular key object and all its encryption / decryption state objects.
 *
 * Arguments:	node	Key object to clean.
 *
 * Returns:	0 if successfully cleaned up the keystore node; -1 otherwise.
 */
STATICFNDEF int gtm_keystore_cleanup_node(gtm_keystore_t *node)
{
	gtm_cipher_ctx_t	*curr, *temp;
	int			status;

	status = 0;
	curr = node->cipher_head;
	while (NULL != curr)
	{
		temp = curr->next;
		gc_sym_destroy_cipher_handle(curr->handle);
		FREE(curr);
		curr = temp;
	}
	memset(node->key, 0, SYMMETRIC_KEY_MAX);
	memset(node->key_hash, 0, GTMCRYPT_HASH_LEN);
	FREE(node);
	return status;
}

/*
 * Clean up (recursively) a binary search tree for looking up keys by their hashes.
 *
 * Arguments:	entry	Pointer to the node from which to descend for cleaning.
 *
 * Returns:	0 if successfully cleaned up the hash tree; -1 otherwise.
 */
STATICFNDEF int gtm_keystore_cleanup_hash_tree(gtm_keystore_hash_link_t *entry)
{
	gtm_keystore_hash_link_t	*curr;
	int				status;

	status = 0;
	while (TRUE)
	{
		if (NULL != entry->left)
			gtm_keystore_cleanup_hash_tree(entry->left);
		if (-1 == gtm_keystore_cleanup_node(entry->link))
			status = -1;
		curr = entry;
		if (NULL != entry->right)
			entry = entry->right;
		else
			break;
		FREE(curr);
	}
	return status;
}

/*
 * Clean up (recursively) a binary search tree for looking up keys by their paths.
 *
 * Arguments:	entry	Pointer to the node from which to descend for cleaning.
 *
 * Returns:	0 if successfully cleaned up the path tree; -1 otherwise.
 */
STATICFNDEF void gtm_keystore_cleanup_keypath_tree(gtm_keystore_keypath_link_t *entry)
{
	gtm_keystore_keypath_link_t *curr;

	while (TRUE)
	{
		if (NULL != entry->left)
			gtm_keystore_cleanup_keypath_tree(entry->left);
		curr = entry;
		if (NULL != entry->right)
			entry = entry->right;
		else
			break;
		FREE(curr);
	}
}

/*
 * Clean up (recursively) a binary search tree for looking up keys by their names.
 *
 * Arguments:	entry	Pointer to the node from which to descend for cleaning.
 */
STATICFNDEF void gtm_keystore_cleanup_keyname_tree(gtm_keystore_keyname_link_t *entry)
{
	gtm_keystore_keyname_link_t *curr;

	while (TRUE)
	{
		if (NULL != entry->left)
			gtm_keystore_cleanup_keyname_tree(entry->left);
		curr = entry;
		if (NULL != entry->right)
			entry = entry->right;
		else
			break;
		FREE(curr);
	}
}

/*
 * Clean up (linearly) an unresolved keys list.
 */
STATICFNDEF void gtm_keystore_cleanup_unres_key_list(void)
{
	gtm_keystore_unres_key_link_t *temp, *curr;

	curr = keystore_by_unres_key_head;
	while (NULL != curr)
	{
		temp = curr;
		curr = curr->next;
		FREE(temp);
	}
	keystore_by_unres_key_head = NULL;
}

#ifdef GTM_CRYPT_KEYS_LOG
/* Following are debugging functions for printing the state of the keystores. */

/*
 * Print the relevant fields of the passed-in node of one of the trees we use for looking up keys.
 *
 * Arguments:	node	Binary tree node to print.
 * 		type	Type of the node, depending on what tree it came from.
 * 		child1	Pointer to save the pointer to the node's left child in.
 * 		child2	Pointer to save the pointer to the node's right child in.
 */
STATICFNDEF void print_node(void *node, int type, void **child1, void **child2)
{
	gtm_keystore_keyname_link_t	*name_node;
	gtm_keystore_keypath_link_t	*path_node;
	gtm_keystore_hash_link_t	*hash_node;
	gtm_keystore_t			*keystore;
	gtm_cipher_ctx_t		*cipher;

	switch (type)
	{
		case 0:	/* Hash-based tree */
			hash_node = (gtm_keystore_hash_link_t *)node;
			*child1 = (void *)hash_node->left;
			*child2 = (void *)hash_node->right;
			keystore = hash_node->link;
			break;
		case 1: /* Path-based tree */
			path_node = (gtm_keystore_keypath_link_t *)node;
			*child1 = (void *)path_node->left;
			*child2 = (void *)path_node->right;
			keystore = path_node->link;
			break;
		case 2:	/* Name-based tree */
			name_node = (gtm_keystore_keyname_link_t *)node;
			*child1 = (void *)name_node->left;
			*child2 = (void *)name_node->right;
			keystore = name_node->link;
			fprintf(stderr, "Name: %s; ", name_node->key_name);
			break;
	}

	fprintf(stderr, "Path: %s; [", keystore->key_path);
	cipher = keystore->cipher_head;
	while (NULL != cipher)
	{
		if (cipher == keystore->db_cipher_entry)
			fprintf(stderr, "*");
		fprintf(stderr, "X");
		cipher = cipher->next;
	}
	fprintf(stderr, "]\n");
}

/*
 * Recursively print all the nodes of the passed-in tree.
 *
 * Arguments:	node	Root node of the current subtree to print.
 * 		left	Flag indicating whether the current root is a left child of its parent.
 * 		level	Level of the current node in the entire tree.
 * 		type	Type of the tree we are dealing with.
 */
STATICFNDEF void print_tree(void *node, int left, int level, int type)
{
	int 	i;
	void	*child1, *child2;

	fprintf(stderr, "  ");
	for (i = 0; i < level; i++)
	{
		if (i == level - 1)
		{
			fprintf(stderr, left ? "|" : "`");
			fprintf(stderr, "-");
		} else
			fprintf(stderr, "| ");
	}
	if (NULL == node)
		fprintf(stderr, "<null>\n");
	else
	{
		print_node(node, type, &child1, &child2);
		print_tree(child1, 1, level + 1, type);
		print_tree(child2, 0, level + 1, type);
	}
}

/*
 * Print all nodes of the unresolved keys list.
 */
STATICFNDEF void print_unres_list(void)
{
	gtm_keystore_unres_key_link_t *node;

	node = keystore_by_unres_key_head;
	while (NULL != node)
	{
		fprintf(stderr, "  Name: %s; Path: %s; Index: %d; Status: %d\n",
			node->key_name, node->key_path, node->index, node->status);
		node = node->next;
	}
}

/* Print out the contents of the unresolved keys list as well as all binary trees we use for key searches. */
STATICFNDEF void print_debug(void)
{
	fprintf(stderr, " Hash-based tree:\n");
	print_tree((void *)keystore_by_hash_head, 0, 0, 0);
	fprintf(stderr, "\n Keypath-based tree:\n");
	print_tree((void *)keystore_by_keypath_head, 0, 0, 1);
	fprintf(stderr, "\n Keyname-based tree:\n");
	print_tree((void *)keystore_by_keyname_head, 0, 0, 2);
	fprintf(stderr, "\n Unresolved keys list:\n");
	print_unres_list();
	fprintf(stderr, "\n");
	fflush(stderr); /* BYPASSOK */
}
#endif
