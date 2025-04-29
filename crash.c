#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXLINE 1024

typedef struct {
    pid_t pid;
    int job_id;
    char command[256];
    bool ended;
    bool suspended;
} job_t;

typedef struct {
    int  jobs_count;
    int  next_job_id;
    job_t jobs_32[32];
    int  active_jobs;
} info_jobs_t;

/* ------------------------------
   Globals
   ------------------------------ */
info_jobs_t global_jobs;
int  count;
pid_t fg_pid = 0; // Track which job is currently in the foreground (0 => none)

/* ------------------------------------------------------------------
   Helper: write message directly to fd
   ------------------------------------------------------------------ */
void sig_printf(int fd, const char *msg) {
    write(fd, msg, strlen(msg));
}

/* ------------------------------------------------------------------
   job_eliminate:
   Removes the job at array index jobid by shifting subsequent entries up.
   NOTE: This "jobid" is actually an array index, not the shell's job number.
   ------------------------------------------------------------------ */
void job_eliminate(int jobid) {
    if (jobid < 0 || jobid >= global_jobs.jobs_count) return;
        
    for (int i = jobid; i < global_jobs.jobs_count - 1; i++) {
        global_jobs.jobs_32[i] = global_jobs.jobs_32[i + 1];
    }
    global_jobs.jobs_count--;
    global_jobs.active_jobs--;
}

/* ------------------------------------------------------------------
   SIGCHLD handler:
   - Reap children that changed state (terminated, stopped, continued).
   - Print messages for finished/killed/suspended/continued jobs.
   ------------------------------------------------------------------ */
void sigchld_handler(int sig) {
    (void)sig;

    pid_t pid;
    int status;

    /* Reap as many changed children as possible in a loop */
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {

        /* Find which job_t corresponds to this pid */
        int i;
        for (i = 0; i < global_jobs.jobs_count; i++) {
            if (global_jobs.jobs_32[i].pid == pid) {
                break;
            }
        }
        if (i >= global_jobs.jobs_count) {
            // Not found => ignore
            continue;
        }

        job_t *j = &global_jobs.jobs_32[i];

        /* Prepare prefix: "[job_id] (pid)  " */
        char header[128];
        snprintf(header, sizeof(header), "[%d] (%d)  ",
                 j->job_id, j->pid);
        sig_printf(STDOUT_FILENO, header);

        /* Check status cases */
        if (WIFSIGNALED(status)) {
            // killed by signal
            sig_printf(STDOUT_FILENO, "killed");
            if (WCOREDUMP(status)) {
                sig_printf(STDOUT_FILENO, " (core dumped)");
            }
            sig_printf(STDOUT_FILENO, "  ");
            sig_printf(STDOUT_FILENO, j->command);
            sig_printf(STDOUT_FILENO, "\n");
            j->ended = true;
            job_eliminate(i);
        }
        else if (WIFSTOPPED(status)) {
            // suspended
            sig_printf(STDOUT_FILENO, "suspended  ");
            sig_printf(STDOUT_FILENO, j->command);
            sig_printf(STDOUT_FILENO, "\n");
            j->suspended = true;
            // if this job was in fg, then there's no longer a foreground job
            if (pid == fg_pid) {
                fg_pid = 0;
            }
        }
        else if (WIFCONTINUED(status)) {
            // continued
            sig_printf(STDOUT_FILENO, "continued  ");
            sig_printf(STDOUT_FILENO, j->command);
            sig_printf(STDOUT_FILENO, "\n");
            j->suspended = false; // it's running now
        } else if (WIFEXITED(status)) {
            // finished
            sig_printf(STDOUT_FILENO, "finished  ");
            sig_printf(STDOUT_FILENO, j->command);
            sig_printf(STDOUT_FILENO, "\n");
            j->ended = true;
            // remove from array
            job_eliminate(i);
        }
    }

}

/* ------------------------------------------------------------------
   SIGINT handler:
   If there's a foreground job (fg_pid > 0), forward SIGINT to it.
   Otherwise do nothing.
   ------------------------------------------------------------------ */
void sigint_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGINT);
    }
}

/* ------------------------------------------------------------------
   SIGQUIT handler:
   If there's a foreground job, forward SIGQUIT to it.
   If no foreground job, exit the shell.
   ------------------------------------------------------------------ */
void sigquit_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGQUIT);
    } else {
        // no fg job => exit
        _exit(0);
    }
}

