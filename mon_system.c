#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

void sigQuit(int code) { fprintf(stderr, ">>> SIGTERM received [%d]\n", code); }

int mon_system(const char *commande) {
  int wstatus;
  pid_t pid;
  int rwait;

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, SIG_IGN);

  if (commande == NULL)
    return 1;

  pid = fork();
  switch (pid) {
  case -1: // failed to fork
    return 32512;

  case 0: // child process
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    execl("/bin/bash", "bash", "-c", commande, NULL);
    /* if execl() has failed */
    exit(127);

  default: // parent process
    /* the parent process calls wait on the child */
    rwait = waitpid(pid, &wstatus, 0);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    if (WIFSIGNALED(wstatus)) {
      // child has been signaled
      fprintf(stderr, "%i WIFSIGNALED:%i - ", rwait, WTERMSIG(wstatus));
      switch (WTERMSIG(wstatus)) {
      case SIGSEGV:
        return 32512;
      case SIGHUP:
        return 1;
      case SIGINT:
        return 2;
      case SIGQUIT:
        return 3;
      default:
        return 0;
      }
    }

    if (WIFSTOPPED(wstatus)) {
      // child has been stopped
      fprintf(stderr, "%i WIFSTOPPED:%i - ", rwait, WSTOPSIG(wstatus));
    }

    if (WIFEXITED(wstatus)) {
      // child has excited
      fprintf(stderr, "%i WIFEXITED:%i - ", rwait, WEXITSTATUS(wstatus));
      switch (WEXITSTATUS(wstatus)) {
      case 127: // unknown command
        return 32512;
      case 1:
        return 256;
      case 3:
        return 3;
      }
    }
    return 0;
  }
}

void verification_system(const char *commande) {
  int a = system(commande);
  int b = mon_system(commande);
  fprintf(stderr, "%s: \"%s\" (%d, %d)\n", a == b ? "OK  " : "FAIL",
          commande ? commande : "(null)", a, b);
}

void verification_system_limit(int resource, int valeur, const char *commande) {
  struct rlimit oldrl, newrl;
  if (getrlimit(resource, &oldrl) == -1) {
    perror("getrlimit");
    return;
  }
  newrl = oldrl;
  newrl.rlim_cur = valeur;
  if (setrlimit(resource, &newrl) == -1) {
    perror("setrlimit");
    return;
  }
  verification_system(commande);
  setrlimit(resource, &oldrl);
}

int main(int argc, char *argv[]) {
  const struct rlimit zerorl = {0, 0};
  if (setrlimit(RLIMIT_CORE, &zerorl) == -1)
    perror("setrlimit(RLIMIT_CORE)");

  if (argc >= 2) {
    for (int i = 1; i < argc; i++)
      verification_system(argv[i]);
  } else {
    const char *cmds[] = {
        "",                       /* empty command */
        "true",                   /* successful command */
        "false",                  /* failing command */
        "ls / > /dev/null",       /* another command */
        "exec 2>/dev/null; plop", /* non-existent command */
        "kill -HUP $$",           /* killed by SIGHUP */
        "kill -INT $$",           /* killed by SIGINT */
        "kill -QUIT $$",          /* killed by SIGQUIT */
        "kill -INT $PPID",        /* send SIGINT to main process */
        "kill -QUIT $PPID",       /* send SIGQUIT to main process */
        NULL                      /* NULL command */
    };

    for (int i = 0; i == 0 || cmds[i - 1] != NULL; i++) {
      verification_system(cmds[i]);
    }
    verification_system_limit(RLIMIT_NPROC, 0, ": failed fork");
    verification_system_limit(RLIMIT_AS, 0, ": failed exec");
  }
}
