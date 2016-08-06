/*
** Copyright 2015, Pierre-Hugues "phhusson" Husson
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdint.h>
#include <pwd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/types.h>
#include <selinux/selinux.h>
#include <sys/sendfile.h>
#include <dlfcn.h>

#include "placeholder.h"

static int copy_file(const char* src, const char *dst) {
	int ifd = open(src, O_RDONLY);
	if(ifd == -1)
		return 1;
	unlink(dst);
	int ofd = open(dst, O_WRONLY|O_CREAT, 0666);

	loff_t size = lseek(ifd, 0, SEEK_END);
	lseek(ifd, 0, SEEK_SET);

	sendfile(ofd, ifd, NULL, size);
	close(ifd);
	close(ofd);

	return 0;
}

//This is calle das 1000:1000 system_server
//The only guy who has the rights to write to /data/security/current, and to set selinux.reload_policy
static void setup_selinux() {
	mkdir("/data/security", 0755);
	mkdir("/data/security/current/", 0755);
	//Copy current policy
	copy_file("/seapp_contexts", "/data/security/current/seapp_contexts");
	copy_file("/file_contexts", "/data/security/current/file_contexts");
	//if(copy_file("/sys/fs/selinux/policy", "/data/security/current/sepolicy"))
		//Gloups, we are missing permission to read current policy :s
		copy_file("/sepolicy", "/data/security/current/sepolicy");
	copy_file("/service_contexts", "/data/security/current/service_contexts");
	copy_file("/property_contexts", "/data/security/current/property_contexts");
	copy_file("/selinux_version", "/data/security/current/selinux_version");

	//Change current policy
	setup_policy();

	//Ask init to apply new policy
	void *hdl = dlopen("/system/lib/libcutils.so", RTLD_NOW|RTLD_LOCAL);
	int (*l_property_set)(const char *key, const char *value) = dlsym(hdl, "property_set");
	l_property_set("selinux.reload_policy", "1");
}

int main(int argc, char *argv[], char *envp[]) {
	int pid, status;

	if(geteuid() != 0)
		goto failure;

	pid = fork();
	if(!pid) { /* child process doing SELinux work */
		setuid(1000);
		seteuid(1000);
		setcon("u:r:system_server:s0");
		setup_selinux();
		return 0;
	}

	/* otherwise we've got an error and can do little */
	if(pid>0) {
		//Wait for it...
		status = -1;
		waitpid(pid, &status, 0);
		//TODO: Error checking ?
		if(!fork()) {
			const char *const su_exec="/system/xbin/su";
			char *const su_argv[]={ "su", "--daemon", NULL, };
			setcon("u:r:system_server:s0");
			execve(su_exec, su_argv, envp);
		}
	}


failure:
	//Exec original app_process32
	//Should be useless, because it's current context ?
	//setexeccon("u:r:zygote:s0");
#define STR_HELP(s) #s
#define STR(s) STR_HELP(s)
	execve("/system/bin/app_process"STR(__WORDSIZE)".old", argv, envp);
#undef STR_HELP
#undef STR

	return 1;
}
