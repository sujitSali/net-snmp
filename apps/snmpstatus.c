/*
 * snmpstatus.c - send snmp GET requests to a network entity.
 *
 */
/***********************************************************************
	Copyright 1988, 1989, 1991, 1992 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.  

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/
#include <config.h>

#ifdef STDC_HEADERS
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef ultrix4
extern char *optarg;
extern int optind;
#endif
#else
#include <strings.h>
extern char getopt();
extern char *optarg;
extern int optind;
extern void *malloc();
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <stdio.h>
#include <ctype.h>
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#include <netdb.h>
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#include "asn1.h"
#include "snmp_impl.h"
#include "snmp_api.h"
#include "snmp_client.h"
#include "party.h"
#include "context.h"
#include "view.h"
#include "acl.h"
#include "mib.h"
#include "snmp.h"
#include "snmp_parse_args.h"

extern int  errno;

oid	objid_sysDescr[] = {1, 3, 6, 1, 2, 1, 1, 1, 0};
int	length_sysDescr = sizeof(objid_sysDescr)/sizeof(oid);
oid	objid_sysUpTime[] = {1, 3, 6, 1, 2, 1, 1, 3, 0};
int	length_sysUpTime = sizeof(objid_sysUpTime)/sizeof(oid);
oid	objid_ifOperStatus[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 8};
int	length_ifOperStatus = sizeof(objid_ifOperStatus)/sizeof(oid);
oid	objid_ifInUCastPkts[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 11};
int	length_ifInUCastPkts = sizeof(objid_ifInUCastPkts)/sizeof(oid);
oid	objid_ifInNUCastPkts[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 12};
int	length_ifInNUCastPkts = sizeof(objid_ifInNUCastPkts)/sizeof(oid);
oid	objid_ifOutUCastPkts[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 17};
int	length_ifOutUCastPkts = sizeof(objid_ifOutUCastPkts)/sizeof(oid);
oid	objid_ifOutNUCastPkts[] = {1, 3, 6, 1, 2, 1, 2, 2, 1, 18};
int	length_ifOutNUCastPkts = sizeof(objid_ifOutNUCastPkts)/sizeof(oid);
oid	objid_ipInReceives[] = {1, 3, 6, 1, 2, 1, 4, 3, 0};
int	length_ipInReceives = sizeof(objid_ipInReceives)/sizeof(oid);
oid	objid_ipOutRequests[] = {1, 3, 6, 1, 2, 1, 4, 10, 0};
int	length_ipOutRequests = sizeof(objid_ipOutRequests)/sizeof(oid);

int main __P((int, char **));
char *uptime_string __P((u_long, char *));
void snmp_v1_setup __P((struct snmp_session*, char *, char *));
void snmp_v2_setup __P((struct snmp_session*, char *, char *, char *, char *, u_long, u_long));

void
usage __P((void))
{
  fprintf(stderr,"Usage:\n  snmpstatus ");
  snmp_parse_args_usage(stderr);
  fprintf(stderr,"\n\n");
  snmp_parse_args_descriptions(stderr);
}

char *uptime_string(timeticks, buf)
    register u_long timeticks;
    char *buf;
{
    int	seconds, minutes, hours, days;

    timeticks /= 100;
    days = timeticks / (60 * 60 * 24);
    timeticks %= (60 * 60 * 24);

    hours = timeticks / (60 * 60);
    timeticks %= (60 * 60);

    minutes = timeticks / 60;
    seconds = timeticks % 60;

    if (days == 0){
	sprintf(buf, "%d:%02d:%02d", hours, minutes, seconds);
    } else if (days == 1) {
	sprintf(buf, "%d day, %d:%02d:%02d", days, hours, minutes, seconds);
    } else {
	sprintf(buf, "%d days, %d:%02d:%02d", days, hours, minutes, seconds);
    }
    return buf;
}

int
main(argc, argv)
    int	    argc;
    char    *argv[];
{
    struct snmp_session session, *ss;
    struct snmp_pdu *pdu, *response;
    struct variable_list *vars;
    char name[MAX_NAME_LEN];
    char *sysdescr = NULL;
    u_long uptime = 0;
    int status;
    int ipin = 0, ipout = 0, ipackets = 0, opackets = 0;
    int good_var;
    int down_interfaces = 0;
    char buf[40];
    int index, count;

    init_mib();
    snmp_parse_args(argc, argv, &session);
    snmp_synch_setup(&session);
    ss = snmp_open(&session);
    if (ss == NULL){
        snmp_perror("snmpstatus: Couldn't open snmp");
	exit(1);
    }

    strcpy(name, "No System Description Available");
    pdu = snmp_pdu_create(GET_REQ_MSG);

    snmp_add_null_var(pdu, objid_sysDescr, length_sysDescr);
    snmp_add_null_var(pdu, objid_sysUpTime, length_sysUpTime);
    snmp_add_null_var(pdu, objid_ipInReceives, length_ipInReceives);
    snmp_add_null_var(pdu, objid_ipOutRequests, length_ipOutRequests);

retry:
    status = snmp_synch_response(ss, pdu, &response);
    if (status == STAT_SUCCESS){
	if (response->errstat == SNMP_ERR_NOERROR){
	    for(vars = response->variables; vars; vars = vars->next_variable){
		if (vars->name_length == length_sysDescr &&
		    !memcmp(objid_sysDescr, vars->name, sizeof(objid_sysDescr))){
			sysdescr = malloc(vars->val_len+1);
			memcpy(sysdescr, vars->val.string, vars->val_len);
			sysdescr[vars->val_len] = '\0';
		}
		if (vars->name_length == length_sysUpTime &&
		    !memcmp(objid_sysUpTime, vars->name, sizeof(objid_sysUpTime))){
			uptime = *vars->val.integer;
		}
		if (vars->name_length == length_ipInReceives &&
		    !memcmp(objid_ipInReceives, vars->name, sizeof(objid_ipInReceives))){
			ipin = *vars->val.integer;
		}
		if (vars->name_length == length_ipOutRequests &&
		    !memcmp(objid_ipOutRequests, vars->name, sizeof(objid_ipOutRequests))){
			ipout = *vars->val.integer;
		}
	    }
	} else {
	    printf("Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
	    if (response->errstat == SNMP_ERR_NOSUCHNAME){
		printf("This name doesn't exist: ");
		for(count = 1, vars = response->variables; vars && count != response->errindex;
		    vars = vars->next_variable, count++)
			;
		if (vars)
		    print_objid(vars->name, vars->name_length);
		printf("\n");
	    }
	    if ((pdu = snmp_fix_pdu(response, GET_REQ_MSG)) != NULL)
		goto retry;
	}

    } else if (status == STAT_TIMEOUT){
	fprintf(stderr, "No Response from %s\n", session.peername);
	exit(1);
    } else {    /* status == STAT_ERROR */
        snmp_perror("snmpstatus: An error occurred");
	exit(2);
    }

    printf("[%s]=>[%s] Up: %s\n", inet_ntoa(response->address.sin_addr),
	sysdescr, uptime_string(uptime, buf));

    if (response)
	snmp_free_pdu(response);

    pdu = snmp_pdu_create(GETNEXT_REQ_MSG);

    snmp_add_null_var(pdu, objid_ifOperStatus, length_ifOperStatus);
    snmp_add_null_var(pdu, objid_ifInUCastPkts, length_ifInUCastPkts);
    snmp_add_null_var(pdu, objid_ifInNUCastPkts, length_ifInNUCastPkts);
    snmp_add_null_var(pdu, objid_ifOutUCastPkts, length_ifOutUCastPkts);
    snmp_add_null_var(pdu, objid_ifOutNUCastPkts, length_ifOutNUCastPkts);

    good_var = 5;
    while(good_var == 5){
	good_var = 0;
	status = snmp_synch_response(ss, pdu, &response);
	if (status == STAT_SUCCESS){
	    if (response->errstat == SNMP_ERR_NOERROR){
		pdu = snmp_pdu_create(GETNEXT_REQ_MSG);

		index = 0;
		for(vars = response->variables; vars; vars = vars->next_variable){
		    if (vars->name_length >= length_ifOperStatus &&
			!memcmp(objid_ifOperStatus, vars->name, sizeof(objid_ifOperStatus))){
			    if (*vars->val.integer != MIB_IFSTATUS_UP)
				down_interfaces++;
			    snmp_add_null_var(pdu, vars->name, vars->name_length);
			    good_var++;
		    } else if (vars->name_length >= length_ifInUCastPkts &&
			!memcmp(objid_ifInUCastPkts, vars->name, sizeof(objid_ifInUCastPkts))){
			    ipackets += *vars->val.integer;
			    snmp_add_null_var(pdu, vars->name, vars->name_length);
			    good_var++;
		    } else if (vars->name_length >= length_ifInNUCastPkts &&
			!memcmp(objid_ifInNUCastPkts, vars->name,
				sizeof(objid_ifInNUCastPkts))){
			    ipackets += *vars->val.integer;
			    snmp_add_null_var(pdu, vars->name, vars->name_length);
			    good_var++;
		    } else if (vars->name_length >= length_ifOutUCastPkts &&
			!memcmp(objid_ifOutUCastPkts, vars->name,
				sizeof(objid_ifOutUCastPkts))){
			    opackets += *vars->val.integer;
			    snmp_add_null_var(pdu, vars->name, vars->name_length);
			    good_var++;
		    } else if (vars->name_length >= length_ifOutNUCastPkts &&
			!memcmp(objid_ifOutNUCastPkts, vars->name,
				sizeof(objid_ifOutNUCastPkts))){
			    opackets += *vars->val.integer;
			    snmp_add_null_var(pdu, vars->name, vars->name_length);
			    good_var++;
		    }
		    index++;
		}
	    } else {
		printf("Error in packet.\nReason: %s\n", snmp_errstring(response->errstat));
		if (response->errstat == SNMP_ERR_NOSUCHNAME){
		    printf("This name doesn't exist: ");
		    for(count = 1, vars = response->variables; vars && count != response->errindex;
			vars = vars->next_variable, count++)
			    ;
		    if (vars)
			print_objid(vars->name, vars->name_length);
		    printf("\n");
		}
	    }

	} else if (status == STAT_TIMEOUT){
	    fprintf(stderr,"No Response from %s\n", session.peername);
	} else {    /* status == STAT_ERROR */
	    fprintf(stderr,"An error occurred, Quitting\n");
	}

	if (response)
	    snmp_free_pdu(response);
    }
    printf("Recv/Trans packets: Interfaces: %d/%d | IP: %d/%d\n", ipackets, opackets, ipin, ipout);
    if (down_interfaces > 0){
	printf("%d interface%s down!\n", down_interfaces, down_interfaces > 1 ? "s are": " is" );
    }
    exit (0);
}
