/* xcrun - clone of Apple's xcode xcrun utility
 *
 * Copyright (c) 2013-2017, Brian McKenzie <mckenzba@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *  3. Neither the name of the organization nor the names of its contributors may
 *     be used to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <libgen.h>
#include <limits.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ini.h"

/* General stuff */
#define TOOL_VERSION "1.0.0"
#define SDK_CFG ".xcdev.dat"
#define XCRUN_DEFAULT_CFG "/etc/xcrun.ini"

/* Toolchain configuration struct */
typedef struct {
	const char *name;
	const char *version;
} toolchain_config;

/* SDK configuration struct */
typedef struct {
	const char *name;
	const char *version;
	const char *toolchain;
	const char *default_arch;
	const char *deployment_target;
} sdk_config;

/* xcrun default configuration struct */
typedef struct {
	const char *sdk;
	const char *toolchain;
} default_config;

/* Output mode flags */
static int logging_mode = 0;
static int verbose_mode = 0;
static int finding_mode = 0;

/* Behavior mode flags */
static int explicit_sdk_mode = 0;
static int explicit_toolchain_mode = 0;
static int ios_deployment_target_set = 0;
static int macosx_deployment_target_set = 0;

/* Runtime info */
static char developer_dir[PATH_MAX];
static char current_sdk[PATH_MAX];
static char current_toolchain[PATH_MAX];

/* Alternate behavior flags */
static char *alternate_sdk_path;
static char *alternate_toolchain_path;

/* Ways that this tool may be called */
static const char *multicall_tool_names[4] = {
	"xcrun",
	"xcrun_log",
	"xcrun_verbose",
	"xcrun_nocache"
};

/* Our program's name as called by the user */
static char *progname;

/* helper function to strip file extensions */
static void stripext(char *dst, const char *src)
{
	int len;
	char *s;

	if ((s = strchr(src, '.')) != NULL)
		len = (s - src);
	else
		len = strlen(src);

	strncpy(dst, src, len);
}

/* helper function to test for the authenticity of an sdk */
static int test_sdk_authenticity(const char *path)
{
	int retval = 0;
	char fname[PATH_MAX];

	sprintf(fname, "%s/info.ini", path);
	if (access(fname, F_OK) != (-1))
		retval = 1;

	return retval;
}

/**
 * @func verbose_printf -- Print output to fp in verbose mode.
 * @arg fp  - pointer to file (file, stderr, or stdio)
 * @arg str - string to print
 * @arg ... - additional arguments used
 */
static void verbose_printf(FILE *fp, const char *str, ...)
{
	va_list args;

	if (verbose_mode) {
		va_start(args, str);
		vfprintf(fp, str, args);
		va_end(args);
	}
}

/**
 * @func logging_printf -- Print output to fp in logging mode.
 * @arg fp  - pointer to file (file, stderr, or stdio)
 * @arg str - string to print
 * @arg ... - additional arguments used
 */
static void logging_printf(FILE *fp, const char *str, ...)
{
	va_list args;

	if (logging_mode) {
		va_start(args, str);
		vfprintf(fp, str, args);
		va_end(args);
	}
}

/**
 * @func usage -- Print helpful information about this program.
 */
static int usage(void)
{
	fprintf(stderr,
		"Usage: %s [options] <tool name> ... arguments ...\n"
		"\n"
		"Find and execute the named command line tool from the active developer directory.\n"
		"\n"
		"The active developer directory can be set using `xcode-select`, or via the\n"
		"DEVELOPER_DIR environment variable.\n"
		"\n"
		"Options:\n"
		"  -h, --help                   show this help message and exit\n"
		"  --version                    show the xcrun version\n"
		"  -v, --verbose                show verbose logging output\n"
		"  --sdk <sdk name>             find the tool for the given SDK name\n"
		"  --toolchain <name>           find the tool for the given toolchain\n"
		"  -l, --log                    show commands to be executed (with --run)\n"
		"  -f, --find                   only find and print the tool path\n"
		"  -r, --run                    find and execute the tool (the default behavior)\n"
#if 0
		"  -n, --no-cache               do not use the lookup cache (not implemented yet - does nothing)\n"
		"  -k, --kill-cache             invalidate all existing cache entries (not implemented yet - does nothing)\n"
#endif
		"  --show-sdk-path              show selected SDK install path\n"
		"  --show-sdk-version           show selected SDK version\n"
		"  --show-sdk-target-triple     show selected SDK target triple\n"
		"  --show-sdk-toolchain-path    show selected SDK toolchain path\n"
		"  --show-sdk-toolchain-version show selected SDK toolchain version\n\n"
		, progname);

	return 0;
}

