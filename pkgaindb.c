/*
 * Copyright (c) 2009-2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emile "iMil" Heitor <imil@NetBSD.org> .
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sqlite3.h>
#include "pkgain.h"

static sqlite3	*pdb;
static int              repo_counter = 0;
static uint64_t		savepoint_counter = 0;

static const char *pragmaopts[] = {
	"locking_mode = EXCLUSIVE",
	"empty_result_callbacks = 1",
	"synchronous = EXTRA",
	NULL
};

/*
 * Used to keep track of the current query so that it can be used in the
 * error log callback for diagnostics.
 *
 * Most database access goes through pkgaindb_doquery which will set curquery
 * to the query it has been passed, and then resets it to NULL afterwards.
 * Any callers that perform their own database access should use curquery to
 * store their query so that errors are logged correctly.
 */
static const char *curquery = NULL;

char *pkgain_dbdir;
char *pkgain_sqldb;
char *pkgain_cache;
char *pkgain_errlog;
char *pkgain_sqllog;

void
setup_pkgain_dbdir(void)
{
	char *p;

	if ((p = getenv("PKGAIN_DBDIR")) != NULL)
		pkgain_dbdir = xasprintf("%s", p);
	else
		pkgain_dbdir = xasprintf("%s", PKGAIN_DBDIR);

	pkgain_sqldb = xasprintf("%s/pkgain.db", pkgain_dbdir);
	pkgain_cache = xasprintf("%s/cache", pkgain_dbdir);
	pkgain_errlog = xasprintf("%s/pkg_install-err.log", pkgain_dbdir);
	pkgain_sqllog = xasprintf("%s/sql.log", pkgain_dbdir);

	if (access(pkgain_dbdir, F_OK) != 0) {
		if (mkdir(pkgain_dbdir, 0755) < 0)
			err(1, "Failed to create %s", pkgain_dbdir);
	}

	if (access(pkgain_cache, F_OK) != 0) {
		if (mkdir(pkgain_cache, 0755) < 0)
			err(1, "Failed to create %s", pkgain_cache);
	}
}

uint8_t
have_privs(int reqd)
{
	if ((reqd & PRIVS_PKGDB) &&
	    (access(pkgdb_get_dir(), F_OK) == 0) &&
	    (access(pkgdb_get_dir(), W_OK) < 0))
		return 0;

	if ((reqd & PRIVS_PKGAINDB) &&
	    (access(pkgain_dbdir, F_OK) == 0) &&
	    (access(pkgain_dbdir, W_OK) < 0))
		return 0;

	return 1;
}

const char *
pdb_version(void)
{
	return "SQLite "SQLITE_VERSION;
}

/*
 * unused: optional parameter given as 4th argument of sqlite3_exec
 * argc  : row number
 * argv  : row
 * col   : column
 *
 *    col[0]    col[1]          col[argc]
 *  ______________________________________
 * | argv[0] | argv[1] | ... | argv[argc] |
 *
 * WARNING: callback is called on every line
 */

/* sqlite callback, record a single value */
int
pdb_get_value(void *param, int argc, char **argv, char **colname)
{
	char *value = (char *)param;

	if (argv != NULL) {
		XSTRCPY(value, argv[0]);

		return PDB_OK;
	}

	return PDB_ERR;
}

/*
 * Record an error to the SQL log, optionally logging the query that caused
 * the failure.  Any errors opening the log for writing are ignored so that
 * regular users can issue queries.
 */
static void
pkgaindb_sqlerr_cb(void *arg, int errcode, const char *errmsg)
{
	FILE *fp;
	struct tm tm;
	time_t now;
	char curtime[64];
	char **query = (char **)arg;

	now = time(NULL);
	tm = *(localtime(&now));
	strftime(curtime, sizeof(curtime), "%Y-%m-%d %H:%M:%S %Z", &tm);

	if ((fp = fopen(pkgain_sqllog, "a")) != NULL) {
		fprintf(fp, "SQL error at %s\n", curtime);
		fprintf(fp, "   errmsg: %s\n", errmsg);
		if (*query)
			fprintf(fp, "    query: %s\n", *query);
		fclose(fp);
	}
}

