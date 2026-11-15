/*-
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026 Abdelmonem Awad <eg2@live.com>
 *
 * One TACACS+ authentication through FreeBSD's own libtacplus (taclib(3)), for the TACACS+ server type
 * of System: Access: Servers (src/opnsense/mvc/app/library/OPNsense/Auth/TacacsPlus.php; plan 01,
 * package 3.10). OPNsense's PHP has no binding to the library and no FFI, so this small tool is built
 * at install with the appliance's own cc, as tools/sensors/xgs3300-sensors.c is.
 *
 * Everything is read from standard input, one item a line, so neither the shared secret nor the
 * password is ever on a command line that another process could list:
 *
 *     host            a name or an address
 *     port            0 for TACACS+'s own, 49
 *     secret          the key the server shares with this firewall; empty for none (not encrypted)
 *     timeout         seconds; 0 for 5
 *     method          pap, ascii or chap
 *     remote address  the address the user logs in from, for the server's own log; may be empty
 *     user
 *     password
 *
 * It prints one line - PASS, FAIL or ERROR, a space, and the server's message or the library's error -
 * and exits 0 for PASS, 1 for FAIL and 2 for anything else. ascii answers the server's prompts for the
 * user and the password, as a terminal login would; chap sends the response a PPP peer would, computed
 * here from the password and a random challenge (RFC 8907, 5.4.2.3).
 *
 * The server is given to the library as a one-line tacplus.conf(5), in a folder of its own that only
 * root can read, removed as soon as the library has read it: tac_add_server() of OPNsense 26.7's
 * FreeBSD 15.1 dereferences a null pointer inside the library (measured on the second XGS 3300,
 * 2026-11-15) while tac_config(), the way pam_tacplus(8) gives it its servers, does not. libtacplus
 * reaches a server by IPv4 only.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <taclib.h>
#include <unistd.h>

#define LINE	1024
#define STEPS	8	/* prompts answered in one ascii login before giving up */

/* the server as a one-line tacplus.conf: host:port "secret" timeout, the secret's " and \ escaped */
static int
config(struct tac_handle *h, const char *host, int port, const char *secret, int timeout)
{
	char dir[] = "/tmp/xgs-tacauth.XXXXXX", path[sizeof(dir) + 16];
	const char *p;
	FILE *f;
	int fd, rc;

	if (mkdtemp(dir) == NULL)
		return (-1);
	snprintf(path, sizeof(path), "%s/tacplus.conf", dir);
	rc = -1;
	if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600)) != -1) {
		if ((f = fdopen(fd, "w")) != NULL) {
			fprintf(f, "%s:%d \"", host, port > 0 ? port : 49);
			for (p = secret; *p != '\0'; p++) {
				if (*p == '"' || *p == '\\')
					fputc('\\', f);
				fputc(*p, f);
			}
			fprintf(f, "\" %d\n", timeout > 0 ? timeout : 5);
			if (fclose(f) == 0)
				rc = tac_config(h, path);
		} else
			close(fd);
	}
	unlink(path);
	rmdir(dir);
	return (rc);
}

static char *
field(char *buf)
{
	size_t n;

	if (fgets(buf, LINE, stdin) == NULL)
		return (NULL);
	n = strcspn(buf, "\r\n");
	buf[n] = '\0';
	return (buf);
}

static int
answer(const char *verdict, const char *msg, int code)
{
	printf("%s %s\n", verdict, msg != NULL && msg[0] != '\0' ? msg : "-");
	return (code);
}