/**
 * @func version -- print out version info for this tool
 */
static int version(void)
{
	fprintf(stdout, "xcrun version %s\n", TOOL_VERSION);

	return 0;
}

/**
 * @func validate_directory_path -- validate if requested directory path exists
 * @arg dir - directory to validate
 * @return: 0 on success, -1 on failure
 */
static int validate_directory_path(const char *dir)
{
	struct stat fstat;

	if (stat(dir, &fstat) != 0) {
		fprintf(stderr, "xcrun: error: unable to validate path \'%s\' (%s)\n", dir, strerror(errno));
		return -1;
	}

	if (S_ISDIR(fstat.st_mode) == 0) {
		fprintf(stderr, "xcrun: error: \'%s\' is not a valid path\n", dir);
		return -1;
	}

	return 0;
}

/**
 * @func toolchain_cfg_handler -- handler used to process toolchain info.ini contents
 * @arg user    - ini user pointer (see ini.h)
 * @arg section - ini section name (see ini.h)
 * @arg name    - ini variable name (see ini.h)
 * @arg value   - ini variable value (see ini.h)
 * @return: 1 on success, 0 on failure
 */
static int toolchain_cfg_handler(void *user, const char *section, const char *name, const char *value)
{
	toolchain_config *config = (toolchain_config *)user;

	if (MATCH_INI_STON("TOOLCHAIN", "name"))
		config->name = strdup(value);
	else if (MATCH_INI_STON("TOOLCHAIN", "version"))
		config->version = strdup(value);
	else
		return 0;

	return 1;
}

/**
 * @func sdk_cfg_handler -- handler used to process sdk info.ini contents
 * @arg user    - ini user pointer (see ini.h)
 * @arg section - ini section name (see ini.h)
 * @arg name    - ini variable name (see ini.h)
 * @arg value   - ini variable value (see ini.h)
 * @return: 1 on success, 0 on failure
 */
static int sdk_cfg_handler(void *user, const char *section, const char *name, const char *value)
{
	sdk_config *config = (sdk_config *)user;

	if (MATCH_INI_STON("SDK", "name"))
		config->name = strdup(value);
	else if (MATCH_INI_STON("SDK", "version"))
		config->version = strdup(value);
	else if (MATCH_INI_STON("SDK", "toolchain"))
		config->toolchain = strdup(value);
	else if (MATCH_INI_STON("SDK", "default_arch"))
		config->default_arch = strdup(value);
	else if (MATCH_INI_STON("SDK", "iphoneos_deployment_target")) {
		ios_deployment_target_set = 1;
		macosx_deployment_target_set = 0;
		config->deployment_target = strdup(value);
	} else if (MATCH_INI_STON("SDK", "macosx_deployment_target")) {
		ios_deployment_target_set = 0;
		macosx_deployment_target_set = 1;
		config->deployment_target = strdup(value);
	} else
		return 0;

	return 1;
}

/**
 * @func default_cfg_handler -- handler used to process xcrun's xcrun.ini contents
 * @arg user    - ini user pointer (see ini.h)
 * @arg section - ini section name (see ini.h)
 * @arg name    - ini variable name (see ini.h)
 * @arg value   - ini variable value (see ini.h)
 * @return: 1 on success, 0 on failure
 */
static int default_cfg_handler(void *user, const char *section, const char *name, const char *value)
{
	default_config *config = (default_config *)user;

	if (MATCH_INI_STON("SDK", "name"))
		config->sdk = strdup(value);
	else if (MATCH_INI_STON("TOOLCHAIN", "name"))
		config->toolchain = strdup(value);
	else
		return 0;

	return 1;
}

/**
 * @func get_toolchain_info -- fetch config info from a toolchain's info.ini
 * @arg path - path to toolchain's info.ini
 * @return: struct containing toolchain config info
 */
static toolchain_config get_toolchain_info(const char *path)
{
	toolchain_config config;
	char info_path[PATH_MAX] = { 0 };

	sprintf(info_path, "%s/info.ini", path);

	if (ini_parse(info_path, toolchain_cfg_handler, &config) != (-1))
		return config;

	fprintf(stderr, "xcrun: error: failed to retrieve toolchain info from '\%s\'. (%s)\n", info_path, strerror(errno));

	exit(1);
}

/**
 * @func get_sdk_info -- fetch config info from a toolchain's info.ini
 * @arg path - path to sdk's info.ini
 * @return: struct containing sdk config info
 */
