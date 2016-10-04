/*	urlyconnect 0.1

	Copyright 2010 Thomas Karpiniec <tk@1.21jiggawatts.net>

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

/* Number of simultaneous connections to attempt */
#define N 8
/* Maximum amount of data to receive from each connection */
#define BUFSIZE 8192

int pwritefds[N], preadfds[N], creadfds[N], cwritefds[N];
pid_t children[N];
char recv[N][BUFSIZE];
int recvfree[N];

void
send_to_children (char *text)
{
	int i;
	for (i = 0; i < N; ++i)
	{
		write (pwritefds[i], text, strlen (text));
	}
}

/* Concatenate to each child's receive buffer until roughly endtime */
void
select_loop (long endtime)
{
	fd_set rfds;
	struct timeval timeout;
	int maxfd = 0;
	struct timeval now;
	int i;
	int readsize;
	gettimeofday (&now, NULL);
	
	do {
		timeout.tv_sec = endtime - now.tv_sec;
		timeout.tv_usec = 0;

		FD_ZERO (&rfds);

		for (i = 0; i < N; ++i)
		{
			FD_SET (preadfds[i], &rfds);
			if (preadfds[i] > maxfd)
				maxfd = preadfds[i];
		}
		
		if ((i = select (maxfd+1, &rfds, NULL, NULL, &timeout)) > 0)
		{
			for (i = 0; i < N; ++i)
			{
				if (FD_ISSET (preadfds[i], &rfds))
				{
					char buf[BUFSIZE];
					memset (buf, 0, BUFSIZE);
					readsize = read (preadfds[i], buf, BUFSIZE);
					strncat (recv[i], buf,
						(readsize < recvfree[i] ? readsize : recvfree[i]));
				}
			}
		}
		gettimeofday (&now, NULL);
	} while (now.tv_sec < endtime);
}

void
usage_fail (char *argv0)
{
	printf ("Usage:\n"
		"%s user@host [ssh-port [tunnel-port]]\n"
		"\tssh-port: port for ssh server\n"
		"\ttunnel-port: passed as '-D port' to ssh\n\n", argv0);
	exit (EXIT_FAILURE);
}

int
main (int argc, char *argv[])
{
	int writefds[2], readfds[2];
	struct timeval now;
	pid_t pid;
	int i, winner, nwinners;
	int ssh_port = 22;
	int tunnel_port = 0;
	int sentinel;
	char hostname[512];
	char cssh_port[512];
	char sktname[512];
	char command[512];
	
	if (argc < 2 || argc > 4)
		usage_fail (argv[0]);
	
	if (argc > 2)
	{
		if (!sscanf (argv[2], "%d", &ssh_port))
			usage_fail (argv[0]);
		if (argc > 3)
		{
			if (!sscanf (argv[3], "%d", &tunnel_port))
				usage_fail (argv[0]);
		}
	}
	
	strncpy (hostname, argv[1], 512);
	
	sprintf (cssh_port, "%d", ssh_port);
	
	/* Prepare pipes to communicate with children */
	for (i = 0; i < N; ++i)
	{
		if (pipe (readfds) || pipe (writefds))
		{
			fprintf (stderr, "Pipe failed.\n");
			return EXIT_FAILURE;
		}
		preadfds[i] = readfds[0];
		cwritefds[i] = readfds[1];
		pwritefds[i] = writefds[1];
		creadfds[i] = writefds[0];
	}
	
	gettimeofday(&now, NULL);
	sentinel = now.tv_sec;
	
	/* Launch all connection attempts */
	for (i = 0; i < N; ++i)
	{
		memset (recv[i], 0, BUFSIZE);
		pid = fork ();
		if (pid == 0) /* child */
		{
			sprintf (sktname, "/tmp/%d-ssh_sock%d", sentinel, i);
			
			printf ("Connection #%d launched!\n", i);
			dup2 (creadfds[i], STDIN_FILENO);
			dup2 (cwritefds[i], STDOUT_FILENO);
			close (STDERR_FILENO);
			
			execlp ("ssh", "ssh", hostname, "-t", "-t",
				"-p", cssh_port, "-M", "-S", sktname, NULL);
			perror ("execlp");
			_exit (EXIT_FAILURE);
		}
		else if (pid < 0)
		{
			fprintf (stderr, "Fork failed.\n");
			return EXIT_FAILURE;
		}
		else /* parent */
		{
			children[i] = pid;
			sleep (1);
		}
	}

	/* Set up receive buffer window */
	for (i = 0; i < N; ++i)
	{
		recvfree[i] = BUFSIZE;
	}
	
	sleep (10);
	
	printf ("First echo...\n");
	send_to_children ("echo SOLIDCONNECTION1\n");
	gettimeofday (&now, NULL);
	select_loop (now.tv_sec + 10);	/* Wait 10 seconds */
	
	printf("Second echo...\n");
	send_to_children ("echo SOLIDCONNECTION2\n");
	select_loop (now.tv_sec + 20);	/* Wait another 10 seconds */
	
	printf("Third echo...\n");
	send_to_children ("echo SOLIDCONNECTION3\n");
	select_loop (now.tv_sec + 23);	/* Wait 3 seconds */
	
	/* Check the buffers for two instances of each string */
	winner = -1;
	nwinners = 0;
	for (i = 0; i < N; ++i)
	{
		char *p = recv[i];
		/*printf("Received from %d:\n%s\n", i, p);*/
		if (!strstr (recv[i], "\nSOLIDCONNECTION1"))
			continue;
		p = recv[i];
		if (!strstr (recv[i], "\nSOLIDCONNECTION2"))
			continue;
		p = recv[i];
		if (!strstr (recv[i], "\nSOLIDCONNECTION3"))
			continue;
		if (winner == -1)
			winner = i;
		nwinners++;
		printf ("Found a solid connection: #%d\n", i);
	}
	printf("Total: %d/%d of connections solid.\n", nwinners, N);
	
	if (winner < 0)
	{
		printf ("All attempts failed! Terminating...\n");
		for (i = 0; i < N; ++i)
		{
			int status;
			kill (children[i], SIGTERM);
			pid = waitpid (children[i], &status, 0);
		}
		exit (EXIT_FAILURE);
	}
	
	for (i = 0; i < N; ++i)
	{
		if (i != winner)
		{
			int status;
			close (pwritefds[i]);
			close (preadfds[i]);
			/* Unfortunately we don't get SSH escape characters without
			 a pseudo-TTY. Oh well, kill them good. */
			kill (children[i], SIGTERM);
			/* Reap zombies before moving on */
			pid = waitpid (children[i], &status, 0);
		}
	}
	
	if (tunnel_port > 0)
	{
		sprintf (command, "ssh %s -p %d -D %d -S /tmp/%d-ssh_sock%d",
			hostname, ssh_port, tunnel_port, sentinel, winner);
	}
	else
	{
		sprintf (command, "ssh %s -p %d -S /tmp/%d-ssh_sock%d",
			hostname, ssh_port, sentinel, winner);
	}
	
	printf ("Launching ssh with socket from connection #%d...\n", winner);
	
	/* Launch the front-end to go with the winner */
	system (command);
	
	kill (children[winner], SIGKILL);
	pid = waitpid (children[winner], &i, 0);
	
	exit (EXIT_SUCCESS);
}