int
pkgaindb_doquery(const char *query, int (*cb)(void *, int, char *[], char *[]),
    void *param)
{
	int rv;

	/*
	 * Save the current query for the error logging callback, then reset
	 * to NULL after the query has completed to avoid leaking into the
	 * error log of callers that use the sqlite3_exec interface directly.
	 */
	curquery = query;

	rv = sqlite3_exec(pdb, query, cb, param, NULL);

	curquery = NULL;

	return (rv == SQLITE_OK) ? PDB_OK : PDB_ERR;
}

int
pkgaindb_dovaquery(const char *fmt, ...)
{
	char *buf;
	va_list ap;
	int rv;

	va_start(ap, fmt);
	if ((buf = sqlite3_vmprintf(fmt, ap)) == NULL)
		errx(EXIT_FAILURE, "Insufficient memory to construct query");
	va_end(ap);

	rv = pkgaindb_doquery(buf, NULL, NULL);

	sqlite3_free(buf);

	return rv;
}

uint64_t
pkgaindb_savepoint(void)
{
	uint64_t savepoint = savepoint_counter++;

	if (pkgaindb_dovaquery("SAVEPOINT sp%"PRIu64, savepoint))
		errx(EXIT_FAILURE, "savepoint: %s", sqlite3_errmsg(pdb));

	return savepoint;
}

void
pkgaindb_savepoint_rollback(uint64_t savepoint)
{

	if (pkgaindb_dovaquery("ROLLBACK TO sp%"PRIu64, savepoint)) {
		errx(EXIT_FAILURE, "rollback savepoint %s",
		    sqlite3_errmsg(pdb));
	}
}

void
pkgaindb_savepoint_release(uint64_t savepoint)
{

	if (pkgaindb_dovaquery("RELEASE sp%"PRIu64, savepoint)) {
		errx(EXIT_FAILURE, "release savepoint %s",
		    sqlite3_errmsg(pdb));
	}
}

/*
 * Configure the pkgain database.  Returns 0 if opening an existing compatible
 * database, or 1 if the database needs to be created or recreated (in the case
 * of a schema upgrade).  Any other error is fatal.
 */
int
pkgaindb_open(void)
{
	int create, i, oflags;
	char buf[128];

	/*
	 * Configure our sqlite error log callback function.
	 */
	sqlite3_config(SQLITE_CONFIG_LOG, pkgaindb_sqlerr_cb, &curquery);

	/*
	 * Determine if we need to create a new database or can load an
	 * existing one.  The SQLITE_OPEN_READWRITE flag does not require that
	 * write privileges are available, if they are not then the database
	 * will be opened read-only, allowing normal users to perform queries.
	 */
recreate:
	create = 0;
	oflags = SQLITE_OPEN_READWRITE;
	if (access(pkgain_sqldb, F_OK) < 0) {
		create = 1;
		oflags |= SQLITE_OPEN_CREATE;
	}

	if (sqlite3_open_v2(pkgain_sqldb, &pdb, oflags, NULL) != SQLITE_OK)
		err(EXIT_FAILURE, "cannot open database");

	/*
	 * If we're creating or recreating a new database, attempt to populate
	 * the initial schema, otherwise perform a compatibility check.  If the
	 * compatibility check fails then we simply remove the existing database
	 * and recreate, there is no support for online upgrades at this time.
	 */
	if (create) {
		if (pkgaindb_doquery(CREATE_DRYDB, NULL, NULL) != PDB_OK)
			errx(EXIT_FAILURE, "cannot create database: %s",
			    sqlite3_errmsg(pdb));
	} else {
		if (pkgaindb_doquery(CHECK_DB_LATEST, NULL, NULL) != PDB_OK) {
			if (unlink(pkgain_sqldb) < 0)
				err(EXIT_FAILURE, "cannot recreate database");
			goto recreate;
		}
	}

	/* Apply PRAGMA properties */
	for (i = 0; pragmaopts[i] != NULL; i++) {
		snprintf(buf, sizeof(buf), "PRAGMA %s;", pragmaopts[i]);
		pkgaindb_doquery(buf, NULL, NULL);
	}

	return create;
}

