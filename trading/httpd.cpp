#include <winsock2.h>
#include "../queue/queue.h"
#include "algo.h"

#pragma comment(lib,"ws2_32.lib")
#pragma warning(disable:4996)

#define MAX_BYTES_RECV 1024
#define MAX_CLT_NUM 30

volatile bool _stop_monitor_thread = false;
u_short srv_port = 8888;
const char *resp = "\
HTTP/1.1 200 OK\r\n\
Content-Length: %d\r\n\
Keep-Alive:timeout-15\r\n\
Connection:Keep-Alive\r\n\
Access-Control-Allow-Origin:*\r\n\
Content-Type:%s\r\n\
\r\n\
%s";

extern char _perf_stat_json[];

DWORD WINAPI httpd_thread(LPVOID par)
{
	WSADATA wsa;
	SOCKET srv_sock, new_clt_sock, clt_socks[MAX_CLT_NUM], clt_sock;
	struct sockaddr_in srv_addr, clt_addr;
	fd_set read_fds;
	char *req_buffer;
	req_buffer = (char*)malloc((MAX_BYTES_RECV + 1) * sizeof(char));
	for (int i = 0; i < MAX_CLT_NUM; i++) {
		clt_socks[i] = 0;
	}
	if (0 != WSAStartup(MAKEWORD(2, 3), (LPWSADATA)&wsa))
		panic((char*)"WSAStartup failed");
	srv_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (INVALID_SOCKET == srv_sock)
		panic((char*)"failed to create socket");
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.S_un.S_addr = INADDR_ANY;
	srv_addr.sin_port = htons(srv_port);
	if (SOCKET_ERROR == bind(srv_sock, (const struct sockaddr *)&srv_addr, (int)(sizeof(srv_addr)))) {
		printf("Bind failed with error code: %d\n", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
	listen(srv_sock, 3);
	int addr_len = sizeof(struct sockaddr_in);
	timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	while (TRUE) {
		// clear the socket fd set
		FD_ZERO(&read_fds);
		// add server socket to fd set 
		FD_SET(srv_sock, &read_fds);
		// add child sockets to fd set
		for (int i = 0; i < MAX_CLT_NUM; i++) {
			clt_sock = clt_socks[i];
			if (clt_sock > 0) {
				FD_SET(clt_sock, &read_fds);
			}
		}
		// wait for an activity on any of the sockets; returns 0 on timeout,
		// error code or number of sockets that received data
		int num_socks_activ = select(0, &read_fds, NULL, NULL, &timeout);
//		printf("select returned: %d\n", num_socks_activ);
		if (0 == num_socks_activ) {
			if (_stop_monitor_thread)
				break;
			else
				continue;
		}
		if (SOCKET_ERROR == num_socks_activ) {
			int err = WSAGetLastError();
			//panic((char*)"select failed");
			continue;
		}
		// if something happened on the server socket, then its an incoming connection 
		if (FD_ISSET(srv_sock, &read_fds)) {
			if ((new_clt_sock = accept(srv_sock, (struct sockaddr FAR *)&clt_addr, &addr_len)) < 0)
				panic((char*)"error accepting new connection");
//			printf("new client connected; socket: %d\n", (int)new_clt_sock);
			// add new client socket to array of sockets
			bool found_free_spot = false;
			for (int i = 0; i < MAX_CLT_NUM; i++) {
				if (0 == clt_socks[i]) {
					clt_socks[i] = new_clt_sock;
					found_free_spot = true;
					break;
				}
			}
			if (!found_free_spot) {
				shutdown(clt_sock, SD_SEND);
				closesocket(clt_sock);
				printf("not enough room for new connection\n");
			}
		}
		// else it is some IO operation on a client socket
		for (int i = 0; i < MAX_CLT_NUM; i++) {
			clt_sock = clt_socks[i];
			if (!FD_ISSET(clt_sock, &read_fds))
				continue;
			// get details of the client
//			printf("activity on the socket: %d\n", (int)clt_sock);
			getpeername(clt_sock, (struct sockaddr*)&clt_addr, &addr_len);
			// check if it was for closing, and also read the incoming message
			// NOTE that recv does not place a null terminator at the end of the string
			int bytes_read = recv(clt_sock, req_buffer, MAX_BYTES_RECV, 0);
			if (SOCKET_ERROR == bytes_read) {
				int error_code = WSAGetLastError();
				if (WSAECONNRESET == error_code) {
					//Somebody disconnected, get his details and print
					printf("Host disconnected unexpectedly , ip %s , port %d \n",
						inet_ntoa(clt_addr.sin_addr),
						ntohs(clt_addr.sin_port));
					//close the socket and mark as 0 in list for reuse
					shutdown(clt_sock, SD_SEND);
					closesocket(clt_sock);
					clt_socks[i] = 0;
				}
				else {
					printf("recv failed with error code %d\n", error_code);
				}
			}
			if (0 == bytes_read) {
				//Somebody disconnected
				//close the socket and mark as 0 in list for reuse
//				printf("closing socket %d\n", (int)clt_sock);
				shutdown(clt_sock, SD_SEND);
				closesocket(clt_sock);
				clt_socks[i] = 0;
			}
			else {
				req_buffer[bytes_read] = '\0';
				char *tok0 = strtok(req_buffer, " ");
				char *tok1 = strtok(NULL, " ");
				// skip non-GET methods
				if (0 != strcmp(tok0, "GET"))
					continue;
				// respond to favicon request
				if (0 == strcmp(tok1, "/favicon.ico")) {
					const char *x = "";
					char out[1024];
					sprintf(out, resp, strlen(x), x);
					int i = send(clt_sock, out, (int)strlen(out), 0);
//					printf("sent favicon; bytes: %d\n", i);
					int j = 0;
					continue;
				}
				if (0 == strcmp(tok1, "/perf")) {
					const char *x = "some data {} []";
					char out[1024 * 10];
					sprintf(out, resp, strlen(_perf_stat_json), "application/json", _perf_stat_json);
					int i = send(clt_sock, out, (int)strlen(out), 0);
//					printf("sent perf; bytes: %d\n", i);
					int j = 0;
				}
				else {
					// unknown request
					const char *x = "unknown request";
					char out[1024 * 10];
					sprintf(out, resp, strlen(x), x);
					int i = send(clt_sock, out, (int)strlen(out), 0);
//					printf("sent unknown; bytes: %d\n", i);
					int j = 0;
				}
			}
		}
	}
	for (int i = 0; i < MAX_CLT_NUM; i++) {
		clt_sock = clt_socks[i];
		if (clt_sock > 0) {
			shutdown(clt_sock, SD_SEND);
			closesocket(clt_sock);
		}
	}
	WSACleanup();
	return 0;
}
