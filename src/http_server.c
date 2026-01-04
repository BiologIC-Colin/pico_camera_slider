/**
 * @file http_server.c
 * @brief Simple HTTP server implementation for WiFi provisioning
 */

#include "http_server.h"
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(http_server, LOG_LEVEL_INF);

/* HTTP server thread stack */
#define HTTP_SERVER_STACK_SIZE 4096
#define HTTP_SERVER_PRIORITY 5

K_THREAD_STACK_DEFINE(http_server_stack, HTTP_SERVER_STACK_SIZE);

/* HTML template for configuration page */
static const char html_header[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n\r\n";

static const char html_page_start[] =
	"<!DOCTYPE html><html><head>"
	"<meta name='viewport' content='width=device-width,initial-scale=1'>"
	"<title>WiFi Setup</title>"
	"<style>"
	"body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0;}"
	"h1{color:#333;}"
	".container{background:white;padding:20px;border-radius:8px;max-width:600px;margin:0 auto;}"
	".network{padding:10px;margin:5px 0;border:1px solid #ddd;border-radius:4px;cursor:pointer;}"
	".network:hover{background:#e8f4f8;}"
	".signal{float:right;color:#666;}"
	"input[type=text],input[type=password]{width:100%;padding:10px;margin:8px 0;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;}"
	"button{background:#4CAF50;color:white;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;}"
	"button:hover{background:#45a049;}"
	".security{color:#666;font-size:0.9em;}"
	"</style></head><body>"
	"<div class='container'>"
	"<h1>WiFi Configuration</h1>"
	"<p>Select a network or enter credentials manually:</p>";

static const char html_form[] =
	"<form method='POST' action='/connect'>"
	"<label>SSID:</label><input type='text' id='ssid' name='ssid' required>"
	"<label>Password:</label><input type='password' name='password'>"
	"<button type='submit'>Connect</button>"
	"</form>";

static const char html_page_end[] =
	"</div>"
	"<script>"
	"function selectNetwork(ssid){document.getElementById('ssid').value=ssid;}"
	"</script>"
	"</body></html>";

static const char html_success[] =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html\r\n"
	"Connection: close\r\n\r\n"
	"<!DOCTYPE html><html><head><title>Success</title></head><body>"
	"<h1>WiFi Configuration Saved</h1>"
	"<p>The device will now attempt to connect to the specified network.</p>"
	"<p>This setup page will close shortly.</p>"
	"</body></html>";

/**
 * @brief Parse HTTP POST data for credentials
 *
 * @param data POST data string
 * @param ssid Output buffer for SSID
 * @param password Output buffer for password
 * @return 0 on success, -1 on failure
 */
static int parse_post_data(const char *data, char *ssid, char *password)
{
	const char *ssid_start, *ssid_end;
	const char *pass_start, *pass_end;

	/* Find SSID */
	ssid_start = strstr(data, "ssid=");
	if (!ssid_start) {
		return -1;
	}
	ssid_start += 5;  /* Skip "ssid=" */

	ssid_end = strchr(ssid_start, '&');
	if (!ssid_end) {
		ssid_end = ssid_start + strlen(ssid_start);
	}

	size_t ssid_len = ssid_end - ssid_start;
	if (ssid_len > 32) {
		ssid_len = 32;
	}
	strncpy(ssid, ssid_start, ssid_len);
	ssid[ssid_len] = '\0';

	/* URL decode SSID (replace + with space, handle %XX) */
	for (size_t i = 0; ssid[i]; i++) {
		if (ssid[i] == '+') {
			ssid[i] = ' ';
		}
	}

	/* Find password */
	pass_start = strstr(data, "password=");
	if (pass_start) {
		pass_start += 9;  /* Skip "password=" */
		pass_end = strchr(pass_start, '&');
		if (!pass_end) {
			pass_end = pass_start + strlen(pass_start);
		}

		size_t pass_len = pass_end - pass_start;
		if (pass_len > 64) {
			pass_len = 64;
		}
		strncpy(password, pass_start, pass_len);
		password[pass_len] = '\0';

		/* URL decode password */
		for (size_t i = 0; password[i]; i++) {
			if (password[i] == '+') {
				password[i] = ' ';
			}
		}
	} else {
		password[0] = '\0';
	}

	return 0;
}

/**
 * @brief Handle HTTP client connection
 *
 * @param server HTTP server context
 * @param client_sock Client socket
 */
static void handle_client(struct http_server *server, int client_sock)
{
	char buffer[1024];
	int ret;

	/* Receive HTTP request */
	ret = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
	if (ret <= 0) {
		close(client_sock);
		return;
	}
	buffer[ret] = '\0';

	LOG_DBG("HTTP request received: %d bytes", ret);

	/* Check if POST request with credentials */
	if (strncmp(buffer, "POST /connect", 13) == 0) {
		char ssid[33] = {0};
		char password[65] = {0};

		/* Find POST data (after \r\n\r\n) */
		const char *post_data = strstr(buffer, "\r\n\r\n");
		if (post_data && parse_post_data(post_data + 4, ssid, password) == 0) {
			LOG_INF("Credentials received: SSID=%s", ssid);

			/* Send success response */
			send(client_sock, html_success, strlen(html_success), 0);

			/* Call credentials callback */
			if (server->creds_cb) {
				server->creds_cb(ssid, password, server->cb_user_data);
			}
		}
	} else {
		/* Send configuration page */
		send(client_sock, html_header, strlen(html_header), 0);
		send(client_sock, html_page_start, strlen(html_page_start), 0);

		/* Add scan results if available */
		if (server->scanner) {
			size_t count;
			const struct wifi_scan_result *results =
				wifi_scanner_get_results(server->scanner, &count);

			if (results && count > 0) {
				char network_html[256];

				send(client_sock, "<h2>Available Networks:</h2>", 28, 0);

				for (size_t i = 0; i < count; i++) {
					int signal_bars = (results[i].rssi + 100) / 15;
					if (signal_bars < 0) signal_bars = 0;
					if (signal_bars > 4) signal_bars = 4;

					snprintf(network_html, sizeof(network_html),
					         "<div class='network' onclick='selectNetwork(\"%s\")'>"
					         "%s <span class='signal'>Signal: %d dBm</span><br>"
					         "<span class='security'>%s</span></div>",
					         results[i].ssid, results[i].ssid, results[i].rssi,
					         wifi_scanner_security_to_string(results[i].security));

					send(client_sock, network_html, strlen(network_html), 0);
				}
			}
		}

		/* Send form and end of page */
		send(client_sock, "<h2>Enter Credentials:</h2>", 28, 0);
		send(client_sock, html_form, strlen(html_form), 0);
		send(client_sock, html_page_end, strlen(html_page_end), 0);
	}

	close(client_sock);
}

/**
 * @brief HTTP server thread function
 *
 * @param arg1 Pointer to http_server context
 * @param arg2 Unused
 * @param arg3 Unused
 */
static void http_server_thread(void *arg1, void *arg2, void *arg3)
{
	struct http_server *server = (struct http_server *)arg1;
	struct sockaddr_in addr;
	int ret;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	LOG_INF("HTTP server thread started");

	/* Create listening socket */
	server->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server->listen_sock < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		printk("ERROR: HTTP server failed to create socket: %d\n", errno);
		server->state = HTTP_SERVER_FAILED;
		return;
	}
	printk("HTTP server: socket created (fd=%d)\n", server->listen_sock);

	/* Bind to port */
	addr.sin_family = AF_INET;
	addr.sin_port = htons(HTTP_SERVER_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	ret = bind(server->listen_sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind socket: %d", errno);
		printk("ERROR: HTTP server failed to bind to port %d: %d\n", HTTP_SERVER_PORT, errno);
		close(server->listen_sock);
		server->state = HTTP_SERVER_FAILED;
		return;
	}
	printk("HTTP server: bound to port %d\n", HTTP_SERVER_PORT);

	/* Listen for connections */
	ret = listen(server->listen_sock, HTTP_SERVER_MAX_CONNECTIONS);
	if (ret < 0) {
		LOG_ERR("Failed to listen: %d", errno);
		printk("ERROR: HTTP server failed to listen: %d\n", errno);
		close(server->listen_sock);
		server->state = HTTP_SERVER_FAILED;
		return;
	}

	server->state = HTTP_SERVER_RUNNING;
	LOG_INF("HTTP server listening on port %d", HTTP_SERVER_PORT);
	printk("HTTP server: listening on port %d (ready for connections)\n", HTTP_SERVER_PORT);

	/* Accept and handle connections */
	while (server->running) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);

		int client_sock = accept(server->listen_sock,
		                          (struct sockaddr *)&client_addr,
		                          &client_addr_len);

		if (client_sock < 0) {
			if (server->running) {
				LOG_ERR("Accept failed: %d", errno);
				printk("HTTP server: accept failed: %d\n", errno);
			}
			break;
		}

		LOG_DBG("Client connected");
		printk("HTTP server: client connected from %d.%d.%d.%d\n",
		       ((uint8_t *)&client_addr.sin_addr.s_addr)[0],
		       ((uint8_t *)&client_addr.sin_addr.s_addr)[1],
		       ((uint8_t *)&client_addr.sin_addr.s_addr)[2],
		       ((uint8_t *)&client_addr.sin_addr.s_addr)[3]);
		handle_client(server, client_sock);
	}

	close(server->listen_sock);
	server->state = HTTP_SERVER_STOPPED;
	LOG_INF("HTTP server thread stopped");
}