void
pkgaindb_close(void)
{
	sqlite3_close(pdb);
}

__attribute__((noreturn))
static void
pkgaindb_sqlfail(void)
{
	pkgaindb_close();

	errx(EXIT_FAILURE, "SQL query failed, see %s", pkgain_sqllog);
}

/*
 * Prepare a statement for repeated execution with bound values, avoiding the
 * cost of parsing the same query for every row.  Errors are fatal.
 */
sqlite3_stmt *
pkgaindb_stmt_prepare(const char *query)
{
	sqlite3_stmt *stmt;

	curquery = query;
	if (sqlite3_prepare_v2(pdb, query, -1, &stmt, NULL) != SQLITE_OK)
		pkgaindb_sqlfail();
	curquery = NULL;

	return stmt;
}

/*
 * Execute a prepared statement with values already bound by the caller, and
 * reset it ready for the next execution.  As with pkgaindb_doquery(), errors
 * are returned to the caller and logged to the SQL log, they may be expected
 * (for example UNIQUE constraint failures for duplicate packages).
 */
int
pkgaindb_stmt_exec(sqlite3_stmt *stmt)
{
	int rv;

	curquery = sqlite3_sql(stmt);
	rv = sqlite3_step(stmt);
	(void) sqlite3_reset(stmt);
	curquery = NULL;

	return (rv == SQLITE_DONE) ? PDB_OK : PDB_ERR;
}

void
pkgaindb_stmt_finalize(sqlite3_stmt *stmt)
{
	(void) sqlite3_finalize(stmt);
}

int
pkg_db_mtime(struct stat *st)
{
	sqlite3_stmt	*stmt;
	time_t	   	db_mtime;
	long		db_ntime;
	int		rc;

	/* No pkgdb, just return up-to-date so we can start installing. */
	if (stat(pkgdb_get_dir(), st) < 0)
		return 0;

	/*
	 * Fetch only the most recent rowid.
	 */
	curquery = "SELECT PKGDB_MTIME, PKGDB_NTIME FROM PKGDB "
		   "ORDER BY ROWID DESC LIMIT 1;";

	if (sqlite3_prepare_v2(pdb, curquery, -1, &stmt, NULL) != SQLITE_OK)
		pkgaindb_sqlfail();

	rc = sqlite3_step(stmt);

	if (rc == SQLITE_ROW) {
		db_mtime = sqlite3_column_int64(stmt, 0);
		db_ntime = (long)sqlite3_column_int64(stmt, 1);
	} else if (rc == SQLITE_DONE) {
		db_mtime = 0;
		db_ntime = 0;
	} else {
		/* This shouldn't happen, abort and investigate */
		pkgaindb_sqlfail();
	}

	sqlite3_finalize(stmt);

	/* Databases matched pkgdb, we're up-to-date */
	if (db_mtime == st->st_mtime && db_ntime == st->pkgain_nanotime)
		return 0;

	/* We're not up to date, request a refresh. */
	return 1;
}

/*
 * Update database to current mtime after insertion.
 */
void
pkg_db_update_mtime(struct stat *st)
{
	sqlite3_stmt *stmt;

	curquery = "INSERT INTO PKGDB (PKGDB_MTIME, PKGDB_NTIME) "
		   "VALUES (?, ?);";

	if (sqlite3_prepare_v2(pdb, curquery, -1, &stmt, NULL) != SQLITE_OK)
		pkgaindb_sqlfail();

	if (sqlite3_bind_int64(stmt, 1, st->st_mtime) != SQLITE_OK)
		pkgaindb_sqlfail();

	if (sqlite3_bind_int64(stmt, 2, st->pkgain_nanotime) != SQLITE_OK)
		pkgaindb_sqlfail();

	if (sqlite3_step(stmt) != SQLITE_DONE)
		pkgaindb_sqlfail();

	sqlite3_finalize(stmt);
}