int
main(void)
{
	char host[LINE], port[LINE], secret[LINE], timeout[LINE], method[LINE], rem[LINE];
	char user[LINE], pass[LINE], *msg;
	unsigned char chap[1 + 16 + 16];
	struct tac_handle *h;
	MD5_CTX md5;
	int type, status, step, rc;

	if (field(host) == NULL || field(port) == NULL || field(secret) == NULL ||
	    field(timeout) == NULL || field(method) == NULL || field(rem) == NULL ||
	    field(user) == NULL || field(pass) == NULL)
		return (answer("ERROR", "eight lines expected on standard input", 2));

	if (strcasecmp(method, "pap") == 0)
		type = TAC_AUTHEN_TYPE_PAP;
	else if (strcasecmp(method, "ascii") == 0)
		type = TAC_AUTHEN_TYPE_ASCII;
	else if (strcasecmp(method, "chap") == 0)
		type = TAC_AUTHEN_TYPE_CHAP;
	else
		return (answer("ERROR", "method must be pap, ascii or chap", 2));

	if (host[0] == '\0' || strpbrk(host, " \t\"#:") != NULL)
		return (answer("ERROR", "the server must be an IPv4 address or a host name", 2));
	if ((h = tac_open()) == NULL)
		return (answer("ERROR", "the library could not start", 2));
	rc = 2;
	if (config(h, host, atoi(port), secret, atoi(timeout)) == -1) {
		rc = answer("ERROR", tac_strerror(h)[0] != '\0' ? tac_strerror(h) :
		    "the server's settings could not be written for the library", 2);
		goto out;
	}
	if (tac_create_authen(h, TAC_AUTHEN_LOGIN, type, TAC_AUTHEN_SVC_LOGIN) == -1 ||
	    tac_set_user(h, user) == -1 || tac_set_port(h, "opnsense") == -1 ||
	    (rem[0] != '\0' && tac_set_rem_addr(h, rem) == -1)) {
		rc = answer("ERROR", tac_strerror(h), 2);
		goto out;
	}
	if (type == TAC_AUTHEN_TYPE_PAP) {
		if (tac_set_data(h, pass, strlen(pass)) == -1) {
			rc = answer("ERROR", tac_strerror(h), 2);
			goto out;
		}
	} else if (type == TAC_AUTHEN_TYPE_CHAP) {
		/* the PPP identifier and the challenge, then MD5 over identifier, password and challenge */
		arc4random_buf(chap, 1 + 16);
		MD5Init(&md5);
		MD5Update(&md5, chap, 1);
		MD5Update(&md5, (unsigned char *)pass, strlen(pass));
		MD5Update(&md5, chap + 1, 16);
		MD5Final(chap + 1 + 16, &md5);
		if (tac_set_data(h, chap, sizeof(chap)) == -1) {
			rc = answer("ERROR", tac_strerror(h), 2);
			goto out;
		}
	}

	for (step = 0; step < STEPS; step++) {
		if ((status = tac_send_authen(h)) == -1) {
			rc = answer("ERROR", tac_strerror(h), 2);
			goto out;
		}
		msg = tac_get_msg(h);
		switch (TAC_AUTHEN_STATUS(status)) {
		case TAC_AUTHEN_STATUS_PASS:
			rc = answer("PASS", msg, 0);
			goto out;
		case TAC_AUTHEN_STATUS_FAIL:
			rc = answer("FAIL", msg, 1);
			goto out;
		case TAC_AUTHEN_STATUS_GETUSER:
			if (tac_set_msg(h, user) == -1) {
				rc = answer("ERROR", tac_strerror(h), 2);
				goto out;
			}
			break;
		case TAC_AUTHEN_STATUS_GETPASS:
			if (tac_set_msg(h, pass) == -1) {
				rc = answer("ERROR", tac_strerror(h), 2);
				goto out;
			}
			break;
		case TAC_AUTHEN_STATUS_GETDATA:
			/* a question beyond the user and the password, which a login form cannot answer */
			rc = answer("FAIL", "the server asked for more than a user and a password", 1);
			goto out;
		default:
			rc = answer("ERROR", msg != NULL && msg[0] != '\0' ? msg : "the server answered with an error", 2);
			goto out;
		}
		free(msg);
	}
	rc = answer("FAIL", "too many prompts", 1);
out:
	explicit_bzero(pass, sizeof(pass));
	explicit_bzero(secret, sizeof(secret));
	explicit_bzero(chap, sizeof(chap));
	tac_close(h);
	return (rc);
}