static sdk_config get_sdk_info(const char *path)
{
	sdk_config config;
	char info_path[PATH_MAX] = { 0 };

	sprintf(info_path, "%s/info.ini", path);

	if (ini_parse(info_path, sdk_cfg_handler, &config) != (-1))
		return config;

	fprintf(stderr, "xcrun: error: failed to retrieve sdk info from '\%s\'. (%s)\n", info_path, strerror(errno));

	exit(1);
}

/**
 * @func get_default_info -- fetch default configuration for xcrun
 * @arg path - path to xcrun.ini
 * @return: struct containing default config info
 */
static default_config get_default_info(const char *path)
{
	default_config config;

	if (ini_parse(path, default_cfg_handler, &config) != (-1))
		return config;

	fprintf(stderr, "xcrun: error: failed to retrieve default info from '\%s\'. (%s)\n", path, strerror(errno));

	exit(1);
}

/**
 * @func get_developer_path -- retrieve current developer path
 * @arg path - buffer to hold the developer dir absolute path
 * @return: number of bytes read.
 */
static int get_developer_path(char *path)
{
	FILE *fp;
	int len = 0;
	char *home_path, *dev_path;
	char cfg_path[PATH_MAX] = { 0 };

	verbose_printf(stdout, "xcrun: info: attempting to retrieve developer path from DEVELOPER_DIR...\n");

	if ((dev_path = getenv("DEVELOPER_DIR")) != NULL) {
		verbose_printf(stdout, "xcrun: info: using developer path \'%s\' from DEVELOPER_DIR.\n", dev_path);
		len = strlen(dev_path);
		strncpy(path, dev_path, len);
		return len;
	}

	verbose_printf(stdout, "xcrun: info: attempting to retrieve developer path from configuration cache...\n");
	if ((home_path = getenv("HOME")) == NULL) {
		fprintf(stderr, "xcrun: error: failed to read HOME variable.\n");
		return len;
	}

	strcat(cfg_path, home_path);
	strcat(cfg_path, "/");
	strcat(cfg_path, SDK_CFG);

	if ((fp = fopen(cfg_path, "r")) != NULL) {
		fseek(fp, 0, SEEK_END);
		int fsize = ftell(fp);
		fseek(fp, SEEK_SET, 0);
		len = fread(path, fsize, 1, fp);
		fclose(fp);
	} else {
		fprintf(stderr, "xcrun: error: unable to read configuration cache. (%s)\n", strerror(errno));
		return len;
	}

	verbose_printf(stdout, "xcrun: info: using developer path \'%s\' from configuration cache.\n", path);

	return len;
}

/**
 * @func get_toolchain_path -- Return the specified toolchain path
 * @arg name - name of the toolchain
 * @return: absolute path of toolchain on success, exit on failure
 */
static char *get_toolchain_path(const char *name)
{
	char *devpath, *path;

	devpath = developer_dir;
	path = (char *)calloc(PATH_MAX, sizeof(char));

	if (devpath == NULL) {
		fprintf(stderr, "xcrun: error: failed to retrieve developer path, do you have it set?\n");
		goto failure;
	}

	sprintf(path, "%s/Toolchains/%s.toolchain", devpath, name);
	if (validate_directory_path(path) != (-1))
		return path;

	fprintf(stderr, "xcrun: error: \'%s\' is not a valid toolchain path.\n", path);

failure:
	free(path);
	exit(1);
}

/**
 * @func get_sdk_path -- Return the specified sdk path
 * @arg name - name of the sdk
 * @return: absolute path of sdk on success, exit on failure
 */
static char *get_sdk_path(const char *name)
{
	char *devpath, *path;

	devpath = developer_dir;
	path = (char *)calloc(PATH_MAX, sizeof(char));

	if (devpath == NULL) {
		fprintf(stderr, "xcrun: error: failed to retrieve developer path, do you have it set?\n");
		goto failure;
	}

	sprintf(path, "%s/SDKs/%s.sdk", devpath, name);
	if (validate_directory_path(path) != (-1))
		return path;

	fprintf(stderr, "xcrun: error: \'%s\' is not a valid sdk path.\n", path);

failure:
	free(path);
	exit(1);
}

/**
 * @func parse_target_triple -- Generate target triple by parsing iOS/MacOSX version and cpu architecture
 * @arg triple - buffer to place the target triple
 * @arg ver    - macOS or iOS version
 * @arg arch   - macOS or iOS cpu architecture
 */