void
repo_record(RepoConf *repos)
{
	int	i;
	char	query[BUFSIZ], value[20];

	for (i = 0; repos[i].url != NULL; i++) {
		sqlite3_snprintf(BUFSIZ, query, EXISTS_REPO, repos[i].url);
		pkgaindb_doquery(query, pdb_get_value, &value[0]);
                repo_counter++;

		if (value[0] == '0') {
			/* repository does not exists */
			if (repos[i].alias != NULL) {
				/* release alias from any other URL */
				sqlite3_snprintf(BUFSIZ, query,
				    RELEASE_REPO_ALIAS, repos[i].alias);
				pkgaindb_doquery(query, NULL, NULL);
			}
			sqlite3_snprintf(BUFSIZ, query, INSERT_REPO,
			    repos[i].url, repos[i].alias);
			pkgaindb_doquery(query, NULL, NULL);
		} else if (repos[i].alias != NULL) {
			/*
			 * Release the alias from any other URL before
			 * claiming it, so aliases can be freely reassigned
			 * between repositories.
			 */
			sqlite3_snprintf(BUFSIZ, query, RELEASE_REPO_ALIAS,
			    repos[i].alias);
			pkgaindb_doquery(query, NULL, NULL);
			sqlite3_snprintf(BUFSIZ, query, UPDATE_REPO_ALIAS,
			    repos[i].alias, repos[i].url);
			pkgaindb_doquery(query, NULL, NULL);
		} else {
			/* clear any stale alias for un-named repositories */
			sqlite3_snprintf(BUFSIZ, query, UPDATE_REPO_ALIAS,
			    NULL, repos[i].url);
			pkgaindb_doquery(query, NULL, NULL);
		}
	}
}

time_t
pkg_sum_mtime(char *repo)
{
	time_t	db_mtime = 0;
	char	str_mtime[20], query[BUFSIZ];

	str_mtime[0] = '\0';

	sqlite3_snprintf(BUFSIZ, query,
	    "SELECT REPO_MTIME FROM REPOS WHERE REPO_URL GLOB %Q || '*';",
	    repo);
	pkgaindb_doquery(query, pdb_get_value, str_mtime);

	if (str_mtime[0] != '\0')
		db_mtime = (time_t)strtol(str_mtime, (char **)NULL, 10);

	return db_mtime;
}

void
pkgaindb_stats(void)
{
	sqlite3_stmt	*stmt;
	int		lcount, rcount;
	char	lsize[H_BUF], rsize[H_BUF];

	/*
	 * The remote totals consider only the copy from the first-listed
	 * repository, matching the package actually offered to the user.
	 */
	curquery = "SELECT "
		   " (SELECT COUNT(PKG_ID) FROM LOCAL_PKG) AS lcount, "
		   " (SELECT SUM(SIZE_PKG) FROM LOCAL_PKG) AS lsize, "
		   " (SELECT COUNT(DISTINCT FULLPKGNAME) FROM REMOTE_PKG) AS rcount, "
		   " (SELECT SUM(FILE_SIZE) FROM REMOTE_PKG WHERE PKG_ID IN "
		   "    (SELECT MIN(PKG_ID) FROM REMOTE_PKG "
		   "      GROUP BY FULLPKGNAME)) AS rsize;";

	if (sqlite3_prepare_v2(pdb, curquery, -1, &stmt, NULL) != SQLITE_OK)
		pkgaindb_sqlfail();

	/* Anything other than a single row is an error */
	if (sqlite3_step(stmt) != SQLITE_ROW)
		pkgaindb_sqlfail();

	/*
	 * Counts are simple integers.  Sizes are converted to human readable
	 * format.
	 */
	lcount = sqlite3_column_int(stmt, 0);
	humanize_size(lsize, sqlite3_column_int64(stmt, 1));
	rcount = sqlite3_column_int(stmt, 2);
	humanize_size(rsize, sqlite3_column_int64(stmt, 3));

	sqlite3_finalize(stmt);

	printf("Local package database:\n"
	       "\tInstalled packages: %d\n"
	       "\tDisk space occupied: %s\n\n"
	       "Remote package database(s):\n"
	       "\tNumber of repositories: %d\n"
	       "\tPackages available: %d\n"
	       "\tTotal size of packages: %s\n",
	       lcount, lsize, repo_counter, rcount, rsize);
}