/* ------------------------------------------------------------------
   SIGTSTP handler:
   If there's a foreground job, forward SIGTSTP to it (suspend).
   Otherwise do nothing.
   ------------------------------------------------------------------ */
void sigtstp_handler(int sig) {
    (void)sig;
    if (fg_pid > 0) {
        kill(fg_pid, SIGTSTP);
    }
}

/* ------------------------------------------------------------------
   jobs_command: list all known jobs that haven't ended
   ------------------------------------------------------------------ */
void jobs_command(void) {
    for (int i = 0; i < global_jobs.jobs_count; i++) {
        if (!global_jobs.jobs_32[i].ended) {
            printf("[%d] (%d)  %s  %s\n", 
                   global_jobs.jobs_32[i].job_id, 
                   global_jobs.jobs_32[i].pid,
                   (global_jobs.jobs_32[i].suspended ? "suspended" : "running"),
                   global_jobs.jobs_32[i].command);
        }
    }
}

/* ------------------------------------------------------------------
   nuke_command: kill with SIGKILL

   usage: 
      "nuke"            -> kill all jobs
      "nuke %<jobid>"   -> kill job by job_id
      "nuke <pid>"      -> kill job by PID
   ------------------------------------------------------------------ */
void nuke_command(const char **toks) {
    if (*toks == NULL) {
        // no arguments => kill all
        for (int i = 0; i < global_jobs.jobs_count; i++) {
            if (!global_jobs.jobs_32[i].ended) {
                kill(global_jobs.jobs_32[i].pid, SIGKILL);
            }
        }
        return;
    }

    for (int i = 0; toks[i] != NULL; i++) {
        char *endptr;
        if (toks[i][0] == '%') {
            // job id
            int job_id = strtol(toks[i] + 1, &endptr, 10);
            if (*endptr != '\0' || job_id <= 0) {
                fprintf(stderr, "ERROR: bad argument for nuke: %s\n", toks[i]);
                continue;
            }

            bool found = false;
            for (int j = 0; j < global_jobs.jobs_count; j++) {
                if (global_jobs.jobs_32[j].job_id == job_id && 
                    !global_jobs.jobs_32[j].ended)
                {
                    kill(global_jobs.jobs_32[j].pid, SIGKILL);
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "ERROR: no job %d\n", job_id);
            }
        }
        else {
            // a PID
            int pid = strtol(toks[i], &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "ERROR: bad argument for nuke: %s\n", toks[i]);
                continue;
            }
            bool found = false;
            for (int j = 0; j < global_jobs.jobs_count; j++) {
                if (global_jobs.jobs_32[j].pid == pid && 
                    !global_jobs.jobs_32[j].ended)
                {
                    kill(pid, SIGKILL);
                    found = true;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "ERROR: no PID %d\n", pid);
            }
        }
    }
}

/* ------------------------------------------------------------------
   fg_command:
     fg %<jobid> or fg <pid>
   - Bring that job into the foreground (if suspended -> send SIGCONT).
   - Then block until it finishes or suspends.
   ------------------------------------------------------------------ */