static void parse_target_triple(char *triple, const char *ver, const char *arch)
{
	int where = 1;
	bool is_macosx = false;
	int xx, yy, zz, ch, kern_ver;

	if (ver == NULL)
		return;

	/* For now, assume that any x86 target is macOS. */
	if ((strcmp(arch, "x86_64") == 0) || (strcmp(arch, "i386") == 0))
		is_macosx = true;

	xx = yy = zz = 0;

	do {
		ch = (int)*ver;

		switch (ch) {
			case '9':
			case '8':
			case '7':
			case '6':
			case '5':
			case '4':
			case '3':
			case '2':
			case '1':
			case '0':
				{
					switch (where) {
						case 1: /* major */
							xx *= 10;
							xx += (ch - '0');
							break;
						case 2: /* minor */
							yy *= 10;
							yy += (ch - '0');
							break;
						case 3: /* patch */
							zz *= 10;
							zz += (ch - '0');
						default:
							break;
					}
					break;
				}
			case '.':
			default:
				where++;
				break;
		}
	} while (*ver++ != '\0');

	switch (xx) {
		case 11:
			kern_ver = 17;
			break;
		case 10:
			{
				if (is_macosx)
					kern_ver = (yy + 4);
				else
					kern_ver = 16;
				break;
			}
		case 9:
			kern_ver = 15;
			break;
		case 8:
		case 7:
			kern_ver = 14;
			break;
		case 6:
			kern_ver = 13;
			break;
		case 5:
			kern_ver = 11;
			break;
		case 4:
			{
				if (yy <= 2)
					kern_ver = 10;
				else
					kern_ver = 11;
				break;
			}
		case 3:
			kern_ver = 10;
			break;
		case 2:
			kern_ver = 9;
			break;
		case 1:
		default:
			kern_ver = 9;
			break;
	}

	sprintf(triple, "%s-apple-darwin%d", arch, kern_ver);

	return;
}

/**
 * @func get_target_triple -- get the target triple for the current sdk.
 * @arg current_sdk - specified sdk (ignored if TARGET_TRIPLE env variable is set)
 * @return: target triple string or NULL on error
 */
static char *get_target_triple(const char *current_sdk)
{
	char *triple, *default_arch, *deployment_target;

	if ((triple = getenv("TARGET_TRIPLE")) != NULL)
		return triple;

	triple = (char *)calloc(NAME_MAX, sizeof(char));

	if ((default_arch = strdup(get_sdk_info(get_sdk_path(current_sdk)).default_arch)) == NULL)
		return NULL;

	if ((deployment_target = strdup(get_sdk_info(get_sdk_path(current_sdk)).deployment_target)) == NULL)
		return NULL;

	parse_target_triple(triple, deployment_target, default_arch);

	return triple;
}

/**
 * @func call_command -- Execute new process to replace this one.
 * @arg cmd  - absolute path to the program
 * @arg argc - number of arguments to be passed to new process
 * @arg argv - arguments to be passed to new process
 * @return: -1 on error, otherwise no return
 */