int http_server_init(struct http_server *server, struct wifi_scanner *scanner)
{
	if (!server) {
		return -EINVAL;
	}

	memset(server, 0, sizeof(struct http_server));
	server->state = HTTP_SERVER_STOPPED;
	server->scanner = scanner;

	LOG_INF("HTTP server initialized");
	return 0;
}

int http_server_start(struct http_server *server,
                       http_server_creds_cb_t creds_cb,
                       void *user_data)
{
	if (!server) {
		return -EINVAL;
	}

	if (server->state == HTTP_SERVER_RUNNING) {
		LOG_WRN("HTTP server already running");
		return -EALREADY;
	}

	server->creds_cb = creds_cb;
	server->cb_user_data = user_data;
	server->running = true;
	server->state = HTTP_SERVER_STARTING;

	/* Create server thread */
	server->server_tid = k_thread_create(
		&server->server_thread,
		http_server_stack,
		HTTP_SERVER_STACK_SIZE,
		http_server_thread,
		server, NULL, NULL,
		HTTP_SERVER_PRIORITY,
		0, K_NO_WAIT);

	if (!server->server_tid) {
		LOG_ERR("Failed to create server thread");
		server->state = HTTP_SERVER_FAILED;
		return -ENOMEM;
	}

	k_thread_name_set(&server->server_thread, "http_server");

	LOG_INF("HTTP server starting...");
	return 0;
}

int http_server_stop(struct http_server *server)
{
	if (!server) {
		return -EINVAL;
	}

	if (server->state != HTTP_SERVER_RUNNING) {
		return -EALREADY;
	}

	LOG_INF("Stopping HTTP server...");

	server->running = false;

	/* Close socket to unblock accept() */
	if (server->listen_sock >= 0) {
		close(server->listen_sock);
	}

	/* Wait for thread to finish */
	k_thread_join(&server->server_thread, K_SECONDS(5));

	return 0;
}

enum http_server_state http_server_get_state(struct http_server *server)
{
	if (!server) {
		return HTTP_SERVER_STOPPED;
	}

	return server->state;
}
