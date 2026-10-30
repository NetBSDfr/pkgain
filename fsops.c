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

#include <sys/statvfs.h>
#if _FILE_OFFSET_BITS == 32
#undef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64 /* needed for large filesystems on sunos */
#endif
#include "pkgain.h"
#include <dirent.h>

/* Variable options for the repositories file */
static const struct VarParam {
	const char *opt;
	char *(*func)(void);
} var[] = {
	{ "$arch", getosarch },
	{ "$osrelease", getosrelease },
	{ NULL, NULL }
};

uint64_t
fs_room(const char *dir)
{
	struct statvfs	fsbuf;

	if (statvfs(dir, &fsbuf) < 0)
		err(EXIT_FAILURE, "Can't statvfs() `%s'", dir);

	return fsbuf.f_bavail * fsbuf.f_frsize;
}

void
clean_cache(void)
{
	DIR		*dp;
	struct dirent	*ep;
	char		pkgpath[BUFSIZ + 1];

	if ((dp = opendir(pkgain_cache)) == NULL)
		err(EXIT_FAILURE, "couldn't open %s", pkgain_cache);

	while ((ep = readdir(dp)) != NULL)
		if (ep->d_name[0] != '.') {
			snprintf(pkgpath, sizeof(pkgpath), "%s/%s",
				pkgain_cache, ep->d_name);
			if (unlink(pkgpath) < 0)
				err(EXIT_FAILURE,
					"could not delete %s", pkgpath);
		}
	closedir(dp);
}

/*
 * Check whether a string starts with a supported repository URL scheme.
 */
static int
is_repo_url(const char *s)
{
	return strncmp(s, "ftp://", 6) == 0 ||
	    strncmp(s, "http://", 7) == 0 ||
	    strncmp(s, "https://", 8) == 0 ||
	    strncmp(s, "file://", 7) == 0;
}

/*
 * Parse repositories.conf into a NULL terminated array of RepoConf entries.
 * Each whitespace-separated token starting with a URL scheme defines a
 * repository, optionally preceded by an alias name, e.g.:
 *
 *	http://example.org/pub/pkgsrc/packages/All
 *	alias http://example.org/pub/pkgsrc/packages/All
 *
 * Lines starting with '#' are comments; other non-URL tokens are ignored,
 * so the original one-URL-per-line format (including several URLs on a
 * single line) continues to work unchanged.  Repositories without an alias
 * act as the default, and the first listed one may also be selected
 * explicitly as "default".  Macro expansion applies to the URL only.
 * Returns NULL if no repositories were found.
 */
RepoConf *
read_repos(void)
{
	FILE	*fp;
	RepoConf	*repos;
	size_t	capacity, count, i;
	char	buf[BUFSIZ], urlbuf[BUFSIZ], *save, *tok, *alias, *tmp, *b;
	int	dup;
	const struct VarParam	*vp;

	if ((fp = fopen(PKGAIN_CONF"/"REPOS_FILE, "r")) == NULL)
		return NULL;

	capacity = 2; /* 1 entry + NULL */
	repos = xcalloc(capacity, sizeof(RepoConf));
	count = 0;

	while (!feof(fp) && !ferror(fp)) {
		if (fgets(buf, BUFSIZ, fp) == NULL)
			continue;
		trimcr(buf);

		/* Lines starting with '#' are comments */
		if (buf[strspn(buf, " \t")] == '#')
			continue;

		alias = NULL;
		for (tok = strtok_r(buf, " \t", &save); tok != NULL;
		    tok = strtok_r(NULL, " \t", &save)) {
			if (!is_repo_url(tok)) {
				/* candidate alias for a following URL */
				alias = tok;
				continue;
			}

			/* Validate and claim the optional alias */
			if (alias != NULL) {
				dup = 0;
				if (strchr(alias, '/') != NULL ||
				    strchr(alias, '=') != NULL) {
					warnx("invalid repository alias"
					    " '%s', ignoring", alias);
					alias = NULL;
				} else {
					for (i = 0; i < count; i++) {
						if (repos[i].alias != NULL &&
						    strcmp(repos[i].alias,
						    alias) == 0) {
							dup = 1;
							break;
						}
					}
					if (dup) {
						warnx("duplicate repository"
						    " alias '%s', ignoring",
						    alias);
						alias = NULL;
					}
				}
			}

			/*
			 * Try to replace all the ocurrences of supported
			 * macros with the proper system values.
			 */
			strlcpy(urlbuf, tok, sizeof(urlbuf));
			for (vp = var; vp->func != NULL; vp++) {
				if (strstr(urlbuf, vp->opt) == NULL)
					continue;
				tmp = vp->func();
				if (tmp == NULL) {
					warn(MSG_TRANS_FAILED, vp->opt);
					continue;
				}
				if ((b = strreplace(urlbuf, vp->opt,
				    tmp)) != NULL) {
					strncpy(urlbuf, b,
					    sizeof(urlbuf) - 1);
					urlbuf[sizeof(urlbuf) - 1] = '\0';
					XFREE(b);
				} else
					warn(MSG_INVALID_REPOS, urlbuf);

				XFREE(tmp);
			}

			if (count + 2 > capacity) {
				repos = xrealloc(repos,
				    2 * capacity * sizeof(RepoConf));
				memset(&repos[capacity], 0,
				    capacity * sizeof(RepoConf));
				capacity *= 2;
			}

			repos[count].alias = alias != NULL ?
			    xstrdup(alias) : NULL;
			repos[count].url = xstrdup(urlbuf);
			count++;
			repos[count].alias = NULL;
			repos[count].url = NULL;

			alias = NULL;
		}
	}

	fclose(fp);

	if (count == 0) {
		XFREE(repos);
		return NULL;
	}

	return repos;
}