static int call_command(const char *cmd, int argc, char *argv[])
{
	int i;
	char *envp[8] = { NULL };
	char *target_triple, *deployment_target;

	/*
	 * Pass useful variables to the enviroment of the program to be executed.
	 *
	 *  * SDKROOT is used for when programs such as clang need to know the location of the sdk.
	 *
	 *  * PATH is used for when programs such as clang need to call on another program (such as the linker).
	 *
	 *  * HOME is used for recursive calls to xcrun (such as when xcrun calls a script calling xcrun ect).
	 *
	 *  * LD_LIBRARY_PATH is used for when tools needs to access libraries that are specific to the toolchain.
	 *
	 *  * TARGET_TRIPLE is used for clang/clang++ cross compilation when building on a foreign host.
	 *
	 *  * {MACOSX|IPHONEOS}_DEPLOYMENT_TARGET is used for tools like ld that need to set the minimum compatibility
	 *    version number for a linked binary.
	 *
	 *  * DEVELOPER_DIR is used as a performance optimization when making recursive calls to xcrun.
	 */

	envp[0] = (char *)calloc(PATH_MAX, sizeof(char));
	envp[1] = (char *)calloc(PATH_MAX, sizeof(char));
	envp[2] = (char *)calloc(PATH_MAX, sizeof(char));
	envp[3] = (char *)calloc(PATH_MAX, sizeof(char));
	envp[4] = (char *)calloc(NAME_MAX, sizeof(char));
	envp[5] = (char *)calloc(NAME_MAX, sizeof(char));
	envp[6] = (char *)calloc(PATH_MAX, sizeof(char));

	sprintf(envp[0], "SDKROOT=%s", get_sdk_path(current_sdk));
	sprintf(envp[1], "PATH=%s/usr/bin:%s/usr/bin:%s", developer_dir, get_toolchain_path(current_toolchain), getenv("PATH"));
	sprintf(envp[2], "LD_LIBRARY_PATH=%s/usr/lib", get_toolchain_path(current_toolchain));
	sprintf(envp[3], "HOME=%s", getenv("HOME"));
	sprintf(envp[6], "DEVELOPER_DIR=%s", developer_dir);

	if ((target_triple = get_target_triple(current_sdk)) != NULL)
		sprintf(envp[4], "TARGET_TRIPLE=%s", target_triple);
	else
		fprintf(stderr, "xcrun: warning: failed to retrieve target triple information for %s.sdk.\n", current_sdk);

	if ((deployment_target = getenv("IPHONEOS_DEPLOYMENT_TARGET")) != NULL)
		sprintf(envp[5], "IPHONEOS_DEPLOYMENT_TARGET=%s", deployment_target);
	else if ((deployment_target = getenv("MACOSX_DEPLOYMENT_TARGET")) != NULL)
		sprintf(envp[5], "MACOSX_DEPLOYMENT_TARGET=%s", deployment_target);
	else {
		/* Use the deployment target info that is provided by the SDK. */
		if ((deployment_target = strdup(get_sdk_info(get_sdk_path(current_sdk)).deployment_target)) != NULL) {
			if (macosx_deployment_target_set == 1)
				sprintf(envp[5], "MACOSX_DEPLOYMENT_TARGET=%s", deployment_target);
			else if (ios_deployment_target_set == 1)
				sprintf(envp[5], "IPHONEOS_DEPLOYMENT_TARGET=%s", deployment_target);
		} else {
			fprintf(stderr, "xcrun: error: failed to retrieve deployment target information for %s.sdk.\n", current_sdk);
			return -1;
		}
	}

	if (logging_mode == 1) {
		logging_printf(stdout, "xcrun: info: invoking command:\n\t\"%s", cmd);
		for (i = 1; i < argc; i++)
			logging_printf(stdout, " %s", argv[i]);
		logging_printf(stdout, "\"\n");
	}

	return execve(cmd, argv, envp);
}

/**
 * @func search_command -- Search a set of directories for a given command
 * @arg buf  - buffer to hold the absolute path to the command
 * @arg name - command name
 * @arg dirs - set of directories to search, seperated by colons
 * @return: 0 on a successful search, -1 on failure
 */
static int search_command(char *buf, const char *name, char *dirs)
{
	char *cmd_search_path;
	char cmd_absl_path[PATH_MAX] = { 0 };

	/* Search each path entry in dirs until we find our program. */
	cmd_search_path = strtok(dirs, ":");
	while (cmd_search_path != NULL) {
		verbose_printf(stdout, "xcrun: info: checking directory \'%s\' for command \'%s\'...\n", cmd_search_path, name);

		/* Construct our program's absolute path. */
		sprintf(cmd_absl_path, "%s/%s", cmd_search_path, name);

		/* Does it exist? Is it an executable? */
		if (access(cmd_absl_path, (F_OK | X_OK)) == 0) {
			verbose_printf(stdout, "xcrun: info: found command's absolute path: \'%s\'\n", cmd_absl_path);
			strncpy(buf, cmd_absl_path, strlen(cmd_absl_path));
			return 0;
		}

		/* If not, move onto the next entry.. */
		memset(cmd_absl_path, 0, PATH_MAX);
		cmd_search_path = strtok(NULL, ":");
	}

	return -1;
}

/**
 * @func request_command -- Request a program.
 * @arg name - name of program
 * @arg argv - arguments to be passed if program found
 * @return: -1 on failed search, 0 on successful search, no return on execute
 */
