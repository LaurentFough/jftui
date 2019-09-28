#include "config.h"


////////// GLOBALS //////////
extern jf_options g_options;
/////////////////////////////


////////// STATIC FUNCTIONS //////////
// Will fill in fields client, device, deviceid and version of the global
// options struct, unless they're already filled in.
static void jf_options_complete_with_defaults(void);
//////////////////////////////////////


////////// JF_OPTIONS //////////
static void jf_options_complete_with_defaults()
{
	if (g_options.client == NULL) {
		assert((g_options.client = strdup(JF_CONFIG_CLIENT_DEFAULT)) != NULL);
	}
	if (g_options.device == NULL) {
		assert((g_options.device = strdup(JF_CONFIG_DEVICE_DEFAULT)) != NULL);
	}
	if (g_options.deviceid[0] == '\0') {
		if (gethostname(g_options.deviceid, JF_CONFIG_DEVICEID_MAX_LEN) == 0) {
			g_options.deviceid[JF_CONFIG_DEVICEID_MAX_LEN] = '\0';
		} else {
			strcpy(g_options.deviceid, JF_CONFIG_DEVICEID_DEFAULT);
		}
	}
	if (g_options.version == NULL) {
		assert((g_options.version = strdup(JF_CONFIG_VERSION_DEFAULT)) != NULL);
	}
}


void jf_options_init(void)
{
	g_options = (jf_options){ 0 };
	// these two must not be overwritten when calling _defaults() again
	// during config file parsing
	g_options.ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
	g_options.check_updates = JF_CONFIG_CHECK_UPDATES_DEFAULT;
	jf_options_complete_with_defaults();
}


void jf_options_clear()
{
	free(g_options.server);
	free(g_options.token);
	free(g_options.userid);
	free(g_options.client);
	free(g_options.device);
	free(g_options.version);
}
////////////////////////////////


////////// CONFIGURATION FILE //////////
// NB return value will need to be free'd
// returns NULL if $HOME not set
char *jf_config_get_default_dir(void)
{
	char *dir;
	if ((dir = getenv("XDG_CONFIG_HOME")) == NULL) {
		if ((dir = getenv("HOME")) != NULL) {
			dir = jf_concat(2, getenv("HOME"), "/.config/jftui");
		}
	} else {
		dir = jf_concat(2, dir, "/jftui");
	}
	return dir;
}


// TODO: allow whitespace
void jf_config_read(const char *config_path)
{
	FILE *config_file;
	char *line;
	size_t line_size = 1024;
	char *value;
	size_t value_len;

	assert(config_path != NULL);

	assert((line = malloc(line_size)) != NULL);

	assert((config_file = fopen(config_path, "r")) != NULL);

	// read from file
	while (getline(&line, &line_size, config_file) != -1) {
		assert(line != NULL);
		if ((value = strchr(line, '=')) == NULL) {
			// the line is malformed; issue a warning and skip it
			fprintf(stderr,
					"Warning: skipping malformed settings file line: %s.\n",
					line);
			continue;
		}
		value += 1; // digest '='
		// figure out which option key it is
		// NB options that start with a prefix of other options must go after those!
		if (JF_CONFIG_KEY_IS("server")) {
			JF_CONFIG_FILL_VALUE(server);
			g_options.server_len = value_len;
		} else if (JF_CONFIG_KEY_IS("token")) {
			JF_CONFIG_FILL_VALUE(token);
		} else if (JF_CONFIG_KEY_IS("userid")) {
			JF_CONFIG_FILL_VALUE(userid);
		} else if (JF_CONFIG_KEY_IS("ssl_verifyhost")) {
			JF_CONFIG_FILL_VALUE_BOOL(ssl_verifyhost);
		} else if (JF_CONFIG_KEY_IS("client")) {
			JF_CONFIG_FILL_VALUE(client);
		} else if (JF_CONFIG_KEY_IS("deviceid")) {
			value_len = strlen(value);
			if (value[value_len - 1] == '\n') value_len--;
			if (value_len > JF_CONFIG_DEVICEID_MAX_LEN - 1) {
				value_len = JF_CONFIG_DEVICEID_MAX_LEN - 1;
			}
			strncpy(g_options.deviceid, value, value_len);
			g_options.deviceid[value_len] = '\0';
		} else if (JF_CONFIG_KEY_IS("device")) {
			JF_CONFIG_FILL_VALUE(device);
		} else if (JF_CONFIG_KEY_IS("version")) {
			JF_CONFIG_FILL_VALUE(version);
		} else if (JF_CONFIG_KEY_IS("check_updates")) {
			JF_CONFIG_FILL_VALUE_BOOL(check_updates);
		} else {
			// option key was not recognized; print a warning and go on
			fprintf(stderr,
					"Warning: unrecognized option key in settings file line: %s.\n",
					line);
		}
	}

	// apply defaults for missing values
	jf_options_complete_with_defaults();

	free(line);
	fclose(config_file);
}