void fg_command(const char **toks) {
    if (toks[1] == NULL || toks[2] != NULL) {
        fprintf(stderr, "ERROR: fg needs exactly one argument\n");
        return;
    }

    pid_t the_pid = 0;
    int job_index = -1;

    if (toks[1][0] == '%') {
        // parse job id
        char *endptr;
        int job_id = strtol(toks[1] + 1, &endptr, 10);
        if (*endptr != '\0' || job_id <= 0) {
            fprintf(stderr, "ERROR: bad argument for fg: %s\n", toks[1]);
            return;
        }
        // find the job
        for (int i = 0; i < global_jobs.jobs_count; i++) {
            if (global_jobs.jobs_32[i].job_id == job_id && 
                !global_jobs.jobs_32[i].ended)
            {
                the_pid = global_jobs.jobs_32[i].pid;
                job_index = i;
                break;
            }
        }
        if (job_index < 0) {
            fprintf(stderr, "ERROR: no job %d\n", job_id);
            return;
        }
    } else {
        // parse pid
        char *endptr;
        long pid_val = strtol(toks[1], &endptr, 10);
        if (*endptr != '\0') {
            fprintf(stderr, "ERROR: bad argument for fg: %s\n", toks[1]);
            return;
        }
        for (int i = 0; i < global_jobs.jobs_count; i++) {
            if (global_jobs.jobs_32[i].pid == (pid_t)pid_val && 
                !global_jobs.jobs_32[i].ended)
            {
                the_pid = (pid_t)pid_val;
                job_index = i;
                break;
            }
        }
        if (job_index < 0) {
            fprintf(stderr, "ERROR: no PID %ld\n", pid_val);
            return;
        }
    }

    // If job is suspended, send SIGCONT
    if (global_jobs.jobs_32[job_index].suspended) {
        kill(the_pid, SIGCONT);
        global_jobs.jobs_32[job_index].suspended = false;
    }

    // Now treat it as foreground
    fg_pid = the_pid;

    // Wait for it to finish or stop
    // We'll do a blocking wait until it changes state.  The SIGCHLD
    // handler will call waitpid(WNOHANG) though, so we can do a loop:
    while (1) {
        // If the job ended or suspended, break
        if (global_jobs.jobs_32[job_index].ended ||
            global_jobs.jobs_32[job_index].suspended)
        {
            break;
        }

        // If the job was removed or no longer in array, break
        bool still_exists = false;
        for (int i = 0; i < global_jobs.jobs_count; i++) {
            if (global_jobs.jobs_32[i].pid == the_pid) {
                still_exists = true;
                break;
            }
        }
        if (!still_exists) {
            // job was removed from array => it finished
            break;
        }

        usleep(1000); // 1ms
    }

    fg_pid = 0;  // no longer in foreground
}

/* ------------------------------------------------------------------
   bg_command:
     bg %<jobid>
   - Resume a suspended job in the background (send SIGCONT).
   - Print a message "[job_id] (pid)  running  command".
   ------------------------------------------------------------------ */
void bg_command(const char **toks) {
    // This code currently only handles EXACTLY ONE argument (like "bg %7")
    // If you want multiple arguments, you'd parse them in a loop.
    if (toks[1] == NULL) {
        fprintf(stderr, "ERROR: bg needs some arguments\n");
        return;
    }
    char *endptr;
    int job_id = strtol(toks[1] + 1, &endptr, 10);
    if (toks[1][0] != '%') {
        fprintf(stderr, "ERROR: no job %d\n", job_id);
        return;
    }

    // parse jobid
    
    if (*endptr != '\0' || job_id <= 0) {
        fprintf(stderr, "ERROR: bad argument for bg: %s\n", toks[1]);
        return;
    }

    int jidx = -1;
    for (int i = 0; i < global_jobs.jobs_count; i++) {
        if (global_jobs.jobs_32[i].job_id == job_id &&
            global_jobs.jobs_32[i].suspended && 
            !global_jobs.jobs_32[i].ended)
        {
            jidx = i;
            break;
        }
    }
    if (jidx < 0) {
        fprintf(stderr, "ERROR: no job %d\n", job_id);
        return;
    }

    pid_t pid = global_jobs.jobs_32[jidx].pid;

    kill(pid, SIGCONT);
    global_jobs.jobs_32[jidx].suspended = false;

    printf("[%d] (%d)  running  %s\n",
           global_jobs.jobs_32[jidx].job_id,
           pid,
           global_jobs.jobs_32[jidx].command);
}

/* ------------------------------------------------------------------
   eval: check built-ins or external command
   ------------------------------------------------------------------ */