static int request_command(const char *name, int argc, char *argv[])
{
	char cmd[PATH_MAX] = { 0 };
	char search_string[PATH_MAX * 256] = { 0 };
	char *sdk_env, *toolch_name, *toolchain_env;

	/*
	 * If xcrun was called in a multicall state, we still want to specify current_sdk for SDKROOT and
	 * current_toolchain for PATH.
	 */
	if (strlen(current_sdk) == 0) {
		if ((sdk_env = getenv("SDKROOT")) != NULL) {
			stripext(current_sdk, basename(sdk_env));
		} else {
			const char *sdk_info = get_default_info(XCRUN_DEFAULT_CFG).sdk;
			strncpy(current_sdk, sdk_info, strlen(sdk_info));
		}
	}

	if (strlen(current_toolchain) == 0) {
		if ((toolchain_env = getenv("TOOLCHAINS")) != NULL) {
			stripext(current_toolchain, basename(toolchain_env));
		} else {
			const char *toolch_info = get_default_info(XCRUN_DEFAULT_CFG).toolchain;
			strncpy(current_toolchain, toolch_info, strlen(toolch_info));
		}
	}

	/* No matter the circumstance, search the developer dir. */
	sprintf(search_string, "%s/usr/bin:", developer_dir);

	/* If we explicitly specified an sdk, search the sdk and it's associated toolchain. */
	if (explicit_sdk_mode == 1) {
		toolch_name = strdup(get_sdk_info(get_sdk_path(current_sdk)).toolchain);
		sprintf((search_string + strlen(search_string)), "%s/usr/bin:%s/usr/bin", get_sdk_path(current_sdk), get_toolchain_path(toolch_name));
		goto do_search;
	}

	/* If we explicitly specified a toolchain, only search the toolchain. */
	if (explicit_toolchain_mode == 1) {
		sprintf((search_string + strlen(search_string)), "%s/usr/bin", get_toolchain_path(current_toolchain));
		goto do_search;
	}

	/* If we explicitly specified an SDK, append it to the search string. */
	if (alternate_sdk_path != NULL) {
		sprintf((search_string + strlen(search_string)), "%s/usr/bin:", alternate_sdk_path);
		/* We also want to append an associated toolchain if this is really an SDK folder. */
		if (test_sdk_authenticity(alternate_sdk_path) == 1) {
			toolch_name = strdup(get_sdk_info(alternate_sdk_path).toolchain);
			sprintf((search_string + strlen(search_string)), "%s/usr/bin", get_toolchain_path(toolch_name));
			/* We now have a toolchain, so skip to search. */
			goto do_search;
		}
	}

	/* If we explicitly specified a toolchain, append it to the search string. */
	if (alternate_toolchain_path != NULL)
		sprintf((search_string + strlen(search_string)), "%s/usr/bin", alternate_toolchain_path);

	/* By default, we search our developer dir, our default sdk, and our default toolchain only. */
	if (explicit_sdk_mode == 0 && explicit_toolchain_mode == 0 && alternate_toolchain_path == NULL && alternate_sdk_path == NULL)
		sprintf((search_string + strlen(search_string)), "%s/usr/bin:%s/usr/bin", get_sdk_path(current_sdk), get_toolchain_path(current_toolchain));

	/* Search each path entry in search_string until we find our program. */
do_search:
	if (search_command(cmd, name, search_string) == 0) {
		if (finding_mode == 1) {
			if (access(cmd, (F_OK | X_OK)) == 0) {
				fprintf(stdout, "%s\n", cmd);
				return 0;
			} else {
				return -1;
			}
		} else {
			if (call_command(cmd, argc, argv) != 0) {
				fprintf(stderr, "xcrun: error: can't exec \'%s\' (%s)\n", cmd, strerror(errno));
				return -1;
			}
		}
	}

	/* We have searched everywhere, but we haven't found our program. State why. */
	fprintf(stderr, "xcrun: error: can't stat \'%s\' (%s)\n", name, strerror(errno));

	return -1;
}

/**
 * @func xcrun_main -- xcrun's main routine
 * @arg argc - number of arguments passed by user
 * @arg argv - array of arguments passed by user
 * @return: 0 (or none) on success, 1 on failure
 */