void jf_config_write(const char *config_path)
{
	FILE *config_file;

	if ((config_file = fopen(config_path, "w")) != NULL) {
		JF_CONFIG_WRITE_VALUE(server);
		JF_CONFIG_WRITE_VALUE(token);
		JF_CONFIG_WRITE_VALUE(userid);
		fprintf(config_file, "ssl_verifyhost=%s\n",
				g_options.ssl_verifyhost ? "true" : "false" );
		JF_CONFIG_WRITE_VALUE(client);
		JF_CONFIG_WRITE_VALUE(device);
		JF_CONFIG_WRITE_VALUE(deviceid);
		JF_CONFIG_WRITE_VALUE(version);
		// NB don't write check_updates, we want it set manually

		if (fclose(config_file) != 0) {
			perror("Warning: jf_config_write: fclose");
			fprintf(stderr, "Settings may not have been saved to disk.\n");
		}
	} else {
		perror("Warning: jf_config_write: fopen");
		fprintf(stderr, "Settings could not be saved to disk.\n");
	}
}
////////////////////////////////////////


////////// INTERACTIVE USER CONFIG //////////
void jf_config_ask_user_login()
{
	struct termios old, new;
	char *username, *login_post;
	jf_growing_buffer *password;
	jf_reply *login_reply;
	int c;

	password = jf_growing_buffer_new(128);

	while (true) {
		printf("Please enter your username.\n");
		errno = 0;
		username = jf_menu_linenoise("> ");
		printf("Please enter your password.\n> ");
		tcgetattr(STDIN_FILENO, &old);
		new = old;
		new.c_lflag &= (unsigned int)~ECHO;
		tcsetattr(STDIN_FILENO, TCSANOW, &new);
		while ((c = getchar()) != '\n' && c != EOF) {
			jf_growing_buffer_append(password, &c, 1);
		}
		jf_growing_buffer_append(password, "", 1);
		tcsetattr(STDIN_FILENO, TCSANOW, &old);
		putchar('\n');
		
		login_post = jf_json_generate_login_request(username, password->buf);
		free(username);
		memset(password->buf, 0, password->used);
		jf_growing_buffer_empty(password);
		login_reply = jf_net_request("/emby/Users/authenticatebyname",
				JF_REQUEST_IN_MEMORY, login_post);
		free(login_post);
		if (! JF_REPLY_PTR_HAS_ERROR(login_reply)) break;
		if (JF_REPLY_PTR_ERROR_IS(login_reply, JF_REPLY_ERROR_HTTP_401)) {
			jf_reply_free(login_reply);
			if (! jf_menu_user_ask_yn("Error: invalid login credentials. Would you like to try again?")) {
				exit(EXIT_SUCCESS);
			}
		} else {
			fprintf(stderr,
					"FATAL: could not login: %s.\n",
					jf_reply_error_string(login_reply));
			jf_reply_free(login_reply);
			exit(EXIT_FAILURE);
		}
	}
	jf_json_parse_login_response(login_reply->payload);
	jf_reply_free(login_reply);
	jf_growing_buffer_free(password);
}


void jf_config_ask_user()
{
	// login user input
	printf("Please enter the encoded URL of your Jellyfin server. Example: http://foo%%20bar.baz:8096/jf\n");
	printf("(note: unless specified, ports will be the protocol's defaults, i.e. 80 for HTTP and 443 for HTTPS)\n");
	while (true) {
		g_options.server = jf_menu_linenoise("> ");
		if (jf_net_url_is_valid(g_options.server)) {
			g_options.server_len = g_options.server == NULL ? 0 : strlen(g_options.server);
			break;
		} else {
			fprintf(stderr, "Error: malformed URL. Please try again.\n");
			free(g_options.server);
		}
	}

	// critical network stuff: must be configured before network init
	if (jf_menu_user_ask_yn("Do you need jftui to ignore hostname validation (required e.g. if you're using Jellyfin's built-in SSL certificate)?")) {
		g_options.ssl_verifyhost = false;
	}

	jf_config_ask_user_login();

	printf("Configuration and login successful.\n");
}
/////////////////////////////////////////////