void eval(const char **toks, bool bg) {
    if (!toks[0]) return; // empty command

    // 1) "quit"
    if (strcmp(toks[0], "quit") == 0) {
        if (toks[1]) {
            fprintf(stderr, "ERROR: quit takes no arguments\n");
        } else {
            exit(0);
        }
        return;
    }
    // 2) "jobs"
    if (strcmp(toks[0], "jobs") == 0) {
        if (toks[1]) {
            fprintf(stderr, "ERROR: jobs takes no arguments\n");
        } else {
            jobs_command();
        }
        return;
    }
    // 3) "nuke"
    if (strcmp(toks[0], "nuke") == 0) {
        nuke_command(toks + 1);
        return;
    }
    // 4) "fg"
    if (strcmp(toks[0], "fg") == 0) {
        fg_command(toks);
        return;
    }
    // 5) "bg"
    if (strcmp(toks[0], "bg") == 0) {
        bg_command(toks);
        return;
    }

    // 6) External command
    // Check if we can add another job
    int active_count = 0;
    for (int i = 0; i < global_jobs.jobs_count; i++) {
        if (!global_jobs.jobs_32[i].ended) {
            active_count++;
        }
    }
    if (active_count >= 32) {
        fprintf(stderr, "ERROR: too many jobs\n");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "ERROR: cannot run %s\n", toks[0]);
        return;
    } 
    else if (pid == 0) {
        // child
        // put child in its own process group
        setpgid(0, 0);
        execvp(toks[0], (char *const *)toks);
        // If execvp fails:
        fprintf(stderr, "ERROR: cannot run %s\n", toks[0]);
        _exit(1);
    }
    else {
        // parent
        // record the new job
        job_t *j = &global_jobs.jobs_32[global_jobs.jobs_count];
        j->pid       = pid;
        j->job_id    = global_jobs.next_job_id++;
        j->ended     = false;
        j->suspended = false;
        strncpy(j->command, toks[0], sizeof(j->command) - 1);
        j->command[sizeof(j->command) - 1] = '\0';

        global_jobs.jobs_count++;
        global_jobs.active_jobs++;

        if (bg) {
            // background => print "running" message
            printf("[%d] (%d)  running  %s\n", j->job_id, (int)pid, j->command);
        }
        else {
            // foreground => set fg_pid
            fg_pid = pid;
        }

        if (!bg) {
            // Wait for it to finish or stop
            // We'll do a small loop, and rely on SIGCHLD to actually handle it
            while (1) {
                // see if ended or suspended
                if (j->ended || j->suspended) {
                    break;
                }
                // or removed from array
                bool still_exists = false;
                for (int i = 0; i < global_jobs.jobs_count; i++) {
                    if (global_jobs.jobs_32[i].pid == pid) {
                        still_exists = true;
                        break;
                    }
                }
                if (!still_exists) {
                    // job was removed => must have ended
                    break;
                }
                usleep(1000);
            }
            fg_pid = 0;
        }
    }
}

/* ------------------------------------------------------------------
   parse_and_eval:
   - parse the line into commands separated by ';' or '&'
   - if ends with '&', background, else foreground
   ------------------------------------------------------------------ */
void parse_and_eval(char *s) {
    const char *toks[MAXLINE+1];

    while (*s != '\0') {
        bool end = false;
        bool bg  = false;
        int  t   = 0;

        // Skip leading whitespace
        while (*s == ' ' || *s == '\t' || *s == '\n') {
            *s++ = '\0';
        }
        if (*s == '\0') break;

        while (*s != '\0' && !end) {
            // skip whitespace
            while (*s == ' ' || *s == '\t' || *s == '\n') {
                *s++ = '\0';
            }
            if (*s == '\0') break;

            if (*s == '&') {
                bg = true;
                *s++ = '\0';
                end = true;
            }
            else if (*s == ';') {
                *s++ = '\0';
                end = true;
            }
            else {
                toks[t++] = s;
                // consume until whitespace or delimiter
                while (*s != '\0' && *s != '&' && *s != ';'
                       && *s != ' ' && *s != '\t' && *s != '\n') {
                    s++;
                }
            }
        }
        toks[t] = NULL;

        if (t > 0) {
            eval(toks, bg);
        }
    }
}

/* ------------------------------------------------------------------
   prompt: prints "crash> "
   ------------------------------------------------------------------ */
void prompt() {
    const char *p = "crash> ";
    write(STDOUT_FILENO, p, strlen(p));
}

/* ------------------------------------------------------------------
   repl: read lines until EOF or error
   ------------------------------------------------------------------ */
int repl() {
    char *buf = NULL;
    size_t len = 0;

    while (true) {
        prompt();
        ssize_t nr = getline(&buf, &len, stdin);
        if (nr == -1) {
            // EOF or error
            // If no fg job, exit. Else ignore
            if (fg_pid <= 0) {
                break;
            } else {
                continue;
            }
        }
        parse_and_eval(buf);
    }

    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; 
    (void)argv;

    global_jobs.jobs_count = 0;
    global_jobs.active_jobs = 0;
    global_jobs.next_job_id = 1;
    count = 0;
    fg_pid = 0;

    // Install signal handlers
    signal(SIGCHLD, sigchld_handler);
    signal(SIGINT,  sigint_handler);
    signal(SIGQUIT, sigquit_handler);
    signal(SIGTSTP, sigtstp_handler);

    return repl();
}