static int xcrun_main(int argc, char *argv[])
{
	int ch;
	int optindex = 0;
	int argc_offset = 0;
	char *sdk_env, *toolchain_env;
	char *sdk, *toolchain, *tool_called;

	static int help_f, verbose_f, log_f, find_f, run_f, nocache_f, killcache_f, version_f, sdk_f, toolchain_f, ssdkp_f, ssdkv_f, ssdkpp_f, ssdktt_f, ssdkpv_f;
	help_f = verbose_f = log_f = find_f = run_f = nocache_f = killcache_f = version_f = sdk_f = toolchain_f = ssdkp_f = ssdkv_f = ssdkpp_f = ssdktt_f = ssdkpv_f = 0;

	/* Supported options */
	static struct option options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, &version_f, 1 },
		{ "verbose", no_argument, 0, 'v' },
		{ "sdk", required_argument, &sdk_f, 1 },
		{ "toolchain", required_argument, &toolchain_f, 1 },
		{ "log", no_argument, 0, 'l' },
		{ "find", required_argument, 0, 'f' },
		{ "run", required_argument, 0, 'r' },
		{ "no-cache", no_argument, 0, 'n' },
		{ "kill-cache", no_argument, 0, 'k' },
		{ "show-sdk-path", no_argument, &ssdkp_f, 1 },
		{ "show-sdk-version", no_argument, &ssdkv_f, 1 },
		{ "show-sdk-target-triple", no_argument, &ssdktt_f, 1},
		{ "show-sdk-toolchain-path", no_argument, &ssdkpp_f, 1 },
		{ "show-sdk-toolchain-version", no_argument, &ssdkpv_f, 1 },
		{ NULL, 0, 0, 0 }
	};

	/* Print help if nothing is specified */
	if (argc < 2)
		return usage();

	/* Only parse arguments if they are given */
	if (*(*(argv + 1)) == '-') {
		if (strcmp(argv[1], "-") == 0 || strcmp(argv[1], "--") == 0)
			return usage();
		while ((ch = getopt_long_only(argc, argv, "+hvlr:f:nk", options, &optindex)) != (-1)) {
			switch (ch) {
				case 'h':
					help_f = 1;
					break;
				case 'v':
					verbose_f = 1;
					break;
				case 'l':
					log_f = 1;
					break;
				case 'r':
					run_f = 1;
					tool_called = basename(optarg);
					++argc_offset;
					break;
				case 'f':
					find_f = 1;
					tool_called = basename(optarg);
					++argc_offset;
					break;
				case 'n':
					nocache_f = 1;
					break;
				case 'k':
					killcache_f = 1;
					break;
				case 0: /* long-only options */
					switch (optindex) {
						case 1: /* --version */
							break;
						case 3: /* --sdk */
							if (*optarg != '-') {
								++argc_offset;
								sdk = optarg;
								/* we support absolute paths and short names */
								if (*sdk == '/') {
									if (validate_directory_path(sdk) != (-1))
										alternate_sdk_path = sdk;
									else
										return 1;
								} else {
									explicit_sdk_mode = 1;
									stripext(current_sdk, sdk);
								}
							} else {
								fprintf(stderr, "xcrun: error: sdk flag requires an argument.\n");
								return 1;
							}
							break;
						case 4: /* --toolchain */
							if (*optarg != '-') {
								++argc_offset;
								toolchain = optarg;
								/* we support absolute paths and short names */
								if (*toolchain == '/') {
									if (validate_directory_path(toolchain) != (-1))
										alternate_toolchain_path = toolchain;
									else
										return 1;
								} else {
									explicit_toolchain_mode = 1;
									stripext(current_toolchain, toolchain);
								}
							} else {
								fprintf(stderr, "xcrun: error: toolchain flag requires an argument.\n");
								return 1;
							}
							break;
						case 10: /* --show-sdk-path */
							break;
						case 11: /* --show-sdk-version */
							break;
						case 12: /* --snow-sdk-target-triple */
							break;
						case 13: /* --show-sdk-toolchain-path */
							break;
						case 14: /* --show-sdk-toolchain-version */
							break;
					}
					break;
				case '?':
				default:
					help_f = 1;
					break;
			}

			++argc_offset;

			/* We don't want to parse any more arguments after these are set. */
			if (ch == 'f' || ch == 'r')
				break;
		}
	} else { /* We are just executing a program. */
		tool_called = basename(argv[1]);
		++argc_offset;
	}

	/* The last non-option argument may be the command called. */
	if (optind < argc && ((!run_f || !find_f) && tool_called == NULL)) {
		tool_called = basename(argv[optind++]);
		++argc_offset;
	}

	/* Don't continue if we are missing arguments. */
	if ((verbose_f || log_f) && tool_called == NULL) {
		fprintf(stderr, "xcrun: error: specified arguments require -r or -f arguments.\n");
		return 1;
	}

	/* Print help? */
	if (help_f || argc < 2)
		return usage();

	/* Print version? */
	if (version_f)
		return version();

	/* If our SDK and/or Toolchain hasn't been specified, fall back to environment or defaults. */
	if (strlen(current_sdk) == 0) {
		if ((sdk_env = getenv("SDKROOT")) != NULL) {
			stripext(current_sdk, basename(sdk_env));
		} else {
			const char *sdk_info = get_default_info(XCRUN_DEFAULT_CFG).sdk;
			strncpy(current_sdk, sdk_info, strlen(sdk_info));
		}
	}

	if (strlen(current_toolchain) == 0) {
		if ((toolchain_env = getenv("TOOLCHAINS")) != NULL) {
			stripext(current_toolchain, basename(toolchain_env));
		} else {
			const char *toolch_info = get_default_info(XCRUN_DEFAULT_CFG).toolchain;
			strncpy(current_toolchain, toolch_info, strlen(toolch_info));
		}
	}

	/* Show SDK path? */
	if (ssdkp_f) {
		printf("%s\n", get_sdk_path(current_sdk));
		return 0;
	}

	/* Show SDK version? */
	if (ssdkv_f) {
		printf("%s SDK version %s\n", get_sdk_info(get_sdk_path(current_sdk)).name, get_sdk_info(get_sdk_path(current_sdk)).version);
		return 0;
	}

	/* Show SDK toolchain path? */
	if (ssdkpp_f) {
		printf("%s\n", get_toolchain_path(current_toolchain));
		return 0;
	}

	/* Show SDK toolchain version? */
	if (ssdkpv_f) {
		printf("%s SDK Toolchain version %s (%s)\n", get_sdk_info(get_sdk_path(current_sdk)).name, get_toolchain_info(get_toolchain_path(current_toolchain)).version, get_toolchain_info(get_toolchain_path(current_toolchain)).name);
		return 0;
	}

	/* Show SDK target triple ? */
	if (ssdktt_f) {
		printf("%s\n", get_target_triple(current_sdk));
		return 0;
	}

	/* Clear the lookup cache? */
	if (killcache_f)
		fprintf(stderr, "xcrun: warning: --kill-cache not supported.\n");

	/* Don't use the lookup cache? */
	if (nocache_f)
		fprintf(stderr, "xcrun: warning: --no-cache not supported.\n");

	/* Turn on verbose mode? */
	if (verbose_f)
		verbose_mode = 1;

	/* Turn on logging mode? */
	if (log_f)
		logging_mode = 1;

	/* Before we continue, double check if we have a tool to call. */
	if (tool_called == NULL) {
		fprintf(stderr, "xcrun: error: no tool specified.\n");
		return 1;
	}

	/* Search for program? */
	if (find_f) {
		finding_mode = 1;
		if (request_command(tool_called, 0, NULL) == 0) {
			return 0;
		} else {
			fprintf(stderr, "xcrun: error: unable to locate command \'%s\' (%s)\n", tool_called, strerror(errno));
			return 1;
		}
	}

	/* Search and execute program. (default behavior) */
	if (request_command(tool_called, (argc - argc_offset),  (argv += ((argc - argc_offset) - (argc - argc_offset) + (argc_offset)))) != 0) {
		fprintf(stderr, "xcrun: error: failed to execute command \'%s\'. aborting.\n", tool_called);
		return 1;
	}

	return 1;
}

/**
 * @func get_multicall_state -- Return a number that is associated to a given multicall state.
 * @arg cmd        - command that binary is being called
 * @arg state      - char array containing a set of possible "multicall states"
 * @arg state_size - number of elements in state array
 * @return: a number from 1 to state_size (first enrty to last entry found in state array), or -1 if one isn't found
 */
static int get_multicall_state(const char *cmd, const char *state[], int state_size)
{
	int i;

	for (i = 0; i < (state_size - 1); i++) {
		if (strcmp(cmd, state[i]) == 0)
			return (i + 1);
	}

	return -1;
}

int main(int argc, char *argv[])
{
	int call_state;

	/* Strip out any path name that may have been passed into argv[0] */
	progname = basename(argv[0]);

	/* Get our developer dir */
	if (!get_developer_path(developer_dir))
		return 1;

	/* Check if we are being treated as a multi-call binary. */
	call_state = get_multicall_state(progname, multicall_tool_names, 4);

	/* Execute based on the state that we were called in. */
	switch (call_state) {
		case 1: /* xcrun */
			return xcrun_main(argc, argv);
			break;
		case 2: /* xcrun_log */
			logging_mode = 1;
			return xcrun_main(argc, argv);
			break;
		case 3: /* xcrun_verbose */
			verbose_mode = 1;
			return xcrun_main(argc, argv);
			break;
		case 4: /* xcrun_nocache */
			return xcrun_main(argc, argv);
			break;
		case -1:
		default: /* called as tool name */
			/* Locate and execute the command */
			if (request_command(progname, argc, argv) != -1) {
				return 1; /* NOREACH */
			} else {
				fprintf(stderr, "xcrun: error: failed to execute command \'%s\'. aborting.\n", progname);
			}
			break;
	}

	return 1;
}
